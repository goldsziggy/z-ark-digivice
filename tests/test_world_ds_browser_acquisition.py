"""Offline acquisition regressions: synthetic PNGs, fake Chrome and fake time only."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('browser_acquisition', ROOT / 'scripts/download-world-ds-browser.py')
acquisition = importlib.util.module_from_spec(spec)
spec.loader.exec_module(acquisition)

# Only source metadata/control rows from the observed Puttimon page are retained.
# No browser tabs, account labels, ads, cookies or full accessibility capture.
TREE = '''7 text field (settable) Address and search bar, Value: spriters-resource.com/ds_dsi/dgmnworldds/asset/48320/, Placeholder: Ask Google or type a URL
17 HTML content Puttimon - Digimon World DS - DS / DSi - The Spriters Resource
227 link arrow_back
230 link download
237 link arrow_forward
245 row Name Puttimon
256 row Game Digimon World DS
262 row Section Fresh Digimon
273 row Uploaded By Garamonde
279 row Size 11.04 KB (361x136)
284 row Format PNG (image/png)
'''
ENTRY = {'entryKey': 'puttimon', 'formId': 70, 'source': {'listedName': 'Puttimon', 'section': 'Fresh Digimon'}}
INVENTORY = {('Puttimon', 'Fresh Digimon'): ENTRY}
PARTIAL = '0 standard window\n1 text Download complete\n2 button Open file\n'
PERMISSION = '0 standard window\n1 text spriters-resource.com wants to:\nDownload multiple files\n'
FILENAME = 'DS _ DSi - Digimon World DS - Fresh Digimon - Puttimon.png'


def document(tree=TREE):
    return {'ok': True, 'result': {'snapshot': {'treeText': tree}}}


def png(width=2, height=2, label=b'A'):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    pixels = (b'\x00' + b'\x21\x97\x58\xff' * width) * height
    return (acquisition.PNG_SIGNATURE + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'tEXt', b'Original fixture\0' + label) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


class Clock:
    def __init__(self):
        self.seconds = 0
        self.sleeps = []

    def monotonic(self):
        return self.seconds

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.seconds += seconds


class PageTests(unittest.TestCase):
    def test_observed_metadata_and_contributor_variants(self):
        page = acquisition.parse_page(document(), INVENTORY)
        self.assertEqual((page['entryKey'], page['formId'], page['width'], page['height']), ('puttimon', 70, 361, 136))
        self.assertEqual(page['url'], 'https://www.spriters-resource.com/ds_dsi/dgmnworldds/asset/48320/')
        self.assertEqual(page['links'], {'download': 230, 'arrow_back': 227, 'arrow_forward': 237})
        self.assertEqual(page['uploadedBy'], 'Garamonde')
        self.assertIsNone(page['contributors'])
        for tree in [TREE.replace('Uploaded By', 'Contributors'), TREE + '300 row Contributors Example artist\n301 row Credits Original credit\n']:
            with self.subTest(tree=tree):
                result = acquisition.parse_page(document(tree), INVENTORY)
                self.assertIsNotNone(result['contributors'])
        self.assertEqual(acquisition.parse_page(document(TREE.replace('361x136', '361×136')), INVENTORY)['width'], 361)

    def test_nested_ad_title_does_not_replace_outer_asset_identity(self):
        outer = TREE.replace('17 HTML content', '\t\t17 HTML content')
        nested = '\t\t\t304 HTML content SuperScape Continuous Frame Template\n'
        self.assertEqual(acquisition.parse_page(document(outer + nested), INVENTORY)['entryKey'], 'puttimon')
        # A nested matching asset title must not rescue a different outer page.
        wrong = outer.replace('17 HTML content Puttimon', '17 HTML content Kapurimon')
        matching_child = '\t\t\t305 HTML content Puttimon - Digimon World DS - DS / DSi - The Spriters Resource\n'
        with self.assertRaises(acquisition.PageNotReady):
            acquisition.parse_page(document(wrong + matching_child), INVENTORY)
        duplicate_outer = '\t\t306 HTML content Puttimon - Digimon World DS - DS / DSi - The Spriters Resource\n'
        with self.assertRaises(acquisition.PageNotReady):
            acquisition.parse_page(document(outer + nested + duplicate_outer), INVENTORY)

    def test_incomplete_metadata_title_or_link_is_retryable(self):
        for tree in [PARTIAL, TREE.replace('245 row Name Puttimon\n', ''), TREE.replace('Uploaded By', 'Unrecognized label'),
                     TREE.replace('17 HTML content Puttimon', '17 HTML content Kuramon'), TREE.replace('230 link download', '230 button download')]:
            with self.subTest(tree=tree), self.assertRaises(acquisition.PageNotReady):
                acquisition.parse_page(document(tree), INVENTORY)

    def test_wrong_identity_permissions_and_challenges_are_fatal(self):
        cases = [PERMISSION, PARTIAL + '3 dialog Permission required\n', PARTIAL + '3 text Checking your browser\n',
                 PARTIAL + '3 text Verify that you are human\n', TREE.replace('/dgmnworldds/', '/differentgame/'),
                 TREE.replace('Value: spriters-resource.com/', 'Value: spriters-resource.com.invalid/'),
                 TREE.replace('256 row Game Digimon World DS', '256 row Game Digimon World Dawn'),
                 TREE.replace('245 row Name Puttimon', '245 row Name Unknown creature'),
                 TREE.replace('262 row Section Fresh Digimon', '262 row Section Mega Digimon'),
                 TREE.replace('PNG (image/png)', 'JPEG (image/jpeg)'), TREE + '300 row Name Puttimon\n',
                 TREE + '300 link download\n', TREE.replace('361x136', '32769x136')]
        for tree in cases:
            with self.subTest(tree=tree):
                with self.assertRaises(acquisition.StopAcquisition) as caught:
                    acquisition.parse_page(document(tree), INVENTORY)
                self.assertNotIsInstance(caught.exception, acquisition.PageNotReady)

    def test_ready_snapshot_waits_for_matching_title_without_input(self):
        browser = acquisition.Browser('never-executed', 1, INVENTORY)
        clock = Clock()
        with patch.object(browser, 'call', side_effect=[document(PARTIAL), document(TREE.replace('17 HTML content Puttimon', '17 HTML content Kuramon')), document()]) as call, \
             patch.object(acquisition.time, 'monotonic', clock.monotonic), patch.object(acquisition.time, 'sleep', clock.sleep):
            self.assertEqual(browser.ready_snapshot()['entryKey'], 'puttimon')
        self.assertEqual(clock.seconds, 6)
        self.assertEqual([args.args for args in call.call_args_list], [('get-app-state',)] * 3)

    def test_click_uses_newly_observed_control_after_transient_page(self):
        browser = acquisition.Browser('never-executed', 1, INVENTORY)
        page = acquisition.parse_page(document(), INVENTORY)
        clock = Clock()
        fresh = document(TREE.replace('230 link download', '901 link download'))
        with patch.object(browser, 'call', side_effect=[document(PARTIAL), fresh, {'ok': True}]) as call, \
             patch.object(acquisition.time, 'monotonic', clock.monotonic), patch.object(acquisition.time, 'sleep', clock.sleep):
            self.assertTrue(browser.click(page, 'download'))
        self.assertEqual([args.args for args in call.call_args_list], [
            ('get-app-state',), ('get-app-state',), ('click', '--element-index', '901')])
        self.assertEqual(clock.seconds, 3)

    def test_ready_snapshot_deadline_and_permission_do_not_click(self):
        for tree, expected_wait in [(PARTIAL, 45), (PERMISSION, 0)]:
            with self.subTest(tree=tree):
                browser = acquisition.Browser('never-executed', 1, INVENTORY)
                clock = Clock()
                with patch.object(browser, 'call', return_value=document(tree)) as call, \
                     patch.object(acquisition.time, 'monotonic', clock.monotonic), patch.object(acquisition.time, 'sleep', clock.sleep), \
                     self.assertRaises(acquisition.StopAcquisition):
                    browser.ready_snapshot()
                self.assertEqual(clock.seconds, expected_wait)
                self.assertTrue(all(args.args == ('get-app-state',) for args in call.call_args_list))
                self.assertLessEqual(call.call_count, 16)


class PngTests(unittest.TestCase):
    def test_crc_dimensions_size_and_complete_ending(self):
        data = png()
        page = {'width': 2, 'height': 2, 'sizeUnit': 'B', 'sizeValue': str(len(data))}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'original.png'
            path.write_bytes(data)
            info, returned = acquisition.png_info(path, page)
            self.assertEqual(returned, data)
            self.assertEqual(info['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(info['bytes'], len(data))
            corrupt = bytearray(data); corrupt[20] ^= 1
            path.write_bytes(corrupt)
            with self.assertRaisesRegex(acquisition.StopAcquisition, 'checksum'):
                acquisition.png_info(path, page)
            path.write_bytes(png(3, 2))
            with self.assertRaisesRegex(acquisition.StopAcquisition, 'dimensions'):
                acquisition.png_info(path, page)
            path.write_bytes(data)
            with self.assertRaisesRegex(acquisition.StopAcquisition, 'byte count'):
                acquisition.png_info(path, {**page, 'sizeValue': str(len(data) + 10)})
            path.write_bytes(data[:-12])
            with self.assertRaises(acquisition.PageNotReady):
                acquisition.png_info(path, page)
            link = Path(tmp) / 'symlink.png'; link.symlink_to(path)
            with self.assertRaisesRegex(acquisition.StopAcquisition, 'regular file'):
                acquisition.png_info(link, page)

    def test_only_exact_browser_filenames_match(self):
        page = acquisition.parse_page(document(), INVENTORY)
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            names = [FILENAME, FILENAME[:-4] + ' (1).png', FILENAME + '.crdownload', FILENAME[:-4] + ' copy.png', 'unrelated.png']
            for name in names:
                (folder / name).write_bytes(b'untouched')
            self.assertEqual({p.name for p in acquisition.matching_downloads(folder, page)}, set(names[:2]))


class PollingTests(unittest.TestCase):
    def scenario(self, poll_trees=(), initial=(), fresh=(), existing=(), private=None, entry=ENTRY, prior=None):
        """Execute the real run loop entirely within a new temporary filesystem."""
        data = png()
        tree = TREE.replace('11.04 KB (361x136)', f'{len(data)} B (2x2)')
        tree = tree.replace('Puttimon', entry['source']['listedName']).replace('Fresh Digimon', entry['source']['section'])
        filename = f"DS _ DSi - Digimon World DS - {entry['source']['section']} - {entry['source']['listedName']}.png"
        snapshots = [*initial, tree, *fresh, tree, *poll_trees]
        clock = Clock(); calls = []; cursor = 0
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); downloads = root / 'downloads'; downloads.mkdir()
            (downloads / 'unrelated.txt').write_bytes(b'untouched')
            catalog = root / 'data/world-ds-catalog.json'; catalog.parent.mkdir()
            entries = [entry] + [{'entryKey': f'synthetic-{i}', 'formId': i + 1000,
                       'source': {'listedName': f'Synthetic {i}', 'section': 'Synthetic'}} for i in range(254)]
            catalog.write_text(json.dumps({'entries': entries}))
            for index, content in enumerate(existing):
                (downloads / (filename if index == 0 else filename[:-4] + f' ({index}).png')).write_bytes(content)
            source = root / '.personal-assets/world-ds/source'
            destination = source / f"{entry['entryKey']}.png"
            if private is not None:
                source.mkdir(parents=True); destination.write_bytes(private)
            prior_path = root / '.personal-assets/source' / f"{entry['entryKey']}.png"
            if prior is not None:
                prior_path.parent.mkdir(parents=True); prior_path.write_bytes(prior)

            def fake_call(_browser, operation, *arguments):
                nonlocal cursor
                calls.append((operation, arguments))
                if operation == 'get-app-state':
                    chosen = snapshots[min(cursor, len(snapshots) - 1)]; cursor += 1
                    return document(chosen)
                self.assertEqual(operation, 'click')
                (downloads / filename).write_bytes(data)
                return {'ok': True}

            args = SimpleNamespace(orca='never-executed', window_id=1, downloads=downloads,
                                   max_sheets=1, max_visits=1, direction='forward', pause=3)
            with patch.object(acquisition, 'ROOT', root), patch.object(acquisition.Browser, 'call', fake_call), \
                 patch.object(acquisition.time, 'monotonic', clock.monotonic), patch.object(acquisition.time, 'sleep', clock.sleep), \
                 patch.object(acquisition, 'progress'), patch.object(acquisition.subprocess, 'run', side_effect=AssertionError('External process forbidden')):
                result = acquisition.run(args)
            manifest = json.loads((source / 'acquisition.json').read_text())
            copied = destination.read_bytes() if destination.exists() else None
            if prior is not None:
                self.assertEqual(prior_path.read_bytes(), prior, 'Reviewed source must remain unchanged')
            originals = {path.name: path.read_bytes() for path in downloads.iterdir()}
        self.assertEqual(originals['unrelated.txt'], b'untouched')
        return result, manifest, copied, originals, calls, clock

    def test_partial_download_notification_waits_once_without_reclick(self):
        result, manifest, copied, originals, calls, clock = self.scenario(poll_trees=[PARTIAL, TREE.replace('11.04 KB (361x136)', f'{len(png())} B (2x2)')])
        self.assertEqual(result, 0)
        self.assertEqual(copied, originals[FILENAME])
        self.assertEqual(manifest['lastRun']['acquired'], 1)
        self.assertFalse(manifest['entries']['puttimon']['integrated'])
        self.assertEqual(sum(op == 'click' for op, _ in calls), 1)
        self.assertEqual(clock.seconds, 6)

    def test_initial_and_preclick_incomplete_pages_wait_without_blind_input(self):
        result, _, copied, _, calls, clock = self.scenario(initial=[PARTIAL], fresh=[TREE.replace('17 HTML content Puttimon', '17 HTML content Kuramon')])
        self.assertEqual(result, 0)
        self.assertEqual(copied, png())
        self.assertEqual(sum(op == 'click' for op, _ in calls), 1)
        self.assertEqual(clock.seconds, 9)

    def test_poll_deadline_never_reclicks(self):
        result, manifest, copied, _, calls, clock = self.scenario(poll_trees=[PARTIAL])
        self.assertEqual(result, 1)
        self.assertIn('within45seconds', manifest['lastRun']['error'])
        self.assertIsNone(copied)
        self.assertEqual(sum(op == 'click' for op, _ in calls), 1)
        self.assertEqual(clock.seconds, 45)

    def test_poll_permissions_challenges_wrong_game_and_navigation_are_fatal(self):
        for tree in [PERMISSION, PARTIAL + '3 text Security verification\n',
                     TREE.replace('256 row Game Digimon World DS', '256 row Game Wrong game'), TREE.replace('/48320/', '/48321/')]:
            with self.subTest(tree=tree):
                result, manifest, copied, _, calls, clock = self.scenario(poll_trees=[tree])
                self.assertEqual(result, 1)
                self.assertEqual(manifest['lastRun']['status'], 'stopped-error')
                self.assertIsNone(copied)
                self.assertEqual(sum(op == 'click' for op, _ in calls), 1)
                self.assertEqual(clock.seconds, 3)

    def test_permission_or_page_change_before_click_sends_no_input(self):
        for tree in [PERMISSION, TREE.replace('/48320/', '/48321/')]:
            with self.subTest(tree=tree):
                result, _, copied, _, calls, _ = self.scenario(fresh=[tree])
                self.assertEqual(result, 1)
                self.assertIsNone(copied)
                self.assertFalse(any(op == 'click' for op, _ in calls))

    def test_conflicting_exact_names_preserve_originals_and_stop(self):
        originals = [png(label=b'A'), png(label=b'B')]
        result, manifest, copied, files, calls, _ = self.scenario(existing=originals)
        self.assertEqual(result, 1)
        self.assertIn('Conflicting exact-name', manifest['lastRun']['error'])
        self.assertIsNone(copied)
        self.assertEqual([files[FILENAME], files[FILENAME[:-4] + ' (1).png']], originals)
        self.assertFalse(any(op == 'click' for op, _ in calls))

    def test_identical_existing_names_adopt_without_download(self):
        result, _, copied, files, calls, _ = self.scenario(existing=[png(), png()])
        self.assertEqual(result, 0)
        self.assertEqual(copied, png())
        self.assertEqual(files[FILENAME], png())
        self.assertFalse(any(op == 'click' for op, _ in calls))

    def test_reviewed_prior_sources_are_adopted_without_download(self):
        for key, name, section, form_id in [('agumon', 'Agumon', 'Rookie Digimon', 18),
                ('greymon', 'Greymon', 'Champion Digimon', 19),
                ('metalgreymon', 'MetalGreymon', 'Ultimate Digimon', 20)]:
            with self.subTest(entry=key):
                entry = {'entryKey': key, 'formId': form_id, 'source': {'listedName': name, 'section': section}}
                result, manifest, copied, files, calls, clock = self.scenario(entry=entry, prior=png())
                self.assertEqual(result, 0)
                self.assertEqual(copied, png())
                self.assertEqual(manifest['entries'][key]['sha256'], hashlib.sha256(png()).hexdigest())
                self.assertEqual(set(files), {'unrelated.txt'})
                self.assertFalse(any(op == 'click' for op, _ in calls))
                self.assertEqual(clock.seconds, 0)

    def test_mismatched_reviewed_prior_stops_without_overwrite(self):
        entry = {'entryKey': 'agumon', 'formId': 18, 'source': {'listedName': 'Agumon', 'section': 'Rookie Digimon'}}
        for prior, private, existing, error in [(png(3, 2), None, (), 'dimensions'),
                (png(label=b'B'), None, (png(),), 'Conflicting exact-name'),
                (png(label=b'B'), png(), (), 'no overwrite')]:
            with self.subTest(error=error):
                result, manifest, copied, files, calls, _ = self.scenario(entry=entry, prior=prior, private=private, existing=existing)
                self.assertEqual(result, 1)
                self.assertIn(error, manifest['lastRun']['error'])
                self.assertEqual(copied, private)
                self.assertNotIn('agumon', manifest['entries'])
                self.assertFalse(any(op == 'click' for op, _ in calls))
                if existing:
                    self.assertEqual(files['DS _ DSi - Digimon World DS - Rookie Digimon - Agumon.png'], png())

    def test_different_private_source_is_never_overwritten(self):
        result, manifest, copied, files, calls, _ = self.scenario(existing=[png()], private=png(label=b'B'))
        self.assertEqual(result, 1)
        self.assertIn('no overwrite', manifest['lastRun']['error'])
        self.assertEqual(copied, png(label=b'B'))
        self.assertEqual(files[FILENAME], png())
        self.assertFalse(any(op == 'click' for op, _ in calls))


if __name__ == '__main__':
    unittest.main()

"""Private batch tests use generated original pixels only; no external artwork."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('world_ds_assets', ROOT / 'scripts/world-ds-assets.py')
pipeline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pipeline)


class WorldDsBatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.source = self.base / 'input'; self.source.mkdir()
        self.private = self.base / '.personal-assets' / 'world-ds'
        self.manifest = self.source / 'batch.json'
        self.batch = dict(formatVersion=1, collection='digimon-world-ds', version=1, entries=[])
        self.add('synthetic-one', 32)

    def tearDown(self):
        self.temporary.cleanup()

    def add(self, ident, size):
        image = Image.new('RGBA', (size * 2, size), (255, 0, 255, 255))
        # Two deliberately shifted original poses with opaque black retained.
        for frame in range(2):
            for x in range(4, size - 4):
                for y in range(6 + frame, size - 5 + frame):
                    image.putpixel((frame * size + x, y), (0, 0, 0, 255) if x == 4 else (20, 180, 90, 255))
        source = self.source / f'{ident}.png'; image.save(source); image.close()
        mapping = dict(formatVersion=1, packId=f'personal-{ident}', version=1, frameSize=size,
            provenance=dict(creator='Synthetic test author', sourceUrl='https://example.com/original-fixture',
                            game='Original synthetic test', rights='Original pixels', reuseScope='Local tests only'),
            sprites={ident:dict(name=ident, family='synthetic', stage=1, source=source.name,
                               anchor='bottom-center', transparentColor='#ff00ff',
                               animations={'idle':dict(frameMs=180, rects=[[0, 0, size, size], [size, 0, size, size]],
                                                       sourceDescription='Two original synthetic poses'),
                                           **{name:dict(fallback='idle') for name in pipeline.importer.ANIMATIONS[1:]}})})
        mapping_path = self.source / f'{ident}.mapping.json'
        mapping_path.write_text(json.dumps(mapping))
        self.batch['entries'].append(dict(artId=ident, mapping=mapping_path.name, deviceResize='nearest' if size == 64 else 'none',
            inspection=dict(reviewer='Synthetic fixture author', notes='Known original test geometry and magenta background',
                            sources=[dict(file=source.name, sha256=hashlib.sha256(source.read_bytes()).hexdigest())])))

    def save(self):
        self.manifest.write_text(json.dumps(self.batch))

    def build(self, check=False):
        self.save()
        return pipeline.build_batch(self.manifest, check=check, private_root=self.private)

    def test_per_form_outputs_are_deterministic_bounded_and_keep_audited_poses(self):
        self.add('synthetic-two', 64)
        index = self.build()
        self.assertEqual(index['budget']['forms'], 2)
        self.assertEqual(index['budget']['deviceTwoActorsRgb565Bytes'], 4096)
        self.assertLessEqual(index['budget']['largestDeviceBlobBytes'], 24688)
        for entry in index['entries']:
            for key in ['browser', 'device', 'provenance', 'audit']:
                raw = (self.private / entry[key]['file']).read_bytes()
                self.assertEqual(hashlib.sha256(raw).hexdigest(), entry[key]['sha256'])
                self.assertEqual(len(raw), entry[key]['bytes'])
            binary = (self.private / entry['device']['file']).read_bytes()
            decoded = pipeline.device.parse_dva(binary)
            self.assertEqual((decoded['width'], decoded['frames']), (32, 12))
            self.assertEqual(binary[18], 0)
            audit = json.loads((self.private / entry['audit']['file']).read_text())
            coverage = audit['coverage'][entry['artId']]
            self.assertEqual(coverage['attack']['kind'], 'reused-fallback')
            self.assertEqual(coverage['idle']['transforms'][0]['anchor'], 'bottom-center')
            self.assertEqual(coverage['idle']['transforms'][0]['downsampleFactor'], 2 if entry['artId'] == 'synthetic-two' else 1)
            sprite = json.loads((self.private / entry['browser']['file']).read_text())['sprites'][entry['artId']]
            image = pipeline.importer.decode_preview(sprite, 'idle', 0)
            self.assertEqual(image.getbbox()[3], sprite['height'] - 1)  # second pose is one pixel lower; one shared anchor preserves motion
            self.assertIn((0, 0, 0, 255), list(pipeline.importer.flattened(image)))
            image.close()
        before = (self.private / 'index.json').read_bytes()
        self.assertEqual(self.build(check=True), index)
        self.assertEqual((self.private / 'index.json').read_bytes(), before)
        self.assertEqual(list(self.private.glob('.staging-*')), [])

    def test_plan_handles_hundreds_without_opening_source_pixels(self):
        # Reuse metadata only; no fictitious game identity or downloaded pixels.
        original = json.loads((self.source / 'synthetic-one.mapping.json').read_text())
        self.batch['entries'] = []
        for n in range(255):
            ident = f'synthetic-{n}'
            mapping = copy.deepcopy(original)
            mapping['sprites'][ident] = mapping['sprites'].pop('synthetic-one')
            path = self.source / f'{ident}.mapping.json'; path.write_text(json.dumps(mapping))
            self.batch['entries'].append(dict(artId=ident, mapping=path.name, deviceResize='none',
                inspection=dict(reviewer='Fixture author', notes='Metadata-only planning fixture',
                    sources=[dict(file='synthetic-one.png', sha256='0' * 64)])))
        (self.source / 'synthetic-one.png').unlink()
        self.save()
        _, entries, _ = pipeline.read_batch(self.manifest)
        self.assertEqual(len(entries), 255)
        self.assertFalse(self.private.exists())
        self.batch['entries'] *= 3; self.save()
        with self.assertRaisesRegex(pipeline.Failure, '512'):
            pipeline.read_batch(self.manifest)

    def test_changed_uninspected_sheet_does_not_activate_any_index(self):
        self.batch['entries'][0]['inspection']['sources'][0]['sha256'] = '0' * 64
        with self.assertRaisesRegex(pipeline.Failure, 'inspected sheet'):
            self.build()
        self.assertFalse((self.private / 'index.json').exists())

    def test_mapping_replaced_during_import_cannot_claim_the_old_audit(self):
        self.save()
        original = pipeline.importer.build_import
        def changed(path):
            value = json.loads(path.read_text())
            value['sprites']['synthetic-one']['animations']['idle']['frameMs'] = 200
            path.write_text(json.dumps(value))
            return original(path)
        with patch.object(pipeline.importer, 'build_import', side_effect=changed):
            with self.assertRaisesRegex(pipeline.Failure, 'Mapping changed'):
                pipeline.build_batch(self.manifest, private_root=self.private)
        self.assertFalse((self.private / 'index.json').exists())

    def test_late_failure_preserves_previously_active_index(self):
        self.build(); previous = (self.private / 'index.json').read_bytes()
        self.batch['version'] = 2
        self.add('synthetic-two', 32)
        self.batch['entries'][1]['inspection']['sources'][0]['sha256'] = '0' * 64
        with self.assertRaises(pipeline.Failure):
            self.build()
        self.assertEqual((self.private / 'index.json').read_bytes(), previous)

    def test_identity_aliases_missing_inspection_and_silent_resizing_are_refused(self):
        original = copy.deepcopy(self.batch)
        for mutate in [lambda: self.batch['entries'].append(copy.deepcopy(self.batch['entries'][0])),
                       lambda: self.batch['entries'][0].update(artId='other'),
                       lambda: self.batch['entries'][0]['inspection'].update(reviewer='')]:
            self.batch = copy.deepcopy(original); mutate(); self.save()
            with self.assertRaises(pipeline.Failure):
                pipeline.read_batch(self.manifest)
        self.batch = original
        self.add('synthetic-two', 64); self.batch['entries'][1]['deviceResize'] = 'none'; self.save()
        with self.assertRaisesRegex(pipeline.Failure, 'explicit nearest'):
            pipeline.read_batch(self.manifest)

    def test_input_and_output_escape_are_rejected(self):
        self.batch['entries'][0]['mapping'] = '../outside.json'; self.save()
        with self.assertRaisesRegex(pipeline.Failure, 'relative paths'):
            pipeline.read_batch(self.manifest)
        with self.assertRaisesRegex(pipeline.Failure, 'world-ds'):
            pipeline.private_output(self.base / 'public', self.private)
        self.private.parent.mkdir(parents=True)
        self.private.symlink_to(self.source, target_is_directory=True)
        with self.assertRaisesRegex(pipeline.Failure, 'symlink'):
            pipeline.private_output(private_root=self.private)

    def test_changed_version_and_corrupted_output_do_not_silently_replace_assets(self):
        index = self.build()
        source = self.source / 'synthetic-one.mapping.json'
        value = json.loads(source.read_text()); value['sprites']['synthetic-one']['animations']['idle']['frameMs'] = 200
        source.write_text(json.dumps(value)); self.batch['version'] = 2
        with self.assertRaisesRegex(pipeline.Failure, 'per-art version'):
            self.build()
        value['version'] = 2; source.write_text(json.dumps(value)); self.build()
        corrupted = self.private / index['entries'][0]['device']['file']
        corrupted.write_bytes(b'bad')
        # The newer immutable version remains selected; old corrupt content is
        # never accepted by its saved SHA or used as a newer version.
        current = json.loads((self.private / 'index.json').read_text())
        selected = self.private / current['entries'][0]['device']['file']
        selected.write_bytes(b'bad')
        with self.assertRaisesRegex(pipeline.Failure, 'differs'):
            self.build(check=True)


if __name__ == '__main__':
    unittest.main()

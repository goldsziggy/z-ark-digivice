"""Metadata/byte-checkpoint regressions; generated original fixture pixels only."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from test_world_ds_browser_acquisition import png

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('asset_audit', ROOT / 'scripts/audit-world-ds-assets.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class CheckpointTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.private = self.root / '.personal-assets/world-ds'
        self.source = self.private / 'source'
        self.source.mkdir(parents=True)
        (self.root / 'data').mkdir()
        catalog = {'entries': [{'entryKey': f'fixture-{i}', 'formId': i,
                               'source': {'listedName': f'Fixture {i}', 'section': 'Rookie Digimon'}}
                              for i in range(255)]}
        catalog_bytes = json.dumps(catalog).encode()
        (self.root / 'data/world-ds-catalog.json').write_bytes(catalog_bytes)
        raw = png()
        (self.source / 'fixture-1.png').write_bytes(raw)
        self.record = {'entryKey': 'fixture-1', 'formId': 1, 'name': 'Fixture 1',
                       'section': 'Rookie Digimon', 'file': 'fixture-1.png',
                       'url': 'https://www.spriters-resource.com/ds_dsi/dgmnworldds/asset/1/',
                       'sizeLabel': f'{len(raw)} B (2x2)', 'bytes': len(raw), 'width': 2, 'height': 2,
                       'sha256': hashlib.sha256(raw).hexdigest()}
        self.manifest = {'formatVersion': 1, 'kind': 'private-source-acquisition',
                         'catalogSha256': hashlib.sha256(catalog_bytes).hexdigest(),
                         'entries': {'fixture-1': self.record}}
        self.index = {'formatVersion': 1, 'privateOnly': True, 'collection': 'digimon-world-ds',
                      'version': 1, 'entries': [], 'budget': {'forms': 0, 'browserEncodedBytes': 0,
                                                            'deviceEncodedBytes': 0}}
        self.save()

    def save(self):
        (self.source / 'acquisition.json').write_text(json.dumps(self.manifest))
        (self.private / 'index.json').write_text(json.dumps(self.index))

    def test_available_is_not_integrated_and_output_contains_no_pixels(self):
        result = audit.audit(self.root)
        self.assertEqual((result['availableSheets'], result['integratedPacks']), (1, 0))
        self.assertEqual(result['availableNotIntegrated'], ['fixture-1'])
        self.assertEqual(len(result['missingSourceEntries']), 254)
        self.assertNotIn('base64', json.dumps(result))

    def test_identity_missing_source_and_byte_tampering_fail_closed(self):
        self.record['formId'] = 4
        self.save()
        with self.assertRaisesRegex(ValueError, 'identity'):
            audit.audit(self.root)
        self.record['formId'] = 1
        self.save()
        path = self.source / 'fixture-1.png'
        path.write_bytes(png(label=b'Changed original fixture'))
        with self.assertRaises((ValueError, RuntimeError)):
            audit.audit(self.root)
        path.unlink()
        with self.assertRaises(FileNotFoundError):
            audit.audit(self.root)

    def test_unlisted_integrated_identity_is_rejected(self):
        self.index['entries'] = [{'artId': 'ds-form-999'}]
        self.save()
        with self.assertRaisesRegex(ValueError, 'identity'):
            audit.audit(self.root)

    def test_referenced_artifact_and_source_provenance_tampering(self):
        source = {field: self.record[field] for field in ('bytes', 'sha256', 'width', 'height')}
        source.update(spriteId='ds-form-1', provenance={'sourceUrl': self.record['url']})
        browser = {'packId': 'personal-ds-form-1', 'version': 1, 'sprites': {'ds-form-1': {'name': 'Fixture 1'}}}
        payloads = {'browser': json.dumps(browser).encode(), 'device': b'byte-check-only fixture'}
        provenance = {'sources': [source], 'packId': browser['packId'], 'version': 1,
                      'packSha256': hashlib.sha256(payloads['browser']).hexdigest()}
        device_audit = {'artId': 'ds-form-1', 'bytes': len(payloads['device']),
                        'sha256': hashlib.sha256(payloads['device']).hexdigest(), 'sourceSha256': source['sha256']}
        payloads['provenance'] = json.dumps(provenance).encode()
        payloads['audit'] = json.dumps(device_audit).encode()
        row = {'artId': 'ds-form-1', 'name': 'Fixture 1', 'version': 1}
        for kind, raw in payloads.items():
            name = f'fixture-{kind}.bin'
            (self.private / name).write_bytes(raw)
            row[kind] = {'file': name, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}
        self.index['entries'] = [row]
        self.index['budget'] = {'forms': 1, 'browserEncodedBytes': len(payloads['browser']),
                                'deviceEncodedBytes': len(payloads['device'])}
        self.save()
        self.assertEqual(audit.audit(self.root)['verifiedPackArtifacts'], 4)
        (self.private / row['device']['file']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Artifact bytes differ'):
            audit.audit(self.root)
        (self.private / row['device']['file']).write_bytes(payloads['device'])
        source['sha256'] = '0' * 64
        raw = json.dumps(provenance).encode()
        (self.private / row['provenance']['file']).write_bytes(raw)
        row['provenance'].update(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        self.save()
        with self.assertRaisesRegex(ValueError, 'source binding'):
            audit.audit(self.root)
        source['sha256'] = self.record['sha256']
        raw = json.dumps(provenance).encode()
        (self.private / row['provenance']['file']).write_bytes(raw)
        row['provenance'].update(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        browser['packId'] = 'personal-ds-form-2'
        raw = json.dumps(browser).encode()
        (self.private / row['browser']['file']).write_bytes(raw)
        row['browser'].update(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        self.save()
        with self.assertRaisesRegex(ValueError, 'Browser pack identity'):
            audit.audit(self.root)

    def test_escape_symlink_and_resource_bounds(self):
        path = self.source / 'small.json'
        path.write_bytes(b'12345')
        with self.assertRaisesRegex(ValueError, 'bounded'):
            audit.bounded_file(self.source, 'small.json', 4)
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            audit.bounded_file(self.source, '../index.json', 10000)
        link = self.source / 'link.json'
        link.symlink_to(path)
        with self.assertRaisesRegex(ValueError, 'regular'):
            audit.bounded_file(self.source, 'link.json', 10000)


if __name__ == '__main__':
    unittest.main()

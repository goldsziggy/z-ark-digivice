"""Run with the existing Pillow interpreter; writes no repository assets."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('device_asset_builder', ROOT / 'scripts/build-device-assets.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class ProductionAssetBuildTest(unittest.TestCase):
    def test_default_build_contains_only_scenery_and_no_resident_creature(self):
        outputs, header = builder.build()
        index = json.loads(outputs['index.json'])
        self.assertEqual(len(index['packs']), 8)
        self.assertTrue(all(entry['kind'] == 'background' and entry['id'].startswith('scene-') for entry in index['packs']))
        self.assertFalse(any(name.endswith('.dva') for name in outputs))
        self.assertIsNone(header)
        self.assertEqual(json.loads(outputs['budget.json'])['measured']['residentFallbackBytes'], 0)

    def test_explicit_fixture_build_retains_codec_examples(self):
        outputs, header = builder.build(include_test_fixtures=True)
        self.assertEqual(len(json.loads(outputs['index.json'])['packs']), 18)
        self.assertEqual(sum(name.endswith('.dva') for name in outputs), 10)
        self.assertIn(b'mote', header)
        self.assertIn(b'flicker', header)

    def test_toy_browser_builder_requires_explicit_opt_in(self):
        result = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/build-assets.py')], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--include-test-fixtures', result.stderr)

    def test_fixture_header_cannot_overwrite_release_fallback(self):
        result = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/build-device-assets.py'),
                                 '--include-test-fixtures', '--fallback-header', str(ROOT / 'firmware/runtime/fallback_asset.hpp')],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('cannot replace the release fallback', result.stderr)


if __name__ == '__main__':
    unittest.main()

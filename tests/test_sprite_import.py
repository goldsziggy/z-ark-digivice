"""Risk tests use only original synthetic pixels created in temporary directories."""
import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

from PIL import Image

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sprite_importer', ROOT / 'scripts/import-sprite-sheet.py')
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


class SpriteImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name) / 'input'
        self.base.mkdir()
        self.source = self.base / 'sheet.png'
        Image.new('RGBA', (64, 64), (255, 0, 0, 255)).save(self.source)
        self.mapping = {
            'formatVersion': 1, 'packId': 'personal-synthetic', 'version': 1, 'frameSize': 64,
            'provenance': {'creator': 'Synthetic test author', 'sourceUrl': 'https://example.com/original-fixture',
                'game': 'Original test pixels', 'rights': 'Original synthetic fixture', 'reuseScope': 'Tests only'},
            'sprites': {'mote': {'name': 'Synthetic', 'family': 'test', 'stage': 1, 'source': 'sheet.png',
                'resize': 'none', 'animations': {'idle': {'frameMs': 180, 'rects': [[0, 0, 32, 64]],
                    'sourceDescription': 'Synthetic tall standing pose'}, **{name: {'fallback': 'idle'} for name in importer.ANIMATIONS if name != 'idle'}}}},
        }
        self.manifest = self.base / 'mapping.json'

    def tearDown(self):
        self.temporary.cleanup()

    def build(self):
        self.manifest.write_text(json.dumps(self.mapping), encoding='utf-8')
        return importer.build_import(self.manifest)

    def test_native_tall_pixels_preserved_and_fallbacks_explicit(self):
        result = self.build()
        sprite = result['pack']['sprites']['mote']
        image = importer.decode_preview(sprite, 'idle', 0)
        self.assertEqual(image.getbbox(), (16, 0, 48, 64))
        self.assertEqual(sum(pixel[3] != 0 for pixel in importer.flattened(image)), 32 * 64)
        self.assertEqual(result['pack']['license'], importer.LICENSE)
        idle = result['sidecar']['coverage']['mote']['idle']
        self.assertEqual(idle['originalFrameCount'], 1)
        self.assertEqual(idle['sourceDescription'], 'Synthetic tall standing pose')
        self.assertEqual(idle['transforms'][0]['downsampleFactor'], 1)
        for name in importer.ANIMATIONS[1:]:
            info = result['sidecar']['coverage']['mote'][name]
            self.assertEqual((info['kind'], info['originalFrameCount'], info['sourceRects']), ('reused-fallback', 0, []))
            self.assertEqual(sprite['animations'][name]['frames'], sprite['animations']['idle']['frames'])

    def test_missing_pose_and_fallback_chains_rejected(self):
        del self.mapping['sprites']['mote']['animations']['sleep']
        with self.assertRaisesRegex(importer.ImportFailure, 'explicitly map'):
            self.build()
        self.mapping['sprites']['mote']['animations']['sleep'] = {'fallback': 'attack'}
        with self.assertRaisesRegex(importer.ImportFailure, 'fallback to idle'):
            self.build()
        self.mapping['sprites']['mote']['animations']['sleep'] = {'fallback': 'idle'}
        self.mapping['sprites']['mote']['animations']['idle'] = {'fallback': 'idle'}
        with self.assertRaises(importer.ImportFailure):
            self.build()

    def test_no_implicit_downsize_and_explicit_integer_nearest(self):
        self.mapping['frameSize'] = 32
        with self.assertRaisesRegex(importer.ImportFailure, 'explicit resize'):
            self.build()
        self.mapping['sprites']['mote']['resize'] = 'nearest'
        result = self.build()
        transform = result['sidecar']['coverage']['mote']['idle']['transforms'][0]
        self.assertEqual(transform['downsampleFactor'], 2)
        self.assertEqual(transform['contentSize'], [16, 32])
        image = importer.decode_preview(result['pack']['sprites']['mote'], 'idle', 0)
        self.assertEqual(image.getbbox(), (8, 0, 24, 32))
        self.mapping['sprites']['mote']['animations']['idle']['rects'] = [[0, 0, 45, 39]]
        odd = self.build()['sidecar']['coverage']['mote']['idle']['transforms'][0]
        self.assertEqual(odd['paddedSourceSize'], [46, 40])
        self.assertEqual(odd['contentSize'], [23, 20])

    def test_alpha_threshold_color_key_and_opaque_black(self):
        image = Image.new('RGBA', (32, 32))
        for x, value in enumerate([(255, 0, 0, 127), (0, 0, 255, 128), (255, 0, 255, 255), (0, 0, 0, 255)]):
            image.putpixel((x, 0), value)
        image.save(self.source)
        self.mapping['frameSize'] = 32
        sprite = self.mapping['sprites']['mote']
        sprite['transparentColor'] = '#FF00FF'
        sprite['anchor'] = 'center'
        sprite['animations']['idle']['rects'] = [[0, 0, 32, 32]]
        decoded = importer.decode_preview(self.build()['pack']['sprites']['mote'], 'idle', 0)
        self.assertEqual([decoded.getpixel((x, 0))[3] for x in range(4)], [0, 255, 0, 255])
        self.assertEqual(decoded.getpixel((3, 0)), (0, 0, 0, 255))
        raw = base64.b64decode(self.build()['pack']['sprites']['mote']['animations']['idle']['frames'][0], validate=True)
        self.assertEqual(len(raw), 512)
        self.assertEqual(raw[0] >> 4, 0)
        self.assertGreater(raw[0] & 15, 0)

    def test_quantization_is_deterministic_and_bounded(self):
        image = Image.new('RGBA', (32, 32))
        image.putdata([((x * 7) % 256, (y * 11) % 256, ((x + y) * 13) % 256, 255) for y in range(32) for x in range(32)])
        image.save(self.source)
        self.mapping['frameSize'] = 32
        self.mapping['sprites']['mote']['animations']['idle']['rects'] = [[0, 0, 32, 32]]
        one, two = self.build(), self.build()
        self.assertEqual(one['packBytes'], two['packBytes'])
        self.assertEqual(one['sidecarBytes'], two['sidecarBytes'])
        report = one['sidecar']['conversion']['colorReports']['mote']
        self.assertGreater(report['sourceOpaqueRgb565Colors'], 15)
        self.assertLessEqual(report['paletteOpaqueColors'], 15)
        self.assertEqual(len(one['pack']['sprites']['mote']['palette']), 16)
        self.assertTrue(report['quantized'])

    def test_crop_and_image_byte_dimension_bounds(self):
        self.mapping['sprites']['mote']['animations']['idle']['rects'] = [[40, 0, 32, 64]]
        with self.assertRaisesRegex(importer.ImportFailure, 'outside'):
            self.build()
        self.mapping['sprites']['mote']['animations']['idle']['rects'] = [[0, 0, 32, 32]]
        Image.new('RGBA', (4097, 1), (1, 2, 3, 255)).save(self.source)
        with self.assertRaisesRegex(importer.ImportFailure, '4096'):
            self.build()
        with self.source.open('wb') as handle:
            handle.truncate(importer.MAX_IMAGE_BYTES + 1)
        with self.assertRaisesRegex(importer.ImportFailure, '16 MiB'):
            self.build()

    def test_source_escape_and_outside_output_refused(self):
        outside = Path(self.temporary.name) / 'outside.png'
        Image.new('RGBA', (32, 32), (1, 2, 3, 255)).save(outside)
        self.mapping['sprites']['mote']['source'] = '../outside.png'
        with self.assertRaisesRegex(importer.ImportFailure, 'beneath'):
            self.build()
        (self.base / 'link.png').symlink_to(outside)
        self.mapping['sprites']['mote']['source'] = 'link.png'
        with self.assertRaisesRegex(importer.ImportFailure, 'symlink'):
            self.build()
        self.mapping['sprites']['mote']['source'] = 'sheet.png'
        result = self.build()
        private_root = Path(self.temporary.name) / '.personal-assets'
        with self.assertRaisesRegex(importer.ImportFailure, 'ignored'):
            importer.write_import(result, Path(self.temporary.name) / 'public', private_root=private_root)
        output = private_root / 'personal-synthetic'
        importer.write_import(result, output, private_root=private_root, previews=True)
        self.assertEqual((output / 'pack.json').read_bytes(), result['packBytes'])
        self.assertEqual(hashlib.sha256((output / 'pack.json').read_bytes()).hexdigest(), result['sidecar']['packSha256'])
        self.assertTrue((output / 'preview-mote.png').is_file())

    def test_transparent_frame_excess_frames_and_decoded_budget(self):
        Image.new('RGBA', (64, 64)).save(self.source)
        with self.assertRaisesRegex(importer.ImportFailure, 'transparent'):
            self.build()
        self.mapping['sprites']['mote']['animations']['idle']['rects'] *= 9
        with self.assertRaisesRegex(importer.ImportFailure, '1..8'):
            self.build()
        self.mapping['sprites']['mote']['animations']['idle']['rects'] = [[0, 0, 32, 32]] * 8
        sprite = copy.deepcopy(self.mapping['sprites']['mote'])
        self.mapping['sprites'] = {f'creature-{index}': copy.deepcopy(sprite) for index in range(3)}
        with self.assertRaisesRegex(importer.ImportFailure, '2 MiB'):
            self.build()

    def test_shared_alpha_anchor_preserves_motion_and_all_opaque_pixels(self):
        image = Image.new('RGBA', (64, 32))
        for x in range(4):
            for y in range(6):
                image.putpixel((2 + x, 4 + y), (255, 0, 0, 255))
                image.putpixel((32 + 3 + x, 2 + y), (255, 0, 0, 255))
        image.save(self.source)
        self.mapping['frameSize'] = 32
        self.mapping['sprites']['mote']['animations']['idle']['rects'] = [[0, 0, 32, 32], [32, 0, 32, 32]]
        result = self.build()
        sprite = result['pack']['sprites']['mote']
        one = importer.decode_preview(sprite, 'idle', 0)
        two = importer.decode_preview(sprite, 'idle', 1)
        self.assertEqual(one.getbbox(), (13, 26, 17, 32))
        self.assertEqual(two.getbbox(), (14, 24, 18, 30))
        for frame in (one, two):
            self.assertEqual(sum(pixel[3] != 0 for pixel in importer.flattened(frame)), 24)
        transforms = result['sidecar']['coverage']['mote']['idle']['transforms']
        self.assertEqual(transforms[0]['sharedShift'], transforms[1]['sharedShift'])
        self.assertEqual(transforms[0]['anchor'], 'bottom-center')

    def test_private_root_symlink_and_source_output_overlap_are_rejected(self):
        result = self.build()
        public = Path(self.temporary.name) / 'tracked-assets'
        public.mkdir()
        private = Path(self.temporary.name) / '.personal-assets'
        private.symlink_to(public, target_is_directory=True)
        with self.assertRaisesRegex(importer.ImportFailure, 'must not be a symlink'):
            importer.write_import(result, private / 'personal-synthetic', private_root=private)
        self.assertEqual(list(public.iterdir()), [])
        private.unlink()
        private.mkdir()
        target = private / 'personal-synthetic'
        target.mkdir()
        original = target / 'preview-mote.png'
        original.write_bytes(self.source.read_bytes())
        mapping = copy.deepcopy(self.mapping)
        mapping['sprites']['mote']['source'] = 'preview-mote.png'
        mapping_path = target / 'mapping.json'
        mapping_path.write_text(json.dumps(mapping))
        imported = importer.build_import(mapping_path)
        before = original.read_bytes()
        with self.assertRaisesRegex(importer.ImportFailure, 'overwrite a source'):
            importer.write_import(imported, target, private_root=private, previews=True)
        self.assertEqual(original.read_bytes(), before)
        self.assertFalse((target / 'pack.json').exists())
        # A mapping itself can occupy an output filename; detect that as well.
        mapping_path = target / 'provenance.json'
        mapping_path.write_text(json.dumps(mapping))
        imported = importer.build_import(mapping_path)
        before_mapping = mapping_path.read_bytes()
        with self.assertRaisesRegex(importer.ImportFailure, 'overwrite a source'):
            importer.write_import(imported, target, private_root=private)
        self.assertEqual(mapping_path.read_bytes(), before_mapping)

    def test_text_limits_match_raw_browser_utf16_lengths(self):
        for name in (' ' + 'A' * 64, 'A' * 63 + '\U0001f431'):
            self.mapping['sprites']['mote']['name'] = name
            with self.assertRaisesRegex(importer.ImportFailure, 'Invalid identity'):
                self.build()
        self.mapping['sprites']['mote']['name'] = 'A' * 62 + '\U0001f431'
        result = self.build()
        self.assertEqual(result['pack']['sprites']['mote']['name'], 'A' * 62 + '\U0001f431')
        script = "import {readFileSync} from 'node:fs'; import {validatePersonalImport} from './web/personal-pack.js'; const input=JSON.parse(readFileSync(0,'utf8')); const out=await validatePersonalImport(input.pack,input.sidecar); if(out.pack.sprites.mote.name.length!==64) throw Error('UTF16 length mismatch');"
        checked = subprocess.run([str(ROOT / 'scripts/node.sh'), '--input-type=module', '-e', script],
            input=json.dumps({'pack': result['packBytes'].decode(), 'sidecar': result['sidecarBytes'].decode()}),
            text=True, cwd=ROOT, capture_output=True, timeout=10)
        self.assertEqual(checked.returncode, 0, checked.stderr)

    def test_provenance_and_duplicate_manifest_keys(self):
        self.mapping['sprites']['mote']['provenance'] = {**self.mapping['provenance'], 'creator': 'Different credited sheet author'}
        result = self.build()
        self.assertEqual(result['sidecar']['sources'][0]['provenance']['creator'], 'Different credited sheet author')
        self.assertEqual(result['sidecar']['sources'][0]['sha256'], hashlib.sha256(self.source.read_bytes()).hexdigest())
        self.mapping['provenance']['sourceUrl'] = 'file:///private/path'
        with self.assertRaisesRegex(importer.ImportFailure, 'HTTP'):
            self.build()
        self.manifest.write_text('{"formatVersion":1,"formatVersion":2}')
        with self.assertRaisesRegex(importer.ImportFailure, 'Duplicate'):
            importer.build_import(self.manifest)


if __name__ == '__main__':
    unittest.main()

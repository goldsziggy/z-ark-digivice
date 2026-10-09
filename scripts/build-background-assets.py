#!/usr/bin/env python3
"""Build approved, bounded scene packs without changing artwork composition.

Uses Python's standard library and an existing ImageMagick executable. No installs,
network access, source extraction, catalog signing, resizing, or generated artwork.
Pinned source bytes prevent accidentally publishing a screenshot or unrelated ZIP.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = '76d3dfab5344868eab07b0ac682fb1716c2b47222f464eb682d8719af9299fbb'
SCENES = ('meadow', 'forest', 'beach', 'ruins', 'cavern', 'snow', 'volcanic', 'digital')
LICENSE = 'User-approved generated art; source license unspecified'
MAX_JPEG_BYTES = 100 * 1024
MAX_CACHE_BYTES = 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def json_bytes(value, pretty=False):
    return (json.dumps(value, ensure_ascii=True, sort_keys=True,
                       indent=2 if pretty else None,
                       separators=None if pretty else (',', ':')) + '\n').encode('ascii')


def png_dimensions(data):
    require(data[:16] == b'\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR', 'Expected a PNG with IHDR')
    return struct.unpack('>II', data[16:24])


def jpeg_dimensions(data):
    """Check baseline SOF and metadata restrictions before any runtime decoder."""
    require(data[:2] == b'\xff\xd8' and data[-2:] == b'\xff\xd9', 'JPEG needs SOI/EOI')
    offset = 2
    dimensions = None
    while offset < len(data):
        require(data[offset] == 255, 'Invalid JPEG marker')
        while data[offset] == 255:
            offset += 1
        marker = data[offset]
        offset += 1
        require(marker not in (0xe1, 0xe2, 0xfe), 'EXIF, ICC, and comments must be stripped')
        require(offset + 2 <= len(data), 'Truncated JPEG segment')
        length = int.from_bytes(data[offset:offset + 2], 'big')
        require(length >= 2 and offset + length <= len(data), 'Invalid JPEG segment length')
        if marker in (0xc0, 0xc1, 0xc2):
            require(marker == 0xc0 and length == 17, 'Only baseline three-component JPEG is allowed')
            bits, height, width, components = struct.unpack('>BHHB', data[offset + 2:offset + 8])
            require(bits == 8 and components == 3, 'Expected 8-bit RGB JPEG')
            dimensions = (width, height)
        if marker == 0xda:
            break
        offset += length
    require(dimensions == (480, 480), 'JPEG must stay 480 by 480')
    return dimensions


def build(args):
    source = args.source_zip.resolve()
    archive = source.read_bytes()
    require(digest(archive) == SOURCE_SHA256, 'Source ZIP differs from the approved archive')
    magick = shutil.which(args.magick)
    require(magick is not None, 'ImageMagick is required; this script never installs tools')
    tool_version = subprocess.check_output([magick, '-version'], text=True).splitlines()[0]
    files = {}
    records = []
    with zipfile.ZipFile(source) as zipped:
        original_provenance = zipped.read('battlefields/provenance.json')
        provenance = json.loads(original_provenance)
        require(provenance.get('generation_mode') == 'built-in image_gen', 'Unexpected provenance')
        source_records = provenance['records']
        require(len(source_records) == len(SCENES), 'Expected eight provenance records')
        files['assets/backgrounds/source-provenance.json'] = original_provenance
        for index, scene in enumerate(SCENES, 1):
            key = f'{index:02d}-{scene}'
            entry = source_records[index - 1]
            require(entry['key'] == key, 'Source order or scene identity differs')
            paths = {kind: f'battlefields/{kind}/{key}{suffix}' for kind, suffix in
                     [('masters', '-master.png'), ('png480', '-480.png'), ('rgb565', '-480.rgb565')]}
            original = {kind: zipped.read(path) for kind, path in paths.items()}
            require(digest(original['masters']) == entry['master_sha256'], f'Master hash mismatch: {scene}')
            require(png_dimensions(original['masters']) == tuple(entry['master_dimensions']), 'Master dimensions differ')
            require(png_dimensions(original['png480']) == (480, 480), 'Approved runtime source must be 480 square')
            require(len(original['rgb565']) == 480 * 480 * 2, 'Unexpected source RGB565 size')
            # Verify the shipped raw variant's exact LE pixel encoding, using the PNG source.
            rgb = subprocess.check_output([magick, 'png:-', '-depth', '8', 'rgb:-'], input=original['png480'])
            require(len(rgb) == 480 * 480 * 3, 'Unexpected source RGB byte count')
            raw = bytearray()
            for pixel in range(0, len(rgb), 3):
                red, green, blue = rgb[pixel:pixel + 3]
                raw.extend(struct.pack('<H', ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)))
            require(raw == original['rgb565'], f'Source RGB565 pixel encoding differs: {scene}')
            jpeg = subprocess.check_output([
                magick, 'png:-', '-strip', '-sampling-factor', '4:2:0',
                '-interlace', 'none', '-quality', str(args.quality), 'jpeg:-'], input=original['png480'])
            jpeg_dimensions(jpeg)
            require(len(jpeg) <= MAX_JPEG_BYTES, f'JPEG exceeds 100 KiB: {scene}')
            pack_id = f'scene-{scene}-v1'
            pack = {
                'formatVersion': 3, 'packId': pack_id, 'version': 1, 'rulesVersion': 1,
                'license': LICENSE,
                'background': {'sceneId': scene, 'name': entry['name'], 'encoding': 'jpeg-base64',
                               'width': 480, 'height': 480, 'data': base64.b64encode(jpeg).decode('ascii')},
                'provenance': {'source': 'approved-generated-background', 'sourceSha256': digest(original['png480'])},
            }
            pack_bytes = json_bytes(pack)
            require(len(pack_bytes) <= 256 * 1024, 'Pack exceeds existing transport cap')
            jpeg_path = f'assets/backgrounds/jpeg/{scene}-480.jpg'
            pack_path = f'assets/packs/{pack_id}.json'
            files[jpeg_path] = jpeg
            files[pack_path] = pack_bytes
            if args.include_rgb565:
                files[f'assets/backgrounds/rgb565/{scene}-480.rgb565'] = original['rgb565']
            records.append({
                'sceneId': scene, 'name': entry['name'], 'packId': pack_id,
                'source': {kind: {'archivePath': paths[kind], 'bytes': len(data), 'sha256': digest(data)}
                           for kind, data in original.items()},
                'jpeg': {'path': jpeg_path, 'width': 480, 'height': 480, 'quality': args.quality,
                         'sampling': '4:2:0', 'progressive': False, 'metadata': 'stripped',
                         'bytes': len(jpeg), 'sha256': digest(jpeg)},
                'pack': {'path': pack_path, 'bytes': len(pack_bytes), 'sha256': digest(pack_bytes),
                         'decodedRgbaBytes': 480 * 480 * 4},
            })
    existing = [ROOT / 'assets/packs' / (name + '.json') for name in ('starter-v2', 'tide-v1', 'ember-v1')]
    sprite_bytes = sum(path.stat().st_size for path in existing)
    pack_bytes = sum(record['pack']['bytes'] for record in records)
    total = sprite_bytes + pack_bytes
    largest_pack = max(record['pack']['bytes'] for record in records)
    staged_total = total + largest_pack
    require(staged_total <= MAX_CACHE_BYTES, 'Pack payloads plus largest staging copy alone exceed 1 MiB; full cache accounting is a separate check')
    metadata = {
        'formatVersion': 1,
        'sourceArchive': {'path': str(source), 'bytes': len(archive), 'sha256': SOURCE_SHA256,
                          'provenancePath': 'battlefields/provenance.json', 'provenanceSha256': digest(original_provenance)},
        'rights': {'runtimeLicenseText': LICENSE, 'sourceLicense': None,
                   'label': 'User-approved generated art; source license unspecified',
                   'sourceLicenseStatus': 'No explicit license field or license file is present in the supplied ZIP.',
                   'authorizedScope': 'User-approved integration of the generated battlefield artwork into this Digivice project.',
                   'redistributionLicense': 'Not asserted; this label does not grant CC0 or third-party reuse rights.'},
        'conversion': {'script': 'scripts/build-background-assets.py', 'tool': tool_version,
                       'quality': args.quality, 'resize': 'none', 'crop': 'none', 'compositionChanges': 'none',
                       'note': 'Pinned ZIP, master hashes, PNG dimensions, and source RGB565 pixels verified before encoding.'},
        'records': records,
    }
    budget = {
        'formatVersion': 1, 'measurement': 'Exact pack payload bytes only; cache envelopes, record/index overhead, and runtime decoder/GUI/parser/GPU allocations are excluded.',
        'cacheAccountingNote': 'allPacksPlusLargestStagingBytes and payloadOnly*HeadroomBytes exclude cache accounting overhead; actual cache headroom is recorded in docs/evidence/background-cache-budget.json.',
        'backgroundCount': len(records), 'jpegBytes': sum(r['jpeg']['bytes'] for r in records),
        'backgroundPackBytes': pack_bytes, 'existingSpritePackBytes': sprite_bytes,
        'allRuntimePackBytes': total, 'cacheLimitBytes': MAX_CACHE_BYTES, 'payloadOnlyHeadroomBytes': MAX_CACHE_BYTES - total,
        'allPacksPlusLargestStagingBytes': staged_total, 'payloadOnlyStagingHeadroomBytes': MAX_CACHE_BYTES - staged_total,
        'largestJpegBytes': max(r['jpeg']['bytes'] for r in records),
        'largestBackgroundPackBytes': max(r['pack']['bytes'] for r in records),
        'singleBackground': {'width': 480, 'height': 480, 'decodedRgbaBytes': 921600,
                             'rawRgb565Bytes': 460800, 'doubleRgb565FramebufferBytes': 921600},
        'allBackgroundsDecodedRgbaBytes': 8 * 921600,
        'sourceRawRgb565Bytes': 8 * 460800,
        'rawRgb565CopiedToRepository': args.include_rgb565,
        'standaloneJpegAndPackBytesOnDisk': sum(r['jpeg']['bytes'] + r['pack']['bytes'] for r in records),
        'deviceStatus': 'Host conversion only. ESP JPEG decoder, display buffers, PSRAM use, frame rate and flash have not been measured.',
        'packs': [{'id': r['packId'], **r['pack']} for r in records],
    }
    files['assets/backgrounds/provenance.json'] = json_bytes(metadata, True)
    files['assets/backgrounds/budget.json'] = json_bytes(budget, True)
    for name, data in files.items():
        path = ROOT / name
        if args.check:
            require(path.is_file() and path.read_bytes() == data, f'Reproducibility check failed: {name}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print(json.dumps({'mode': 'check' if args.check else 'build', 'files': len(files),
                      'backgroundPackBytes': pack_bytes, 'allRuntimePackBytes': total,
                      'payloadOnlyHeadroomBytes': MAX_CACHE_BYTES - total, 'payloadOnlyStagingHeadroomBytes': MAX_CACHE_BYTES - staged_total}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-zip', required=True, type=Path)
    parser.add_argument('--magick', default='magick')
    parser.add_argument('--quality', type=int, choices=range(82, 86), default=82)
    parser.add_argument('--include-rgb565', action='store_true', help='Also copy the eight exact source raw variants (3.52 MiB).')
    parser.add_argument('--check', action='store_true', help='Regenerate in memory and require byte-identical checked-in outputs.')
    try:
        build(parser.parse_args())
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f'Background build failed: {error}', file=sys.stderr)
        sys.exit(1)

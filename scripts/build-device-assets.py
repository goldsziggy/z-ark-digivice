#!/usr/bin/env python3
"""Build scene-only device assets; toy sprites require --include-test-fixtures.

Pillow is needed only for JPEG resizing.

No browser pack, signed catalog, game rule or private artwork is changed.
Use --self-test with plain Python; use the existing Pillow environment for build
or --check. Output and metadata have no timestamps or absolute source paths.
"""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
PACKS = ROOT / 'assets' / 'packs'
MAX_BLOB = 128 * 1024
NATIVE_SIZE = 412  # Official Waveshare ESP32-S3-Touch-LCD-1.46, SKU29565.
ANIMATIONS = ('idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate')
FALLBACK_COUNTS = (2, 2, 1, 1, 1, 2)
SCENES = ('meadow', 'forest', 'beach', 'ruins', 'cavern', 'snow', 'volcanic', 'digital')
HEADER = struct.Struct('<4sHHIHHBBBBHHII')
ANIMATION = struct.Struct('<BBHHH')
assert HEADER.size == 32 and ANIMATION.size == 8


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True) + '\n').encode('ascii')


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def ensure(condition, message):
    if not condition:
        raise ValueError(message)


def parse_dva(raw):
    """Independent bounded reader also used by corruption tests and build checks."""
    ensure(32 <= len(raw) <= MAX_BLOB, 'DVA size outside bound')
    (magic, fmt, header_bytes, version, width, height, palette_count, animation_count,
     transparent, flags, frame_count, frame_bytes, payload_bytes, crc) = HEADER.unpack_from(raw)
    ensure((magic, fmt, header_bytes) == (b'DVA1', 1, 32), 'DVA header/version')
    ensure(1 <= version <= 0xffffffff, 'DVA asset version')
    ensure(width == height and width in (16, 32), 'DVA dimensions')
    ensure((palette_count, animation_count, transparent, flags) == (16, 6, 0, 0), 'DVA palette/animation/flags')
    ensure(frame_bytes == width * height // 2 and 1 <= frame_count <= 48, 'DVA frame dimensions/count')
    ensure(payload_bytes == 32 + animation_count * 8 + frame_count * frame_bytes, 'DVA sections')
    ensure(len(raw) == 32 + payload_bytes, 'DVA length')
    ensure(zlib.crc32(raw[32:]) == crc, 'DVA payload CRC')
    palette = struct.unpack_from('<16H', raw, 32)
    clips = []
    next_frame = 0
    for animation_id in range(animation_count):
        record = ANIMATION.unpack_from(raw, 64 + animation_id * 8)
        ident, count, frame_ms, first, reserved = record
        ensure(ident == animation_id and reserved == 0, 'DVA animation ID/reserved')
        ensure(1 <= count <= 8 and 40 <= frame_ms <= 2000, 'DVA animation bounds')
        ensure(first == next_frame and first + count <= frame_count, 'DVA frame range')
        next_frame += count
        clips.append(dict(id=ident, name=ANIMATIONS[ident], frames=count, frameMs=frame_ms, firstFrame=first))
    ensure(next_frame == frame_count, 'DVA unused frames')
    return dict(width=width, height=height, version=version, palette=palette, animations=clips,
                frames=frame_count, frameBytes=frame_bytes, frameOffset=112)


def encode_dva(width, height, palette, clips, version=1):
    ensure(len(palette) == 16 and all(type(value) is int and 0 <= value <= 65535 for value in palette), 'RGB565 palette')
    table, frames = bytearray(), bytearray()
    frame_count = 0
    for ident, name in enumerate(ANIMATIONS):
        frame_ms, sequence = clips[name]
        ensure(1 <= len(sequence) <= 8, 'Source frame count')
        table.extend(ANIMATION.pack(ident, len(sequence), frame_ms, frame_count, 0))
        for frame in sequence:
            ensure(len(frame) == width * height // 2, 'Source frame length')
            frames.extend(frame)
        frame_count += len(sequence)
    payload = struct.pack('<16H', *palette) + table + frames
    raw = HEADER.pack(b'DVA1', 1, 32, version, width, height, 16, 6, 0, 0,
                      frame_count, width * height // 2, len(payload), zlib.crc32(payload)) + payload
    parse_dva(raw)
    return raw


def downsample_frame(frame):
    """A deterministic 2x2 index vote; ties prefer an existing opaque outline.

    Palette and transparency indices remain unchanged; fine detail can be lost.
    The source remains the original 32px project drawing, not an imported model.
    """
    ensure(len(frame) == 512, 'Fallback requires 32px source')
    pixels = [index for byte in frame for index in (byte >> 4, byte & 15)]
    output = []
    for y in range(16):
        for x in range(16):
            block = [pixels[(2*y + dy)*32 + 2*x + dx] for dy in (0, 1) for dx in (0, 1)]
            output.append(max(set(block), key=lambda index: (block.count(index), index != 0, -index)))
    return bytes((output[i] << 4) | output[i+1] for i in range(0, len(output), 2))


def read_sprites():
    sprites, inputs = {}, {}
    for pack_id in ('starter-v2', 'tide-v1', 'ember-v1'):
        path = PACKS / f'{pack_id}.json'
        raw = path.read_bytes()
        pack = json.loads(raw)
        ensure(pack.get('license') == 'CC0-1.0' and pack.get('formatVersion') == 1, 'Only original project sprite packs are accepted')
        inputs[str(path.relative_to(ROOT))] = sha256(raw)
        for ident, source in pack['sprites'].items():
            ensure(ident not in sprites and source['width'] == source['height'] == 32, 'Duplicate/non32px creature')
            clips = {}
            for name in ANIMATIONS:
                clip = source['animations'][name]
                sequence = [base64.b64decode(value, validate=True) for value in clip['frames']]
                ensure(all(base64.b64encode(frame).decode('ascii') == value for frame, value in zip(sequence, clip['frames'])), 'Noncanonical source base64')
                clips[name] = (clip['frameMs'], sequence)
            sprites[ident] = dict(palette=source['palette'], clips=clips, name=source['name'])
    ensure(len(sprites) == 10, 'Expected ten original creatures')
    return sprites, inputs


def fallback_header(blobs):
    lines = ['// Generated by scripts/build-device-assets.py. Original CC0 project art.',
             '// Encoded DVA1 blobs reside in read-only storage; decode one frame at a time.',
             '#pragma once', '#include <cstddef>', '#include <cstdint>', '',
             'namespace digivice::runtime::fallback {']
    for ident, raw in sorted(blobs.items()):
        lines.append(f'alignas(4) inline constexpr std::uint8_t {ident}[] = {{')
        for offset in range(0, len(raw), 16):
            lines.append('    ' + ', '.join(f'0x{value:02x}' for value in raw[offset:offset+16]) + ',')
        lines.extend(['};', f'inline constexpr std::size_t {ident}_bytes = sizeof({ident});', ''])
    lines.extend(['inline constexpr std::size_t resident_bytes = sizeof(mote) + sizeof(flicker);',
                  'static_assert(resident_bytes < 8192, "Resident fallback must stay below 8 KiB");',
                  '} // namespace digivice::runtime::fallback', ''])
    return '\n'.join(lines).encode('ascii')


def jpeg_dimensions(raw):
    ensure(4 < len(raw) <= MAX_BLOB and raw[:2] == b'\xff\xd8' and raw[-2:] == b'\xff\xd9', 'JPEG envelope')
    offset, dimensions = 2, None
    while offset < len(raw):
        ensure(raw[offset] == 0xff, 'JPEG marker')
        while offset < len(raw) and raw[offset] == 0xff:
            offset += 1
        ensure(offset < len(raw), 'JPEG marker truncation')
        marker = raw[offset]; offset += 1
        if marker == 0xda:
            break
        ensure(marker not in (0xd8, 0xd9, 0x00) and offset + 2 <= len(raw), 'JPEG unexpected marker')
        size = int.from_bytes(raw[offset:offset+2], 'big')
        ensure(size >= 2 and offset + size <= len(raw), 'JPEG segment bound')
        if marker in (0xc0, 0xc1, 0xc2):
            ensure(marker == 0xc0 and dimensions is None and size == 17, 'JPEG must be baseline 3-component')
            segment = raw[offset+2:offset+size]
            ensure(segment[0] == 8 and segment[5] == 3, 'JPEG precision/components')
            dimensions = (int.from_bytes(segment[3:5], 'big'), int.from_bytes(segment[1:3], 'big'))
        offset += size
    ensure(dimensions is not None, 'JPEG missing baseline SOF')
    return dimensions


def build(include_test_fixtures=False):
    try:
        from PIL import Image, __version__ as pillow_version
    except ImportError as error:
        raise RuntimeError('JPEG build/check needs existing Pillow. See assets/device/FORMAT.md; --self-test uses only Python.') from error
    sprites, inputs = read_sprites() if include_test_fixtures else ({}, {})
    outputs, entries, measurements, fallback = {}, [], {}, {}
    def add(ident, kind, width, height, raw, extension):
        ensure(len(raw) <= MAX_BLOB, f'{ident} exceeds 128 KiB')
        file = f'{ident}.{extension}'
        outputs[file] = raw
        entries.append(dict(id=ident, version=1, bytes=len(raw), sha256=sha256(raw), kind=kind, width=width, height=height, file=file))
    for ident, source in sorted(sprites.items()):
        raw = encode_dva(32, 32, source['palette'], source['clips'])
        add(f'sprite-{ident}-v1', 'sprite', 32, 32, raw, 'dva')
        parsed = parse_dva(raw)
        measurements[ident] = dict(encodedBytes=len(raw), frameBytes=parsed['frameBytes'], frames=parsed['frames'],
                                   pixels=32*32*parsed['frames'], animations=parsed['animations'])
        if ident in ('mote', 'flicker'):
            clips = {name: (source['clips'][name][0], [downsample_frame(frame) for frame in source['clips'][name][1][:FALLBACK_COUNTS[index]]])
                     for index, name in enumerate(ANIMATIONS)}
            fallback[ident] = encode_dva(16, 16, source['palette'], clips)
    for scene in SCENES:
        path = PACKS / f'scene-{scene}-v1.json'
        source_raw = path.read_bytes(); source = json.loads(source_raw)
        ensure(source.get('formatVersion') == 3 and source.get('provenance', {}).get('source') == 'approved-generated-background', 'Only approved scenery accepted')
        ensure(source['background']['sceneId'] == scene, 'Background ID mismatch')
        encoded = base64.b64decode(source['background']['data'], validate=True)
        ensure(len(encoded) <= MAX_BLOB, 'Source JPEG bound')
        with Image.open(io.BytesIO(encoded)) as original:
            ensure(original.size == (480, 480) and original.format == 'JPEG', 'Expected current 480px approved scenery')
            image = original.convert('RGB').resize((NATIVE_SIZE, NATIVE_SIZE), Image.Resampling.LANCZOS)
            buffer = io.BytesIO()
            image.save(buffer, format='JPEG', quality=82, subsampling=2, optimize=True, progressive=False)
        raw = buffer.getvalue()
        ensure(jpeg_dimensions(raw) == (NATIVE_SIZE, NATIVE_SIZE), 'JPEG native dimensions')
        add(f'scene-{scene}-412-v1', 'background', NATIVE_SIZE, NATIVE_SIZE, raw, 'jpg')
        inputs[str(path.relative_to(ROOT))] = sha256(source_raw)
    entries.sort(key=lambda entry: entry['id'])
    index = canonical(dict(formatVersion=1, release=1, profile='s3-146-v1', packs=entries))
    ensure(len(index) <= 8192, 'Unsigned index exceeds 8 KiB')
    outputs['index.json'] = index
    resident = sum(map(len, fallback.values()))
    total = sum(entry['bytes'] for entry in entries)
    outputs['budget.json'] = canonical(dict(formatVersion=1, measured=dict(
        encodedAssetBytes=total, spriteBytes=sum(entry['bytes'] for entry in entries if entry['kind'] == 'sprite'),
        backgroundBytes=sum(entry['bytes'] for entry in entries if entry['kind'] == 'background'),
        largestBlobBytes=max(entry['bytes'] for entry in entries), indexBytes=len(index), residentFallbackBytes=resident,
        fallback={ident: dict(bytes=len(raw), frames=parse_dva(raw)['frames'], width=16, height=16, sha256=sha256(raw)) for ident, raw in fallback.items()},
        sprites=measurements), assumed=dict(
        normal=dict(psramBytes=8*1024*1024, encodedCacheRetainedSlots=4, encodedCacheAtomicSpareSlots=1,
                    encodedCacheMaxBytes=5*MAX_BLOB, downloadChunkBytes=4096,
                    displayStripBytes=2*NATIVE_SIZE*48*2, spriteOneFrameEachRGB565Bytes=2*32*32*2,
                    spriteFourFrameClipsEachRGB565Bytes=2*4*32*32*2,
                    optionalFullSceneRGB565Bytes=NATIVE_SIZE*NATIVE_SIZE*2,
                    optionalFullDisplayRGB565Bytes=NATIVE_SIZE*NATIVE_SIZE*2),
        lowMemory=dict(psramBytes=2*1024*1024, backgroundDecode=False, spriteMode='selected creature only or identity-preserving missing-art placeholder',
                       spriteOneFrameEachRGB565Bytes=2*16*16*2, encodedCacheReservation='must be reduced by runtime profile; do not reserve normal five-slot maximum'),
        decoderScratchBytes='unmeasured; JPEG decoder and BSP dependent', networkTLSAndSystemHeapBytes='unmeasured; excluded',
        notes=['Encoded content is flash/download storage, not simultaneous decoded RAM.',
               'Normal strip budget is provisional until QSPI DMA and RGB565 byte order are bench verified.',
               'Optional full scene and full display buffers are separate alternatives; do not silently allocate both.',
               'No microSD is needed for current asset bytes alone; firmware, OTA slots and partitions still require a flash plan.',
               'No RAM, FPS or power measurement was made on physical hardware.'])))
    outputs['sources.json'] = canonical(dict(formatVersion=1, inputs=inputs, spriteLicense='CC0-1.0',
        backgroundLicense='LicenseRef-User-Approved-Art', backgroundSource='approved browser format3 packs only',
        display=dict(board='Waveshare ESP32-S3-Touch-LCD-1.46', sku=29565, width=NATIVE_SIZE, height=NATIVE_SIZE,
                     reference='https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46'),
        builder=dict(pillow=pillow_version, resize='LANCZOS', jpeg=dict(quality=82, subsampling='4:2:0', progressive=False, optimize=True)),
        immutableVersionRule='Any changed device blob requires a new asset version and signed catalog release.'))
    return outputs, fallback_header(fallback) if include_test_fixtures else None


def self_test():
    palette = list(range(16))
    clips = {name: (180, [bytes([0x12])*512]) for name in ANIMATIONS}
    valid = encode_dva(32, 32, palette, clips)
    ensure(parse_dva(valid)['frames'] == 6, 'Valid sample')
    ensure(downsample_frame(bytes([0x11])*512) == bytes([0x11])*128, 'Uniform fallback vote')
    cases = []
    def changed(offset, value, update_crc=False):
        raw = bytearray(valid); raw[offset] = value
        if update_crc: struct.pack_into('<I', raw, 28, zlib.crc32(raw[32:]))
        cases.append(bytes(raw))
    for offset, value in ((0, 0), (4, 2), (6, 31), (12, 33), (16, 17), (17, 7), (18, 1), (19, 1), (20, 49), (22, 1), (24, 0)):
        changed(offset, value)
    changed(len(valid)-1, 0xff)  # Corrupt payload CRC.
    changed(64, 5, True)        # Duplicate/out-of-order animation ID.
    changed(65, 9, True)        # Too many frames.
    changed(66, 1, True)        # Too-fast animation interval.
    changed(68, 2, True)        # Out-of-range/noncontiguous frame section.
    changed(70, 1, True)        # Nonzero reserved table field.
    cases.extend((valid[:-1], valid + b'\x00', b'DVA1', b'\x00'*(MAX_BLOB+1)))
    for number, raw in enumerate(cases):
        try: parse_dva(raw)
        except ValueError: continue
        raise AssertionError(f'Malformed DVA case {number} accepted')
    print(f'PASS: bounded DVA parser, fallback voting, {len(cases)} malformed/truncated cases')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--include-test-fixtures', action='store_true', help='Also generate legacy toy sprites for isolated tests; excluded by default')
    parser.add_argument('--check', action='store_true', help='Regenerate in memory and require byte-identical existing output; never write')
    parser.add_argument('--self-test', action='store_true', help='Run standard-library format and corruption tests only')
    parser.add_argument('--out-dir', type=Path, default=ROOT / 'assets' / 'device')
    parser.add_argument('--fallback-header', type=Path, help='Explicit test-only header destination; never changes the release fallback implicitly')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.fallback_header and not args.include_test_fixtures:
        parser.error('--fallback-header requires --include-test-fixtures')
    if args.fallback_header and args.fallback_header.resolve() == ROOT / 'firmware' / 'runtime' / 'fallback_asset.hpp':
        parser.error('Test creature headers cannot replace the release fallback')
    outputs, header = build(args.include_test_fixtures)
    files = {args.out_dir / name: raw for name, raw in outputs.items()}
    if args.fallback_header:
        files[args.fallback_header] = header
    if args.check:
        differences = [str(path) for path, raw in files.items() if not path.is_file() or path.read_bytes() != raw]
        ensure(not differences, 'Generated output differs: ' + ', '.join(differences))
        print(f'PASS: {len(files)} generated files byte-identical; browser packs unchanged')
    else:
        for path, raw in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        budget = json.loads(outputs['budget.json'])['measured']
        print(f'Built {len(json.loads(outputs["index.json"])["packs"])} device blobs: {budget["encodedAssetBytes"]} bytes; resident fallback {budget["residentFallbackBytes"]} bytes; largest blob {budget["largestBlobBytes"]} bytes')


if __name__ == '__main__':
    try: main()
    except (ValueError, RuntimeError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        raise SystemExit(1)

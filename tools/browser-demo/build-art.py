#!/usr/bin/env python3
"""Extract browser atlases from the exact locally installed DVA game artwork.

Requires the separately supplied, authorized source artwork. No downloading,
device access or publication occurs. The output contains only compact rendered
frames, scene JPEGs and a public manifest, never raw sheets or private packs.
Uses Python's standard library; PNG palette entries preserve the RGB565 colors.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zlib

SOURCE_COMMIT = '171cda7e698cf2915b50aa46bc766d8d1d0ee50d'
SCENES = ('meadow', 'forest', 'beach', 'ruins', 'cavern', 'snow', 'volcanic', 'digital')
ANIMATIONS = ('idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate')
INCLUDED = ('idle',)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked(directory, descriptor):
    path = directory / descriptor['file']
    if path.resolve() != path.absolute() or not path.resolve().is_relative_to(directory.resolve()):
        raise ValueError('Artwork source must be a regular nonsymlinked local file')
    data = path.read_bytes()
    if len(data) != descriptor['bytes'] or digest(data) != descriptor['sha256']:
        raise ValueError('Artwork no longer matches the audited source index')
    return data


def rgb565(value):
    r, g, b = value >> 11, (value >> 5) & 63, value & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def png_chunk(name, value):
    return struct.pack('>I', len(value)) + name + value + struct.pack('>I', zlib.crc32(name + value))


def indexed_png(width, height, pixels, palette):
    if len(pixels) != width * height:
        raise ValueError('Invalid atlas dimensions')
    rows = b''.join(b'\0' + pixels[y * width:(y + 1) * width] for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + b''.join([
        png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 3, 0, 0, 0)),
        png_chunk(b'PLTE', bytes(channel for color in palette for channel in rgb565(color))),
        png_chunk(b'tRNS', b'\0' + b'\xff' * 15),
        png_chunk(b'IDAT', zlib.compress(rows, 9)), png_chunk(b'IEND', b'')])


def decode_dva(data):
    if len(data) < 112 or data[:4] != b'DVA1':
        raise ValueError('Expected installed DVA1 artwork')
    _, version, header, asset_version, width, height, colors, count, transparent, reserved, frames, stride, length, crc = struct.unpack_from('<4sHHIHHBBBBHHII', data)
    if (version, header, width, height, colors, count, transparent, reserved) != (1, 32, 32, 32, 16, 6, 0, 0):
        raise ValueError('Unsupported DVA header')
    if not asset_version or not 6 <= frames <= 48 or stride != 512 or length != len(data) - 32 or length != 80 + frames * stride or zlib.crc32(data[32:]) != crc:
        raise ValueError('DVA length or CRC failed')
    palette = struct.unpack_from('<16H', data, 32)
    decoded = []
    for frame in range(frames):
        packed = data[112 + frame * stride:112 + (frame + 1) * stride]
        decoded.append(bytes(value for byte in packed for value in (byte >> 4, byte & 15)))
    clips, next_frame = {}, 0
    for index, name in enumerate(ANIMATIONS):
        ident, size, frame_ms, start, zero = struct.unpack_from('<BBHHH', data, 64 + index * 8)
        if ident != index or zero or not 1 <= size <= 8 or not 40 <= frame_ms <= 2000 or start != next_frame or start + size > frames:
            raise ValueError('Invalid DVA animation table')
        next_frame += size
        if name in INCLUDED:
            images = decoded[start:start + size]
            points = [(i % width, i // width) for image in images for i, value in enumerate(image) if value]
            if not points:
                raise ValueError('An included artwork clip is empty')
            left, right = min(x for x, _ in points), max(x for x, _ in points)
            top, bottom = min(y for _, y in points), max(y for _, y in points)
            clips[name] = {'frameMs': frame_ms, 'images': images, 'bounds': [left, top, right - left + 1, bottom - top + 1]}
    if next_frame != frames:
        raise ValueError('Unreferenced DVA frames')
    unique = list(dict.fromkeys(image for clip in clips.values() for image in clip['images']))
    for clip in clips.values():
        clip['frames'] = [unique.index(image) for image in clip.pop('images')]
    atlas = b''.join(b''.join(image[y * width:(y + 1) * width] for image in unique) for y in range(height))
    return indexed_png(width * len(unique), height, atlas, palette), clips, len(unique)


def git_blob(root, name):
    return subprocess.check_output(['git', 'show', f'{SOURCE_COMMIT}:{name}'], cwd=root)


def facing_map(source):
    # Read the installed audit rather than inferring direction from anatomy.
    right, front, left = set(), set(), set()
    for line in source.splitlines():
        ids = map(int, re.findall(r'case (\d+):', line))
        if 'return Facing::Right' in line:
            right.update(ids)
        elif 'return Facing::Front' in line:
            front.update(ids)
    ranges = source.split('constexpr Range left[]{', 1)[1].split('};', 1)[0]
    for begin, end in re.findall(r'\{(\d+),(\d+)\}', ranges):
        left.update(range(int(begin), int(end) + 1))
    return lambda ident: 'right' if ident in right else 'front' if ident in front else 'left' if ident in left else 'unknown'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Local game source with its audited artwork and staged SD index')
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2] / 'docs/play/art')
    args = parser.parse_args()
    root, output = args.source.resolve(), args.output.resolve()
    private = root / '.personal-assets/world-ds'
    index = json.loads((private / 'index.json').read_bytes())
    staged = json.loads((private / 'sd-card/INDEX.JSON').read_bytes())
    by_id = {row['formId']: row for row in staged['entries']}
    catalog = json.loads(git_blob(root, 'data/world-ds-runtime.json'))
    # Production IDs include the preserved starter forms absent from the DS
    # source inventory; IDs1..10 are decode-only original fixtures.
    production = {row['formId']: row['name'] for row in catalog['forms'] if 11 <= row['formId'] <= 276}
    facing_bytes = git_blob(root, 'firmware/runtime/local_form_facing.hpp')
    facing = facing_map(facing_bytes.decode())
    sprites = output / 'forms'
    backgrounds = output / 'scenes'
    sprites.mkdir(parents=True, exist_ok=True)
    backgrounds.mkdir(parents=True, exist_ok=True)
    forms = []
    for entry in sorted(index['entries'], key=lambda value: int(value['artId'][8:])):
        ident = int(entry['artId'][8:])
        if ident not in production:
            continue
        data = checked(private, entry['device'])
        stage = by_id[ident]
        if stage['sha256'] != digest(data) or stage['bytes'] != len(data) or (private / 'sd-card' / stage['file']).read_bytes() != data:
            raise ValueError(f'Form {ident} differs from the installed SD staging source')
        png, clips, count = decode_dva(data)
        filename = f'forms/{ident}.png'
        (output / filename).write_bytes(png)
        forms.append({'id': ident, 'name': entry['name'], 'file': filename, 'bytes': len(png), 'sha256': digest(png),
                      'sourceDvaSha256': digest(data), 'nativeFacing': facing(ident), 'frameWidth': 32, 'frameHeight': 32,
                      'atlasFrames': count, 'animations': clips})
    scenes = []
    for name in SCENES:
        source = f'assets/device/scene-{name}-412-v1.jpg'
        data = git_blob(root, source)
        if (root / source).read_bytes() != data:
            raise ValueError(f'Scene {name} differs from installed firmware baseline')
        filename = f'scenes/{name}.jpg'
        (output / filename).write_bytes(data)
        scenes.append({'id': name, 'file': filename, 'bytes': len(data), 'sha256': digest(data), 'source': source, 'width': 412, 'height': 412})
    included = {form['id'] for form in forms}
    manifest = {'formatVersion': 1, 'sourceCommit': SOURCE_COMMIT,
                'pixelSource': 'Exact installed 32px DVA frames; original palette, transparency, poses and timing.',
                'framingSource': 'firmware/runtime/device_ui.cpp and sprite_bounds.hpp at sourceCommit',
                'facingSource': 'firmware/runtime/local_form_facing.hpp at sourceCommit', 'facingSourceSha256': digest(facing_bytes),
                'rights': 'These third-party assets receive no project-wide or open-content license from this demo. Exact-form attribution is in attribution.json. No underlying franchise or redistribution license is granted.',
                'forms': forms, 'missingForms': [{'id': ident, 'name': production[ident]} for ident in sorted(production.keys() - included)],
                'scenes': scenes}
    (output / 'manifest.json').write_text(json.dumps(manifest, separators=(',', ':'), ensure_ascii=False) + '\n')
    result = {'forms': len(forms), 'missingForms': len(manifest['missingForms']), 'scenes': len(scenes),
              'uniqueSpriteFrames': sum(form['atlasFrames'] for form in forms), 'atlasBytes': sum(form['bytes'] for form in forms),
              'backgroundBytes': sum(scene['bytes'] for scene in scenes), 'manifestBytes': (output / 'manifest.json').stat().st_size}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

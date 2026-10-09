#!/usr/bin/env python3
"""Convert an explicitly mapped local PNG sheet to a private indexed sprite pack.

No network, catalog publication, inferred poses, or implicit background removal.
Pillow reads/crops PNGs; deterministic quantization below does not use its palette
optimizer. Output is restricted to the repository's ignored .personal-assets.
"""
import argparse
import base64
from collections import Counter
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

sys.dont_write_bytecode = True
from urllib.parse import urlsplit

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ANIMATIONS = ('idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate')
LICENSE = 'LicenseRef-Personal-Use-Restrictions'
MAX_IMAGE_BYTES = 16 * 1024 * 1024
MAX_MANIFEST_BYTES = 128 * 1024
MAX_PACK_BYTES = 256 * 1024
MAX_SIDECAR_BYTES = 64 * 1024
MAX_DECODED_BYTES = 2 * 1024 * 1024
ID = re.compile(r'^(?!constructor$|prototype$)[a-z][a-z0-9-]{0,47}$')


class ImportFailure(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ImportFailure(message)


def integer(value, minimum, maximum):
    return type(value) is int and minimum <= value <= maximum


def fields(value, required, optional=()):
    return isinstance(value, dict) and set(required) <= value.keys() <= set(required) | set(optional)


def text(value, maximum):
    return (isinstance(value, str) and bool(value.strip()) and
            len(value.encode('utf-16-le', errors='surrogatepass')) // 2 <= maximum and
            not any(ord(c) < 32 or 0xD800 <= ord(c) <= 0xDFFF for c in value))


def canonical(value):
    return (json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(',', ':')) + '\n').encode('ascii')


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f'Duplicate JSON field: {key}')
        result[key] = value
    return result


def provenance(value):
    require(fields(value, ('creator', 'sourceUrl', 'game', 'rights', 'reuseScope')), 'Provenance requires creator/sourceUrl/game/rights/reuseScope')
    for key, limit in [('creator', 256), ('sourceUrl', 2048), ('game', 128), ('rights', 2048), ('reuseScope', 1024)]:
        require(text(value[key], limit), f'Invalid provenance {key}')
    try:
        url = urlsplit(value['sourceUrl'])
        hostname = url.hostname
    except ValueError as error:
        raise ImportFailure('Source URL must be a valid HTTP(S) reference') from error
    require(url.scheme in ('http', 'https') and hostname and not url.username and not url.password, 'Source URL must be an HTTP(S) reference without credentials')
    return dict(value)


def validate_manifest(manifest):
    require(fields(manifest, ('formatVersion', 'packId', 'version', 'provenance', 'sprites'), ('frameSize', 'alphaThreshold')), 'Unknown or missing manifest field')
    require(manifest['formatVersion'] == 1 and type(manifest['formatVersion']) is int, 'Unsupported manifest format')
    require(isinstance(manifest['packId'], str) and re.fullmatch(r'personal-[a-z0-9][a-z0-9-]{0,38}', manifest['packId']), 'Pack ID must use the personal- prefix')
    require(integer(manifest['version'], 1, 2**31 - 1), 'Invalid pack version')
    frame_size = manifest.get('frameSize', 32)
    require(type(frame_size) is int and frame_size in (32, 64), 'frameSize must be 32 or 64')
    threshold = manifest.get('alphaThreshold', 128)
    require(integer(threshold, 1, 255), 'alphaThreshold must be 1..255')
    provenance(manifest['provenance'])
    sprites = manifest['sprites']
    require(isinstance(sprites, dict) and 1 <= len(sprites) <= 32, 'Expected 1..32 creatures')
    frame_count = 0
    for sprite_id, sprite in sprites.items():
        require(ID.fullmatch(sprite_id), f'Invalid sprite ID: {sprite_id}')
        require(fields(sprite, ('name', 'family', 'stage', 'source', 'animations'), ('resize', 'transparentColor', 'provenance', 'anchor')), f'Unknown or missing {sprite_id} field')
        require(text(sprite['name'], 64) and text(sprite['family'], 48) and integer(sprite['stage'], 0, 16), f'Invalid identity for {sprite_id}')
        require(text(sprite['source'], 512), f'Invalid source path for {sprite_id}')
        require(sprite.get('resize', 'none') in ('none', 'nearest'), 'resize must be none or nearest')
        require(sprite.get('anchor', 'bottom-center') in ('bottom-center', 'center'), 'anchor must be bottom-center or center')
        if 'transparentColor' in sprite:
            require(isinstance(sprite['transparentColor'], str) and re.fullmatch(r'#[0-9A-Fa-f]{6}', sprite['transparentColor']), 'transparentColor must be explicit #RRGGBB')
        if 'provenance' in sprite:
            provenance(sprite['provenance'])
        animations = sprite['animations']
        require(isinstance(animations, dict) and set(animations) == set(ANIMATIONS), f'{sprite_id} must explicitly map all six animation states')
        for name in ANIMATIONS:
            animation = animations[name]
            if fields(animation, ('fallback',)):
                require(name != 'idle' and animation['fallback'] == 'idle', 'Only an explicit non-idle fallback to idle is supported')
                continue
            require(fields(animation, ('frameMs', 'rects'), ('sourceDescription',)), f'Invalid {sprite_id}/{name} mapping')
            require(integer(animation['frameMs'], 40, 2000), 'frameMs must be 40..2000')
            if 'sourceDescription' in animation:
                require(text(animation['sourceDescription'], 256), 'sourceDescription must be a short source-pose description')
            require(isinstance(animation['rects'], list) and 1 <= len(animation['rects']) <= 8, 'An animation must map 1..8 source frames')
            for rect in animation['rects']:
                require(isinstance(rect, list) and len(rect) == 4 and all(type(n) is int for n in rect), 'Frame rectangle must be [x,y,width,height] integers')
                x, y, width, height = rect
                require(0 <= x < 4096 and 0 <= y < 4096 and 1 <= width <= 256 and 1 <= height <= 256, 'Source rectangle exceeds bounds')
                require(max(width, height) <= frame_size or sprite.get('resize', 'none') == 'nearest', f'{sprite_id}/{name} needs larger frameSize or explicit resize:nearest')
        idle = animations['idle']
        require('rects' in idle, 'Idle must map genuine source frames')
        frame_count += sum(len(animation.get('rects', idle['rects'])) for animation in animations.values())
    require(frame_count * frame_size * frame_size * 4 <= MAX_DECODED_BYTES, 'Pack exceeds 2 MiB all-frame RGBA bound')
    return frame_size, threshold


def source_path(base, relative):
    raw = Path(relative)
    require(not raw.is_absolute() and '..' not in raw.parts and raw.suffix.lower() == '.png', 'PNG sources must stay beneath the manifest directory')
    path = (base / raw).resolve()
    require(path.is_relative_to(base.resolve()), 'Source symlink escapes the manifest directory')
    require(path.is_file(), f'Source PNG not found: {relative}')
    return path


def load_png(base, relative):
    path = source_path(base, relative)
    require(0 < path.stat().st_size <= MAX_IMAGE_BYTES, 'PNG exceeds 16 MiB input limit')
    with path.open('rb') as handle:
        data = handle.read(MAX_IMAGE_BYTES + 1)
    require(0 < len(data) <= MAX_IMAGE_BYTES, 'PNG changed or exceeds byte limit')
    try:
        with Image.open(io.BytesIO(data), formats=('PNG',)) as image:
            require(image.format == 'PNG' and getattr(image, 'n_frames', 1) == 1, 'Only a static PNG sprite sheet is accepted')
            require(1 <= image.width <= 4096 and 1 <= image.height <= 4096, 'PNG dimensions exceed 4096 × 4096')
            image.load()
            rgba = image.convert('RGBA')
    except (OSError, Image.DecompressionBombError) as error:
        raise ImportFailure(f'Invalid PNG: {relative}') from error
    return rgba, dict(path=relative, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), width=rgba.width, height=rgba.height)


def rgb565(rgb):
    red, green, blue = rgb
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def unpack(color):
    red, green, blue = (color >> 11) & 31, (color >> 5) & 63, color & 31
    return ((red << 3) | (red >> 2), (green << 2) | (green >> 4), (blue << 3) | (blue >> 2))


def flattened(image):
    # Pillow 12.3 replaces getdata; retain compatibility with existing older installations.
    method = getattr(image, 'get_flattened_data', None)
    return method() if method else image.getdata()


def crop_frame(source, rect, frame_size, threshold, color_key, resize):
    x, y, width, height = rect
    require(x + width <= source.width and y + height <= source.height, 'Frame rectangle falls outside its PNG')
    crop = source.crop((x, y, x + width, y + height))
    pixels = [(r, g, b, 255) if a >= threshold and (r, g, b) != color_key else (0, 0, 0, 0) for r, g, b, a in flattened(crop)]
    crop.putdata(pixels)
    require(any(pixel[3] for pixel in pixels), 'Mapped source frame is entirely transparent after masking')
    factor = 1
    padded_size = crop.size
    if max(width, height) > frame_size:
        require(resize == 'nearest', 'Oversized source frame needs explicit nearest resizing')
        factor = math.ceil(max(width, height) / frame_size)
        padded_size = (math.ceil(width / factor) * factor, math.ceil(height / factor) * factor)
        padded = Image.new('RGBA', padded_size)
        padded.paste(crop, (0, 0))
        crop = padded.resize((padded_size[0] // factor, padded_size[1] // factor), Image.Resampling.NEAREST)
    offset = ((frame_size - crop.width) // 2, (frame_size - crop.height) // 2)
    output = Image.new('RGBA', (frame_size, frame_size))
    output.paste(crop, offset)
    require(output.getbbox() is not None, 'Nearest downsampling removed all visible pixels')
    return output, dict(sourceSize=[width, height], paddedSourceSize=list(padded_size), downsampleFactor=factor,
                        contentSize=list(crop.size), offset=list(offset), frameSize=frame_size)


def anchor_frames(frames, coverage, frame_size, anchor):
    """Move every pose together using one common alpha union, never per-frame trim."""
    bounds = [frame.getbbox() for sequence in frames.values() for frame in sequence]
    union = (min(box[0] for box in bounds), min(box[1] for box in bounds),
             max(box[2] for box in bounds), max(box[3] for box in bounds))
    width, height = union[2] - union[0], union[3] - union[1]
    shift = (((frame_size - width) // 2 - union[0], frame_size - union[3])
             if anchor == 'bottom-center' else (0, 0))
    for name, sequence in frames.items():
        for index, frame in enumerate(sequence):
            if anchor == 'bottom-center':
                cropped = frame.crop(union)
                aligned = Image.new('RGBA', (frame_size, frame_size))
                aligned.paste(cropped, ((frame_size - width) // 2, frame_size - height))
                cropped.close()
                frame.close()
                sequence[index] = aligned
            transform = coverage[name]['transforms'][index]
            transform.update(anchor=anchor, unionAlphaBounds=list(union), sharedShift=list(shift),
                             preAnchorOffset=transform['offset'])
            transform['offset'] = [transform['offset'][0] + shift[0], transform['offset'][1] + shift[1]]


def quantize_palette(histogram):
    """Weighted median cut over RGB565 colors with deterministic tie breaking."""
    require(histogram, 'Creature has no opaque pixels')
    if len(histogram) <= 15:
        return sorted(histogram)
    boxes = [sorted(histogram)]
    while len(boxes) < 15:
        choices = []
        for index, box in enumerate(boxes):
            if len(box) < 2:
                continue
            channels = list(zip(*(unpack(color) for color in box)))
            ranges = [max(channel) - min(channel) for channel in channels]
            axis = max(range(3), key=lambda channel: (ranges[channel], -channel))
            weight = sum(histogram[color] for color in box)
            choices.append((ranges[axis] * weight, -min(box), index, axis))
        if not choices:
            break
        _, _, index, axis = max(choices)
        ordered = sorted(boxes.pop(index), key=lambda color: (unpack(color)[axis], color))
        total = sum(histogram[color] for color in ordered)
        cumulative, boundary = 0, 1
        for cursor, color in enumerate(ordered[:-1], 1):
            cumulative += histogram[color]
            boundary = cursor
            if cumulative * 2 >= total:
                break
        boxes.extend((ordered[:boundary], ordered[boundary:]))
    palette = []
    for box in boxes:
        total = sum(histogram[color] for color in box)
        average = tuple((sum(unpack(color)[axis] * histogram[color] for color in box) * 2 + total) // (2 * total) for axis in range(3))
        palette.append(rgb565(average))
    return sorted(set(palette))


def encode_frames(real_frames):
    histogram = Counter(rgb565(pixel[:3]) for frames in real_frames.values() for image in frames for pixel in flattened(image) if pixel[3])
    colors = quantize_palette(histogram)
    palette = [0] + colors + [0] * (15 - len(colors))
    nearest = {}
    error = 0
    for color, count in histogram.items():
        rgb = unpack(color)
        distance, index = min((sum((a - b)**2 for a, b in zip(rgb, unpack(candidate))), position + 1) for position, candidate in enumerate(colors))
        nearest[color] = index
        error += distance * count
    encoded = {}
    for name, frames in real_frames.items():
        encoded[name] = []
        for image in frames:
            indexes = [nearest[rgb565(pixel[:3])] if pixel[3] else 0 for pixel in flattened(image)]
            packed = bytes((indexes[index] << 4) | indexes[index + 1] for index in range(0, len(indexes), 2))
            encoded[name].append(base64.b64encode(packed).decode('ascii'))
    return palette, encoded, dict(sourceOpaqueRgb565Colors=len(histogram), paletteOpaqueColors=len(colors),
        quantized=len(histogram) > 15, meanSquaredRgbError=round(error / sum(histogram.values()), 4))


def build_import(manifest_path):
    manifest_path = Path(manifest_path).resolve()
    require(manifest_path.is_file() and manifest_path.stat().st_size <= MAX_MANIFEST_BYTES, 'Manifest missing or exceeds 128 KiB')
    with manifest_path.open('rb') as handle:
        manifest_bytes = handle.read(MAX_MANIFEST_BYTES + 1)
    require(len(manifest_bytes) <= MAX_MANIFEST_BYTES, 'Manifest changed or exceeds byte limit')
    try:
        manifest = json.loads(manifest_bytes, object_pairs_hook=unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ImportFailure('Manifest must be UTF-8 JSON') from error
    frame_size, threshold = validate_manifest(manifest)
    pack = dict(formatVersion=1, packId=manifest['packId'], version=manifest['version'], license=LICENSE,
                paletteEncoding='rgb565', sprites={}, effects={}, icons={})
    coverage, sources, color_reports = {}, [], {}
    for sprite_id in sorted(manifest['sprites']):
        specification = manifest['sprites'][sprite_id]
        source, evidence = load_png(manifest_path.parent, specification['source'])
        evidence.update(spriteId=sprite_id, provenance=provenance(specification.get('provenance', manifest['provenance'])))
        sources.append(evidence)
        key = specification.get('transparentColor')
        color_key = tuple(int(key[index:index + 2], 16) for index in (1, 3, 5)) if key else None
        frames, sprite_coverage = {}, {}
        try:
            for name in ANIMATIONS:
                animation = specification['animations'][name]
                if 'fallback' in animation:
                    continue
                frames[name], transforms = [], []
                for rect in animation['rects']:
                    frame, transform = crop_frame(source, rect, frame_size, threshold, color_key, specification.get('resize', 'none'))
                    frames[name].append(frame)
                    transforms.append(transform)
                sprite_coverage[name] = dict(kind='genuine', sourceAnimation=name, originalFrameCount=len(animation['rects']),
                    outputFrameCount=len(frames[name]), sourceRects=animation['rects'], transforms=transforms,
                    sourceDescription=animation.get('sourceDescription', 'Source rectangles mapped by the user; original pose semantics not independently inferred'))
            anchor_frames(frames, sprite_coverage, frame_size, specification.get('anchor', 'bottom-center'))
            palette, encoded, color_reports[sprite_id] = encode_frames(frames)
            animations = {}
            for name in ANIMATIONS:
                animation = specification['animations'][name]
                source_name = animation.get('fallback', name)
                animations[name] = dict(frameMs=specification['animations'][source_name]['frameMs'], frames=encoded[source_name])
                if 'fallback' in animation:
                    sprite_coverage[name] = dict(kind='reused-fallback', sourceAnimation='idle', originalFrameCount=0,
                        outputFrameCount=len(encoded['idle']), sourceRects=[], transforms=[],
                        sourceDescription='No dedicated source pose; explicitly reused mapped idle frames')
            pack['sprites'][sprite_id] = dict(name=specification['name'], family=specification['family'], stage=specification['stage'],
                width=frame_size, height=frame_size, palette=palette, transparentIndex=0, animations=animations)
            coverage[sprite_id] = sprite_coverage
            color_reports[sprite_id].update(transparentColor=key, alphaThreshold=threshold)
        finally:
            source.close()
            for animation_frames in frames.values():
                for frame in animation_frames:
                    frame.close()
    pack_bytes = canonical(pack)
    require(len(pack_bytes) <= MAX_PACK_BYTES, 'Converted pack exceeds 256 KiB; map fewer frames or creatures')
    decoded_bytes = sum(len(animation['frames']) * frame_size * frame_size * 4 for sprite in pack['sprites'].values() for animation in sprite['animations'].values())
    sidecar = dict(formatVersion=1, packId=pack['packId'], version=pack['version'], packSha256=hashlib.sha256(pack_bytes).hexdigest(),
        packBytes=len(pack_bytes), decodedBytes=decoded_bytes, provenance=provenance(manifest['provenance']), coverage=coverage,
        sources=sources, conversion=dict(importerVersion=1, frameSize=frame_size, alphaThreshold=threshold,
            palette='deterministic weighted median-cut RGB565, at most 15 opaque colors', colorReports=color_reports,
            fallbackPolicy='Only explicitly declared idle reuse; no inferred or generated poses', manifestSha256=hashlib.sha256(manifest_bytes).hexdigest()))
    sidecar_bytes = canonical(sidecar)
    require(len(sidecar_bytes) <= MAX_SIDECAR_BYTES, 'Provenance/coverage exceeds 64 KiB; shorten notes or split the import')
    return dict(pack=pack, sidecar=sidecar, packBytes=pack_bytes, sidecarBytes=sidecar_bytes,
        inputPaths=[manifest_path] + [source_path(manifest_path.parent, item['source']) for item in manifest['sprites'].values()])


def decode_preview(sprite, animation, frame_index):
    raw = base64.b64decode(sprite['animations'][animation]['frames'][frame_index], validate=True)
    indices = [index for byte in raw for index in (byte >> 4, byte & 15)]
    image = Image.new('RGBA', (sprite['width'], sprite['height']))
    image.putdata([(*unpack(sprite['palette'][index]), 255) if index else (0, 0, 0, 0) for index in indices])
    return image


def atomic_write(path, data):
    descriptor, temporary = tempfile.mkstemp(prefix=f'.{path.name}.', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def write_import(result, output, *, private_root=None, previews=False):
    private_path = Path(private_root) if private_root else ROOT / '.personal-assets'
    require(not private_path.is_symlink(), 'Private asset root must not be a symlink')
    private_root = private_path.resolve()
    output = Path(output).resolve()
    require(output.is_relative_to(private_root) and output != private_root, 'Output must be a child of ignored .personal-assets')
    destinations = [output / 'provenance.json', output / 'pack.json']
    if previews:
        destinations += [output / f'preview-{sprite_id}.png' for sprite_id in result['pack']['sprites']]
    inputs = {Path(path).resolve() for path in result['inputPaths']}
    require(all(path.resolve() not in inputs for path in destinations), 'Output would overwrite a source PNG or mapping; choose a different private output directory')
    output.mkdir(parents=True, exist_ok=True)
    # Publish pack last. Interrupted multi-file replacement is rejected by the
    # consumer's sidecar SHA check rather than accepting mismatched evidence.
    atomic_write(output / 'provenance.json', result['sidecarBytes'])
    atomic_write(output / 'pack.json', result['packBytes'])
    if previews:
        for sprite_id, sprite in result['pack']['sprites'].items():
            image = Image.new('RGB', (1072, 960), '#172132')
            draw = ImageDraw.Draw(image)
            draw.text((16, 12), f'{sprite["name"]} / PERSONAL IMPORT / {sprite["width"]} px', fill='#e8f0f6')
            for row, name in enumerate(ANIMATIONS):
                info = result['sidecar']['coverage'][sprite_id][name]
                y = 40 + row * 152
                draw.text((16, y), f'{name}: {info["kind"]}; {info["originalFrameCount"]} mapped source frames', fill='#a4c5d2')
                for index in range(len(sprite['animations'][name]['frames'])):
                    frame = decode_preview(sprite, name, index)
                    enlarged = frame.resize((128, 128), Image.Resampling.NEAREST)
                    image.paste(enlarged, (16 + index * 132, y + 20), enlarged)
                    frame.close()
                    enlarged.close()
            buffer = io.BytesIO()
            image.save(buffer, format='PNG', optimize=False)
            image.close()
            atomic_write(output / f'preview-{sprite_id}.png', buffer.getvalue())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', type=Path, help='Defaults to .personal-assets/<packId>; other tracked output locations are refused')
    parser.add_argument('--preview', action='store_true', help='Write private contact sheets with explicit fallback labels')
    arguments = parser.parse_args(argv)
    try:
        require('.personal-assets/' in (ROOT / '.gitignore').read_text().splitlines(), 'Add .personal-assets/ to .gitignore before creating private imports')
        result = build_import(arguments.manifest)
        output = arguments.output or ROOT / '.personal-assets' / result['pack']['packId']
        write_import(result, output, previews=arguments.preview)
        print(json.dumps(dict(output=str(output.resolve()), packBytes=len(result['packBytes']),
            decodedBytes=result['sidecar']['decodedBytes'], creatures=len(result['pack']['sprites']),
            sha256=result['sidecar']['packSha256']), sort_keys=True))
        return 0
    except (ImportFailure, OSError, UnicodeError) as error:
        print(f'Import rejected: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

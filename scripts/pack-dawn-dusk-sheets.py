#!/usr/bin/env python3
"""Pack Dawn/Dusk PNG sheets into SD sprites for forms added from those sheets.

Idle keeps the large side-view battle frames plus one real 3-frame walk when
the 8-frame clip has room. Attack keeps the large attack frames. Hurt, sleep,
care, and celebrate reuse those idle bytes. No invented poses. Output stays
under the ignored .personal-assets directory.
"""
from __future__ import annotations

import argparse
import base64
import importlib.util
import io
import json
import math
import sys
import zipfile
from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ZIP_PATH = Path('/tmp/digivice-sheets/wtw/sprite_thread.zip')
PLAN = ROOT / '.personal-assets' / 'sheet-roster-plan.json'
OUT = ROOT / '.personal-assets' / 'world-ds' / 'sheet-sd'


def load(name: str):
    path = ROOT / 'scripts' / name
    spec = importlib.util.spec_from_file_location(name.replace('.py', '').replace('-', '_'), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


SHEETS = load('import-sprite-sheet.py')
ASSETS = load('build-device-assets.py')


def isolate(image: Image.Image) -> Image.Image:
    """Turn the sheet background into real transparency. Corner color is the key when alpha is unused."""
    rgba = image.convert('RGBA')
    width, height = rgba.size
    raw = list(rgba.getdata())
    corners = [raw[0], raw[width - 1], raw[(height - 1) * width], raw[height * width - 1]]
    alpha_sheet = sum(pixel[3] < 128 for pixel in raw[:: max(1, len(raw) // 800)]) > 2
    background = max({pixel[:3] for pixel in corners}, key=lambda color: sum(pixel[:3] == color for pixel in corners))

    def near_background(color):
        return all(abs(channel - key) <= 12 for channel, key in zip(color, background))

    masked = []
    for red, green, blue, alpha in raw:
        keep = alpha >= 128 if alpha_sheet else alpha >= 128 and not near_background((red, green, blue))
        masked.append((red, green, blue, 255) if keep else (0, 0, 0, 0))
    rgba.putdata(masked)
    return rgba


def components(image: Image.Image):
    width, height = image.size
    raw = list(image.getdata())
    mask = bytearray(1 if pixel[3] else 0 for pixel in raw)
    seen = bytearray(width * height)
    boxes = []
    for start, value in enumerate(mask):
        if not value or seen[start]:
            continue
        queue = deque([start])
        seen[start] = 1
        min_x = max_x = start % width
        min_y = max_y = start // width
        count = 0
        while queue:
            cursor = queue.popleft()
            count += 1
            x, y = cursor % width, cursor // width
            min_x = min(min_x, x)
            max_x = max(max_x, x)
            min_y = min(min_y, y)
            max_y = max(max_y, y)
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < width and 0 <= ny < height:
                    nxt = ny * width + nx
                    if mask[nxt] and not seen[nxt]:
                        seen[nxt] = 1
                        queue.append(nxt)
        boxes.append((min_x, min_y, max_x - min_x + 1, max_y - min_y + 1, count))
    return boxes


def split_wide(image, box):
    x, y, width, height, _count = box
    if width < height * 1.7 or width < 70:
        return [(x, y, width, height)]
    pixels = image.load()
    columns = []
    for dx in range(width):
        hit = 0
        for dy in range(height):
            if pixels[x + dx, y + dy][3]:
                hit += 1
        columns.append(hit)
    peak = max(columns) or 1
    splits = [0]
    gap = 0
    for index, hit in enumerate(columns):
        if hit <= max(1, peak // 12):
            gap += 1
        elif gap >= 2 and index - splits[-1] > width // 8:
            splits.append(index - gap // 2)
            gap = 0
        else:
            gap = 0
    splits.append(width)
    if len(splits) < 3:
        return [(x, y, width, height)]
    parts = []
    for left, right in zip(splits, splits[1:]):
        if right - left < 12:
            continue
        crop = image.crop((x + left, y, x + right, y + height))
        bounds = crop.getbbox()
        if not bounds:
            continue
        parts.append((x + left + bounds[0], y + bounds[1], bounds[2] - bounds[0], bounds[3] - bounds[1]))
    return parts or [(x, y, width, height)]


def rows_of(boxes, slack):
    pending = sorted(boxes, key=lambda box: (box[1] + box[3] / 2, box[0]))
    grouped = []
    for box in pending:
        center = box[1] + box[3] / 2
        placed = False
        for row in grouped:
            row_center = sum(item[1] + item[3] / 2 for item in row) / len(row)
            row_height = sum(item[3] for item in row) / len(row)
            if abs(center - row_center) <= max(slack, row_height * 0.45):
                row.append(box)
                placed = True
                break
        if not placed:
            grouped.append([box])
    for row in grouped:
        row.sort(key=lambda box: box[0])
    return grouped


def median(values):
    ordered = sorted(values)
    return ordered[len(ordered) // 2]


def walk_cycles(small):
    if not small:
        return []
    grouped = rows_of(small, 8)
    cycles = []
    for row in grouped:
        heights = [box[3] for box in row]
        widths = [box[2] for box in row]
        typical_h, typical_w = median(heights), median(widths)
        regular = [box for box in row if typical_h * 0.72 <= box[3] <= typical_h * 1.35 and box[2] <= typical_w * 1.6]
        if len(regular) < 3:
            continue
        gaps = [regular[index + 1][0] - (regular[index][0] + regular[index][2]) for index in range(len(regular) - 1)]
        typical_gap = median(gaps) if gaps else 0
        groups, current = [], [regular[0]]
        for index, box in enumerate(regular[1:]):
            if gaps[index] > max(6, typical_gap * 1.55):
                groups.append(current)
                current = [box]
            else:
                current.append(box)
        groups.append(current)
        for group in groups:
            if len(group) >= 6:
                cycles.append(group[:3])
                if len(group) >= 12:
                    cycles.append(group[6:9])
            elif len(group) == 3:
                cycles.append(group)
    return cycles


def choose_frames(image):
    found = components(image)
    pieces = []
    for box in found:
        if box[4] < 40 or box[2] < 8 or box[3] < 8:
            continue
        if box[3] < 14 and box[2] > box[3] * 2:
            continue
        pieces.extend(split_wide(image, box))
    if not pieces:
        return None
    tallest = max(box[3] for box in pieces)
    large = [box for box in pieces if box[3] >= max(48, tallest * 0.55)]
    small = [box for box in pieces if box[3] <= tallest * 0.42 and box[2] <= tallest * 0.55]
    if len(large) < 3 or not small:
        return None
    large_rows = rows_of(large, 12)
    if len(large_rows[0]) >= 5:
        idle, attack = large_rows[0][:3], large_rows[0][3:5]
    elif len(large_rows) >= 2:
        idle = large_rows[0][:3]
        attack = [box for row in large_rows[1:] for box in row][:2]
    else:
        return None
    cycles = walk_cycles(small)
    if len(idle) < 1 or len(attack) < 1 or not cycles:
        return None
    return idle, attack[:2], cycles


def scale_group(image, boxes):
    crops = []
    for x, y, width, height in boxes:
        crop = image.crop((x, y, x + width, y + height))
        bounds = crop.getbbox()
        if not bounds:
            raise ValueError('empty frame')
        crops.append(crop.crop(bounds))
    max_w = max(crop.width for crop in crops)
    max_h = max(crop.height for crop in crops)
    factor = max(1, math.ceil(max(max_w, max_h) / 32))
    pad_w = math.ceil(max_w / factor) * factor
    pad_h = math.ceil(max_h / factor) * factor
    frames = []
    for crop in crops:
        padded = Image.new('RGBA', (pad_w, pad_h), (0, 0, 0, 0))
        padded.paste(crop, ((pad_w - crop.width) // 2, pad_h - crop.height))
        reduced = padded.resize((pad_w // factor, pad_h // factor), Image.Resampling.NEAREST)
        final = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
        final.paste(reduced, ((32 - reduced.width) // 2, 32 - reduced.height))
        if final.getbbox() is None:
            raise ValueError('downsample removed the frame')
        frames.append(final)
    return frames


def pack_sprite(image):
    chosen = choose_frames(image)
    if not chosen:
        return None
    idle_boxes, attack_boxes, cycles = chosen
    idle_frames = scale_group(image, idle_boxes)
    attack_frames = scale_group(image, attack_boxes)
    walk_frames = []
    for cycle in cycles:
        if len(idle_frames) + len(walk_frames) + 3 > 8:
            break
        walk_frames.extend(scale_group(image, cycle))
    if len(walk_frames) < 3:
        return None
    real = {'idle': idle_frames + walk_frames, 'attack': attack_frames}
    palette, encoded, report = SHEETS.encode_frames(real)
    clips = {}
    idle_bytes = [base64.b64decode(frame) for frame in encoded['idle']]
    attack_bytes = [base64.b64decode(frame) for frame in encoded['attack']]
    for name in ASSETS.ANIMATIONS:
        sequence = attack_bytes if name == 'attack' else idle_bytes
        clips[name] = (140 if name == 'attack' else 180, sequence)
    blob = ASSETS.encode_dva(32, 32, palette, clips)
    info = {
        'idleFrames': len(idle_boxes),
        'walkFrames': len(walk_frames),
        'attackFrames': len(attack_boxes),
        'walkCyclesSeen': len(cycles),
        'bytes': len(blob),
        'frames': sum(len(sequence) for _ms, sequence in clips.values()),
        'colors': report['paletteOpaqueColors'],
    }
    return blob, info


def preview(image, chosen, path):
    idle, attack, cycles = chosen
    sheet = Image.new('RGBA', (32 * (len(idle) + len(attack) + 3) + 8, 32), (20, 24, 32, 255))
    frames = scale_group(image, idle) + scale_group(image, attack) + scale_group(image, cycles[0])
    for index, frame in enumerate(frames):
        sheet.paste(frame, (index * 32, 0), frame)
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--preview', action='store_true')
    args = parser.parse_args()
    plan = json.loads(PLAN.read_text())
    packed, skipped = [], []
    with zipfile.ZipFile(ZIP_PATH) as archive:
        for row in plan['added']:
            image = isolate(Image.open(io.BytesIO(archive.read(row['file']))))
            try:
                result = pack_sprite(image)
            except (ValueError, OSError) as error:
                skipped.append((row['displayName'], str(error)))
                continue
            if not result:
                skipped.append((row['displayName'], 'sheet is missing a battle idle, attack, or 3-frame walk'))
                if args.preview:
                    continue
                continue
            blob, info = result
            info.update(formId=row['formId'], displayName=row['displayName'])
            packed.append(info)
            if args.write:
                OUT.mkdir(parents=True, exist_ok=True)
                (OUT / f"DSF{row['formId']:05d}.DVA").write_bytes(blob)
            if args.preview and row['displayName'] in {'Greymon', 'Woodmon', 'WarGreymon', 'Alphamon', 'Belphemon', 'Agnimon', 'Butenmon', 'MasterTyrannomon', 'V-dramon', 'ShineGreymon', 'Ballistamon'}:
                chosen = choose_frames(image)
                preview(image, chosen, Path('/tmp/digivice-sheet-preview/contacts') / f"{row['displayName']}.png")
    print(f'packed {len(packed)} skipped {len(skipped)}')
    if packed:
        print('largest', max(packed, key=lambda row: row['bytes']))
        print('most frames', max(item['frames'] for item in packed))
    for name, reason in skipped:
        print(f'SKIP {name}: {reason}')
    if args.write:
        (OUT / 'sheet-pack.json').write_text(json.dumps({'packed': packed, 'skipped': skipped}, indent=2) + '\n')


if __name__ == '__main__':
    sys.exit(main())

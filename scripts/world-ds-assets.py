#!/usr/bin/env python3
"""Prepare private, individually loadable artwork from audited local mappings.

No networking or sheet discovery. --plan validates the batch without opening
images; --build requires already-inspected source hashes and reuses the existing
sprite importer. Sources and explicit frame coordinates are never inferred.

Batch JSON (one existing import-sprite-sheet.py mapping per exact art identity):
  {"formatVersion":1,"collection":"digimon-world-ds","version":1,
   "entries":[{"artId":"example","mapping":"example.mapping.json",
     "deviceResize":"none","inspection":{"reviewer":"name",
       "notes":"Inspected all mapped poses and transparency",
       "sources":[{"file":"source/example.png","sha256":"64 hex digits"}]}}]}

Use deviceResize:nearest explicitly for a 64px browser mapping. This reruns the
same audited rectangles at32px with the importer's integer nearest resampling;
it never silently shrinks the browser artwork. Output is restricted to ignored
.personal-assets/world-ds. Index activation is last, with immutable objects;
partial failures preserve the previous index. The private index is unsigned and
not a firmware or public catalog. Consumers must request one artId at a time.
"""
import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
PRIVATE = ROOT / '.personal-assets' / 'world-ds'
MAX_ENTRIES = 512
MAX_BATCH_BYTES = 1024 * 1024
SHA = re.compile(r'^[0-9a-f]{64}$')


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


importer = module('world_ds_sprite_importer', 'import-sprite-sheet.py')
device = module('world_ds_device_encoder', 'build-device-assets.py')
require = importer.require
Failure = importer.ImportFailure
canonical = importer.canonical


def read_json(path, maximum):
    require(path.is_file() and path.stat().st_size <= maximum, 'JSON input is missing or exceeds its byte bound')
    raw = path.read_bytes()
    require(len(raw) <= maximum, 'JSON input changed beyond its byte bound')
    try:
        return json.loads(raw, object_pairs_hook=importer.unique_object), raw
    except (UnicodeError, json.JSONDecodeError) as error:
        raise Failure('Expected UTF-8 JSON') from error


def relative_path(base, value, suffix):
    require(importer.text(value, 512), 'Invalid local input path')
    relative = Path(value)
    require(not relative.is_absolute() and '..' not in relative.parts and relative.suffix.lower() == suffix,
            'Inputs must be local relative paths beneath the batch directory')
    result = (base / relative).resolve()
    require(result.is_relative_to(base.resolve()), 'Input symlink escapes its directory')
    return result


def read_batch(path):
    path = Path(path).resolve()
    batch, raw = read_json(path, MAX_BATCH_BYTES)
    require(importer.fields(batch, ('formatVersion', 'collection', 'version', 'entries')),
            'Unknown or missing batch fields')
    require(type(batch['formatVersion']) is int and batch['formatVersion'] == 1
            and batch['collection'] == 'digimon-world-ds' and importer.integer(batch['version'], 1, 2**31 - 1),
            'Unsupported batch collection/version')
    require(isinstance(batch['entries'], list) and 1 <= len(batch['entries']) <= MAX_ENTRIES,
            'Batch must contain1..512 independently loadable entries')
    entries, ids = [], set()
    for entry in batch['entries']:
        require(importer.fields(entry, ('artId', 'mapping', 'deviceResize', 'inspection')), 'Invalid batch entry')
        ident = entry['artId']
        require(isinstance(ident, str) and importer.ID.fullmatch(ident) and ident not in ids, 'Invalid or duplicate artId')
        ids.add(ident)
        require(entry['deviceResize'] in ('none', 'nearest'), 'Explicit deviceResize must be none or nearest')
        inspection = entry['inspection']
        require(importer.fields(inspection, ('reviewer', 'notes', 'sources')) and importer.text(inspection['reviewer'], 128)
                and importer.text(inspection['notes'], 1024), 'An explicit human source inspection record is required')
        require(isinstance(inspection['sources'], list) and len(inspection['sources']) == 1, 'One inspected source per artId is required')
        source = inspection['sources'][0]
        require(importer.fields(source, ('file', 'sha256')) and isinstance(source['sha256'], str) and SHA.fullmatch(source['sha256']),
                'Inspection requires the exact source file and SHA-256')
        mapping_path = relative_path(path.parent, entry['mapping'], '.json')
        mapping, mapping_raw = read_json(mapping_path, importer.MAX_MANIFEST_BYTES)
        frame_size, _ = importer.validate_manifest(mapping)
        require(set(mapping['sprites']) == {ident}, 'Each mapping must contain exactly its named artId')
        require(mapping['sprites'][ident]['source'] == source['file'], 'Inspection must name the mapped source file')
        relative_path(mapping_path.parent, source['file'], '.png')
        require(frame_size == 32 or entry['deviceResize'] == 'nearest', '64px browser artwork requires explicit nearest deviceResize')
        entries.append(dict(entry=entry, mapping=mapping, path=mapping_path,
                            mappingSha256=hashlib.sha256(mapping_raw).hexdigest()))
    return batch, entries, hashlib.sha256(raw).hexdigest()


def private_output(path=None, private_root=None):
    root = Path(private_root) if private_root else PRIVATE
    require(not root.is_symlink() and not root.parent.is_symlink(), 'Private output root must not be a symlink')
    root = root.resolve()
    output = Path(path).resolve() if path else root
    require(output == root or output.is_relative_to(root), 'Output must stay beneath .personal-assets/world-ds')
    require(not output.exists() or output.is_dir(), 'Output must be a directory')
    return output


def inspect_result(result, planned):
    require(result['sidecar']['conversion']['manifestSha256'] == planned['mappingSha256'],
            'Mapping changed after batch validation; review and retry the batch')
    source = result['sidecar']['sources'][0]
    require(source['sha256'] == planned['entry']['inspection']['sources'][0]['sha256'],
            'Source hash differs from the inspected sheet; inspect the changed image before import')
    return source


def build_device(planned, browser, staging):
    if browser['pack']['sprites'][planned['entry']['artId']]['width'] == 32:
        converted = browser
    else:
        # A private temporary copy keeps existing importer path protections intact.
        # No source image is edited, and at most one sheet is copied at a time.
        mapping = json.loads(canonical(planned['mapping']))
        ident = planned['entry']['artId']
        source = importer.source_path(planned['path'].parent, mapping['sprites'][ident]['source'])
        require(source.stat().st_size <= importer.MAX_IMAGE_BYTES, 'Source exceeds16MiB')
        raw = source.read_bytes()
        require(len(raw) <= importer.MAX_IMAGE_BYTES and hashlib.sha256(raw).hexdigest() == browser['sidecar']['sources'][0]['sha256'],
                'Source changed between browser and device conversion')
        (staging / 'source.png').write_bytes(raw)
        mapping['frameSize'] = 32
        mapping['sprites'][ident].update(source='source.png', resize='nearest')
        temporary_mapping = staging / 'mapping.json'
        temporary_mapping.write_bytes(canonical(mapping))
        converted = importer.build_import(temporary_mapping)
    sprite = converted['pack']['sprites'][planned['entry']['artId']]
    clips = {name: (clip['frameMs'], [base64.b64decode(frame, validate=True) for frame in clip['frames']])
             for name, clip in sprite['animations'].items()}
    blob = device.encode_dva(32, 32, sprite['palette'], clips, version=converted['pack']['version'])
    info = device.parse_dva(blob)
    audit = dict(formatVersion=1, artId=planned['entry']['artId'], sourceSha256=browser['sidecar']['sources'][0]['sha256'],
                 mappingSha256=planned['mappingSha256'], deviceResize=planned['entry']['deviceResize'],
                 sha256=hashlib.sha256(blob).hexdigest(), bytes=len(blob), width=32, height=32, frames=info['frames'],
                 transparentIndex=0, paletteEncoding='rgb565', coverage=converted['sidecar']['coverage'],
                 conversion=converted['sidecar']['conversion'])
    return blob, audit


def descriptor(path, raw):
    return dict(file=path, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def immutable_write(path, raw, check):
    if path.exists():
        require(path.is_file() and not path.is_symlink() and path.stat().st_size == len(raw) and path.read_bytes() == raw,
                'Existing private object differs; refusing replacement')
    else:
        require(not check, f'Expected built object is missing: {path.name}')
        path.parent.mkdir(parents=True, exist_ok=True)
        importer.atomic_write(path, raw)


def build_batch(path, output=None, *, check=False, private_root=None):
    batch, planned, batch_hash = read_batch(path)
    output = private_output(output, private_root)
    require('.personal-assets/' in (ROOT / '.gitignore').read_text().splitlines(), 'Private assets must remain ignored')
    old = None
    if (output / 'index.json').exists():
        old, _ = read_json(output / 'index.json', MAX_BATCH_BYTES)
        require(isinstance(old, dict) and old.get('formatVersion') == 1 and old.get('privateOnly') is True
                and isinstance(old.get('entries'), list) and len(old['entries']) <= MAX_ENTRIES,
                'Existing private index is unreadable')
        require(all(isinstance(entry, dict) and isinstance(entry.get('artId'), str)
                    and importer.integer(entry.get('version'), 1, 2**31 - 1)
                    and all(isinstance(entry.get(kind), dict) and isinstance(entry[kind].get('sha256'), str)
                            and SHA.fullmatch(entry[kind]['sha256']) for kind in ('browser', 'device'))
                    for entry in old['entries']), 'Existing private index entries are unreadable')
    old_entries = {entry['artId']: entry for entry in old['entries']} if old else {}
    rows, total_browser, total_device, largest, peak_rgba = [], 0, 0, 0, 0
    require(not check or output.is_dir(), 'Private output does not exist for --check')
    output.mkdir(parents=True, exist_ok=True)
    for record in planned:
        result = importer.build_import(record['path'])
        inspect_result(result, record)
        with tempfile.TemporaryDirectory(prefix='.staging-', dir=output) as temporary:
            blob, audit = build_device(record, result, Path(temporary))
        ident = record['entry']['artId']
        version = result['pack']['version']
        audit['inspection'] = record['entry']['inspection']
        audit_bytes = canonical(audit)
        object_hash = hashlib.sha256(result['packBytes'] + result['sidecarBytes'] + blob + audit_bytes).hexdigest()
        relative = f'objects/{ident}-v{version}-{object_hash[:16]}'
        row = dict(artId=ident, name=result['pack']['sprites'][ident]['name'], version=version,
                   browser=descriptor(f'{relative}/pack.json', result['packBytes']),
                   device={**descriptor(f'{relative}/sprite.dva', blob), 'width':32, 'height':32, 'frames':audit['frames']},
                   provenance=descriptor(f'{relative}/provenance.json', result['sidecarBytes']),
                   audit=descriptor(f'{relative}/audit.json', audit_bytes))
        previous = old_entries.get(ident)
        if previous and previous['version'] == version:
            require(previous['browser']['sha256'] == row['browser']['sha256'] and previous['device']['sha256'] == row['device']['sha256'],
                    'Changed artwork requires a new per-art version')
        for name, raw in [('pack.json', result['packBytes']), ('provenance.json', result['sidecarBytes']), ('sprite.dva', blob), ('audit.json', audit_bytes)]:
            destination = output / relative / name
            require(destination.resolve().is_relative_to(output), 'Output symlink escapes the private root')
            immutable_write(destination, raw, check)
        rows.append(row)
        total_browser += len(result['packBytes']); total_device += len(blob)
        largest = max(largest, len(blob)); peak_rgba = max(peak_rgba, result['sidecar']['decodedBytes'])
    index = dict(formatVersion=1, collection='digimon-world-ds', version=batch['version'], privateOnly=True,
                 license=importer.LICENSE, batchSha256=batch_hash, entries=rows,
                 budget=dict(forms=len(rows), browserEncodedBytes=total_browser, deviceEncodedBytes=total_device,
                             largestDeviceBlobBytes=largest, maximumOneBrowserPackDecodedBytes=peak_rgba,
                             deviceOneFrameRgb565Bytes=2048, deviceTwoActorsRgb565Bytes=4096,
                             policy='Load selected partner and current opponent only; stream one32px frame per actor. No full-roster flash embedding or RAM decode.'))
    raw = canonical(index)
    require(len(raw) <= MAX_BATCH_BYTES, 'Private index exceeds1MiB')
    if old and old.get('version') == batch['version']:
        require(canonical(old) == raw, 'Changed batch index requires a new batch version')
    if check:
        require((output / 'index.json').is_file() and (output / 'index.json').read_bytes() == raw, 'Private index differs from deterministic build')
    else:
        importer.atomic_write(output / 'index.json', raw)
    return index


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--plan', action='store_true', help='Validate mappings and inspection metadata; do not open or change images')
    mode.add_argument('--build', action='store_true', help='Build only already inspected and authorized local sheets')
    mode.add_argument('--check', action='store_true', help='Regenerate and compare existing private objects and index')
    args = parser.parse_args(argv)
    try:
        if args.plan:
            batch, entries, digest = read_batch(args.manifest)
            private_output(args.output)
            result = dict(collection=batch['collection'], entries=len(entries), batchSha256=digest,
                          sourcePixelsRead=False, outputWritten=False, deviceFrameBytes=2048,
                          maximumDeviceBlobBytes=112 + 48 * 512,
                          note='Metadata validation only; source inspection and access authorization are external prerequisites.')
        else:
            result = build_batch(args.manifest, args.output, check=args.check)['budget']
        print(json.dumps(result, sort_keys=True)); return 0
    except (Failure, OSError, ValueError) as error:
        print(f'Private batch rejected: {error}', file=sys.stderr); return 2


if __name__ == '__main__':
    raise SystemExit(main())

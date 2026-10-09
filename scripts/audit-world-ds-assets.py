#!/usr/bin/env python3
"""Audit a snapshot of private source/pack bytes; emit metadata only, no pixels.

This reads the atomic acquisition manifest and active index independently.
Counts are checkpoint counts, not a claim that native/browser tests ran here.
No network, download, source rewrite, pack activation or external upload.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import stat

ROOT = Path(__file__).resolve().parents[1]
MAX_JSON = 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def bounded_file(root, relative, maximum):
    relative = Path(relative)
    require(not relative.is_absolute() and '..' not in relative.parts, 'Unsafe relative artifact path')
    path = root / relative
    require(path.resolve().is_relative_to(root.resolve()), 'Artifact escapes private directory')
    before = path.lstat()
    require(stat.S_ISREG(before.st_mode) and before.st_size <= maximum, 'Artifact is not a bounded regular file')
    with path.open('rb') as handle:
        raw = handle.read(maximum + 1)
    after = path.lstat()
    require((before.st_ino, before.st_size, before.st_mtime_ns) ==
            (after.st_ino, after.st_size, after.st_mtime_ns) and len(raw) == before.st_size,
            'Artifact changed during audit; retry checkpoint')
    return raw


def json_file(root, relative):
    raw = bounded_file(root, relative, MAX_JSON)
    return json.loads(raw), hashlib.sha256(raw).hexdigest()


def audit(root=ROOT):
    spec = importlib.util.spec_from_file_location('world_ds_acquisition', ROOT / 'scripts/download-world-ds-browser.py')
    acquisition = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(acquisition)
    private = root / '.personal-assets/world-ds'
    source = private / 'source'
    catalog, catalog_hash = json_file(root, 'data/world-ds-catalog.json')
    known = {row['entryKey']: row for row in catalog['entries']}
    require(len(known) == len(catalog['entries']) == 255, 'Expected 255 unique catalog entries')
    by_id = {f"ds-form-{row['formId']}": row for row in known.values()}
    require(len(by_id) == 255, 'Duplicate catalog form identity')
    manifest, manifest_hash = json_file(source, 'acquisition.json')
    require(manifest.get('formatVersion') == 1 and manifest.get('kind') == 'private-source-acquisition',
            'Unsupported source manifest')
    require(manifest.get('catalogSha256') == catalog_hash, 'Acquisition catalog changed')
    records = manifest['entries']
    require(isinstance(records, dict) and set(records) <= set(known), 'Unknown acquired identity')
    verified, source_bytes = {}, 0
    for key, row in records.items():
        expected = known[key]
        require(row['entryKey'] == key and row['formId'] == expected['formId'] and
                row['name'] == expected['source']['listedName'] and
                row['section'] == expected['source']['section'] and row['file'] == f'{key}.png',
                f'Source identity mismatch: {key}')
        require(acquisition.ASSET_URL.fullmatch(row['url']), f'Unexpected source URL: {key}')
        match = re.fullmatch(r'([0-9]+(?:\.[0-9]+)?) (B|KB|MB) \(([0-9]+)\s*[x×]\s*([0-9]+)\)', row['sizeLabel'])
        require(match is not None, f'Invalid source size label: {key}')
        require((int(match[3]), int(match[4])) == (row['width'], row['height']),
                f'Source label dimensions mismatch: {key}')
        page = dict(row, sizeValue=match[1], sizeUnit=match[2])
        info, _ = acquisition.png_info(source / row['file'], page)
        require(all(info[field] == row[field] for field in ('bytes', 'sha256', 'width', 'height')),
                f'Source bytes differ from manifest: {key}')
        verified[key] = info
        source_bytes += info['bytes']

    # Three independently reviewed sources predate the acquisition manifest.
    # Read and verify their recorded provenance before counting them available.
    index, index_hash = json_file(private, 'index.json')
    require(index.get('formatVersion') == 1 and index.get('privateOnly') is True and
            index.get('collection') == 'digimon-world-ds', 'Unsupported private index')
    rows = index['entries']
    require(isinstance(rows, list) and len(rows) <= 255, 'Invalid private index length')
    integrated, identities, artifact_count, totals = [], set(), 0, {'browser': 0, 'device': 0}
    for row in rows:
        ident = row['artId']
        require(ident in by_id and ident not in identities, f'Unknown or duplicate integrated identity: {ident}')
        identities.add(ident)
        expected = by_id[ident]
        key = expected['entryKey']
        require(row['name'] == expected['source']['listedName'], f'Integrated name mismatch: {ident}')
        artifacts = {}
        for kind in ('browser', 'device', 'provenance', 'audit'):
            descriptor = row[kind]
            raw = bounded_file(private, descriptor['file'], MAX_JSON)
            require(len(raw) == descriptor['bytes'] and hashlib.sha256(raw).hexdigest() == descriptor['sha256'],
                    f'Artifact bytes differ: {ident}/{kind}')
            artifacts[kind] = raw
            artifact_count += 1
            if kind in totals:
                totals[kind] += len(raw)
        browser = json.loads(artifacts['browser'])
        require(browser['packId'] == f'personal-{ident}' and browser['version'] == row['version'] and
                set(browser['sprites']) == {ident} and browser['sprites'][ident]['name'] == row['name'],
                f'Browser pack identity mismatch: {ident}')
        provenance = json.loads(artifacts['provenance'])
        require(provenance['packId'] == browser['packId'] and provenance['version'] == row['version'] and
                provenance['packSha256'] == row['browser']['sha256'],
                f'Browser provenance binding mismatch: {ident}')
        device_audit = json.loads(artifacts['audit'])
        require(device_audit['artId'] == ident and device_audit['sha256'] == row['device']['sha256'] and
                device_audit['bytes'] == row['device']['bytes'], f'Device audit binding mismatch: {ident}')
        require(len(provenance['sources']) == 1, f'Expected one source for {ident}')
        original = provenance['sources'][0]
        require(original['spriteId'] == ident, f'Provenance identity mismatch: {ident}')
        require(device_audit['sourceSha256'] == original['sha256'], f'Device source binding mismatch: {ident}')
        if key not in verified and key in ('agumon', 'greymon', 'metalgreymon'):
            raw = bounded_file(root / '.personal-assets/source', f'{key}.png', acquisition.MAX_PNG_BYTES)
            require(len(raw) == original['bytes'] and hashlib.sha256(raw).hexdigest() == original['sha256'],
                    f'Prior source bytes differ: {key}')
            page = dict(original, sizeValue=str(len(raw)), sizeUnit='B')
            info, _ = acquisition.png_info(root / '.personal-assets/source' / f'{key}.png', page)
            verified[key] = info
            source_bytes += info['bytes']
        require(key in verified and all(original[field] == verified[key][field]
                                        for field in ('bytes', 'sha256', 'width', 'height')),
                f'Integrated provenance differs from acquired source: {ident}')
        if key in records:
            require(original['provenance']['sourceUrl'].rstrip('/') == records[key]['url'].rstrip('/'),
                    f'Integrated source URL mismatch: {ident}')
        integrated.append(key)
    require(index['budget']['forms'] == len(rows) and
            index['budget']['browserEncodedBytes'] == totals['browser'] and
            index['budget']['deviceEncodedBytes'] == totals['device'], 'Index byte budget mismatch')
    return {
        'formatVersion': 1, 'checkedAt': dt.datetime.now(dt.timezone.utc).isoformat(),
        'scope': 'Read-only byte/provenance checkpoint; browser and native decode validation is recorded separately.',
        'result': 'PASS', 'catalogSha256': catalog_hash, 'acquisitionManifestSha256': manifest_hash,
        'acquisitionUpdatedAt': manifest.get('updatedAt'), 'activeIndexSha256': index_hash,
        'activeIndexVersion': index['version'], 'targetSheets': 255,
        'manifestEntries': len(records), 'availableSheets': len(verified), 'integratedPacks': len(rows),
        'verifiedSourceBytes': source_bytes, 'verifiedPackArtifacts': artifact_count,
        'browserEncodedBytes': totals['browser'], 'deviceEncodedBytes': totals['device'],
        'missingSourceEntries': sorted(set(known) - set(verified)),
        'availableNotIntegrated': sorted(set(verified) - set(integrated)),
        'limitations': ['Private pixels are excluded from this metadata report.',
                        'Integration count requires matching bytes and provenance; visual/browser/native tests are separate.',
                        'No physical SD, ESP or display testing is performed here.'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='Optional local metadata-only JSON checkpoint')
    args = parser.parse_args()
    try:
        result = audit()
        text = json.dumps(result, indent=2) + '\n'
        if args.output:
            args.output.write_text(text)
        print(text, end='')
        return 0
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print(json.dumps({'result': 'FAIL', 'error': str(error)}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

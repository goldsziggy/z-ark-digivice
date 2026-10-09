#!/usr/bin/env python3
"""Stage explicitly reviewed local DigiGame sprites; never execute upstream code.

Reads only PNG/JSON Git blobs at a pinned commit. Reuses our bounded sprite
importer and DVA encoder. Preparation is private and does not activate assets.
The active roster remains a separate, independently approved integration step.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
PRIVATE = ROOT / '.personal-assets' / 'digigame'
REPOSITORY = 'https://github.com/EwertonMendes/DigiGame'
BASELINE_SHA256 = '0f547a62d33aa613dbccc1842000528bef7fabac64f2dfafa7dbde8777bb62d8'
BASELINE_FORMS = 87
MAX_PNG = 16 * 1024 * 1024
MAX_JSON = 128 * 1024
SHA = re.compile(r'^[a-f0-9]{64}$')
COMMIT = re.compile(r'^[a-f0-9]{40}$')
SLUG = re.compile(r'^[a-z0-9][a-z0-9_-]{0,63}$')
ORDER = ['down_left', 'down_right', 'up_left', 'up_right']
PHASES = ['idle', 'step_a', 'step_b']


def own_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


pipeline = own_module('digigame_private_pipeline', 'world-ds-assets.py')
require = pipeline.require
Failure = pipeline.Failure
canonical = pipeline.canonical


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def integer(value, minimum, maximum):
    return type(value) is int and minimum <= value <= maximum


def text(value, maximum):
    return isinstance(value, str) and 0 < len(value.strip()) <= maximum


def read_json_bytes(raw, maximum=MAX_JSON):
    require(isinstance(raw, bytes) and 0 < len(raw) <= maximum, 'JSON exceeds its bound')
    try:
        return json.loads(raw, object_pairs_hook=pipeline.importer.unique_object)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise Failure('Expected bounded UTF-8 JSON') from error


def git_blob(repository, commit, path, maximum):
    """Built-in object reads only: no checkout, hooks, filters, or project tools."""
    require(isinstance(commit, str) and COMMIT.fullmatch(commit) is not None, 'Pin the full40-character commit')
    require(re.fullmatch(r'assets/characters/[a-z0-9][a-z0-9_-]{0,63}/field\.(png|json)', path),
            'Only an exact character field PNG or JSON blob may be read')
    command = ['git', '-c', 'core.hooksPath=/dev/null', '-c', 'core.fsmonitor=false',
               '--no-optional-locks', '-C', str(repository), 'cat-file']
    ref = f'{commit}:{path}'
    try:
        size = int(subprocess.run(command + ['-s', ref], check=True, capture_output=True,
                                  timeout=10).stdout)
        require(0 < size <= maximum, 'Git blob exceeds its byte bound')
        raw = subprocess.run(command + ['blob', ref], check=True, capture_output=True,
                             timeout=10).stdout
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise Failure(f'Cannot read pinned data blob: {path}') from error
    require(len(raw) == size, 'Pinned blob size disagrees')
    return raw


def validate_plan(plan, catalog, baseline):
    require(pipeline.importer.fields(plan, ('formatVersion', 'sourceRepository', 'sourceCommit',
            'cohort', 'baselineIndexSha256', 'entries')), 'Unknown or missing plan fields')
    require(plan['formatVersion'] == 1 and type(plan['formatVersion']) is int
            and plan['sourceRepository'] == REPOSITORY and isinstance(plan['sourceCommit'], str)
            and COMMIT.fullmatch(plan['sourceCommit']),
            'Unsupported source or unpinned commit')
    require(isinstance(plan['cohort'], str) and re.fullmatch(r'[a-z0-9][a-z0-9-]{0,39}', plan['cohort']),
            'Invalid cohort label')
    require(isinstance(plan['baselineIndexSha256'], str) and SHA.fullmatch(plan['baselineIndexSha256'])
            and plan['baselineIndexSha256'] == digest(canonical(baseline)), 'Frozen baseline hash differs')
    require(isinstance(plan['entries'], list) and 1 <= len(plan['entries']) <= 32,
            'A reviewable cohort contains1..32 exact forms')
    existing = {row['artId'] for row in baseline['entries']}
    known = {row['formId']: row for row in catalog['entries']}
    seen = set()
    for entry in plan['entries']:
        require(pipeline.importer.fields(entry, ('formId', 'name', 'sourceSlug', 'pngSha256',
                'fieldSha256', 'direction', 'frameMs', 'browserResize', 'creator', 'game', 'inspection')),
                'Unknown or missing source entry fields')
        ident = entry['formId']
        require(integer(ident, 1, 512) and ident in known and ident not in seen
                and f'ds-form-{ident}' not in existing, 'Unknown, duplicate, or already verified form')
        require(entry['name'] == known[ident]['displayName'], 'Use the exact existing canonical form name')
        require(isinstance(entry['sourceSlug'], str) and SLUG.fullmatch(entry['sourceSlug']), 'Invalid source slug')
        require(all(isinstance(entry[k], str) and SHA.fullmatch(entry[k]) for k in ('pngSha256', 'fieldSha256')),
                'Both pinned data hashes are required')
        require(entry['direction'] in ORDER and integer(entry['frameMs'], 40, 2000)
                and entry['browserResize'] in ('none', 'nearest'), 'Invalid explicit frame selection')
        require(text(entry['creator'], 512) and text(entry['game'], 512), 'Source attribution is required')
        inspection = entry['inspection']
        require(pipeline.importer.fields(inspection, ('reviewer', 'notes'))
                and text(inspection['reviewer'], 128) and text(inspection['notes'], 1024),
                'Record actual source-image inspection before preparation')
        seen.add(ident)


def prepare_mapping(entry, commit, png, field_raw):
    require(digest(png) == entry['pngSha256'] and digest(field_raw) == entry['fieldSha256'],
            'Source bytes do not match the inspected PNG/JSON hashes')
    require(0 < len(png) <= MAX_PNG and len(png) >= 33 and png[:8] == b'\x89PNG\r\n\x1a\n'
            and png[8:16] == b'\x00\x00\x00\rIHDR', 'Expected a bounded PNG with first IHDR')
    width, height = struct.unpack_from('>II', png, 16)
    field = read_json_bytes(field_raw)
    require(isinstance(field, dict), 'Field metadata must be an object')
    require(field.get('source_kind') == 'official_ds',
            'Only reviewed official_ds source-kind claims are eligible; mixed-origin exceptions remain excluded')
    cw, ch = field.get('cell_width'), field.get('cell_height')
    require(integer(cw, 1, 256) and integer(ch, 1, 256) and width == cw * 12 and height == ch
            and max(width, height) <= 4096, 'PNG/cell dimensions disagree with the bounded horizontal strip')
    directions = field.get('canonical_runtime_order', field.get('directions'))
    require(('canonical_runtime_order' not in field or field['canonical_runtime_order'] == ORDER)
            and ('directions' not in field or field['directions'] == ORDER), 'Direction metadata disagrees')
    require(field.get('frame_count') == 12 and field.get('frames_per_direction') == 3
            and directions == ORDER
            and field.get('canonical_runtime_phases') == PHASES,
            'Unsupported field layout; inspect and explicitly map this exception separately')
    expected_path = f"res://assets/characters/{entry['sourceSlug']}/field.png"
    require(field.get('field_path') == expected_path, 'Field identity/path disagrees with its blob')
    require(max(cw, ch) <= 64 or entry['browserResize'] == 'nearest',
            'Oversized source cells require explicit browser nearest downsampling')
    first = ORDER.index(entry['direction']) * 3
    rects = [[(first + i) * cw, 0, cw, ch] for i in range(3)]
    ident = f"ds-form-{entry['formId']}"
    source = f"source/digigame/{entry['sourceSlug']}-{entry['pngSha256'][:12]}.png"
    provenance = dict(creator=entry['creator'],
        sourceUrl=f"{REPOSITORY}/blob/{commit}/assets/characters/{entry['sourceSlug']}/field.png",
        game=entry['game'],
        rights='Third-party character artwork remains restricted to its respective rights holders. Repository availability and normalization do not grant an artwork license.',
        reuseScope='Local unpublished personal prototype only; no redistribution, public upload or commercial use.')
    description = f"DigiGame pinned normalized {entry['direction']} idle/step_a/step_b cells, repurposed as idle. No dedicated battle pose claimed."
    sprite = dict(name=entry['name'], family='digigame-private-import', stage=1,
                  source=source, resize=entry['browserResize'], anchor='bottom-center',
                  provenance=provenance,
                  animations={'idle': dict(frameMs=entry['frameMs'], rects=rects, sourceDescription=description),
                              **{name: dict(fallback='idle') for name in pipeline.importer.ANIMATIONS[1:]}})
    mapping = dict(formatVersion=1, packId=f'personal-{ident}', version=1,
                   frameSize=64, provenance=provenance, sprites={ident: sprite})
    pipeline.importer.validate_manifest(mapping)
    return mapping, dict(formId=entry['formId'], artId=ident, name=entry['name'],
        sourceSlug=entry['sourceSlug'], pngSha256=digest(png), fieldSha256=digest(field_raw),
        runtimeWidth=width, runtimeHeight=height, cellWidth=cw, cellHeight=ch,
        selectedDirection=entry['direction'], selectedRects=rects,
        browserResize=entry['browserResize'],
        upstreamDeclaredOriginalSourceSha256=field.get('source_sha256'),
        originalSourceBytesIndependentlyVerified=False,
        upstreamMetadataIsProvenanceClaim=True)


def immutable(path, raw):
    require(not path.is_symlink(), 'Private path cannot be a symlink')
    require(not path.exists() or path.is_file() and path.read_bytes() == raw,
            'Existing prepared data differs; use a new reviewed plan')
    require(path.resolve() == path.absolute(), 'Private path cannot contain symlinks')
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        pipeline.importer.atomic_write(path, raw)


def frozen_baseline():
    path = PRIVATE / 'baseline-index-v10.json'
    require(path.resolve() == path.absolute(), 'Frozen baseline cannot use a symlink')
    baseline, raw = pipeline.read_json(path, 1024 * 1024)
    require(digest(raw) == BASELINE_SHA256 and len(baseline['entries']) == BASELINE_FORMS,
            'The trusted87-form baseline bytes differ')
    return baseline, raw


def checked_artifact(directory, descriptor, maximum=256 * 1024):
    relative = Path(descriptor['file'])
    require(not relative.is_absolute() and '..' not in relative.parts, 'Unsafe artifact path')
    path = directory / relative
    require(path.resolve() == path.absolute() and path.resolve().is_relative_to(directory.resolve())
            and path.is_file() and 0 < path.stat().st_size <= maximum, 'Artifact is not a bounded private regular file')
    raw = path.read_bytes()
    require(len(raw) == descriptor['bytes'] and len(raw) <= maximum and digest(raw) == descriptor['sha256'],
            'Immutable artifact bytes changed')
    return raw


def verify_frozen(baseline, active):
    require(active['entries'][:len(baseline['entries'])] == baseline['entries'], 'Frozen verified entries changed')
    directory = ROOT / '.personal-assets/world-ds'
    for row in baseline['entries']:
        for kind in ('browser', 'device', 'provenance', 'audit'):
            checked_artifact(directory, row[kind])


def prepare(plan_path):
    require(PRIVATE.resolve() == PRIVATE.absolute(), 'Private source root cannot use symlinks')
    plan_path = Path(plan_path).resolve()
    require(plan_path.is_relative_to(PRIVATE) and plan_path.stat().st_size <= MAX_JSON,
            'Plan must be a bounded private DigiGame file')
    plan_raw = plan_path.read_bytes(); plan = read_json_bytes(plan_raw)
    baseline, baseline_raw = frozen_baseline()
    require(digest(baseline_raw) == plan['baselineIndexSha256'], 'Frozen index bytes differ')
    catalog = read_json_bytes((ROOT / 'data/world-ds-catalog.json').read_bytes(), 4 * 1024 * 1024)
    validate_plan(plan, catalog, baseline)
    active = read_json_bytes((ROOT / '.personal-assets/world-ds/index.json').read_bytes(), 1024 * 1024)
    verify_frozen(baseline, active)
    active_ids = {row['artId'] for row in active['entries']}
    require(all(f"ds-form-{e['formId']}" not in active_ids for e in plan['entries']), 'An intended target already has verified art')
    output = PRIVATE / 'prepared' / f"{plan['cohort']}-{digest(plan_raw)[:12]}"
    repository = PRIVATE / 'source-git'
    rows, ledger = [], []
    for entry in plan['entries']:
        prefix = f"assets/characters/{entry['sourceSlug']}"
        png = git_blob(repository, plan['sourceCommit'], f'{prefix}/field.png', MAX_PNG)
        field_raw = git_blob(repository, plan['sourceCommit'], f'{prefix}/field.json', MAX_JSON)
        mapping, record = prepare_mapping(entry, plan['sourceCommit'], png, field_raw)
        ident = record['artId']; raw = canonical(mapping)
        immutable(output / 'input' / mapping['sprites'][ident]['source'], png)
        immutable(output / 'input' / f'{ident}.mapping.json', raw)
        immutable(output / 'upstream-metadata' / f'{ident}.field.json', field_raw)
        record['mappingSha256'] = digest(raw); ledger.append(record)
        rows.append(dict(artId=ident, mapping=f'{ident}.mapping.json', deviceResize='nearest',
                         inspection={**entry['inspection'], 'sources': [dict(file=mapping['sprites'][ident]['source'], sha256=digest(png))]}))
    batch = dict(formatVersion=1, collection='digimon-world-ds', version=1, entries=rows)
    immutable(output / 'input/batch.json', canonical(batch))
    result = pipeline.build_batch(output / 'input/batch.json', output / 'staged', private_root=PRIVATE)
    report = dict(formatVersion=1, privateOnly=True, status='awaiting-independent-review',
        sourceRepository=REPOSITORY, sourceCommit=plan['sourceCommit'], planSha256=digest(plan_raw),
        baselineIndexSha256=plan['baselineIndexSha256'], entries=ledger, budget=result['budget'],
        stagedIndexSha256=digest((output / 'staged/index.json').read_bytes()),
        upstreamCodeExecuted=False, activeIndexChanged=False)
    immutable(output / 'preparation.json', canonical(report))
    return output, report


def finish_activation(transaction, expected_plan, expected_review):
    """Recover only exact old/new bytes from a durable local commit intent."""
    intent, _ = pipeline.read_json(transaction / 'intent.json', MAX_JSON)
    require(intent.get('formatVersion') == 1 and intent.get('planSha256') == expected_plan
            and intent.get('reviewSha256') == expected_review, 'Activation intent binding differs')
    payloads = {}
    for key in ('oldIndex', 'oldBatch', 'newIndex', 'newBatch', 'result'):
        payloads[key] = (transaction / f'{key}.json').read_bytes()
        require(0 < len(payloads[key]) <= 1024 * 1024
                and digest(payloads[key]) == intent['hashes'][key], 'Activation intent payload changed')
    world = ROOT / '.personal-assets/world-ds'
    new_index = read_json_bytes(payloads['newIndex'], 1024 * 1024)
    current_index = (world / 'index.json').read_bytes()
    current_batch = (world / 'input/batch.json').read_bytes()
    baseline, _ = frozen_baseline()
    verify_frozen(baseline, read_json_bytes(current_index, 1024 * 1024))
    # Every immutable object must exist before the first public index swap.
    for row in new_index['entries']:
        for kind in ('browser', 'device', 'provenance', 'audit'):
            checked_artifact(world, row[kind])
    require((current_index, current_batch) in (
        (payloads['oldIndex'], payloads['oldBatch']),
        (payloads['newIndex'], payloads['oldBatch']),
        (payloads['newIndex'], payloads['newBatch'])),
        'Activation recovery refused: current files are not an exact expected old/new pair')
    if current_index != payloads['newIndex']:
        pipeline.importer.atomic_write(world / 'index.json', payloads['newIndex'])
    if current_batch != payloads['newBatch']:
        pipeline.importer.atomic_write(world / 'input/batch.json', payloads['newBatch'])
    immutable(transaction / 'activation.json', payloads['result'])
    return read_json_bytes(payloads['result'])


def activate(prepared_path, review_path):
    """Append approved missing forms; original bytes and IDs are never replaced."""
    prepared = Path(prepared_path).resolve()
    require(prepared.is_relative_to(PRIVATE / 'prepared'), 'Activation input must be our private prepared cohort')
    review_path = Path(review_path).resolve()
    require(review_path.is_relative_to(PRIVATE), 'Review record must remain private')
    report, _ = pipeline.read_json(prepared / 'preparation.json', MAX_JSON)
    review, review_raw = pipeline.read_json(review_path, MAX_JSON)
    require(review.get('formatVersion') == 1 and review.get('status') == 'approved'
            and text(review.get('reviewer'), 128), 'Independent internal review approval is required')
    require(all(review.get(k) == report[k] for k in ('sourceCommit', 'planSha256', 'stagedIndexSha256')),
            'Review does not identify these prepared bytes')
    expected = {row['formId']: row for row in report['entries']}
    approvals = review.get('entries')
    require(isinstance(approvals, list) and len(approvals) == len(expected), 'Review must cover the exact cohort')
    seen = set()
    for row in approvals:
        ident = row.get('formId')
        require(ident in expected and ident not in seen and row.get('status') == 'approved',
                'Unknown, duplicate, or unapproved target')
        require(all(row.get(k) == expected[ident][k] for k in ('pngSha256', 'fieldSha256', 'mappingSha256')),
                'Approved source or mapping hashes changed')
        seen.add(ident)
    staged, staged_raw = pipeline.read_json(prepared / 'staged/index.json', 1024 * 1024)
    require(digest(staged_raw) == report['stagedIndexSha256'], 'Staged index changed after review')
    baseline, _ = frozen_baseline()
    world = ROOT / '.personal-assets/world-ds'
    active, old_raw = pipeline.read_json(world / 'index.json', 1024 * 1024)
    verify_frozen(baseline, active)
    transaction = PRIVATE / 'activations' / report['planSha256']
    if (transaction / 'intent.json').exists():
        return finish_activation(transaction, report['planSha256'], digest(review_raw))
    existing = {row['artId'] for row in active['entries']}
    require(all(f'ds-form-{ident}' not in existing for ident in expected), 'Refusing to replace verified existing art')
    # Validate every staged object and source before writing any active inputs.
    for row in staged['entries']:
        for kind in ('browser', 'device', 'provenance', 'audit'):
            checked_artifact(prepared / 'staged', row[kind])
    proposed, records, _ = pipeline.read_batch(prepared / 'input/batch.json')
    require({row['entry']['artId'] for row in records} == {f'ds-form-{ident}' for ident in expected},
            'Prepared batch and approved form set disagree')
    active_batch, old_batch_raw = pipeline.read_json(world / 'input/batch.json', 1024 * 1024)
    require([row['artId'] for row in active_batch['entries']] == [row['artId'] for row in active['entries']]
            and active_batch['version'] == active['version'], 'Active input batch disagrees with its index')
    for record in records:
        ident = int(record['entry']['artId'][8:]); source = record['entry']['inspection']['sources'][0]
        require(record['mappingSha256'] == expected[ident]['mappingSha256'], 'Prepared mapping changed')
        src = pipeline.relative_path(record['path'].parent, source['file'], '.png')
        require(digest(src.read_bytes()) == expected[ident]['pngSha256'], 'Prepared source changed')
        field_raw = (prepared / 'upstream-metadata' / f'ds-form-{ident}.field.json').read_bytes()
        require(digest(field_raw) == expected[ident]['fieldSha256'], 'Pinned field metadata changed')
    candidate = dict(active_batch)
    candidate['version'] = active['version'] + 1
    candidate['entries'] = active_batch['entries'] + proposed['entries']
    audit_dir = transaction
    immutable(audit_dir / 'before-index.json', old_raw)
    immutable(audit_dir / 'review.json', review_raw)
    for record in records:
        source = record['entry']['inspection']['sources'][0]
        immutable(world / 'input' / record['entry']['mapping'], record['path'].read_bytes())
        immutable(world / 'input' / source['file'], (record['path'].parent / source['file']).read_bytes())
    # Build privately first. A failed comparison must never activate its index.
    candidate_path = world / 'input' / f".digigame-{report['planSha256'][:12]}.batch.json"
    immutable(candidate_path, canonical(candidate))
    candidate_output = audit_dir / 'candidate'
    merged = pipeline.build_batch(candidate_path, candidate_output, private_root=PRIVATE)
    require(merged['entries'][:len(active['entries'])] == active['entries'], 'Previously verified rows changed')
    staged_rows = {row['artId']: row for row in staged['entries']}
    for row in merged['entries'][len(active['entries']):]:
        require(row == staged_rows[row['artId']], 'Active artwork differs from reviewed staged artifacts')
    verify_frozen(baseline, active)
    require((world / 'index.json').read_bytes() == old_raw, 'Active index changed during preparation; retry from its new state')
    for row in merged['entries']:
        for kind in ('browser', 'device', 'provenance', 'audit'):
            descriptor = row[kind]
            immutable(world / descriptor['file'], checked_artifact(candidate_output, descriptor))
    result = dict(formatVersion=1, privateOnly=True, sourceCommit=report['sourceCommit'],
        planSha256=report['planSha256'], reviewSha256=digest(review_raw),
        baselineIndexSha256=BASELINE_SHA256, original87RowsAndArtifactsUnchanged=True,
        previousForms=len(active['entries']), addedForms=sorted(expected), forms=len(merged['entries']),
        version=merged['version'], indexSha256=digest(canonical(merged)),
        entries=report['entries'], sourceKind='digigame-pinned-runtime-strip', upstreamCodeExecuted=False)
    payloads = dict(oldIndex=old_raw, oldBatch=old_batch_raw, newIndex=canonical(merged),
                    newBatch=canonical(candidate), result=canonical(result))
    for key, raw in payloads.items():
        immutable(transaction / f'{key}.json', raw)
    immutable(transaction / 'intent.json', canonical(dict(formatVersion=1,
        planSha256=report['planSha256'], reviewSha256=digest(review_raw),
        hashes={key: digest(raw) for key, raw in payloads.items()})))
    return finish_activation(transaction, report['planSha256'], digest(review_raw))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--plan', type=Path)
    mode.add_argument('--activate', type=Path)
    parser.add_argument('--review', type=Path)
    args = parser.parse_args(argv)
    try:
        if args.plan:
            require(args.review is None, 'Review is used only for activation')
            output, report = prepare(args.plan)
            print(json.dumps(dict(output=str(output), status=report['status'], forms=len(report['entries']),
                                  budget=report['budget'], activeIndexChanged=False)))
        else:
            require(args.review is not None, 'Activation requires an internal independent review record')
            print(json.dumps(activate(args.activate, args.review)))
        return 0
    except (Failure, OSError, ValueError) as error:
        print(f'DigiGame preparation rejected: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

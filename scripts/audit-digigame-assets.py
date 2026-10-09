#!/usr/bin/env python3
"""Read-only mixed-origin private asset audit; emits metadata, never artwork."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('digigame_import_audit', ROOT / 'scripts/import-digigame-assets.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)


def audit():
    baseline, _ = m.frozen_baseline()
    world = m.ROOT / '.personal-assets/world-ds'
    active, raw = m.pipeline.read_json(world / 'index.json', 1024 * 1024)
    m.verify_frozen(baseline, active)
    catalog = m.read_json_bytes((m.ROOT / 'data/world-ds-ids.json').read_bytes(), 1024 * 1024)
    identities = {row['formId']: row['displayName'] for row in catalog['entries']}
    by_id = {row['artId']: row for row in active['entries']}
    m.require(len(by_id) == len(active['entries']), 'Duplicate active identity')
    proven = {row['artId'] for row in baseline['entries']}
    additions, reviews = [], []
    for receipt_path in sorted((m.PRIVATE / 'activations').glob('*/activation.json')):
        receipt, receipt_raw = m.pipeline.read_json(receipt_path, 128 * 1024)
        directory = receipt_path.parent
        intent, _ = m.pipeline.read_json(directory / 'intent.json', 128 * 1024)
        m.require(m.digest(receipt_raw) == intent['hashes']['result'], 'Activation receipt differs from durable intent')
        review, review_raw = m.pipeline.read_json(directory / 'review.json', 128 * 1024)
        m.require(review['status'] == 'approved' and m.digest(review_raw) == receipt['reviewSha256']
                  and review['planSha256'] == receipt['planSha256'] == intent['planSha256'], 'Independent review binding changed')
        approved = {row['formId']: row for row in review['entries']}
        version, version_raw = m.pipeline.read_json(directory / 'newIndex.json', 1024 * 1024)
        m.require(m.digest(version_raw) == receipt['indexSha256'] == intent['hashes']['newIndex'], 'Activated index evidence changed')
        recorded = {row['artId']: row for row in version['entries']}
        for row in receipt['entries']:
            ident = row['artId']; fid = row['formId']
            m.require(ident not in proven and ident in by_id and by_id[ident] == recorded[ident]
                      and row['name'] == identities[fid], 'Active identity differs from reviewed immutable addition')
            m.require(all(approved[fid][key] == row[key] for key in ('pngSha256', 'fieldSha256', 'mappingSha256')),
                      'Approval hash binding changed')
            mapping = world / 'input' / f'{ident}.mapping.json'
            mapping_raw = mapping.read_bytes(); mapping_data = m.read_json_bytes(mapping_raw)
            m.require(m.digest(mapping_raw) == row['mappingSha256'], 'Active mapping changed')
            source = m.pipeline.relative_path(world / 'input', mapping_data['sprites'][ident]['source'], '.png')
            m.require(m.digest(source.read_bytes()) == row['pngSha256'], 'Pinned normalized PNG changed')
            expected_url = f"{m.REPOSITORY}/blob/{receipt['sourceCommit']}/assets/characters/{row['sourceSlug']}/field.png"
            m.require(mapping_data['sprites'][ident]['provenance']['sourceUrl'] == expected_url, 'Pinned source URL differs')
            proven.add(ident); additions.append(fid)
        reviews.append(dict(planSha256=receipt['planSha256'], reviewSha256=receipt['reviewSha256'], forms=len(receipt['entries'])))
    m.require(proven == set(by_id), 'An active addition has no approved source-aware activation receipt')
    totals = dict(browser=0, device=0); objects = 0
    for ident, row in by_id.items():
        blobs = {kind: m.checked_artifact(world, row[kind]) for kind in ('browser', 'device', 'provenance', 'audit')}
        objects += 4
        browser = m.read_json_bytes(blobs['browser'], 256 * 1024)
        provenance = m.read_json_bytes(blobs['provenance'], 256 * 1024)
        device = m.read_json_bytes(blobs['audit'], 256 * 1024)
        m.require(browser['packId'] == f'personal-{ident}' and set(browser['sprites']) == {ident}
                  and browser['sprites'][ident]['name'] == row['name'] and browser['version'] == row['version'], 'Browser identity differs')
        m.require(provenance['packSha256'] == row['browser']['sha256'] and device['sha256'] == row['device']['sha256']
                  and device['sourceSha256'] == provenance['sources'][0]['sha256'], 'Pack provenance binding differs')
        m.pipeline.device.parse_dva(blobs['device'])
        for kind in totals: totals[kind] += len(blobs[kind])
    m.require(active['budget']['forms'] == len(by_id) and active['budget']['browserEncodedBytes'] == totals['browser']
              and active['budget']['deviceEncodedBytes'] == totals['device'], 'Index budget differs')
    return dict(formatVersion=1, result='PASS', scope='Read-only mixed-origin byte, identity and provenance audit; browser/native checks recorded separately.',
                activeIndexVersion=active['version'], activeIndexSha256=m.digest(raw), baselineIndexSha256=m.BASELINE_SHA256,
                preservedForms=len(baseline['entries']), original87RowsAndArtifactsUnchanged=True,
                integratedPacks=len(by_id), addedDigiGameForms=len(additions), verifiedPackArtifacts=objects,
                browserEncodedBytes=totals['browser'], deviceEncodedBytes=totals['device'],
                sourceCommit=reviews and receipt['sourceCommit'] or None, newFormIds=sorted(additions),
                missingFormIds=sorted(set(identities)-{int(i[8:]) for i in by_id}), independentReviews=reviews,
                privateArtworkPublished=False, upstreamCodeExecuted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--output', type=Path); args = parser.parse_args()
    try:
        result = audit(); raw = json.dumps(result, indent=2) + '\n'
        if args.output: args.output.write_text(raw)
        print(raw, end='')
    except (m.Failure, OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps(dict(result='FAIL', error=str(error)))); raise SystemExit(1)

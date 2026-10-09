#!/usr/bin/env python3
"""Compare the final native run to selected isolated candidates; no fights run."""
import hashlib
import importlib.util
import itertools
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/balance-investigation'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows(path, kind):
    with path.open() as source:
        for line in source:
            row = json.loads(line)
            if row['kind'] != kind or row.get('policy') == 'public-quarter':
                continue
            row.pop('firstCaptureHpPermille', None)
            row.pop('floorBindings', None)
            yield row


def main():
    current = ROOT / 'build/world-ds-balance/battles.jsonl'
    sources = {'wild': BUILD / 'capture50/results.jsonl',
               'damage': BUILD / 'capture50/results.jsonl',
               'practice': BUILD / 'practice-floor/div20/results.jsonl'}
    comparisons = {}
    for kind, source in sources.items():
        count = 0
        for actual, candidate in itertools.zip_longest(rows(current, kind), rows(source, kind)):
            assert actual == candidate, (kind, count, actual, candidate)
            count += 1
        comparisons[kind] = {'rows': count, 'exactJsonEqual': True,
                             'candidateRawPath': str(source.relative_to(ROOT)),
                             'candidateRawSha256': sha(source)}
    report = json.loads((ROOT / 'docs/evidence/world-ds-balance.json').read_text())
    assert report['execution']['rawSha256'] == sha(current)
    for path, digest in report['execution']['sourceHashes'].items():
        assert sha(ROOT / path) == digest, 'source changed after final audit: ' + path
    spec = importlib.util.spec_from_file_location('summary', ROOT / 'scripts/investigate-balance.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    forms = {f['formId']: f for f in json.loads((ROOT / 'data/world-ds-runtime.json').read_text())['forms']}
    distribution = module.summarize(current, forms)['formLevelDistributions']
    result = {
        'formatVersion': 1, 'kind': 'final-native-selection-confirmation',
        'catalogVersion': report['catalogVersion'], 'rulesVersion': report['rulesVersion'],
        'practiceRulesVersion': 6,
        'selected': {'capture': '50+40*(maxHp-2*hp)/maxHp, integer; existing half-HP eligibility',
                     'heavyPower': 16, 'heavyEnergy': 6, 'practiceExchangeCap': 40,
                     'practiceMinimumRawDamage': 'max(4,ceil(intendedDefenderMaxHp/20)), validated4..32 before type and guard; newPractice6 only'},
        'sameCaseSha256': report['execution']['caseSha256'],
        'actualRawSha256': sha(current), 'comparisons': comparisons,
        'sourceHashesCurrent': True,
        'formLevelDistributions': distribution,
        'limits': ['Exact measurement-row comparison, not a persistence/browser/hardware test.',
                   'Form-level quantiles weight each sampled form-level point equally, not real encounters.',
                   'Success in capture-first distributions means capture OR defeat; separate aggregate capture rates are in the full report.']}
    path = ROOT / 'docs/evidence/gameplay-selected-verification.json'
    path.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS:', {kind: row['rows'] for kind, row in comparisons.items()}, path)


if __name__ == '__main__':
    main()

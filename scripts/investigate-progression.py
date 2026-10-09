#!/usr/bin/env python3
"""Native baseline progression action counts; no production source changes.

First run scripts/investigate-balance.py --variants baseline to prepare the
immutable baseline and shared native policy. --reuse only summarizes saved raw.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import math
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/balance-investigation'
spec = importlib.util.spec_from_file_location('candidate', ROOT / 'scripts/investigate-balance.py')
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def distribution(values):
    values = sorted(values)
    return {'min': values[0], 'p50': values[math.ceil(len(values) * .5) - 1],
            'p95': values[math.ceil(len(values) * .95) - 1], 'max': values[-1],
            'mean': round(sum(values) / len(values), 6)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reuse', action='store_true')
    parser.add_argument('--current', action='store_true', help='Run actual working-tree care rules and compare all checkpoints with pinned baseline')
    args = parser.parse_args()
    if args.current:
        source = ROOT / 'core'
        source_paths = [str(path.relative_to(ROOT)) for path in sorted(source.iterdir()) if path.is_file()]
        helper = BUILD / 'current-progression'
        helper.mkdir(parents=True, exist_ok=True)
        (helper / 'audit.cpp').write_bytes((ROOT / 'tests/world_ds_balance_audit.cpp').read_bytes())
        raw, care = helper / 'progression.jsonl', helper / 'care.txt'
        sources = candidate.SOURCES + ['legacy_forms_v7.cpp', 'legacy_combat_v7.cpp']
    else:
        source = BUILD / 'source-baseline'
        source_paths = subprocess.check_output(
            ['git', 'ls-tree', '-r', '--name-only', candidate.PIN, 'core'], cwd=ROOT, text=True).splitlines()
        for path in source_paths:
            assert (source / pathlib.Path(path).name).read_bytes() == candidate.git_bytes(path)
        helper = BUILD / 'baseline'
        assert (helper / 'audit.cpp').read_text() == candidate.native_harness()
        raw, care = BUILD / 'progression.jsonl', BUILD / 'care.txt'
        sources = candidate.SOURCES
    initial_hashes = {path: sha(source / pathlib.Path(path).name) for path in source_paths}
    harness = ROOT / 'tests/gameplay_progression_audit.cpp'
    command = ['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-I' + str(source), '-I' + str(helper), str(harness),
               *[str(source / name) for name in sources], '-o', str(helper / 'progression')]
    if not args.reuse:
        subprocess.run(command, cwd=ROOT, check=True)
        with raw.open('w') as out, care.open('w') as err:
            subprocess.run([helper / 'progression'], cwd=ROOT, stdout=out, stderr=err, check=True)
    rows = [json.loads(line) for line in raw.read_text().splitlines()]
    assert len(rows) == 384
    groups = collections.defaultdict(list)
    for row in rows:
        groups[row['checkpoint'], 'all'].append(row)
        groups[row['checkpoint'], str(row['branch'])].append(row)
    summary = []
    for (checkpoint, branch), selected in sorted(groups.items()):
        summary.append({'checkpoint': checkpoint, 'branch': branch, 'paths': len(selected),
                        'metrics': {key: distribution([row[key] for row in selected])
                                    for key in ['fights', 'wins', 'retreats', 'battleActions',
                                                'recoveryRests', 'bond', 'evolutions']}})
    care_rows = []
    for line in care.read_text().splitlines():
        match = re.fullmatch(r'CARE mode=(\d+) actions=(\d+) level=(\d+) xp=(\d+) bond=(\d+)', line)
        if match:
            care_rows.append(dict(zip(['mode', 'actions', 'level', 'xp', 'bond'], map(int, match.groups()))))
    assert len(care_rows) == 3 and all(row['level'] == 1 and row['xp'] == 0 for row in care_rows)
    result = {
        'formatVersion': 1, 'kind': 'host-native-current-progression-not-physical-time' if args.current else 'host-native-baseline-progression-not-physical-time',
        'baselineCommit': candidate.PIN, 'productionChanged': False,
        'paths': 96, 'starters': 8, 'seedsPerPath': 4,
        'branchChoices': {'0': 'Remain Rookie', '1': 'First outgoing branch',
                          '2': 'Second outgoing branch; first when only one exists'},
        'seedFormula': '(0x9e3779b9 * (seedIndex + 1)) XOR 0x51f15e5d, uint32',
        'policy': 'Actual Hatch/Walk100 encounters; public greedy damage without capture; native Rest until full HP and energy>=80; evolve immediately at real gates for selected branch. No invented opponents or RNG overrides.',
        'limits': ['Four deterministic seeds, no real encounter-frequency probability claim.',
                   'Rest counts are immediate actions, not elapsed time or required inactivity.',
                   'Actual working-tree graph; all checkpoints compared to the pinned 163-edge baseline.' if args.current else 'This is the pinned 163-edge baseline, not the subsequently changed production graph.',
                   'Measured wins-only progression; capture-heavy collection routes are a separate objective.'],
        'summaries': summary, 'careProbes': care_rows,
        'careModes': {'0': '100 Feed', '1': '100 Rest', '2': '100 Play plus Rest pairs'},
        'rows': rows,
        'evidence': {'rawSha256': sha(raw), 'careSha256': sha(care), 'harnessSha256': sha(harness),
                     'sharedPolicySha256': sha(helper / 'audit.cpp'),
                     'sourceHashes': {path: sha(source / pathlib.Path(path).name) for path in source_paths},
                     'buildCommand': command}}
    assert initial_hashes == {path: sha(source / pathlib.Path(path).name) for path in source_paths}, 'native source changed during progression run'
    if args.current:
        baseline = json.loads((ROOT / 'docs/evidence/gameplay-progression-baseline.json').read_text())
        assert rows == baseline['rows'], 'current progression checkpoints differ from baseline'
        assert care_rows == baseline['careProbes'], 'current care probes differ from baseline'
        result['pairedBaseline'] = {'baselineCommit': candidate.PIN, 'checkpointsCompared': len(rows),
                                    'allRowsExactlyEqual': True, 'careProbesExactlyEqual': True,
                                    'baselineEvidenceSha256': sha(ROOT / 'docs/evidence/gameplay-progression-baseline.json')}
        result['sourceCommit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dest = ROOT / ('docs/evidence/gameplay-progression-current.json' if args.current else 'docs/evidence/gameplay-progression-baseline.json')
    dest.write_text(json.dumps(result, indent=2) + '\n')
    print(f'PASS: 96 native paths, 384 checkpoints, 3 bounded care probes; {dest}')


if __name__ == '__main__':
    main()

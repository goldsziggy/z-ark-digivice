#!/usr/bin/env python3
"""Bounded native rules4/rules5 comparison using identical matchup-derived seeds."""
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build/world-ds-early-baseline'
BASELINE = '916c0d8'


def run(command, input=None):
    return subprocess.run(command, cwd=ROOT, input=input, capture_output=True, text=True, check=True).stdout


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    before = BUILD / 'before'
    before.mkdir(exist_ok=True)
    source_names = ['game', 'combat', 'forms', 'battle_trace', 'legacy_combat_v3']
    for name in source_names:
        for suffix in ['.cpp', '.hpp']:
            (before / (name + suffix)).write_text(run(['git', 'show', f'{BASELINE}:core/{name}{suffix}']))
    fixtures = []
    for player in [11, 18, 25, 32, 39, 46, 53, 60]:
        for level in [1, 5]:
            for enemy in [4, 5, 8]:
                for index in range(4):
                    key = f'world-ds-balance-v1:{player}:{level}:{enemy}:{level}:{index}'
                    seed = int.from_bytes(hashlib.sha256(key.encode()).digest()[:4], 'big') or 1
                    fixtures.append(dict(player=player, level=level, enemy=enemy, enemyLevel=level, seedIndex=index, seed=seed))
    text = ''.join(f'{f["player"]} {f["level"]} {f["enemy"]} {f["seedIndex"]} {f["seed"]}\n' for f in fixtures)
    (BUILD / 'cases.txt').write_text(text)
    results = {}
    for label, source in [('before', before), ('after', ROOT / 'core')]:
        binary = BUILD / ('probe-' + label)
        command = shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I' + str(source)]
        if label == 'before': command.append('-DWORLD_DS_OLD_BASELINE')
        command += ['tests/world_ds_early_baseline.cpp'] + [str(source / (n + '.cpp')) for n in source_names] + ['-o', str(binary)]
        run(command)
        output = run([str(binary)], text)
        (BUILD / (label + '.jsonl')).write_text(output)
        results[label] = [json.loads(line) for line in output.splitlines()]
    assert len(results['before']) == len(results['after']) == len(fixtures) * 6
    for old, new in zip(results['before'], results['after']):
        assert all(old[k] == new[k] for k in ('player', 'level', 'enemy', 'seedIndex', 'seed', 'fightRng', 'policy', 'objective'))
    summaries = []
    for label, rows in results.items():
        for level in [1, 5]:
            for objective in ['capture-first', 'defeat-full-collection']:
                for policy in ['auto', 'public-greedy', 'public-economy']:
                    group = [r for r in rows if r['level'] == level and r['objective'] == objective and r['policy'] == policy]
                    summaries.append(dict(version=label, rules=group[0]['rules'], level=level, objective=objective, policy=policy,
                        fights=len(group), outcomes=dict(Counter(r['outcome'] for r in group)),
                        meanTurns=statistics.mean(r['turns'] for r in group), meanEnergySpent=statistics.mean(r['energySpent'] for r in group),
                        meanRecoveryRests=statistics.mean(r['recoveryRests'] for r in group),
                        choices=[sum(r['choices'][i] for r in group) for i in range(4)], guards=[sum(r['guards'][i] for r in group) for i in range(3)]))
    report = dict(formatVersion=1, baselineCommit=BASELINE, scope='Native host simulation only; no hardware or field-play evidence.',
        description='Eight preserved Rookie founders at levels1 and5 versus the same Flicker/Rill/Cinder forms at equal level, four seeds per matchup. This isolates rule/policy changes; it does not sample the expanded rival pool.',
        seedDerivation='First4 bytes, big-endian SHA256(world-ds-balance-v1:playerForm:level:enemyForm:enemyLevel:seedIndex), zero replaced with1.',
        fixture='Same founder stats/care, energy80, sequence100, encounters7+seed%3, actual native Walk100. Capture fixture has1member/captures0; defeat fixture has8members/captures7. New-only journal/nextMemberId fields satisfy valid-state rules. Explicit rival replacement isolates identical form/level matchups.',
        policies='Auto uses each native version. Public-greedy captures when eligible; otherwise maximizes immediate HP swing using only visible guard/profiles/HP. Ties Physical, Magic, Heavy; Heavy requires energy6. Public-economy excludes Heavy. Baseline has no wild guard; new rules expose Brace/Ward/Counter. No cards or hidden intent/RNG reads for decisions.',
        limitation='Fixture historical counters and abundant starting care isolate fights; this is not a first-encounter onboarding flow or independent probability estimate. Capture success is not defeat victory. Common seeds and fight-start RNG match exactly before/after.',
        caseCount=len(fixtures), fightCountPerVersion=len(results['before']), fixtures=fixtures, summaries=summaries,
        rawSha256={key:hashlib.sha256((BUILD / (key + '.jsonl')).read_bytes()).hexdigest() for key in results},
        currentSourceSha256={f'core/{name}{suffix}':hashlib.sha256((ROOT / f'core/{name}{suffix}').read_bytes()).hexdigest()
                            for name in source_names for suffix in ['.cpp', '.hpp']})
    destination = ROOT / 'docs/evidence/world-ds-early-baseline.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(fixtures)} common-seed cases; {len(results["before"])} native fights per version; initial RNG identical.')
    for s in summaries:
        print(f'{s["version"]} L{s["level"]} {s["objective"]} {s["policy"]}: {s["outcomes"]}; turns{s["meanTurns"]:.2f}, energy{s["meanEnergySpent"]:.2f}, rests{s["meanRecoveryRests"]:.2f}')
    print(destination.relative_to(ROOT))


if __name__ == '__main__':
    main()

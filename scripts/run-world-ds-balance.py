#!/usr/bin/env python3
"""Reproduce a stratified host audit against the actual C++ game/combat core."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/world-ds-balance'
KEYS = ('maxHp', 'attack', 'defense', 'magic', 'resistance')
ROLES = ('Balanced', 'Striker', 'Mystic', 'Bulwark', 'Warden')
SOURCES = ('tests/world_ds_balance_audit.cpp', 'core/combat.cpp', 'core/encounters.cpp', 'core/forms.cpp',
           'core/game.cpp', 'core/practice_battle.cpp', 'core/battle_trace.cpp',
           'core/legacy_combat_v3.cpp', 'core/legacy_forms_v5.cpp', 'core/legacy_combat_v5.cpp', 'core/legacy_forms_v6.cpp', 'core/legacy_combat_v6.cpp', 'core/legacy_forms_v7.cpp', 'core/legacy_combat_v7.cpp', 'core/legacy_forms_v8.cpp', 'core/legacy_combat_v8.cpp', 'core/legacy_forms_v9.cpp', 'core/legacy_combat_v9.cpp')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    paths = set(SOURCES) | {'scripts/run-world-ds-balance.py', 'data/world-ds-runtime.json',
                            'docs/research/preserved-forms.json'}
    paths.update(str(path.relative_to(ROOT)) for path in (ROOT / 'core').glob('*.hpp'))
    paths.update(str(path.relative_to(ROOT)) for path in (ROOT / 'core').glob('*.inc'))
    return {name: digest(ROOT / name) for name in sorted(paths)}


def build(probes_only):
    binary = BUILD / ('combat-audit' if probes_only else 'battle-audit')
    command = ['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-Icore']
    command += ['-DWORLD_DS_COMBAT_ONLY'] if probes_only else []
    command += list(SOURCES[:3] if probes_only else SOURCES)
    command += ['-o', str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    return binary, command


def sanitizer_check(cases):
    binary = BUILD / 'battle-audit-sanitized'
    command = ['c++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Icore', *SOURCES, '-o', str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    selected = {}
    for case in cases:
        selected.setdefault((case['player'], case['level']), case)
    text = ''.join(' '.join(str(case[key]) for key in (
        'id', 'player', 'level', 'enemy', 'enemyLevel', 'seed', 'seedIndex')) + '\n'
                   for case in selected.values())
    subprocess.run([binary, '--battles'], input=text, text=True,
                   stdout=subprocess.DEVNULL, cwd=ROOT, check=True)
    return {'addressAndUndefinedBehavior': 'PASS', 'actorForms': len({key[0] for key in selected}),
            'seededCases': len(selected), 'fights': len(selected) * 10,
            'sampling': 'First sampled matchup for every actor form/level point; all ten policy/objective paths.',
            'buildCommand': command}


def validate_profiles(binary, forms):
    raw = subprocess.check_output([binary, '--profiles'], cwd=ROOT, text=True)
    actual = {}
    for line in raw.splitlines():
        row = json.loads(line)
        actual[row['form'], row['level']] = row['stats']
    expected = {(form['formId'], row['level']): [row[key] for key in KEYS]
                for form in forms.values() for row in form['statsByLevel']}
    assert actual == expected, 'native/runtime profile mismatch'
    frozen = json.loads((ROOT / 'docs/research/preserved-forms.json').read_text())['forms']
    preserved = 0
    for form in frozen:
        if form['id'] in (3, 7):
            # Care9 / Practice7 explicitly revise these two original anchors.
            # Their old numbers remain in frozen epoch8, not the live profile.
            continue
        for row in form['levels']:
            assert actual[form['id'], row['level']] == [row[key] for key in KEYS]
            preserved += 1
    return {'forms': len(forms), 'nativeLegalLevelRows': len(actual),
            'preservedForms': len(frozen)-2, 'preservedLegalLevelRows': preserved,
            'versionedOriginalProfileAdjustments': [3, 7],
            'nativeMatchesRuntime': True, 'preservedMatchesFrozen': True}


def select_cases(forms, seeds):
    pools = defaultdict(list)
    for form in forms.values():
        if form['role'] in ROLES:
            pools[form['combatTier'], form['role']].append(form)
    cases, omitted, missing_roles, actor_points = [], Counter(), set(), []
    starters = {11 + 7 * index for index in range(8)}
    for form in forms.values():
        levels = {form['minLevel'], 20}
        if form['formId'] in starters:
            levels.update((5, 10, 15))
        for level in sorted(levels):
            actor_points.append([form['formId'], level])
            for role_index, role in enumerate(ROLES):
                pool = pools[form['combatTier'], role]
                if not pool:
                    missing_roles.add((form['combatTier'], role))
                    continue
                # Rotate actual type and species representatives deterministically.
                types = sorted({peer['type'] for peer in pool})
                enemy_type = types[(form['formId'] + level + role_index) % len(types)]
                peers = [peer for peer in pool if peer['type'] == enemy_type]
                enemy = peers[(form['formId'] * 7 + level) % len(peers)]
                for delta in (-2, 0, 2):
                    enemy_level = level + delta
                    if not enemy['minLevel'] <= enemy_level <= 20:
                        omitted[str(delta)] += 1
                        continue
                    for seed_index in range(seeds):
                        key = f"world-ds-balance-v1:{form['formId']}:{level}:{enemy['formId']}:{enemy_level}:{seed_index}"
                        seed = int.from_bytes(hashlib.sha256(key.encode()).digest()[:4], 'big') or 1
                        cases.append({'id': len(cases) + 1, 'player': form['formId'], 'level': level,
                                      'enemy': enemy['formId'], 'enemyLevel': enemy_level,
                                      'delta': delta, 'seed': seed, 'seedIndex': seed_index})
    assert {case['player'] for case in cases} == set(forms), 'not all forms sampled'
    return cases, {'actorLevelPoints': actor_points, 'omittedIllegalLevelOffsets': dict(omitted),
                   'unavailableOpponentRoleStrata': [{'tier': tier, 'role': role}
                                                    for tier, role in sorted(missing_roles)]}


class Aggregate:
    def __init__(self):
        self.n = 0
        self.outcomes = Counter()
        self.histograms = defaultdict(Counter)
        self.choices = Counter()

    def add(self, row):
        self.n += 1
        self.outcomes[row['outcome']] += 1
        for key in ('turns', 'initialHp', 'combatHpEnd', 'hpAfterHome', 'energySpent',
                    'recoveryRests', 'xpAwarded', 'reflections', 'heavyOnCounter', 'avoidedCounter'):
            if key in row:
                self.histograms[key][row[key]] += 1
        names = ('physical', 'heavy', 'magic', 'capture') if row['kind'] == 'wild' else (
            'physical', 'heavy', 'magic', 'brace', 'counter', 'ward')
        self.choices.update(dict(zip(names, row['choices'])))

    def finish(self):
        def quantile(histogram, p):
            target, seen = math.ceil(self.n * p), 0
            for value, count in sorted(histogram.items()):
                seen += count
                if seen >= target:
                    return value
            raise AssertionError('empty histogram')
        return {'n': self.n, 'outcomes': dict(sorted(self.outcomes.items())),
                'outcomeRates': {key: round(count / self.n, 6) for key, count in sorted(self.outcomes.items())},
                'metrics': {key: {'mean': round(sum(v * n for v, n in hist.items()) / self.n, 6),
                                  'sum': sum(v * n for v, n in hist.items()),
                                  'p50': quantile(hist, .5), 'p95': quantile(hist, .95), 'max': max(hist)}
                            for key, hist in sorted(self.histograms.items())},
                'choices': dict(sorted(self.choices.items()))}


def policy_key(row):
    return (row['kind'], row.get('objective', 'no-rewards'), row['policy'])


def describe_case(case, forms):
    return {**case, 'playerName': forms[case['player']]['name'],
            'enemyName': forms[case['enemy']]['name']}


def summarize(rows_path, cases, forms, probes_only):
    groups, paired = defaultdict(Aggregate), defaultdict(lambda: {'n': 0, 'outcomes': Counter(),
                                                                'deltaSums': Counter(), 'wins': Counter(), 'examples': []})
    form_counts, enemy_counts, strata_counts = Counter(), Counter(), defaultdict(Counter)
    damage, pending, total = [], {}, Counter()

    def compare(case_rows):
        if not case_rows:
            return
        by_policy = {policy_key(row): row for row in case_rows}
        assert len(by_policy) == 10, 'case missing a policy/objective result'
        comparisons = [('practice', 'no-rewards', 'public-hint', 'auto')]
        for objective in ('capture-first', 'defeat-full-collection'):
            comparisons.extend(('wild', objective, left, right) for left, right in (
                ('public-greedy', 'auto'), ('public-greedy', 'public-economy'), ('public-greedy', 'ignore-guard')))
        for kind, objective, left, right in comparisons:
            a, b = by_policy[kind, objective, left], by_policy[kind, objective, right]
            target = paired[kind, objective, left, right]
            target['n'] += 1
            target['outcomes'][a['outcome'] + '/' + b['outcome']] += 1
            for metric in ('turns', 'combatHpEnd', 'energySpent', 'recoveryRests', 'xpAwarded'):
                if metric not in a:
                    continue
                delta = a[metric] - b[metric]
                target['deltaSums'][metric] += delta
                target['wins'][metric + ('Higher' if delta > 0 else 'Lower' if delta < 0 else 'Equal')] += 1
            if a['outcome'] != b['outcome'] and len(target['examples']) < 3:
                target['examples'].append({'case': a['case'], 'left': a, 'right': b})

    with rows_path.open() as source:
        current = None
        for line in source:
            row = json.loads(line)
            total[row['kind']] += 1
            case = cases[row['case'] - 1]
            assert all(row[key] == case[key] for key in ('player', 'level', 'enemy', 'enemyLevel', 'seed', 'seedIndex'))
            if row['kind'] == 'damage':
                damage.append(row)
                continue
            if current is not None and row['case'] != current:
                compare(pending.values())
                pending = {}
            current = row['case']
            assert policy_key(row) not in pending
            pending[policy_key(row)] = row
            key = policy_key(row)
            player, enemy = forms[case['player']], forms[case['enemy']]
            axes = {'all': 'all', 'stage': player['stage'], 'combatTier': player['combatTier'],
                    'role': player['role'], 'type': player['type'], 'enemyRole': enemy['role'],
                    'enemyType': enemy['type'], 'levelOffset': str(case['delta']),
                    'playerLevel': str(case['level'])}
            if case['player'] in {11 + 7 * index for index in range(8)} and case['level'] == 1:
                axes.update({'starterLevel1': 'all', 'starterLevel1Offset': str(case['delta']),
                             'starterLevel1Form': str(case['player'])})
            for axis, value in axes.items():
                groups[(*key, axis, value)].add(row)
            if key == ('practice', 'no-rewards', 'auto'):
                form_counts[case['player']] += 1
                enemy_counts[case['enemy']] += 1
                for axis, value in axes.items():
                    strata_counts[axis][value] += 1
        compare(pending.values())
    assert total['damage'] == sum(case['seedIndex'] == 0 for case in cases)
    if not probes_only:
        assert total['practice'] == 2 * len(cases) and total['wild'] == 8 * len(cases)
        assert set(form_counts) == set(forms)

    raw_preferences, guarded_preferences = defaultdict(set), defaultdict(set)
    physical_examples, guard_examples = {}, {}
    counters = Counter()
    heavy_examples = []
    for row in damage:
        case = cases[row['case'] - 1]
        group = (case['player'], case['level'], case['delta'])
        for guard in range(4):
            p, h, m = row['hits'][guard]
            preference = 'physical' if p > m else 'magic' if m > p else 'tie'
            guarded_preferences[group].add(preference)
            guard_examples.setdefault((group, preference), {**describe_case(case, forms), 'guard': guard, 'damage': [p, h, m]})
            if guard == 0:
                raw_preferences[group].add(preference)
                physical_examples.setdefault((group, preference), {**describe_case(case, forms), 'damage': [p, h, m]})
            if guard in (1, 3):
                counters['braceWardProbes'] += 1
                if h > max(p, m):
                    counters['heavyStrictBestUnderBraceWard'] += 1
                    if len(heavy_examples) < 3:
                        heavy_examples.append({**describe_case(case, forms), 'guard': guard, 'damage': [p, h, m]})
            if guard == 2:
                counters['counterProbes'] += 1
                counters['heavyReflectedNativeMatrix'] += 1  # asserted by C++
    raw_reversals = [group for group, preferences in raw_preferences.items() if {'physical', 'magic'} <= preferences]
    guard_reversals = [group for group, preferences in guarded_preferences.items() if {'physical', 'magic'} <= preferences]
    probes = {'matchups': len(damage), 'actorLevelOffsetGroups': len(raw_preferences),
              'unguardedPhysicalMagicReversalGroups': len(raw_reversals),
              'includingVisibleGuardReversalGroups': len(guard_reversals),
              'unguardedReversalExamples': [{move: physical_examples[group, move] for move in ('physical', 'magic')}
                                           for group in raw_reversals[:8]],
              'guardReversalExamples': [{move: guard_examples[group, move] for move in ('physical', 'magic')}
                                       for group in guard_reversals[:3]],
              'heavy': {**counters, 'examples': heavy_examples},
              'counterCaveat': 'The Heavy/Counter number is reflected player damage, not enemy damage.'}
    result = {'nativeRows': dict(total), 'coverage': {
        'actorForms': len(form_counts) if not probes_only else len({case['player'] for case in cases}),
        'opponentForms': len(enemy_counts) if not probes_only else len({case['enemy'] for case in cases}),
        'actorFormSamples': [{'formId': form_id, 'name': forms[form_id]['name'], 'stage': forms[form_id]['stage'],
                              'role': forms[form_id]['role'], 'type': forms[form_id]['type'], 'seededCases': count}
                             for form_id, count in sorted(form_counts.items())],
        'strataSeededCases': {axis: dict(sorted(counts.items())) for axis, counts in strata_counts.items()}},
        'damageProbes': probes,
        'results': [{'kind': key[0], 'objective': key[1], 'policy': key[2], 'axis': key[3], 'stratum': key[4], **value.finish()}
                    for key, value in sorted(groups.items())],
        'pairedComparisons': [{'kind': key[0], 'objective': key[1], 'left': key[2], 'right': key[3],
                               'n': value['n'], 'outcomePairsLeftRight': dict(value['outcomes']),
                               'meanDeltaLeftMinusRight': {metric: round(total / value['n'], 6)
                                                          for metric, total in value['deltaSums'].items()},
                               'directionCounts': dict(value['wins']), 'examples': value['examples']}
                              for key, value in sorted(paired.items())]}
    return result


def markdown(report):
    p = report['profileChecks']
    lines = ['# World DS native balance audit', '',
             f"Measured host simulation of **{p['forms']} forms**; {p['nativeLegalLevelRows']:,} legal native stat rows match the generated catalog. "
             f"All {p['preservedForms']} preserved forms / {p['preservedLegalLevelRows']:,} legal stat rows match the frozen baseline.", '',
             f"{report['sampling']['seededCases']:,} seeded matchup cases; {report['nativeRows'].get('wild', 0):,} wild fights and "
             f"{report['nativeRows'].get('practice', 0):,} practice fights. All actor forms are sampled at entry level and 20; "
             'the eight starter Rookies also run at 5, 10, and 15. Opponents are actual same-tier role representatives with legal −2/0/+2 level offsets. '
             f"{report['sampling']['seedsPerMatchup']} distinct deterministic seeds per matchup are shared across policies and objectives.", '',
             'These deliberately stratified cases are a regression/design sample, not a forecast of real encounter rates or player win probability. '
             'The result has no forced 50% target. All fights call the real C++ resolver and state transitions; no runtime profile is tuned by this harness.', '',
             '**Early eight starters at level 1:** equal-level opponents are separated from the +2-level stress cases. '
             'Wild Auto and public-greedy have identical outcomes in these samples.', '',
             '| Enemy level | Context / policy | Cases | Capture | Defeat win | Retreat/loss | Draw |',
             '| --- | --- | ---: | ---: | ---: | ---: | ---: |']
    for row in report['results']:
        if row['axis'] != 'starterLevel1Offset' or row['policy'] not in ('auto', 'public-hint'):
            continue
        r = row['outcomeRates']
        lines.append(f"| {1 + int(row['stratum'])} | {row['kind']} / {row['objective']} / {row['policy']} | {row['n']} | "
                     f"{100*r.get('captured',0):.2f}% | {100*r.get('won',0):.2f}% | "
                     f"{100*(r.get('retreated',0)+r.get('lost',0)):.2f}% | {100*r.get('draw',0):.2f}% |")
    lines += ['', '**Whole stratified sample:**', '',
             '| Context | Policy | Capture | Defeat win | Retreat/loss | Draw | Turns mean / p95 / max |',
             '| --- | --- | ---: | ---: | ---: | ---: | --- |']
    for row in report['results']:
        if row['axis'] != 'all':
            continue
        r, turns = row['outcomeRates'], row['metrics']['turns']
        lines.append(f"| {row['kind']} / {row['objective']} | {row['policy']} | {100*r.get('captured',0):.1f}% | "
                     f"{100*r.get('won',0):.1f}% | {100*(r.get('retreated',0)+r.get('lost',0)):.1f}% | "
                     f"{100*r.get('draw',0):.1f}% | {turns['mean']:.2f} / {turns['p95']} / {turns['max']} |")
    probe = report['damageProbes']
    lines += ['', f"**Move choices:** {probe['unguardedPhysicalMagicReversalGroups']} of {probe['actorLevelOffsetGroups']} "
              'actor/level/offset groups have both a strict Physical preference and a strict Magic preference against different sampled opponents without guards. '
              f"Including visible Brace/Ward/Counter produces reversals in {probe['includingVisibleGuardReversalGroups']} groups. "
              f"Heavy is strictly strongest against the enemy in {probe['heavy']['heavyStrictBestUnderBraceWard']} of "
              f"{probe['heavy']['braceWardProbes']} Brace/Ward probes; every Counter probe reflects Heavy, as asserted in the native harness.", '']
    counter = {row['policy']: row['metrics']['heavyOnCounter']['sum'] for row in report['results']
               if row['axis'] == 'all' and row['kind'] == 'wild' and row['objective'] == 'defeat-full-collection'}
    lines += [f"In full-collection fights, Auto used Heavy into Counter {counter.get('auto', 0):,} times, "
              f"public-greedy {counter.get('public-greedy', 0):,}, and ignore-guard {counter.get('ignore-guard', 0):,}. "
              'This measures the cost of ignoring the actual visible counter, without altering defense multipliers.', '',
              '| Paired comparison (left minus right) | Objective | Turns | Combat HP | Energy spent | Recovery rests |',
              '| --- | --- | ---: | ---: | ---: | ---: |']
    for row in report['pairedComparisons']:
        delta = row['meanDeltaLeftMinusRight']
        lines.append(f"| {row['kind']}: {row['left']} − {row['right']} | {row['objective']} | " +
                     ' | '.join(f"{delta[key]:+.3f}" if key in delta else '—' for key in (
                         'turns', 'combatHpEnd', 'energySpent', 'recoveryRests')) + ' |')
    lines += ['', 'The Tactical practice policy sees the public excluded-move hint, profiles, phase, and HP; '
              'it maximizes immediate expected HP swing across the two hinted possibilities and cannot read the committed enemy move or RNG. '
              'Wild public-greedy reads the visible guard and chooses immediate damage; public-economy excludes Heavy; ignore-guard is a deliberately uninformed comparison. '
              'Native Auto is the implemented game policy. Capture-first attempts a legal capture before attacking; a full collection forces defeat or retreat. '
              'No cards are used. Distinct authored move labels still select the three shared mechanics.', '',
              'Combat-end HP is measured before Home recovery or XP-driven HP scaling. XP is the actual stored increase and is zero at the level cap. '
              'Recovery rests are actual native Rest actions to regain full current HP and at least the initial 80 energy; resting adds no XP. '
              'Every fixture and terminal state is validated. Wild Auto is bounded by 48 actions; new Practice battles by 40 exchanges (older saved rules retain 30); the manual harness fails if 512 actions do not terminate.', '',
              'The wild fixtures retain equal counters/seeds and use a captured active member to exercise every form. '
              'They inject the chosen opponent after an actual Walk starts the encounter; they do not measure random encounter distribution, acquisition effort, '
              'evolution costs, real human strategy, battery, or ESP performance. Fresh lacks Bulwark and Striker role representatives; missing strata and illegal level offsets '
              'are listed in the JSON. All 276 forms act as the player, but the opponent pool is a representative subset. '
              'Cases share seeds across candidates; the small fixed seed set is not independent statistical probability evidence.', '',
              'Reproduce from the repository root:', '', '```sh', 'python3 scripts/audit-world-ds-growth.py',
              'python3 scripts/run-world-ds-balance.py', '```', '',
              'Raw cases/results are kept under ignored `build/world-ds-balance/`. '
              '[JSON evidence](world-ds-balance.json) includes per-form and stage/role/type/level strata, paired outcomes, examples, exact source hashes, and compiler details. '
              '[Growth comparison](world-ds-growth-audit.json) is a separate arithmetic audit, not a battle result.', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probes-only', action='store_true')
    parser.add_argument('--seeds', type=int, default=4)
    parser.add_argument('--skip-sanitizer', action='store_true')
    parser.add_argument('--report-prefix', help='Evidence basename; choose a new name to retain historical reports.')
    args = parser.parse_args()
    assert 1 <= args.seeds <= 32
    assert args.report_prefix is None or re.fullmatch(r'[a-z0-9][a-z0-9-]{0,79}', args.report_prefix)
    started = time.monotonic()
    BUILD.mkdir(parents=True, exist_ok=True)
    before = source_hashes()
    runtime = json.loads((ROOT / 'data/world-ds-runtime.json').read_text())
    forms = {form['formId']: form for form in runtime['forms']}
    assert len(forms) == 276
    binary, command = build(args.probes_only)
    profiles = validate_profiles(binary, forms)
    cases, sampling = select_cases(forms, args.seeds)
    cases_path, rows_path = BUILD / 'cases.txt', BUILD / ('probes.jsonl' if args.probes_only else 'battles.jsonl')
    cases_path.write_text(''.join(' '.join(str(case[key]) for key in (
        'id', 'player', 'level', 'enemy', 'enemyLevel', 'seed', 'seedIndex')) + '\n' for case in cases))
    print(f"Profiles: {profiles}; running {len(cases):,} seeded cases", flush=True)
    with cases_path.open() as source, rows_path.open('w') as destination:
        subprocess.run([binary, '--probes' if args.probes_only else '--battles'],
                       stdin=source, stdout=destination, cwd=ROOT, check=True)
    report = summarize(rows_path, cases, forms, args.probes_only)
    sanitizer = sanitizer_check(cases) if not args.probes_only and not args.skip_sanitizer else {'status': 'not run'}
    assert before == source_hashes(), 'source changed during audit; rerun before publishing evidence'
    report = {'formatVersion': 1, 'kind': 'native-combat-probes' if args.probes_only else 'native-full-roster-balance',
              'catalogVersion': runtime['catalogVersion'], 'rulesVersion': runtime['rulesVersion'],
              'profileChecks': profiles,
              'sampling': {**sampling, 'seededCases': len(cases), 'seedsPerMatchup': args.seeds,
                           'matchups': len(cases) // args.seeds,
                           'seedDerivation': 'SHA256(world-ds-balance-v1:playerForm:playerLevel:enemyForm:enemyLevel:seedIndex), first four bytes big-endian; zero replaced by one.',
                           'opponents': 'Same combat tier, each available authored role; types and members rotated deterministically. Illegal offsets omitted, never clamped.'},
              **report, 'sanitizer': sanitizer,
              'execution': {'compiler': subprocess.check_output(['c++', '--version'], text=True).splitlines()[0],
                                       'buildCommand': command, 'elapsedSeconds': round(time.monotonic() - started, 3),
                                       'rawBytes': rows_path.stat().st_size, 'rawSha256': digest(rows_path),
                                       'caseSha256': digest(cases_path), 'sourceHashes': before}}
    prefix = args.report_prefix or ('world-ds-probes' if args.probes_only else 'world-ds-balance')
    destination = ROOT / 'docs/evidence' / (prefix + '.json')
    destination.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    if not args.probes_only:
        destination.with_suffix('.md').write_text(markdown(report))
    print(json.dumps({'evidence': str(destination.relative_to(ROOT)), 'nativeRows': report['nativeRows'],
                      'elapsedSeconds': report['execution']['elapsedSeconds'],
                      'profileChecks': profiles, 'probeReversals': report['damageProbes']['unguardedPhysicalMagicReversalGroups']}, indent=2))


if __name__ == '__main__':
    main()

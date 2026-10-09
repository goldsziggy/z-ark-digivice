#!/usr/bin/env python3
"""Compare proposed role arithmetic with frozen profiles; no game mutations."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEYS = ('maxHp', 'attack', 'defense', 'magic', 'resistance')
ANCHORS = {
    'Rookie': (1, (104, 18, 14, 18, 13), 1),
    'Champion': (5, (136, 28, 24, 29, 25), 2),
    'Ultimate': (10, (176, 44, 38, 45, 39), 3),
    'Mega': (15, (216, 60, 54, 62, 56), 4),
}
# Allocation uses four-HP units in its first column; growth uses actual HP.
ROLES = {
    'Balanced': ((0, 0, 0, 0, 0), (4, 2, 1, 2, 1)),
    'Striker': ((0, 4, 0, -3, -1), (4, 3, 1, 1, 1)),
    'Mystic': ((0, -3, -1, 4, 0), (4, 1, 1, 3, 1)),
    'Bulwark': ((2, -2, 3, -2, -1), (8, 1, 2, 1, 1)),
    'Warden': ((2, -2, -1, -2, 3), (8, 1, 1, 1, 2)),
}
MATCHING = {
    (4, 1, 1, 1, 1): ('Balanced',),
    (4, 2, 1, 1, 1): ('Striker',),
    (3, 1, 1, 2, 2): ('Mystic',),
    (5, 1, 2, 1, 2): ('Bulwark', 'Warden'),
    (4, 2, 1, 2, 1): (),  # Preserved Hybrid has no exact new-role counterpart.
}
LEVELS = (1, 5, 10, 15, 20)


def weight(stats):
    return stats[0] / 4 + sum(stats[1:])


def proposed(stage, role, level, slower=False):
    anchor, base, factor = ANCHORS[stage]
    allocation, growth = ROLES[role]
    before = max(0, min(level, 10) - anchor)
    after = max(0, level - max(anchor, 10))
    return [base[i] + allocation[i] * factor * (4 if i == 0 else 1) +
            (growth[i] * (level - anchor) if not slower else
             growth[i] * before + growth[i] * after * 6 // 7)
            for i in range(5)]


def stats_at(form, level):
    row = next(row for row in form['levels'] if row['level'] == level)
    return [row[key] for key in KEYS]


def dominates(left, right):
    return all(a >= b for a, b in zip(left, right)) and left != right


def summary(pairs):
    return {
        'pairs': len(pairs),
        'newDominates': sum(pair['newDominates'] for pair in pairs),
        'oldDominates': sum(pair['oldDominates'] for pair in pairs),
        'strictNew': sum(pair['strictNew'] for pair in pairs),
        'examples': [pair for pair in pairs if pair['newDominates'] or pair['oldDominates']],
    }


def audit():
    old = json.loads((ROOT / 'docs/research/preserved-forms.json').read_text())['forms']
    assert len(old) == 66
    report = {
        'formatVersion': 1,
        'method': 'Frozen native profile values versus proposed role arithmetic with zero species variation. Not a battle simulation or measured win probability.',
        'definitions': {
            'dominance': 'At least as high in all five stats and higher in at least one.',
            'strictDominance': 'Higher in all five stats.',
            'matchingRoles': 'Physical maps to Striker, Guardian to both Bulwark and Warden; preserved Hybrid has no exact role counterpart.',
            'scaled6-after10': 'Comparison only: scale each post-level-10 growth component by 6/7, rounding each cumulative component down. Not implemented; integer rounding makes the budget approximate.',
            'originalTierComparison': 'Original forms have no franchise stage. Only for this comparison, entry levels 1/5/10 map to Rookie/Champion/Ultimate.',
        },
        'levels': LEVELS,
        'anchors': ANCHORS,
        'roles': ROLES,
        'comparisons': [],
        'originalTierComparisons': [],
        'sourceHashes': {},
    }
    growth_groups = {}
    for form in old:
        curve = tuple(form['growth'][key] for key in KEYS)
        growth_groups.setdefault(curve, []).append(form['name'])
    report['preservedGrowthGroups'] = [
        {'growth': curve, 'weightedGrowth': weight(curve), 'forms': names}
        for curve, names in growth_groups.items()]
    for stage, (anchor, _, _) in ANCHORS.items():
        peers = [form for form in old if form['stage'] == stage]
        for level in LEVELS:
            if level < anchor:
                continue
            for slower in (False, True):
                pairs, matching = [], []
                for form in peers:
                    current = stats_at(form, level)
                    matches = MATCHING[tuple(form['growth'][key] for key in KEYS)]
                    for role in ROLES:
                        candidate = proposed(stage, role, level, slower)
                        pair = {'old': form['name'], 'role': role, 'new': candidate,
                                'oldStats': current, 'newDominates': dominates(candidate, current),
                                'oldDominates': dominates(current, candidate),
                                'strictNew': all(a > b for a, b in zip(candidate, current))}
                        pairs.append(pair)
                        if role in matches:
                            matching.append(pair)
                budgets = [weight(stats_at(form, level)) for form in peers]
                report['comparisons'].append({
                    'stage': stage, 'level': level,
                    'model': 'scaled6-after10' if slower else 'uniform7',
                    'newBalancedBudget': weight(proposed(stage, 'Balanced', level, slower)),
                    'preservedBudgetRange': [min(budgets), max(budgets)],
                    'allRoles': summary(pairs), 'matchingRoles': summary(matching)})
    for form in old:
        if form['stage'] != 'Original':
            continue
        tier = {1: 'Rookie', 5: 'Champion', 10: 'Ultimate'}[form['minLevel']]
        peers = [peer for peer in old if peer['stage'] == tier]
        for level in LEVELS:
            if level < form['minLevel']:
                continue
            current = stats_at(form, level)
            report['originalTierComparisons'].append({
                'name': form['name'], 'comparisonTier': tier, 'level': level,
                'oldStats': current,
                'newDominatingRoles': [role for role in ROLES
                                      if dominates(proposed(tier, role, level), current)],
                'reduced6DominatingRoles': [role for role in ROLES
                                           if dominates(proposed(tier, role, level, True), current)],
                'alreadyDominatingPreservedPeers': [peer['name'] for peer in peers
                                                    if dominates(stats_at(peer, level), current)]})
    uniform = [row for row in report['comparisons'] if row['model'] == 'uniform7']
    report['summary'] = {
        'sameStagePairs': sum(row['allRoles']['pairs'] for row in uniform),
        'newDominatingPairs': sum(row['allRoles']['newDominates'] for row in uniform),
        'strictNewDominatingPairs': sum(row['allRoles']['strictNew'] for row in uniform),
        'matchingRolePairs': sum(row['matchingRoles']['pairs'] for row in uniform),
        'matchingRoleNewDominatingPairs': sum(row['matchingRoles']['newDominates'] for row in uniform),
        'recommendation': 'Retain uniform 7 as the candidate for native battle simulation. Preserved role growth ranges 5–7.25, so lowering growth from averages alone is not supported. Original-form anchor gaps already exist against preserved franchise forms and are not fixed by post-level-10 growth reduction.',
    }
    for name in ('docs/research/preserved-forms.json',
                 'docs/research/world-ds-balance-design.md',
                 'scripts/audit-world-ds-growth.py'):
        report['sourceHashes'][name] = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/evidence/world-ds-growth-audit.json')
    args = parser.parse_args()
    result = audit()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps(result['summary'], indent=2))

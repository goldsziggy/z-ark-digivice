#!/usr/bin/env python3
"""Generate native constant profiles and matching host metadata. No artwork I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parent.parent
STAT_KEYS = ('maxHp', 'attack', 'defense', 'magic', 'resistance')
TIERS = {
    'Fresh': (1, 0, 1, [64, 8, 6, 8, 6]),
    'In-Training': (1, 0, 1, [80, 12, 9, 12, 9]),
    'Rookie': (1, 0, 1, [104, 18, 14, 18, 13]),
    'Champion': (5, 20, 2, [136, 28, 24, 29, 25]),
    'Ultimate': (10, 50, 3, [176, 44, 38, 45, 39]),
    'Mega': (15, 80, 4, [216, 60, 54, 62, 56]),
}
ROLES = {
    'Balanced': ([0, 0, 0, 0, 0], [4, 2, 1, 2, 1]),
    'Striker': ([0, 4, 0, -3, -1], [4, 3, 1, 1, 1]),
    'Mystic': ([0, -3, -1, 4, 0], [4, 1, 1, 3, 1]),
    'Bulwark': ([2, -2, 3, -2, -1], [8, 1, 2, 1, 1]),
    'Warden': ([2, -2, -1, -2, 3], [8, 1, 1, 1, 2]),
}
STAGES = {'Fresh': 'Fresh', 'In-Training': 'InTraining', 'Rookie': 'Rookie', 'Champion': 'Champion',
          'Ultimate': 'Ultimate', 'Mega': 'Mega', 'Armor': 'Armor', 'No Level': 'NoLevel'}
OLD_SLUGS = ('none', 'mote', 'flicker', 'rill', 'cinder', 'impmon', 'agumon', 'gabumon',
             'patamon', 'tentomon', 'palmon', 'gomamon', 'renamon')

def check(ok, message):
    if not ok:
        raise ValueError(message)

def integer(value, low, high):
    return type(value) is int and low <= value <= high

def label(value, maximum):
    return isinstance(value, str) and bool(value) and value == value.strip() and len(value.encode()) <= maximum \
        and not any(ord(c) < 32 or ord(c) == 127 or c in '"\\' for c in value)

def stats(values):
    return dict(zip(STAT_KEYS, values))

def original_ultimate_adjustments(preserved):
    """The two reviewed rules9 anchors; the historical source stays immutable."""
    old = {form['id']: form for form in preserved['forms']}
    # Existing native Ultimate stage bonus plus the Mystic rank-two bonus.
    bonus = dict(zip(STAT_KEYS, (64, 24, 22, 30, 28)))
    return [dict(formId=form_id, rulesVersion=9,
                 previousBaseStats=dict(old[form_id]['baseStats']),
                 baseStats={key: old[root_id]['baseStats'][key] + bonus[key] for key in STAT_KEYS},
                 reason='Original Rookie anchor plus existing Ultimate/Mystic bonuses; growth, identity and routes unchanged.')
            for form_id, root_id in ((3, 1), (7, 5))]

def weighted(values):
    return values[0] / 4 + sum(values[1:])

def authored_profile(row):
    entry, bond, factor, anchor = TIERS[row['combatTier']]
    delta, growth = ROLES[row['role']]
    variation = row.get('speciesVariation', dict.fromkeys(STAT_KEYS, 0))
    check(isinstance(variation, dict) and set(variation) == set(STAT_KEYS), 'invalid variation fields')
    check(all(type(variation[k]) is int for k in STAT_KEYS), 'variation must be integer')
    check(variation['maxHp'] in (-4, 0, 4) and all(-2 <= variation[k] <= 2 for k in STAT_KEYS[1:]), 'variation exceeds budget')
    check(weighted([variation[k] for k in STAT_KEYS]) == 0, 'variation must have zero weighted sum')
    base = [anchor[i] + delta[i] * factor * (4 if i == 0 else 1) + variation[k] for i, k in enumerate(STAT_KEYS)]
    levels = [dict(level=level, **stats([base[i] + growth[i] * (level - entry) for i in range(5)]))
              for level in range(entry, 21)]
    check(all(1 <= v['maxHp'] <= 512 and all(1 <= v[k] <= 128 for k in STAT_KEYS[1:]) for v in levels), 'stat bounds exceeded')
    return entry, bond, stats(base), stats(growth), levels

def evolution_graph(edges, anchors, required_edges, dispositions=None):
    """Validate actual routes independently of immutable profile/family fields."""
    check(isinstance(edges, list) and len(edges) <= 2 * len(anchors), 'invalid evolution edge list')
    parents = {i: [] for i in anchors}
    children = {i: [] for i in anchors}
    pairs = {}
    for edge in edges:
        check(isinstance(edge, dict), 'invalid evolution edge')
        source, target = edge.get('fromFormId'), edge.get('toFormId')
        level, bond = edge.get('minimumLevel'), edge.get('minimumBond')
        check(integer(source, 1, 512) and integer(target, 1, 512) and source in anchors and target in anchors and source != target,
              'evolution references an absent or identical form')
        check((source, target) not in pairs, 'duplicate evolution edge')
        check(integer(level, 1, 20) and integer(bond, 0, 200), 'invalid evolution gates')
        check(level >= anchors[target][0] and bond >= anchors[target][1], 'evolution gates below target profile requirements')
        canonical = dict(fromFormId=source, toFormId=target, minimumLevel=level, minimumBond=bond)
        pairs[source, target] = canonical
        parents[target].append(source)
        children[source].append(target)
        check(len(children[source]) <= 2, 'native evolution choices are bounded to two')
    check(isinstance(required_edges, list), 'missing frozen evolution gates')
    required_pairs = set()
    for edge in required_edges:
        check(isinstance(edge, dict) and set(edge) == {'fromFormId', 'toFormId', 'minimumLevel', 'minimumBond'}, 'invalid frozen edge')
        pair = (edge['fromFormId'], edge['toFormId'])
        check(pair not in required_pairs, 'duplicate frozen edge')
        required_pairs.add(pair)
        check(pairs.get(pair) == edge, 'pre-existing evolution edge or gates changed')
    # Kahn's bounded traversal allows multiple parents but never a cycle.
    indegree = {i: len(p) for i, p in parents.items()}
    pending = [i for i, count in indegree.items() if count == 0]
    visited = 0
    while pending:
        source = pending.pop()
        visited += 1
        for target in children[source]:
            indegree[target] -= 1
            if indegree[target] == 0:
                pending.append(target)
    check(visited == len(anchors), 'evolution cycle')
    check(dispositions is None or isinstance(dispositions, list), 'invalid evolution dispositions')
    reasons = {}
    for item in dispositions or []:
        check(isinstance(item, dict) and set(item) == {'formId', 'status', 'reason'}, 'invalid leaf disposition')
        form_id = item['formId']
        check(integer(form_id, 1, 512) and form_id in anchors and form_id not in reasons and not children[form_id], 'disposition must identify a unique leaf')
        check(item['status'] in ('terminal', 'independent') and label(item['reason'], 256), 'invalid leaf status or reason')
        check(item['status'] != 'independent' or not parents[form_id], 'independent form cannot have incoming routes')
        reasons[form_id] = item
    result = {}
    for form_id in sorted(anchors):
        incoming, outgoing = sorted(parents[form_id]), sorted(children[form_id])
        status = 'progression' if outgoing else reasons.get(form_id, {}).get('status', 'terminal' if incoming else 'independent')
        reason = None if outgoing else reasons.get(form_id, {}).get('reason', 'End of the reviewed prototype route.' if incoming else 'No reviewed evolution route yet.')
        result[form_id] = dict(parents=incoming, children=outgoing,
            edges=[dict(toFormId=target, requiredLevel=pairs[form_id, target]['minimumLevel'], requiredBond=pairs[form_id, target]['minimumBond']) for target in outgoing],
            status=status, reason=reason)
    return [pairs[pair] for pair in sorted(pairs)], result

def generate(catalog, ledger, preserved, inventory, families):
    check(catalog.get('formatVersion') == ledger.get('formatVersion') == preserved.get('formatVersion') == inventory.get('formatVersion') == 1, 'unsupported input format')
    rows = catalog.get('entries')
    check(isinstance(rows, list) and 255 <= len(rows) <= 512, 'source inventory must stay within the append-only form bound')
    check(integer(catalog.get('catalogRevision'), 2, 65535), 'invalid catalog revision')
    check(families.get('formatVersion') == 1 and isinstance(families.get('identities'), list), 'invalid family identity file')
    frozen = {row['formId']: row for row in families['identities']}
    identity_ids = [row['formId'] for row in families['identities']]
    check(len(frozen) == len(identity_ids) and identity_ids == list(range(67, identity_ids[-1] + 1)), 'family identities must stay a contiguous append-only set')
    check(set(range(67, 277)) <= set(frozen), 'frozen family identity prefix changed')
    old = {f['id']: f for f in preserved['forms']}
    adjustments = original_ultimate_adjustments(preserved)
    check(catalog.get('originalProfileAdjustments') == adjustments,
          'only the two reviewed rules9 original Ultimate anchors are supported')
    adjusted = {row['formId']: row for row in adjustments}
    check(set(old) == set(range(1, 67)), 'preserved66 ID set changed')
    reserved = {e['entryKey']: e for e in ledger['entries']}
    named = {e['entryKey']: e for e in inventory['entries']}
    check(len(reserved) == len(named) == len(rows) and set(reserved) == set(named), 'inventory/ledger keys differ')
    by_key, by_id = {}, {}
    new_labels = set()
    old_labels = {label.casefold() for form in old.values() for label in form['skills'].values()}
    required = {'entryKey', 'formId', 'displayName', 'sourceStage', 'combatTier', 'role', 'type', 'skills', 'lineageId', 'parent', 'children', 'preservedFormId'}
    for row in rows:
        check(isinstance(row, dict) and required <= row.keys(), 'missing required authoring fields')
        check(not ({'baseStats', 'growth', 'statsByLevel', 'minLevel', 'minBond', 'statModel'} & row.keys()), 'derived mechanics cannot be overridden in source metadata')
        key, form_id = row['entryKey'], row['formId']
        check(isinstance(key, str) and re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', key) and len(key) <= 63, 'invalid entry key')
        check(key in reserved and key not in by_key and integer(form_id, 1, 512) and form_id not in by_id, 'duplicate/unknown identity')
        binding = reserved[key]
        check(all(row[k] == binding[k] for k in ('formId', 'displayName', 'sourceStage', 'preservedFormId')), 'stable ID ledger mismatch: ' + key)
        check(row['displayName'] == named[key]['listedName'] and row['sourceStage'] == named[key]['sourceStage'], 'inventory identity mismatch: ' + key)
        check(label(row['displayName'], 64), 'invalid display name: ' + key)
        check(row['sourceStage'] in STAGES and row['combatTier'] in TIERS and row['role'] in ROLES and row['type'] in ('grove', 'tide', 'ember', 'neutral'), 'invalid stage/tier/role/type: ' + key)
        check(integer(row['lineageId'], 1, 65535) and integer(row['parent'], 0, 512), 'invalid lineage/parent')
        check(isinstance(row['children'], list) and len(row['children']) <= 2 and len(set(row['children'])) == len(row['children']) and all(integer(x, 1, 512) for x in row['children']), 'invalid children')
        check(isinstance(row['skills'], dict) and set(row['skills']) == {'physical', 'heavy', 'magic'} and all(label(x, 32) for x in row['skills'].values()), 'invalid skills: ' + key)
        labels = [row['skills'][k].casefold() for k in ('physical', 'heavy', 'magic')]
        check(len(set(labels)) == 3, 'duplicate skill within species: ' + key)
        if row['preservedFormId'] is not None:
            check(form_id in old and row['preservedFormId'] == form_id, 'preserved binding cannot replace an identity')
            original = old[form_id]
            check(row['lineageId'] == original['lineage'] and row['parent'] == original['parent'] and row['children'] == [x for x in original['children'] if x], 'preserved lineage/edges changed')
            check(row['type'] == original['type'] and row['skills'] == original['skills'], 'preserved type/skills changed')
            variation = row.get('speciesVariation', dict.fromkeys(STAT_KEYS, 0))
            check(isinstance(variation, dict) and set(variation) == set(STAT_KEYS) and all(type(v) is int and v == 0 for v in variation.values()), 'preserved profiles cannot have numeric variations')
        else:
            check(form_id > 66, 'new profile overwrites preserved form')
            check(form_id in frozen and all(row[k] == frozen[form_id][k] for k in ('lineageId', 'parent', 'children')), 'frozen family identity changed')
            check(not old_labels.intersection(labels), 'new skill duplicates preserved label: ' + key)
            check(not new_labels.intersection(labels), 'duplicate new skill label: ' + key)
            new_labels.update(labels)
            authored_profile(row)
        by_key[key] = row; by_id[form_id] = row
    check(set(by_key) == set(reserved), 'inventory coverage changed')
    appended = sorted(i for i in by_id if i > 66)
    check(appended == list(range(67, max(appended) + 1)), 'new IDs must be contiguous stable append-only assignments')
    check(max(appended) <= 512, 'native form bound exceeded')
    for form_id in appended:
        row = by_id[form_id]
        if row['parent']:
            check(row['parent'] in appended and form_id in by_id[row['parent']]['children'], 'parent edge is not reciprocal')
        for child in row['children']:
            check(child in appended and by_id[child]['parent'] == form_id, 'child edge is not reciprocal')
            check(TIERS[row['combatTier']][0] < TIERS[by_id[child]['combatTier']][0], 'evolution entry level must strictly increase')
            check(row['lineageId'] == by_id[child]['lineageId'], 'evolution crosses lineages')
        visited = set(); root = row
        while root['parent']:
            check(root['formId'] not in visited, 'evolution cycle')
            visited.add(root['formId']); root = by_id[root['parent']]
        check(row['lineageId'] == root['formId'] + 1000, 'new lineage does not match its stable root')
    profiles = []
    for form_id in range(1, max(appended) + 1):
        source = by_id.get(form_id)
        if form_id <= 66:
            f = old[form_id]
            tier = f['stage'] if f['stage'] != 'Original' else 'Ultimate' if f['minLevel'] >= 10 else 'Champion' if f['minLevel'] >= 5 else 'Rookie'
            record = dict(formId=form_id, name=f['name'], lineageId=f['lineage'], lineageSlug=OLD_SLUGS[f['lineage']], stage=f['stage'], combatTier=tier,
                type=f['type'], role='Preserved', parent=f['parent'], children=[x for x in f['children'] if x], minLevel=f['minLevel'], minBond=f['minBond'],
                skills=dict(f['skills']), baseStats=f['baseStats'], growth=f['growth'], preserved=True, statModel='preserved-v4', statsByLevel=f['levels'], artId=f['artId'])
            if form_id in adjusted:
                record['baseStats'] = dict(adjusted[form_id]['baseStats'])
                record['statsByLevel'] = [dict(level=level, **{
                    key: record['baseStats'][key] + (level - f['minLevel']) * f['growth'][key]
                    for key in STAT_KEYS}) for level in range(f['minLevel'], 21)]
                record['statModel'] = 'original-ultimate-v9'
        else:
            f = by_id[form_id]; entry, bond, base, growth, levels = authored_profile(f)
            root_id = f['lineageId'] - 1000
            record = dict(formId=form_id, name=f['displayName'], lineageId=f['lineageId'], lineageSlug=by_id[root_id]['entryKey'], stage=f['sourceStage'], combatTier=f['combatTier'],
                type=f['type'], role=f['role'], parent=f['parent'], children=f['children'], minLevel=entry, minBond=bond, skills=dict(f['skills']), baseStats=base, growth=growth,
                preserved=False, statModel='linear-v1', statsByLevel=levels, artId=None)
        record['entryKey'] = source['entryKey'] if source else None
        record['obtainable'] = True
        profiles.append(record)
    anchors = {f['formId']: (f['minLevel'], f['minBond']) for f in profiles}
    edges, graph = evolution_graph(catalog.get('evolutionEdges'), anchors, families.get('requiredEdges'), catalog.get('evolutionDispositions'))
    for row in rows:
        check(row.get('evolution') == graph[row['formId']], 'assembled evolution metadata differs from validated graph')
    for record in profiles:
        record['evolution'] = graph[record['formId']]
        record['leafReason'] = record['evolution']['reason']
    def q(value): return json.dumps(value, ensure_ascii=False)
    def values(value): return '{' + ','.join(str(value[k]) for k in STAT_KEYS) + '}'
    lines = ['// Generated by scripts/generate-world-ds-profiles.py; do not edit.']
    for f in profiles[66:]:
        children = f['children'] + [0] * (2 - len(f['children']))
        lines.append('    {' + ','.join((str(f['formId']), str(f['lineageId']), 'Stage::' + STAGES[f['stage']], str(f['parent']), '{' + ','.join(map(str, children)) + '}',
            str(f['minLevel']), str(f['minBond']), q(f['name']), q(f['type']), q(f['skills']['physical']), q(f['skills']['heavy']), q(f['skills']['magic']), 'nullptr',
            values(f['baseStats']), values(f['growth']), 'CombatTier::' + STAGES[f['combatTier']])) + '},')
    meta = ['// Generated inventory mapping; official/provenance metadata remains host-only.',
            f'static_assert(kFormCount == {len(profiles)} && kCatalogVersion == {catalog["catalogRevision"]}, "Regenerate/review catalog limits");',
            'constexpr CatalogEntry kCatalogEntries[] = {']
    for f in sorted(rows, key=lambda r: r['formId']):
        meta.append('    {' + f'{f["formId"]},{q(f["entryKey"])},{q(f["displayName"])},{q(f["role"])}' + '},')
    meta.append('};')
    graph_lines = ['// Generated evolution routes; historical family/profile fields remain unchanged.', 'constexpr EvolutionEdge kEvolutionEdges[] = {']
    for edge in edges:
        graph_lines.append('    {' + ','.join(str(edge[k]) for k in ('fromFormId', 'toFormId', 'minimumLevel', 'minimumBond')) + '},')
    graph_lines.extend(['};', 'constexpr const char* kEvolutionLeafReasons[kFormCount] = {'])
    graph_lines.extend('    ' + ('nullptr' if f['leafReason'] is None else q(f['leafReason'])) + ',' for f in profiles)
    graph_lines.append('};')
    graph_lines.append('constexpr bool kEvolutionTerminalLeaves[kFormCount] = {')
    graph_lines.extend('    ' + ('true' if f['evolution']['status'] == 'terminal' else 'false') + ',' for f in profiles)
    graph_lines.append('};')
    # Apply the reviewed names only after emitting immutable base Form tables.
    # Both native combat and host profiles use this explicit overlay. Historical
    # replay tables keep the original names, numeric curves and identifiers.
    bindings = catalog.get('battleSkillBindings', [])
    check(isinstance(bindings, list) and len(bindings) <= len(profiles) * 3, 'invalid battle skill bindings')
    overrides = {}
    for binding in bindings:
        check(isinstance(binding, dict), 'invalid battle skill binding')
        form_id, category, name = binding.get('formId'), binding.get('category'), binding.get('name')
        check(integer(form_id, 1, len(profiles)) and category in ('physical', 'heavy', 'magic') and label(name, 32),
              'invalid battle skill identity, category or label')
        form = profiles[form_id - 1]
        check(binding.get('formName') == form['name'], 'battle skill form name mismatch')
        check(binding.get('previousName') == form['skills'][category], 'battle skill baseline mismatch')
        source = binding.get('source', {})
        check(binding.get('officialCategoryClaim') is False and binding.get('identityMatch') == 'exact-native-form'
              and source.get('moveName') == name and source.get('verification') == 'official-name-and-described-action-read'
              and isinstance(source.get('url'), str) and source['url'].startswith('https://digimon.net/reference_en/detail.php?directory_name=')
              and isinstance(source.get('semanticSummary'), str) and bool(source['semanticSummary']),
              'battle skill requires reviewed official action evidence')
        slots = overrides.setdefault(form_id, {})
        check(category not in slots, 'duplicate battle skill binding')
        slots[category] = name
    skill_lines = ['// Generated reviewed combat-name overrides; frozen Form tables are unchanged.',
                   'struct BattleSkillBinding { uint16_t formId; const char* physical; const char* heavy; const char* magic; };',
                   'constexpr BattleSkillBinding kBattleSkillBindings[] = {']
    for form_id, slots in sorted(overrides.items()):
        form = profiles[form_id - 1]
        form['skills'].update(slots)
        check(len({v.casefold() for v in form['skills'].values()}) == 3, 'duplicate effective skill within form')
        skill_lines.append('    {' + str(form_id) + ',' + ','.join(q(slots[k]) if k in slots else 'nullptr' for k in ('physical', 'heavy', 'magic')) + '},')
    if not overrides:
        skill_lines.append('    {0,nullptr,nullptr,nullptr},')
    skill_lines.append('};')
    rarity = catalog.get('encounterRarity')
    check(isinstance(rarity, dict) and rarity.get('formatVersion') == 1 and rarity.get('rulesVersion') == 10
          and rarity.get('catalogVersion') == catalog['catalogRevision'] and rarity.get('canonicalRarityClaim') is False,
          'invalid authored encounter rarity epoch')
    check(rarity.get('bucketWeights') == {'common': 70, 'uncommon': 25, 'rare': 5}, 'unreviewed rarity bucket weights')
    rows_rarity = rarity.get('forms')
    check(isinstance(rows_rarity, list) and len(rows_rarity) == len(profiles), 'rarity must cover every native form')
    rarity_lines = ['// Generated authored encounter frequencies; not canonical rarity or combat power.',
                    f'static_assert(forms::kFormCount == {len(profiles)}, "Regenerate rarity table");',
                    'constexpr Rarity kRarities[forms::kFormCount] = {']
    for form, row in zip(profiles, rows_rarity):
        check(isinstance(row, dict) and set(row) == {'formId', 'name', 'combatTier', 'rarity'}
              and row['formId'] == form['formId'] and row['name'] == form['name'] and row['combatTier'] == form['combatTier']
              and row['rarity'] in ('common', 'uncommon', 'rare'), 'rarity identity/order/category mismatch')
        form['encounterRarity'] = row['rarity']
        rarity_lines.append('    Rarity::' + row['rarity'].title() + ', // ' + str(form['formId']) + ' ' + form['name'])
    rarity_lines.append('};')
    runtime = dict(formatVersion=1, catalogVersion=catalog['catalogRevision'], rulesVersion=10,
                   profileDesign='world-ds-authored-v1', sourceEntryCount=len(rows), total=len(profiles),
                   forms=profiles, entries=[dict(entryKey=r['entryKey'], formId=r['formId']) for r in rows])
    return {'core/encounter_rarity_generated.inc': '\n'.join(rarity_lines) + '\n',
            'core/world_ds_forms_generated.inc': '\n'.join(lines) + '\n',
            'core/world_ds_catalog_generated.inc': '\n'.join(meta) + '\n',
            'core/world_ds_evolutions_generated.inc': '\n'.join(graph_lines) + '\n',
            'core/battle_skill_bindings_generated.inc': '\n'.join(skill_lines) + '\n',
            'data/world-ds-runtime.json': json.dumps(runtime, ensure_ascii=False, indent=2) + '\n'}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    inputs = ['data/world-ds-catalog.json', 'data/world-ds-ids.json', 'docs/research/preserved-forms.json', 'docs/research/world-ds-inventory.json', 'data/world-ds-family-identities.json']
    outputs = generate(*(json.loads((ROOT / name).read_text()) for name in inputs))
    for name, value in outputs.items():
        target = ROOT / name
        if args.check:
            check(target.exists() and target.read_text() == value, 'stale generated file: ' + name)
        else:
            target.parent.mkdir(parents=True, exist_ok=True); target.write_text(value)
    print(json.dumps({'result': 'PASS', 'mode': 'check' if args.check else 'generate', 'files': {name: {'bytes': len(value.encode()), 'sha256': hashlib.sha256(value.encode()).hexdigest()} for name, value in outputs.items()}}, indent=2))

if __name__ == '__main__':
    main()

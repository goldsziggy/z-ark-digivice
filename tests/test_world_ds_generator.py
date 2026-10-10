import copy
import csv
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location('world_ds_generator', ROOT / 'scripts/generate-world-ds-profiles.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

def script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / ('scripts/' + name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

ASSEMBLER = script('assemble-world-ds-catalog')
EXPORTER = script('export-world-ds-roster')

class GeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inputs = [json.loads((ROOT / path).read_text()) for path in (
            'data/world-ds-catalog.json', 'data/world-ds-ids.json',
            'docs/research/preserved-forms.json', 'docs/research/world-ds-inventory.json',
            'data/world-ds-family-identities.json')]

    def inputs_copy(self):
        return copy.deepcopy(self.inputs)

    def test_generated_files_are_deterministic_and_current(self):
        a = MODULE.generate(*self.inputs)
        self.assertEqual(a, MODULE.generate(*self.inputs))
        for path, value in a.items():
            self.assertEqual((ROOT / path).read_text(), value, path)
        runtime = json.loads(a['data/world-ds-runtime.json'])
        self.assertEqual(len(runtime['forms']), 465)
        self.assertEqual(len(runtime['entries']), 444)
        self.assertEqual(runtime['catalogVersion'], 6)
        self.assertEqual(runtime['rulesVersion'], 10)

    def test_player_facing_names_use_english(self):
        english = json.loads((ROOT / 'data/english-form-names.json').read_text())['names']
        runtime = json.loads(MODULE.generate(*self.inputs)['data/world-ds-runtime.json'])
        by_key = {form['entryKey']: form for form in runtime['forms'] if form.get('entryKey')}
        display = {row['entryKey']: row['displayName'] for row in self.inputs[0]['entries']}
        retired = {row['formId'] for row in self.inputs[0]['entries'] if row.get('retiredAliasOf') is not None}
        shown = {form['name'] for form in runtime['forms'] if form['formId'] not in retired}
        for key, name in english.items():
            self.assertEqual(by_key[key]['name'], name)
            if by_key[key]['formId'] not in retired and display[key] != name:
                self.assertNotIn(display[key], shown)

    def effective_skills(self, form_id, base):
        return {**base, **{b['category']: b['name'] for b in self.inputs[0]['battleSkillBindings'] if b['formId'] == form_id}}

    def restore_english_name_overlay(self, profiles):
        # The rules 6/7 fingerprint covers sheet-era labels. Put those back so the
        # check still proves every stat, skill and route is unchanged.
        english = json.loads((ROOT / 'data/english-form-names.json').read_text())['names']
        display = {row['entryKey']: row for row in self.inputs[0]['entries']}
        for form in profiles:
            key = form.get('entryKey')
            if key in english and form['formId'] > 66:
                self.assertEqual(form['name'], english[key])
                form['name'] = display[key]['displayName']
                form['skills'] = copy.deepcopy(display[key]['skills'])

    def restore_original_anchors(self, profiles):
        # Normalize only the two authorized changes before comparing the complete
        # old profile fingerprint; every unrelated numeric curve stays exact.
        for form_id in (3, 7):
            old, current = self.inputs[2]['forms'][form_id - 1], profiles[form_id - 1]
            current['baseStats'] = copy.deepcopy(old['baseStats'])
            current['statsByLevel'] = copy.deepcopy(old['levels'])
            current['statModel'] = 'preserved-v4'

    def test_rules6_profiles_and_all_147_routes_remain_unchanged(self):
        baseline = json.loads((ROOT / 'tests/fixtures/rules6-evolution-baseline.json').read_text())
        runtime = json.loads(MODULE.generate(*self.inputs)['data/world-ds-runtime.json'])
        profiles = [{k: v for k, v in f.items() if k not in [*baseline['profileHashExcludes'], 'encounterRarity']}
                    for f in runtime['forms']]
        # The only profile changes are explicit reviewed names. Restore each
        # exact prior label before checking every other field byte-for-byte.
        for binding in self.inputs[0]['battleSkillBindings']:
            form = profiles[binding['formId'] - 1]
            self.assertEqual(form['skills'][binding['category']], binding['name'])
            form['skills'][binding['category']] = binding['previousName']
        self.restore_english_name_overlay(profiles)
        self.restore_original_anchors(profiles)
        profiles = profiles[:276]
        self.assertEqual(len(profiles), 276)
        fingerprint = hashlib.sha256(json.dumps(profiles, sort_keys=True, separators=(',', ':'),
                                               ensure_ascii=False).encode()).hexdigest()
        self.assertEqual(fingerprint, baseline['all276ProfileSha256'])
        routes = {(f['formId'], e['toFormId']): (e['requiredLevel'], e['requiredBond'])
                  for f in runtime['forms'] for e in f['evolution']['edges']}
        self.assertEqual(len(baseline['edges']), 147)
        for edge in baseline['edges']:
            self.assertEqual(routes[(edge['fromFormId'], edge['toFormId'])],
                             (edge['minimumLevel'], edge['minimumBond']))

    def test_only_two_reviewed_original_anchors_change(self):
        runtime = json.loads(MODULE.generate(*self.inputs)['data/world-ds-runtime.json'])
        targets = {3: [164, 42, 36, 46, 44], 7: [168, 38, 38, 50, 46]}
        for old, new in zip(self.inputs[2]['forms'], runtime['forms'][:66]):
            self.assertEqual(old['id'], new['formId'])
            if old['id'] in targets:
                expected = dict(zip(MODULE.STAT_KEYS, targets[old['id']]))
                self.assertEqual(expected, new['baseStats'])
                for level in new['statsByLevel']:
                    self.assertEqual({key: expected[key] + (level['level'] - 10) * old['growth'][key]
                                      for key in MODULE.STAT_KEYS}, {key: level[key] for key in MODULE.STAT_KEYS})
            else:
                self.assertEqual(old['levels'], new['statsByLevel'])
                self.assertEqual(old['baseStats'], new['baseStats'])
            self.assertEqual(self.effective_skills(old['id'], old['skills']), new['skills'])
            self.assertEqual(old['growth'], new['growth'])
            self.assertEqual(old['parent'], new['parent'])
            self.assertEqual([x for x in old['children'] if x], new['children'])

    def test_unreviewed_numeric_adjustment_is_rejected(self):
        for change in ('missing', 'extra-form', 'extra-stat'):
            values = self.inputs_copy()
            adjustments = values[0]['originalProfileAdjustments']
            if change == 'missing': adjustments.pop()
            elif change == 'extra-form': adjustments.append({**adjustments[0], 'formId': 10})
            else: adjustments[0]['baseStats']['attack'] += 1
            with self.subTest(change=change), self.assertRaises(ValueError):
                MODULE.generate(*values)

    def test_rarity_identity_bounds_and_no_canonical_claim(self):
        for change in ('missing', 'duplicate', 'unknown', 'name', 'stage', 'weights', 'claim'):
            values = self.inputs_copy(); rarity=values[0]['encounterRarity']
            if change=='missing': rarity['forms'].pop()
            elif change=='duplicate': rarity['forms'][1]=copy.deepcopy(rarity['forms'][0])
            elif change=='unknown': rarity['forms'][0]['rarity']='legendary'
            elif change=='name': rarity['forms'][0]['name']='Unknown'
            elif change=='stage': rarity['forms'][0]['combatTier']='Mega'
            elif change=='weights': rarity['bucketWeights']['rare']=6
            else: rarity['canonicalRarityClaim']=True
            with self.subTest(change=change), self.assertRaises(ValueError): MODULE.generate(*values)

    def test_immutable_identity_or_profile_override_rejected(self):
        for mutation in ('formId', 'displayName', 'sourceStage', 'baseStats'):
            values = self.inputs_copy()
            row = values[0]['entries'][0]
            row[mutation] = 999 if mutation == 'formId' else 'unreviewed' if mutation != 'baseStats' else {'attack': 999}
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                MODULE.generate(*values)

    def test_rules7_routes_and_numeric_profiles_remain_unchanged(self):
        baseline = json.loads((ROOT / 'tests/fixtures/rules7-evolution-baseline.json').read_text())
        runtime = json.loads(MODULE.generate(*self.inputs)['data/world-ds-runtime.json'])
        profiles = [{k: v for k, v in f.items() if k not in [*baseline['profileHashExcludes'], 'encounterRarity']} for f in runtime['forms']]
        for binding in self.inputs[0]['battleSkillBindings']:
            profiles[binding['formId'] - 1]['skills'][binding['category']] = binding['previousName']
        self.restore_english_name_overlay(profiles)
        self.restore_original_anchors(profiles)
        original_profiles = profiles[:276]
        self.assertEqual(len(original_profiles), 276)
        self.assertEqual(hashlib.sha256(json.dumps(original_profiles, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()).hexdigest(), baseline['all276ProfileSha256'])
        routes = {(f['formId'], e['toFormId']): (e['requiredLevel'], e['requiredBond']) for f in runtime['forms'] for e in f['evolution']['edges']}
        old_routes = {pair: gates for pair, gates in routes.items() if pair[0] <= 276 and pair[1] <= 276}
        self.assertEqual(len(baseline['edges']), 163)
        self.assertEqual(len(old_routes), 172)
        self.assertEqual(len(routes), 254)  # 9 unreviewed or retired-duplicate routes held
        for edge in baseline['edges']:
            self.assertEqual(routes[(edge['fromFormId'], edge['toFormId'])], (edge['minimumLevel'], edge['minimumBond']))

    def test_reviewed_skill_binding_bounds_and_evidence_are_required(self):
        for field, value in [('formId', 500), ('formName', 'Wrong identity'), ('category', 'summon'),
                             ('name', 'x' * 33), ('previousName', 'unverified baseline'),
                             ('officialCategoryClaim', True), ('source', {})]:
            inputs = self.inputs_copy()
            inputs[0]['battleSkillBindings'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                MODULE.generate(*inputs)
        inputs = self.inputs_copy()
        inputs[0]['battleSkillBindings'].append(copy.deepcopy(inputs[0]['battleSkillBindings'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate battle skill'):
            MODULE.generate(*inputs)

    def test_official_names_may_repeat_between_forms_but_not_slots(self):
        inputs = self.inputs_copy()
        source = inputs[0]['battleSkillBindings'][0]
        target = inputs[0]['battleSkillBindings'][1]
        target['name'] = source['name']
        target['source']['moveName'] = source['name']
        MODULE.generate(*inputs)

    def test_overlay_does_not_mutate_base_form_tables_or_generator_inputs(self):
        before = self.inputs_copy()
        effective = MODULE.generate(*self.inputs)
        inputs = self.inputs_copy()
        inputs[0]['battleSkillBindings'] = []
        authored = MODULE.generate(*inputs)
        self.assertEqual(self.inputs, before)
        self.assertEqual(effective['core/world_ds_forms_generated.inc'], authored['core/world_ds_forms_generated.inc'])
        self.assertNotEqual(effective['core/battle_skill_bindings_generated.inc'], authored['core/battle_skill_bindings_generated.inc'])

    def test_missing_or_duplicate_inventory_entry_rejected(self):
        for duplicate in (False, True):
            values = self.inputs_copy()
            if duplicate:
                values[0]['entries'][-1] = copy.deepcopy(values[0]['entries'][0])
            else:
                values[0]['entries'].pop()
            with self.subTest(duplicate=duplicate), self.assertRaises(ValueError):
                MODULE.generate(*values)

    def test_invalid_role_tier_skill_and_variation_rejected(self):
        mutations = [('role', 'God'), ('combatTier', 'No Level'), ('skills', {'physical': 'x'}),
                     ('skills', {'physical': 'Claw Jab', 'heavy': 'new h', 'magic': 'new m'}),
                     ('skills', {'physical': 'x' * 33, 'heavy': 'h', 'magic': 'm'}),
                     ('skills', {'physical': 'quote"', 'heavy': 'h', 'magic': 'm'}),
                     ('speciesVariation', dict(maxHp=4, attack=0, defense=0, magic=0, resistance=0))]
        for key, value in mutations:
            values = self.inputs_copy(); values[0]['entries'][0][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                MODULE.generate(*values)

    def test_new_duplicate_skills_rejected(self):
        values = self.inputs_copy()
        values[0]['entries'][1]['skills'] = values[0]['entries'][0]['skills']
        with self.assertRaises(ValueError): MODULE.generate(*values)

    def test_preserved_edges_and_skills_cannot_be_changed(self):
        for field, value in [('parent', 67), ('children', []), ('type', 'neutral'),
                             ('speciesVariation', dict(maxHp=4, attack=-1, defense=0, magic=0, resistance=0))]:
            values = self.inputs_copy()
            row = next(r for r in values[0]['entries'] if r['formId'] == 18)
            row[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): MODULE.generate(*values)

    def test_broken_graph_and_lineage_rejected(self):
        for field, value in [('lineageId', 60000), ('parent', 400), ('children', [400]), ('children', [68, 69, 70])]:
            values = self.inputs_copy(); values[0]['entries'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): MODULE.generate(*values)

    def test_host_provenance_does_not_override_native_output(self):
        values = self.inputs_copy()
        values[0]['entries'][0]['official'] = {'note': 'Host-only provenance; no native stats.'}
        self.assertEqual(MODULE.generate(*self.inputs), MODULE.generate(*values))

    def test_all_new_family_identities_remain_frozen(self):
        runtime = json.loads(MODULE.generate(*self.inputs)['data/world-ds-runtime.json'])
        for frozen, form in zip(self.inputs[4]['identities'], runtime['forms'][66:]):
            self.assertEqual(frozen, {key: form[key] for key in frozen})
        self.assertEqual(len(self.inputs[4]['requiredEdges']), 99)

    def test_missing_or_modified_original_route_rejected(self):
        for modification in ('remove', 'level', 'bond'):
            values = self.inputs_copy()
            edges = values[0]['evolutionEdges']
            original = next(e for e in edges if e['fromFormId'] == 1 and e['toFormId'] == 2)
            if modification == 'remove':
                edges.remove(original)
            elif modification == 'level':
                original['minimumLevel'] += 1
            else:
                original['minimumBond'] += 1
            with self.subTest(modification=modification), self.assertRaisesRegex(ValueError, 'pre-existing'):
                MODULE.generate(*values)

    def test_actual_graph_can_cross_frozen_families(self):
        # Separate roots can join one target; neither profile/family is re-rooted.
        anchors = {1: (1, 0), 2: (1, 0), 3: (1, 0), 4: (5, 20)}
        edges = [dict(fromFormId=1, toFormId=3, minimumLevel=2, minimumBond=5),
                 dict(fromFormId=2, toFormId=3, minimumLevel=2, minimumBond=5),
                 dict(fromFormId=3, toFormId=4, minimumLevel=5, minimumBond=20)]
        canonical, graph = MODULE.evolution_graph(edges[::-1], anchors, [])
        self.assertEqual(canonical, edges)
        self.assertEqual(graph[3]['parents'], [1, 2])
        self.assertEqual(graph[1]['edges'], [dict(toFormId=3, requiredLevel=2, requiredBond=5)])
        self.assertEqual(graph[4]['status'], 'terminal')
        self.assertEqual(graph[3]['reason'], None)

    def test_cycle_duplicate_unknown_and_excess_children_rejected(self):
        def edge(a, b): return dict(fromFormId=a, toFormId=b, minimumLevel=1, minimumBond=0)
        cases = [[edge(1, 2), edge(2, 1)], [edge(1, 2), edge(1, 2)],
                 [edge(1, 5)], [edge(1, 1)], [edge(1, 2), edge(1, 3), edge(1, 4)]]
        for edges in cases:
            with self.subTest(edges=edges), self.assertRaises(ValueError):
                MODULE.evolution_graph(edges, {i: (1, 0) for i in range(1, 5)}, [])

    def test_gate_bounds_and_target_state_requirements(self):
        for level, bond in [(0, 20), (21, 20), (4, 20), (5, 19), (5, 201), (True, 20)]:
            with self.subTest(level=level, bond=bond), self.assertRaises(ValueError):
                MODULE.evolution_graph([dict(fromFormId=1, toFormId=2, minimumLevel=level, minimumBond=bond)], {1: (1, 0), 2: (5, 20)}, [])

    def test_leaf_dispositions_are_bounded_and_explicit(self):
        edges = [dict(fromFormId=1, toFormId=2, minimumLevel=1, minimumBond=0)]
        anchors = {1: (1, 0), 2: (1, 0), 3: (1, 0)}
        _, graph = MODULE.evolution_graph(edges, anchors, [], [dict(formId=3, status='terminal', reason='Reviewed standalone final form.')])
        self.assertEqual(graph[3]['status'], 'terminal')
        self.assertEqual(graph[3]['reason'], 'Reviewed standalone final form.')
        for item in [dict(formId=1, status='terminal', reason='Has children.'),
                     dict(formId=2, status='independent', reason='Has an incoming route.'),
                     dict(formId=3, status='progression', reason='Invalid leaf status.'),
                     dict(formId=3, status='terminal', reason='x' * 257),
                     dict(formId=3, status='terminal', reason='Unsafe " quote')]:
            with self.subTest(item=item), self.assertRaises(ValueError):
                MODULE.evolution_graph(edges, anchors, [], [item])

    def test_catalog_cannot_lie_about_runtime_routes(self):
        values = self.inputs_copy()
        values[0]['entries'][0]['evolution']['parents'] = [512]
        with self.assertRaisesRegex(ValueError, 'metadata differs'):
            MODULE.generate(*values)

    def assembled_cross_family_fixture(self):
        routes = dict(edges=[e for e in self.inputs[4]['requiredEdges'] if e['fromFormId'] > 66])
        routes['edges'] += [dict(fromFormId=source, toFormId=1, minimumLevel=2, minimumBond=5) for source in (67, 68)]
        read = ASSEMBLER.read
        with mock.patch.object(ASSEMBLER, 'read', side_effect=lambda path: copy.deepcopy(routes) if path == 'data/world-ds-evolutions.json' else read(path)):
            assembled = ASSEMBLER.assemble()
        inputs = self.inputs_copy()
        inputs[0] = assembled
        runtime = json.loads(MODULE.generate(*inputs)['data/world-ds-runtime.json'])
        return assembled, runtime

    def test_assembler_preserves_family_when_graph_adds_old_form_and_multi_parent(self):
        assembled, runtime = self.assembled_cross_family_fixture()
        self.assertEqual(runtime['forms'][0]['evolution']['parents'], [67, 68])
        for form_id in (67, 68):
            form = runtime['forms'][form_id - 1]
            self.assertEqual(form['parent'], 0)
            self.assertEqual(form['children'], [])
            self.assertEqual(form['lineageId'], 1000 + form_id)
            self.assertEqual(form['evolution']['children'], [1])
            self.assertEqual(form['evolution']['edges'][0]['requiredLevel'], 2)
        self.assertEqual(len(assembled['evolutionEdges']), 101)

    def test_export_uses_actual_edges_and_gates_and_keeps_stats(self):
        assembled, runtime = self.assembled_cross_family_fixture()
        rows = list(csv.DictReader(io.StringIO(EXPORTER.export(assembled, runtime))))
        self.assertEqual(len(rows), len(assembled['entries']))
        row = next(row for row in rows if row['form_id'] == '67')
        self.assertEqual(row['children_ids'], '1')
        self.assertEqual(row['historical_family_children_ids'], '')
        self.assertEqual(row['evolution_status'], 'progression')
        self.assertEqual(json.loads(row['evolution_edges']), [dict(toFormId=1, requiredLevel=2, requiredBond=5)])
        self.assertEqual(int(row['maxHp']), runtime['forms'][66]['baseStats']['maxHp'])
        self.assertEqual(row['physical_skill'], runtime['forms'][66]['skills']['physical'])

if __name__ == '__main__':
    unittest.main()

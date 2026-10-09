"""Metadata-only contract checks; no network or upstream repository execution."""
import json
from pathlib import Path
import re
import unittest
from urllib.parse import urlparse, parse_qs

ROOT = Path(__file__).resolve().parents[1]
TABLE = json.loads((ROOT / 'docs/research/battle-skill-bindings.json').read_text())
FORMS = {row['formId']: row for row in json.loads((ROOT / 'data/world-ds-runtime.json').read_text())['forms']}


class BattleSkillBindings(unittest.TestCase):
    def test_stable_form_identity_and_bounded_category_bindings(self):
        seen = set()
        for row in TABLE['bindings']:
            self.assertIn(row['formId'], FORMS)
            self.assertEqual(row['formName'], FORMS[row['formId']]['name'])
            self.assertIn(row['category'], ('physical', 'heavy', 'magic'))
            key = (row['formId'], row['category'])
            self.assertNotIn(key, seen)
            seen.add(key)
            self.assertGreater(len(row['name'].encode()), 0)
            self.assertLessEqual(len(row['name'].encode()), 32)
            self.assertIsNone(re.search(r'[\x00-\x1f\x7f<>\\"]', row['name']))
        self.assertEqual(len(seen), 30)
        self.assertTrue(all(form_id > 10 for form_id, _ in seen), 'original characters retain authored moves')

    def test_every_binding_has_primary_semantic_evidence(self):
        for row in TABLE['bindings']:
            source = row['source']
            url = urlparse(source['url'])
            self.assertEqual((url.scheme, url.netloc, url.path), ('https', 'digimon.net', '/reference_en/detail.php'))
            self.assertTrue(parse_qs(url.query).get('directory_name'))
            self.assertEqual(source['moveName'], row['name'])
            self.assertEqual(source['verification'], 'official-name-and-described-action-read')
            self.assertTrue(source['semanticSummary'].strip())
            self.assertFalse(row['officialCategoryClaim'])
            self.assertIn('Damage only', row['implementedEffectContract'])

    def test_contact_energy_and_committed_attacks_are_not_name_guesses(self):
        by_key = {(row['formId'], row['category']): row['name'] for row in TABLE['bindings']}
        for key, label in {
            (47, 'physical'): 'Needle Spray',
            (49, 'physical'): 'Roses Rapier',
            (20, 'physical'): 'Trident Arm',
            (20, 'magic'): 'Giga Storm',
            (21, 'heavy'): 'Great Tornado',
            (21, 'magic'): 'Terra Force',
            (32, 'physical'): 'Wing Slap',
            (32, 'magic'): 'Air Shot',
        }.items():
            self.assertEqual(by_key[key], label)
        self.assertNotIn((47, 'magic'), by_key, 'Needle Spray is a fist attack on this official page')
        self.assertNotIn((14, 'magic'), by_key, 'shotgun fire cannot silently become magic')

    def test_effectful_or_unexplained_names_stay_unbound(self):
        selected = {(row['formId'], row['name']) for row in TABLE['bindings']}
        held = {(row['formId'], row['name']) for row in TABLE['held']}
        self.assertFalse(selected & held)
        for pair in [(46, 'Poison Ivy'), (60, 'Kohenkyo'), (49, 'Thorn Whip'),
                     (34, "Heaven's Gate"), (35, 'Testament'), (25, 'Blue Blaster')]:
            self.assertIn(pair, held)
        self.assertTrue(all(row['decision'] == 'hold-no-new-binding' and len(row['reason']) > 25 for row in TABLE['held']))

    def test_upstream_catalog_never_supplies_authenticity_or_mechanics(self):
        self.assertFalse(TABLE['mechanicsChanges'])
        self.assertEqual(TABLE['sourcePolicy']['secondary']['rejectedAdvertisementTechniqueCount'], 9)
        for row in TABLE['bindings']:
            ref = row['digigameCorroboration']
            self.assertRegex(ref['commit'], r'^[a-f0-9]{40}$')
            self.assertTrue(all(re.fullmatch(r'[a-z0-9_]+', key) for key in ref['techniqueIds']))
            self.assertFalse(any(re.search(r'adsby|googletag|document|window|script', key, re.I) for key in ref['techniqueIds']))
            self.assertTrue(all(x['signatureClaimAccepted'] is False for x in ref['storedMatchingAssignments']))
            self.assertFalse({'power', 'damageClass', 'element', 'effects', 'stats'} & set(row))

    def test_current_and_historical_label_authority_is_explicit(self):
        self.assertIn('immutable old profile strings', TABLE['integration']['recommendation'])
        self.assertIn('frozen trace.player.combat.skills', TABLE['integration']['browserAuto'])
        self.assertIn('saved-response output', TABLE['integration']['migration'])
        self.assertIn('Unmapped slots keep', TABLE['sourcePolicy']['fallback'])


if __name__ == '__main__':
    unittest.main()

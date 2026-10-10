#!/usr/bin/env python3
"""Assemble reviewed source metadata and authored rules; never fetch artwork.

The checked-in ID ledger is append-only. This script deliberately does not infer
aliases, evolution routes, or IDs from a name, category, URL, or page order.
Native profile generation and graph validation belong to generate-world-ds-profiles.py.
"""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('world_ds_profiles', ROOT / 'scripts/generate-world-ds-profiles.py')
PROFILES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILES)


def read(path):
    return json.loads((ROOT / path).read_text())


def indexed(records, description):
    result = {}
    for row in records:
        key = row['entryKey']
        if key in result:
            raise ValueError(f'Duplicate {description} key: {key}')
        result[key] = row
    return result


def assemble():
    inventory = read('docs/research/world-ds-inventory.json')
    ledger = read('data/world-ds-ids.json')
    old = {row['id']: row for row in read('docs/research/preserved-forms.json')['forms']}
    source = indexed(inventory['entries'], 'source')
    authored = indexed(sum((read(f'docs/research/world-ds-authored-{part}.json')['entries']
                            for part in ('early', 'late')), []), 'authored')
    official = indexed(sum((read(f'docs/research/world-ds-official-{part}.json')['entries']
                            for part in ('early', 'late')), []), 'official')
    assignments = indexed(ledger['entries'], 'ledger')
    reviews = indexed(read('data/world-ds-source-reviews.json')['entries'], 'visual review')
    skill_bindings = read('docs/research/battle-skill-bindings.json')['bindings']
    for key, review in reviews.items():
        selected = review.get('canonicalCandidateSelection')
        if not selected:
            continue
        record = official[key]
        candidate = next(row for row in record['officialReferences'] if row['canonicalEntityKey'] == selected)
        for field in ('canonicalName', 'canonicalStage', 'officialSpeciesType', 'officialAttribute',
                      'signatureSkillLabel', 'canonicalEntityKey'):
            record[field] = candidate[field]
        record['officialReferenceUrl'] = candidate['url']
        record['canonicalStatus'] = 'official-reference-with-local-sheet-visual-review'
        record['sheetVariantReviewRequired'] = False
        record['localSheetReview'] = review
    if set(source) != set(authored) or set(source) != set(assignments) or set(source) != set(official):
        raise ValueError('All four inputs must represent exactly the same source identities')
    edge_file = ROOT / 'data/world-ds-evolutions.json'
    routes = read('data/world-ds-evolutions.json') if edge_file.exists() else {'edges': []}
    edges = routes['edges']
    family_file = read('data/world-ds-family-identities.json')
    families = {row['formId']: row for row in family_file['identities']}
    by_form = {row['formId']: row for row in ledger['entries']}
    # Keep original66 routes even when the reviewed data contains only added routes.
    original_edges = [dict(fromFormId=f['id'], toFormId=child,
                           minimumLevel=old[child]['minLevel'], minimumBond=old[child]['minBond'])
                      for f in old.values() for child in f['children'] if child]
    anchors = {f['id']: (f['minLevel'], f['minBond']) for f in old.values()}
    anchors.update({form_id: PROFILES.TIERS[authored[binding['entryKey']]['combatTier']][:2]
                    for form_id, binding in by_form.items() if form_id > 66})
    edges, graph = PROFILES.evolution_graph(original_edges + edges, anchors,
        family_file['requiredEdges'], routes.get('dispositions'))
    entries = []
    for binding in ledger['entries']:
        key = binding['entryKey']
        item, rules = source[key], authored[key]
        form_id = binding['formId']
        preserved = old.get(binding['preservedFormId'])
        family = families.get(form_id)
        parent = preserved['parent'] if preserved else family['parent']
        next_forms = [v for v in preserved['children'] if v] if preserved else family['children']
        entry = {
            **binding,
            'lineageId': preserved['lineage'] if preserved else family['lineageId'],
            'parent': parent,
            'children': next_forms,
            'evolution': graph[form_id],
            'combatTier': rules['combatTier'],
            'role': rules['role'],
            'roleRationale': rules['roleRationale'],
            'type': preserved['type'] if preserved else rules['type'],
            'skills': preserved['skills'] if preserved else rules['skills'],
            'skillLabelOrigin': 'preserved-prototype' if preserved else 'authored',
            'speciesVariation': {'maxHp': 0, 'attack': 0, 'defense': 0, 'magic': 0, 'resistance': 0},
            'profileOrigin': 'preserved-rules-4' if preserved else 'authored-world-ds-1',
            'source': item,
            'official': official[key],
            'appearance': {
                'artId': f'ds-form-{form_id}', 'sourceEntryKey': key,
                'status': 'requires-local-audited-pack',
                'sourceUrl': item.get('sheetUrl'),
                'policy': 'Optional private unpublished artwork; availability is checked locally, never inferred from metadata.'
            },
            'obtainability': {
                'method': 'stage-appropriate-encounter-and-capture',
                'evolutionStatus': 'reviewed-authored-route' if graph[form_id]['parents'] or graph[form_id]['children'] else 'independent-encounter-form',
                'leafReason': graph[form_id]['reason'],
                'canonicalEvolutionClaim': False,
            },
        }
        entries.append(entry)
    return {
        'formatVersion': 1,
        'catalogId': 'digimon-world-ds',
        'catalogRevision': 6,
        'scope': f'{len(entries)} individually named source sheets, with distinct variants retained; not a deduplicated canonical species count.',
        'sourceUrl': inventory['sourceUrl'],
        'sourceEntryCount': len(entries),
        'nativeFormCount': max(by_form),
        'preservedBindings': sum(entry['preservedFormId'] is not None for entry in entries),
        'authoring': {
            'numericRules': 'Prototype balance, not official Digimon World DS stats.',
            'skills': 'Base labels remain frozen. Reviewed battleSkillBindings override selected runtime names while retaining the shared Physical, Heavy and Magic mechanics; no unique per-species combat implementations.',
            'officialReferences': 'Observed official labels are retained separately. Qualified aliases remain qualified.',
            'evolutions': 'Reviewed prototype routes; no claim to reproduce canonical game evolution requirements.',
            'artwork': 'No source sprites are embedded in this catalog or committed to the repository.',
        },
        'evolutionEdges': edges,
        'evolutionDispositions': routes.get('dispositions', []),
        'battleSkillBindings': skill_bindings,
        'encounterRarity': read('data/encounter-rarity.json'),
        'originalProfileAdjustments': PROFILES.original_ultimate_adjustments(read('docs/research/preserved-forms.json')),
        'entries': entries,
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Fail if the assembled catalog differs from the checked-in output')
    args = parser.parse_args()
    output = ROOT / 'data/world-ds-catalog.json'
    text = json.dumps(assemble(), ensure_ascii=False, indent=2) + '\n'
    if args.check:
        if not output.exists() or output.read_text() != text:
            raise SystemExit('Catalog is stale; run scripts/assemble-world-ds-catalog.py')
    else:
        output.write_text(text)
    print(f'{"Checked" if args.check else "Wrote"} {output.relative_to(ROOT)}')

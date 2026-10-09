#!/usr/bin/env python3
"""Export native-equivalent profiles and the actual reviewed evolution graph."""
import argparse
import csv
import io
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def export(catalog, runtime):
    forms = {row['formId']: row for row in runtime['forms']}
    reviewed_names = {row['formId'] for row in catalog.get('battleSkillBindings', [])}
    stats = ('maxHp', 'attack', 'defense', 'magic', 'resistance')
    rows = []
    for entry in catalog['entries']:
        form, official = forms[entry['formId']], entry['official']
        evolution = form['evolution']
        row = {'form_id': form['formId'], 'source_name': entry['displayName'], 'source_stage': entry['sourceStage'],
               'canonical_name': official.get('canonicalName'), 'canonical_stage': official.get('canonicalStage'),
               'canonical_mapping_status': official.get('mappingStatus', official.get('canonicalStatus')),
               'combat_tier': form['combatTier'], 'encounter_rarity': form['encounterRarity'], 'authored_role': entry['role'], 'game_type': form['type'],
               'entry_level': form['minLevel'], 'evolution_bond': form['minBond'],
               **{key: form['baseStats'][key] for key in stats},
               **{'growth_' + key: form['growth'][key] for key in stats},
               'physical_skill': form['skills']['physical'], 'heavy_skill': form['skills']['heavy'], 'magic_skill': form['skills']['magic'],
               'skill_origin': 'mixed-reviewed-official-and-authored' if form['formId'] in reviewed_names else entry['skillLabelOrigin'], 'stat_model': form['statModel'],
               'growth_note': 'Preserved Rookie interpolation through level10; listed growth applies above10.' if form['preserved'] and form['stage'] == 'Rookie' else 'Linear growth after the entry-level anchor.',
               'parent_ids': '|'.join(map(str, evolution['parents'])),
               'children_ids': '|'.join(map(str, evolution['children'])),
               'evolution_edges': json.dumps(evolution['edges'], separators=(',', ':')),
               'evolution_status': evolution['status'], 'leaf_reason': evolution['reason'],
               'historical_family_parent_id': form['parent'] or '',
               'historical_family_children_ids': '|'.join(map(str, form['children'])),
               'obtainable': form['obtainable'],
               'official_url': official.get('officialReferenceUrl'), 'sheet_url': entry['source'].get('sheetUrl'),
               'artwork': 'Optional audited private local pack; availability checked locally; not included in this export'}
        rows.append(row)
    stream = io.StringIO(newline='')
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return stream.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    catalog = json.loads((ROOT / 'data/world-ds-catalog.json').read_text())
    runtime = json.loads((ROOT / 'data/world-ds-runtime.json').read_text())
    text = export(catalog, runtime)
    output = ROOT / 'docs/evidence/world-ds-roster.csv'
    if args.check:
        if not output.exists() or output.read_text() != text:
            raise SystemExit('Roster CSV is stale; run scripts/export-world-ds-roster.py')
    else:
        output.write_text(text)
    print(f'{"Checked" if args.check else "Exported"} {len(catalog["entries"])} source entries: {output.relative_to(ROOT)}')


if __name__ == '__main__':
    main()

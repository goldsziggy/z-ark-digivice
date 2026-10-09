#!/usr/bin/env python3
"""Reproduce native/generator parity and preservation evidence; no network/art I/O."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=True)
    return result.stdout, result.stderr


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--esp-cxx', help='Optional existing ESP32-S3 g++ binary; compile an object only.')
    args = parser.parse_args()
    build = ROOT / 'build/catalog'
    build.mkdir(parents=True, exist_ok=True)
    generated, _ = run([sys.executable, 'scripts/generate-world-ds-profiles.py', '--check'])
    _, python_summary = run([sys.executable, 'tests/test_world_ds_generator.py'])
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    flags = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-Icore']
    run(compiler + flags + ['tests/world_ds_catalog_test.cpp', 'core/forms.cpp', 'core/combat.cpp', 'core/encounters.cpp',
                            '-o', str(build / 'world-ds-catalog-test')])
    output, native_summary = run([str(build / 'world-ds-catalog-test'), '--dump'])
    (build / 'native-forms.jsonl').write_text(output)
    native = [json.loads(line) for line in output.splitlines()]
    runtime = json.loads((ROOT / 'data/world-ds-runtime.json').read_text())
    baseline = json.loads((ROOT / 'docs/research/preserved-forms.json').read_text())
    bindings = json.loads((ROOT / 'docs/research/battle-skill-bindings.json').read_text())['bindings']
    require(native == runtime['forms'], 'Native details differ from generated runtime metadata')
    compared_levels = 0
    for old, current in zip(baseline['forms'], native):
        prior_skills = dict(current['skills'])
        for binding in bindings:
            if binding['formId'] == current['formId']:
                require(prior_skills[binding['category']] == binding['name'], 'Reviewed label not applied')
                prior_skills[binding['category']] = binding['previousName']
        compared = dict(id=current['formId'], name=current['name'], lineage=current['lineageId'],
                        stage=current['stage'], parent=current['parent'],
                        children=current['children'] + [0] * (2 - len(current['children'])),
                        minLevel=current['minLevel'], minBond=current['minBond'], type=current['type'],
                        skills=prior_skills, artId=current['artId'], baseStats=current['baseStats'],
                        growth=current['growth'], levels=current['statsByLevel'])
        if old['id'] in (3, 7):
            adjustment=next(row for row in json.loads((ROOT/'data/world-ds-catalog.json').read_text())['originalProfileAdjustments'] if row['formId']==old['id'])
            require(compared['baseStats']==adjustment['baseStats'], 'Reviewed original anchor differs')
            require(compared['levels']==[dict(level=level, **{key:adjustment['baseStats'][key]+(level-old['minLevel'])*old['growth'][key] for key in old['baseStats']}) for level in range(old['minLevel'],21)], 'Reviewed original curve differs')
            compared['baseStats']=old['baseStats'];compared['levels']=old['levels']
        require(compared == old, 'Preserved baseline changed beyond reviewed adjustments at form ' + str(old['id']))
        compared_levels += len(old['levels'])
    summary = re.fullmatch(r'catalog: (\d+) checks, (\d+) failures; max detail=(\d+)/(\d+), page=(\d+)/(\d+); Form=(\d+) CatalogEntry=(\d+) bytes \(host\)\n', native_summary)
    require(summary is not None, 'Unrecognized native summary')
    numbers = list(map(int, summary.groups()))
    report = dict(formatVersion=1, result='PASS', scope='Native host simulation and optional target object measurement; no physical hardware test.',
                  baselineCommit=baseline['baselineCommit'], nativeFormCount=len(native), sourceInventoryCount=runtime['sourceEntryCount'],
                  preservedForms=len(baseline['forms']), preservedLegalLevelProfiles=compared_levels,
                  nativeRuntimeExactEquality=True, preservedMetadataAndCurvesExactExceptReviewedLabelsAndTwoOriginalAnchors=True, reviewedOriginalAnchorFormIds=[3,7],
                  reviewedLabelBindings=len(bindings), reviewedLabelRenames=sum(b['name'] != b['previousName'] for b in bindings),
                  nativeChecks=numbers[0], nativeFailures=numbers[1], sanitizers=['address', 'undefined'],
                  nativeSummary=native_summary.strip(), pythonGeneratorSummary=python_summary.strip(),
                  maximumDetailBytes=numbers[2], detailCapacityBytes=numbers[3], maximumPageBytes=numbers[4], pageCapacityBytes=numbers[5],
                  hostRowBytes=dict(Form=numbers[6], CatalogEntry=numbers[7]),
                  maximumLineageForms=max(Counter(f['lineageId'] for f in native).values()),
                  maximumChildren=max(len(f['children']) for f in native),
                  maximumStats={k: max(s[k] for f in native for s in f['statsByLevel']) for k in ('maxHp', 'attack', 'defense', 'magic', 'resistance')},
                  generated=json.loads(generated)['files'])
    if args.esp_cxx:
        esp = Path(args.esp_cxx).resolve()
        require(esp.name.endswith('g++'), '--esp-cxx must name the g++ executable')
        prefix = str(esp)[:-3]
        obj = build / 'forms-esp32s3.o'
        target_flags = ['-std=c++17', '-Os', '-fdata-sections', '-ffunction-sections', '-Icore']
        run([str(esp)] + target_flags + ['-c', 'core/forms.cpp', '-o', str(obj)])
        sizes, _ = run([prefix + 'size', '-A', str(obj)])
        symbols, _ = run([prefix + 'nm', '--demangle', '--print-size', '--size-sort', str(obj)])
        (build / 'forms-esp32s3-size.txt').write_text(sizes + '\n' + symbols)
        sections = {parts[0]: int(parts[1]) for line in sizes.splitlines()
                    if len(parts := line.split()) == 3 and parts[0].startswith('.') and parts[1].isdigit()}
        def symbol_size(name):
            line = next(line for line in symbols.splitlines() if line.endswith('::' + name))
            return int(line.split()[1], 16)
        rows, mappings = symbol_size('kForms'), symbol_size('kCatalogEntries')
        banner, _ = run([str(esp), '--version'])
        report['esp32s3Object'] = dict(compiler=banner.splitlines()[0], flags=target_flags,
            formRowsBytes=rows, formRowBytes=rows // len(native), catalogMappingBytes=mappings,
            catalogMappingRowBytes=mappings // runtime['sourceEntryCount'],
            rodataBytes=sum(size for section, size in sections.items() if section.startswith('.rodata')),
            textBytes=sum(size for section, size in sections.items() if section.startswith('.text')),
            literalBytes=sum(size for section, size in sections.items() if section.startswith('.literal')),
            dataBytes=sections.get('.data', 0), bssBytes=sections.get('.bss', 0),
            caveat='Unlinked -Os object, not complete application flash/heap usage. Final linker merging and section removal may differ.')
    paths = ['core/forms.cpp', 'core/forms.hpp', 'core/combat.cpp', 'core/combat.hpp',
             'core/encounters.cpp', 'core/encounters.hpp', 'core/encounter_rarity_generated.inc', 'data/encounter-rarity.json',
             'scripts/generate-world-ds-profiles.py', 'scripts/verify-world-ds-catalog.py',
             'tests/world_ds_catalog_test.cpp', 'tests/test_world_ds_generator.py',
             'docs/research/preserved-forms.json', 'data/world-ds-catalog.json', 'data/world-ds-ids.json',
             'core/battle_skill_bindings_generated.inc', 'docs/research/battle-skill-bindings.json']
    report['sourceSha256'] = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths}
    destination = ROOT / 'docs/evidence/world-ds-catalog-verification.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(native_summary.strip())
    print(f'PASS: all {len(native)} native profiles equal runtime data; {len(baseline["forms"])} preserved forms / {compared_levels} legal levels exact apart from reviewed label bindings and approved form 3/7 anchors.')
    print(destination.relative_to(ROOT))


if __name__ == '__main__':
    main()

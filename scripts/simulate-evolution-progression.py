#!/usr/bin/env python3
"""Build and run the bounded native progression model; no device or network I/O."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
build = ROOT / 'build/progression'
build.mkdir(parents=True, exist_ok=True)
sources = ['scripts/simulate-evolution-progression.cpp', 'core/game.cpp', 'core/combat.cpp', 'core/encounters.cpp',
           'core/forms.cpp', 'core/battle_trace.cpp', 'core/legacy_combat_v3.cpp',
           'core/legacy_forms_v5.cpp', 'core/legacy_combat_v5.cpp',
           'core/legacy_forms_v6.cpp', 'core/legacy_combat_v6.cpp',
           'core/legacy_forms_v7.cpp', 'core/legacy_combat_v7.cpp', 'core/legacy_forms_v8.cpp', 'core/legacy_combat_v8.cpp', 'core/legacy_forms_v9.cpp', 'core/legacy_combat_v9.cpp']
flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Icore']
command = shlex.split(os.environ.get('CXX', 'c++')) + flags + sources + ['-o', str(build / 'simulate-evolution-progression')]
subprocess.run(command, cwd=ROOT, check=True)
result = subprocess.run([str(build / 'simulate-evolution-progression')], cwd=ROOT, text=True, capture_output=True)
if result.stderr:
    raise SystemExit(result.stderr)
report = json.loads(result.stdout)
runtime = json.loads((ROOT / 'data/world-ds-runtime.json').read_text())
forms = {f['formId']: f for f in runtime['forms']}
expected = {(f['formId'], e['toFormId']) for f in forms.values() for e in f['evolution']['edges']}
covered = {(r['from'], r['to']) for r in report['cases'] if r['result'] == 'PASS'}
report['coverage'] = dict(expectedEdges=len(expected), successfulEdges=len(covered),
                          missingEdges=[list(pair) for pair in sorted(expected-covered)])
report['scope'] = 'Host native simulation, not measured human play time, ESP frame rate, battery use or physical hardware.'
report['initialization'] = 'Each graph root starts as a valid captured form at its profile entry level/minBond with exactly threshold XP, full HP and normal 80 energy/70 fullness/80 mood. After initialization all state changes use public core actions; branching copies the reached state.'
report['policy'] = 'Two fixed seeds and both modes. Tactical uses visible guard/profile damage, one Shelter card, capture below half HP, and public release for extras. Auto uses the unchanged native policy. Rest restores HP/energy; Feed/Play/Rest earn bond naturally. No XP/bond/stat boosts after initialization.'
report['caveats'] = ['Reachability evidence covers these deterministic policies/seeds; it is not a balance guarantee for every opponent order or player decision.',
                     'Source roots are initialized as already captured; acquiring those source forms was covered by the separate catalog encounter/capture test.',
                     'Care-only Play/Rest loops are legal today but can be repetitive; action counts are reported without inventing elapsed play time.']
report['summary']['maximumBattlesPerEdge'] = max((r['battles'] for r in report['cases']), default=0)
report['summary']['maximumCareActionsPerEdge'] = max((r['careActions'] for r in report['cases']), default=0)
report['summary']['totalRetreats'] = sum(r['retreats'] for r in report['cases'])
report['summary']['earlyCareOnlyTransitions'] = sum(r['result']=='PASS' and r['battles']==0 and r['startXp']==r['gateXp'] and forms[r['from']]['combatTier'] in ('Fresh','In-Training') for r in report['cases'])
report['sourceSha256'] = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources + ['core/game.hpp','core/forms.hpp','core/world_ds_evolutions_generated.inc','data/world-ds-runtime.json']}
report['compilerFlags'] = flags
report['sanitizers'] = ['address','undefined']
if expected != covered or result.returncode:
    report['result'] = 'FAIL'
output = ROOT / 'docs/evidence/evolution-progression-simulation.json'
output.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps({k:report[k] for k in ('result','summary','coverage')}, indent=2))
print(output.relative_to(ROOT))
raise SystemExit(0 if report['result']=='PASS' else 1)

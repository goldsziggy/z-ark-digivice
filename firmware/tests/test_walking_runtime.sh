#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/walking-runtime
python3 - <<'PY'
from pathlib import Path
import hashlib,json
entries=[]
for name in ['handheld_walking.cpp','handheld_nearby.cpp']:
 source=Path('firmware/main')/name
 raw=source.read_bytes()
 old=b'#include "handheld_runtime.hpp"\n'
 new=b'#include "handheld_runtime_double.hpp"\n'
 assert raw.startswith(old) and raw.count(old)==1
 adapted=new+raw[len(old):]
 assert adapted[len(new):]==raw[len(old):]
 (Path('build/walking-runtime')/name).write_bytes(adapted)
 entries.append({'source':str(source),'sha256':hashlib.sha256(raw).hexdigest()})
 print('Testing exact runtime method body '+name+' SHA256 '+entries[-1]['sha256'])
manifest={'sources':entries,
          'linked_ui':[{'source':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [Path('firmware/runtime/device_ui.cpp'),Path('firmware/runtime/device_ui.hpp')]],
          'transform':'Replace first include only; every subsequent byte is exact production source.',
          'doubles':'Runtime declaration, published sampler snapshots, optional UI gate overrides, radio/network lifecycle, audio, SDK clock/random/delay; actual shared UI (Home panel tests), game, save, lifetime persistence and nearby protocol/match linked.'}
Path('build/walking-runtime/source-scope.json').write_text(json.dumps(manifest,indent=2)+'\n')
PY
sources=(firmware/tests/walking_runtime_test.cpp build/walking-runtime/handheld_walking.cpp build/walking-runtime/handheld_nearby.cpp
  firmware/main/save_store.cpp firmware/runtime/usage_store.cpp firmware/runtime/battle_presentation.cpp
  firmware/runtime/device_ui.cpp firmware/runtime/starter.cpp firmware/runtime/evolution_choice.cpp
  core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp core/battle_trace.cpp core/nearby_match.cpp firmware/runtime/nearby_protocol.cpp
  core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp
  core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp
  core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp)
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/walking_runtime_stubs -Ifirmware/tests/power_runtime_stubs \
  -Icore -Ifirmware/runtime -Ifirmware/main "${sources[@]}" -o build/walking-runtime/walking-runtime-test
./build/walking-runtime/walking-runtime-test

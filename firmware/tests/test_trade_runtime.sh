#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/trade-runtime
python3 - <<'PY'
from pathlib import Path
import hashlib,json
source=Path('firmware/main/handheld_trade.cpp');raw=source.read_bytes();old=b'#include "handheld_runtime.hpp"\n';new=b'#include "handheld_runtime_double.hpp"\n'
assert raw.startswith(old) and raw.count(old)==1
Path('build/trade-runtime/handheld_trade.cpp').write_bytes(new+raw[len(old):])
Path('build/trade-runtime/scope.json').write_text(json.dumps({'source':str(source),'sha256':hashlib.sha256(raw).hexdigest(),'transform':'First include only; all method bodies exact.','doubles':'Runtime declaration, UI touch cancellation, radio identity/close, SDK entropy and clock. Actual trade Session, journal Store, care SaveStore, core exchange and wire linked.'},indent=2)+'\n')
PY
sources=(firmware/tests/trade_runtime_test.cpp build/trade-runtime/handheld_trade.cpp firmware/runtime/trade_session.cpp firmware/runtime/trade_store.cpp firmware/runtime/trade_protocol.cpp firmware/runtime/nearby_protocol.cpp firmware/main/save_store.cpp core/trade.cpp core/nearby_match.cpp core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp core/battle_trace.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp)
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/trade_runtime_stubs -Ifirmware/tests/power_runtime_stubs -Icore -Ifirmware/runtime -Ifirmware/main "${sources[@]}" -o build/trade-runtime/trade-runtime-test
./build/trade-runtime/trade-runtime-test

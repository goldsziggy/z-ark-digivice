#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
capture_test_out="${DIGIVICE_CAPTURE_TEST_OUTPUT:-build/capture-runtime}"
mkdir -p "$capture_test_out"
python3 - "$capture_test_out" <<'PY'
from pathlib import Path
import hashlib,json,sys
out=Path(sys.argv[1])
p=Path('firmware/main/handheld_ui.cpp');s=p.read_text();a=s.index('void HandheldRuntime::pollInterface(');b=s.index('\nvoid HandheldRuntime::pauseInterface(',a);body=s[a:b]
anchor='    const auto motion = imu_.begin();\n'
start=s.index(anchor,s.index('void HandheldRuntime::beginInterface('))+len(anchor);end=s.index('    if (!saves_.writable())',start)
boot=s[start:end] # Exact save-to-presentation/UI startup block; peripheral init is excluded.
h=Path('firmware/main/handheld_runtime.hpp').read_text();a=h.index('    std::uint32_t recommendedPollDelayMs() const {');b=h.index('\n    }',a)+6;delay=h[a:b].replace('    std::uint32_t recommendedPollDelayMs() const {','std::uint32_t HandheldRuntime::recommendedPollDelayMs() const {',1)
(out/'runtime.cpp').write_text('#include "handheld_runtime_double.hpp"\n#include "esp_timer.h"\n#include <algorithm>\n#include <cstdio>\nnamespace digivice {\nvoid HandheldRuntime::restoreCaptureForTest(std::uint64_t now) {\n'+boot+'}\n'+body+'\n'+delay+'\n}\n')
(out/'scope.json').write_text(json.dumps({'methodSource':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'scope':'Exact save-to-presentation/UI startup block, complete pollInterface method and recommendedPollDelayMs method; peripheral initialization excluded; main owner declaration, SDKclock/touch, storage, audio and transferduration doubled. Actual UI renderer, capture model and CCW90 stripepack linked. Original synthetic artwork, no privatefiles.'},indent=2)+'\n')
PY
sources=(firmware/tests/capture_runtime_test.cpp "$capture_test_out/runtime.cpp" firmware/runtime/device_ui.cpp firmware/runtime/starter.cpp firmware/runtime/evolution_choice.cpp firmware/runtime/battle_presentation.cpp firmware/runtime/nearby_protocol.cpp core/nearby_match.cpp core/trade.cpp core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp core/battle_trace.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp)
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
 -Ifirmware/tests/capture_runtime_stubs -Ifirmware/tests/power_runtime_stubs -Icore -Ifirmware/runtime -Ifirmware/main \
 "${sources[@]}" -o "$capture_test_out/capture-runtime-test"
"$capture_test_out/capture-runtime-test"

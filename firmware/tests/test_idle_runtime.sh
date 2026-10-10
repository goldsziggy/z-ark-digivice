#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
idle_test_out="${DIGIVICE_IDLE_TEST_OUTPUT:-build/idle-runtime}"
mkdir -p "$idle_test_out"
python3 - "$idle_test_out" <<'PY'
from pathlib import Path
import hashlib,json,sys
out=Path(sys.argv[1]);entries=[]
def record(path,content,scope):
 raw=Path(path).read_bytes();entries.append({'source':path,'sha256':hashlib.sha256(raw).hexdigest(),'scope':scope})
 print('Testing actual idle integration',path,entries[-1]['sha256'])
 return content
path='firmware/main/handheld_idle.cpp';s=Path(path).read_text()
assert s.startswith('#include "handheld_runtime.hpp"\n')
(out/'handheld_idle.cpp').write_text(record(path,s.replace('"handheld_runtime.hpp"','"handheld_runtime_double.hpp"',1),'first include substituted; all method bytes unchanged'))
path='firmware/main/handheld_walking.cpp';s=Path(path).read_text()
start=s.index('bool HandheldRuntime::pollUsage(');end=s.index('\nbool HandheldRuntime::prepareUsageRestart',start)
body=s[start:end]
(out/'usage.cpp').write_text('#include "handheld_runtime_double.hpp"\n#include "esp_random.h"\n#include <algorithm>\nnamespace digivice {\n'+record(path,body,'exact pollUsage body; SDK/UI owner declaration doubled')+'\n}\n')
path='firmware/main/handheld_ui.cpp';s=Path(path).read_text()
start=s.index('void HandheldRuntime::handleTouchSample(');end=s.index('    // Full frames',start)
prefix=s[start:end]
assert 'pollIdle(now);' in prefix and 'idle_.blanked()}, model);' in prefix and 'touch_.feed(sample)' in prefix
# Assert the excluded renderer retains its idle guard; this harness does not render.
assert 'const bool canDraw = !idle_.blanked() && frame_ && display::displayReady();' in s[end:]
start=s.index('void HandheldRuntime::pauseInterface(');end=s.index('\nbool HandheldRuntime::interfaceQuiescent',start)
pause=s[start:end]
(out/'interface.cpp').write_text('#include "handheld_runtime_double.hpp"\n#include "esp_timer.h"\n#include <cstdio>\nnamespace digivice {\n'+record(path,prefix,'exact touch pipeline methods and input/policy prefix through pollIdle; rendering excluded and its guard asserted')+'}\n'+pause+'\n}\n')
(out/'source-scope.json').write_text(json.dumps(entries,indent=2)+'\n')
PY
sources=(firmware/tests/idle_runtime_test.cpp "$idle_test_out/handheld_idle.cpp" "$idle_test_out/usage.cpp" "$idle_test_out/interface.cpp"
  firmware/runtime/idle.cpp firmware/runtime/touch_stream.cpp firmware/runtime/usage_store.cpp firmware/main/save_store.cpp
  core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp core/battle_trace.cpp
  core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp
  core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp
  core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp)
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/idle_runtime_stubs -Ifirmware/tests/power_runtime_stubs -Icore -Ifirmware/runtime -Ifirmware/main \
  "${sources[@]}" -o "$idle_test_out/idle-runtime-test"
"$idle_test_out/idle-runtime-test"

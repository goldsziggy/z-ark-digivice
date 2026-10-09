#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
sources=(firmware/tests/power_runtime_test.cpp firmware/main/handheld_runtime.cpp firmware/main/save_store.cpp)
for unit in power network motion step_delivery recovery_choice asset_cache sprite prefetch file_asset_storage starter battle_mode_choice practice_session evolution_choice release_confirmation; do
  sources+=("firmware/runtime/$unit.cpp")
done
sources+=(core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp core/battle_trace.cpp core/practice_battle.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp)
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/power_runtime_stubs -Icore -Ifirmware/runtime -Ifirmware/main "${sources[@]}" -o build/firmware-host/power-runtime-test
./build/firmware-host/power-runtime-test

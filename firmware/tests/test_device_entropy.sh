#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/device-entropy
sources=(firmware/tests/device_entropy_test.cpp firmware/main/device_entropy.cpp firmware/main/save_store.cpp
  core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp core/battle_trace.cpp
  core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp
  core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp
  core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp)
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/entropy_stubs -Ifirmware/tests/power_runtime_stubs -Ifirmware/tests -Icore -Ifirmware/runtime -Ifirmware/main \
  "${sources[@]}" -o build/device-entropy/device-entropy-test
build/device-entropy/device-entropy-test

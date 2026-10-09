#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Icore -Ifirmware/main core/combat.cpp core/encounters.cpp core/forms.cpp \
  core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp \
  core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp \
  core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp \
  core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/game.cpp core/battle_trace.cpp \
  firmware/main/save_store.cpp firmware/runtime/evolution_choice.cpp \
  firmware/tests/save_store_test.cpp -o build/firmware-host/save-store-test
./build/firmware-host/save-store-test

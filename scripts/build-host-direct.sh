#!/usr/bin/env bash
# CMake-independent host build. Uses an existing compiler; installs nothing.
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."
mkdir -p build

compiler="${CXX:-c++}"
flags=(-std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -Werror -I core)

"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/game.cpp core/cli.cpp core/legacy_v1.cpp core/legacy_v2.cpp core/legacy_v3.cpp core/legacy_v4.cpp core/legacy_v5.cpp core/legacy_v6.cpp core/legacy_v7.cpp core/legacy_v8.cpp core/legacy_v9.cpp core/legacy_v10.cpp core/legacy_v11.cpp core/legacy_v12.cpp -o build/digivice-core
"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/practice_battle.cpp core/practice_cli.cpp -o build/digivice-battle
"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/game.cpp core/legacy_v3.cpp core/legacy_v4.cpp core/legacy_v5.cpp core/legacy_v6.cpp core/legacy_v7.cpp core/legacy_v8.cpp core/legacy_v9.cpp core/legacy_v10.cpp core/legacy_v12.cpp tests/core_test.cpp -o build/core-test
"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/game.cpp core/legacy_v3.cpp core/legacy_v4.cpp core/legacy_v5.cpp core/legacy_v6.cpp core/legacy_v7.cpp core/legacy_v8.cpp core/legacy_v9.cpp core/legacy_v10.cpp core/legacy_v11.cpp core/legacy_v12.cpp tests/care_capture_core_test.cpp -o build/care-capture-core-test
"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/practice_battle.cpp tests/practice_battle_test.cpp -o build/practice-battle-test

"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp tests/combat_test.cpp -o build/combat-test

"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/legacy_v3.cpp tests/legacy_v3_test.cpp -o build/legacy-v3-test

"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/legacy_v3.cpp core/legacy_v4.cpp tests/legacy_v4_test.cpp -o build/legacy-v4-test

"$compiler" "${flags[@]}" core/combat.cpp core/encounters.cpp core/forms.cpp tests/world_ds_catalog_test.cpp -o build/world-ds-catalog-test

printf '%s\n' 'Built host core, practice battle, and seven test executables in build/.'

#!/usr/bin/env bash
# Existing native compiler and Python stdlib only; no network or hardware access.
set -euo pipefail
script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."
if [[ $# != 0 ]]; then
  printf '%s\n' 'usage: scripts/run-balance-sim.sh' >&2
  exit 2
fi
mkdir -p build/balance
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Icore \
  scripts/balance-sim.cpp core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp \
  core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/practice_battle.cpp core/battle_trace.cpp \
  -o build/balance/balance-sim
build/balance/balance-sim > build/balance/raw.jsonl
python3 scripts/summarize-balance.py build/balance/raw.jsonl docs/evidence/balance-rpg.json

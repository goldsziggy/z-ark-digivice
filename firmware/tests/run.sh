#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
bash firmware/tests/test_device_entropy.sh
bash firmware/tests/test_save_store.sh
bash firmware/tests/test_audio_settings.sh
bash firmware/tests/test_audio_worker.sh
bash firmware/tests/test_trade_session.sh
bash firmware/tests/test_trade_runtime.sh

# Shared portable runtime and fault models; no ESP SDK or serial port needed.
for module in power network motion step_delivery recovery_choice asset_cache sprite prefetch file_asset_storage starter battle_mode_choice practice_session evolution_choice release_confirmation local_form_art; do
  sources=("firmware/runtime/${module}.cpp" "firmware/tests/${module}_test.cpp")
  if [[ "$module" == local_form_art ]]; then sources+=(firmware/runtime/sprite.cpp); fi
  if [[ "$module" == file_asset_storage ]]; then sources+=(firmware/runtime/asset_cache.cpp); fi
  if [[ "$module" == prefetch || "$module" == evolution_choice || "$module" == step_delivery || "$module" == recovery_choice || "$module" == release_confirmation ]]; then sources+=(core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/battle_trace.cpp); fi
  if [[ "$module" == step_delivery ]]; then sources+=(firmware/runtime/motion.cpp firmware/main/save_store.cpp); fi
  if [[ "$module" == recovery_choice || "$module" == release_confirmation ]]; then sources+=(firmware/main/save_store.cpp); fi
  if [[ "$module" == practice_session ]]; then sources+=(core/game.cpp core/combat.cpp core/encounters.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp core/battle_trace.cpp core/practice_battle.cpp); fi
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Icore -Ifirmware/runtime -Ifirmware/main "${sources[@]}" -o "build/firmware-host/${module}-test"
  "./build/firmware-host/${module}-test"
done

#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
mode=wasm
if [[ "${1:-}" == "--native" ]]; then mode=native; shift; fi
if [[ "${1:-}" == "--notices" ]]; then mode=notices; shift; fi
source_root="${1:-$repo_root}"
default_output="$repo_root/docs/play/runtime"
if [[ "$mode" == native ]]; then default_output="$repo_root/tools/browser-demo/native-output"; fi
output_dir="${2:-$default_output}"
source_commit="${DEMO_SOURCE_COMMIT:-171cda7e698cf2915b50aa46bc766d8d1d0ee50d}"
[[ "$source_commit" =~ ^[a-f0-9]{40}$ ]] || { echo 'DEMO_SOURCE_COMMIT must be a full lowercase Git SHA.' >&2; exit 2; }
[[ -f "$source_root/core/game.cpp" ]] || { echo 'Source root must contain core/game.cpp.' >&2; exit 2; }

sources=(game combat encounters battle_trace forms legacy_combat_v3 legacy_forms_v5 legacy_combat_v5 legacy_forms_v6 legacy_combat_v6 legacy_forms_v7 legacy_combat_v7 legacy_forms_v8 legacy_combat_v8 legacy_forms_v9 legacy_combat_v9)
inputs=("$script_dir/bridge.cpp")
for source in "${sources[@]}"; do inputs+=("$source_root/core/$source.cpp"); done
common=(-std=c++17 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti "-I$source_root/core" "-DDEMO_SOURCE_COMMIT=\"$source_commit\"")
mkdir -p -- "$output_dir"

if [[ "$mode" == native ]]; then
  "${CXX:-clang++}" "${common[@]}" -O2 -DDIGIVICE_DEMO_NATIVE "${inputs[@]}" -o "$output_dir/digivice-demo-native"
  echo "Native bridge: $output_dir/digivice-demo-native"
else
  emxx="${EMXX:-}"
  if [[ -z "$emxx" && -n "${EMSDK:-}" ]]; then emxx="$EMSDK/upstream/emscripten/em++"; fi
  if [[ -z "$emxx" ]]; then emxx="$(command -v em++ || true)"; fi
  [[ -n "$emxx" && -x "$emxx" ]] || { echo 'Set EMXX to em++, or EMSDK to a prepared Emscripten SDK.' >&2; exit 2; }
  toolchain_root="${EMSCRIPTEN_ROOT:-$(dirname -- "$emxx")}"
  notices=(LICENSE system/lib/libc/musl/COPYRIGHT system/lib/compiler-rt/LICENSE.TXT system/lib/libcxx/LICENSE.TXT system/lib/libcxxabi/LICENSE.TXT)
  for notice in "${notices[@]}"; do
    [[ -f "$toolchain_root/$notice" ]] || { echo "Missing SDK notice: $notice (set EMSCRIPTEN_ROOT when EMXX is a wrapper)." >&2; exit 2; }
  done
  if [[ "$mode" == wasm ]]; then
    "$emxx" "${common[@]}" -Oz "${inputs[@]}" --no-entry \
      -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createDemoCore -sENVIRONMENT=web,worker,node \
      -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16777216 -sMAXIMUM_MEMORY=67108864 \
      -sSTACK_SIZE=1048576 -sASSERTIONS=0 \
      '-sEXPORTED_RUNTIME_METHODS=["ccall"]' \
      '-sEXPORTED_FUNCTIONS=["_demo_reset","_demo_command","_demo_state","_demo_snapshot","_demo_load","_demo_starters","_demo_form","_demo_catalog","_demo_evolutions"]' \
      -o "$output_dir/demo-core.js"
    chmod 0644 "$output_dir/demo-core.js" "$output_dir/demo-core.wasm"
    echo "Browser runtime: $output_dir/demo-core.js and demo-core.wasm"
  fi
  {
    printf '%s\n' 'Runtime component notices — Emscripten 6.0.12' 'These notices apply only to the named compiler/runtime components.'
    for notice in "${notices[@]}"; do
      printf '\n--- %s ---\n\n' "$notice"
      cat -- "$toolchain_root/$notice"
      printf '\n'
    done
  } | awk '{ sub(/[ \t]+$/, ""); if ($0 == "") { blanks++; next } while (blanks > 0) { print ""; blanks-- } print }' > "$output_dir/NOTICES.txt"
  echo "Runtime notices: $output_dir/NOTICES.txt"
fi

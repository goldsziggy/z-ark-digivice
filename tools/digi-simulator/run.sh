#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
assets=${1:-/Volumes/T9/digivice-migration-20261015/assets-matched-to-source}
out=${2:-"$root/../recordings"}
cmake_bin=${CMAKE:-/opt/homebrew/bin/cmake}
"$cmake_bin" -S "$root" -B "$root/../build/host" -DCMAKE_BUILD_TYPE=Debug
"$cmake_bin" --build "$root/../build/host" --target digi-simulator -j4
mkdir -p "$out"
for scene in manual auto dungeon element fx; do
  "$root/../build/host/digi-simulator" "$assets" "$scene" "$out/$scene.rgb"
  python3 "$root/tools/digi-simulator/encode.py" "$out/$scene.rgb" "$out/$scene.mp4" --title "Digi-Modify | $scene"
done

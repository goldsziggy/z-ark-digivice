#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d "${TMPDIR:-/tmp}/digivice-sprite-bounds.XXXXXX")
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/runtime firmware/tests/sprite_bounds_test.cpp -o "$work/sprite-bounds-test"
"$work/sprite-bounds-test"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d "${TMPDIR:-/tmp}/digivice-board21.XXXXXX")
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
 -Ifirmware/tests/board21_stubs -Ifirmware/main firmware/main/board_hal.cpp firmware/tests/board21_test.cpp -o "$work/test"
"$work/test" 0
for fault in {1..18}; do "$work/test" "$fault"; done

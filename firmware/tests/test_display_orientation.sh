#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d "${TMPDIR:-/tmp}/digivice-display-orientation.XXXXXX")
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  firmware/tests/display_orientation_test.cpp -o "$work/display-orientation-test"
"$work/display-orientation-test"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d "${TMPDIR:-/tmp}/digivice-sam.XXXXXX")
trap 'rm -rf "$work"' EXIT
for version in before after; do
 defines=(-DPN532_STARTUP_REGRESSION=1)
 if [[ "$version" == before ]]; then defines=(-DEXPECT_OLD_STARTUP=1 '-DPN532_SESSION_HEADER="fixtures/pn532_hsu_before_sam.hpp"'); fi
 "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  "${defines[@]}" firmware/tests/pn532_startup_regression.cpp -o "$work/$version"
 "$work/$version"
done

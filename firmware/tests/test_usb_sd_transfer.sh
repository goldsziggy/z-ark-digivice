#!/usr/bin/env bash
# Uses the existing host mbedTLS3 installation; no toolchain installation.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/digivice-usb-sd.XXXXXX")
trap 'rm -rf "$build"' EXIT
mbed=${MBEDTLS_PREFIX:-/opt/homebrew/opt/mbedtls@3}
if [[ ! -f "$mbed/include/mbedtls/sha256.h" ]]; then
  printf '%s\n' "Missing existing host dependency: $mbed/include/mbedtls/sha256.h (nothing installed)." >&2
  exit 2
fi
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I"$mbed/include" \
  "$repo/firmware/tests/usb_sd_transfer_test.cpp" -L"$mbed/lib" -lmbedcrypto -o "$build/test"
"$build/test"

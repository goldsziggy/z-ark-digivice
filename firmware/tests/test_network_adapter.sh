#!/usr/bin/env bash
# Actual adapter against host SDK/RTOS doubles and existing cJSON; no installs.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/digivice-network-adapter.XXXXXX")
trap 'rm -rf "$build"' EXIT
cjson=${CJSON_PREFIX:-/opt/homebrew}
cd "$repo"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/network_adapter_stubs -Ifirmware/tests/assets_client_stubs -Icore -I"$cjson/include/cjson" \
  firmware/tests/network_adapter_test.cpp firmware/runtime/network.cpp \
  -L"$cjson/lib" -lcjson -o "$build/test"
"$build/test"

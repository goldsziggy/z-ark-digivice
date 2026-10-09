#!/usr/bin/env bash
# Actual SD adapter/FileAssetStorage on a disposable host file. SD/board HAL
# doubled, POSIX fsync failures injected; no card or GPIO access, no installs.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/digivice-sd-power.XXXXXX")
trap 'rm -rf "$build"' EXIT
cd "$repo"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DCONFIG_DIGIVICE_BOARD_WAVESHARE_146=1 -DCONFIG_DIGIVICE_SD_ASSETS=1 \
  -Ifirmware/tests/sd_storage_stubs -Ifirmware/tests/assets_client_stubs \
  firmware/tests/sd_power_test.cpp firmware/runtime/file_asset_storage.cpp \
  -o "$build/test"
"$build/test" "$build/DVASSET1.CCH"

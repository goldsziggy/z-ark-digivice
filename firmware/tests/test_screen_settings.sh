#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
output="${DIGIVICE_SCREEN_TEST_OUTPUT:-build/firmware-host}"
stubs="${DIGIVICE_SCREEN_TEST_STUBS:-firmware/tests/idle_stubs}"
mkdir -p "$output"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$stubs" -Ifirmware/main \
  firmware/main/screen_settings.cpp firmware/tests/screen_settings_test.cpp \
  -o "$output/screen-settings-test"
"$output/screen-settings-test"

#!/usr/bin/env bash
# Compile the REAL board HAL against minimal GPIO-call stubs. No ESP/serial I/O.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
for board in 0 1; do
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DCONFIG_DIGIVICE_BOARD_WAVESHARE_146="$board" \
    -Ifirmware/tests/power_hal_stubs -Ifirmware/main \
    firmware/main/board_hal.cpp firmware/tests/power_hal_test.cpp \
    -o "build/firmware-host/power-hal-$board-test"
  "./build/firmware-host/power-hal-$board-test" 0
  if [[ "$board" == 1 ]]; then
    # Fresh processes reset the production file's application-lifetime statics.
    for fault in 1 2 3 4; do "./build/firmware-host/power-hal-$board-test" "$fault"; done
  fi
done

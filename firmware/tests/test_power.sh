#!/usr/bin/env bash
# Focused power verification; existing host compiler/dependencies only.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/runtime firmware/runtime/power.cpp firmware/tests/power_test.cpp \
  -o build/firmware-host/power-test
./build/firmware-host/power-test
bash firmware/tests/test_power_runtime.sh
bash firmware/tests/test_network_adapter.sh
bash firmware/tests/test_sd_power.sh
bash firmware/tests/test_power_hal.sh

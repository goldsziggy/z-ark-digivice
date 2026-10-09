#!/usr/bin/env bash
# Host compiler only. Does not open a serial port or use hardware/network/storage.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -Ifirmware/runtime firmware/runtime/power.cpp scripts/power-demo.cpp \
  -o build/firmware-host/power-demo
./build/firmware-host/power-demo

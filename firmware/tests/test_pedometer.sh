#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Ifirmware/runtime \
  firmware/runtime/pedometer.cpp firmware/tests/pedometer_test.cpp \
  -o build/firmware-host/pedometer-test
build/firmware-host/pedometer-test

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/nearby_radio_stubs -Ifirmware/main \
  firmware/main/nearby_radio.cpp firmware/tests/nearby_radio_test.cpp \
  -o build/firmware-host/nearby-radio-test
build/firmware-host/nearby-radio-test

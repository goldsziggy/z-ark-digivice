#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/imu_worker_stubs -Ifirmware/main -Ifirmware/runtime \
  firmware/runtime/pedometer.cpp firmware/runtime/imu_filter.cpp \
  firmware/main/device_imu.cpp firmware/tests/imu_worker_test.cpp \
  -o build/firmware-host/imu-worker-test
build/firmware-host/imu-worker-test

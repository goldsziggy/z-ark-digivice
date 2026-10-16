#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
 -fsanitize=address,undefined -fno-omit-frame-pointer \
 -Ifirmware/tests/buzzer_stubs -Ifirmware/tests/audio_stubs -Ifirmware/tests/board21_stubs -Ifirmware/tests \
 -Ifirmware/main -Ifirmware/runtime \
 firmware/main/board_hal.cpp firmware/main/device_audio.cpp firmware/main/device_buzzer.cpp \
 firmware/main/audio_settings.cpp firmware/runtime/audio_settings.cpp firmware/runtime/audio_cues.cpp \
 firmware/tests/audio_test_nvs.cpp firmware/tests/buzzer_test.cpp -o build/firmware-host/buzzer-test
build/firmware-host/buzzer-test

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/audio_stubs -Ifirmware/main -Ifirmware/runtime \
  firmware/runtime/audio_settings.cpp firmware/main/audio_settings.cpp \
  firmware/runtime/audio_cues.cpp firmware/runtime/music_synth.cpp \
  firmware/main/device_audio.cpp firmware/tests/audio_test_nvs.cpp firmware/tests/audio_worker_test.cpp \
  -o build/firmware-host/audio-worker-test
build/firmware-host/audio-worker-test

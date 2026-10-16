#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -DTEST_HOST=1 -DCONFIG_DIGIVICE_NFC_UART_GAMEPLAY=1 -Icore -Ifirmware/runtime -Ifirmware/tests/nfc_uart_stubs firmware/main/device_nfc_diag.cpp firmware/tests/nfc_uart_mock.cpp firmware/tests/nfc_test_screen_test.cpp -o build/firmware-host/nfc-test-screen
./build/firmware-host/nfc-test-screen

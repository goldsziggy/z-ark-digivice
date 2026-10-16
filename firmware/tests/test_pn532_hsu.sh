#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d "${TMPDIR:-/tmp}/digivice-hsu.XXXXXX")
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    firmware/tests/pn532_hsu_test.cpp -o "$work/test"
"$work/test"
for gate in enabled disabled; do
    defines=(-DTEST_HOST=1 -DCONFIG_DIGIVICE_NFC_UART_GAMEPLAY=1)
    if [[ "$gate" == disabled ]]; then defines=(-DTEST_DISABLED=1); fi
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
        "${defines[@]}" -Icore -Ifirmware/runtime -Ifirmware/tests/nfc_uart_stubs \
        firmware/main/device_nfc_diag.cpp firmware/tests/nfc_uart_device_test.cpp -o "$work/device"
    "$work/device"
done

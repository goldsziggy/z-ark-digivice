#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d "${TMPDIR:-/tmp}/digivice-idle.XXXXXX")
trap 'rm -rf "$work"' EXIT
python3 - "$work" <<'PY'
from pathlib import Path
import sys,hashlib
source=Path('firmware/main/display_touch.cpp').read_text()
start=source.index('esp_err_t setIdleBlank(bool blanked) {')
end=source.index('\nesp_err_t setSuspended(',start)
body=source[start:end]
(Path(sys.argv[1])/'idle_hal.inc').write_text(body)
print('Actual idle backlight HAL SHA256',hashlib.sha256(body.encode()).hexdigest())
PY
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/idle_stubs -Ifirmware/runtime -Ifirmware/main -I"$work" \
  firmware/runtime/idle.cpp firmware/runtime/idle_settings.cpp firmware/main/idle_settings.cpp \
  firmware/tests/idle_test.cpp -o "$work/idle-test"
"$work/idle-test"

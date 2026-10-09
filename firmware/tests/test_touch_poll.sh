#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d "${TMPDIR:-/tmp}/digivice-touch-poll.XXXXXX")
trap 'rm -rf "$work"' EXIT
python3 - "$work" <<'PY'
from pathlib import Path
import subprocess,sys,hashlib
out=Path(sys.argv[1]); path='firmware/main/display_touch.cpp'
def extract(source):
    start=source.index('esp_err_t pollTouch(TouchPoint& point) {')
    end=min(source.find('\nesp_err_t '+name+'(',start) for name in ['setIdleBlank','setSuspended'] if source.find('\nesp_err_t '+name+'(',start)>=0)
    return source[start:end]
current=extract(Path(path).read_text())
legacy=extract(subprocess.check_output(['git','show','1d5552edeaa7d3aec0eb16853429dbb7d843dde6:'+path],text=True))
assert 'board::lockSharedI2c(20)' in current
assert 'board::lockSharedI2c(2)' in legacy
(out/'current_poll.inc').write_text(current)
(out/'legacy_poll.inc').write_text(legacy.replace('esp_err_t pollTouch(', 'esp_err_t legacyPollTouch(',1))
print('Actual current pollTouch SHA256',hashlib.sha256(current.encode()).hexdigest())
print('Actual installed1d5552e pollTouch SHA256',hashlib.sha256(legacy.encode()).hexdigest())
PY
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/tests/touch_poll_stubs -Ifirmware/main -I"$work" \
  firmware/tests/touch_poll_test.cpp -o "$work/touch-poll-test"
"$work/touch-poll-test"

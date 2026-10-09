#!/usr/bin/env bash
# Same portable practice parser/session as ESP, with two host files in a fresh
# private temporary directory. No SDK, device, radio, network or installs.
set -euo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
cd "$script_dir/.."
if [[ $# -gt 1 || ( $# == 1 && $1 != --interactive ) ]]; then
  printf '%s\n' 'usage: scripts/run-practice-serial-demo.sh [--interactive]' >&2
  exit 2
fi
mkdir -p build/firmware-host
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -Icore -Ifirmware/runtime core/combat.cpp core/encounters.cpp core/game.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp \
  core/practice_battle.cpp firmware/runtime/practice_session.cpp \
  scripts/practice-serial-host.cpp -o build/firmware-host/practice-serial-host
python3 - "${1:-}" <<'PY'
from pathlib import Path
import json
import subprocess
import sys
import tempfile

binary = Path.cwd() / 'build/firmware-host/practice-serial-host'
with tempfile.TemporaryDirectory(prefix='digivice-practice-serial-', dir='/tmp') as folder:
    if sys.argv[1] == '--interactive':
        print('Temporary Impmon fixture; use practice help/status, then practice start 1 0 tactical or auto.', flush=True)
        print('Each mutation needs a NEW increasing command ID and the current expected revision; an exact retry reuses both.', flush=True)
        print('Example after Tactical start: practice act 2 1 physical. Ctrl-D ends this disposable session.', flush=True)
        with open('/dev/tty', 'r') as terminal:
            raise SystemExit(subprocess.call([str(binary), folder], stdin=terminal))

    def run(commands):
        result = subprocess.run([str(binary), folder], input='\n'.join(commands)+'\n', text=True, capture_output=True, check=True)
        assert 'CARE_UNCHANGED=true' in result.stdout
        # Public output must never reveal native RNG/commitment or raw snapshots.
        for hidden in ('rngState', 'excludedChoice', 'snapshotBase64', 'initialSnapshot'):
            assert hidden not in result.stdout
        print(result.stdout, end='')
        return result.stdout

    def last_battle(output):
        return json.loads([line[7:] for line in output.splitlines() if line.startswith('BATTLE ')][-1])

    def files():
        return {p.name:p.read_bytes() for p in Path(folder).glob('*.bin')}

    first = run(['practice start 1 0 tactical', 'practice act 2 1 physical'])
    assert last_battle(first)['sequence'] == 1
    before_retry = files()
    retry = run(['practice act 2 1 physical'])
    assert 'REPLY exact retry; saved result unchanged' in retry
    assert files() == before_retry and last_battle(retry) == last_battle(first)
    ended = run(['practice act 3 2 retreat'])
    assert last_battle(ended)['status'] == 'retreated'
    automatic = run(['practice start 4 3 auto', 'practice replay'])
    saved = files(); terminal = last_battle(automatic)
    assert terminal['status'] in ('won','lost','draw') and terminal['exchanges'] <= terminal['maxExchanges'] == 40
    recovered = run(['practice start 4 3 auto', 'practice replay', 'practice act 5 4 physical'])
    assert 'REPLY exact retry; saved result unchanged' in recovered
    assert 'REPLY Auto result is complete; manual actions are disabled' in recovered
    assert files() == saved and last_battle(recovered) == terminal
    trace = lambda output: [line for line in output.splitlines() if line.startswith(('REPLAY ', 'TURN '))]
    assert trace(automatic) == trace(recovered)
    assert all(len(value) == 304 for value in saved.values())
    print('PASS: same ESP practice parser; Tactical exchange/retry/retreat; confirmed Auto; five fresh processes; exact result/replay; no extra writes or care changes.')
print('CLEANUP: removed only this run\'s private practice directory.')
PY

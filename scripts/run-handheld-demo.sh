#!/usr/bin/env bash
# Existing host compiler + Python stdlib only. No installs, radios or network.
set -euo pipefail
script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."
mkdir -p build/firmware-host
demo_mode=nor
demo_binary=handheld-demo
demo_flags=()
demo_sources=()
if [[ $# == 1 && $1 == --file-cache ]]; then
  demo_mode=file
  demo_binary=handheld-sd-demo
  demo_flags=(-DDIGIVICE_DEMO_FILE_CACHE)
  demo_sources=(firmware/runtime/file_asset_storage.cpp)
elif [[ $# != 0 ]]; then
  printf '%s\n' 'usage: scripts/run-handheld-demo.sh [--file-cache]' >&2
  exit 2
fi
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  "${demo_flags[@]}" \
  -Icore -Ifirmware/main -Ifirmware/runtime \
  core/combat.cpp core/encounters.cpp core/game.cpp core/battle_trace.cpp core/forms.cpp core/legacy_combat_v3.cpp core/legacy_forms_v5.cpp core/legacy_combat_v5.cpp core/legacy_forms_v6.cpp core/legacy_combat_v6.cpp core/legacy_forms_v7.cpp core/legacy_combat_v7.cpp core/legacy_forms_v8.cpp core/legacy_combat_v8.cpp core/legacy_forms_v9.cpp core/legacy_combat_v9.cpp firmware/main/save_store.cpp \
  firmware/runtime/motion.cpp firmware/runtime/network.cpp firmware/runtime/asset_cache.cpp \
  "${demo_sources[@]}" scripts/handheld-demo.cpp -o "build/firmware-host/$demo_binary"
python3 - "$demo_mode" "$demo_binary" <<'PY'
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path.cwd()
directory = root / 'assets/device'

def require(condition, message):
    if not condition:
        raise SystemExit(message)

index_bytes = (directory / 'index.json').read_bytes()
if len(index_bytes) > 8192:
    raise SystemExit('device fixture index exceeds bound')
index = json.loads(index_bytes)
require(index['formatVersion'] == 1 and index['profile'] == 's3-146-v1', 'unexpected device fixture profile')
entries = [entry for entry in index['packs'] if entry['id'] == 'sprite-flicker-v1']
require(len(entries) == 1, 'expected one original Flicker entry')
entry = entries[0]
require(entry['kind'] == 'sprite' and entry['width'] == entry['height'] == 32, 'unexpected fixture type/dimensions')
require(entry['file'] == 'sprite-flicker-v1.dva' and 4096 < entry['bytes'] <= 128 * 1024, 'unexpected fixture filename/size')
path = directory / entry['file']
require(path.stat().st_size == entry['bytes'], 'fixture size mismatch')
require(hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256'], 'fixture SHA-256 mismatch')
args = [str(root / 'build/firmware-host' / sys.argv[2]), str(path), entry['id'],
        str(entry['version']), str(entry['bytes']), entry['sha256'],
        str(entry['width']), str(entry['height'])]
if sys.argv[1] == 'file':
    # Only this fresh 0700 temporary directory is owned/removed by the launcher.
    # The binary uses O_EXCL; it never truncates or replaces an existing file.
    with tempfile.TemporaryDirectory(prefix='digivice-sd-host-', dir='/tmp') as temporary:
        cache_path = Path(temporary) / 'owned-cache.bin'
        result = subprocess.run(args + [str(cache_path)], check=False)
        if result.returncode == 0 and cache_path.stat().st_size != 696832:
            raise SystemExit('owned cache file size disagrees with current profile')
    print('CLEANUP: removed only this run\'s owned temporary cache directory.', flush=True)
    raise SystemExit(result.returncode)
raise SystemExit(subprocess.run(args, check=False).returncode)
PY

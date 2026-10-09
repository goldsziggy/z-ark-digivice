#!/usr/bin/env bash
# Build only with an already installed official ESP-IDF 5.3.6 toolchain.
# No installation, flashing, monitor, device port, or shell-profile writes.
if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
    printf '%s\n' 'Run this script; do not source it.' >&2
    return 2
fi
set -euo pipefail

usage() {
    cat <<'HELP'
Usage: scripts/build-esp.sh [generic|waveshare|waveshare-dev]

Default: generic (no board GPIOs, development assets/private HTTP disabled).
Waveshare: SKU29565 board + microSD primary asset storage, network opt-ins off.
Waveshare-dev: same board/storage + public test asset key/private HTTP build flags.
No credentials are included; private HTTP still requires runtime opt-in.

Uses existing task-local ESP-IDF 5.3.6 and Python 3.13 environment. Optional paths:
  DIGIVICE_IDF_PATH          default ../.toolchains/esp-idf-v5.3.6
  DIGIVICE_IDF_TOOLS_PATH    default ../.toolchains/espressif-v5.3.6
  DIGIVICE_IDF_PYTHON_ENV    default <tools>/python_env/idf5.3_py3.13_env

Each profile builds in firmware/build-<profile>. Its generated sdkconfig is
recreated from committed defaults; manual edits there are intentionally replaced.
Only 'build size' is invoked. Nothing is flashed or downloaded.
HELP
}
if [[ $# -gt 1 ]]; then usage >&2; exit 2; fi
profile=${1:-generic}
case "$profile" in
    -h|--help) usage; exit 0 ;;
    generic|waveshare|waveshare-dev) ;;
    *) usage >&2; exit 2 ;;
esac

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
export IDF_PATH=${DIGIVICE_IDF_PATH:-$repo_dir/../.toolchains/esp-idf-v5.3.6}
export IDF_TOOLS_PATH=${DIGIVICE_IDF_TOOLS_PATH:-$repo_dir/../.toolchains/espressif-v5.3.6}
export IDF_PYTHON_ENV_PATH=${DIGIVICE_IDF_PYTHON_ENV:-$IDF_TOOLS_PATH/python_env/idf5.3_py3.13_env}
if [[ ! -f "$IDF_PATH/export.sh" || ! -f "$IDF_PATH/tools/idf.py" || ! -x "$IDF_PYTHON_ENV_PATH/bin/python" ]]; then
    printf '%s\n' 'Official ESP-IDF 5.3.6/Python environment is missing. Install it separately before building; this wrapper installs nothing.' >&2
    exit 2
fi
# Canonicalize paths so IDF recognizes its own venv (no ../ spelling mismatch).
export IDF_PATH=$(CDPATH= cd -- "$IDF_PATH" && pwd -P)
export IDF_TOOLS_PATH=$(CDPATH= cd -- "$IDF_TOOLS_PATH" && pwd -P)
export IDF_PYTHON_ENV_PATH=$(CDPATH= cd -- "$IDF_PYTHON_ENV_PATH" && pwd -P)
export IDF_PY_BUILD_JOBS=${IDF_PY_BUILD_JOBS:-4}
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
# Keep a caller's unrelated Python environment from changing IDF imports.
unset PYTHONHOME PYTHONPATH
"$IDF_PYTHON_ENV_PATH/bin/python" - "$IDF_PATH/tools/cmake/version.cmake" <<'PY'
import pathlib, re, sys
text = pathlib.Path(sys.argv[1]).read_text()
version = tuple(int(re.search(r'set\(IDF_VERSION_' + part + r' (\d+)\)', text).group(1)) for part in ('MAJOR', 'MINOR', 'PATCH'))
if version != (5, 3, 6):
    sys.exit('This build wrapper requires the official ESP-IDF 5.3.6 release.')
PY
# Official export scripts are written for a shell without nounset. Changes are
# confined to this script process, including the official CMake/Ninja PATH.
set +u
source "$IDF_PATH/export.sh"
set -u

build_dir="$repo_dir/firmware/build-$profile"
if [[ -L "$build_dir" ]]; then printf '%s\n' 'Refusing a symlinked build directory.' >&2; exit 2; fi
mkdir -p "$build_dir"
if ! mkdir "$build_dir/.wrapper-lock" 2>/dev/null; then
    printf '%s\n' 'This profile is already building, or has a stale .wrapper-lock. Remove a stale lock only after verifying no build is running.' >&2
    exit 2
fi
trap 'rmdir "$build_dir/.wrapper-lock"' EXIT
# These two paths are generated outputs in this isolated profile only.
rm -f -- "$build_dir/sdkconfig" "$build_dir/sdkconfig.old"
defaults="$repo_dir/firmware/sdkconfig.defaults;$repo_dir/firmware/config/sdkconfig.$profile.defaults"
export IDF_TARGET=esp32s3
"$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" \
    -C "$repo_dir/firmware" -B "$build_dir" \
    -DIDF_TARGET=esp32s3 "-DSDKCONFIG=$build_dir/sdkconfig" "-DSDKCONFIG_DEFAULTS=$defaults" \
    build size

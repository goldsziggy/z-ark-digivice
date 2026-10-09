#!/usr/bin/env bash
# Real temporary POSIX cache file; no physical card, mount or format operations.
set -euo pipefail
script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "$script_dir/run-handheld-demo.sh" --file-cache

#!/bin/sh
set -eu
# Prefer the caller's Node. This Mac already has Node 24 via mise; no installs.
for candidate in "${DIGIVICE_NODE:-node}" "$HOME"/.local/share/mise/installs/node/24.*/bin/node; do
  if "$candidate" -e 'const [a,b]=process.versions.node.split(".").map(Number); process.exit(a>24 || a===24&&b>=12 ? 0 : 1)' >/dev/null 2>&1; then
    exec "$candidate" "$@"
  fi
done
echo 'Node 24.12+ is required. Put it on PATH or set DIGIVICE_NODE to its executable.' >&2
exit 1

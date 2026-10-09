#!/usr/bin/env bash
# Source-only publication checks. Excludes tests requiring omitted scenery bytes.
set -euo pipefail
cd "$(dirname "$0")/.."
printf '%s\n' 'Checking source export; private-scene decoder/cache/service fixture suites are excluded.'
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "${DIGIVICE_BUILD_JOBS:-4}"
ctest --test-dir build --output-on-failure -E background-decode-test
python3 -m unittest discover -s tests -p test_install_sd_usb.py
./scripts/node.sh --test tests/publication-assets.test.ts tests/service*.ts tests/personal-art.test.mjs tests/audio-engine.test.mjs tests/garage*.ts tests/garage-library.test.mjs tests/device-navigation.test.mjs tests/battle-client.test.mjs tests/battle-service.test.ts tests/battle-http.test.ts tests/two-button-input.test.mjs tests/device-assets.test.ts tests/starter-onboarding.test.mjs tests/auto-battle.test.mjs tests/progression.test.mjs tests/world-ds-assets.test.ts tests/roster-client.test.mjs tests/auto-tuning-compat.test.mjs tests/care-actions.test.mjs tests/capture-gesture.test.mjs tests/capture-trajectory.test.mjs tests/care-capture-state.test.mjs tests/capture-ring-model.test.mjs tests/capture-ring-input.test.mjs tests/party.test.mjs

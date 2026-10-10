# Agent instructions: z-ark Digivice

An offline virtual-pet game: a deterministic C++ core (`core/`), ESP32-S3 firmware with a native 412 × 412 touch UI (`firmware/`), a Node/TypeScript service (`service/`), a browser demo (`docs/play/`) and the GitHub Pages showcase (`docs/`). Read the README "Current source" paragraph for the live schema/rules version.

## Git

- `public` is GitHub (`goldsziggy/z-ark-digivice`) and the source of truth. `origin` is an old local mirror: never pull from or push to it. Run `git fetch public` before starting.
- Other agents may be working in this checkout or in sibling worktrees (`git worktree list`). Work on your own branch, preferably in your own worktree, and never reset or switch branches under someone else's uncommitted changes.
- GitHub Pages serves `docs/` from `main`. Anything merged there is published.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 4
npm run test:publication          # published scope; only background-decode-test is expected to be absent
bash firmware/tests/run.sh        # firmware host tests (touch stream, capture, idle, walking, ...)
TAP_AUDIT_REPORT=1 ./build/device-ui-test
DIGIVICE_IDF_PYTHON_ENV="$PWD/../.toolchains/espressif-v5.3.6/python_env/idf5.3_py3.9_env" bash scripts/build-esp.sh waveshare
```

Firmware builds must have 0 warnings. IRAM is full (16,383 / 16,384 bytes): do not add `IRAM_ATTR` code.

## Deploying to the physical Digivices

Follow [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) and use `scripts/deploy-device.py`. Do not write ad-hoc esptool or serial scripts.

- `boards` lists connected boards by MAC (no reset). Address boards by `--mac`, never by port.
- `status --mac M [--watch S]` is read-only and safe at any time. Check `lsof /dev/cu.usbmodem*` first: if another process holds a board's port, someone is using that board. Leave it alone.
- `firmware --mac A --mac B` and `sprites --mac A --dir D --forms X-Y` are dry runs. Add `--execute` only when the user asked for a deploy.
- For any schema/rules bump, run the `saves --nvs … --against …` migration dry run (DEPLOYMENT.md step 5) before flashing.
- Saves are never erased, reset or copied between boards. Never erase flash or write the bootloader, partition table, OTA data or NVS. Never format an SD card.
- If the tool prints `STOP`, report what it said and wait. Do not retry in a loop or work around the check.

## Game rules changes

- Saves migrate forward only. A rules change that alters what an existing history replays to needs a schema/rules bump that freezes the old engine. Follow the rules 19 pattern in [docs/ROSTER_RULES19.md](docs/ROSTER_RULES19.md): `core/legacy_vNN`, decode as `Migrated`, service store-format migration, web constants, and byte-identical replay of old histories.
- UI taps only propose intents. They never write `State`. Every screen must pass the tap audit in `tests/device_ui_test.cpp`: inside the 204 px circle with a 4 px bezel margin, ≥ 36 px per side, ≥ 6 px gaps, and the centre of each enabled target acts. BACK/LEAVE/DONE use the shared `kNav*` spot.

## Never commit

Device MACs, NVS/flash backups, boot logs, saves, Wi-Fi settings, credentials, or sprite/scenery packs (`.personal-assets/` is private). Backups live in `../device-backups`. Showcase and demo art must stay within the scope in [docs/SHOWCASE_SOURCES.md](docs/SHOWCASE_SOURCES.md) and [docs/play/ART_SOURCES.md](docs/play/ART_SOURCES.md).

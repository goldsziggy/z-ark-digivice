# Browser demo native rules bridge

This bridge compiles the unchanged shared C++ rules into a static WebAssembly module. It has no service, credential, filesystem, hardware or network dependency. The browser controller owns only presentation and its separate local demo save. It must not present a browser encounter trigger as real walking, or claim radio/NFC/pedometer support.

The source baseline is `171cda7e698cf2915b50aa46bc766d8d1d0ee50d` (rules 15, schema 22, 60 carried members, three additional XP companions). Supply a matching source tree explicitly during isolated builds. Source provenance must be updated and verified when rebuilding from a newer approved export; changing the build macro alone is not verification.

## Build

Use an already prepared Emscripten SDK; this script never downloads tooling.

```sh
EMXX=/path/to/em++ bash tools/browser-demo/build.sh /path/to/source /path/to/output
# Alternatively set EMSDK=/path/to/prepared/emsdk.
bash tools/browser-demo/build.sh --native /path/to/source /path/to/native-output
# Refresh only the copied SDK notices without recompiling:
EMXX=/path/to/em++ bash tools/browser-demo/build.sh --notices /path/to/source /path/to/output
```

Without arguments the source is the repository containing this script. Browser output defaults to `docs/play/runtime`; `--native` output defaults to the ignored `tools/browser-demo/native-output` directory, outside the website. `DEMO_SOURCE_COMMIT` accepts a full lowercase Git SHA. `CXX` can select a native C++17 compiler. Publish `demo-core.js`, `demo-core.wasm` and `NOTICES.txt`; do not publish the native test executable. Set `EMSCRIPTEN_ROOT` to the SDK's Emscripten directory if `EMXX` is a wrapper outside that directory. The memory configuration starts at 16 MiB, grows only when needed and is capped at 64 MiB; it is not a measured download size.

The publication toolchain is pinned to **Emscripten 6.0.12** (compiler revision prefix `5488e087`), official `emscripten-core/emsdk` revision `35ff8a6d150541276abbc6bae512ca90bcfbe220`, and upstream release revision `6e9f6885cb17a3d6c8fd488a5cff730f50aba61d`. Compiler flags, source translation units and exports are explicit in `build.sh`; no source patches or downloaded asset packs are part of the build. Native and browser outputs should be checked against the same source content and recorded SHA. The SDK is a build prerequisite and is not distributed with the sample.

## JavaScript ABI

```js
import createDemoCore from './runtime/demo-core.js';
const core = await createDemoCore();
const call = (name, types = [], values = []) => core.ccall(name, 'string', types, values);
const state = JSON.parse(call('demo_reset', ['number'], [12345]));
const hatched = JSON.parse(call('demo_command', ['string', 'number'], ['hatch', 1]));
const encounter = JSON.parse(call('demo_command', ['string', 'number'], ['demo-encounter', 0]));
const snapshotHex = call('demo_snapshot');
const restored = JSON.parse(call('demo_load', ['string'], [snapshotHex]));
```

All returned strings must be consumed before the next ABI call; buffers are module-owned. `demo_state()`, `demo_reset(seed)`, `demo_command(command,value)` and `demo_load(snapshotHex)` return `{ok,error,state,trace,sourceCommit,rulesVersion,schemaVersion,demoSteps}`. The `state` is the native `writeJson` result, including care, collection, battle information, capture chance/result, party IDs, walking progress and evolution eligibility. `trace` is a native Auto battle trace for a successful `auto-fight`/`auto-resume`, otherwise `null`. Failed commands return the unchanged state and a readable error.

The controller must pass bounded integer values, never negative/fractional/NaN values: the C ABI receives unsigned 32-bit integers. Commands accepted here are `hatch` (fixed slots 1–8), `feed`, `play`, `rest`, `card`, `attack`/`physical`, `heavy`, `magic`, `mode` (0 Tactical / 1 Auto), `select`, `evolve`, `release`, `ring-capture` (phase 0–2399), `party-add`, `party-remove`, `auto-fight`, and `auto-resume`. Parameter validity and phase restrictions are decided by the native core. Old direct/flick capture and private setup/seed commands are not exposed.

`demo-encounter` is the only sample convenience command. At Home, it advances real native `Explore` actions until the next actual encounter, with a hard budget of 1,000 synthetic steps. It first lets the native core initialize an unknown target, then uses native `encounterStepsRemaining` to stop at the encounter boundary. It commits the candidate only on success and returns the exact synthetic step count as `demoSteps`. It never selects a foe directly, changes probabilities, awards free XP, or injects a favorable random draw. The UI must label those steps as simulated. Reset accepts a documented sample seed, so new demo games can be reproducible; no production RNG behavior is changed.

`demo_snapshot()` returns 5,928 hex characters representing the canonical 2,964-byte snapshot with its native CRC. Store it under a demo-only versioned key/envelope. `demo_load(hex)` accepts only a current-length, valid native snapshot with the current schema/rules; invalid encoding, CRC or state leaves the current game untouched. The UI must keep unknown/corrupt stored data available for recovery or explicit reset, use an in-memory fallback if storage is unavailable, and handle multiple-tab ownership. This is a browser demo API, not a physical-device save import interface.

Metadata exports return the existing native JSON directly: `demo_starters()` (eight fixed starter slots), `demo_form(formId)`, `demo_catalog(offset,limit)`, and `demo_evolutions(formId,offset,limit)`. Page limits are 1–16. Invalid requests return `{ok:false,error:...}`. These functions do not fetch or embed artwork. Original form IDs 1–10 are decode-only fixtures and are excluded from native production catalog pages.

## Native parity harness

The native executable accepts one command per input line and emits one result per line. Use `reset SEED`, `state`, `snapshot`, `load HEX`, `starters`, `form ID`, `catalog OFFSET LIMIT`, `evolutions FORM OFFSET LIMIT`, or a gameplay command and optional value. Native and WASM builds use the same adapter and rule sources, enabling exact state/snapshot parity checks for identical action streams. Capture success is still probabilistic; a favorable grade is not a guaranteed catch.

```sh
printf 'reset 12345\nhatch 1\ndemo-encounter\nattack\nstate\n' | /path/to/native-output/digivice-demo-native
```

Meaningful release checks include matching native/WASM state and snapshot bytes; graded captures and Auto pause/resume; collection/party persistence; snapshot CRC rejection without mutation; current metadata; touch/keyboard repeat handling; reload/reset isolation; and actual static-host asset loading.

From the repository root, after building the browser runtime:

```sh
bash tools/browser-demo/build.sh --native "$PWD" "$PWD/tools/browser-demo/native-output"
node tools/browser-demo/test-parity.mjs
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target digivice-core capture-ring-model-test -j 2
node tools/browser-demo/test-shared.mjs
node tools/browser-demo/test-static.mjs
node tools/browser-demo/ui-save-regression.mjs
```

The parity test accepts `--native BINARY`, `--module JS`, `--source ROOT`, and optional `--report JSON` overrides. The static test accepts an optional report path as its first argument. Test reports and native binaries are review evidence, not runtime files. The shared test exercises byte-identical shipped input, ring, party and starter helpers with their existing product tests. The UI save regression executes the real controller and WASM with a minimal browser fixture to check stale queued input, corrupt-save preservation and modal capture disarming. None of these suites touches connected devices.

Native smoke verification on this baseline covered a fresh egg, Impmon hatch, ordinary care actions, an encounter after 55 synthetic steps, native magic attacks, a successful graded capture of Kumamon with a 60% native chance, canonical save reload, bad-CRC rejection without state mutation, and a seven-step Auto battle trace pausing for manual capture. This is software verification, not physical-device acceptance. Reproduce the care/capture path from seed 12345 with `hatch 1`, ten ordinary `feed`/`play` pairs, `demo-encounter`, `magic` until capture becomes eligible, then a green-ring phase. The deterministic sample outcome does not make future green throws guaranteed.

## Runtime notices

Emscripten's emitted runtime is available under MIT or University of Illinois/NCSA terms. Its linked musl C library uses MIT terms; LLVM compiler support and C++ libraries use their respective Apache 2.0 with LLVM exceptions notices. Preserve the build's `runtime/NOTICES.txt` alongside the browser files. The notices retain the full license text with trailing whitespace normalized, copied from the pinned SDK's `LICENSE`, `system/lib/libc/musl/COPYRIGHT`, and linked LLVM library license files. They apply to those components only and do not grant rights to third-party Digimon artwork or establish a project-wide license for game source.


## In-game artwork for the public demo

The browser presentation now uses the authorized current runtime art, with scoped [credits and provenance](../../docs/play/ART_SOURCES.md). It includes only deduplicated native idle frames and the eight device scene JPEGs. The normal service and device download catalogs remain unchanged.

To reproduce the compact atlases from an authorized local source checkout with its separately supplied audited artwork and staged SD index:

```sh
python3 tools/browser-demo/build-art.py --source /path/to/authorized/game-source
node tools/browser-demo/test-art.mjs
node tools/browser-demo/test-art-integration.mjs
node tools/browser-demo/ui-save-regression.mjs
node tools/browser-demo/test-static.mjs
```

The extractor reads but never modifies that source. It verifies DVA hashes, CRCs, staged-file identity, native facing and the installed `171cda7` scene blobs. It performs no download or hardware access. Original sheets and import packs are not outputs; the existing sanitized attribution files must remain beside generated assets.

Fresh/reset browser adventures apply native Auto mode immediately after hatch, because the native egg snapshot has a fixed Tactical invariant. Existing saved mode selections are restored unchanged. The browser's default does not alter the firmware or WASM rules. Auto still waits for a fresh manual capture input.

## Round-screen touch preview

`docs/play/device-touch-input.js` adapts Pointer Events to the installed `firmware/runtime/device_ui.cpp` gesture contract. Register it before the unchanged shared capture helper so a screen contact has one owner. The shared helper remains responsible for the optional HTML capture button and keyboard D; its `canArm` callback excludes active screen contacts. `device-view.js` maps a bounded set of native-layout screens to ordinary WASM commands, retaining review intent and selected member identity. It is browser presentation, not a second implementation of game rules.

Run `node tools/browser-demo/test-device-touch-input.mjs` and `node tools/browser-demo/test-device-view.mjs`, plus the existing controller/save/static checks. The native input baseline is 171cda7; coordinate forthcoming larger Back targets and gameplay changes with the firmware owner before changing that baseline.

## Battle presentation

`docs/play/battle-playback.js` expands a successful native Auto trace into player/opponent actions, or derives a Manual exchange from its before/after native states. It owns no game rules or random draws. The controller saves the native outcome immediately, then renders a disposable 1.1-second-per-actor queue before its normal device view. Slow frames preserve each action and impact instead of jumping to the final snapshot. The browser animates the shipped idle sprites with transforms; no attack sheet or fallback form is invented.

Completion, skip, navigation, visibility/page exit, reset and cross-tab invalidation settle once without native calls. A pending screen contact is canceled before gameplay returns. A hidden page never opens capture. Terminal combat HP comes from the trace, keeping later XP growth/recovery separate; Manual retreat's unavailable wild HP is explicitly unknown.

Run `node --test tools/browser-demo/test-battle-playback.mjs tools/browser-demo/test-battle-controller.mjs` for real-WASM sequence, reflection, capture boundary, final reward, slow-frame, reduced-motion and save-lifecycle coverage. Existing controller/save/art/touch/static checks remain required. The C++ source, WASM, rules version and save schema are unchanged by this presentation update.

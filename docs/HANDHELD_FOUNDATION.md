# Handheld foundation

The current hardware target is the **Waveshare ESP32-S3-Touch-LCD-1.46, standard glass SKU29565: 412×412, 16 MB flash, 8 MB PSRAM**. The owned Heltec V4 is a separate interim profile with 2 MB PSRAM and an unverified board revision. [Verified board facts and open wiring gates](PARK_HARDWARE.md).

The portable game, save journal, motion-counter policy, reconnect policy and asset cache run on this Mac. ESP-IDF 5.3.6 is now installed and **all three profiles cross-compile**; none has run on a board. Physical display, JPEG decoding, SD hardware behavior, external buttons, NFC and pedometer configuration remain unverified. The QMI reader requires verified sensor identity/revision and externally verified configuration; it does not configure the hardware pedometer itself.

Wild battles now support confirmed Tactical/Auto selection through the serial interface. Auto resolves in the native core and checkpoints once; round physical mode controls remain future work. The shared practice engine now has a separate durable ESP serial session; [run its host proof](ESP_PRACTICE.md). [Mode commands and recovery](BATTLE_MODES.md).

## Run the actual host slice

```sh
cd .
npm run demo:handheld
npm run test:handheld
```

The demo uses the existing C++17 compiler and Python standard library. It installs nothing and opens no sockets, radios or serial devices. Its underlying command is `bash scripts/run-handheld-demo.sh`. The full browser game remains available with `npm run dev:direct` at `http://127.0.0.1:8787`.

The deterministic demo runs the real portable C++ components with fake sensor counts, monotonic clock, NVS/NOR storage, hotspot events and a file-backed range transport:

1. Decode the resident Mote fallback; collect 100 simulated steps and trigger a real Flicker encounter.
2. Simulate hotspot connection, failed service health, bounded retry and recovery.
3. Download 4,096 bytes of the real 8,816-byte Flicker sprite, disconnect, recreate runtime objects, then resume from the saved offset. Verify SHA-256 before activation.
4. Pause networking, apply a simulated card, attack three times, capture and restore the saved result.

Verified result: **sequence 6, 100 steps, two collection members, partner HP 58**, with no network requests during battle/capture. This proves local rules and recovery through the fake adapters. The launcher checks the local asset index hash; this demo does **not** verify a signed catalog, exercise TLS or prove electrical power-loss behavior. Motion checkpoint plus acknowledgement is not one cross-reset atomic transaction, so it makes no power-loss exactly-once step claim.

## Content and flash

The compact device library is separate from the 480px browser packs. It contains ten original 32px DVA1 creature files and eight **412px baseline JPEG** backgrounds. The decoder consumes bounded sprite frames instead of expanding the whole library. [Format and reproducible build](../assets/device/FORMAT.md), [measured budget](../assets/device/budget.json).

| Measured encoded content | Bytes |
| --- | ---: |
| Ten creature files | 88,160 |
| Eight backgrounds | 404,545 |
| Complete downloadable library | **492,705** |
| Resident 16px Mote + Flicker fallback, compiled read-only | **2,528** |
| Largest individual download | 63,510 |

The flash cache reserves **696,320 bytes (680 KiB)**: five 128 KiB payload slots plus **40 KiB of metadata**. It retains four selected assets and a replacement slot, rather than all 18 library files at once. Active partner, opponent, scene and optional next opponent can be protected from eviction. Interrupted data remains inactive; hash verification precedes activation, and boot recovery validates retained records. Transfers use **4 KiB** chunks and reject stale transfer completions.

The Waveshare profile now uses **microSD as primary asset storage**: one 696,832-byte file (512-byte ownership envelope plus the bounded cache). Resident fallback art lives in the app image, separately from downloads and NVS saves. The old 9 MiB internal partition reservation remains unchanged for compatibility. No automatic format, hot-remount or mid-transfer storage substitution occurs. [SD behavior and recovery](SD_ASSETS.md).

Encoded storage is **not PSRAM usage**. Do not add the whole 492,705-byte library to every frame's decoded memory budget.

## Working-memory targets

| Calculated allocation | Bytes | Status |
| --- | ---: | --- |
| One full 412² RGB565 frame or decoded scene | 339,488 | Optional; avoid silently allocating both |
| Two 412×48 RGB565 draw strips | 79,104 | Proposed display path; DMA/byte order unverified |
| Two 32² RGB565 sprite frame buffers | 4,096 | One frame per actor, or double buffering one actor |
| Double buffering both 32² actors | 8,192 | Only if the renderer needs it |
| Two resident 16² RGB565 actor frames | 1,024 | Low-memory fallback target |
| Transfer chunk | 4,096 | Portable cache bound |

JPEG scratch, transparency bookkeeping, GUI state, driver queues, TLS and Wi-Fi allocations are additional. The 2 MB Heltec profile should start with resident sprites and no decoded full-screen background; its screen is not the Waveshare display.

The reusable DVA reader streams validation through a **128-byte read chunk**. A decoded 32px frame needs **2,048 bytes** plus an optional **128-byte opacity mask**; its host descriptor is **112 bytes**. These figures exclude total call-stack usage. Panel transfer byte order remains a display-driver responsibility.

The host ABI measured `State` **384 B**, `SaveStore` **32 B**, `MotionCounter` **96 B**, network controller **40 B**, and cache object **768 B**. The demo's 696,320-byte fake flash array represents nonvolatile storage; it is not the intended ESP working RAM. Network configuration is **292 B**, serialized NVS record **304 B**, and its health worker is configured with a **6,144-byte stack**. The opt-in asset worker adds a **12,288-byte stack**, fixed catalog/envelope/chunk storage, and parser/crypto/TLS allocations that still need target measurement. Physical heap headroom, FPS, sleep current and battery runtime remain unmeasured.

## Network and motion boundaries

A **2.4 GHz WPA2-compatible phone hotspot** can provide internet access; reconnect behavior is independent of local battles. “Online” means the configured service passed its health check, not merely that Wi-Fi obtained an IP. The source uses bounded retries and queues. Credentials stay in local NVS and are not logged or sent to the game service, but this prototype has **no NVS encryption**.

HTTPS verifies certificates and requires a valid clock. Automatic trusted time setup is not yet implemented, so a board cannot be called service-ready merely because it joined a hotspot. Private HTTP requires both build-time and runtime opt-in and accepts only literal private IPv4 origins. It defaults off. The public RFC asset-signing fixture also defaults off in firmware and provides **no production publisher authenticity**. A real release needs an owner-controlled signer and matching firmware trust pin.

The SDK HTTP parser is configured for a **4 KiB header limit**. Application callbacks alone cannot bound the SDK's earlier allocations. Socket timeouts remain per operation: a slow peer can keep the single worker occupied beyond the nominal request deadline. This cap does not establish a hard cancellation deadline or a total TLS/HTTP heap budget. Header interruption and reconnect heap tests remain a hardware gate. Source audit (historical local evidence omitted)

The motion policy consumes a verified cumulative 24-bit counter, handles batches/wrap, and rebases after resets or stale gaps. Host fixtures exercise that policy; they do not establish real carrying accuracy. The shared I2C reader remains gated and cannot enable motion by simply recognizing a QMI family ID. Walking, shaking, sleep/wake and sensor-reset behavior need bench validation.

No GPS app, location sharing or nearby-player PvP is implemented by this slice. The user is still comparing optional location features with step-based encounters; no final GPS decision is implied. BLE discovery with mutual challenge confirmation is a later option independent of GPS.

## Verification and the next gate

Focused host checks pass: **1,727 cache, 750 network, 693 motion, 301 sprite-reader, 360 prefetch and 252 file-storage checks**, plus save-policy and core suites. The complete service/browser-module regression reports **132 passing tests**. Both the NOR harness and the real temporary-file SD harness pass; these do not establish physical SD durability.

Official **ESP-IDF 5.3.6** is installed in the task-local `.toolchains` directory. Generic serial, Waveshare SD and Waveshare development-assets profiles all cross-compile for ESP32-S3. App binaries measure **1,063,232 / 1,071,984 / 1,083,152 bytes**, respectively, against 3 MiB app slots. [Exact commands, tool identities and static-memory evidence](ESP_BUILD.md).

The pinned vendor example uses IDF 5.3.2; the project builds with official 5.3.6. This SDK choice includes interrupted-header cleanup absent from the older reference. The source audit also found an SDK `pwrite` error path that can retain a lock on a full FAT filesystem; our exclusive-file adapter uses `lseek` plus `write`, without modifying the SDK. HTTP source review (historical local evidence omitted), SD source review (historical local evidence omitted).

Compilation is complete, but no device was flashed or opened. The next physical gates are board/revision confirmation, power hold and save recovery, SD absence/full/removal behavior, display, and motion. Runtime heap, reconnect stability, SD power-loss durability, FPS and battery current remain unmeasured. [Firmware guide](FIRMWARE.md), [hardware gates](PARK_HARDWARE.md).

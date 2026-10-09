# ESP device readiness

> Publication checkpoint: source and previously installed firmware are f74ee4c (schema 17 / rules 13). Both units passed bounded installation/save/SD/reboot checks; older entries below are historical. No hardware was accessed for this export. See [verified images and remaining acceptance](../releases/firmware-f74ee4c/README.md).

**Two-device status:** Unit 1 runs **`8a420c7`**. Its app-only upgrade, all **261 destination SHA-256 checks**, eight starter DVA decodes, scene JPEG decode and display-transfer counters passed. Its own **576-byte saved egg remained unchanged and survived software reboot**; no hatch was performed. Unit 2 remains on **`90ee501`**, currently absent from both USB and serial enumeration after the user confirmed reconnection; a check with Unit 1’s working cable and Mac port is pending. Actual visible-screen, finger, sound, battery and hotspot acceptance remain pending. [Per-device status](TWO_DEVICE_PREPARATION.md) · Current evidence (historical local evidence omitted).

The current ESP-IDF **5.3.6** Waveshare build uses **1,449,936 app bytes** and **145,419 static DIRAM bytes**, with **1,695,792 bytes free** in the unchanged 3 MiB app slot. These are linker/artifact measurements, not runtime heap or battery estimates. Current build pins (historical local evidence omitted).

The private pack contains **261 files / 4,700,573 bytes**, covering **251 of 276 forms plus eight scenes**; the other **25 forms retain fallback art**. [USB-to-SD installation](USB_SD_TRANSFER.md) uses the card inside each explicitly identified board and does not require a Mac card reader. Unit 1's exFAT mount passed; unit 2's earlier inventory remains inconclusive. No formatting occurred. Unit 1’s completed transfer took 733.237 seconds at 6,410.714 acknowledged B/s with zero retries; all hashes passed again on the current firmware.

[Touch playtest](TOUCH_READINESS.md#physical-touch-playtest-path) · [On-device setup](DEVICE_SETUP.md) · [Installation/recovery](INSTALLATION.md)

| Area | Implemented or measured | Remaining verification or scope |
|---|---|---|
| LCD / touch | SPD2010 initialization/transfers, circular RGB565 renderer; 148 touch samples / zero errors on unit 2 `90ee501` | Orientation, colors, finger coordinates and target comfort |
| Local game | Eight-starter review/hatch, care, simulated encounters, Tactical/Auto wild battles, flick capture and partners | Physical interaction through the whole loop; additional browser/USB features are outside this screen set |
| Saves | Existing schema 13/rules 10, checkpoint-before-publish; unit 1’s own saved egg/reboot unchanged on `8a420c7`; earlier unit 2 own-snapshot reboot passed on `90ee501` | Controlled power-loss recovery |
| Sound | Original I²S cues, default 15% volume, mute, initialization and cue request verified | Actual fitted-speaker audibility/level |
| Motion | Valid raw QMI readings; optional cosmetic tilt | Unit 2 calibration incomplete; visible axes/neutral pose; pedometer disabled |
| SD art | Bounded exact-form DVA frames and allowlisted JPEG scenery; original fallback retained; unit 1 exFAT mount passed | Unit 1 pack hashes and eight starter/scenery decodes passed; unit 2 upgrade/copy pending; human visual acceptance pending |
| Wi-Fi / service | On-device masked keyboard, scan/reconnect/forget, Wi-Fi-only config, bounded SNTP and HTTPS clock gating | Actual hotspot connection, clock bootstrap, TLS and service reachability |
| Power | Existing PWR handling plus display/audio/IMU/setup suspension barriers | Battery/latch/current and deep sleep |
| NFC / GPS / external buttons | No NFC reader fitted; both builds default disabled, unit 2 reports NFC unavailable | Optional future modules; no requirement for local play |

Current transfer checks pass **277 runtime, 27 USB installer, 318 FatFs 8,663 transfer-engine and 75 I²C timeout checks**. Runtime tests use device I/O doubles, and their delayed-barrier cases do not directly exercise the physical audio worker. The combined host suites retain **532 UI/artwork, 2,382 setup-keyboard, 757 network-policy, 57 adapter and 172 audio/IMU checks**. Renderer optimization preserved pixels across 896 frame and 1,192 primitive comparisons; sanitizer checks pass. The clean combined build uses the existing ESP-IDF 5.3.6 standard Waveshare profile. Current artifact sizes/hashes belong to the linked build record; earlier build values below remain historical.

The earlier unit 2 `90ee501` checkpoint measured **140,989 µs maximum frame cost**, **148 touch samples / zero errors**, and valid IMU readings with **calibrated=0**. A **6.95°/s** gyro sample exceeded the **5°/s** stillness gate; actual stillness was not observed, so this is not a confirmed sensor defect. Screen, finger-input and audible acceptance remain pending. Per-device evidence (historical local evidence omitted).

Unit 1’s earlier `59bf076` checkpoint below is retained separately; its calibration completion and heaps do not describe unit 2. Historical physical evidence (historical local evidence omitted).

| Earlier unit 1 checkpoint at `59bf076` | Observed value |
|---|---:|
| Maximum synchronous frame cost | 75,653 µs |
| Maximum render / flush cost | 29,956 / 45,691 µs |
| Internal free / largest block | 206,143 / 110,592 bytes |
| PSRAM free / largest block | 8,044,504 / 7,995,392 bytes |
| Framebuffer | 339,488 bytes |
| Touch samples / errors at recorded checkpoint | 144 / 0 |

These are sampled diagnostics, not sustained FPS, touch latency, lifetime minimum heaps or battery estimates. The combined SD/network workload needs fresh measurements. The existing 13,184-byte internal DMA stripe is joined by at most one 339,488-byte decoded background and 104,448 bytes of decoded sprite frames in PSRAM when art is available; decoder/input and SDK allocations add overhead. These latter bounds describe requested resources, not measured combined free memory.

Save formats, NVS boundaries, partition table and app offsets remain unchanged. Network settings are separate from game saves; the new 304-byte `DNET` v2 Wi-Fi-only record preserves legacy v1 records with endpoints. Compatible deployment remains an app-only update after identity/layout checks. Each unit’s full-flash backup stays private. No card format or whole-chip erase is part of this work.

## Historical 6 October build milestone

The following section records an earlier schema-6 source/build audit. Its sizes, missing drivers and no-flash statements apply only to that milestone; the current implementation and verification boundary are above. Historical evidence files have not been rewritten.

**6 October 2026: current schema-6 Tactical/Auto source passes real ESP32-S3 cross-compilation for all three profiles with zero warnings. Host checks also pass. No device has been flashed or physically tested.** Current board target: Waveshare ESP32-S3-Touch-LCD-1.46 standard glass, 412 × 412, 16 MiB flash and 8 MiB PSRAM. Current build evidence and hashes (historical local evidence omitted), [build commands](ESP_BUILD.md), [official hardware sources](PARK_HARDWARE.md).

| Area | Implemented and verified | Remaining physical work |
|---|---|---|
| Game and collection | Allocation-free shared C++ care, encounters, cards, combat, capture and member selection; host checks and current target compilation | Connect verified input and display drivers |
| First-run onboarding | Eight-entry selection controller, direct Rookie hatch and versioned core state; see [onboarding](ONBOARDING.md) | Physical screen and button presentation |
| Wild battle modes | Confirmed Tactical/Auto selection at Home; explicit whole-Auto command; shared rules and bounded result summary | Physical menu/button presentation; see [battle modes](BATTLE_MODES.md) |
| Saves | Dual NVS records; schema-6, 428-byte snapshots; CRC/version validation, candidate-before-commit, readback and recovery policy | Actual NVS and controlled power-loss checks |
| Primary assets | Bounded 696,832-byte microSD cache file, hash/journal validation, verified SD pin map, no autoformat, built-in fallback | Absence/full/removal/brownout tests with real cards |
| Network | Bounded controller, credential config, health worker and opt-in asset download client | Clock/TLS, reconnect heap, actual radio behavior; durable gameplay event outbox remains future work |
| Motion | Cumulative-counter policy and gated shared-I2C adapter | Exact QMI configuration, carrying accuracy and sleep counting |
| Display/touch | Vendor source/pin audit and resource calculations | SPD2010/LVGL driver integration; no physical rendering yet |
| NFC / GPIO buttons | Simulated events and portable navigation | Exact wiring, debounce, reader and button drivers |
| Practice battle | Shared Tactical/Auto engine linked into ESP, separate NVS session, serial commands and saved replay; [host proof](ESP_PRACTICE.md) | Physical LCD/button presentation, NVS fault and stack measurements |
| Power / GPS / BLE | Power-hold source only; no location upload | Battery/charging/current tests; GPS and nearby-player scope undecided |

The generic profile touches no board GPIOs. Waveshare profiles enable the source-verified SD mapping; optional inputs remain gated. The development profile additionally compiles public fixture trust and private-HTTP opt-ins. Those settings provide no production authenticity and contain no device credentials.

Serial wild mode selection is `mode tactical` or `mode auto`, then `mode confirm`; `mode cancel` discards the pending choice and `mode status` reports it. A changed save sequence invalidates confirmation. After walking starts an Auto encounter, the explicit `auto` command resolves the whole fight and checkpoints its terminal state once before acknowledgment. An uncertain write blocks further play until recovery; restoring a committed result cannot repeat its rewards. Old snapshots migrate to Tactical without resetting the creature. [Console details](FIRMWARE.md) and focused persistence checks (historical local evidence omitted).

Current host ABI measurements are 404 bytes for game State, 428 bytes for its snapshot, 440 bytes for a save slot and 8 bytes for the mode-choice controller. Firmware uses the compact result summary and does not allocate the optional replay trace or its host-only 16 KiB JSON buffer. These measurements do not replace current target maps or runtime heap measurements.

Current target application binaries are 1,071,504 B (GenericSerial), 1,080,288 B (Waveshare) and 1,091,424 B (Waveshare development). Each fits its 3 MiB app slot with over 2 MB remaining. [The firmware resource table](FIRMWARE.md#durability-and-resources) gives exact slot space and static DIRAM use; the build record (historical local evidence omitted) preserves the underlying measurements.

PSRAM is disabled in current build configurations. Static map remainder is not runtime free heap. The future 412² RGB565 full frame would require 339,488 bytes; two proposed 412 × 48 strips require 79,104 bytes. Those are calculations, not allocated or measured display buffers. Actual radio, TLS, FAT, decode, task and DMA memory must be measured together on the board.

No serial port was opened, no card was formatted, no physical device was flashed, and no deployment occurred. Build commands use `npm run build:esp`, `npm run build:esp:waveshare` and `npm run build:esp:waveshare-dev`. Host storage demonstration: `npm run demo:sd`.

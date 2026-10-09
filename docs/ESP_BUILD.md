**Current source package:** `171cda7`, schema 22/rules 15. Read the [current package instructions](../releases/firmware-171cda7/README.md) and [publication validation](../PUBLICATION_VALIDATION.json). Retained checkpoints below are historical.

# Actual ESP32-S3 builds

> Current exported source: `66ceaaf` (schema21/rules14, 60 Digimon). [Current build and migration guide](ROSTER60.md) · [Publication validation](../PUBLICATION_VALIDATION.json). Installation statements below describe their named historical checkpoints; this export preparation does not assert a new physical installation.

The [capture-ring/world-seed source update](CAPTURE_RING_WORLD_SEED.md) is prepared separately and has not been flashed. Its compiler evidence is kept with that release.

## Current release and installed checkpoint

Both playtest devices are installed on **`8be26c6`** following save-preserving
verification. See the [current build and resource guide](LANYARD_NEARBY_AUDIO_RELEASE.md)
and sanitized physical evidence (historical local evidence omitted).
This release adds automatic sensor recovery, more forgiving software step
detection, explicit Nearby Tactical/Auto selection and 100% / 44,100 Hz audio.
Exact image, own-save, settings, SD and reboot checks passed; physical walking
accuracy, audible output and RF acceptance remain open. The previous
[f9 build and resource guide](SOUND_TRADE_AUTO_RELEASE.md) remains historical.

## Historical 8a420c7 target and per-device verification

The following build and device status are retained from that earlier checkpoint;
its version labels, USB availability and measurements are not current status.

Clean source **`8a420c765ce4be418e10be4bb28266b8d9a33608`** builds the standard Waveshare 1.46 profile with ESP-IDF **5.3.6** and zero compiler warnings. The app is **1,449,936 bytes**, leaving **1,695,792 bytes** in the unchanged 3 MiB slot; static DIRAM is **145,419 bytes**. This build includes USB-to-SD transfer, FAT/FAT32/exFAT support and a bounded wait for the audio worker to acknowledge the installation pause before SD access. It also backports the official bounded synchronous I²C NACK wait into a pinned project-local component; the installed SDK remains unchanged. Current source/artifact pins (historical local evidence omitted).

**Two-device status:** Unit 1 runs **`8a420c7`**. Its app-only upgrade, all **261 destination SHA-256 checks**, eight starter DVA decodes, scene JPEG decode and display-transfer counters passed. Its own **576-byte saved egg remained unchanged and survived software reboot**; no hatch was performed. Unit 2 remains on **`90ee501`**, currently absent from both USB and serial enumeration after the user confirmed reconnection; a check with Unit 1’s working cable and Mac port is pending. Actual visible-screen, finger, sound, battery and hotspot acceptance remain pending. [Per-device status](TWO_DEVICE_PREPARATION.md) · Current evidence (historical local evidence omitted).

The private pack contains **261 files / 4,700,573 bytes**, covering **251 of 276 forms plus eight scenes**; the other **25 forms retain fallback art**. The [USB installer](USB_SD_TRANSFER.md) copies through each identified board to its installed card, so this route needs no Mac card reader. Unit 1's successful exFAT mount supersedes its earlier mount failure; unit 2's earlier inventory remains inconclusive. No formatting occurred. Unit 1 transferred 4,700,573 payload bytes in 733.237 seconds (6,410.714 B/s), with zero retries; all hashes were checked again after the I²C-fix upgrade. These are actual USB/card results, separate from human visual acceptance.

Current focused host checks pass **277 runtime, 27 USB installer, 318 FatFs 8,663 transfer-engine and 75 I²C timeout checks**. The earlier suites retain **532 UI/artwork, 2,382 setup-keyboard, 757 network-policy, 57 adapter and 172 audio/IMU checks**. Runtime tests use device I/O doubles; the delayed acquisition cases do not directly exercise the physical audio worker. [Transfer contracts and verification limits](USB_SD_TRANSFER.md).

Private SD artwork is enabled. The project-local FatFs override retains the pinned SDK implementation, enables exFAT with heap long filenames (128 UTF-16 units) and UTF-8, rejects oversized file lengths and disables TRIM. Game/save schemas and partition layout are unchanged. Static linker use is not measured runtime free heap. Actual finger input, visual/audio acceptance, battery behavior and hotspot/TLS acceptance remain unverified. Network setup also needs a reviewed reachable service origin. [On-device setup](DEVICE_SETUP.md).

The build and device statements in the sections below are historical snapshots; their artifacts and measurements have not been rewritten to describe the current installation.

## Earlier unit 1 combined build: 569643d

Unit 1’s installed `569643d` app is **1,435,680 bytes**, with **143,699 bytes** static DIRAM. Its app-only flash/hash verification passed, but the recurring USB reset stall blocks its postflash checks pending cable reconnection. No new unit 1 flash or reset was attempted during unit 2 preparation. Archived build (historical local evidence omitted) · Unit 1 flash/USB evidence (historical local evidence omitted).

Unit 1’s earlier `59bf076` running checkpoint had completed IMU calibration and exact saved-egg/snapshot reboot recovery, with maximum **75,653 µs frame**, **29,956 µs render**, **45,691 µs flush**, and sampled free **206,143 internal / 8,044,504 PSRAM bytes**. Those measurements remain specific to that unit/build/workload. Historical physical checkpoint (historical local evidence omitted) · Its build (historical local evidence omitted).

## Initial physical playtest build: 0c158d3

Clean source **`0c158d3581eb464f4e0e409d0f91ab4f0defb73c`** builds the exact **Waveshare ESP32-S3-Touch-LCD-1.46 standard glass SKU29565** profile with ESP-IDF **5.3.6**, zero compiler warnings and embedded version `0c158d3`. This adds compiled SPD2010 display/touch, a circular native game UI, original speaker cues and raw IMU cosmetic tilt. At this initial build checkpoint, deployment and peripheral acceptance were pending; subsequent installation and timing results are recorded above. Initial source/artifact pins (historical local evidence omitted).

| Measure | Build/host value |
|---|---:|
| App binary | 1,391,072 bytes |
| Remaining 3 MiB app slot | 1,754,656 bytes |
| Static DIRAM | 141,891 bytes |
| Host UI controller | 376 bytes |
| Requested PSRAM RGB565 frame | 339,488 bytes |
| Requested internal DMA stripe | 13,184 bytes |
| Physical UI host checks | 403 passed |
| Audio/IMU host checks | 172 passed |

**Historical initial bring-up behavior:** the motion and audio statements in this
paragraph are superseded by the [current release guide](LANYARD_NEARBY_AUDIO_RELEASE.md).
Octal 80 MHz PSRAM is enabled for this standard profile. Only disposable pixels are explicitly placed there; core saves remain in NVS. The display QSPI clock is 40 MHz, audio synthesis 22,050 Hz with default 15% software volume, and physical pedometer support stays disabled. Raw IMU availability enables optional cosmetic tilt, not walking encounters. Save schema/rules, the partition table, NVS and app offsets are unchanged. Static linker use and allocation requests are not measured runtime heap; the near-12 Hz scheduling cap is not observed FPS or input latency. Audio task/queue/DMA consumption is estimated at approximately 8 KiB plus SDK overhead.

The build command is `bash scripts/build-esp.sh waveshare`; it does not flash. Only this exact standard profile has a recorded cross-build for the physical playtest source. Older GenericSerial and development profile results below retain their earlier source scope. [Touch acceptance path](TOUCH_READINESS.md#physical-touch-playtest-path) · [Install/package checks](INSTALLATION.md).

## Earlier USB console repair build

The **USB console initialization repair**, clean source commit `006488eef3c3309e3d305c4671f009be47442ba2`, compiles with ESP-IDF **5.3.6**, zero compiler warnings, and embedded version `006488e`. The app is **1,276,496 bytes**, leaving **1,869,232 bytes** in its 3 MiB slot; static DIRAM is **119,915 bytes**. Compared with the prior build this adds 8,944 app bytes and 4,456 static DIRAM bytes. Driver rings request another 512 RX + 1,024 TX bytes plus unmeasured allocation overhead. Exact source/build pins (historical local evidence omitted). App-only flashing, USB command replies and identical game-state/snapshot recovery across a software reboot are verified. A prior USB-reset communication stall recovered after the user reconnected the USB cable. Physical evidence (historical local evidence omitted).

Earlier milestones below retain their original build-time scope.

The **flick-capture shared-core update** compiles and links in the Waveshare + microSD profile with the existing ESP-IDF 5.3.6 toolchain and zero compiler warnings. The actual app binary is **1,267,552 bytes**, leaving **1,878,176 bytes** in each 3 MiB app slot; static DIRAM remains **115,459 bytes**. This adds 144 app bytes over the power-control build below and no saved state bytes. Exact source/artifact hashes and measured accounting (historical local evidence omitted). The native action compiles, but physical display, touchscreen gesture and audio drivers remain absent. No board was flashed; other profiles and the existing installer package were not rebuilt for this change.

## Previous power-control milestone

The **onboard power-control** milestone builds in all three profiles with official ESP-IDF 5.3.6 and zero warnings. Source hashes, artifact hashes and measured sizes (historical local evidence omitted) identify the compiled source; [power controls and remaining physical checks](POWER_CONTROL.md) explain its behavior. Care rules, practice rules, save formats and partitions are unchanged.

| Power-control profile | App binary bytes | Static DIRAM bytes | Remaining 3 MiB app slot |
| --- | ---: | ---: | ---: |
| Generic serial | 1,258,544 | 114,363 | 1,887,184 |
| Waveshare + microSD | 1,267,408 | 115,459 | 1,878,320 |
| Waveshare development | 1,280,160 | 137,347 | 1,865,568 |

The Waveshare change adds **6,592 app bytes and 112 static DIRAM bytes** over the park release. The portable power controller is **56 bytes on the host**. Static linker accounting is not runtime free heap or battery-current measurement. Countdown feedback currently uses the serial console; a physical LCD renderer and measured deep sleep remain absent. No board was flashed.

## Earlier park milestone

Current **care-rules10 / practice-rules7** builds pass in all three profiles with zero warnings. Source-hashed park build evidence (historical local evidence omitted) records the exact artifacts. Rarity, confirmed recovery, encounter release, frozen rules9 support and motion delivery add **4,912 app bytes and 48 static DIRAM bytes** on Waveshare compared with the prior final-Auto release.

| Current profile | App bytes | Static DIRAM bytes | Remaining 3 MiB app slot |
| --- | ---: | ---: | ---: |
| Generic serial | 1,252,192 | 114,267 | 1,893,536 |
| Waveshare + microSD | 1,260,816 | 115,347 | 1,884,912 |
| Waveshare development | 1,273,536 | 137,235 | 1,872,192 |

The host care State remains 552 B and canonical snapshot 576 B; widest sampled state JSON is 6,668/8,192 B. The 276-entry rarity table is 276 bytes and adds no persisted field. Actual maximum sampled full recovery is 12 existing Rest events; the public bound is 40. Practice snapshots and combat profiles remain unchanged. Static linker accounting is not free-heap measurement. PSRAM and physical LCD rendering remain disabled in these builds; radio, TLS, FAT, stacks and future display buffers still need runtime measurements. Private artwork uses separate microSD files. No hardware was flashed. [Park behavior and test boundaries](PARK_PLAYTEST.md). Older measurements below retain their historical scope.

On 2026-10-06, **all three firmware profiles cross-compiled and linked successfully** with official ESP-IDF **5.3.6**, commit `79e3454c68248bd7d881721c9b2e94561378a8ce`. The SDK source and all 27 pinned submodules are unmodified. This document records the microSD milestone before the subsequent egg-onboarding and battle-mode work. Current ESP practice builds (historical local evidence omitted) retain the same SDK and board configuration; [current verification](VERIFICATION.md) records their results.

The installation is task-contained under `../.toolchains/`: the SDK checkout, compiler/debugger tools, CMake 3.30.2, Ninja 1.12.1, Python 3.13 virtual environment and package cache. The native Apple Silicon Xtensa compiler is GCC 13.2.0, Espressif build `esp-13.2.0_20250707`. Espressif's installer used its pinned tool manifest and download checksums. No Homebrew packages, global Python packages or shell startup files were changed. The checkout/tools/Python/cache occupy approximately **3.15 GiB**; about 21 GiB was available before installation. [Official release](https://github.com/espressif/esp-idf/releases/tag/v5.3.6) · [Official installation instructions](https://docs.espressif.com/projects/esp-idf/en/v5.3.6/esp32s3/get-started/linux-macos-setup.html).

## Rebuild

```sh
npm run build:esp                 # GenericSerial: no board GPIOs
npm run build:esp:waveshare       # 1.46 board + primary microSD
npm run build:esp:waveshare-dev   # Also compile development asset trust/HTTP opt-ins
npm run test:handheld
npm run demo:sd
```

`scripts/build-esp.sh` only invokes `build size`. It accepts these three profiles, uses four build jobs by default, recreates each generated configuration from committed defaults, and builds in isolated `firmware/build-<profile>` directories. Those directories are ignored by Git. The wrapper changes environment variables only in its own process. No credentials are compiled in. Standard firmware uses on-device setup; the development profile alone offers an explicit private-HTTP opt-in. The public development asset key provides no production authenticity.

The managed execution sandbox blocked the SDK's parent-process lookup through macOS `sysctl`; the same authorized build succeeded with process-inspection permission. No automatic approval rejection or remaining permission blocker occurred. Ordinary Terminal builds do not use that sandbox.

## Historical microSD build output

| Profile | App binary bytes | Free in each 3 MiB app slot | Static DIRAM used | DIRAM not assigned to reported static sections |
|---|---:|---:|---:|---:|
| GenericSerial | 1,063,232 | 2,082,496 | 113,923 | 227,837 |
| Waveshare + SD | 1,071,984 | 2,073,744 | 115,003 | 226,757 |
| Waveshare + SD + development assets | 1,083,152 | 2,062,576 | 136,891 | 204,869 |

These numbers come from actual Xtensa ELF/map files and generated binaries. All final builds have zero compiler warnings. The linker reports 341,760 bytes in its DIRAM accounting region. Its almost-full separate 16 KiB IRAM bucket is a dedicated slice: instruction code also occupies shared DIRAM. It is not a one-byte total executable-RAM limit. Reported static remainder is **not measured free heap**; runtime Wi-Fi, TLS, FAT/SD, task stacks and allocator overhead consume additional memory. PSRAM is disabled in these bring-up builds. Display buffers, JPEG decoding and physical LCD rendering are not enabled.

The generated binary partition tables fit the configured **16 MiB** flash. NVS remains 64 KiB; the two app slots remain 3 MiB each. The historical 9 MiB raw-asset reservation is retained unchanged for partition compatibility; normal downloaded assets now use the **696,832-byte microSD file**. The app's **2,528-byte resident fallback** remains available without a card. Do not change a physical device's partition layout casually. [Storage behavior and recovery](SD_ASSETS.md).

Machine-readable build evidence (historical local evidence omitted) includes artifact sizes/SHA-256 hashes, profile flags, decoded partition tables, SDK identity, disk usage and full size-tool fields. The source code required two target-only fixes: portable `PRIu32` formatting for combat JSON and unambiguous conditional formatting in the asset parser. Host tests remain green. The SD adapter also avoids a source-confirmed SDK full-card `pwrite` lock defect through ordinary serialized seek/write, without patching Espressif. SDK SD audit (historical local evidence omitted).

## What the historical microSD build did not prove

During that build-only milestone, no card was formatted or accessed, no serial port was opened, and no device was flashed. Later USB verification and the current physical-playtest candidate are described above. That historical build did not verify LCD/touch, pedometer accuracy, SD removal/brownout behavior, battery/power hold, radio/TLS or runtime heaps/stacks. Later display and heap observations are above. FAT `fsync` is not proof of physical SD controller power-loss durability. The current combined build implements clock bootstrap; actual HTTPS operation and production asset/device trust remain separate gates. Nothing was deployed or pushed during the historical build-only milestone.

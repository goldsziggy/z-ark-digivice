# USB installation of the SD asset pack

The [host installer](../scripts/install-sd-usb.py) copies the reviewed pack through an explicitly identified Waveshare ESP32-S3-Touch-LCD-1.46. The board writes its mounted microSD; a Mac card reader is not required for this route. This does not flash firmware, copy saves, change device identity, or configure Wi-Fi. Each unit is selected and verified separately.

**Current checkpoint:** both units passed all 261 asset hashes on `6e058e1` without additional copies. A later Unit 2 USB probe stopped replying after bootloader output, so current console availability and release reliability remain unresolved. The corrected host opening order is tested without devices; observe the screen before another USB open. Current asset/save checkpoint (historical local evidence omitted) · [USB reliability diagnosis](USB_BOOT_RELIABILITY.md).

## Historical deployment status — 2026-10-08

The table below preserves the earlier `8a420c7`/`90ee501` milestone and transfer throughput, not the current installed versions. Unit 2's later installation and both units' rules-12 checkpoint are linked above.

| Unit | Measured result | Next verification |
| --- | --- | --- |
| Unit 1 | On `5edec43`, **all 261 destination hashes passed** after USB installation: **4,700,573 payload bytes**, **733.23708958301 seconds**, **6410.713624256519 B/s**. After upgrade to **`8a420c7`**, all 261 files again reported `VERIFIED` in a read-only recheck. | Physical screen appearance, finger input, audio and battery/PWR acceptance remain pending. |
| Unit 2 | Last installed **`90ee501`**; absent from USB/serial enumeration after confirmed reconnection. | Check with Unit 1’s working cable/port, identify this unit again, then separately install and hash-check its pack. |

The initial post-copy USB reopen hung in the pinned ESP-IDF 5.3.6 synchronous I²C NACK bus-busy wait before reaching SD mount. Firmware `8a420c7` includes the [project-local I²C backport](../firmware/components/esp_driver_i2c/README.digivice.md) of [Espressif's official bounded NACK fix](https://github.com/espressif/esp-idf/commit/2523fee9cd49ca59b90bdb475c50b5277c44c876). The actual driver function passed 75 ASan/UBSan host checks (historical local evidence omitted), and the exact old source reproduced the unbounded polling. Unit 1's subsequent runtime boots now pass. The installed SDK and caller timeouts are unchanged; successful boots and finite-wait tests do not prove recovery from an electrically stuck bus.

On `8a420c7`, the device decoded **all eight starter DVA sprites** and the **egg-screen scene JPEG** from the card. LCD diagnostics reported **`ESP_OK`**, with rendered-frame count advancing **14 → 34**. These are actual decoder/flush observations, not confirmation of the screen's visible appearance. Its own **576-byte egg snapshot remained unchanged** and restored after a software reboot; **no hatch was requested**. The deployment evidence (historical local evidence omitted) records these checks separately from the pending hands-on tests.

Device identifiers, transfer reports and asset bytes remain private. This deployment does not copy or replace either unit's saves or identity.

## Pack and filesystem requirements

The staged pack contains **261 files / 4,700,573 bytes**: 251 DVA sprites, eight JPEG scenes, `INDEX.JSON` and `ATTRIB.TXT`. It supplies artwork for 251 of the current 276 forms; 25 retain fallback artwork. The largest staged metadata file is `ATTRIB.TXT`, 1,902,964 bytes. These describe the prepared pack; Unit 1's separate destination verification is recorded above. Private content and device identifiers stay outside the repository.

Files go directly in the card root. The firmware accepts only:

- `DSF00001.DVA` through `DSF00512.DVA` by stable form ID; the prepared pack contains a subset.
- `MEADOW.JPG`, `FOREST.JPG`, `BEACH.JPG`, `RUINS.JPG`, `CAVERN.JPG`, `SNOW.JPG`, `VOLCANIC.JPG`, `DIGITAL.JPG`.
- `INDEX.JSON` and `ATTRIB.TXT`; the renderer does not parse these host metadata files.

The host pins the manifest SHA-256, rejects duplicate/case-colliding destinations and unexpected staged files, and snapshots all source bytes before opening USB. It requires exactly 251 DVA files, all eight scenes and both metadata files. Host limits are 128 KiB per DVA/JPEG, 2 MiB per metadata file and 8 MiB for the complete pack. The firmware independently caps every file at **2 MiB** and each data chunk at **512 bytes**.

The Waveshare profiles enable FAT/FAT32 and exFAT through the project-local [FatFs component](../firmware/components/fatfs/README.digivice.md), pinned to ESP-IDF **5.3.6**, revision `79e3454c68248bd7d881721c9b2e94561378a8ce`. The installed SDK is not edited; [upstream file hashes](../firmware/components/fatfs/UPSTREAM.json) record provenance. This override uses heap long filenames (128 UTF-16 units), UTF-8 and **32-bit sector addressing**: MBR and unpartitioned volumes are supported; **GPT is not supported**. It rejects oversized file lengths before narrowing them to the ESP VFS's signed 32-bit size. This is not a promise to support arbitrary card capacities or partition layouts.

The card must already mount and its owned cache must be ready. Neither installer nor protocol formats, repartitions or repairs media. `SD_UNAVAILABLE` requires diagnosis; it is not permission to format. The existing `DVASSET1.CCH` and unrelated files are excluded from transfer. See [SD storage behavior](SD_ASSETS.md) and the [firmware build instructions](FIRMWARE.md).

## Runbook

Use an existing Python environment with `pyserial` for USB access; local dry runs do not import it. Install firmware advertising `sdput=1` through the separately authorized firmware workflow first. Keep stable power, close other serial monitors and select one unit at a time. No automatic port selection by suffix, deliberate reset or reconnection is performed. The host follows Espressif's monitor opening order, but OS/driver control-line transitions can still reset a device; a read-only command mode does not guarantee a reset-free open. [Current reliability limit](USB_BOOT_RELIABILITY.md).

Replace these placeholders with the private staged directory and that unit's **complete** USB serial/chip MAC. A port path only narrows a matching serial; it cannot replace identity verification.

```sh
PACK_DIR="/absolute/path/to/private-pack"
DEVICE_SERIAL="FULL_DEVICE_USB_SERIAL"

python3 scripts/install-sd-usb.py \
  --manifest "$PACK_DIR/pack-manifest.json" \
  --serial "$DEVICE_SERIAL" --dry-run

python3 scripts/install-sd-usb.py \
  --manifest "$PACK_DIR/pack-manifest.json" \
  --serial "$DEVICE_SERIAL" --inspect \
  --report "$PACK_DIR/unit-inspection-private.json"

python3 scripts/install-sd-usb.py \
  --manifest "$PACK_DIR/pack-manifest.json" \
  --serial "$DEVICE_SERIAL" --execute \
  --report "$PACK_DIR/unit-install-private.json"
```

The default mode is `--dry-run`: it validates local files and opens no USB port. `--inspect` verifies identity and destination hashes without creating or changing card files; missing files are reported in the optional JSON report. `--execute` first checks all existing destination hashes and stops on any conflict before beginning writes. Identical files are reused. Each newly published file is read back, and a final pass verifies **all 261 hashes**. The report path must be new; it is created with mode `0600` and includes the full device identity, so keep it private.

By default, the manifest's sibling `manifest.sha256` pins it and sibling `SD-CONTENTS` supplies the files. Use `--manifest-sha256 DIGEST` or `--contents PATH` to specify those explicitly. Optional `--firmware EXACT_VERSION` pins the reported application version; `--port PATH` adds a port restriction. Run `--help` for the current flags. Firmware build and device flashing are separate operations.

For the second unit, repeat the dry run, inspection and execution with its own full serial and a different report path. Do not clone the first unit's flash, NVS, cache or save files. After installation and lease release, inspect actual sprite/background loading and fallback behavior on each screen, then check that each unit retains its own save after a normal reboot. Those observations remain separate from file-hash verification.

## Protocol and pause behavior

The [firmware engine](../firmware/runtime/usb_sd_transfer.hpp) is synchronous and owned by the runtime task. `device identity` reports the chip MAC, board profile, firmware version and `sdput=1`. The host first drains initial USB startup output for at least four seconds, requiring one second of quiet within a ten-second / 64 KiB bound. It then sends the discard fence and identity request. Quiet alone does not prove boot success; resets after the fence remain fatal. The host checks the reported MAC against the exact enumerated USB serial and requires the reviewed 1.46 profile. This prevents accidental unit selection; it is not cryptographic device authentication.

Commands use ASCII lines. Sizes and offsets are decimal; hashes and payload hex are lowercase. Ordinary command lines are limited to 1,535 characters. Before identification, the host sends one deliberately invalid 1,536-character discard line to clear any stale input fragment without submitting it as a gameplay command. Responses may follow the console prompt; the host ignores unrelated bounded console chatter.

| Command | Successful response |
| --- | --- |
| `sdput status` | `SDPUT IDLE` or `SDPUT PENDING name=NAME size=N offset=N sha256=HASH` |
| `sdput verify NAME SIZE SHA256` | `SDPUT VERIFIED name=NAME size=N sha256=HASH` |
| `sdput begin NAME SIZE SHA256` | `SDPUT READY name=NAME size=N offset=N sha256=HASH prefixSha256=HASH`, or `SDPUT EXISTS name=NAME size=N sha256=HASH` |
| `sdput chunk OFFSET HEX` | `SDPUT ACK offset=N` after synchronization and close |
| `sdput finish` | `SDPUT DONE name=NAME size=N sha256=HASH` after full staged-file SHA-256 verification and publication |
| `sdput abort` | `SDPUT PAUSED`; retain the partial and release the installation pause |

`READY` includes the hash of the actual staged prefix. The host must match it against its local source prefix before sending more bytes. It sends one chunk at a time and advances only on the exact expected ACK. Retrying the same acknowledged chunk compares existing bytes and synchronizes again; it does not append a duplicate. A completed `finish` can be retried without republishing.

The first SD command, including `status` or `verify`, acquires an installation pause after checking existing I/O barriers. Rendering, touch, motion/step polling, prefetch and ordinary gameplay commands pause. The prior asset/interface pause states are restored when the host releases the lease with `abort`; artwork requests are retried so newly installed files can load. The firmware retains the lease across individual file completions, then releases it after **60 seconds without a command**. Hash/flush execution time is activity, not inactivity. The timeout response is `SDPUT TIMEOUT partial=retained`.

Only one transfer file descriptor is open at a time, alongside the existing cache descriptor (`max_files=2`); no descriptor survives a command. Reads and writes are at most 512 bytes. SHA processing cooperates after each block; runtime yields one RTOS tick per 16 blocks (8 KiB). Power handling runs between bounded owner operations. A requested shutdown closes the logical session and retains staging before the existing I/O/save/power barriers proceed. The installer does not initiate shutdown or reset.

The host waits up to 30 seconds per response and permits three retries of the same command. A reset indication, lease timeout, partial USB write, unsafe stream or disconnect stops that invocation. It never switches devices or reconnects automatically. It sends `abort` on exit only while the connection is still trusted; otherwise the inactivity timeout releases the pause. A failed release acknowledgement prevents a success result.

## Recovery and preserved files

The engine uses only two staging names: `DVXFER.MET` and `DVXFER.TMP`. New staging requires both absent. The 128-byte metadata record identifies the exact target, expected size and SHA-256 and contains its own checksum. Existing staging is resumed only with a matching valid record and a regular bounded temporary file. The host then checks the prefix hash. An interrupted data write can resume from an unaligned byte offset; a torn metadata initialization is retained and reported as a conflict.

| Result | Next action |
| --- | --- |
| `MISSING` during verification | Execution may install the allowlisted file. |
| `CONFLICT` | Preserve the existing destination; compare it privately before deciding what to do. |
| `PENDING_OTHER` | Preserve the other staged transfer; resume with its original matching source. |
| `STAGING_CONFLICT` or `HASH_MISMATCH` | Stop and inspect retained staging. Do not delete or truncate it merely to make the installer continue. |
| `IO`, `SD_UNAVAILABLE`, `INTERRUPTED`, `POWER`, `BUSY` | Resolve the device/I/O condition and start a newly identified invocation when ready. Only `MISSING` means absent. |
| USB interruption or timeout | Keep the same card and source pack; rerun for the same full device identity. Resume requires the validated metadata and prefix hash. |

`abort` does not discard data. Unknown or malformed staging files are never adopted, truncated or removed by filename alone. Matching finalized files are recognized on rerun; cleanup is limited to validated metadata belonging to that completed transfer. The existing game save, Wi-Fi configuration and cache identity are outside this protocol.

**Sudden-power-loss limit:** the ESP FatFs rename refuses an existing destination, but its directory updates are not an atomic transaction under abrupt power loss. A cut during publication can leave inconsistent names or filesystem metadata. Likewise, successful `fsync` reaches FatFs but does not prove durability inside an SD controller's internal cache. The implementation retains uncertain state and stops; it does not promise automatic recovery from every interrupted rename or repair filesystem damage. Keep stable power and use normal shutdown after completion. Physical power-cut and card-removal validation is still pending.

## Risk-based checks

```sh
bash firmware/tests/test_usb_sd_transfer.sh
python3 -m unittest discover -s tests -p 'test_install_sd_usb.py'
bash firmware/tests/test_power_runtime.sh
python3 firmware/tests/test_fatfs_exfat.py
```

The engine suite has **8,663 passing checks** with real mbedTLS, POSIX files and ASan/UBSan. It covers descriptor/chunk bounds, 2 MiB cooperative hashing, differing destinations, symlinks and unknown staging, duplicate ACK recovery, partial writes/ENOSPC, failed synchronization, failed readback, prefix resume, hash rejection and interrupted metadata cleanup. These are host results.

The [host installer tests](../tests/test_install_sd_usb.py) have **30 passing tests** using fake serial and temporary files for opening order/failure cleanup, identity/capability checks, manifest pinning, preflight conflicts, framing, lost replies, bad prefixes, disconnects, large attribution transfer and final complete-pack verification. A separate local-pipe run used the actual engine with mbedTLS and sanitizers: all 261 staged files passed, then an identical rerun sent zero payload and rechecked all final hashes. That is interoperability evidence, not measured USB throughput.

The [runtime tests](../firmware/tests/power_runtime_test.cpp) exercise pause restoration, inactivity timeout, gameplay exclusion, card loss and power transitions with device I/O doubles. The [FatFs tests](../firmware/tests/test_fatfs_exfat.py) have 318 passing sanitizer checks (historical local evidence omitted) on disposable FAT32/exFAT MBR images, including remount/read-write integrity, Unicode names, rename collision preservation and actual VFS oversized-file rejection. None substitutes for per-unit USB, SD, display or sudden-power-loss acceptance.

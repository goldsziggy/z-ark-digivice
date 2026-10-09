# microSD asset storage

The Waveshare 1.46 profile uses **microSD as the primary downloaded-asset store**. The application image always contains the 2,528-byte original Mote/Flicker fallback. Game saves stay in their existing dual-slot NVS namespace. There is no automatic switch to internal flash when SD storage fails, and no formatting command.

The adapter mounts an existing compatible FAT card at `/sdcard` and opens only `/sdcard/DVASSET1.CCH`. The 8.3 filename works without enabling long filenames. A new file is created exclusively; an existing file is never truncated or replaced. Its exact size is **696,832 bytes**: a 512-byte ownership/version header plus the existing 696,320-byte bounded cache. The rest of the card is outside this cache's API.

Creation fills this one file with erased-state bytes in 512-byte pieces, syncs it, writes its ownership header last, syncs again and verifies it. An incomplete, foreign, wrong-size or unknown-version file is preserved and the application uses resident art. Initial creation involves 1,361 small writes plus the ownership header and can lengthen first boot; actual timing needs measurement.

The shared cache retains its five slots, 4 KiB download checkpoints, SHA-256 verification, protected working assets and final activation marker. File writes complete and sync before the next data/journal/commit operation. The portable policy checks every range and NOR-style zero-to-one transition. It never allocates the entire cache in RAM.

| Condition | Behavior |
|---|---|
| Card absent or filesystem cannot mount | Preserve media; show resident art and continue local gameplay |
| Card full/read-only, partial write or failed sync | Stop further cache writes; preserve the file and NVS saves |
| Runtime removal or I/O error | Pause downloads and use fallback; no hot-remount or medium substitution |
| Interrupted download with valid file/header | Reopen on boot and resume from the last verified 4 KiB checkpoint |
| Unknown ownership header/layout | Preserve it for reviewed recovery; do not rewrite |
| Bad optional asset integrity | Do not activate it; retain valid installed art/fallback |

Recovery requires a reboot/rescan after the card problem is resolved. The firmware does not delete incomplete files, format a card, or silently rebuild an unrecognized filesystem. Back up a problematic card before any separately reviewed recovery.

## Board integration

The audited vendor path is **one-bit SDMMC**, with CLK14, CMD17 and D0=16. SD D3/CS is not an ESP GPIO: it is **EXIO3, TCA9554 P2, mask `0x04` at I²C address `0x20`**. The board-owned I²C bus uses SCL10/SDA11. Under the shared bus lock, preload only P2 high, set only P2 to output, and verify both registers while preserving other bits. This avoids the vendor demo's whole-expander reset/configuration. GPIO21 remains LCD chip-select. [Audited sources](PARK_HARDWARE.md).

The mount configuration always sets `format_if_mount_failed=false`. Card-detect/write-protect GPIOs remain unassigned; the schematic has SD pull-ups. The adapter uses default 20 MHz SDMMC timing and one fixed file descriptor. The board, expander, card identity and timing are not physically verified by compiling.

## SDK-specific handling and limits

ESP-IDF 5.3.6's FAT `pwrite` implementation can return on a full-card path without releasing its filesystem lock. Our adapter uses `lseek` plus ordinary `write` on the exclusively owned descriptor, under existing serialized cache access. This avoids modifying the installed SDK. Source audit (historical local evidence omitted).

`fsync` reaches FatFS `f_sync`; the SDK SDMMC `CTRL_SYNC` handler returns success without an independent physical card-cache flush. Consequently host tests and successful return values **do not establish sudden-power-loss durability on a real SD controller**. Electrical power-cut tests, card-removal tests, first-boot timing and heap/stack profiling remain required. No card was accessed or formatted while developing this change.

Run `npm run demo:sd` for the actual file-backed host scenario. It creates a private temporary file, reopens the same inode, resumes byte4096, verifies the asset, captures offline and removes only its own temporary directory. `npm run test:handheld` includes 252 focused file-policy fault checks. [Build commands and actual binary sizes](ESP_BUILD.md).

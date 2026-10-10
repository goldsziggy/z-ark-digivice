# Firmware 53686e6 build package

Target: Waveshare ESP32-S3-Touch-LCD-1.46 standard glass, SKU29565. Built from clean source `53686e66708bf512729bf4fee8d61340747fcb92` with official ESP-IDF 5.3.6 (`79e3454c68248bd7d881721c9b2e94561378a8ce`). Compiler warnings: 0. These raw images are build records, not an automatic installer.

This image is schema 23 / rules 16. Snapshots are 3,216 bytes and native `State` is 3,188 bytes on the host that produced the ABI check. The level cap is 50. Selected XP companions earn the victory or capture bond once. A new duplicate merges into the oldest owned exact form. Failed captures stay in the same battle for three attempts. Auto battles start on their own, Run Away remains, and capture stays a manual throw. Critical hits are 5% at 1.5× damage. Useful care grants 2 XP off cooldown. Powered-off and overnight time are not punished. The motion detector is unchanged: false steps while stationary remain unresolved.

[manifest.json](manifest.json) gives the four image hashes, sizes and build layout offsets. The partition table and OTA-data images match the bytes already recorded for the installed `171cda7` layout. The bootloader image does **not** match that installed bootloader. Their presence is a reproducibility record, not an instruction to overwrite those regions. For a compatible existing installation, the reviewed update scope is application-only at `0x20000`, after preserving that unit's own NVS, settings and SD card. Never erase an existing save to accommodate an update.

The application is 1,607,872 bytes, 6,816 bytes larger than `171cda7`, with 1,537,856 bytes free in its 3 MiB OTA slot. Static DIRAM is 213,055 of 341,760 bytes. Linker remainder is not runtime heap. The application stack chain and the synthetic JSON maximum were not remeasured; do not reuse the `171cda7` figures for this binary. [Resource summary](resource-summary.json).

`scripts/flash-device.py` remains pinned to historical source `6e058e1` and rejects this raw package. No release-specific automatic installer is bundled.

The historical [171cda7 package](../firmware-171cda7/README.md) stays the record of the previous verified installation. This package does not include saves, credentials, private art or device identifiers.

Both connected boards received this application at `0x20000` only. Bootloader, partition table and OTA metadata were not written. Immediately after that write, and before the application ran, each board's NVS matched its own pre-flash backup and the on-flash assets partition was still the empty image. A later verification boot migrated each save in RAM. That boot also let the new rules checkpoint once, so those NVS partitions were restored from the pre-flash backups and the boards were reset onto this application. The SD cards were not mounted or written. [Sanitized installation result](installation-summary.json). False steps while stationary remain unresolved, and this package does not include a photograph of either screen.

[XP companion guide](../../docs/XP-COMPANIONS.md) · [Collection](../../docs/ROSTER60.md) · [Publication scope](../../PUBLICATION.md).

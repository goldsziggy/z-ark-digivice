# Verified f74ee4c development firmware

These are the exact immutable images previously installed and hash-verified on both project units. Source: `f74ee4c132b4b9bc6407d99bbc20b13af9dfd962`; official ESP-IDF 5.3.6. Target: **Waveshare ESP32-S3-Touch-LCD-1.46, standard glass, SKU29565**, 16 MiB flash / 8 MiB PSRAM, with CCW90 display mounting and matching touch transform. This is not a universal ESP32-S3 image.

[manifest.json](manifest.json) records each byte length, SHA-256, offset, SDK and configuration hash. It includes the app, bootloader, partition table and initial OTA metadata. No NVS image, credentials, save, device identity, private artwork or SD backup is included. Initial OTA metadata is a build artifact, not a device readback.

The application is **1,550,512 bytes** (SHA-256 `9e728539f57bd82f3d53e4cc749c8475e52e68b124d8901d1d9324cf692ae324`). Its 3,145,728-byte OTA slot has 1,595,216 bytes free. Measured static DIRAM is 166,847 bytes, leaving 174,913 linker bytes; the main stack is configured at 16 KiB. The framebuffer uses 339,488 PSRAM bytes; display DMA uses 13,184 bytes. These are build/layout measurements, not measured battery endurance or a worst-case live heap guarantee.

## Installation boundary

Do not erase a device merely to reproduce the earlier authorized reset. Preserve its own current save and SD card. A qualified installation must verify the exact board, flash size, security state, partition layout, existing boot/OTA state, artifact hashes and intended app slot before writing. App-only installation on the verified original layout used offset `0x20000`; that address must not be assumed on another unit. Bootloader, partition table and initial OTA metadata are supplied for reproducibility, not as blanket permission to overwrite them.

The repository's historical guarded `scripts/flash-device.py` is pinned to an older artifact set and requires private local review evidence; it is **not an installer for this package**. Do not bypass its checks. Review a release-specific installer and the intended device connection before an update. The supplied image hashes are data, not an authorization to reset or flash.

## Previously verified, still pending

The same f74 app was installed and readback-verified on both project units. At that earlier checkpoint, freshly initialized schema-17/rules-13 saves survived reboot, and 261 existing SD files were hash-verified without payload changes. Those private files and saves are not distributed. Unit 2's earlier USB silence did not recur in the bounded verification run; long-term reliability remains unproven.

Physical finger alignment, animation timing, live battles, walking accuracy, speaker behavior, sleep/wake and two-device RF play remain acceptance tasks. The firmware predates the volume/music/trading update, which is deliberately absent from this stable publication. No board was accessed to prepare this export.

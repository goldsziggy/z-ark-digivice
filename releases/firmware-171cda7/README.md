# Firmware 171cda7 build package

Target: Waveshare ESP32-S3-Touch-LCD-1.46 standard glass, SKU29565. Built from source `171cda7e698cf2915b50aa46bc766d8d1d0ee50d` with official ESP-IDF 5.3.6. These raw images are build records, not an automatic installer. The manifest records the completed two-device installation; [the sanitized result](installation-summary.json) and [publication validation](../../PUBLICATION_VALIDATION.json) distinguish verified data preservation from untested gameplay acceptance.

Both existing devices passed the application-only `171cda7` update: their own saves migrated exactly to 2,964 bytes, settings were preserved, and all 261 SD files (4,700,573 bytes per device) verified without asset writes. Each save remained byte-identical through checkpoint and reboot, with zero gameplay events. The protected flash region matched before boot; all serial handles are closed. The three XP companion slots began empty.

The application adds three optional XP companions while retaining capacity 60, moves saves to schema 22/rules 15, and fixes the completed-capture startup/result-idle behavior. The motion detector is unchanged: earlier reports of false steps while stationary remain unresolved. This package does not establish physical motion accuracy.

Preserve each device's own save, settings and SD content before upgrading. Firmware older than schema 22 cannot read the new companion record. A downgrade requires compatible firmware or that device's own pre-upgrade backup. No saves, credentials, private art or device identifiers are included.

[manifest.json](manifest.json) gives the four image hashes, sizes and build layout offsets. Bootloader, partition and OTA images are reproducibility records; their presence is not an instruction to overwrite those regions. For a compatible existing installation, the reviewed update scope is application-only at `0x20000`, after checking that unit's flash layout and preserving NVS, save/settings, partition/OTA metadata and its own backup. Never erase an existing save to accommodate an update.

The repository's `scripts/flash-device.py` remains pinned to historical source `6e058e1`. It rejects this raw package with `Unsupported board/build manifest`; changing a version string or folder name does not make it compatible. No release-specific automatic installer is bundled. Review the exact device and layout before using these images.

The [resource summary](resource-summary.json) records measured application/static memory, the 38,500-byte synthetic state-JSON maximum, NVS geometry and conservative compiler stack analysis. The known application chain is 26,720 bytes of the configured 32,768-byte main stack; its remaining 6,048 bytes are **before unmeasured SDK/library and indirect-call use**, not a physical safety margin.

[XP companion guide](../../docs/XP-COMPANIONS.md) · [Collection](../../docs/ROSTER60.md) · [Publication scope](../../PUBLICATION.md).

# Pinned project-local FatFs

Runtime sources come from Espressif ESP-IDF 5.3.6 commit
`79e3454c68248bd7d881721c9b2e94561378a8ce`, component `fatfs` (FatFs R0.15
patch2 plus Espressif fixes). `UPSTREAM.json` lists the SHA-256 of each copied
file before project changes. Original notices remain in each file; the SDK's
Apache-2.0 license is included as `LICENSE.ESPRESSIF`.

ESP-IDF selects this `firmware/components/fatfs` component ahead of the SDK's
component. The SDK checkout is not modified. Reconfigure an existing build after
adding the override. Changes from upstream are limited to:

- `Kconfig`: opt-in `CONFIG_FATFS_EXFAT`, requiring long filenames.
- `src/ffconf.h`: map that option, give the optional volume-label flag an explicit
  zero when disabled (the exFAT path uses it in C code), and disable TRIM.
- `vfs/vfs_fat.c`: return `EOVERFLOW` from `stat` and `fstat` before narrowing a
  file size above `INT32_MAX` into the ESP/newlib signed 32-bit `off_t`.

Both Waveshare profiles select exFAT, heap long-filename buffers, a 128 UTF-16-unit
name bound, and UTF-8 API encoding. FAT12/16/32 support remains enabled.
`FF_LBA64` stays zero: this is MBR/unpartitioned support, not GPT or SDUC support.
No disk-interface ABI changes or new filesystem implementation are introduced.

The application continues to mount with `format_if_mount_failed=false`; no
formatter or repair command is exposed. Upstream `f_mkfs` was already compiled
and remains unchanged for IDF API compatibility. The test harness invokes it
only on new, exclusive-created regular files in a disposable host directory.
TRIM is disabled, so normal file operations do not request card erase/discard.
The upstream image-generation CMake helper still refers to SDK FAT12/16 Python
tools; they are not exFAT/GPT image generators.

Run from the repository root:

```sh
python3 firmware/tests/test_fatfs_exfat.py
```

The sanitizer-backed test compiles the actual `ff.c`/`ffunicode.c` and extracts
the actual VFS `stat`/`fstat` functions without edits. OS locks, allocation and
the regular-file block device are host doubles. Two disposable 64 MiB images
have MBR partitions at LBA 2048; mounting uses automatic partition discovery.
Tests cover byte-identical file reads/writes and remounts, UTF-8 filenames,
interleaved directory reads/stat, refusal to rename over an existing file,
allocation failure recovery, signed-size boundaries, and an exFAT directory
fixture advertising a >4 GiB file. Image SHA-256 values are printed before the
temporary images are removed. No physical card, device or private asset is read.

At LFN128, exFAT name scratch is 610 bytes per active named operation, compared
with 258 bytes without exFAT: an added 352 bytes. This was also observed by the
host allocator test. Structure sizes printed by the harness are host sizes,
not ESP measurements. Target image/static-memory changes belong to the build
report; actual heap, latency and card durability require device verification.

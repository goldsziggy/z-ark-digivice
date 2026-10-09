> Publication content: ten original DVAs are retained as explicit legacy test fixtures (88,160 bytes). Private scenery is omitted. Normal service startup filters all fixtures and serves a signed empty catalog; fixture opt-in serves the ten unchanged sprites. Full-content historical size figures below are not the export size.

# Compact device assets

Normal gameplay serves only the eight scenery assets from this directory. The
ten original toy creature blobs (Mote, Flicker and their families) are legacy
codec test fixtures, excluded from normal catalogs and download URLs. Real
Digimon use the separately installed private SD art; this service does not supply
public Digimon artwork.

These assets are separate from the existing browser packs. The unsigned `index.json`
is input to the service's device-catalog signer; firmware must verify the signed
catalog and each blob's SHA-256 before activation. DVA's CRC catches damaged local
payloads but is not an authenticity check. There are no private character imports.

The primary profile is `s3-146-v1`, for the officially documented Waveshare
ESP32-S3-Touch-LCD-1.46 standard glass SKU29565: **412 × 412**, 16 MB flash,
8 MB PSRAM. [Official documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46).
This is a device asset profile, not a claim that the board has been bench tested.

## Build and verify

The sprite encoder, CRC reader and format tests use Python's standard library.
JPEG resizing also requires Pillow; version **12.3.0** produced this release. No
package installation or SDK download is performed by the builder. This workspace
already has a suitable interpreter:

```sh
python3 scripts/build-device-assets.py --self-test
python3 -B scripts/build-device-assets.py --out-dir /tmp/digivice-scenes
python3 -B scripts/build-device-assets.py --out-dir /tmp/digivice-scenes --check
```

The interpreter is only reused; the CAD project is never edited. `--check`
regenerates the output in memory and fails on any byte difference. To build into
a temporary location, specify `--out-dir`. Default output contains eight scenes
and never writes a firmware header. `--include-test-fixtures` explicitly adds ten
toy sprite blobs; an optional `--fallback-header` must name a separate test-only
destination and cannot overwrite the release fallback header. Inputs,
their hashes, JPEG settings and the Pillow version are recorded in `sources.json`.
Changed blob content requires a new asset version and catalog release before
distribution. The browser packs and browser signed catalog are unchanged.

## DVA1 indexed sprite file

All integers are little endian. Each file describes **one creature**, with a
16-entry RGB565 palette and packed 4-bit indices. Index 0 is transparent. For every
byte, the high nibble is the first pixel, then the low nibble; rows have no padding.
Full creature files are 32 × 32 and contain 17 original animation frames.

| Offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 4 | ASCII `DVA1` |
| 4 | 2 | Format version, 1 |
| 6 | 2 | Header length, 32 |
| 8 | 4 | Asset version, 1 for this release |
| 12 | 2 | Width, 16 or 32 |
| 14 | 2 | Height, equal to width |
| 16 | 1 | Palette entries, 16 |
| 17 | 1 | Animation records, 6 |
| 18 | 1 | Transparent index, 0 |
| 19 | 1 | Flags/reserved, 0 |
| 20 | 2 | Total frame count |
| 22 | 2 | Bytes per frame, width × height ÷ 2 |
| 24 | 4 | Payload bytes, exactly file length minus 32 |
| 28 | 4 | IEEE/zlib CRC32 of bytes from offset 32 through EOF |

Payload sections are contiguous: palette **32 bytes**, animation table **48
bytes**, then frame data. Each 8-byte animation record is `id:u8, count:u8,
frameMs:u16, firstFrame:u16, reserved:u16`. IDs must appear exactly once in this
order, and `firstFrame` must follow the preceding record's range with no gaps:

| ID | Animation | Full frames | Resident fallback frames |
| ---: | --- | ---: | ---: |
| 0 | idle | 4 | 2 |
| 1 | attack | 3 | 2 |
| 2 | hurt | 2 | 1 |
| 3 | sleep | 2 | 1 |
| 4 | care | 2 | 1 |
| 5 | celebrate | 4 | 2 |

Frame data starts at offset **112**. Readers reject unknown flags/format, changed
palette size, invalid dimensions, animation intervals outside 40–2000 ms, frame
counts outside 1–8 per animation, invalid ranges, trailing bytes, truncated
sections or a failed payload CRC. All blobs have a hard **128 KiB** limit.
RGB565 values are palette colors; panel transfer byte order is a separate BSP
concern that still needs physical verification.

## Legacy codec fixture header

An explicit `--include-test-fixtures --fallback-header /tmp/test-fallback.hpp`
build can emit the old 16 × 16 Mote/Flicker decoder samples. They are not release
fallback creatures. Each sample is 1,264 bytes (2,528 bytes together); those bytes
are fixture data, not a current production RAM or flash budget. Missing real
partner art must keep that partner's identity rather than substitute a toy.

## Scenery and index

Each `scene-<name>-412-v1.jpg` is a standalone **412 × 412 baseline JPEG**, RGB
source, 8-bit, 4:2:0, quality 82. The builder resizes the already approved 480px
scene once using LANCZOS. There is no full-screen UI text in these images; device
controls and sprites remain separately rendered. No JPEG-to-base64 wrapper is
needed on the device. Browser packs remain at their existing 480px resolution.

The canonical unsigned index has exactly this outer structure:

```json
{"formatVersion":1,"release":1,"profile":"s3-146-v1","packs":[{"id":"sprite-mote-v1","version":1,"bytes":8816,"sha256":"...","kind":"sprite","width":32,"height":32,"file":"sprite-mote-v1.dva"}]}
```

The checked-in legacy index lists ten fixture sprites and eight backgrounds;
normal service startup filters the fixture entries before reading their files or
serving URLs. A default rebuild writes an eight-background index. Paths are fixed local
filenames; only the signing service replaces `file` with authorized download
URLs. The index is less than 8 KiB. Full on-disk asset bytes and per-creature
frame counts are measured in `budget.json`.

## Storage and working memory

The retained full test fixture set totals **492,705 bytes**: **88,160** sprite bytes and
**404,545** background bytes. Normal service content is those **404,545**
background bytes, with no creature fallback payload. The largest single blob is
**63,510 bytes**. In the isolated legacy fixture build only, adding
the 2,528-byte resident fallback gives 495,233 bytes of content; a design that
downloads assets on demand need not retain all of it in flash. Metadata, firmware,
OTA slots, saved games and filesystem overhead are additional. Current art alone
does not require microSD; partition and OTA sizing still need a complete flash plan.

Encoded storage is not decoded RAM. The runtime's conservative upper bound for
four retained 128 KiB encoded slots plus one atomic replacement slot is **655,360
bytes of payload capacity**. Each slot also reserves two 4 KiB sectors for its header and progress journal, so the fixed raw partition window is **696,320 bytes**, including 40,960 bytes of metadata. Actual current blobs are much smaller. A 4 KiB transfer chunk is separate working RAM.
Do not preload or decode all roster art.

For the 8 MiB PSRAM primary profile, assume two 412 × 48 RGB565 display strips:
**79,104 bytes**. Two current 32px sprite frames cost **4,096 bytes**; two full
four-frame animation clips cost **16,384 bytes** if a renderer retains clips.
An optional full decoded scene is **339,488 bytes**. A full display framebuffer
is another **339,488 bytes**, an alternative to strips; do not silently reserve
both full buffers. JPEG scratch, TLS/network buffers, BSP allocations and system
heap have not been measured and are excluded from these arithmetic estimates.

For a 2 MiB PSRAM alternative, default to **no background decoding** and only
the selected creature, with an identity-preserving missing-art placeholder. The encoded cache lives in flash and can retain the same five-slot allocation when the flash partition permits; smaller PSRAM alone does not require reducing it. That is a conservative
profile proposal, not a claim that either memory configuration has passed a
physical device test. QSPI DMA compatibility, runtime free heap, FPS and power
remain unmeasured.

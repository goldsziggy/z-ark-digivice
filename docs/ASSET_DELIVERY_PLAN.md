# Assets, Wi-Fi and storage delivery plan

Updated 5 October 2026. The browser/service is the test surface for content delivery. The ESP32 source has no enabled Wi-Fi, asset filesystem or downloader; no target compile or physical display performance is claimed. See [feature status and Wi-Fi setup](FEATURE_ROSTER.md) and [firmware status](FIRMWARE.md).

## Keep the working game small

Use original indexed pixel art grouped into a few versioned packs. Keep the starter companion, battle fallback and essential interface art available locally. Optional art families can be installed and previewed without changing rules. Rules version, save schema version and artwork version are separate; a downloaded image cannot silently grant a move, add an owned creature or change a battle result.

Immediate local delivery scope is a starter pack plus Tide and Ember family packs, with a gallery for content inspection. The three generated packs now exist. Gameplay still has the Mote/Glint/Lumen line, wild Flicker and aggregate captures. Family choices in a gallery are not unlocked teams or functioning new encounter species.

## Local development contract

The service implementation for this delivery milestone uses a fixed allowlist of three packs: `starter-v2`, `tide-v1`, `ember-v1`. Its catalog envelope is:

```json
{"keyId":"digivice-dev-v1","payloadBase64":"...","signature":"..."}
```

Decode `payloadBase64` into the exact UTF-8 manifest bytes. Verify ECDSA P-256 with SHA-256 over those bytes; do not parse and reserialize the JSON before signature verification. `signature` is base64 of a fixed 64-byte IEEE-P1363 `r || s` signature. The manifest shape is:

```json
{
  "formatVersion": 1,
  "release": 1,
  "rulesVersion": 1,
  "packs": [
    {
      "id": "starter-v2",
      "version": 1,
      "bytes": 0,
      "sha256": "<digest of exact pack bytes>",
      "url": "/api/assets/packs/starter-v2/1",
      "required": true,
      "decodedBytes": 0
    }
  ]
}
```

The zero byte counts and digest above are structural examples, not usable signed content; the abbreviated array shows only the starter entry. The actual signed catalog always lists all three fixed pack IDs, with measured byte counts and digests. The starter is required to retain; Tide and Ember are optional to install. The local fixture has no expiry field. Release/version checks can prevent accidental cached downgrade but do not prove freshness after storage reset.

The signing fixture deliberately uses the publicly documented RFC 6979 P-256 example key. Anyone can reproduce its signatures. It exercises validation and failure paths, **not production authenticity**. The development service refuses production mode. A real release needs a separately authorized signing process and public trust root; do not generate production keys or copy this fixture key into a product trust policy.

| Bound | Local delivery contract |
| --- | --- |
| Signed catalog | At most 16 KiB |
| Encoded pack | At most 256 KiB |
| Decoded-size claim | At most 2 MiB per pack in the aligned service/browser contract. Calculated as all frame widths × heights × 4 RGBA bytes; validate against actual frame data |
| Browser durable cache | At most 1 MiB accounting budget: exact payload and staging bytes, serialized signed envelopes and estimated metadata overhead; not an on-disk IndexedDB size guarantee |
| In-flight transfer | One pack at a time; finite response, retry and timeout limits |
| URLs | Fixed same-origin allowlist; no arbitrary upload or executable asset URL |

The browser cache limit is not a JavaScript heap measurement. Indexed strings, parsed objects, canvas surfaces and RGBA decoding use additional memory. The ESP loader needs its own lower live decode budget and must not decode all installed content merely because the browser accepted it. Cached art/fallback can remain usable after asset-network failure in a loaded page; an offline cold-open application shell is a separate feature, not implied by IndexedDB. Browser gameplay still depends on its running local rules service.

The service verifies the catalog signature and exact pack length/hash at startup, then serves immutable loaded bytes. Downloads use strong digest ETags. GET/HEAD support full responses; `If-None-Match` can yield 304. A single byte range can yield 206; malformed, multiple or unsatisfiable ranges yield 416 with the total size. A mismatched, weak or date-valued `If-Range` falls back to a full 200 response. The six focused service asset tests have passed, including browser-compatible signature verification, resume behavior, corrupt-file/startup rejection and refusal to sign with the public fixture in production mode. See [service API](../service/API.md) and [asset tests](../tests/assets-service.test.ts); this document is not a claim that the ESP implements these endpoints.

## Measured generated content

The [generated budget](../assets/packs/budget.json) records exact file lengths, frame counts and pixel totals. Expanded byte columns below are calculated from those measured pixel counts, not measured browser/ESP heap allocations.

| Pack | JSON bytes on disk | Frames, all categories | Packed pixel bytes | Expanded RGB565 pixels | Expanded RGBA pixels |
| --- | ---: | ---: | ---: | ---: | ---: |
| Starter v2 | 61,485 | 99 | 41,088 | 164,352 | 328,704 |
| Tide v1 | 36,433 | 51 | 26,112 | 104,448 | 208,896 |
| Ember v1 | 36,451 | 51 | 26,112 | 104,448 | 208,896 |
| Total | **134,369** | **201** | **93,312** | **373,248** | **746,496** |

Palette, JSON, base64 and names are included in file sizes but excluded from packed-pixel totals. Expanded-pixel totals exclude palette tables, transparency masks where needed, canvas/parser objects, driver state and framebuffers. All three packs fit the current encoded limit; that does not establish hardware render speed.

## Download, verify and activate

1. Retain the fixed starter fallback and current active pack. Fetch and bound the envelope, verify its known key/signature, then parse and validate manifest fields, compatibility and sizes. Never let an untrusted `keyId` supply a new trust root.
2. Make room before downloading. Evict only optional inactive packs; protect the active pack, required fallback and metadata needed for recovery. If the candidate and its staging data cannot fit without deleting those, reject installation and explain the storage limit.
3. Resume only a partial transfer recorded against the same manifest identity, exact total bytes, expected SHA-256 and strong ETag. Send `Range` plus `If-Range`. Append only a correct 206 with matching range start/total and validator; discard stale partial bytes on a full 200 and restart from byte zero. A 416 or contradictory header triggers bounded restart/error handling, not blind concatenation.
4. Stream within the declared and hard maximum length. Validate the full digest, then all dimensions, palette indexes, frame counts and decompressed-size arithmetic. Reject unknown versions, extra content and truncated data. Signature validation does not replace resource validation.
5. Stage a complete verified candidate. Switch the active-pack pointer only in one durable transaction after its data and metadata exist. An interruption before activation leaves the previous pack active. Boot/reload verifies the selected pack; fall back to the last good pack or fixed starter if content is missing or corrupt.
6. Preserve partial progress across a normal reconnect where valid; expose cancel/retry. Asset failures do not erase or rewind gameplay saves. Cache initialization failure should still leave the packaged starter usable.

For a browser, use an IndexedDB transaction for data/metadata/active-pointer changes and bound the temporary record. For firmware, keep numbered complete generations with validated lengths/hashes and a checksummed activation record. At boot ignore partial candidates, validate the chosen generation and retain the old generation until the new one is durable. A FAT rename alone is not proof of power-loss atomicity. Keep recovery art in the application image so a damaged asset volume does not prevent a readable error screen.

## Device flash and optional microSD

The existing [partition table](../firmware/partitions.csv) reserves two 3 MiB app slots, 9 MiB for assets, 64 KiB NVS, 8 KiB OTA metadata and 4 KiB PHY data. It leaves 896 KiB at the tail unassigned. These are allocations, not measured firmware size, installed content or working OTA. Preserve two app slots; if the real binary requires 4 MiB slots, reduce the asset reservation and review partition migration explicitly. Two 4 MiB apps plus the present 9 MiB assets would not fit the 16 MiB device.

Use the 9 MiB asset region as the first device content store after a filesystem/format decision and benchmark. Maintain space for active content, a complete staged replacement, metadata and recovery. Do not use the nominal 9 MiB as permission to fill every byte, and do not overwrite app slots to fit a pack. Essential starter art remains built into the application independently of optional pack eviction.

microSD is optional expansion after the flash-backed slice works, and can hold a larger full art roster while flash preserves minimum offline play. Prefer a reviewed SPI TF path integrated with the board initialization: GPIO1 MOSI and GPIO2 clock are shared with LCD configuration, GPIO42 is the card data return, and card CS uses **EXIO4 on the expander, not GPIO4**, which is battery ADC. Never initialize a second unrelated driver over those pins. SD D1/D2 are unconnected, so 4-bit SDMMC is unavailable. A reviewed 1-bit SDMMC path is possible—the inspected vendor example uses that approach—so the board is not universally “SPI only.” The chosen mode must be tested with display reset, insert/remove and sleep behavior. [Official schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-2.1_schematic_diagram.pdf)

No automatic format on mount failure. A missing, corrupt or removed card keeps the starter and pet save usable; save state stays in NVS, not on a removable card. Content installation waits for adequate verified space. The PN532 candidate at address 0x24 shares I2C GPIO7/15 with touch, IMU and RTC; serialize finite bus transactions and leave downloads out of that bus/driver task. The EXIO8-controlled buzzer supports simple on/off patterns, not streamed PCM/WAV assets.

## Rendering budget and content limits

| Item | Budget / meaning |
| --- | --- |
| Physical display | 480×480; planned RGB565 output gives 65,536 colors |
| Two RGB565 framebuffers | 921,600 bytes = 900 KiB, calculated; use PSRAM with audited driver synchronization |
| One 32×32 sprite frame | 2,048 bytes RGB565; 4,096 bytes RGBA, calculated |
| Four displayed 32×32 frames | 8 KiB RGB565 pixels, plus transparency/mask data, metadata and compositor buffers |
| One creature's 17 frames | 34 KiB RGB565 or 68 KiB RGBA pixels if all expanded; load frames lazily |
| Ten creatures' 170 frames | 340 KiB RGB565 or 680 KiB RGBA pixels if all expanded; excludes icons, effects, metadata and runtime allocations |
| Active sprite instances | Target at most four; cache only their required animation frames and small effects |
| Browser UI prepared clips | Up to four player frames + four wild frames + four gallery frames + six capture-effect frames = 72 KiB RGBA canvas pixel data while the effect is retained; temporary decode adds one 4 KiB frame and an ImageData copy. Without the effect, the three creature clips total 48 KiB |
| Optional decoded-frame cache class | 64 KiB RGBA pixel-data cap in the cache module; the UI currently uses prepared clips instead, so do not add this as an observed UI allocation |
| 512×512 8-bit indexed page | 256 KiB pixels + 512-byte RGB565 palette, calculated; a transparent index needs an agreed convention |
| 1024×1024 RGB565 atlas | 2 MiB before transparency/metadata, calculated; avoid making this the minimum allocation |
| Device download buffer | Initial 8–16 KiB streaming target; TLS buffers/heap are additional and must be measured |
| Device resident decoded content | At most 2 MiB initial ceiling; choose much less for these small sprites; not measured usage |
| Device decode scratch / PSRAM headroom | At most 512 KiB scratch and retain at least 2 MiB free PSRAM as engineering targets, separate from framebuffers; measure with Wi-Fi/TLS active |
| Internal RAM headroom | At least 120 KiB free during display + Wi-Fi as an initial acceptance target; measure fragmentation/DMA-capable heap and stacks too |
| Frame rate / battery runtime | Unknown until real display, TLS, storage and supply tests; video export FPS is not device performance |

The current art inventory is ten 32×32 creatures with 17 frames each: idle 4, attack 3, hurt 2, sleep 2, care 2 and celebrate 4. Forest contains Mote, Glint, Lumen and Flicker; Tide contains Rill, Brine and Pelagia; Ember contains Cinder, Scoria and Pyrel. There are also fourteen 16×16 one-frame icons and four effects (spark/capture/hit/heart, 4/6/3/4 frames). These ten art forms now correspond to three implemented evolution lines and Flicker; they are four stable species identities, not ten unrelated species. Each sprite uses a 16-entry RGB565 palette and 4-bit packed pixels (512 bytes for one 32×32 frame before base64/JSON and palette metadata). Both service and browser limit dimensions to square 16 or 32 pixels, each animation to eight frames, frame timing to 40–2,000 ms and each asset category to 32 entries. Creature sprites require all six recognized animation names; icons and effects use idle. The browser prepares selected clips outside the animation loop rather than expanding the whole pack. Its heart, hit, spark and capture effects are presentation behavior, not a measured embedded allocation. Browser parser, canvas-engine and CPU overhead remain unmeasured.

The frame budget above is per frame, not per animation. Browser RGBA totals and device RGB565 totals differ, and neither includes every runtime allocation. Keep effects bounded, prefer authored sprite effects over large dynamic blur surfaces, and profile simultaneous saving/networking; the RGB display and flash/PSRAM traffic compete for bandwidth. Actual hardware concurrency remains unverified. [Espressif RGB display guidance](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html)

No download speed or battery runtime is promised. A rough transfer-time estimate is `verified payload bytes / measured application throughput` plus connection, TLS, signature and flash-write time. A runtime estimate needs measured current in each state and the actual safe battery/power path; nominal cell capacity alone is insufficient.

## Firmware network and trust port — planned

Use 2.4 GHz Wi-Fi in brief explicit/idle sync sessions. The game/render task never waits for the network task. Bound HTTP buffers, set timeouts, use backoff with jitter and retain pending content and saves on disconnect. Low-battery pause depends on real calibrated telemetry; it is not enabled today. The device's Wi-Fi password is never sent to the catalog or save service.

Use HTTPS for a real reachable service with CA/hostname verification and a defined trusted-clock bootstrap/recovery policy; never bypass certificate checks to make a test connect. ESP-IDF's HTTP client supports streaming reads and CA/certificate-bundle verification, while ESP-TLS provides the underlying verification options. The current loopback HTTP development service is not a deployed phone/device endpoint. [ESP HTTP client](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/protocols/esp_http_client.html), [ESP-TLS](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/protocols/esp_tls.html)

Verify the content signature independently of TLS using the pinned trusted public key. ESP-IDF exposes mbedTLS ECDSA and SECP256R1 configuration support. Test the exact manifest/signature fixture on host and target; the local wire signature is IEEE-P1363 and must not be passed unmodified to an API expecting ASN.1 DER. Treat conversion and signed-byte equality as interoperability tests. Content signatures are distinct from firmware Secure Boot/OTA signing. [ESP-IDF crypto configuration](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/kconfig.html#config-mbedtls-ecp-dp-secp256r1-enabled)

## Acceptance tests and next steps

First complete local contract tests: modified signature/payload, wrong digest/size, unexpected key, incompatible version, oversized dimensions, invalid palette index, integer-overflow attempts, changed ETag during resume, incorrect Content-Range, full-200 fallback, 416, storage exhaustion and activation interrupted at each durable step. Verify the old pack/fallback and pet save remain intact. Check cache reload, corrupt cache and unavailable IndexedDB separately from gameplay-service availability.

Then compile the ESP source and one audited board adapter, profile the starter animation, implement a bounded flash loader, add Wi-Fi setup plus HTTPS, and run the same signed fixture against the device verifier. Only after that add optional SD content. Hardware tests must include power interruption, absent card, bus timeouts, display activity while writing, reconnect during a battle and measured resource high-water marks. OTA, production signing, cloud hosting and paid services are not implemented or authorized by this plan.

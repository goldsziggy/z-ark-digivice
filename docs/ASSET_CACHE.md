# Signed asset cache

Normal local gameplay now serves scenery only from `/api/assets/catalog` and
`/api/device/assets/catalog`. Three browser toy packs and ten native toy sprites
are retained for tests but excluded before reading blobs or exposing URLs. The
browser ignores old cached toy packs without deleting them and uses a named
missing-art placeholder, never Mote/Flicker as another creature's fallback.
Private Digimon artwork remains local/SD content; no public artwork is imported
or published by this change.

`includeTestFixtures: true` in the server options (or `DIGIVICE_TEST_FIXTURES=1`
for the local CLI) explicitly enables the legacy codec fixture catalog. The
signer still uses the documented public test key and refuses `NODE_ENV=production`;
this is not a production signing or enrollment mechanism. `sign-assets.ts`
builds scenery-only catalogs by default; fixture signing requires
`--include-test-fixtures`. Changed catalogs still require `--release N` higher
than the previous release. `build-assets.py` is fixture-only and requires the
same explicit flag; default native builds generate only eight scene blobs.


The browser can download optional art in bounded chunks, pause/resume across page reloads, and restore verified cached art without fetching it again. This is a working **browser/host asset implementation**. Game actions still require the local native-core service; cached art does not make the browser game run offline. No ESP asset filesystem, Wi-Fi transport, partition writer or hardware renderer is implemented here.

## Interface

The sprite names in this codec API example require explicit test-fixture mode.
Normal app code filters cached test packs before passing artwork to the renderer.

```js
import { AssetCache, FrameDecoder, verifyManifest } from './asset-cache.js';
import { IndexedDBAssetStore } from './asset-store.js';

const cache = new AssetCache({ storage: new IndexedDBAssetStore(), fetcher: fetch });
await cache.init();                           // Reverify cached art without network.
const fallback = cache.getPack('starter-v2');
const selected = cache.getActive();
const scene = cache.getActiveBackground();   // Separate from the active sprite pack.
const manifest = await verifyManifest(envelope);
await cache.installPack(envelope, 'tide-v1', {
  signal: controller.signal,
  onProgress: ({ received, total, resumed }) => {},
});
const decoder = new FrameDecoder();           // At most 64 KiB of RGBA frame data.
const frame = decoder.decode(cache.getActive(), 'sprites', 'rill', 'idle', 0);
// frame = { width, height, data: Uint8ClampedArray }
```

`cache.list()` reports installed IDs, versions, sizes and the `active`, `backgroundActive` and `required` flags. `getPack()`, `getActive()` and `getActiveBackground()` update recency in memory; the next cache mutation persists that recency before eviction. `decodeFrame()` is also exported for one bounded sprite frame at a time. Backgrounds never enter `FrameDecoder`: `backgroundJpegBytes(pack)` from `background-pack.js` validates and returns bounded JPEG bytes, and the separate background player decodes only the selected scene. Callers that keep their own canvas/clip buffers must separately bound those buffers; `FrameDecoder` cannot limit caller-owned copies.

The pinned public key is in `web/asset-public-key.js`. **It is a published RFC 6979 development fixture whose private scalar is public.** This proves signature interoperability and rejection behavior, not trusted production publishing. Production requires an owner-controlled signing key, a replaced firmware/browser public-key pin and a reviewed rotation mechanism; no production credentials were generated.

## Integrity and activation

The catalog envelope is `{keyId,payloadBase64,signature}`. The cache verifies a 64-byte IEEE-P1363 ECDSA P-256/SHA-256 signature over the exact decoded UTF-8 payload bytes before using its pack entries. It accepts manifest format 1 and rules version 1. A pack entry fixes ID, version, URL, exact byte count, SHA-256, required status and decoded RGBA cost.

The retained release-2 fixture catalog contains the three unchanged format-1 sprite packs and adds eight format-3 packs: `scene-{meadow,forest,beach,ruins,cavern,snow,volcanic,digital}-v1`. They use the same catalog, `/api/assets/packs/<id>/<version>` route, downloader and cache. Each scene pack contains `{formatVersion:3,packId,version,rulesVersion:1,license,background:{sceneId,name,encoding:"jpeg-base64",width:480,height:480,data},provenance:{source:"approved-generated-background",sourceSha256}}`. The license text is `User-approved generated art; source license unspecified`; it does not assert redistribution rights. Scene packs contain no sprite fields and declare `decodedBytes:921600`.

Downloads request 16 KiB `Range` segments. After each complete validated segment, a storage transaction commits the partial bytes and strong ETag. Retries send `If-Range`; a full `200` response replaces staged bytes rather than appending to them. Each request has an **8-second deadline covering response headers and body**, plus caller cancellation. The constructor accepts `requestTimeoutMs` from 10 to 30,000 ms for testing or tuning. An interrupted request retains the last committed chunk. No unbounded automatic retry loop runs in the background. [HTTP If-Range behavior](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/If-Range)

After the complete byte count arrives, SHA-256 is checked **before JSON parsing**. For sprite packs the parser checks dimensions, palettes, canonical base64 frame lengths, animation bounds and the signed decoded-cost declaration. For backgrounds it checks the exact schema and provenance, canonical base64, bounded JPEG segments/tables, and a single 8-bit baseline scan with dimensions exactly 480 × 480; SVG, GIF, progressive JPEG and malformed dimensions are rejected before image decode. The image decoder separately confirms dimensions and successful decoding. Only after cache validation does one transaction move the complete record, its relevant active pointer and anti-rollback boundaries together, removing staging. Failure leaves the prior active sprite and scene records available. Invalid staging is disposable and cleared for a fresh retry; valid interrupted staging remains resumable.

Signed catalog release numbers and per-pack version/hash boundaries persist across restarts. Older releases, lower versions and different content under an already installed version are rejected. These are local consistency controls, not tamper-proof monotonic counters; clearing browser data clears them. Startup rechecks each cached signature, hash and schema. Within isolated codec fixture mode, corrupt optional art can fall back to verified required starter art, and a later install can repair it without first deleting the retained bytes.

## Storage and bounds

`IndexedDBAssetStore` uses a database separate from game saves and identity. Complete records, one staged download, version boundaries and active pointers occupy one bounded state record. The existing schema-1 record accepts an optional `backgroundActive` pointer; older records without it mean no active scene. Installing a background changes that pointer without changing sprite `active`. No database reset or migration discards old records or anti-rollback boundaries. Each mutation is a single IndexedDB transaction; success is reported after its `complete` event, with strict durability requested where supported. Atomic transactions serialize competing tabs; stage identity and offset checks reject a concurrent overwrite. Browser storage eviction/clearing remains outside application control. [IndexedDB commit event](https://developer.mozilla.org/en-US/docs/Web/API/IDBTransaction/complete_event)

This deliberately small backend rewrites its bounded state record when a chunk is checkpointed. HTTP transfer is incremental, but this is **not** a flash-efficient ESP storage implementation. A future ESP backend should commit chunk extents and metadata separately, verify power-loss recovery, and atomically select the new pack within the planned 9 MiB asset partition. Browser tests do not establish flash wear, NVS durability or electrical brownout behavior.

| Bound | Implementation |
| --- | --- |
| Encoded pack | 256 KiB maximum |
| Logical cache | 1 MiB maximum, including staged bytes and a conservative metadata allowance; actual IndexedDB/browser overhead is not measured |
| Transfer segment | 16 KiB |
| Decoded manifest payload | 16 KiB; at most 16 entries |
| Format-1 categories | `sprites`, `effects`, `icons`; at most 32 assets each |
| Sprite dimensions | Square 16 × 16 or 32 × 32 pixels |
| Palette | Exactly 16 RGB565 integers; transparent index 0 |
| Frame encoding | Canonical base64, packed 4-bit indices, high nibble first |
| Animation | Known six names; 1–8 frames; 40–2,000 ms per frame |
| Format-1 whole-pack RGBA expansion | At most 2 MiB, verified but never eagerly allocated |
| Single decoded sprite frame | At most 4 KiB |
| Optional `FrameDecoder` LRU | At most 64 KiB of frame pixels |
| Format-3 background | One 480 × 480 baseline JPEG, at most 150 KiB before base64; same 256 KiB outer pack limit |
| Decoded background | 921,600 RGBA bytes per bitmap; renderer replacement can briefly retain two, plus decoder/browser overhead |

Eviction uses least-recently-used optional complete packs. It excludes the active sprite pack, active background, required packs, the starter fallback and the staging identity. If protected content consumes the budget, the install fails. It never evicts the active fallback to make a download succeed. A failed install refreshes the in-memory cache listing so evicted optional RAM art is not represented as durable.

The three sprite fixtures total **134,369 encoded bytes**: starter 61,485; tide 36,433; ember 36,451. Their calculated all-frame RGBA cost is 746,496 bytes; this is a validation ceiling, not resident pixel memory. Release 2's eleven packs total **855,893 payload bytes**. `assetCacheBytes(state)` exports the same conservative accounting used by eviction: all eleven complete records occupy **901,116 accounted bytes**, or **1,020,437 bytes** with the largest original pack staged again, leaving 28,139 bytes below 1 MiB. A streamed replacement test retained all eleven packs and the previous active scene without eviction. Measured accounting and test evidence (historical local evidence omitted). Actual browser disk allocation, native ESP renderer memory and performance remain unmeasured.

## Verification

Run:

```sh
./scripts/node.sh --test tests/asset-cache.test.mjs
```

Sixteen focused cache tests pass using the actual implementation with an atomic in-memory storage backend. They cover persisted 16 KiB resume in a new instance, stale `If-Range`, invalid signatures/checksums/schema, failed activation, protected LRU eviction, version rollback, cancellation, corrupt cached optional art, malformed range/oversized responses, stalled headers/body and retry, the native fetch receiver regression, JPEG rejection and additive old-cache upgrade with scene fallback.

An integration case verifies the real signed release-2 catalog, installs all eleven packs, decodes every creature animation through the bounded frame cache and streams a replacement of the largest scene within the accounted budget. Eight HTTP/signing tests cover immutable routes, Range/If-Range, integrity, limits and the three-to-eleven-pack release upgrade. All 24 asset test results (historical local evidence omitted). The memory backend models storage failure outcomes; it does not emulate IndexedDB internals. Browser tests separately exercise actual Chromium, native fetch and IndexedDB; see the current verification report for their recorded results. ESP compilation and physical tests remain a later gate.

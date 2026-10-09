# Legacy original test art

These legacy toy creatures are editable original codec test fixtures. They are
excluded from normal gameplay catalogs, encounter pools and missing-art fallbacks.
Generate them only with `build-assets.py --include-test-fixtures`; normal play
uses the authored Digimon roster and separately installed local art. Each creature has a 32 × 32 silhouette and its own 16-entry RGB565 palette; palette index zero is transparent. No imported character models, traced sprites, raster source images, external fonts, or third-party artwork are used. The original art and its source definitions are dedicated to the public domain under **CC0-1.0**. This dedication covers these art assets; it does not change the licence of unrelated project code.

## Roster and visual language

Dark navy outlines, bright small colour clusters, a top-left highlight and clear facial expressions keep the sprites readable over the navy battle display. Each evolution changes its silhouette and details, rather than only changing colour.

| Pack | Creature | Visual identity |
| --- | --- | --- |
| `starter-v2` | Mote | Small forest sprout with unequal leaf ears and a pale face. |
| `starter-v2` | Glint | Three-leaf crest, longer feet, and layered leaf collar. |
| `starter-v2` | Lumen | Luminous antlers, crown, jewel, and broad woodland mantle. |
| `starter-v2` | Flicker | Wild lantern moth with amber patterned wings and lilac lower wings. |
| `tide-v1` | Rill | Compact aquatic tadpole with a forked tail and coral gills. |
| `tide-v1` | Brine | Armoured reef ray with broad, ribbed sail fins. |
| `tide-v1` | Pelagia | Crowned sea dragon with a curled tail and sweeping fins. |
| `ember-v1` | Cinder | Round ember creature with a hooked tail and flame crown. |
| `ember-v1` | Scoria | Split horns, jagged shoulders, and warm belly plates. |
| `ember-v1` | Pyrel | Sweeping fire wings and a pointed flame mantle. |

The packs expose art, not rules. Historical fixture rules supported Mote → Glint → Lumen, Rill → Brine → Pelagia and Cinder → Scoria → Pyrel plus Flicker. These are not current production evolution routes. Downloading or replacing art never changes progression or collection state.

Each creature has **17 distinct frames**: idle 4, attack 3, hurt 2, sleep 2, care 2, celebrate 4. Eye expressions and appendage poses are authored per state. Small integer translations provide breathing, wind-up, recoil and celebration movement. There is no scaling filter or interpolated runtime artwork.

The starter pack also contains 14 original 16 × 16 icons: `care`, `feed`, `play`, `rest`, `steps`, `card`, `wifi`, `download`, `update`, `recovery`, `back`, `settings`, `battery`, `heart`. Four animated effects are included: `spark` (16 × 16, 4 frames), `capture` (32 × 32, 6), `hit` (16 × 16, 3), and `heart` (16 × 16, 4). An icon's existence does not claim that its physical hardware capability is supported.

## Packed format

Each pack is canonical UTF-8 JSON with top-level `formatVersion: 1`, `packId`, `version: 1`, `license: "CC0-1.0"`, `paletteEncoding: "rgb565"`, `sprites`, `effects`, and `icons`. `starter-v2` identifies the second starter-art generation; its immutable content version starts at 1.

Each asset contains `name`, `family`, `stage`, `width`, `height`, `palette`, `transparentIndex: 0`, and `animations`. Each animation contains `frameMs` and an array of base64 `frames`. A frame decodes directly to packed **4-bit palette indexes, row-major, high nibble first**. There is no image-file header, PNG decoder, compression dictionary, or byte-order-dependent pixel data. The numeric 16-bit palette entries encode red bits 15–11, green 10–5, blue 4–0. A decoder writing RGB565 bytes must choose the display driver's required byte order separately.

```text
decoded byte 0: bits 7..4 = pixel (0, 0); bits 3..0 = pixel (1, 0)
32 × 32 frame: 512 packed bytes → 2,048 RGB565 bytes
16 × 16 frame: 128 packed bytes → 512 RGB565 bytes
transparent index 0: skip pixel / set alpha 0; do not draw black
```

The builder enforces exactly 16 palette entries, indexes 0–15, exact frame dimensions, 1–8 frames per animation and 40–2,000 ms frame timing. It rejects duplicate creature frames within an animation and encoded packs above 256 KiB. The art targets are tighter: starter below 160 KiB and each expansion below 128 KiB. Signing and delivery validation are separate from this art builder.

## Rebuild and inspect

```sh
# Standard library only. No new Python packages or network access.
python3 scripts/build-assets.py --include-test-fixtures --no-preview

# Fail if committed pack or budget bytes differ from editable sources.
python3 scripts/build-assets.py --include-test-fixtures --check

# Optional review sheets and animated GIF; requires an existing Pillow install.
python3 scripts/build-assets.py --include-test-fixtures --check --previews
```

Editable sources are under `assets/source/`: `pixel.py` supplies bounded integer pixel primitives; `forest.py`, `tide.py`, and `ember.py` define creature forms and six expressive poses; `interface.py` defines icons and effects. The deterministic builder adds motion, quantizes the authored colours to RGB565, packs nibbles, and measures the exact serialized bytes. It never reads previews as build inputs.

Review outputs under `assets/packs/previews/` include:

- `creature-roster.png`: all ten creatures at native 32 px and integer 4× scale.
- `creature-animation.gif`: all six states, with all ten creatures displayed at 4× scale.
- `<pack>-contact.png`: each creature's six poses at 4× scale.
- `<pack>-frames.png`: every creature frame at 2× scale.
- `interface-contact.png`: all icons at 4× and effects at 3×.

These PNG/GIF files are review evidence only. Runtime consumers use the packed JSON. Review rendering decodes those same packed bytes, so the proof includes the actual RGB565 quantization rather than a higher-colour source approximation.

## Measured budgets

Exact generated values are in `assets/packs/budget.json`. JSON size includes base64 expansion, palette values, names and metadata. The decoded totals below represent *all frames at once* as an upper bound, not required residency.

| Pack | JSON bytes | Frames | Packed pixel bytes | All-frame RGB565 bytes | All-frame RGBA bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `starter-v2` | 61,485 | 99 | 41,088 | 164,352 | 328,704 |
| `tide-v1` | 36,433 | 51 | 26,112 | 104,448 | 208,896 |
| `ember-v1` | 36,451 | 51 | 26,112 | 104,448 | 208,896 |
| **Total** | **134,369** | **201** | **93,312** | **373,248** | **746,496** |

Of the 201 frames, 170 are creature frames, 17 are effects and 14 are icons. One active 32 × 32 frame needs 512 packed bytes, 2,048 RGB565 bytes or 4,096 RGBA bytes. Four decoded active frames need 8,192 RGB565 bytes or 16,384 RGBA bytes. These are measured pixel payloads and exclude palette storage, JSON objects, cache records, allocations, display buffers and decoder overhead. Each asset's palette adds 32 bytes in a native 16-bit representation.

The three JSON packs occupy about 131.2 KiB before filesystem or cache metadata. Firmware transfer speed, flash layout, DMA format, display timing and physical memory residency remain hardware-integration measurements, not claims established by these art fixtures.

# Local sprite-sheet import

`scripts/import-sprite-sheet.py` converts explicitly mapped local PNG sheets into a personal indexed pack and matching provenance/coverage sidecar. It never downloads, uploads, signs, publishes or edits the original sheets. Outputs stay under ignored `.personal-assets/`; the normal signed original-art catalog is separate.

The converter needs Python and Pillow. An existing verified runtime on this Mac is:

```sh
python3 -B \
  scripts/import-sprite-sheet.py \
  --manifest .personal-assets/digimon-world-ds.mapping.json \
  --preview
```

No installation is required on this Mac. On another machine, use an already configured Python with Pillow. The tool refuses to run until `.personal-assets/` is in the repository’s `.gitignore`.

The default output is `.personal-assets/<packId>/`. `--output` can choose another child of `.personal-assets`, but tracked output locations are refused. The two import files are:

- `pack.json`: packed 4-bit indexes, RGB565 palettes, binary transparency, and the existing six-animation schema. Personal packs retain `LicenseRef-Personal-Use-Restrictions`.
- `provenance.json`: exact pack SHA-256 and byte count, decoded cost, source-sheet hashes/dimensions, creator/source/game/rights/reuse notes, conversion information and per-animation coverage.

`--preview` adds one private contact sheet per creature. Preview captions identify genuine mapped frames and explicit fallbacks. The importer does not claim that user-supplied rights notes grant permission or that a source pose has a particular game meaning.

## Mapping a sheet

Paths are relative to the mapping file’s directory. PNGs must remain beneath that directory; absolute paths, `..` traversal and escaping symlinks are rejected. Rectangles use top-left `[x, y, width, height]` coordinates. No automatic source-frame detection, background inference or pose generation occurs. Transparent-margin alignment is described below.

```json
{
  "formatVersion": 1,
  "packId": "personal-example",
  "version": 1,
  "frameSize": 64,
  "provenance": {
    "creator": "Credited sheet creator or uploader",
    "sourceUrl": "https://example.com/source-sheet",
    "game": "Source game or original work",
    "rights": "Record the actual rights statement and any uncertainty",
    "reuseScope": "Record the intended local use and applicable restrictions"
  },
  "sprites": {
    "mote": {
      "name": "Displayed creature name",
      "family": "source-game",
      "stage": 1,
      "source": "source/creature.png",
      "resize": "none",
      "transparentColor": "#FF00FF",
      "animations": {
        "idle": {
          "frameMs": 180,
          "rects": [[0, 0, 32, 32], [32, 0, 32, 32]],
          "sourceDescription": "Two left-facing movement frames repurposed as idle"
        },
        "attack": {"fallback": "idle"},
        "hurt": {"fallback": "idle"},
        "sleep": {"fallback": "idle"},
        "care": {"fallback": "idle"},
        "celebrate": {"fallback": "idle"}
      }
    }
  }
}
```

All six states must appear. Genuine mappings provide `frameMs` and 1–8 rectangles. Optional `sourceDescription` records what the source frames actually show and any repurposing. A missing state must explicitly declare `{"fallback":"idle"}`; fallback chains are rejected. Idle itself must contain genuine source rectangles. A one-frame mapped pose remains static; the importer adds no artificial movement.

Coverage labels genuine mappings with the number of selected original frames. A fallback records `kind:"reused-fallback"`, `originalFrameCount:0`, `sourceAnimation:"idle"` and its output frame count. This does not claim the source sheet lacks other unmapped animations. Per-sprite `provenance` can override the complete five-field global provenance when sheets have different credits or URLs.

The current personal browser view accepts the existing logical appearance slots `mote`, `glint`, `lumen`, `flicker`, `rill`, `brine`, `pelagia`, `cinder`, `scoria`, and `pyrel`. The converter itself supports up to 32 mapped creatures; the browser currently accepts at most these ten known slots. Appearance replacement does not rename the game rules, change evolution requirements or unlock a new creature.

## Pixel preservation and conversion

`frameSize` is 32 or 64, defaulting to 32. A fitting source rectangle is first placed on a transparent square without stretching or enlarging its pixels. By default, one common alpha bounding box is computed across **all genuine frames of the creature**. Those frames move together to bottom-center the shared visible area. Only outer transparent padding is trimmed; every opaque pixel and each frame’s motion relative to the others is preserved. A 32 × 64 source crop fits the 64-pixel canvas without downsampling. Set per-creature `anchor:"center"` to retain the original centered rectangle and its transparent margins. The sidecar records the common bounds, shared shift and original/final offsets; frames are never independently trimmed in a way that would remove intentional animation movement.

Oversized crops fail unless that creature explicitly sets `resize:"nearest"`. In that case, the converter chooses an integer downsample factor, transparently pads the source to a multiple of that factor, then uses nearest-neighbor reduction. This retains square pixel proportions and records the padding, factor, resulting content size and offset. Downsampling necessarily loses detail; it is never automatic. [Pillow image operations](https://pillow.readthedocs.io/en/stable/reference/Image.html)

An optional `transparentColor` removes exact RGB matches, including matching interior pixels. Choose it deliberately; the importer never guesses a background or flood-fills. Existing PNG alpha is converted to binary transparency at `alphaThreshold`, default 128. Index 0 is transparent; opaque black remains a separate nonzero palette index.

A single shared palette per creature covers its genuine animation frames. RGB colors first convert to RGB565. If there are more than 15 opaque RGB565 colors, deterministic weighted median-cut reduction produces at most 15 colors; nearest-color assignment uses stable tie-breaking and no dithering. The sidecar reports color counts and RGB565-to-palette error. Fallback repetitions do not bias palette selection. Output JSON has stable ordering and no timestamps.

## Bounds and local verification

| Resource | Limit |
| --- | --- |
| Mapping JSON | 128 KiB |
| Source PNG | Static PNG only; 16 MiB; each dimension at most 4,096 |
| Crop rectangle | Each dimension at most 256; must fit inside the PNG |
| Creatures / animation frames | At most 32 creatures; 1–8 frames per genuine state |
| Playback timing | 40–2,000 ms per frame |
| Personal frame | 32 × 32 or 64 × 64 |
| Encoded pack / sidecar | 256 KiB / 64 KiB |
| All-frame decoded RGBA cost | 2 MiB maximum; one 64-pixel frame is 16 KiB |

All conversion and validation complete before output files are replaced. A symlinked `.personal-assets` root is rejected. Every planned pack, provenance and preview destination is checked against all source PNG and mapping paths before any write, preventing source-file replacement. Each file uses atomic replacement; the pack is published last. The pair is not one filesystem transaction: an interrupted replacement can leave mismatched files, which the browser rejects using the sidecar hash. Source PNGs are never rewritten.

Run the focused tests with the same existing Pillow runtime:

```sh
python3 -B \
  -m unittest discover -s tests -p test_sprite_import.py -v
```

Twelve tests pass using original synthetic pixels generated only in temporary directories. They cover native tall-frame preservation, honest fallback requirements, explicit resizing, alpha/keyed transparency and opaque black, deterministic color reduction, crop/file/image bounds, source path escapes, private output restrictions, decoded-memory limits, provenance hashes and duplicate JSON keys. Regression checks also cover a symlinked private root, source/output filename collisions, shared-baseline anchoring without pixel loss, and Unicode text limits round-tripped through the browser validator using the repository’s Node runtime.

The current local Digimon World DS example also passes the browser’s `validatePersonalImport`: three appearance slots, 138,231 encoded bytes, 10,494-byte sidecar and 819,200 bytes of calculated all-frame RGBA data. It maps 11 genuine selected source frames into 50 runtime frames through explicitly declared idle fallbacks. All spatial downsample factors are 1. Agumon and Greymon retain their 15 opaque RGB565 colors; MetalGreymon’s 18 opaque colors reduce to 15. Overworld movement frames are explicitly described as repurposed idle, and the two single-frame raised-arms poses are described as static celebrations. The actual sheets, mapping, previews and generated files remain private and ignored; none belong in the repository’s distributable assets or Library.

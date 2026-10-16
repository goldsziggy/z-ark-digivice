# z-ark Digivice

**[Explore the live showcase](https://goldsziggy.github.io/z-ark-digivice/)** — real-device screen close-ups, labeled native simulator samples and an exploded view of the current C14-P19 enclosure. The samples document firmware baseline `f74ee4c`; later firmware changes are outside this showcase.

**[Buy me a coffee](https://buymeacoffee.com/goldsziggy)**

An offline virtual-pet game, local development service and ESP32-S3 firmware, with C14-P19 enclosure CAD. This is a source export of the working prototype. It contains original artwork only as explicit legacy test fixtures; original personal import packs, device saves, credentials and historical device evidence are excluded. The playable showcase includes a bounded set of optimized in-game art; see its scoped [artwork notices](docs/play/ART_SOURCES.md).

The shared deterministic C++ core handles care, walking encounters, Tactical/Auto battles, capture, collection and explicit evolution. A Node/TypeScript service supplies bounded asset catalogs, browser pairing and replay-validated save revisions. The round browser simulator uses the same core. Immediate handheld play works without a phone or network; native service pairing/outbox/save synchronization is still future work. Local Nearby uses ESP-NOW independently of cloud pairing.

## Current source

The current core is **schema 30 / rules 22**, with **6,876-byte snapshots**, a **level cap of 99**, and **250 carried members**. It includes the 465-form catalog, care-independent Digivolution, three-tier solo and co-op dungeons, Manual/Auto battles, the stronger-basic-attack Auto policy, trading, and Digi Modify support. Frozen rules 19, 20 and 21 engines preserve historical replay before forward migration. The service uses store format 24, and the standalone browser uses the same current core. Home shows stacked XP and HP meters. Firmware supports the 1.46-inch Waveshare SPI/PCM board and the flat 2.1-inch RGB/CST820 board with bounded buzzer output and PN532 diagnostics. See [current integration and verification scope](docs/INTEGRATION_20261016.md), [screen idle](docs/SLEEP_POLICY.md), and [Nearby protocol](docs/NEARBY_PROTOCOL.md). The packages under `releases/` are historical installation records; build current source for the current game. Keep each device's own backup before any separately authorized installation.

The source/build checkpoint and any physical installation result are recorded separately in [publication validation](PUBLICATION_VALIDATION.json). Existing showcase media intentionally remains labeled as the earlier `f74ee4c` demonstration. The C14-P19 CAD and print downloads are unchanged.

## Run locally

Requires an existing C++17 compiler, CMake 3.16+ and Node 24.12+. No package installation is needed for the core development service.

```sh
npm run dev
# Open http://127.0.0.1:8787
```

`npm run dev:direct` provides the existing compiler-only alternative when CMake is unavailable. The service binds to loopback by default. It creates private local `.data` state; that directory must not be committed. Asset catalog signatures use an explicitly public development test key, not production trust. Optional remote Garage access requires an explicitly configured environment file; none is bundled or auto-discovered in another project.

```sh
npm run test:publication
npm run test:browser:party # optional actual-Chrome check; existing Playwright setup required
```

The publication check builds the host core and runs the source-only test scope. Tests requiring omitted background JPEG/pack fixtures remain separate; the full historical `npm test` is not the test command for this reduced asset export. See [export contents and verification boundaries](PUBLICATION.md).

## Firmware and hardware

Current target: **Waveshare ESP32-S3-Touch-LCD-1.46 standard glass, SKU29565**, 412×412, 16 MiB flash and 8 MiB PSRAM. Touch, onboard motion/audio and microSD are configured. External gameplay buttons, PN532 and GPS are not required for the first touch-only build. NFC and GPS remain disabled until their real drivers and wiring are verified.

Firmware builds with an existing official ESP-IDF **5.3.6** toolchain. Set `DIGIVICE_IDF_PATH`, `DIGIVICE_IDF_TOOLS_PATH` and, if necessary, `DIGIVICE_IDF_PYTHON_ENV` to your installation before running:

```sh
npm run build:esp:waveshare
```

That command builds only. The current package is the [6a3bd5c firmware build](releases/firmware-6a3bd5c/README.md). The historical [53686e6](releases/firmware-53686e6/README.md), [171cda7](releases/firmware-171cda7/README.md) and [f74](releases/firmware-f74ee4c/README.md) packages remain beside it. Each includes hashes and installation limits. No device backups or private assets are included. The historical guarded flash helper is pinned to an older release and cannot install this package. [Firmware setup](docs/FIRMWARE.md) · [Connectors](docs/HARDWARE_CONNECTORS.md) · [Optional NFC](docs/NFC_OPTIONAL.md).

[Current C14-P19 CAD and print parts](hardware/README.md) preserve both touch-only and two-button fronts. C14-P19 enlarges only the main white front's receiving pilots to 1.9 mm; common black/red parts and small white RF windows remain unchanged. [Six downloadable plate/G-code ZIPs](hardware/c14-p19/downloads/README.md) include both repair-front and full-white options for Kobra S1 / PLA / 0.4 mm. The [assembly and screw guide](hardware/c14-p19/docs/z-ark_Digivice_C14_P19_Assembly_and_Screws.pdf) covers the 17-screw first build and optional 20-screw NFC-tray assembly.

Required vendor reference meshes are external acquisition prerequisites for rebuilding the CAD; they are not bundled. Physical fit, screw grip, battery compatibility and RF behavior still require hardware validation. C13 white fronts and white plates are superseded.

## Build buylist

Parts for one **C14-P19 touch-first build**, using the same items and links as the [showcase parts list](https://goldsziggy.github.io/z-ark-digivice/#parts). The board listing is verified; supplies use labeled searches. No affiliate links or price/stock guarantees.

1. **Touchscreen board, 1:** [Waveshare ESP32-S3-Touch-LCD-1.46 on Amazon](https://www.amazon.com/dp/B0DRJBVQ3X), standard protective-cover **SKU 29565**, 412 × 412. Confirm the selected option; 1.46B and 1.46C are different variants.
2. **Battery candidate, 1:** [Amazon search for protected 1S 3.7 V LiPo packs](https://www.amazon.com/s?k=103665+3.7V+protected+lipo+MX1.25). The CAD assumes a **36 × 67 × 10 mm** cell envelope. No exact pack is qualified: verify the complete pack, leads, charger compatibility, MX1.25 two-pin connector and polarity before ordering.
3. **M2 screws, 17 total:** **3 × M2×4, 10 × M2×8 and 4 × M2×16 mm**. [Amazon search for a screw kit](https://www.amazon.com/s?k=M2+button+head+screw+kit+4mm+8mm+16mm). Modeled head limits are Ø4 × 2 mm; actual heads and retention need checking against the [assembly schedule](hardware/c14-p19/ASSEMBLY.md#screws-and-remaining-checks).
4. **Printed enclosure:** **1.75 mm PLA in black, white and red**. [Amazon search for filament](https://www.amazon.com/s?k=PLA+1.75mm+black+white+red) · [C14-P19 print files](hardware/c14-p19/downloads). Use the touch-only front and matching common parts. Published toolpaths are for the Kobra S1 with a 0.4 mm nozzle; use the STLs for other printers.
5. **microSD card, 1 for the SD asset pack:** [Amazon search](https://www.amazon.com/s?k=microSD+card). Showcased firmware supports FAT32/exFAT on MBR or unpartitioned cards, not GPT. No brand or capacity is qualified; resident fallback content remains available without a card.
6. **USB-C data cable, 1:** [Amazon search](https://www.amazon.com/s?k=usb+c+data+cable). Choose the host end for your computer and check that the plug housing clears the enclosure. No specific cable is fit-qualified.

Physical enclosure fit, battery compatibility and screw grip remain unverified. Follow the [full parts sources and compatibility notes](docs/PARTS.md), including optional NFC and alternate-front buttons. MagSafe is outside the current C14-P19 build.

## Current readiness

Current host snapshots use schema 30/rules 22 and 6,876 bytes. Host and browser checks establish software behavior; firmware compilation does not establish physical acceptance. The 2.1-inch buzzer fix verifies bounded register transitions, but audible output remains unconfirmed. PN532 transport was verified on one board with a valid no-card response; another reader still returned no bytes. No physical UID capture or universal hardware readiness is claimed. This source integration performs no device operations.

Historical installation results remain under `releases/`. They describe the named older packages and do not identify the current build. See [current integration notes](docs/INTEGRATION_20261016.md) for validation and known fixture limits.

Original third-party import packs remain absent. The playable showcase includes browser-optimized in-game sprite pixels and scenery with [source attribution](docs/play/ART_SOURCES.md); this does not grant reuse rights to third-party artwork. Recorded gameplay and native rendered demonstrations retain their separate provenance. Normal browser/device asset catalogs therefore advertise **zero downloadable packs**; missing exact-form art uses the neutral missing-art display. The three original browser packs and ten original device blobs remain explicit legacy test fixtures, never production starters. [Publication boundaries](PUBLICATION.md) · [Service API](service/API.md) · [Architecture](docs/ARCHITECTURE.md) · [Current Home controls](docs/HOME_CAROUSEL.md) · [Battle fixes](docs/BATTLE_ART_RELEASE.md) · [Nearby protocol](docs/NEARBY_PROTOCOL.md).

No project-wide license has been selected. Existing scoped licenses, including CC0 original artwork and vendored component notices, remain attached to their respective material. No third-party rights are reassigned by this export.

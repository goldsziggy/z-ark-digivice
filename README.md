# z-ark Digivice

**[Explore the live showcase](https://goldsziggy.github.io/z-ark-digivice/)** — real-device screen close-ups, labeled native simulator samples and an exploded view of the current C14-P19 enclosure. The samples document firmware baseline `f74ee4c`; later firmware changes are outside this showcase.

**[Buy me a coffee](https://buymeacoffee.com/goldsziggy)**

An offline virtual-pet game, local development service and ESP32-S3 firmware, with C14-P19 enclosure CAD. This is a source export of the working prototype. It contains original artwork only as explicit legacy test fixtures; original personal import packs, device saves, credentials and historical device evidence are excluded. The playable showcase includes a bounded set of optimized in-game art; see its scoped [artwork notices](docs/play/ART_SOURCES.md).

The shared deterministic C++ core handles care, walking encounters, Tactical/Auto battles, capture, collection and explicit evolution. A Node/TypeScript service supplies bounded asset catalogs, browser pairing and replay-validated save revisions. The round browser simulator uses the same core. Immediate handheld play works without a phone or network; native service pairing/outbox/save synchronization is still future work. Local Nearby uses ESP-NOW independently of cloud pairing.

## Current source

The current core is **schema 23 / rules 16**. Snapshots are **3,216 bytes**. The level cap is **50**, and a Digimon keeps its level through Digivolution. Selected XP companions still receive the full wild victory or capture XP, and they also receive that result's bond once. A new duplicate merges into the oldest owned copy of that exact form and grants bonus XP `20 + 6 × wild level` instead of taking a slot. Failed captures stay in the same battle for three attempts. Auto battles start on their own; **Run Away** remains, and capture stays a manual throw. Critical hits are a 5% chance at 1.5× damage. Useful feeding, play, rest, and toileting raise bond or care and grant 2 XP when that action is off cooldown. Powered-off and overnight time are not punished. Earlier saves migrate with their earned progress. The last packaged device firmware remains [171cda7](releases/firmware-171cda7/README.md); keep each device's own pre-upgrade backup before installing a newer build. [XP companion guide](docs/XP-COMPANIONS.md) · [Collection and resource guide](docs/ROSTER60.md) · [Graded capture](docs/GRADED_CAPTURE.md) · [Nearby trading](docs/NEARBY_TRADING.md).

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

That command builds only. The last installed package is the [171cda7 firmware build](releases/firmware-171cda7/README.md). The historical [f74 package](releases/firmware-f74ee4c/README.md) remains beside it. Both include hashes and installation limits. No device backups or private assets are included. The historical guarded flash helper is pinned to an older release and cannot install this package; a release-specific installer review is required. [Firmware setup](docs/FIRMWARE.md) · [Connectors](docs/HARDWARE_CONNECTORS.md) · [Optional NFC](docs/NFC_OPTIONAL.md).

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

Current host snapshots use schema 23/rules 16 and 3,216 bytes. Native `State` is 3,188 bytes on this host. The level cap is 50, with levels 1–20 unchanged from the previous curve. The export includes the full current volume, music, trading and capture implementations. Host tests establish the software check; a new ESP package and physical installation are separate from that check.

The previous application-only `171cda7` update remains the last verified device installation: those saves migrated to 2,964 bytes, settings were preserved, and all 261 SD files (4,700,573 bytes per device) verified without asset writes. [Sanitized installation result](releases/firmware-171cda7/installation-summary.json). This rules 16 source migrates that save forward. It has not replaced those installed images until a newer package is flashed.

These checks cover existing profiles and data/reboot integrity. New companion controls, a full 60-member collection, long walking accuracy and Nearby trade stress have not passed physical acceptance. Earlier false steps while stationary remain unresolved. Publication tooling itself accesses no board. See [publication validation](PUBLICATION_VALIDATION.json) for scope and limits.

Original third-party import packs remain absent. The playable showcase includes browser-optimized in-game sprite pixels and scenery with [source attribution](docs/play/ART_SOURCES.md); this does not grant reuse rights to third-party artwork. Recorded gameplay and native rendered demonstrations retain their separate provenance. Normal browser/device asset catalogs therefore advertise **zero downloadable packs**; missing exact-form art uses the neutral missing-art display. The three original browser packs and ten original device blobs remain explicit legacy test fixtures, never production starters. [Publication boundaries](PUBLICATION.md) · [Service API](service/API.md) · [Architecture](docs/ARCHITECTURE.md) · [Current Home controls](docs/HOME_CAROUSEL.md) · [Battle fixes](docs/BATTLE_ART_RELEASE.md) · [Nearby protocol](docs/NEARBY_PROTOCOL.md).

No project-wide license has been selected. Existing scoped licenses, including CC0 original artwork and vendored component notices, remain attached to their respective material. No third-party rights are reassigned by this export.

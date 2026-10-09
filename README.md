# z-ark Digivice

**[Explore the live showcase](https://goldsziggy.github.io/z-ark-digivice/)** — real-device screen close-ups, labeled native simulator samples and an exploded view of the current C14-P19 enclosure. The samples document firmware baseline `f74ee4c`; later firmware changes are outside this showcase.

An offline virtual-pet game, local development service and ESP32-S3 firmware, with C14-P19 enclosure CAD. This is a source export of the working prototype. It contains original artwork only as explicit legacy test fixtures; original personal import packs, device saves, credentials and historical device evidence are excluded. The playable showcase includes a bounded set of optimized in-game art; see its scoped [artwork notices](docs/play/ART_SOURCES.md).

The shared deterministic C++ core handles care, walking encounters, Tactical/Auto battles, capture, collection and explicit evolution. A Node/TypeScript service supplies bounded asset catalogs, browser pairing and replay-validated save revisions. The round browser simulator uses the same core. Immediate handheld play works without a phone or network; native service pairing/outbox/save synchronization is still future work. Local Nearby uses ESP-NOW independently of cloud pairing.

## Current source

Source `171cda7` adds **three XP companions** beyond the active partner, retaining the collection of **60 Digimon**, graded ring capture, independent world seeds, sound controls and nearby trading. Each selected companion receives the full wild victory/capture XP reward without fighting or reducing the partner’s reward. Schema 22/rules 15 migrates earlier saves with no XP companions selected, preserving members, seeds and the active partner. Old firmware cannot read the expanded save; keep each device's own pre-upgrade backup. [XP companion guide](docs/XP-COMPANIONS.md) · [Collection and resource guide](docs/ROSTER60.md) · [Graded capture](docs/GRADED_CAPTURE.md) · [Nearby trading](docs/NEARBY_TRADING.md).

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

That command builds only. The current [171cda7 firmware build images and manifest](releases/firmware-171cda7/README.md) and historical [f74 package](releases/firmware-f74ee4c/README.md) are included with hashes and installation limits. No device backups or private assets are included. The historical guarded flash helper is pinned to an older release and cannot install this package; a release-specific installer review is required. [Firmware setup](docs/FIRMWARE.md) · [Connectors](docs/HARDWARE_CONNECTORS.md) · [Optional NFC](docs/NFC_OPTIONAL.md).

[Current C14-P19 CAD and print parts](hardware/README.md) preserve both touch-only and two-button fronts. C14-P19 enlarges only the main white front's receiving pilots to 1.9 mm; common black/red parts and small white RF windows remain unchanged. [Six downloadable plate/G-code ZIPs](hardware/c14-p19/downloads/README.md) include both repair-front and full-white options for Kobra S1 / PLA / 0.4 mm. The [assembly and screw guide](hardware/c14-p19/docs/z-ark_Digivice_C14_P19_Assembly_and_Screws.pdf) covers the 17-screw first build and optional 20-screw NFC-tray assembly.

Required vendor reference meshes are external acquisition prerequisites for rebuilding the CAD; they are not bundled. Physical fit, screw grip, battery compatibility and RF behavior still require hardware validation. C13 white fronts and white plates are superseded.

## Current readiness

Game and firmware source is frozen at **`171cda7e698cf2915b50aa46bc766d8d1d0ee50d`**. Current snapshots use schema 22/rules 15 and 2,964 bytes, with three optional XP companions. The export includes the full current volume, music, trading and capture implementations. Host tests and the supplied ESP build establish software/build checks; physical installation and playtest acceptance are separate milestones.

Both existing devices passed the application-only `171cda7` update: their own saves migrated exactly to 2,964 bytes, settings were preserved, and all 261 SD files (4,700,573 bytes per device) verified without asset writes. Each save remained byte-identical through checkpoint and reboot, with zero gameplay events. The protected flash region matched before boot; all serial handles are closed. The three XP companion slots began empty. [Sanitized installation result](releases/firmware-171cda7/installation-summary.json).

These checks cover existing profiles and data/reboot integrity. New companion controls, a full 60-member collection, long walking accuracy and Nearby trade stress have not passed physical acceptance. Earlier false steps while stationary remain unresolved. Publication tooling itself accesses no board. See [publication validation](PUBLICATION_VALIDATION.json) for scope and limits.

Original third-party import packs remain absent. The playable showcase includes browser-optimized in-game sprite pixels and scenery with [source attribution](docs/play/ART_SOURCES.md); this does not grant reuse rights to third-party artwork. Recorded gameplay and native rendered demonstrations retain their separate provenance. Normal browser/device asset catalogs therefore advertise **zero downloadable packs**; missing exact-form art uses the neutral missing-art display. The three original browser packs and ten original device blobs remain explicit legacy test fixtures, never production starters. [Publication boundaries](PUBLICATION.md) · [Service API](service/API.md) · [Architecture](docs/ARCHITECTURE.md) · [Current Home controls](docs/HOME_CAROUSEL.md) · [Battle fixes](docs/BATTLE_ART_RELEASE.md) · [Nearby protocol](docs/NEARBY_PROTOCOL.md).

No project-wide license has been selected. Existing scoped licenses, including CC0 original artwork and vendored component notices, remain attached to their respective material. No third-party rights are reassigned by this export.

# z-ark Digivice

An offline virtual-pet game, local development service and ESP32-S3 firmware, with C14-P19 enclosure CAD. This is a source export of the working prototype. It contains original artwork only as explicit legacy test fixtures; personal sprite packs, supplied scenery, device saves, credentials and historical device evidence are excluded.

The shared deterministic C++ core handles care, walking encounters, Tactical/Auto battles, capture, collection and explicit evolution. A Node/TypeScript service supplies bounded asset catalogs, browser pairing and replay-validated save revisions. The round browser simulator uses the same core. Immediate handheld play works without a phone or network; native service pairing/outbox/save synchronization is still future work. Local Nearby uses ESP-NOW independently of cloud pairing.

## Run locally

Requires an existing C++17 compiler, CMake 3.16+ and Node 24.12+. No package installation is needed for the core development service.

```sh
npm run dev
# Open http://127.0.0.1:8787
```

`npm run dev:direct` provides the existing compiler-only alternative when CMake is unavailable. The service binds to loopback by default. It creates private local `.data` state; that directory must not be committed. Asset catalog signatures use an explicitly public development test key, not production trust. Optional remote Garage access requires an explicitly configured environment file; none is bundled or auto-discovered in another project.

```sh
npm run test:publication
```

The publication check builds the host core and runs the source-only test scope. Tests requiring omitted background JPEG/pack fixtures remain separate; the full historical `npm test` is not the test command for this reduced asset export. See [export contents and verification boundaries](PUBLICATION.md).

## Firmware and hardware

Current target: **Waveshare ESP32-S3-Touch-LCD-1.46 standard glass, SKU29565**, 412×412, 16 MiB flash and 8 MiB PSRAM. Touch, onboard motion/audio and microSD are configured. External gameplay buttons, PN532 and GPS are not required for the first touch-only build. NFC and GPS remain disabled until their real drivers and wiring are verified.

Firmware builds with an existing official ESP-IDF **5.3.6** toolchain. Set `DIGIVICE_IDF_PATH`, `DIGIVICE_IDF_TOOLS_PATH` and, if necessary, `DIGIVICE_IDF_PYTHON_ENV` to your installation before running:

```sh
npm run build:esp:waveshare
```

That command builds only. The exact previously installed [f74 firmware images and manifest](releases/firmware-f74ee4c/README.md) are included with their hashes and installation limits. No device backups or private assets are included. The historical guarded flash helper is pinned to an older release and cannot install this package; a release-specific installer review is required. [Firmware setup](docs/FIRMWARE.md) · [Connectors](docs/HARDWARE_CONNECTORS.md) · [Optional NFC](docs/NFC_OPTIONAL.md).

[Current C14-P19 CAD and print parts](hardware/README.md) preserve both touch-only and two-button fronts. C14-P19 enlarges only the main white front's receiving pilots to 1.9 mm; common black/red parts and small white RF windows remain unchanged. [Six downloadable plate/G-code ZIPs](hardware/c14-p19/downloads/README.md) include both repair-front and full-white options for Kobra S1 / PLA / 0.4 mm. The [assembly and screw guide](hardware/c14-p19/docs/z-ark_Digivice_C14_P19_Assembly_and_Screws.pdf) covers the 17-screw first build and optional 20-screw NFC-tray assembly.

Required vendor reference meshes are external acquisition prerequisites for rebuilding the CAD; they are not bundled. Physical fit, screw grip, battery compatibility and RF behavior still require hardware validation. C13 white fronts and white plates are superseded.

## Current readiness

Game/firmware source is frozen at **`f74ee4c132b4b9bc6407d99bbc20b13af9dfd962`**, the firmware verified on both project units. It includes the Home carousel, walking across screens, one waiting encounter, production roster, corrected sprite facing/animation selection and tap alternatives for horizontal swipes. Current snapshots use schema 17 / rules 13 / 652 bytes. Later volume, music and trading work is excluded.

Prior installation verified each unit's fresh save, existing SD asset hashes and reboot persistence. Unit 2's earlier USB silence did not recur during that bounded check. Physical touch, gameplay, walking, audio, power and two-device RF acceptance remain open. No board was reset, flashed or accessed for this export.

Private Digimon art and scenery are absent. Normal browser/device asset catalogs therefore advertise **zero downloadable packs**; missing exact-form art uses the neutral missing-art display. The three original browser packs and ten original device blobs remain explicit legacy test fixtures, never production starters. [Publication boundaries](PUBLICATION.md) · [Service API](service/API.md) · [Architecture](docs/ARCHITECTURE.md) · [Current Home controls](docs/HOME_CAROUSEL.md) · [Battle fixes](docs/BATTLE_ART_RELEASE.md) · [Nearby protocol](docs/NEARBY_PROTOCOL.md).

No project-wide license has been selected. Existing scoped licenses, including CC0 original artwork and vendored component notices, remain attached to their respective material. No third-party rights are reassigned by this export.

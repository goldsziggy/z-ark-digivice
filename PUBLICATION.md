# Source publication contents

This curated public export is prepared for **goldsziggy/z-ark-digivice**. Public history includes `f12027d` and the battle-demo animation at `50f658e`. Current gameplay source is schema 23 / rules 16. The current device package is firmware `53686e6`. The previous device package remains firmware `171cda7`. Development-only history that never reached this public repository is not reconstructed here.

The later `/play/` showcase update adds only browser-optimized in-game artwork with [scoped credits and provenance](docs/play/ART_SOURCES.md). The frozen firmware/source export boundaries below describe release `4466aa1`; its service catalogs and original import packs remain unchanged.

## Included

- The deterministic C++ game core, frozen replay executors, ESP firmware adapters, native touch UI, TypeScript service and browser simulator.
- Current capacity 60/schema 23/rules 16, with level cap 50, three XP companions that also earn bond once, duplicate merges, three-attempt capture that stays in the battle, automatic Auto battles, critical hits, and care actions that grant small XP.
- Hash-verified [53686e6 firmware build images](releases/firmware-53686e6/README.md), alongside the historical [171cda7](releases/firmware-171cda7/README.md) and [f74](releases/firmware-f74ee4c/README.md) packages. The `53686e6` images are the rules 16 device build. The older packages stay as their own installation records.
- Source tests, roster research and acquisition references, original CC0 fixture sprites and scoped component notices.
- Unchanged C14-P19 editable CAD, 11 print STLs, six-page assembly PDF, six guide images and [six native-project/G-code ZIP packages](hardware/c14-p19/downloads/README.md).
- The existing public [showcase](docs/index.html), parts sourcing, reviewed device clips and native simulator samples. Those demonstrations retain their original provenance and `f74ee4c` labels; they do not depict this newer build.

Product links remain ordinary links without affiliate tags. No project-wide license is invented; existing scoped licenses and third-party notices continue to apply.

## Content and portability boundaries

Private saves, NVS/flash backups, device identifiers, Wi-Fi settings, credentials, supplied sprite/scenery packs, historical private device logs and development Git history are excluded. Vendor CAD reference meshes/manuals remain external acquisition prerequisites. Existing public footage is retained under its documented demonstration scope; it does not grant reuse rights to third-party artwork or distribute raw sprite/scenery packs. No new gameplay footage is added by this export.

Production selection uses 266 Digimon forms and 166 authored evolution routes. The three original browser sprite packs and ten DVAs are explicit legacy test fixtures, never production starters. Missing exact-form art uses the neutral missing-art treatment. Both normal downloadable asset catalogs are empty because private art and scenery are omitted. The publication adapter permits a bounded empty device catalog while preserving allowlists, hashes, signatures and resource limits. An existing firmware cache can remain usable when native code declines an empty catalog.

Existing publication adaptations retain explicit-only Garage configuration, portable documentation, reduced asset indexes and `test:publication`. History-dependent investigation scripts remain omitted. All 428 core and firmware files are byte-identical to the frozen development source; export adaptations do not change native gameplay. [The manifest](PUBLICATION_MANIFEST.json) lists payload hashes and adaptations; [validation](PUBLICATION_VALIDATION.json) records the checked scope.

## Verification scope

Run `npm run test:publication` from this export. The completed local run of source `53686e6` passed **31 host CTest targets, 30 installer host tests and 330 Node tests**. The original private-scene fixtures remain absent: `background-decode-test`, `assets-service.test.ts`, `asset-cache.test.mjs` and `background*.test.*` are outside this reduced-content command. Publication catalog tests cover empty normal catalogs, explicit fixture opt-in, advertised downloads, hashes and missing-asset responses. These tests use synthetic temporary profiles and no hardware ports.

The current ESP application is 1,607,872 bytes, built with official ESP-IDF 5.3.6 from clean source `53686e6`, with 1,537,856 bytes free in its 3 MiB OTA slot. Host State/snapshot sizes are 3,188/3,216 bytes. Static DIRAM is 213,055 of 341,760 bytes; linker remainder is not runtime heap. The application stack chain and the synthetic JSON maximum were not remeasured for this image. The [resource summary](releases/firmware-53686e6/resource-summary.json) records those limits. The historical [171cda7 summary](releases/firmware-171cda7/resource-summary.json) keeps that package's own 1,601,056-byte application, 2,936/2,964-byte host ABI, 26,720-byte stack chain and 38,500-byte JSON sweep. Those older figures do not describe `53686e6`. Preserve each device's own backup.

Both existing devices passed the application-only `53686e6` update at `0x20000`. Before the new application ran, each NVS partition matched that board's own pre-flash backup and the on-flash assets partition was still the empty image. Bootloader, partition table and OTA metadata were not written, and the chip was not erased. A verification boot migrated both saves in RAM. That boot also checkpointed one Auto exchange on the board already in an encounter and one Home care minute on the other, so both NVS partitions were restored from those pre-flash backups and the boards were reset onto the new application. SD cards were not mounted or written. [Sanitized physical summary](releases/firmware-53686e6/installation-summary.json). The earlier [171cda7 installation](releases/firmware-171cda7/installation-summary.json) remains that package's own record, including its 261-file SD check and 22,280-byte stack observation. Those observations were not repeated for `53686e6`.

These checks cover the application write, NVS identity before boot, and the restored pre-flash saves. New companion controls, a full 60-member collection, long walking accuracy and Nearby trade stress have not passed physical acceptance. Earlier false steps while stationary remain unresolved. The publication test command itself opens no hardware port; the separate installation above did. The motion detector is unchanged. Historical installation notes describe their named firmware versions.

All 55 hardware files and 35 showcase payload/source paths are byte-identical to the latest public baseline. README and this publication note retain showcase links while updating source/build metadata. CAD generation and slicing were not rerun. No CI workflow or deployment setting is added; remote publication and existing Pages status are checked separately after an authorized push.

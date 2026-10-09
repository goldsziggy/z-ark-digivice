# Source publication contents

This curated public export is prepared for **goldsziggy/z-ark-digivice**, preserving public history through `3293352c81f92e22db7c750613d64f0ea5252c75`. Game and firmware source is frozen at **`171cda7e698cf2915b50aa46bc766d8d1d0ee50d`**. Development Git history stays local.

The later `/play/` showcase update adds only browser-optimized in-game artwork with [scoped credits and provenance](docs/play/ART_SOURCES.md). The frozen firmware/source export boundaries below describe release `4466aa1`; its service catalogs and original import packs remain unchanged.

## Included

- The deterministic C++ game core, frozen replay executors, ESP firmware adapters, native touch UI, TypeScript service and browser simulator.
- Current capacity 60/schema 22/rules 15 migration with three additional XP companions, graded capture, independent world seeds, sound controls and durable one-for-one nearby trading.
- Hash-verified [171cda7 firmware build images](releases/firmware-171cda7/README.md), alongside the unchanged historical [f74 package](releases/firmware-f74ee4c/README.md).
- Source tests, roster research and acquisition references, original CC0 fixture sprites and scoped component notices.
- Unchanged C14-P19 editable CAD, 11 print STLs, six-page assembly PDF, six guide images and [six native-project/G-code ZIP packages](hardware/c14-p19/downloads/README.md).
- The existing public [showcase](docs/index.html), parts sourcing, reviewed device clips and native simulator samples. Those demonstrations retain their original provenance and `f74ee4c` labels; they do not depict this newer build.

Product links remain ordinary links without affiliate tags. No project-wide license is invented; existing scoped licenses and third-party notices continue to apply.

## Content and portability boundaries

Private saves, NVS/flash backups, device identifiers, Wi-Fi settings, credentials, supplied sprite/scenery packs, historical private device logs and development Git history are excluded. Vendor CAD reference meshes/manuals remain external acquisition prerequisites. Existing public footage is retained under its documented demonstration scope; it does not grant reuse rights to third-party artwork or distribute raw sprite/scenery packs. No new gameplay footage is added by this export.

Production selection uses 266 Digimon forms and 166 authored evolution routes. The three original browser sprite packs and ten DVAs are explicit legacy test fixtures, never production starters. Missing exact-form art uses the neutral missing-art treatment. Both normal downloadable asset catalogs are empty because private art and scenery are omitted. The publication adapter permits a bounded empty device catalog while preserving allowlists, hashes, signatures and resource limits. An existing firmware cache can remain usable when native code declines an empty catalog.

Existing publication adaptations retain explicit-only Garage configuration, portable documentation, reduced asset indexes and `test:publication`. History-dependent investigation scripts remain omitted. All 428 core and firmware files are byte-identical to the frozen development source; export adaptations do not change native gameplay. [The manifest](PUBLICATION_MANIFEST.json) lists payload hashes and adaptations; [validation](PUBLICATION_VALIDATION.json) records the checked scope.

## Verification scope

Run `npm run test:publication` from this export. The completed local run passed **30 host CTest targets, 30 installer host tests and 330 Node tests**. The original private-scene fixtures remain absent: `background-decode-test`, `assets-service.test.ts`, `asset-cache.test.mjs` and `background*.test.*` are outside this reduced-content command. Publication catalog tests cover empty normal catalogs, explicit fixture opt-in, advertised downloads, hashes and missing-asset responses. These tests use synthetic temporary profiles and no hardware ports.

The bundled ESP application is 1,601,056 bytes, built with official ESP-IDF 5.3.6: 2,224 bytes larger than the prepared66 build, with 1,544,672 bytes free in its 3 MiB OTA slot. Host State/snapshot sizes are 2,936/2,964 bytes. Static DIRAM is 208,663 of 341,760 bytes; linker remainder is not runtime heap. The [resource summary](releases/firmware-171cda7/resource-summary.json) records a 38,500-byte synthetic JSON maximum under the unchanged 64 KiB bound. The conservative known application stack chain is 26,720 of 32,768 bytes, leaving 6,048 bytes before unmeasured SDK/library and indirect-call stack. These are offline measurements, not physical stack high-water or full-roster stress results. Prior profiles retain their members and migrate with an empty XP companion list; firmware predating schema 22 cannot read the new save. Preserve each device's own pre-upgrade backup.

Both existing devices passed the application-only `171cda7` update: their own saves migrated exactly to 2,964 bytes, settings were preserved, and all 261 SD files (4,700,573 bytes per device) verified without asset writes. Each save remained byte-identical through checkpoint and reboot, with zero gameplay events. The protected flash region matched before boot; all serial handles are closed. The three XP companion slots began empty. [Sanitized physical summary](releases/firmware-171cda7/installation-summary.json) includes the measured 22,280-byte minimum free main-stack observation on both existing profiles. This observation does not measure full-roster/trade stack demand.

These checks cover existing profiles and data/reboot integrity. New companion controls, a full 60-member collection, long walking accuracy and Nearby trade stress have not passed physical acceptance. Earlier false steps while stationary remain unresolved. Publication preparation itself does not access hardware. The completed-capture startup/result-idle fix is included; the motion detector is unchanged. Historical installation notes describe their named firmware versions.

All 55 hardware files and 35 showcase payload/source paths are byte-identical to the latest public baseline. README and this publication note retain showcase links while updating source/build metadata. CAD generation and slicing were not rerun. No CI workflow or deployment setting is added; remote publication and existing Pages status are checked separately after an authorized push.

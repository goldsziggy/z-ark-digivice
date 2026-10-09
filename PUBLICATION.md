# Source publication contents

This is a separate source export prepared for the approved **private `goldsziggy/z-ark-digivice` repository**. The original development history stays local. Its game and firmware source is frozen at **`f74ee4c132b4b9bc6407d99bbc20b13af9dfd962`**, the last verified installed firmware. Later volume, music and trading work is excluded.

## Included

- Deterministic C++ core, frozen replay compatibility, firmware adapters, native touch UI, local TypeScript service and browser simulator.
- The verified development firmware images, original package manifest and safe usage notes in [releases/firmware-f74ee4c](releases/firmware-f74ee4c/README.md).
- Source tests, roster research inputs, original CC0 fixture sprites and scoped component notices.
- Approved C14-P19 editable CAD, 11 print STLs, six-page assembly PDF and six PNG guide pages.
- Six [print ZIPs](hardware/c14-p19/downloads/README.md), each containing an actual native 3MF project, matching G-code, README and checksums. Both front variants have repair-only and full-white choices; common black/red plates are unchanged from C13.

Product links are ordinary links without affiliate tags. No project-wide license is invented; scoped licenses continue to apply.

## Content and portability boundaries

Private saves, NVS/flash backups, device identifiers, Wi-Fi settings, credentials, private art, supplied scenery, historical device evidence and development Git history are excluded. No new board access occurred during export. Vendor CAD reference meshes/manuals remain external prerequisites; rebuilding the geometry is not turnkey. Physical fit, screw grip and battery wiring remain unverified.

Production selection uses 266 Digimon forms and 166 authored evolution routes. The three original browser sprite packs and ten DVAs are **legacy test fixtures**, available only with explicit fixture opt-in; they are not new-game creatures. The supplied private Digimon artwork is omitted. Missing exact-form art displays the neutral missing-art treatment.

Because scenery and third-party artwork are omitted, both default service catalogs are empty. Requests for omitted scenery and legacy fixture assets return 404 in normal mode. The publication-only device catalog adaptation allows a bounded empty manifest; entry allowlists, signature verification, hash checks, paths and size bounds remain enforced. The firmware is byte-identical to f74 and can retain an existing valid cache when an empty catalog is not accepted. Browser catalog code already permits empty catalogs. Original fixture bytes remain unchanged for explicit local tests.

Other publication adaptations preserve explicit-only Garage configuration, portable documentation, the reduced asset indexes and `test:publication`. Five history-dependent `investigate-*` scripts remain omitted. [The manifest](PUBLICATION_MANIFEST.json) identifies every payload and whether it matches the frozen source. [Validation](PUBLICATION_VALIDATION.json) distinguishes new export checks from prior build/physical evidence.

## Verification scope

Run `npm run test:publication`. This builds the shared C++ core, runs host CTest targets and the selected Node and installer tests. Private-scene fixtures remain absent: `background-decode-test`, `assets-service.test.ts`, `asset-cache.test.mjs` and `background*.test.*` are excluded from that command. Publication catalog checks cover fixture opt-in, empty production catalogs, all advertised asset downloads, integrity and missing-asset responses.

The bundled ESP images were built using official ESP-IDF 5.3.6 before export. This export does not claim a second ESP build or physical acceptance. Prior installation verified the same f74 app on both units, cleared NVS under then-current authorization, verified fresh saves and existing SD assets, and verified reboot persistence. The earlier Unit 2 USB silence did not recur during that bounded check. Physical touch, battle playback, walking, audio, motion wake and real two-device RF acceptance remain open. No reset or flash is part of this publication.

The hardware payload is unchanged from the approved C14-P19 export. ZIP CRCs/member hashes and source/guide hashes are rechecked; CAD generation and slicing are not rerun. See [hardware/README.md](hardware/README.md).

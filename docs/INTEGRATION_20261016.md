# Source integration — 16 October 2026

The current release combines the previously separate native, service and browser implementations on main. Core rules are 22, snapshot schema is 30 (6,876 bytes), and service store format is 24.

## Included work

- Root checkout's rules20 replay preservation, care-independent evolution, 99-level progression, 250-member collection, browser touch/care/evolution presentation and 2.1-inch board support.
- The selected Codex dungeon implementation, final Auto battle follow-up and rules22 dungeon balance, knockout, co-op consent/replay and service/browser migrations. Frozen rules19/20/21 engines preserve old histories and pending action version markers.
- Home's stacked XP/HP meters and screen-only sleep/background-care policy, including their browser tests and sleep documentation.
- The deployed native Digi Modify, buzzer/music and PN532 code from `8d5ba2d`, followed by the bounded I2C sound and NFC-session fixes from `3750190`. The 1.46-inch PCM profile remains supported.
- The deployment guide/tool branch and the separate SPD2010 raw-packet diagnostics commit, plus local synthetic showcase rendering helpers.
- A rebuilt shipped WebAssembly runtime from native source at `88c2a93a99927c3223736ef035674c1a4670f651`. Its native/WASM parity test uses that exact `DEMO_SOURCE_COMMIT`. Later integration changes affect documentation and host tests, not those compiled core/bridge inputs.

The unselected Concierge comparison implements a competing dungeon design. It remains preserved separately, rather than mixing incompatible product behavior into the selected release. Earlier Codex, final-Auto, Home and sleep snapshots are superseded by their reconciled descendants. Original checkouts and branches are retained.

## Verification scope

Both ESP-IDF 5.3.6 board profiles compile without compiler warnings. Static Xtensa call-frame gates pass for both; these are bounded entry-frame checks, not measurements of complete runtime stack usage. Native host CTest passes 47/48; the remaining background decoder test requires a private JPEG omitted from the publication.

Native/WASM parity, browser controller/touch/art/save-migration tests, and actual Chromium Home checks pass. The Home checks cover touch and button modes at 412 and 480 pixels, all five panels, and no save mutation from navigation. Buzzer, PCM/audio settings, PN532 protocol/startup/test screen/gameplay, board ownership, screen settings, physical display mapping, idle, power and touch host checks use hardware doubles.

Two stale harness assumptions were corrected: batch Rest earns one ordinary cooldown-limited care XP reward, and NFC input discard diagnostics add bounded flush/close drains to the single protocol operation. The touch-poll regression uses a clearly labelled 2 ms lock model when its historical private Git commit is unavailable.

Full Node modules: 386/397 pass. Eight failures require omitted private background fixtures/catalog data. Three pre-existing full-roster HTTP tests return `core_output_invalid`/503 and retain old 60-member assumptions; these are not represented as passing. The aggregate firmware host runner similarly reaches a missing private asset-cache fixture; the remaining portable runtime modules pass when run separately.

A previously documented fresh service-browser onboarding incompatibility also remains: static roster evolution metadata lacks the `requiredCare` field expected by the current client, and offered starter IDs 9–11 are omitted from its action matcher. Restored service play and standalone native/WASM flows are separate from this limitation. Integration does not silently change these product behaviors.

## Publication and hardware boundaries

Only reviewed source, tests, synthetic regression fixtures, documentation, and the existing shipped browser runtime are published. Device saves, full flash/NVS backups, credentials, private artwork, serial logs, caches, toolchains and ordinary build output remain outside Git. The deployment tool is documented for the 1.46-inch board; its presence is not authorization to access hardware or apply it to another profile.

This integration performs no physical device operation. Earlier verification established buzzer register transitions, not audible output. One PN532 returned firmware and a valid no-card response; another remained unresponsive. No physical UID capture or fully working hardware claim is made.

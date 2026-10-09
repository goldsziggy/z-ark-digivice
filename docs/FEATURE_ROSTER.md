# Digivice feature roster

Updated 6 October 2026. The goal is a standalone companion with expressive 2D battles, walking encounters and physical cards. A phone browser helps with setup; ordinary care and battles must work without a phone or network.

**Status matters:** the C++ rules and local Mac service/browser run here. The official ESP-IDF toolchain cross-compiles GenericSerial, Waveshare+SD and development profiles, but the board has not been flashed or bench-tested. The provisional target is Waveshare ESP32-S3-Touch-LCD-1.46 standard glass; see [hardware evidence](HARDWARE.md) and [firmware status](FIRMWARE.md). Physical LCD, buttons, NFC and power behavior remain unverified. [Current full-roster implementation and artwork limits](WORLD_DS_ROSTER.md).

## Gameplay and content

| Feature | Implemented now | Next complete behavior |
| --- | --- | --- |
| Boot / home | Browser home view and serial state; deterministic new game and restoration | Real round LCD home screen, loading/error states, remembered companion and verified cold-boot recovery |
| Care | Feed, play and rest at home; bounded fullness, mood, energy, HP and bond | Hardware reaction animations, comfortable touch targets and simple care feedback; optional small care activity after the core loop is stable |
| Gentle defaults | No elapsed-time neglect, death, streak loss or missed-care punishment; defeat retreats with HP restored to 10 | Preserve forgiving care and a pause/quiet mode; any timed mechanic needs an explicit product decision |
| Evolution | Explicit level/bond choices: existing starter branches plus45 new links across18 families; each member progresses separately | Branching choices or new lines only with explicit versioned rules and migration |
| Walking | Simulated `walk` events accept 1–1000 steps and accumulate progress | QMI8658 sampling, calibrated step estimation, false-motion rejection and batched durable writes; daily views only once clock behavior is defined |
| Encounters | 100 step credits start an encounter at home; deterministic stage-appropriate selection across the native roster | Local habitat/time tables once context and clock behavior are defined; GPS remains optional |
| Battle | Named physical, heavy and magic moves; shared deterministic damage uses combat stats and type matchups, with retaliation, energy, win and gentle retreat | Hardware display and input integration; add status combinations only with versioned rules and focused tests |
| Combat stats / types | 276 native forms supply max HP, ATK, DEF, MAG, RES and named moves. Grove → Tide → Ember → Grove advantages; Flicker is neutral. Round stats, moves and type-chart screens read native-core data | Tune the initial roster through playtesting; profiles and damage are shared by wild encounters and practice |
| Practice battle | Tactical choices/counters or confirmed strict Auto; shared engine runs through browser/service and an ESP serial session with separate durable records and replay; no care, capture or progression rewards | Physical rendering/buttons and NVS tests. Two-player networking remains outside this mode; [serial proof](ESP_PRACTICE.md) |
| Capture | Capture at or below half wild HP; up to three attempts; success appends an individual member; full collection rejects without overwriting or spending an attempt | More encounter content and optional storage expansion after real playtesting |
| Cards | Simulated Spark next-hit boost and Shelter shield; at most one card per encounter | PN532 identification, local UID-to-card mapping, repeat-read debounce and clear accepted/rejected feedback |
| Physical swipe | Input concept and NFC candidate researched; no physical reader integration | Slide/pause/read interaction; direction sensing only if a specific rule needs it. NFC alone does not establish direction or authenticity |
| Collection | Eight slots including founder, stable member IDs, separate care stats, home-only selection, duplicate species supported | Inactive companions can be explicitly released at Home; a512-bit journal retains obtained forms. Trading, nicknames and storage for hundreds of individuals remain deferred |
| Progression | Bond, RPG levels1–20, explicit forms, total steps, encounters, captures and obtained-form journal; replayable rules | Short goals and gentle unlocks; no competitive economy, scarce tradable rewards or paid progression in MVP |
| Art families / gallery | Ten original forms across three playable evolution lines plus Flicker; separate gallery and optional local personal-art import | Additional art does not add rules by itself; port bounded loading and rendering to the real display |
| Battle presentation | Browser sprite clips and heart/hit/spark/capture effects; host-generated 480×480 RGB565 sample uses core replay results for HP/rewards | Sprite pose changes, projectiles, impact flashes, modest particles and layered scenery on LVGL; profile actual FPS, heap and power before setting limits |
| Audio | Optional original browser effects/music, muted by default; no physical audio adapter enabled | Optional short on/off buzzer patterns and mute. The stock EXIO8-controlled buzzer is not a PCM/WAV speaker or an I2S audio output |

The core stores up to eight individual members and their care stats. It does not store a daily calendar, nickname, age, GPS route or NFC UID. Legacy captures retain only their known historical total. Bundled artwork is original; optional third-party appearances remain local, separately attributed and excluded from source bundles. See [shared rules](../core/game.cpp) for exact behavior and [asset delivery](ASSET_DELIVERY_PLAN.md) for content limits.

## Setup, reliability and operation

| Feature | Implemented now | Remaining work |
| --- | --- | --- |
| Local browser setup | Short-lived code pairs a virtual device with the loopback development service | Physical presence, owner confirmation, token rotation/revocation and a reviewed device identity lifecycle |
| Wi-Fi setup | Not implemented on ESP or in the current browser | 2.4 GHz network setup on device or through a short-lived protected SoftAP browser session; detailed flow below |
| Asset updates | Local signed-fixture catalog and HTTP resume routes implemented with focused service tests; browser pack cache with signed validation, pause/resume and saved art is implemented | Bounded device downloader, trusted verification, durable staging, last-good activation and offline starter fallback |
| Save / recovery | Canonical versioned snapshot with CRC; host-tested dual-slot ESP save policy; local service atomic store and recovery copy | Bench-test NVS, interrupted writes, low supply and recovery UX; never erase a pet automatically after an error |
| Save sync | Local service replays events, checks revision and returns stable idempotent receipts; browser retries pending actions | Firmware durable event outbox committed with its snapshot, acknowledgement watermark and explicit full-queue/conflict behavior |
| Offline operation | C++ core has no clock, network or filesystem dependency | Full device loop with built-in art and local saves. The browser gameplay simulator still requires its running local service; cached gallery art is a different capability |
| Settings | Round browser sound, artwork, connection, saved-playtest and recovery screens; no physical settings driver | Brightness, buzzer mute, animation preference, Wi-Fi connect/forget, explicit sync, storage status, version and recovery/export controls |
| Controls / accessibility | Browser left tap selects Next, left hold (600 ms) goes Back, right tap confirms; A/D keys use the same press lifecycle. Touch and arrow keys remain optional; reduced motion is supported | Bind the two-button input to verified physical GPIOs, then bench-test debounce, cancellation and touch targets |
| Power / sleep | No enabled battery telemetry, charge control or sleep driver | Measured dim/idle/sleep states, checkpoint before sleep, verified wake sources and low-voltage recovery; do not promise step counting during deep sleep |
| GPS / privacy | No location collection or required GPS | Optional coarse region context kept local by default; no raw route or child personal profile uploads |
| Firmware update | Two 3 MiB app slots and OTA metadata reserved | Signed update/rollback flow, power interruption tests and compatibility checks; partitions alone do not implement OTA |
| Ownership / reset | Local development token only; no physical ownership transfer | Explicit save export/import, revoke old owner, confirm intentional reset and preserve recovery evidence; forgetting Wi-Fi must not erase gameplay |

## Wi-Fi setup without a native phone app — planned

**On-device route:** select a visible 2.4 GHz WPA2-Personal network and enter its password with the touch keyboard. Hide the password by default; allow deliberate reveal and correction. An optional two-button control scheme may navigate the menu but is not required for MVP and has no GPIO assignment yet. Hidden SSID entry can follow once the normal flow works. Enterprise networks, internet-login captive portals and 5 GHz-only networks are outside the first supported setup path.

**Phone-browser route:** hold a setup control on the device, confirm “Connect Wi-Fi,” and join its temporary WPA2-protected access point from the phone. Use a unique device/session setup password displayed on the screen, not a fleet-wide default. The device's own local page lists networks and accepts credentials. Pair it with a single short-lived setup session, a proposed five-minute deadline, an unpredictable session token, host/origin and CSRF checks, a bounded number of attempts and explicit cancel. Credentials go in a local POST body. Stop the AP after success, cancel or expiry. A failed network attempt must offer retry while preserving the previous working network and the pet save. Never open setup automatically merely because home Wi-Fi disappears.

ESP-IDF supports both SoftAP and BLE provisioning transports. Its provisioning APIs provide protocol endpoints, not a finished compatible phone-browser page. The browser client, local page, host/origin checks and session lifecycle still need implementation and testing. Do not copy the example's SSID/password diagnostic logging. [Official Wi-Fi provisioning API](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/provisioning/wifi_provisioning.html)

For this first local browser route, the short-lived protected Wi-Fi link and physical setup confirmation are the access boundary; local HTTP is not described as HTTPS or end-to-end application encryption. Keep the AP isolated, limit it to the setup client, and serve all client assets locally without a CDN or analytics. Test normal Safari/Chrome navigation and captive-browser restrictions on intended phones, with a displayed local-address fallback. An application-encrypted provisioning channel can be evaluated later using a compatible client for Espressif `protocomm` Security 2 (SRP6a/AES-GCM); it is not a required custom crypto project for the first slice. Do not silently switch to an open AP. BLE may be an optional future path after client compatibility work; it is not a native-app requirement for MVP. [Official protocomm security options](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/provisioning/protocomm.html)

Home-network credentials stay between the user's input device and the Digivice. Never put them in a URL, application log, telemetry, exported game save or asset/sync-service request. Persist them only in the device's reviewed Wi-Fi storage, clear temporary input buffers, and test “forget network” independently of game reset. Device/service pairing is a separate flow using separate credentials. NVS encryption is not implemented; release-grade storage protection and identity provisioning remain work. No production credentials are generated by this plan.

## Small delivery phases

1. **Content and local delivery:** original family packs, gallery, signed-fixture validation, bounded cache, interrupted-download recovery and built-in fallback. Evidence: local tests and a browser playthrough; gameplay roster stays honest.
2. **One real screen:** select the board/power arrangement, compile the existing ESP starter, then integrate the audited vendor LCD/touch stack and starter art. Evidence: measured memory, stable save/reboot and display behavior. Flashing requires its own authorized hardware task.
3. **Device connectivity:** implement on-device Wi-Fi setup, then the protected browser route, HTTPS asset fetching and the durable save outbox. Evidence: wrong password, disconnect, expiry, resume and lost acknowledgement tests without blocked battles or lost pet state.
4. **Real physical play:** motion pipeline and batched writes, then NFC, then optional GPS. Evidence: walk/shake traces, repeated-card handling, no route upload, measured idle and wake behavior.
5. **Expanded companion roster:** individual records, three deterministic encounter species and three evolution lines are implemented. The full255-sheet metadata roster is now supported; continue artwork acquisition/review and hardware playtesting.

Each phase should produce a usable vertical slice before adding more systems. Cloud deployment, paid services, live AI in battles, online multiplayer, trading and production key management are separate decisions, not prerequisites for this build.

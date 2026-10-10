# Firmware: handheld foundation

> Publication checkpoint: source and previously installed firmware are f74ee4c (schema 17 / rules 13). Both units passed bounded installation/save/SD/reboot checks; older entries below are historical. No hardware was accessed for this export. See [verified images and remaining acceptance](../releases/firmware-f74ee4c/README.md).

The ESP application and host demo use the same deterministic C++ game core. Care, encounters, cards, combat, capture, RPG progression, chosen Digivolution and saves work without a network call. The current primary target is **Waveshare ESP32-S3-Touch-LCD-1.46, standard glass SKU29565**, 412×412, 16 MiB flash and 8 MiB PSRAM. The owned Heltec LoRa V4 is a possible interim host, with its exact revision and wiring still unverified. The previous 2.1-inch research in `HARDWARE.md` is historical; its pins and LCD stack must not be applied to the 1.46 board.

**Current build and installation:** firmware `6e058e1` passed its ESP-IDF 5.3.6 build and was installed app-only on both units. Current care is **schema 15/rules 12/636-byte snapshots**, service container 14; practice remains schema/rules 7/store 8. The native 412×412 renderer, display/touch drivers, eleven-choice starter carousel, saved capture playback and screen/backlight idle are implemented. Screen idle keeps the CPU and input sampling active; CPU deep sleep and battery savings are unqualified. Native pairing, durable event outbox and save sync remain unimplemented. Device checkpoint evidence (historical local evidence omitted) records save preservation, stable eleven-choice previews after reboot, asset hashes and reported screen idle/console wake; human finger use, motion wake, battle/audio, RF and power qualification remain pending. [Current release and build evidence](CARE_CAPTURE_RELEASE.md) · [Rules and bounds](CARE_CAPTURE_RULES12.md) · [Saved starter offers](STARTER_OFFERS.md).

The assembled-case sideways-screen report is addressed in source by a shared display/touch quarter-turn transform. The user confirmed image bottom-left/top-right, so the display-enabled `waveshare` case profile selects counterclockwise 90° with inverse touch mapping. No orientation update has been installed yet. See [orientation evidence and physical acceptance](DISPLAY_ORIENTATION.md).

The next source revision replaces the Explore entry with four Home panels: Care, Partners, Settings and Nearby. Valid post-starter walking also earns encounters while browsing or battling; one chosen encounter waits durably for quiet Home. The canonical save becomes schema 17 / 652 bytes with existing schema 15/16 migration; rules 13 excludes test creatures while preserving combat formulas. See [Home controls, walking semantics and save boundary](HOME_CAROUSEL.md). Build/test evidence for this revision is recorded separately from the installed `6e058e1` checkpoint. The later [production encounter and battle-art correction](BATTLE_ART_RELEASE.md) supersedes uninstalled `517f2b6`.

The foundation notes below preserve earlier serial/transport implementation and build milestones. Their older snapshot sizes, renderer gaps and hardware-status statements are historical; the current release links above take precedence.

## Build and run

```sh
npm run build:esp
npm run build:esp:waveshare
npm run build:esp:waveshare-dev
npm run test:handheld
npm run test:handheld:transport
npm run demo:sd
npm run demo:practice:serial
npm run dev:direct
```

The installed SDK/tools/Python environment are under the task-local `../.toolchains` directory. No global setup is required for the provided wrapper; it never flashes. GenericSerial touches no board GPIOs. The Waveshare profiles enable the verified pin map and primary SD adapter; optional buttons and QMI configuration remain disabled. The development profile explicitly opts into the public fixture asset key and private-HTTP build support, with no embedded credentials.

The browser at http://127.0.0.1:8787 remains the fuller UI preview; browser gameplay calls the local service. The host SD demo uses the actual portable file/cache/core policies with a private temporary POSIX file, not a physical card.

## Runtime boundary

| Module | Implemented behavior | Remaining physical or product work |
|---|---|---|
| Shared core and dual NVS saves | Copy/apply/checkpoint/readback before acknowledgement; future/corrupt saves preserved | Brownout testing and Xtensa measurements |
| RPG levels and 276-form catalog | Separate XP/bond/form state, 1–20 levels, explicit branch preview/confirmation and fixed migration | Balance tuning and physical progression UI; no bundled franchise art |
| Wild Tactical / Auto | Confirmed Home mode choice, shared-core resolution and one checkpoint for a complete Auto result | Physical menu/button presentation |
| Practice Tactical / Auto | Shared native engine, explicit serial starts, per-command NVS commit/readback, exact last-command retries and saved Auto replay | Physical rendering/input and power-loss tests; [commands and proof](ESP_PRACTICE.md) |
| `network` and `NetworkAdapter` | Saved bounded station configuration; six fast join attempts, exponential backoff/jitter, five-minute retry; offline/local-only/online states; asynchronous health probe | Hotspot compatibility, trusted wall clock, power measurements, consumer setup UI |
| `asset_cache`, `FileAssetStorage` and `SdAssetStorage` | Five fixed slots, four protected working assets plus replacement; 4 KiB journaled ranges, SHA-256, atomic activation, resume and recovery | Physical SD removal, full-card and power-cut testing |
| `DeviceAssetsClient` | Explicit development-profile catalog signature validation and bounded selected downloads in one worker | Owner-controlled production trust, actual TLS/range integration |
| `sprite` | Bounded DVA validation and one-frame RGB565 decoding, resident original fallback | LCD/LVGL renderer and JPEG decoder |
| `MotionCounter` / `StepDelivery` | 24-bit rollover, plausibility limits, visible continuity faults; verified batched saves during Home and wild encounters | Calibrated physical sensor setup and walking accuracy |
| `MotionAdapter` | Gated, read-only identity/counter inspection on shared I²C | Revision and documented pedometer configuration must be independently verified |
| Onboard PWR | Debounced three-second hold, serial countdown/cancel, durable shutdown barriers, release-before-cut and powered standby/resume | Physical button/latch/USB/SD tests and LCD countdown; [gesture guide](POWER_CONTROL.md) |
| NFC / BLE / GPS / sleep / charging | No enabled driver | Explicit later scope and physical verification |

Firmware has no native pairing/save-sync client or durable event outbox yet. The service already implements pairing and idempotent replay-validated save sync. Before connecting them, persist a bounded action journal atomically with the local snapshot, keep retry-stable batch IDs and server acknowledgement watermark, and define full-queue handling. Do not upload an arbitrary snapshot as validated progression. No raw GPS route or child data is required.

## Console and provisioning

Gameplay commands include `status`, `feed`, `play`, `rest`, `walk 100`, `card 1`, `attack`, `heavy`, `magic`, `capture`, `select 2`, `evolve status`, `checkpoint` and `snapshot`. Walking and card commands are simulations. `capabilities` reports initialized abilities, not checked build options. No LCD or NFC capability is claimed.

XP comes from wild victories or successful captures: `20 + 6 × wild level` for the active partner. Care and practice grant no XP. Levels use the cumulative threshold `20 × (L−1) × L`, capped at level 20 / 7,600 XP. Bond remains separate and care grants it only when the corresponding need improves. A wild rival's level is the partner's level, one below, or one above, clamped to 1–50, when the encounter starts; retaliation alternates physical and magic.

`evolve status` lists actual outgoing graph edges, their level/bond gates, preview stats and art availability. `evolve <formId>` stages an eligible choice; `evolve confirm` applies and checkpoints it; `evolve cancel` keeps the existing form. An intervening game-sequence change invalidates the preview. The partner must be at Home, with writable saves and no active or uncertain practice. Evolution preserves member identity, XP, bond and capture history, and scales HP proportionally. The current 276-form catalog keeps stable identities; individual edges can cross families and use different gates. Names/stats do not imply installed sprites or a physical renderer.

For full HP and energy at Home, run `rest full` to review the native count, then `rest full confirm`. `rest full status` inspects the pending review and `rest full cancel` cancels it. The operation applies up to 40 ordinary `Rest` events to one candidate and saves once after confirmation; current profiles need at most 12. It adds no XP or new healing rule. The review is bound to game sequence, selected member and form, so walking or another game action invalidates it. Already fully recovered means no action and no write. An uncertain commit blocks further writes until recovery; reboot reads the durable result rather than replaying the batch.

`companions` lists permanent member IDs. `release <ID> confirm` explicitly removes a nonactive companion after a verified checkpoint, preserving journal history. It is allowed at Home or during a new rules 10 encounter, including Auto waiting for its start command, so a full eight-member collection can make room before capture. It does not give the opponent a turn or change battle RNG/HP. Older saved encounters keep their Home-only release restriction. Active/uncertain practice still blocks release; the active companion cannot be released.

At Home, choose and confirm the wild battle mode:

```text
mode status
mode auto
mode confirm
walk 100
auto
```

`mode auto` stages a choice without changing the save; `mode confirm` applies and checkpoints it. Use `mode tactical` followed by `mode confirm` to restore manual choices, or `mode cancel` to discard a pending choice. Any intervening game-sequence change invalidates that confirmation. Direct numeric mode commands cannot bypass it. Tactical is the default for new games and older snapshots without a saved mode; migration preserves an existing Auto choice.

An Auto encounter waits for the explicit `auto` command. The core resolves the whole fight without manual attacks, cards or capture timing, then the command checkpoints the terminal result once before replacing RAM state or acknowledging success. The saved mode remains Auto until changed at Home. Practice Tactical/Auto battles use separate **304-byte canonical NVS records**, including frozen form identity. New practice snapshots are schema/rules 7 and 120 bytes; saved versions 2–6 retain their original behavior and exact Auto replay, including 112-byte version 2 duels. Use `practice help`, then `practice start <ID> <REV> tactical|auto` to explicitly confirm; [complete command and retry guide](ESP_PRACTICE.md). [Mode rules, policy and recovery](BATTLE_MODES.md).

Network configuration is deliberate through the local USB console:

```text
net status
net set https://service.example|Your hotspot SSID|your-local-password
net retry
net pause
net resume
net forget
assets status
assets warm
assets fetch sprite-mote-v1
motion status
motion recover
```

Use real local values only in your own terminal. No production service is deployed at the illustrative origin. The first two `|` characters separate fields; an SSID containing `|` is not supported by this minimal setup command. Passwords may contain `|`. Lines are bounded and are not echoed by firmware; clear them after processing. Terminal-side local echo/logging is outside the firmware's control. NVS configuration is **unencrypted prototype storage**, not a production credential vault.

`net set-lan http://192.168.x.y:8787|SSID|password` additionally requires the default-off `DIGIVICE_ALLOW_PRIVATE_HTTP` build option. Only literal RFC1918 addresses are accepted for this insecure development mode; both runtime and build opt-ins are required. The service's optional LAN binding is source-only, explicit and private-interface scoped; default remains loopback. No LAN listener was started for this milestone. Internet access through a phone hotspot does not expose a Mac's loopback service to the ESP.

HTTPS verifies the server certificate with the IDF certificate bundle. A valid wall clock must first be established through a reviewed clock source; this slice does not implement SNTP/RTC synchronization and must not disable certificate checks to bypass that requirement. `online` means the configured Digivice health endpoint validated, not generic internet access. Slow trickling HTTP headers can outlast IDF's per-operation timeout; workers are bounded in number and stale results are discarded, but a hard whole-request deadline is not claimed. The parser component has a 4 KiB header cap; this bounds parser storage, not total wire traffic or time. Interrupted-header/reconnect heap soak remains a target acceptance check after selecting the SDK with the cleanup fix.

`DIGIVICE_DEVELOPMENT_ASSETS` defaults off. It explicitly enables the publicly known RFC signing fixture, which demonstrates validation but supplies no production authenticity. A selected partner, current/next wild and forest scene form a small prefetch plan. Network failures leave local game state and installed art available. The library is not bulk-downloaded at boot; the compiled Mote/Flicker fallback is always resident. No background renderer exists yet, so JPEGs can be verified/stored but are not physically displayed.

## Durability and resources

The `digivice` NVS namespace has `save_a` and `save_b`, canonical **576-byte format-13 snapshots**, game schema 13 and rules 10, with CRC. Each member now stores XP and a form ID independently of lineage and bond. Existing binary snapshots migrate explicitly: old tiers 1/2/3 become RPG levels 1/5/10, original appearances remain, and Rookie starters do not silently Digivolve. Existing mode/Auto-summary fields survive where present; earlier formats default to Tactical. Onboarding and member history are preserved. An uncertain commit stops gameplay; reboot selects the newest valid checkpoint, or preserves evidence in read-only recovery. No NVS initialization failure automatically erases storage. CRC protects accidental corruption, not physical tampering.

The complete Auto outcome is one saved transition. If its write may have succeeded but acknowledgement fails, RAM remains unchanged and further commands are blocked until recovery. Reboot can restore the terminal snapshot; another `auto` command then fails the core's phase guard rather than repeating capture, XP or rewards. Evolution likewise becomes visible only after its checkpoint is verified. Firmware does not allocate the optional wild battle trace or the host-only 16 KiB trace JSON output buffer. Earlier mode persistence evidence (historical local evidence omitted) and [current verification](VERIFICATION.md).

Motion delivery checkpoints already-confirmed counter batches before replacing game RAM or acknowledging them. It now runs during wild encounters as well as Home: native `Walk` adds steps/credit during combat without changing opponent state or triggering a second encounter. Batches are at most 100 steps, normally saved after 100 pending steps or 30 seconds. Active/uncertain practice holds delivery to prevent a Home walk from starting a wild encounter.

`motion status` reports physical-reader readiness, counter status, confirmed pending/total steps, the delivery latch and the practice guard. Pending RAM is bounded at 4,096 steps. Overflow/reset/implausible-count faults stop continuity and print a diagnostic; confirmed pending steps can still drain durably when allowed. `motion recover` explicitly reanchors only after that queue reaches zero, the reader is verified ready and saves/delivery are healthy. The next reading anchors without crediting the unverified gap. Sensor I/O failure or an uncertain checkpoint requires reboot/verified recovery; the command cannot bypass it.

These pending batches are RAM-only. Power loss before a checkpoint can lose recent uncommitted steps; during practice the pending interval can be much longer than 30 seconds. Reboot starts a fresh session and anchors the current raw counter without reconstructing or replaying an old total. This is not an atomic cross-boot sensor transaction. Physical configuration remains deliberately unattested, so the read-only QMI adapter still does not claim usable walking input. A later enabled sampler needs an independent task or a measured transaction budget preserving network/UI responsiveness. The host one-hour test delivered 7,200 synthetic steps during one encounter with 120 verified commits and a 60-step pending peak; it is not a walking-accuracy or NVS-endurance measurement.

The primary cache is one **696,832-byte microSD file**, including its ownership header, metadata and five bounded slots. Card errors preserve NVS and use the compiled fallback; there is no automatic formatting, medium substitution or hot-remount. The old raw partition reservation remains unchanged but is not the normal backend. [SD storage contract](SD_ASSETS.md). Exact installed old bytes remain readable, but new resident-ID rollback or same-version changed-hash downloads are rejected; this is not a persistent publisher high-water mark after eviction.

The core's measured host State is **552 B** and its canonical snapshot is **576 B**. The new portable step-delivery and recovery-review controllers are **16 B each** on the host ABI. These are separate from target heap/stack use. Current Park source cross-compiles with ESP-IDF 5.3.6 in all three profiles, with zero compiler warnings:

| Profile | Application binary | Static mapped DIRAM use | Space remaining in 3 MiB app slot |
|---|---:|---:|---:|
| GenericSerial | 1,252,192 B | 114,267 B | 1,893,536 B |
| Waveshare | 1,260,816 B | 115,347 B | 1,884,912 B |
| Waveshare development | 1,273,536 B | 137,235 B | 1,872,192 B |

Current target evidence and source hashes (historical local evidence omitted) describe compiled and linked binaries, not physical execution. Static map usage and app-slot space are not runtime free-heap measurements. Main task stack is reserved at 16 KiB; the game JSON capacity is **8 KiB**. Runtime worker-stack usage, TLS/cJSON allocations, PSRAM operation, FPS and current draw still need hardware measurements. App slots remain two 3 MiB reservations; no OTA updater is implemented.

Park host verification (historical local evidence omitted) passes 22,203 counted portable-runtime assertions plus SaveStore checks, including schema 11/12 migration, uncertain recovery, one-hour encounter step delivery, overflow/reanchor and confirmed release. The SD-file demo resumes a download across simulated disconnect/reboot and completes a capture offline; the practice demo verifies exact retries across five fresh host processes. Neither accesses physical hardware.

Actual asset measurements and proposed decoded buffers are in [HANDHELD_FOUNDATION.md](HANDHELD_FOUNDATION.md). Deep sleep and sensor wake cannot be promised from an expander interrupt diagram alone. Implement measured sleep/low-battery behavior after display, input and save recovery work on the actual board.

The earlier transport milestone ran the actual catalog/signature/range client code against the existing host mbedTLS 3.6.5 and cJSON libraries: **79 checks passed** with HTTP/RTOS doubles. It checked real signatures, malformed catalogs, mismatched ranges, cancellation and cache resumption. This is separate from target compilation or TLS/hotspot testing. Evidence (historical local evidence omitted). That milestone's service/module regression suite passed **132 tests** and strict TypeScript checking; current battle-mode results are linked from [BATTLE_MODES.md](BATTLE_MODES.md).

# Walking and nearby duels — retained installed checkpoint

> Publication checkpoint: source and previously installed firmware are f74ee4c (schema 17 / rules 13). Both units passed bounded installation/save/SD/reboot checks; older entries below are historical. No hardware was accessed for this export. See [verified images and remaining acceptance](../releases/firmware-f74ee4c/README.md).

**This page describes the installed `6e058e1` checkpoint: care schema 15 / rules 12 / 636 bytes / service container 14.** New source replaces Explore with four Home panels and counts walking in menus/battles using one durable pending encounter (current schema 17 / 652 bytes). [Current Home and walking behavior](HOME_CAROUSEL.md) supersedes the Home-only eligibility described below. The retained checkpoint’s pacing and lifetime accounting are documented here. The [care/capture release guide](CARE_CAPTURE_RELEASE.md) covers current carousels, care bonuses, saved offers, capture timelines and screen idle; clean firmware source **`6e058e1`** is compiled and pinned (historical local evidence omitted), and both units passed their own app-only update, save migration, assets, preview, reboot and diagnostic-idle checks. Current physical record (historical local evidence omitted). Finger, walking, audio, motion-wake and RF acceptance remain open.

The retained **`2d9f1ed`** hardware checkpoint uses **care schema 14 / rules 11 / catalog 6 / service container 13**. It adds physical-step sampling, saved encounter pacing, native collection/Digivolution screens, readable battle presentation and explicit-consent ESP-NOW friendly duels. Its official ESP-IDF 5.3.6 build is **1,531,584 bytes**, with no compiler warnings and all 231 recorded source hashes verified. Exact checkpoint artifact pins (historical local evidence omitted). Source/host verification and physical acceptance remain distinct. Larger swipe carousels, floating battle text and rules-12 care/capture behavior exist in newer source; they are not included in this historical checkpoint.

## What changes in play

The QMI8658 acceleration sampler feeds a bounded software pedometer independently of cosmetic gyro tilt. Three consistent walking cycles establish a run and credit its first three candidates together. Gaps, invalid samples and pauses discard unconfirmed candidates without resetting accepted steps. Repetitive shaking can resemble walking; real walking accuracy, carry orientation and false positives need physical trials.

**Home → Explore** shows physical lifetime and session steps. Session means the current boot, not a calendar day. Production touch screens have no simulated-step button. Steps observed in other menus or battles may increase lifetime usage, but only fresh, eligible Home/Explore observations advance encounters. Setup, Nearby, battle playback, an active encounter and paused/invalid sampling do not accumulate a future encounter queue.

**Settings → Encounter settings** offers Paused (Off), Relaxed, Normal and Frequent. A saved random effort target is drawn once and retained across retries, reloads and rate changes. The device initializes its separate encounter RNG with `esp_random()` once and checkpoints it; capture RNG and creature identity remain separate. The initial shorter range applies only before the save's first encounter, so an experienced migrated pet uses the subsequent range.

| Pace | First encounter: eligible steps | Subsequent full gap: eligible steps |
| --- | ---: | ---: |
| Off | Paused | Paused |
| Relaxed | 80–160 | 160–280 |
| Normal | 40–80, mean 60 | 80–140, mean 110 |
| Frequent | 27–54 | 54–94 |

These are rule-derived ranges, not measured walking times. Draws use even effort targets of 80–160 initially and 160–280 thereafter; each eligible step adds one, two or three points. Changing pace preserves effort already earned. An accepted `explore` event contains 1–1,000 eligible steps and starts at most one encounter; excess credit is discarded. Rarity remains 70% common / 25% uncommon / 5% rare after the initial Flicker, subject to the existing eligible roster. XP, collection capacity and base stat curves are unchanged; rules 12 separately add effective care bonuses.

**Settings → Mode** confirms Tactical or Auto before a wild encounter. New rules-12 Auto attacks retain rules 11's independent Physical/Magic equal-odds choice and capture priority. They do not alternate or optimize damage. The opponent still alternates retaliation after attacks; rules-12 capture throws cause no retaliation, and a third failed throw ends the encounter with HP unchanged. Existing encounters retain their recorded policy. Practice keeps its separate rules-7 policy. [Current stats, controls and progression](BATTLE_STATS.md) · [Mode/retry contracts](BATTLE_MODES.md).

**Partners → Stats + Evolve** pages through combat stats, care/XP, named moves and type effects. The selected partner can browse its listed routes, compare requirements/stats/moves, review and confirm Digivolution, then view the saved result. Ineligible choices explain the missing requirement. Confirmation preserves the same member ID, XP, bond and HP proportion; it does not grant a free heal. Artwork may use the existing exact-form fallback.

**Home → Nearby** opens local discovery. One player challenges with a frozen partner, level, current care bonuses and mode; the other explicitly accepts that matchup. Friendly Tactical and Auto duels use isolated HP/energy, no cards/capture and **no care changes, XP, rewards or win ledger**. The host sends an ordered deterministic result; the guest checks it against the agreed state and its submitted move. Duplicate/reordered packets cannot create an extra exchange. Subsequent exchanges wait at least 2.4 seconds after the previous state is first acknowledged; Tactical inputs may queue during that hold. Rejoining can recover only the same live in-memory session. Leaving, timeout or reboot aborts it. MAC/session/nonce/CRC checks are **not cryptographic authentication**. [Protocol, bounds and recovery](NEARBY_PROTOCOL.md).

## Save migration and durability

The retained walking checkpoint migrated schema 13/rules 10/**576 bytes** to schema 14/rules 11/**600 bytes** by appending six pacing fields. Current source then migrates to schema 15/rules 12/**636 bytes**, adding the latest capture record and starter-offer seed/IDs. Member identities, XP, bond, care, journal, capture RNG, saved pacing and active encounter rules survive; frozen rules 1–11 replay historical events. Migration itself does not reroll a fight, draw starter offers, hatch an egg or reset a creature. [Current storage contract](CARE_CAPTURE_RULES12.md#storage-and-verification).

Physical lifetime usage is separate from that snapshot. The **`digi_usage`** NVS namespace has two alternating **32-byte** records (`steps_a`/`steps_b`), each containing generation, 64-bit total and CRC. A device without these records starts at **zero physical steps**; old simulated `Walk` totals are deliberately excluded. The current-boot session counter starts at zero on reboot. Corrupt/uncertain records are retained and writes stop for recovery; they are not silently erased.

Dirty lifetime totals checkpoint after **64 additional steps or 30 seconds**, with forced checkpoints on the orderly paths that request them. Gameplay walking progress is also batched; a decided encounter is checkpointed before it is published. NVS writes commit and verify the new slot, avoiding a flash write for each step. Abrupt power loss can lose the unflushed tail and unpublished sampler observations. The batching interval is a normal-operation target, not a guaranteed maximum loss during a stalled task or failed write. The two stores are independently durable, so a power interruption can preserve a lifetime count without preserving the same exploration credit; this never grants an uncommitted encounter.

The local service now migrates **container 13 / rules 11 → container 14 / rules 12** when opened by the new code, archiving the original as `store.rules-v11.json`. Older migrations remain supported. Ordered historical events, receipts, revision and initializer survive; new requests require `rulesVersion:12`. Old pending bodies receive `409 migration_required`; relabeling a committed old receipt receives `409 legacy_batch_requires_reconciliation`. Current retries keep their original result. Practice stays rules 7/store 8. [Current release boundary](CARE_CAPTURE_RELEASE.md#migration-service-and-verification) · [API](../service/API.md).

**No running or deployed service data was migrated during this milestone.** Service verification used isolated temporary stores and loopback processes. The ordinary local service remains stopped unless explicitly launched. Firmware pairing/save-sync and a durable device-to-service event journal are still unimplemented; configuring Wi-Fi is not save synchronization.

## Authority and privacy

The handheld owns immediate offline play and durable local snapshots. The small service supplies catalog/assets, development browser pairing and replay-validated save revisions using the same C++ rules. This provides server progression validation without placing a live network or AI call in battle. Replay proves legal transitions; it cannot prove that a submitted step came from a real walk. Browser play currently needs its local service. Nearby needs no service or phone and cannot award progression.

No raw GPS routes, child names, ages or personal profiles are uploaded. GPS and NFC hardware acceptance remain separate work. Battery percentage, deep-sleep step counting and radio power consumption have not been measured for this milestone. There is no cloud deployment, production identity/authentication claim or production-ready claim.

## Evidence and remaining acceptance

| Item | Observed status |
| --- | --- |
| Current clean ESP build | **`6e058e1`**, ESP-IDF 5.3.6, **1,547,280-byte app**, **1,598,448-byte slot headroom**, **166,831-byte static DIRAM**, zero warnings and 16 KiB main-task stack. 241 source hashes and artifact pins (historical local evidence omitted). Installation verification remains separate. |
| Current two-unit device checks | Both **`6e058e1`** app-only hashes passed. Each own **600→636-byte** migration preserved 62 prior values and saved one three-offer event at sequence 1; eggs stayed unhatched and reboot stable. Each pack passed **261 hashes / 4,700,573 B**, eleven previews and scene JPEG; no new copies. Current physical record (historical local evidence omitted). |
| Current sampling / screen idle | At about 73 s, each had **2,589 touch polls / zero errors / zero lock misses** and fresh IMU. Unit 1/2 free internal **173,887 / 173,823 B**, minimum free main-task stack **8,384 / 8,288 B**; each free PSRAM **7,690,188 B**. At 60 s diagnostics reported blanking/render stop with touch/IMU continuing; console wake/save passed. Physical brightness/wake and current draw remain unverified. |
| Walking core — rules-11 checkpoint | **131,298 checks** plus **164,317 regression checks** pass; scoped ASan/UBSan coverage. Source-hashed evidence (historical local evidence omitted). |
| Nearby portable core/protocol — rules-11 checkpoint | **49,486 checks** pass, including loss/reorder/retry, stale sessions, future-state forgery, terminal immutability, depleted energy and readable ACK pacing. 512 scripted Auto duels sample 3,818 Physical / 3,827 Magic attacks. Source-hashed evidence (historical local evidence omitted). |
| Native visual review | **50 synthetic states** rendered by the current firmware renderer with the existing local artwork. The Library contact sheet and review evidence (historical local evidence omitted) establish layout coverage, not physical photographs, finger accuracy, brightness or timing. |
| Touch shared-I²C fix | **163 sanitizer checks** pass. A bounded 20 ms mutex allowance avoids a zero-tick wait at the configured 100 Hz scheduler; the separate 16 ms transfer deadline starts after acquisition. Actual failures cancel gestures. Fix evidence (historical local evidence omitted). |
| External Grok review | Unavailable: the Cursor CLI stopped on an authentication error. No model response or findings were produced; this is not a completed review. |
| Historical rules-11 host sizes | Care State **576 B**, canonical care snapshot **600 B**; nearby Match **72 B**, Protocol **2,384 B**, canonical match **100 B**. Largest packet in that checkpoint **138 B**, configured cap **240 B**, four discovered peers, eight transmit datagrams. These do not measure ESP free heap or stack. |
| Retained unit 1 checkpoint | **2d9f1ed**, own unhatched **600-byte** save unchanged through checks and reboot; migration checkpointed, sequence 0. All **261 SD hashes**, eight starter decodes and scene JPEG checked. Sampled coexistence: **868 touch polls / zero errors / zero lock misses**, fresh valid IMU, usage zero/writable. Physical record (historical local evidence omitted). |
| Retained unit 2 checkpoint | **2d9f1ed**, independent backup, own **600-byte egg** restored after reboot. All **261 files / 4,700,573 B** copied and verified, eight starters and scene decoded. **877 touch polls / zero errors / zero lock misses**, fresh IMU; no hatch. [Device record](TWO_DEVICE_PREPARATION.md). |
| Sampled Unit 1 resources — `2d9f1ed` | **179,055 B internal free** / largest **81,920 B**; **7,690,188 B PSRAM free** / largest **7,602,176 B**; **8,960 B minimum free main-task stack** during the observed run. Maximum frame **523,784 µs** includes initial JPEG decode and is not steady FPS or touch latency. These are not sustained battle/RF workload margins. |
| Physical acceptance still pending | Counted walking at several paces/orientations, idle/shake false positives, finger attack/capture/evolution flows, physical brightness and touch/motion wake, readable frames and audible cues, nonzero-step reboot/abrupt-loss usage recovery, two-board ESP-NOW consent/retry/rejoin/channel coexistence, battery/current and sustained heap/stack margins. |

The stat/type system, progression gates and rarity weights are authored game rules, not official franchise statistics. Scripted battle cohorts measure those particular seeds and profiles, not human win rates, walking accuracy or RF reliability. Pending physical checks must use each unit's own save and preserve existing SD assets.

## Run and build

These commands use current rules-12 source; they do not reproduce or flash the old `2d9f1ed` image. See the [care/capture guide](CARE_CAPTURE_RELEASE.md#migration-service-and-verification) for current focused checks.

From the repository root, with the existing C++17 compiler, Node 24.12+ and optional CMake:

```sh
npm run dev:direct             # local browser simulator at 127.0.0.1:8787
cmake -S . -B /tmp/digivice-host-check -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/digivice-host-check -j 4
ctest --test-dir /tmp/digivice-host-check --output-on-failure
npm run test:handheld
npm run test:power
npm run test:touch-poll        # shared-bus contention and failure cancellation
npm run build:esp:waveshare    # existing official ESP-IDF 5.3.6; build/size only
```

The private CMake directory avoids another worker's shared host build. Browser simulated input and the legacy CLI `walk` command remain development harnesses; they do not establish physical step accuracy. The ESP wrapper installs nothing and does not flash. Use the [installation guide](INSTALLATION.md) with the pinned artifact and the device's own backup. Later source edits require a new build and reviewed pins before installation.

# Care, capture and carousel source milestone

> Publication checkpoint: source and previously installed firmware are f74ee4c (schema 17 / rules 13). Both units passed bounded installation/save/SD/reboot checks; older entries below are historical. No hardware was accessed for this export. See [verified images and remaining acceptance](../releases/firmware-f74ee4c/README.md).

Current source uses **care schema 15 / rules 12 / 636-byte snapshots / catalog 6 / service container 14**. It adds larger creature carousels, effective care stats, saved starter offers, capture-result playback and screen idle. Both units passed the retained **`2d9f1ed`** hardware checkpoint, schema 14/rules 11/600 bytes; its physical results (historical local evidence omitted) do not verify these additions. Clean firmware source **`6e058e1`** is now compiled and pinned (historical local evidence omitted): **1,547,280-byte app**, **1,598,448-byte slot headroom**, **166,831-byte static DIRAM**, zero warnings under ESP-IDF 5.3.6 and the existing 16 KiB main-task stack. Both units passed their own app-only installation and the bounded checks below; physical gameplay acceptance remains open. Static linker figures are not runtime heap/stack margins. Numerical/storage definitions live in [CARE_CAPTURE_RULES12.md](CARE_CAPTURE_RULES12.md) and [STARTER_OFFERS.md](STARTER_OFFERS.md); this page covers the release flow and verification boundary.

## Verified device checkpoint

Both **`6e058e1`** app-only updates followed independent identity, fresh **98,304-byte layout/NVS backup**, prior-app digest and boot-layout checks. Each own save migrated **600→636 bytes**, preserving **62 prior leaf values**, then appended one offer event at sequence 1. Both eggs remain unhatched; three distinct saved offers, eleven starter previews and own-save software reboot passed. Each existing pack passed **261 hashes / 4,700,573 bytes**, with no new assets copied.

At about 73 seconds, each unit recorded **2,589 touch polls / zero errors / zero lock misses** with fresh IMU samples. The default 60-second idle reported blanking and stopped rendering while touch/IMU continued; console wake left each snapshot unchanged. Physical brightness, finger/motion wake, current draw and actual battle/capture/evolution remain untested. Unit 1/2 respectively had **173,887 / 173,823 internal bytes free** and **8,384 / 8,288 bytes minimum free main-task stack**; both had **7,690,188 PSRAM bytes free**. These are sampled diagnostics, not worst-case margins. Current sanitized physical evidence (historical local evidence omitted).

A later read-only Nearby preflight (historical local evidence omitted) reconfirmed Unit 1 unchanged but found Unit 2 unresponsive to application USB commands after boot output, including one bounded reopen. Its current save could not be re-read; no game or firmware writes were sent. **USB boot reliability is an open release blocker.** The completed checkpoint above is historical evidence, not a claim of current Unit 2 console availability. The host opening sequence is corrected and host-tested, but no physical recovery is established; [observe the screen before another USB open](USB_BOOT_RELIABILITY.md). No real RF exchange has been exercised while both eggs are preserved.

## Native controls

- **Meet Partner:** swipe left/right through one large creature at a time, tap **Choose**, then **Hatch** on the review screen. The choice remains uncommitted until Hatch saves successfully.
- **Partners:** swipe between members; **Stats + Evolve** opens swipable stats, care/XP, skills and type pages. **Digivolve** browses routes; **? Info** opens comparisons. Selection leads to a separate confirmation. Invalid routes explain the missing requirement.
- **Tactical battle:** swipe horizontally through Physical, Heavy and Magic; swipe upward to commit the displayed move. Heavy requires six energy. Selection alone does not attack. **Catch** opens a separate orb screen once the target is at half HP or below; flick upward from the orb. Downward/ambiguous browsing gestures do not commit a move.
- **Auto:** confirm the mode in Settings, then start the encounter's Auto battle. Physical/Magic choices remain independent 50/50 rolls with capture priority. The complete result saves before timed playback; inputs remain locked during playback. Floating move/damage text and flashes describe the committed exchange.

Browser navigation remains its separate preview interface. Its simulated steps/cards do not prove physical walking or NFC behavior. The current 50-state native renderer review (historical local evidence omitted) covers synthetic layouts, not physical finger use, brightness or frame timing.

## Care and progression

Care adds a bounded **0–5 offense bonus to Attack/Magic** and **0–5 protection bonus to Defense/Resistance**, derived from bond rank and current mood/fullness. Stats pages show base, care and effective values. HP, XP and base form curves stay unchanged. The exact formula and useful-care action table are maintained in [Care and capture rules 12](CARE_CAPTURE_RULES12.md#care-battle-stats-and-evolution).

A fresh hatch starts at **+1 offense / +0 protection**; one Feed gives **+1 / +1**. Across all **40 level-1 starter candidates × Physical/Magic**, isolated +1 care changes outgoing or incoming damage by a median **one point (9.09%)**, with a **0–20%** range. These are same-form, unguarded, neutral comparisons with the bonus on only one side, not encounter or human win-rate measurements. Impmon physical damage is **12→13** with +1 offense, or **12→11** with +1 protection on the defender. Equal care bonuses on both identical actors cancel. Measured early-game comparison (historical local evidence omitted).

The maximum +5 remains substantial: the same level-1 Impmon outgoing example is **12→17, about 42%**. That is not the fresh benefit. Bond rank 1 needs **50 bond**; exhaustive exploration of the **88 reachable Home-care states** from a fresh hatch reached at most **17**, with no XP. Care alone cannot reach the first rank from that starting state. Type, guard, reflection, damage floors and the opponent's protection still apply. Nearby freezes each partner's care bonuses into its agreed friendly matchup; practice retains its separate historical rules.

Care and practice award **no XP**; victories/captures award `20 + 6 × wild level`. Bond cannot bypass level gates. New starters hatch directly as level-1 Rookies. There is no timed or automatic baby-growth system, and the old full-mood Play/Rest bond loop is closed. Existing baby routes retain their explicit gates; the [tested progression examples](CARE_CAPTURE_RULES12.md#care-battle-stats-and-evolution) combine useful care, encounters and recovery.

## Eleven choices, with three saved offers

The eight fixed starters remain. An unhatched identity receives **three distinct extra Rookie offers**, chosen once from 32 reviewed forms and saved before display. Back, retry and restart retain the same choices; slots 9–11 refer to those saved offers. This is eleven total choices, not eleven random creatures or a reroll button. Hatching selects one companion at level 1 with no bonus XP/bond. Existing hatched companions are unchanged.

Every pool member has a level-1 profile, named moves, an outgoing route and exact-form DVA in the existing pack. The [audited 32-form pool and exclusions](STARTER_OFFERS.md) are the authoritative list. Armadillomon, Gizamon and Kamemon have one route followed by an authored leaf; a complete Mega chain is not promised. The pool adds no downloaded artwork.

Native setup checkpoints a one-time nonzero offer seed. The development service exposes authenticated `POST /api/starter-offers` with `{}`; repeated calls return the saved offer set. The deterministic core accepts `starter-offer-seed` only for an unseeded egg, and `hatch 9` through `hatch 11` require its saved slots.

## Capture odds and saved feedback

A legal aimed throw requires target HP at or below half, an available collection slot and fewer than three prior throws. The [authoritative capture curve](CARE_CAPTURE_RULES12.md#three-capture-throws) improves odds as HP falls, subtracts 10 percentage points for uncommon or 20 for rare, and five per higher wild level (maximum 25), with a 10% floor. At equal levels and exactly half HP: **50% common / 40% uncommon / 30% rare**; at exactly quarter HP: **70% / 60% / 50%**. Ordinary encounters currently match the partner's level, so the higher-level penalty normally contributes zero. Encounter rarity weights remain 70/25/5 after the initial Flicker; these are distinct from capture odds.

An aim miss spends one throw, records zero chance and does not consume capture RNG. An aimed throw consumes one roll. **Rules-12 capture attempts cause no enemy retaliation or HP damage.** After three misses/escapes, the wild creature leaves with current HP unchanged. Older active encounters finish under their saved rules, including their historical failure response.

The outcome saves before playback: **0–500 ms throw, 500–2,300 ms three containment wiggles, 2,300–3,900 ms result**. An aim miss reveals at 500 ms and holds until 2,100 ms. Exact odds appear only after the throw. Delayed polls extend readable stage time; they do not burst queued effects. Pause/cancel only affect visuals. Restart can replay the latest matching saved capture record, never reroll or award a second capture; old/stale records are rejected. The record does not preserve terminal target HP, so replay does not invent it.

## Screen idle

**Settings → Screen Timeout** cycles **Off, 30, 60, 120 and 300 seconds**; the default is **60 seconds**. A separate checked 16-byte `digi_idle` setting persists this preference. Quiet screens may blank after a successful pending-usage flush. Battle/capture playback, setup, Nearby, transfers, outstanding work and storage faults block blanking. Touch and IMU/step sampling remain active. A fresh touch or qualified motion wakes the display; the wake contact is consumed, so release before choosing an action. A new encounter also wakes the screen. This is display/backlight idle, **not CPU deep sleep or measured battery savings**.

## Migration, service and verification

Schema 14 grows **600→636 bytes**, adding a capture record plus offer seed/three IDs. Migration keeps prior game data and active encounter rules; it does not hatch, draw offers or capture anything. Explicit offer initialization is a separate saved event. Service container **13→14** archives the original rules-11 store, replays frozen history and accepts new `rulesVersion:12` batches. Old pending batches require reconciliation, not relabeling; current retries preserve their result. Practice remains rules 7/store 8. Nearby's care-bearing matchup requires matching current peers; it remains ephemeral with no XP/rewards and no cryptographic peer authentication.

The handheld owns immediate offline play. Local Nearby multiplayer is already wired through ESP-NOW discovery, challenge/accept and shared battles; it does not depend on service pairing or synchronization. [First-use controls and the current egg-only RF testing limit](FIRST_USE_CHECKLIST.md). The development service supplies assets/catalog, browser pairing and replay-validated revisions; **native service account pairing, remote save sync and a durable outbound event journal remain unimplemented**. Wi-Fi setup does not synchronize saves. Default service access is loopback, with explicit bounded HTTP LAN development mode; standard firmware requires HTTPS. No production origin/deployment is established. Replay validates legal events, not whether reported steps physically occurred. No raw GPS routes or child profiles are uploaded.

From the repository root, using the existing toolchain:

```sh
npm run dev:direct                     # browser preview: http://127.0.0.1:8787
cmake -S . -B /tmp/digivice-care-check -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/digivice-care-check -j 4
ctest --test-dir /tmp/digivice-care-check --output-on-failure
./scripts/node.sh --test tests/service-care-capture.test.ts tests/care-capture-state.test.mjs
npm run test:idle
npm run test:touch-poll
npm run build:esp:waveshare             # build/size only; does not install or flash
```

Launching the ordinary service opens its data directory and can migrate it; migration verification uses isolated temporary stores. The [installation guide](INSTALLATION.md) remains tied to separately reviewed build pins and each board's own backup. Do not infer a new installed version from a successful host test or source edit.

Combined host/ESP verification, artifact pinning and the bounded two-unit installation checks are complete; the build evidence (historical local evidence omitted) records the host scope and counts. Remaining work, in order: perform real finger/carousel/capture/evolution and audio checks, including saved capture-result restart once a partner is deliberately chosen; measure walking accuracy, physical brightness and touch/motion wake, sustained heap/stack and current; test two-device Nearby consent/retry/rejoin and hotspot coexistence; then implement native service account pairing/outbox/sync against a reviewed reachable service. GPS/NFC hardware and deep sleep remain separate work. Both units' earlier measurements are retained in [the walking checkpoint](WALKING_NEARBY_RELEASE.md); completed command-driven checks do not establish the remaining human/RF acceptance.

Source contracts: [game](../core/game.cpp), [combat](../core/combat.cpp), [native UI](../firmware/runtime/device_ui.cpp), [capture playback](../firmware/runtime/battle_presentation.cpp), [screen-idle adapter](../firmware/main/handheld_idle.cpp) and [service](../service/server.ts).

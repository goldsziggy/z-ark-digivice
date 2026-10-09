# Digivice readiness for tomorrow

The current build is ready for a supervised **Mac browser playtest**. Native simulations support the intended early progression, and the browser evidence below exercises real controls against the shared C++ core. It is **not a verified wearable park demo**: the physical LCD renderer, buttons, pedometer configuration/calibration, NFC, battery and radio/storage behavior remain separate hardware work. Browser gameplay requires the local service; standalone offline behavior is demonstrated by the portable firmware host harness.

## What a 20–30-minute session produces

These are deterministic native simulations of the current rules, not observed human walks. Each nominal row below contains all eight starters × four seeds. Everyone starts at level 1, keeps the original partner, and keeps eight carried companions. Tactical chooses the best immediate visible damage and attempts capture when legal; no cards are used in the pacing model. The model explicitly chooses the first legal Digivolution. It does not automatically release companions in these rows.

Cadence is **100 steps/minute while walking**, with one encounter per 100 credited steps. **Walking stops during every interaction and animation.** Onboarding and initial mode selection happen before the timer. Assumed stops: encounter notice 3 seconds; Tactical command 4 seconds plus 1.2 seconds for capture preparation; Auto confirmation 4 seconds plus 0.65 seconds per trace row; result acknowledgement 2 seconds; Care navigation 4 seconds; Recover confirmation 2 seconds; release or evolution 10 seconds. These times include assumed navigation/decision effort, not individually counted Next taps. The wider 1,408-session grid also tests 80/120 steps/minute and half/double assumed input times.

Values are **median [minimum–maximum]**. The follow-up uses the usual statistical median; the integer outcomes below also match the earlier lower observed p50. Walking/stopped minutes are means; a small unspent remainder may remain.

| Session | Levels gained | Encounters | Captures | Newly obtained by capture | Walking / stopped minutes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20 min Tactical | +4 [+3–4] | 12 [11–13] | 7 [7–7] | 6 [5–7] | 12.58 / 7.40 |
| 20 min Auto | +4 [+4–4] | 15 [14–15] | 7 [7–7] | 6 [5–7] | 15.06 / 4.93 |
| 25 min Tactical | +4 [+4–5] | 15 [14–16] | 7 [7–7] | 6 [5–7] | 15.51 / 9.48 |
| 25 min Auto | +5 [+4–5] | 19 [18–19] | 7 [7–7] | 6 [5–7] | 18.90 / 6.09 |
| 30 min Tactical | +5 [+4–5] | 18 [17–19] | 7 [7–7] | 6 [5–7] | 18.55 / 11.44 |
| 30 min Auto | +6 [+5–6] | 22 [22–23] | 7 [7–7] | 6 [5–7] | 22.67 / 7.33 |

“Newly obtained” counts actual native journal additions caused by capture, excluding the founder and forms already captured or evolved. These are form IDs, not deduplicated canonical species; variants remain separate. The older metric “distinct captured forms” has median 7 [5–7] because it can include an already-known form. Every row stops adding captures when seven captures plus the founder fill the eight slots. The separate explicit-release policy reaches a median **12 genuinely new forms Tactical / 14 Auto at 25 minutes**, with additional management stops; it does not silently discard anything in the product. Across the complete speed/duration/policy grid, final levels range 4–8 (+3–7 gained) and captures 7–23. Updated measurements and definitions (historical local evidence omitted) · Earlier four-way comparison (historical local evidence omitted).

This supports keeping the present XP and encounter spacing. The main uncertainty is the amount of stopping: roughly **38% of a nominal 25-minute Tactical session** is spent on inputs/animation, versus **24% in Auto**. A human playtest must establish whether that feels pleasant. Faster XP is not needed to meet the stated early-level goal.

## Time and steps to the first Digivolution

All first evolutions in this sample were the starter's first Champion route, requiring **level 5 and bond 20**, followed by an explicit modeled 10-second review/confirmation. Using the full 30-minute cohort avoids reporting only the fastest successes:

| Mode | Time to first Champion, median [range] | Credited steps, median [range] |
| --- | ---: | ---: |
| Tactical | 20.05 min [18.14–25.07] | 1,200 [1,200–1,500] |
| Auto | 16.05 min [15.74–20.04] | 1,200 [1,200–1,500] |

By 20 minutes, **16/32 Tactical and 31/32 Auto** sessions have evolved. By 25 minutes, that is **31/32 and 32/32**; the remaining Gomamon session reaches it at 25.07 minutes. By 30 minutes, both are 32/32. Sessions that have not reached it at an earlier cutoff are recorded as “not yet,” not failures. These times start after hatching and mode selection; they are simulation times, not browser automation runtime.

The new instrumentation also records capture journal additions and preserves every previous session result and per-encounter record across all **1,408 cases**. No progression rule was changed. Timing evidence (historical local evidence omitted).

## Rarity and capture outcomes

After the fixed first Flicker encounter, the native selector chooses **70% common / 25% uncommon / 5% rare**, then uniformly chooses an eligible form inside that bucket. Eligibility depends on the current partner's stage and level. Rarity does not alter combat stats or capture odds. Repeats are allowed; no rare is guaranteed.

The independent 65,536-selection probe observed **70.71% / 24.45% / 4.84%**. All eligible forms appeared at the respective stage gates. Early pools contain 37 common / 22 uncommon / 6 rare forms, so a particular early rare has about a **0.833% per-draw chance**. Rarity evidence (historical local evidence omitted).

At 25 minutes / 100 steps per minute, **18/32 Tactical and 20/32 Auto sessions see at least one rare**. The keep-eight runs encounter 24/29 rares and capture 10/10 respectively; 12/17 of those rare encounters begin with a full roster. These are aggregate counts across 32 sessions, not per-player promises. The independent explicit-release policy captures 20/24 Tactical and 24/27 Auto rares. The simulation does not award hypothetical captures for the new Make room button.

In the nominal keep-eight rows, failed capture attempts have median **5**, range **0–16**; failure does not delete progress. Median forced retreats are zero, with up to 3 at 20 minutes, 3 Tactical / 4 Auto at 25 minutes, and 4 Tactical / 5 Auto at 30 minutes. A retreat restores modest HP at Home and awards no victory/capture XP. The roster fills at median **11.25 minutes Tactical [10.40–17.19]** or **9.17 Auto [9.03–13.27]**. A full roster refuses capture before spending an attempt or drawing RNG. New encounters let the player explicitly release an inactive companion while preserving the current fight.

Actual browser evidence includes a **60% capture attempt that fails**, increments the attempt counter once, applies retaliation (partner HP 72→60), and leaves the collection unchanged. Make room was navigated with two buttons: Back sent no request, double confirmation sent one request, a lost reply plus service/browser restart replayed the exact receipt, and a later capture succeeded as **member #9**, without reusing released ID #2. Encounter outcomes (historical local evidence omitted).

## What was actually played in a running browser

A fresh, isolated browser journey now goes **Pair → choose Agumon → hatch → select Auto → 12 encounters → seven captures/full eight-member roster → Recover fully → confirm Greymon**, using only left Next / hold Back / right Confirm. It contains **no injected save, direct game-event seeding or native prediction**. All 40 game request batches / 53 native events originated from those controls. The result is **level 5, XP 438, bond 150, 1,200 simulated steps**, with founder member #1 preserved. It stops at the first Digivolution. Complete fresh journey (historical local evidence omitted).

This automated browser run took 66 seconds with reduced motion and explicit +100-step buttons. It recorded 213 Next taps, 72 Back holds and 148 right confirmations, including cancellation/no-write checks. **Those are test actions and runtime, not a 66-second human park session**: clicking +100 does not involve a minute of walking, and automation does not model human reading/decisions. The amount of navigation is a concrete reason to measure human stopping time tomorrow.

| Named milestone | Actual browser outcome | Setup boundary |
| --- | --- | --- |
| Pair, choose egg, hatch | Fresh Agumon hatched once; review Back and unavailable-service recovery tested | One current starter journey; all eight choices were displayed, not eight complete browser playthroughs |
| Care and full recovery | Actual reviewed batches heal and preserve identity/XP; already-full action disabled | Fresh journey uses normal battle damage; separate restart test uses a genuine damaged save |
| Walking, encounter, level gain | Twelve actual +100-step button presses, confirmed Auto starts, seven captures and level 1→5 | Steps are simulated; no physical pedometer |
| Tactical wild battle and capture | Two-button choices weaken and capture Flicker; Back cancels capture preparation; a separate 60% attempt fails with retaliation | Fresh Tactical journey plus legal-history setup for the failure case |
| Strict Auto | Explicit starts resolve, save and replay without additional inputs/rewards | Fresh journey; separate historical/restart cases use saved fixtures |
| First Digivolution | Actual progression menu, review and Greymon confirmation with stable member #1 | Fully earned through button-driven battles in the fresh journey; no training shortcut |
| Full roster and Make room | Release chosen inactive member, preserve fight, survive lost reply/restart, then capture as member #9 | Full roster prepared by legal native events; release/capture path itself navigated with two buttons |
| Companions, journal and release | Select a high-ID companion, browse paged journal, release/retry, retain journal and allocate a new ID | Large collection prepared by legal current events; setup battles are not claimed button-played |
| Simulated NFC card | Spark sends card event 1, gives +5 attack boost, refuses a second card and consumes boost on attack | Actual two-button card/attack path; no physical NFC reader or swipe timing tested |
| Practice Tactical/Auto | Named attack/defence, explicit Auto, reward-free replay and practice locks exercised | Actual browser controls; selected companions/training may be fixture-prepared |
| API outage/restart | Exact pending request and identity survive; retry returns original receipt with no duplicate reward | Real HTTP/browser/service fault injection while page assets remain available |
| Standalone offline and durable boot | Local care/capture, resumable assets and save recovery pass | Native firmware host harness with mock adapters, not browser-offline play or hardware |

The milestone matrix (historical local evidence omitted) binds outcomes and setup boundaries to current source and binary hashes. Geometry checks are supporting layout evidence, not the basis for claiming those journeys. Current reruns use the exact `build/park` binaries from the release. Current Tactical/evolution flow (historical local evidence omitted) · Current encounter/retry flow (historical local evidence omitted) · Current journal/card flow (historical local evidence omitted).

### The previously baseline-only roster fixture

The old test assumed that a fixed six-capture sequence produced **Calumon as member #7**, and that its journal entry was on the first page. The new weighted selector intentionally breaks that old encounter-order assumption. This was a stale fixture, not evidence of a broken collection feature.

The repaired test generates a bounded legal history with the current native core and replays it through normal authenticated save-sync batches. Its deterministic setup reaches Calumon after **147 encounters / 848 events**, as member **#135**; it then actually navigates the catalog, journal pages, partner selection, practice and release/restart flow. A subsequent capture receives **#136**, and released #2 remains in the journal. That suite now passes on the current build. Those 147 setup fights were accelerated via API and are explicitly **not** presented as a full button-driven playthrough. No product logic changed to make the test pass.

## Recovery, offline and restart boundaries

Actual two-button recovery starts from a genuine damaged-save fixture: **52/92 HP, 70 energy**. The native count is two Rest events. One confirmed batch produces **92/92 HP, 100 energy**, one service revision and two native sequence increments; XP, identity and other companions remain unchanged. Back sends no command; duplicate input sends one batch; a dropped acknowledgement survives service/browser restart and retries the exact body and receipt. Recovery is disabled once full. Connection/recovery outcomes (historical local evidence omitted).

The browser was tested with gameplay APIs returning 503 while shell assets remained reachable. Pairing and paired-save loading show truthful errors; identity and pending bytes survive; reconnect restores the same save without an automatic game command. **This is not an offline-browser gameplay claim.** A cold browser page still needs the service, and there is no service worker.

Offline handheld evidence is native host execution with fake motion/storage/network adapters: local walk→capture makes zero new network requests during capture, retains 8,816 downloaded asset bytes, resumes an interrupted asset at 4,096 bytes, and restores the care state. The practice serial harness survives five fresh processes and exact retries with no additional writes. A synthetic one-hour wild encounter credits 7,200 steps in 120 verified checkpoints, with peak pending queue 60 and no battle HP/RNG/turn changes. These are executable host results, not physical board observations. Device host audit (historical local evidence omitted).

Confirmed motion pending before a checkpoint is RAM-only. Sudden power loss can lose that uncommitted interval; a long practice can hold up to 4,096 confirmed steps. The firmware does not invent missing steps or silently replay an uncertain write. Physical NVS/SD power-loss behavior and pedometer accuracy remain untested.

## Practical gaps for tomorrow

- Run one timed human Tactical session and one Auto session on the Mac. Time actual navigation and decisions, count button taps, and record capture failures, recovery visits, release choices and confusion. Use those measured stops to calibrate the walking model; the Mac test does not measure an outdoor walk.
- Use `npm run dev:direct`, then open `http://127.0.0.1:8787`. Buttons are left tap Next, left hold Back, right tap Confirm. Walk/card controls are simulated. No phone hosting or native phone app is configured.
- Hardware bring-up still needs LCD rendering and physical buttons first, then verified sensor/NFC/storage/radio integration and measured heap, stack and battery current. Three successful ESP builds do not satisfy those gates. Nothing has been flashed.
- Art still has 14 explicit gaps; missing art uses labeled placeholders. The private artwork set has not changed.
- Grok produced no review: Cursor CLI authentication failed, including normal-environment model access. No paid retry or credential change was made.

The release baseline for this follow-up is `e7b905f`; all new work is bounded measurement, browser evidence and test-fixture maintenance. [Build budgets](ESP_BUILD.md) · [Architecture and authority boundary](ARCHITECTURE.md).

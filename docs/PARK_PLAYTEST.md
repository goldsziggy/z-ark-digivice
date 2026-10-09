# Park-session milestone

For the follow-up report with actual session ranges, new journal forms, first-Digivolution timing and named browser milestones, read [readiness for tomorrow](TOMORROW_READINESS.md).

The target is a relaxed 20–30-minute early walk: hatch a partner, meet several creatures, try battles and cards, make captures, gain a few levels and review an available Digivolution. The game remains usable offline on the handheld core. Browser play still needs its local service. There are no missed-care deaths, waiting timers, automatic releases or network calls inside battles.

## Decisions and contracts

Keep **100 credited steps per encounter** and the existing XP curve. A frozen baseline already reaches the intended early levels; increasing XP or encounters would worsen collection management. Steps continue to accumulate during a wild encounter. At Home, the next positive Walk event can consume one queued encounter; queued credit does not start Auto or another fight by itself. The native state exposes `stepCredit`, `queuedEncounters` and `stepsToNextEncounter`; no browser formula decides encounters.

New encounters use authored **70% common / 25% uncommon / 5% rare** bucket weights, then uniform selection within the eligible bucket. Selection is a bounded native function of seed, encounter number, partner stage and level; it neither advances capture RNG nor uses care commands, wall time, location or the network. The first encounter remains Flicker. Rarity changes frequency only, with no hidden stat bonus or capture penalty. These are prototype choices, not official species attributes.

The explicit table contains 123 common, 112 uncommon and 41 rare forms. Every eligible stage has all three buckets. The early 65-form pool contains 37/22/6 forms respectively; each common form is more frequent than each uncommon form, which is more frequent than each rare form. About 13 independent 5% draws gives a 49% mathematical chance of at least one rare sighting; that is neither a capture guarantee nor a measured playtest result. [Authored table](../data/encounter-rarity.json).

**Recover fully** reviews one bounded batch of ordinary native Rest events. Rest still restores up to 25 HP and 25 energy, with the existing useful-care bond behavior and no XP. The native `recoveryRestCount` defines the batch; the browser sends it through existing exact-retry save sync, and firmware checkpoints one complete candidate. Full or unavailable recovery performs no write. Manual Rest remains available. This changes the number of confirmations, not the healing rule.

**Make room** allows an explicitly confirmed release of a chosen inactive companion while a new encounter waits for input. It preserves the current opponent, HP, capture attempts, RNG, turn, prepared card and active partner. The permanent journal survives release. The eight-member limit remains; nothing is discarded automatically. Strict Auto playback still accepts no battle inputs. Encounters saved under older rules keep their historical restriction.

These transition changes use **care rules 10 / schema 13 / service container 12 / catalog 6**. The canonical snapshot remains 576 bytes. Frozen rules 9 preserve previous histories and exact receipts; active older encounters retain their recorded profiles and behavior. Practice remains **rules/schema 7, store 8**, with unchanged combat formulas and profiles. [Service contract](../service/API.md) · [Persistence contract](COLLECTION_RULES.md).

## Measured baseline and timing limits

The baseline is commit `e6f352300ee6c049b7e21598009db4cf52cca0a6`. Each variant runs 1,408 deterministic native sessions across all eight starters, four seeds, 20/25/30 minutes, 80/100/120 steps per minute, Tactical/Auto and two explicit roster policies. Additional 0.5× and 2× decision-time cases test sensitivity. Onboarding occurs before the session clock. Walking stops during navigation, commands and animations. The input and animation durations are authored assumptions, not observations of people; CPU runtime is never treated as walking or decision time.

At 100 steps/minute and ordinary assumed input speed, keeping eight companions:

| Duration | Tactical median | Auto median |
| --- | --- | --- |
| 20 minutes | Level 4; 12 encounters; 30 Rest events | Level 5; 14 encounters; 37 Rest events |
| 30 minutes | Level 6; 18 encounters; 44 Rest events | Level 7; 22 encounters; 54 Rest events |

Every nominal 100-step/minute cohort fills its seven capture slots by 20 minutes, with six or seven distinct captured forms. Median fill time is about 11.5 minutes Tactical / 9.4 minutes Auto. That is why collection flow matters before increasing encounter frequency. A separate explicit release-oldest policy measures continued collecting; it is a simulation policy, not automatic product behavior.

To isolate the convenience of recovery from its full-energy goal, the comparison includes both **manual full recovery** and **one-confirmation full recovery**. At 25 minutes / 100 steps per minute, Tactical median confirmations fall from 38 to 16, and Auto from 46 to 19. The corresponding underlying Rest events remain 38 versus 39 and 46 versus 46; different available walking time can change which fights occur. Both comparisons still use the old encounter selection. Final rarity measurements are separate. Frozen baseline (historical local evidence omitted) · Manual full recovery (historical local evidence omitted) · One-confirmation comparison (historical local evidence omitted).

## Final candidate results

The same 1,408 candidate sessions pass all 22,528 time, outcome and capacity assertions. At 25 minutes / 100 steps per minute, keeping eight companions, Tactical reaches median level 5 with 15 encounters; Auto reaches level 6 with 19 encounters. Both have seven captures / seven distinct forms at the median. A first eligible evolution occurs in 31/32 Tactical and 32/32 Auto sessions. At 20 minutes that falls to 16/32 Tactical and 31/32 Auto; evolution is available early without being guaranteed in every short session.

The independent 65,536-selection native rarity probe observes **70.71% common / 24.45% uncommon / 4.84% rare**, with all eligible forms observed at each stage gate. At 25 minutes, 18/32 Tactical and 20/32 Auto sessions see a rare. Keeping all eight members leaves some rare encounters with a full roster; Make room now permits an explicit choice. The simulation does not count imaginary captures from that new capability. Its separate preemptive-release policy measures continued collecting and management time. Complete comparison and limitations (historical local evidence omitted) · Machine-readable results (historical local evidence omitted).

Current actual two-button browser journeys pass setup, capture cancellation and failure, partner changes, Greymon Digivolution, Tactical and strict Auto. Make room passes Back without a request, duplicate confirmation, a dropped acknowledgement followed by service/browser restart, exact encounter preservation, and a subsequent capture with a new monotonic member ID. Queued-step display does not invent steps. Encounter flow evidence (historical local evidence omitted) · Current progression journey (historical local evidence omitted).

The native core passes 161,899 sanitizer checks, 25 generator tests and 3,877 catalog checks. Service tests pass 239 module cases and 25 focused cases with strict TypeScript. Firmware host tests pass 22,203 counted runtime assertions plus SaveStore checks and offline demos. All nine CMake suites and all three ESP profiles pass; hardware execution remains untested. Native evidence (historical local evidence omitted) · Service evidence (historical local evidence omitted) · Device host audit (historical local evidence omitted) · Compiled ESP budgets (historical local evidence omitted).

## Milestones and required evidence

| Milestone | What the player must be able to do | Verification target |
| --- | --- | --- |
| Setup | Pair locally, choose an egg, review and hatch once | Expired pairing, unavailable service, cancel, lost acknowledgement and reload |
| First care | See needs, use care and confirm recovery | Native count, stale partner/sequence rejection, atomic batch and exact retry |
| Explore | Credit steps and see a new encounter | Saved credit, deterministic rarity, old encounter compatibility and bounded motion queue |
| Tactical battle | Read skills/guard, use a card, attack and capture | Real two-button journey; capture preparation Back makes no request; failure remains playable |
| Auto battle | Review then resolve once | No mid-battle choices; saved trace replay cannot repeat rewards |
| Collection | Inspect individual companions and choose a partner | Stable IDs, duplicate species, per-member care and practice lock |
| Full roster | Explicitly make room without losing the current new encounter | Cancel, wrong member, lost acknowledgement/restart and later capture |
| Progression | Gain levels and review a legal Digivolution | Native gates, Back/confirm, preserved identity/HP fraction and historical replay |
| Interrupted walk | Continue the local game without networking or optional art | Save/readback faults, corrupt asset fallback, motion backlog and boot recovery |

Actual browser baseline tests reproduced misleading connected/setup wording after service failure and the full-roster capture dead end. They also exercised setup, capture cancellation, partner selection, training, evolution and both modes with Next / hold Back / Confirm. Browser geometry tests use 306, 201.6 and 140.16 CSS-pixel circles; they do not establish physical LCD readability. Connection evidence (historical local evidence omitted) · Gameplay evidence (historical local evidence omitted) · Full-roster reproduction (historical local evidence omitted).

The motion integration previously drained confirmed steps only at Home and did not surface a latched counter fault. A sustained synthetic encounter could fill its 4,096-step queue. The thin delivery policy now checkpoints supported Walk events during wild encounters before acknowledging their step batches; active practice still holds new Walk events. Fault diagnostics retain confirmed pending steps, and an explicit reanchor is possible only after they drain and storage is healthy. Steps across an unverified sensor gap are never invented. Pending motion deltas are RAM-only: sudden power loss can lose uncheckpointed steps (normally fewer than 100 steps or 30 seconds, potentially up to 4,096 while practice blocks delivery). Boot anchors the sensor anew to avoid duplicate credit; it cannot reconstruct that gap. This is host/source behavior; the physical pedometer remains disabled pending sensor configuration verification.

## Review and remaining device gates

The requested bounded `grok-4.7-high` review failed CLI authentication before returning a model response. Normal CLI model access also fails without the isolated configuration. There are **no Grok findings** to cite; no retry, login, credential or subscription change was made. Independent native, service, browser and firmware verification supplies the evidence above. Sanitized attempt record (historical local evidence omitted).

The provisional target remains Waveshare ESP32-S3-Touch-LCD-1.46, SKU29565, 412×412, 16 MiB flash / 8 MiB PSRAM. Successful compilation is separate from a working handheld display. LCD/touch/buttons, actual walking accuracy, NFC antenna and wiring, SD power-loss behavior, radio coexistence, heap/stack high-water and battery life need the real board. GPS and peer battles are deferred. No flashing, purchases, remote upload or CAD changes belong to this milestone. [Hardware sources and open questions](PARK_HARDWARE.md) · [Measured builds](ESP_BUILD.md).

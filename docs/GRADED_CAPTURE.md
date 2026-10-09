# Graded capture timing

This prepared refinement supersedes `110212b`. Both physical units remain on
`8be26c6`; no device access or flashing is part of this work.

## What the player sees

The target is one green annulus at 75% opacity, drawn over the encounter
background and behind the actual creature. It replaces the two target outlines.
The shrinking ring is red, orange or green. A short matching word and exact
percentage make the meaning usable without relying on color alone.

Press anywhere in the main play area to throw immediately. Navigation stays
separate. The 450 ms guard, physical release and settled save/presentation state
are still required before another throw. There is no hold/release timing.

The ring repeats its 2.4-second shrink from radius 100 to 20. The target radius
remains `[52,60,68,76][formId % 4]`. Integer Q8 radii give the browser and firmware
the same inclusive boundaries:

| Distance from target radius | Quality | Factor on eligible odds |
| --- | --- | --- |
| At most 12 pixels | Green | 100% |
| More than 12, at most 24 pixels | Orange | 50% |
| More than 24 pixels | Red | 10% |

## Probability and authority

Let `B = captureChance(state)`, the existing eligible percentage. If `B` is zero,
the throw is unavailable. Otherwise the exact integer percentage is
`max(1, floor(B * factor / 100))`. For a 52% encounter, the three chances are
**5%, 26%, 52%**. For the 10% minimum eligible base they are **1%, 5%, 10%**.
Green means the full eligible chance, never a guaranteed capture or an extra
bonus. No grade can exceed `B`.

For current encounters, `B` already includes remaining wild HP, the uncommon/rare
penalty and the higher-wild-level penalty. Those factors are applied once.
Care improves combat stats and can help weaken the opponent; it has no separate
capture multiplier. Older active encounters retain their own existing base-odds
formula. The timing factor is applied once, after that formula.

Each accepted throw of any grade consumes one attempt and one existing capture
RNG draw. The native core rolls `random(state) % 100 < chance`; it persists that
same percentage and outcome in `lastCapture`. The displayed prospective chance
uses the same shared integer function, and result playback shows the saved
percentage. Three wiggles reveal this one result, not three extra rolls. A red
escape is an unsuccessful capture roll, not an automatic aim miss.

The new additive event is `{type:"ring-capture", value:phaseMs}`, where phase is
an integer from 0 through 2399. Core code derives quality from that phase and
the actual saved opponent form. Clients cannot supply a result or percentage.
Client timing remains an observation, not proof of a physical gesture.
Service startup checks the native contract and advertises
`captureTimingQuality:1`; the new browser UI fails closed without it.

`RingCapture` is appended to the action enum. Existing `Capture` and `Flick`
histories retain their original RNG, miss and response semantics. New ring
throws are calm, including in an older active encounter: no counterattack,
three attempts, then a gentle end. Pending-command retries retain the exact
event and receipt rather than sampling timing or drawing RNG again.

## Saves and resources

Schema 20, rules 13 and the 664-byte snapshot layout are unchanged. Existing
saves upgrade without resetting care, companions, independent seeds or pending
encounters. Valid saved capture chances now include 1–9%. An older build such
as `110212b` rejects those new records: rollback requires compatible firmware
or each unit's own pre-upgrade backup. Upgrade compatibility is not a promise
that older firmware can read every new result.

The native renderer keeps its 208×208 partial rectangle, 86,528-byte transfer,
existing framebuffer and DMA stripe. The 33 ms frame-start target and minimum
10 ms owner wait remain. No extra pixel buffer is allocated. Host scheduling
tests use injected render/transfer costs; they do not measure the new shader's
device FPS, power draw or input latency.

## Verification and preview

```sh
npm run test:capture-ring
npm run test:capture-runtime
npm run test:modules
npm run build:esp:waveshare
```

Focused checks cover grade boundaries, all target radii, nonzero red captures,
maximum green eligible odds, displayed-versus-rolled chance, legacy replay,
low-chance save round trips, exact retry receipts, repeated input and clipped
render parity. Private native/browser previews use the existing audited
Agumon artwork and meadow background. No new artwork is imported or published.
Evidence is under `../deliverables/quality-capture-20261009/`.

**Current source:** `171cda7` retains this 60-member expansion and adds three XP companions under schema 22/rules 15 with 2,964-byte snapshots. See the [XP companion guide](XP-COMPANIONS.md) and [publication validation](../PUBLICATION_VALIDATION.json). Capacity/build measurements below describe the earlier `66ceaaf` checkpoint.

# Sixty carried Digimon

The carried collection now holds **60 individual Digimon**. An existing roster
of eight becomes **8/60** after migration. No reserve box, automatic release or
replacement rule is added. Each member retains its stable ID, form, level, XP,
HP, care and capture history; the active partner remains selected.

This is a prepared software/firmware update for the Waveshare ESP32-S3-Touch-
LCD-1.46 standard-glass board (SKU29565). The physical playtest devices remain
on `8be26c6`. No device access or flashing is part of this work.

## Player behavior

- A successful capture at 59/60 appends one member. At 60/60, capture is disabled
  and authoritative rejection consumes no attempt, RNG draw, reward or save
  revision. Existing members are never replaced.
- Auto battle at full capacity continues fighting without a capture pause.
  Both previews explain the full roster before the player commits.
- Release remains an explicit confirmation for a non-partner. It removes that
  individual's care/XP but preserves the obtained-form journal and lifetime
  captures. Released IDs are never reused.
- The browser retains its existing **Make room** route during current wild
  encounters. Native release is still **Home → PARTNERS → STATS + EVOLVE →
  RELEASE DIGIMON**. Native battles say **FINISH BATTLE TO RELEASE**; no new
  in-battle release route is included.
- Selection and evolution use the existing member's slot. Starter selection
  remains single-use. Native Nearby trading swaps one member for another, so
  trading works at 60/60 without a spare slot; another playable partner must
  remain if the active partner is offered.
- The browser shows three members per page, up to twenty pages. Native browsing
  wraps through sixty entries and requests only the selected member's artwork.
  Increasing ownership does not decode sixty sprites at once.

Current player-facing labels and messages use **Digimon**. Frozen replay source
and historical evidence retain their original text; browser presentation maps
the two known historical messages without modifying saved receipts.

## Save and replay compatibility

Current saves are **schema21 / rules14**, with a **2,952-byte** canonical
snapshot. The 52 added slots cost 2,288 bytes. Schemas1–20 read their original
eight slots, validate their original capacity, and initialize the added slots
empty. Invalid or future saves fail closed; migration never truncates a roster.

Rules13 has a frozen eight-slot executor. The service replays and archives its
old histories before converting to a schema21 baseline and a new rules14
suffix. Old pending requests retain their original bodies and require explicit
reconciliation; relabeling an archived batch cannot run it twice. Current
retries retain their exact idempotent result.

The historical `auto` event retains its eight-slot strategy when finishing a
carried-over rules13-or-earlier encounter. Current manual capture and
`auto-fight` use all sixty slots immediately. New encounters use rules14.
Stored combat rules, graded-capture arithmetic and independent world/pacing
seeds otherwise remain intact.

Older firmware cannot read schema21 saves. **Downgrading requires compatible
firmware or that same unit's complete pre-upgrade backup.** It must never mean
discarding members nine through sixty. Existing prepared Nearby trade journals
also need their original consent epoch preserved through migration; new trades
require compatible firmware on both peers.

## Resource and validation boundary

Measured host `State` is 2,924 bytes; snapshot size is fixed at 2,952 bytes. A
3,641-case sweep of legal form/level combinations with sixty members, maximum
IDs and a full journal produced at most **38,430 JSON bytes**. The state buffer
is bounded to **65,536 bytes**. Native diagnostic JSON uses temporary PSRAM on
the display board and reports allocation failure without changing the save.

Native Controller (328 bytes), Model (280 bytes) and RGB565 framebuffer
(339,488 bytes) are unchanged. Save recovery scratch is owned by the main
application object instead of large automatic arrays. The main task stack is configured to **32,768 bytes**. Compiler analysis of
79 application translation units gives a conservative known application call
chain of **26,624 bytes**, leaving **6,144 bytes before unmeasured SDK and
indirect-call stack use**. This is not physical high-water evidence. Trade recovery and task
stack budgets are checked separately because they hold several game states.
The official SDK partition generator fits two 2,952-byte care slots, two
6,096-byte trade journals, one additional journal update and an 8,192-byte
other-data reserve into the existing 65,536-byte NVS partition. This is an
offline capacity check, not a physical garbage-collection or power-loss test.

The final build manifest and validation report record compiled ESP flash/RAM,
task-stack analysis, NVS capacity checks, migration, full-roster trade and
capture tests. Host sizes and static stack analysis are not measurements of
physical free heap, radio behavior, frame rate or battery life. Device reboot,
power-loss and sustained-runtime acceptance remain a later physical check.

## Build and run

```sh
npm run dev:direct
# Open http://127.0.0.1:8787
npm run test:publication
bash scripts/build-esp.sh waveshare
```

The publication test command covers the reduced public asset set. The full
development suites also require omitted private scenery fixtures.

The ESP build command compiles locally; it does not flash a device. Prepared
firmware archives contain only build images and their manifest, without player
saves, credentials or private artwork.

# Capture ring and independent encounter worlds

Current timing probabilities and shading are documented in [Graded capture](GRADED_CAPTURE.md).

Historical prepared milestone `0f56258`. The [direct-capture refinement](DIRECT_CAPTURE.md)
supersedes its center/hold/release interface while retaining its seeds and saves.

This source update replaces the capture flick with a timed center tap and fixes
the shared monster sequence. It is prepared for the Waveshare 1.46 board. Both
physical units remain on `8be26c6`; this update has not been flashed. Device
availability must be reconfirmed before installation.

## Try the capture flow

Run `npm run dev:direct`, open `http://127.0.0.1:8787`, pair a virtual device and
hatch. Explore in Tactical or Auto mode. Capture becomes available when the wild
creature is at or below half HP and the collection has space. Auto finishes the
crossing attack animation before inviting a manual throw.

Tap the creature's center when the shrinking white ring enters the visible target band.
The ring turns green within the band. Pressing freezes the ring; releasing
within 1.5 seconds throws. The browser's right action button, D key, or focused
Confirm activation uses the same timing. Native touch also has a THROW button.
The touch target stays generous even when the animated ring is small. The
creature is shown in a 176-pixel art area on the 412-pixel reference screen.

An **On target** throw uses the existing capture odds. A **Miss** uses
one of the three attempts without rolling capture RNG. There is no hidden
perfect-throw multiplier or guaranteed capture. An unsuccessful on-target throw
can still escape through the existing wiggle animation. Three failures end the
encounter without a missed-care punishment. Auto still requires a manual throw;
Skip / Resume fighting keeps its previous behavior.

Each new prompt or retry starts large, shrinks over 2.4 seconds, then resets.
Four target radii vary by form ID. Their visible acceptance band is ±12 pixels,
giving approximately 720 ms per cycle. Inputs cancel on interruption, stale
state, a prolonged hold or overlapping contacts. Network retry keeps the exact
submitted result instead of sampling the ring again.

The current physical board has no configured action-button GPIO. Its PWR and
BOOT controls are not game buttons. Touch is the implemented board input;
browser two-button controls exercise the action-button path, while native
input uses center touch or the on-screen THROW control. No button GPIOs are invented.

## Why the devices met the same monsters

Previously, native startup created a profile with the development seed `12345`.
The later hardware random seed changed encounter distance, while monster
selection still used that shared profile seed. Two injected walking seeds
produced different first distances (136 and 94 steps) and the same 24-monster
sequence. Browser enrollment also used the fixed seed.

New native profiles use startup hardware entropy, diversified with the public
factory identity. Separate domains provide profile, world, pacing and starter
offer seeds. The entropy source is enabled briefly after the board power hold
and I²C setup, sampled, then disabled before ADC, radio or audio initialization.
This follows the supported [ESP-IDF 5.3.6 RNG startup contract](https://docs.espressif.com/projects/esp-idf/en/v5.3.6/esp32s3/api-reference/system/random.html).
Identity is a uniqueness input, not a secret or a replacement for entropy.

Existing profiles receive a one-time persisted `worldSeed` before future
encounter selection or prefetch. Their original profile/capture RNG, current
monster, queued monster, walking target/progress, offers and care remain intact.
Reboot restores the committed world seed and encounter ordinal; it does not
restart or reroll that sequence. A failed checkpoint pauses new play through
the existing recovery path. No fixed-seed fallback is published as a new game.

The loopback service uses Node cryptographic randomness for new enrollment.
Authenticated `POST /api/world/seed` initializes the world once after hatch and
records a durable server-owned event and receipt. Repeated or concurrent calls
return the saved result. GET/save remains read-only, and clients cannot inject
`world-seed` through save-sync. The browser waits for its pending outbox to
reconcile before requesting setup. Native service synchronization remains
unimplemented; firmware play continues locally without a service.

Independent seeds do not guarantee that two devices can never meet the same
monster. Shared pools and rarity weights intentionally overlap. Tests check
independent reproducible sequences, not an impossible no-collision promise.

## Persistence and shared implementation

Care schema **20**, still rules **13**, appends a uint32 `worldSeed` at byte 656.
Snapshots are **664 bytes** (previously 660); measured host `State` is **636
bytes** (previously 632). Historical snapshots initialize the new field to zero,
which retains their old deterministic replay behavior. The additive `world-seed`
event changes future roster selection without advancing capture or pacing RNG
or the foreground sequence. Trade journals accept the previous embedded
snapshot layout and reconcile mixed old/new mirrors by decoded record equality.
The older installed firmware cannot read a schema 20 save. A future installation
must retain each unit's own pre-upgrade backup; rollback needs that matching
backup rather than trying to load the new save with the old firmware.

`data/capture-ring.json` generates the C++ and JavaScript timing models through
`scripts/generate-capture-ring.py`. Both use integer Q8 radius calculations from
elapsed monotonic time; rendering frequency does not advance the clock. Input
adapters are platform-specific and independently tested. The existing `flick`
wire action remains: 41140 for a centered hit, 0 for an aim miss. This preserves
capture rules and old event replay; it does not require a second battle engine.

## Verification and physical acceptance

```sh
npm run test:capture-ring   # host model/UI + actual isolated browser flow
npm run test:entropy       # real entropy adapter with SDK doubles
npm run check:capture-ring # generated sources match the canonical contract
npm run build:esp:waveshare # existing official toolchain, build only
```

The release evidence is in `../deliverables/capture-ring-world-seed-20261009/`.
Host checks cover every millisecond of the four timing bands, down/up ownership,
repeated input, cancellation, historical save migration, world initialization,
queued-foe preservation, durable faults and trade reconciliation. Browser tests
use an isolated temporary service store and original fallback art; private
assets, live saves and credentials are excluded from previews.

Compiler/static allocation sizes are recorded separately in the build manifest.
New hardware frame rate, touch feel, free heap, power-loss behavior and battery
draw have not been measured. The ring adds no framebuffer or sprite atlas;
existing RGB565 display and art allocations are retained. No promise of physical
acceptance follows from a successful host test or ESP compile.

After an approved installation, verify center taps and retries on both devices,
then compare several future encounters while preserving each unit's own save.
Include a reboot between encounters and check that the sequence continues.
Retest sound, lanyard walking and Nearby mode selection alongside this change.
Do not erase profiles to demonstrate different encounters.

# Touch capture input, version 1

The browser's capture action accepts an upward touchscreen flick. The shared C++
core decides whether its quantized trajectory hits, then applies the existing
capture rules. Animation cannot grant a capture or bypass a failed throw. This
contract adds no gyro requirement, currency, capture-ball inventory, new reward,
or extra capture-chance modifier.

## Event and geometry

Save-sync accepts the ordinary exact event shape:

```json
{"type":"flick","value":41140}
```

`value = (dx + 160) * 256 + reach`, with integer `dx` from −160 through 160 and
integer `reach` from 0 through 255. The packed value is therefore an integer from
0 through 82,175. No additional coordinates, duration, raw pointer history or
sensor samples are uploaded. The observations are client-reported, like steps;
the service cannot prove a person performed a real gesture.

All coordinates refer to a fixed 412 × 412 stage, independent of CSS scaling:

| Quantity | Value |
| --- | --- |
| Launch center | (206, 300) |
| Target center | (206, 120) |
| Landing point | (206 + dx, 300 − reach) |
| Target radius | 48 |
| Native hit condition | `dx*dx + (180-reach)*(180-reach) <= 2304` |

The circle boundary is inclusive. A center hit is value 41,140. The minimum and
maximum packed values are legal trajectory observations that miss. Invalid
numeric values are rejected, never clamped by the native core or service.
Only the gesture recognizer may quantize/clamp a valid release before creating
the event. Browser trajectory feedback must use these decoded coordinates and
this hit condition; arcs and timing between endpoints are presentation only.

`digivice::decodeFlick` is a pure, integer-only decoder. For differential browser
checks, `./build/digivice-core --flick-trajectory 41140` returns:

```json
{"inputVersion":1,"landingX":206,"landingY":120,"hit":true}
```

## Rules and durable attempts

A flick is legal only in a Tactical wild encounter, with the opponent at or below
half HP, collection space available, counters within bounds and fewer than three
previous capture attempts. The same native guards apply to button capture.
Egg, Home, Auto, strong opponents, full collections and exhausted attempts reject
transactionally without changing RNG, HP, attempt count or sequence.

- A legal aim miss spends one existing attempt, advances sequence once and causes
  the ordinary wild response. Capture RNG is unchanged. Energy is unchanged,
  matching existing capture. Damage, shield absorption and gentle retreat use
  the existing response path.
- A hit runs the exact existing `capture 0` path: one RNG draw against the native
  capture chance. A hit can therefore still escape. Success stores one companion
  and its ordinary journal/XP/bond rewards. Terminal results reset encounter
  counters as before.
- A hit grants no aiming bonus. Button and Auto captures preserve their previous
  behavior and odds. No collection member is released automatically.

Use the existing durable batch ID and exact request body for uncertain-response
retry. Retrying one accepted release must return that release's original result,
including after later progress or service restart. A mixed batch containing an
illegal event persists nothing. Releasing a pointer a second time, cancellation,
short taps and gesture navigation must not create a second event. Those recognizer
and lifecycle checks belong to the browser tests; the server receives only the
bounded event, not a physical pointer session.

The saved `CaptureMissed` message is preserved for compatibility. Browser feedback
distinguishes a geometric miss from a hit/escape using the decoded event and the
authoritative returned state. It must not infer capture success from collision
animation, a local coin toss or the saved message alone; a level-up can replace
the normal captured message while the collection still grew.

## Compatibility and readiness

This is an additive input, with `Flick` appended to the action enum. Existing
numeric action IDs, all old event meanings, schema 13/rules 10, and the 576-byte
snapshot remain unchanged. Existing `capture 0` histories are replayed unchanged;
`capture` with nonzero values is still rejected. Frozen rules-1 through rules-9
history engines never accept `flick`. A current save carrying an older encounter
continues that encounter's existing capture odds/profile/response behavior.

The input-v1 packing and hit geometry are immutable once history is saved. A
future incompatible gesture mechanic needs a distinct event/version and explicit
replay handling; changing these constants in place would reinterpret history.

Browser `/api/health` advertises `capabilities.captureFlick: 1` only with a native
core that answers the center-trajectory probe correctly. The browser must require
this capability before offering flick capture against a service. An older service
cannot load a history containing the newly introduced action and fails closed;
do not downgrade it over a store that has accepted flick events. There is no
forced migration of an unchanged pre-flick save and no silent fallback from a
missed flick to button capture.

Measured host structures remain 552 bytes for `State` and 576 bytes for its
encoded snapshot. The decoder allocates no heap, uses bounded 32-bit arithmetic,
and adds no saved bytes. The fresh Waveshare target build (historical local evidence omitted) is 1,267,552 bytes
with 115,459 static DIRAM bytes. These are linker measurements, not runtime heap
or physical display/audio performance. The ESP display, touch driver and
gesture wiring still require hardware bring-up. Native/host and browser passing
tests are not evidence of a working touchscreen on the physical board.

## Focused verification

Run `cmake --build build -j 4`, `ctest --test-dir build --output-on-failure`, then
`./scripts/node.sh --test tests/service-flick.test.ts`.

Native tests cover circle edges, packed bounds, rejected contexts, 256 hit/button
snapshot equivalences across seeds and current/carried rules, three geometric
misses, no capture RNG on misses, retreat and snapshot round trips. CLI tests
cover exact geometry examples, missing/invalid numbers and rejection in every
frozen historical engine. Service tests cover authoritative results, payload
bounds, atomic rollback and persistent idempotent retries. Actual browser touch evidence (historical local evidence omitted) covers misses,
capture, cancellation, retry and rendering cleanup. The consolidated verification
record (historical local evidence omitted) pins the completed source and tests.

Historical equivalence evidence (historical local evidence omitted)
compares complete JSON output from the pre-change host binary against the new
binary for 64 synthetic Tactical/Auto histories, including RNG and captures.

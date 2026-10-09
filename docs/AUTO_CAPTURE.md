# Manual capture during wild Auto

Auto fights now pause for the player to throw. The native UI uses `auto-fight`
to resolve attacks until the usual capture conditions are met: the wild creature
is at or below half HP, collection space remains and a legal attempt is available.
The crossing attack and its enemy response finish before the ball appears.
No throw, capture attempt or capture RNG draw is hidden in that attack sequence.

Current source uses a [graded timed ring](GRADED_CAPTURE.md): press anywhere in
the main play area, or choose **SKIP / RESUME FIGHT**. The shaded band marks
green timing. Red/orange/green use 10%/50%/100% of the existing eligible odds,
rounded down with a 1% minimum. The label shows the exact percentage; green
is not guaranteed. Every valid timed throw rolls once and plays its saved
result. An unsuccessful throw leaves Auto paused; the third ends the encounter
calmly. Successful capture retains the existing progression and collection rules.

Skip sends `auto-resume`, which finishes the remaining fight using attacks only.
It cannot throw or prompt again during that encounter. Battle animation locks
input until the full attack sequence finishes, so a queued attack cannot run
behind the capture screen. Nearby/PvP battles and trading have no capture flow.

## Durable contract

- `AutoCapture::Awaiting` is saved with the partial wild HP and turn. Restarting
  restores the opportunity without rerolling or throwing. Walking checkpoints
  preserve it and the active battle.
- `auto-fight` returns a wild, attack-only trace with `outcome: "none"` when
  paused. Its last frame matches the saved battle HP. Each Auto action is bounded
  to 48 exchanges; a fight that reaches the bound ends in a gentle retreat.
- While awaiting capture, `ring-capture` and historical `flick` can throw. Repeated `auto-fight`, historical
  `auto`, plain `capture` and manual attack actions are rejected. `auto-resume`
  ends the pause without capture. Returning Home clears the flag.
- A partial attack trace survives background walking, but is discarded after a
  later foreground action such as Flick. Capture results use their own saved
  record and presentation.

Current care schema 20 adds a persisted independent world seed and uses 664-byte
snapshots (host State 636 bytes); [migration details](CAPTURE_RING_WORLD_SEED.md).
The prior Auto milestone below remains the historical schema 19 layout.

Schema 19 appends `autoCapture` (`0` None, `1` Awaiting) at byte 652, after the
schema 18 `receivedTrades` counter. Snapshots are **660 bytes**; measured host
`State` is **632 bytes**. Older snapshots migrate with no Auto pause, preserving
their prior fields and any received-trade count. Rules remain13. Historical
`Action::Auto`/`applyAuto` still replay their original complete battles, including
automatic capture; the new player-facing flow uses the additive actions above.

## Host verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure \
  -R 'auto-capture-core|battle-presentation|device-ui|trade-core'
```

The [core suite](../tests/auto_capture_core_test.cpp) passed **44,708 checks** under
ASan/UBSan: 512 new fights, actual flick outcomes, three misses, skip, restart,
background walking, full collection, counter limits and snapshot migration.
It also matches aggregate checksums covering the old state payloads and complete
traces for 512 histories generated independently from `f74ee4c`.
Native host UI tests cover the completed crossing animation, prompt, misses,
resume and restoring an awaiting save. This is host verification; physical
touch, display timing and power-loss behavior still require device acceptance.

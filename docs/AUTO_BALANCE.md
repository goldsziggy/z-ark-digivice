# Auto-battle balance and focus taps (rules 18)

Rules 18 (schema 25) retunes wild battles around **Auto**, which is how most fights are played, and adds one optional **focus tap** per fight so an attentive player can help without taking over.

## What was wrong (rules 17)

Measured with `scripts/auto-balance-sim.cpp` over real encounters through `applyAutoFight` (120 fights per level, levels 5–50, every tier):

| | Rules 17 | Rules 18 untapped | Rules 18 green focus |
| --- | --- | --- | --- |
| Auto win rate | 78.6% | **91.0%** | **94.8%** |
| Exchanges p90 (worst level) | up to 43 | **12** | **11** |
| Exchanges median | grows with level | **4–5, flat L5–L50** | 4–5 |
| Heavy reflected by Counter | common (random policy) | **0** | 0 |

Three causes:

1. **Move power ignored level.** Damage barely grew from L5 to L50 while HP did, so late fights dragged for dozens of exchanges.
2. **Auto chose moves at random**, including Heavy into a telegraphed Counter, so it lost fights a child would win.
3. **Wild damage was flat**, so once fights were short the wild side stopped being a threat.

## Rules 18 changes

| Change | Value |
| --- | --- |
| Move power scales with attacker level | power `+ level / 3` (bonus capped at 16; Heavy gets double) |
| Wild chip floor | each wild hit deals at least `ceil(defender max HP / 24)`, clamped 4–32 |
| Wild damage ramp | `+4%` per wild level, capped at `+40%`, so L1 starters are not punished |
| Auto policy | greedy and guard-aware: never sends Heavy into a Counter, prefers the type-advantaged move |
| Rules gating | an encounter started under rules ≤17 finishes under its own rules (`wildRules`) |

## Focus taps

Once per wild Auto fight, on exchange 1 or 2 (seeded, deterministic), Auto pauses on the existing capture ring:

- **STRIKE! — TIME THE RING**: your partner's next hit. Green ×2, orange ×1.5, red or no tap ×1.
- **BLOCK! — TIME THE RING**: the wild's next hit. Green blocks it fully, orange halves it, red or no tap takes it normally.

It is the same 2,400 ms ring and grade words as graded capture, so there is nothing new to learn. **LET AUTO PLAY** (or doing nothing for 3.2 s on the handheld) answers with no tap and the fight continues exactly as unattended Auto would. Run Away still works from the prompt. The answer is one durable `focus` event, so a retry never plays a second exchange, and replay is exact.

Focus is a bonus, not a requirement: untapped Auto is already tuned to win about 9 in 10 fights; a green tap lifts that to about 19 in 20 and leaves more HP for the next encounter.

## Per-level results (rules 18, 120 fights each)

| Level | Tier | Untapped win% | Green win% | Exch. med / p90 | HP left (untapped → green) |
| --- | --- | --- | --- | --- | --- |
| 5 | 2 | 96.7 | 100.0 | 5 / 7 | 54% → 61% |
| 10 | 2 | 90.0 | 93.3 | 5 / 9 | 48% → 57% |
| 17 | 2 | 88.3 | 93.3 | 5 / 8 | 45% → 54% |
| 18 | 3 | 92.5 | 98.3 | 4 / 8 | 51% → 59% |
| 22 | 3 | 87.5 | 93.3 | 5 / 9 | 49% → 54% |
| 27 | 3 | 89.2 | 92.5 | 4 / 8 | 51% → 57% |
| 28 | 4 | 90.8 | 94.2 | 5 / 9 | 51% → 59% |
| 34 | 4 | 90.0 | 95.8 | 5 / 10 | 53% → 59% |
| 39 | 4 | 88.3 | 94.2 | 5 / 8 | 54% → 61% |
| 40 | 5 | 91.7 | 93.3 | 5 / 12 | 52% → 58% |
| 45 | 5 | 95.8 | 96.7 | 5 / 10 | 53% → 60% |
| 50 | 5 | 90.8 | 92.5 | 5 / 10 | 55% → 61% |

`tests/rules18_test.cpp` locks the band for starters: untapped win 80–97%, green ≥ untapped, p90 ≤ 14 exchanges.

## Reproduce

```sh
cmake --build build
clang++ -std=c++17 -O2 -Icore -DDIGIVICE_HAS_FOCUS scripts/auto-balance-sim.cpp \
  build/libdigivice-game.a build/libdigivice-combat.a -o /tmp/sim
/tmp/sim 120 0   # untapped
/tmp/sim 120 1   # green focus every time
/tmp/sim 120 2   # red focus (same as untapped)
```

## Migration

Schema 24 / rules 17 snapshots decode as schema 25 with `wildRules` preserved; the CLI keeps `--migrate-v17` / `--replay-v17-trace` via the frozen `core/legacy_v17`. The service (store format 20) archives the rules-17 history and recovers paused traces. A schema-24 header can never carry a focus prompt.

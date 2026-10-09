# Care and capture — rules 12

Rules 12 uses schema 15 and a 636-byte canonical snapshot. It adds no stored Care currency or timer. Existing saves migrate without resetting their partner, collection, XP, journal, walking schedule or unfinished encounter. Frozen `core/legacy_v11.*` replays rules 11; unfinished fights through rules 11 keep their original damage and capture policies. Historical `walk` creates rules 10 encounters; physical walking uses `explore` and creates rules 12 encounters.

## Care, battle stats and evolution

Feed, Play and Rest still affect the selected owned instance. Identical forms in two collection slots have independent needs and bond.

| Action | Existing bounded effects | Bond |
|---|---|---|
| Feed | Fullness +15, energy +3, mood +2; each capped at 100 | +2 only when fullness was below 100 |
| Play | Mood +12, capped at 100; costs 5 energy only while mood is below 100 | +5 only when mood was below 100 |
| Rest | HP +25 to base maximum, energy +25 to 100 | +1 only when HP or energy needed recovery |

Full-mood Play now leaves energy unchanged. Repeated full-needs Play/Rest/Feed cannot manufacture more bond. There is no offline drain, death, missed-care penalty or bonus to XP. Bond is still capped at 200.

`rank = floor(bond / 50)` gives 0–4. The selected creature receives:

- ATK and MAG: `rank + (mood >= 80 ? 1 : 0)`, capped at +5.
- DEF and RES: `rank + (fullness >= 80 ? 1 : 0)`, capped at +5.
- HP, type and named skills: unchanged.

These are flat additions before the existing damage, type and defense calculation. They can matter substantially at low levels. For Impmon versus an identical uncared-for Impmon, neutral Physical damage is 12 at level 1 and 13 at level 5. Maximum offense raises those hits to 17 and 18; maximum protection reduces the corresponding incoming hits to 7 and 8. Base HP remains 92 and 108. Damage floors, Brace/Ward and type multipliers can change the visible difference for other opponents.

Base profile data remains separate from the effective battle profile. `memberCare(member)` returns bounded offense/protection; `memberBattleProfile(state, member)` returns effective stats and respects the active legacy fight's unchanged profile. JSON keeps `combat` as base stats and adds `care: {bondRank, offenseBonus, protectionBonus, effective: {maxHp, attack, defense, magic, resistance}}`. Egg `care` is null. Actual bonuses are zero for the active member of an unfinished old encounter.

Evolution still requires an authored forward edge and its existing level and bond thresholds. Care supplies bond; victories/captures supply battle XP and also bond. Winning grants +8 bond, capturing grants +12, and either gives `20 + 6 * wildLevel` XP. A capped Care bonus never bypasses a route gate. No route, XP threshold or maximum level was changed.

The old full-mood Play/Rest loop had allowed baby growth through repeated Home actions alone. That loop is closed. All 15 authored level-1 baby routes were checked through supported encounters/recovery: in the fixed test fixtures each needed at most two encounters. For example, Chibomon→Veemon and Kuramon→Keramon reach bond 16 from useful Home Care; two ordinary encounters and six recovery Rest actions reach bond 22 at level 1, satisfying the existing level-1/bond-20 gate. This is a reproducible fixture example, not a guarantee of identical outcomes for every saved state.

Nearby matches freeze each participant's actual form, level and 0–5 offense/protection bonuses in the accepted match profile. Later Care cannot change an active duel. Rules 12 peers reject incompatible older handshakes. Match results remain reward-free.

## Three capture throws

A target must still be at half HP or lower, and the collection must have room. For rules 12, aimed capture chance in percentage points is:

```
base = 50 + floor(40 * (maxHP - 2 * HP) / maxHP)
rarityPenalty = Common:0, Uncommon:10, Rare:20
levelPenalty = 5 * min(5, max(0, targetLevel - activePartnerLevel))
chance = clamp(base - rarityPenalty - levelPenalty, 10, 90)
```

Rarity is the target's authored rarity. There is no invented player rarity. Integer HP rounding can shift an approximate “quarter health” example by a point.

| Target / relative level | Half HP | Quarter HP |
|---|---:|---:|
| Common, same level | 50% | 70% |
| Common, one level higher | 45% | 65% |
| Rare, same level | 30% | 50% |
| Rare, one level higher | 25% | 45% |

The test table uses valid Mote and Puttimon profiles at levels 1 and 2. Current generated walking encounters still match the active partner's level, so the higher-level penalty currently applies only to imported/synthetic states or future encounter sources. This change does not introduce harder walking encounters or alter the rarity pool.

Every valid committed throw consumes one of three attempts. An aim miss consumes no capture RNG and records `miss` with chance 0. An aimed throw makes exactly one authoritative RNG draw and records `escaped` or `captured` with the calculated chance. New-rules throws do not trigger enemy retaliation, cost energy or reduce either HP bar. On a third failure, the target leaves and the state returns Home with `CaptureEnded`; no XP or capture reward is granted. Ordinary battle actions before or between throws still use the existing battle rules. Old encounters retain their original retaliation and capture-limit behavior.

The committed `lastCapture` object stores sequence, target form, target level, chance, attempt and result. It survives a restart and terminal return Home; no seed or random roll is included in the presentation record. The UI presents the actual result after the durable commit and only replays a saved reveal when its sequence is the latest event. Misses display “Miss”, without presenting 0% as a rolled catch chance. A wiggle animation is only presentation of that one saved result.

Auto reuses the same resolver. Each new capture trace step includes `capture: {chance, attempt, result}` and `opponentAction: null`. A third failure uses the historical trace outcome label `retreated`, with unchanged positive player HP and terminal attempt-3 failed-capture metadata. It does not represent defeat. New trace JSON adds `combatRulesVersion: 12`, `playerCare` and `enemyCare`, each containing offense/protection bonuses. Old trace JSON remains unchanged.

## Storage and verification

The schema-14 body through byte 595 is preserved. Schema 15 appends:

| Offset | Bytes | Value |
|---|---:|---|
| 596 | 4 | Last capture event sequence |
| 600 | 4 | Target form ID |
| 604 | 4 | Chance, or zero for miss/none |
| 608 | 4 | Attempt in low byte; target level in next byte; upper bytes zero |
| 612 | 4 | Result: none 0, miss 1, escaped 2, captured 3 |
| 616 | 4 | One-time starter-offer seed |
| 620 | 12 | Three saved offer form IDs |
| 632 | 4 | CRC32 of preceding 632 bytes |

All integers are little endian. No raw C++ structs are copied to disk. Decode and actions validate a private candidate before publishing. The runtime must durably checkpoint the candidate before displaying a new result; the core itself does not perform filesystem writes.

Measured on the Mac host: State 608 bytes, snapshot 636, Auto Step 32, Auto Trace 1608. The high-width JSON sweep across 276 active forms, a full eight-member collection, full journal, large counters, Care, latest capture and saved offers reaches 8212 bytes excluding NUL. The fixed JSON buffer is now 12288 bytes; 8192 is insufficient. This sweep is resource evidence, not an exhaustive proof over every possible state. The core still allocates no heap memory.

Run the focused and regression tests with the existing host toolchain:

```sh
cmake -S . -B build
cmake --build build --target care-capture-core-test core-test walking-core-test combat-test battle-presentation-test -j 4
ctest --test-dir build --output-on-failure -R 'care-capture-core|core-test|combat-test|battle-presentation'
```

Sanitizer logs and source hashes are recorded in `docs/evidence/care-capture-rules12-validation.json`. Tests cover retries/restarts, finite Care and duplicate instances, capture chances and three throws, one-roll Auto metadata, all 32 starter candidates, all 15 baby routes, malformed records and old replay continuity. No hardware or real save was used for these checks.

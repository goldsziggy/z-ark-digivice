# Battle stats and skills

Current source is **care schema 14 / rules 11 / catalog 6**, with **276 native forms** representing all 255 individually named source entries. Variants/aliases remain distinct; this is not 255 unique canonical species. Complete stats and moves CSV (historical local evidence omitted) · [Roster and authored balance](WORLD_DS_ROSTER.md). Rules 11 changes walking encounter pacing and the player's new wild Auto attack policy, while retaining the five-stat formula, capture curve, XP, types and growth. The earlier Lumen/Pelagia correction remains in effect. [Walking/nearby source milestone and hardware gates](WALKING_NEARBY_RELEASE.md).

Wild encounters, practice and nearby duels share the deterministic [C++ combat resolver](../core/combat.cpp). Each explicit form and numeric level has fixed stats and three named skills. These numerical stats, four game types and evolution gates are authored prototype rules, not official franchise statistics. An appearance pack changes artwork, not species, type or stats. The browser and native screens display core results; physical acceptance of the current native controls, presentation and audio remains pending.

[Tactical and Auto modes](BATTLE_MODES.md) change who chooses the moves, while using the same stats, skills, damage calculation and reward rules.

Numeric levels run 1–20 independently of explicit Digivolution. All eight egg starters have two reviewed routes through Mega; the wider graph contains 172 routes and 41 lower-stage leaves with documented reasons. Use [RPG progression](../README.md#levels-and-digivolution), the [native form table](../core/forms.cpp) and [balance measurements](BALANCE.md) for the full roster.

## Reading the stats

**Max HP** is the health ceiling. **Attack** powers physical and heavy skills; **Defense** resists them. **Magic** powers magic skills; **Resistance** resists magic. Higher defensive stats reduce damage. Numeric level follows XP, while bond is a separate requirement for an explicit form change. Level-up alone never changes form.

These are the ten original forms at their entry levels. Lumen/Pelagia include the retained rules-9 correction; earlier saved encounters can still use their frozen historical profiles. The native profile at the actual numeric level is authoritative.

| Creature | Entry level | Type | Max HP | Attack | Defense | Magic | Resistance |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| Mote | 1 | Grove | 100 | 18 | 14 | 16 | 16 |
| Glint | 5 | Grove | 116 | 24 | 19 | 23 | 22 |
| Lumen | 10 | Grove | 164 | 42 | 36 | 46 | 44 |
| Flicker | 1 | Neutral | 88 | 20 | 12 | 14 | 12 |
| Rill | 1 | Tide | 104 | 14 | 16 | 20 | 18 |
| Brine | 5 | Tide | 120 | 20 | 23 | 29 | 26 |
| Pelagia | 10 | Tide | 168 | 38 | 38 | 50 | 46 |
| Cinder | 1 | Ember | 96 | 22 | 12 | 18 | 12 |
| Scoria | 5 | Ember | 112 | 32 | 18 | 25 | 18 |
| Pyrel | 10 | Ember | 132 | 44 | 25 | 34 | 26 |

The old automatic bond-40/100 evolution is historical. Current original-family form choices unlock at level 5/bond 20 and level 10/bond 50 and require confirmation. XP level-up and Digivolution preserve the current health fraction, rounded up: `new HP = ceil(old HP × new max HP / old max HP)`. Rest heals 25 HP up to the current maximum. Care grants no XP.

New identities can hatch **Impmon, Agumon, Gabumon, Patamon, Tentomon, Palmon, Gomamon or Renamon**. Their level-1 prototype profiles are listed in [ONBOARDING.md](ONBOARDING.md#prototype-profiles). Each now has two curated routes through Champion, Ultimate and Mega, using explicit native form IDs and level/bond requirements. Grove/Tide/Ember/Neutral are game categories, separate from official Digimon types and attributes.

## Type effects

Read rows as the attacker and columns as the defender. The multiplier applies to physical, heavy and magic damage alike.

| Attacker ↓ / Defender → | Grove | Tide | Ember | Neutral |
| --- | ---: | ---: | ---: | ---: |
| Grove | 100% | **125%** | 80% | 100% |
| Tide | 80% | 100% | **125%** | 100% |
| Ember | **125%** | 80% | 100% | 100% |
| Neutral | 100% | 100% | 100% | 100% |

Grove → Tide → Ember → Grove is the strong direction. The reverse direction deals 80%. There are no immunity, accuracy or critical-hit rolls in these rules.

## Skills and damage

| Creature | Physical · power 8 | Heavy · power 16 | Magic · power 8 |
| --- | --- | --- | --- |
| Mote | Twig Tap | Root Ram | Seed Spark |
| Glint | Briar Swipe | Timber Rush | Bloom Flash |
| Lumen | Canopy Claw | Ancient Crash | Solar Bloom |
| Flicker | Quick Peck | Comet Dive | Glimmer Pulse |
| Rill | Fin Slap | River Rush | Bubble Burst |
| Brine | Reef Strike | Breaker Bash | Tidal Orb |
| Pelagia | Trident Sweep | Maelstrom Ram | Abyssal Wave |
| Cinder | Coal Claw | Furnace Charge | Ember Shot |
| Scoria | Obsidian Slash | Magma Crash | Lava Lance |
| Pyrel | Inferno Talon | Volcano Break | Phoenix Flare |

The eight starter move sets in [the onboarding roster](ONBOARDING.md#prototype-profiles) use the same columns. All names in a column share that column's rule; they currently have no extra status effects, including when a named franchise move has additional effects in its source material.

1. Choose the stats: physical/heavy use Attack and Defense; magic uses Magic and Resistance.
2. Calculate `raw = max(floor, attacking stat + power − defending stat)`. Wild uses floor **4**. Current practice and nearby use `max(4, ceil(intended defender maxHP / 20))`, clamped to the resolver's 4–32 bound.
3. Apply type: `typed = floor(raw × type percent / 100)`.
4. A matching defense halves that integer, rounding down. **Brace** halves physical; **Ward / Rune Ward** halves magic. **Counter / Reversal** reflects a heavy hit at half damage back to its attacker, leaving the defender unharmed. Other pairings leave damage unchanged. New wild encounters also expose these guards; in practice and nearby the defender chooses them.
5. The resolver returns at least 1 damage. Then apply cards and finally cap HP lost at the recipient's remaining HP.

Where Tactical card input is available, one **Spark** adds 5 after those calculations to the next ordinary outgoing hit. It does not boost a reflected hit and stays prepared if your heavy attack is reflected. One **Shelter** absorbs up to 12 incoming damage in total; unused protection survives for later hits. Shelter can reduce a hit to zero, including a reflected hit you receive. Choose only one card per wild encounter or practice duel. Auto and nearby use no cards; this rule support does not establish an installed NFC reader.

## Worked comparisons

These values use the table’s exact original stat anchors, before cards and remaining-HP caps. Higher numeric levels can produce different values.

| Situation | Calculation | Result |
| --- | --- | --- |
| Rill uses Bubble Burst on Cinder | `(20 + 8 − 12) × 125%` | **20 damage**; Rune Ward reduces it to **10**. |
| Brine uses Tidal Orb on Cinder | `floor((29 + 8 − 12) × 125%)` | **31 damage**; Rune Ward gives `floor(31 / 2)` = **15**. |
| Cinder uses Furnace Charge on Rill | `floor((22 + 16 − 16) × 80%)` | **17 damage**; Reversal instead reflects **8** to Cinder. |
| Mote uses Twig Tap on Flicker | `18 + 8 − 12` | **14 damage**; Brace reduces it to **7**. |

Spark changes Rill's unguarded 20 to **25**. If Shelter protects the recipient of a 20-point hit, it absorbs 12 and the recipient loses **8 HP**. Apply Spark after type and defense; the card's five points are not multiplied or halved.

The actual browser result (historical local evidence omitted) shows Twig Tap against Flicker's Reversal doing 14 damage. Reversal matches heavy, so it does not reduce this physical hit. Stats (historical local evidence omitted) and Moves (historical local evidence omitted) show the same Mote profile in the round interface.

## Walking, wild encounters and Auto dice

Native physical walking advances encounters only while eligible at Home/Explore. At Normal pace the first encounter needs 40–80 eligible steps, then each full gap is 80–140 (means 60/110); Relaxed doubles the effort per encounter and Frequent advances effort 1.5 times as fast. Off pauses it. Targets persist across reload/rate changes; menu/battle steps contribute to lifetime usage without creating catch-up encounters. These are rule ranges, not measured minutes or pedometer accuracy. [Durability and all pace ranges](WALKING_NEARBY_RELEASE.md).

The first encounter is Flicker. Later encounters use the existing stage-eligible roster with 70% common / 25% uncommon / 5% rare bucket weights; rarity does not change stats. An opponent freezes its level at the active partner's level and starts at full profile HP. New wild guards show Brace, Ward or Counter. Wild retaliation alternates Physical/Magic after surviving attacks or failed captures; a reflected Heavy replaces ordinary retaliation. Heavy costs **6 energy** and requires at least 6. Physical/Magic cost **2**, floored at zero, and remain legal at zero energy.

In **new rules-11 wild Auto**, each committed player attack independently rolls Physical or Magic with equal odds. Consecutive identical rolls are allowed; Heavy is manual. A separate deterministic policy stream keeps retries/replay stable without inspecting capture RNG. Capture still takes priority when the target is weak, attempts remain and the collection has space. The whole Auto result commits once before presentation; replay and skipped animation cannot duplicate rewards. Old active encounters and legacy `walk` harness encounters retain their recorded rules-10 or earlier policy. Practice Auto keeps its rules-7 public-stat scorer.

Victory gives **8 bond** and capture **12 bond**; either grants `20 + 6 × wildLevel` XP. Capture is legal at half HP or lower, with three attempts and an eight-member capacity. Its chance is `50 + floor(40 × (maxHP − 2 × HP) / maxHP)`: **50% at half HP**, **70% at quarter HP**, approaching 90% near zero. Older encounters through rules 7 retain their original chance. A forced retreat returns Home with `ceil(max HP / 10)` health and no XP; there is no death or elapsed-time punishment for missed care. Auto is bounded to 48 turns.

The host's 2,048-case rules-11 Auto sample produced 99 victories, 1,742 captures and 207 retreats, averaging 8.437 turns (maximum 46). Its frozen greedy comparison produced 130/1,903/15, averaging 5.719 turns. This demonstrates a policy tradeoff, not a human win-rate estimate or a balance guarantee. Measured core evidence (historical local evidence omitted).

## Practice and nearby duels

**Practice** freezes the selected companion's identity/form/level and a stage-appropriate rival from the full roster. Both start at full practice HP and use no care energy. Tactical alternates attack/defense with the existing incomplete two-option hint; cards/retries do not reroll a committed rival move. Auto scores public profiles and HP over the three possible opposing choices, with seeded ties. A current duel ends at zero HP, a Tactical retreat or a **40-exchange draw**. It does not change pet HP, XP, bond, care, forms, steps, captures or ownership. The browser and firmware serial interface support practice; this milestone does not add a native practice touch menu. [Practice controls](PRACTICE_BATTLE.md) · [ESP serial boundary](ESP_PRACTICE.md).

**Nearby** requires an explicit challenge and recipient acceptance of the frozen mode/matchup. Each side starts at full duel HP and 100 duel energy. Tactical alternates attacker choices Physical/Heavy/Magic and defender choices Brace/Counter/Ward. Auto independently rolls Physical/Magic at equal odds for either attacker, plus one of the three guards. No cards or capture apply; at most 40 exchanges resolve. It awards no care changes, XP, bond, captures, progression or permanent wins. Subsequent exchanges wait at least **2.4 seconds** after the prior state is first acknowledged, with queued Tactical choices. First Tactical resolution may occur immediately after both choices. Reboot/leave aborts the volatile session. [Nearby protocol and physical RF gates](NEARBY_PROTOCOL.md).

## Native collection, progression and presentation

Open **Partners → Stats + Evolve** to inspect each owned member. Four pages show HP/Attack/Defense/Magic/Resistance; fullness/mood/energy/bond/XP-to-next-level; named Physical/Heavy/Magic moves; and the type chart. Only the selected partner can Digivolve. Other members can become the partner or be explicitly released; member IDs and the obtained-form journal remain stable.

**Digivolve → Details → Review → Digivolve** shows the route's level/bond eligibility, both forms' stats at the same preview level, target moves/type and a confirmation before saving. At most two listed routes are available; unavailable routes give a reason rather than changing state. Confirmation binds the current member/form/sequence, preserves ID/XP/bond and scales HP upward proportionally without a full heal. Back cancels before commitment. A saved result screen leads to the updated stats.

Wild mode is confirmed under **Settings → Mode** before the encounter. In native Tactical attack view, a left/right swipe selects Physical/Magic; an upward swipe commits the selected attack. Heavy remains a separate selection, and capture has its own upward-orb gesture after opening Capture. Input is locked while a committed result plays. Native presentation budgets 1.2 seconds per actor, impact at 350 ms and 1.6 seconds for the summary; late frames do not skip straight over an actor. Move names, HP changes and original cues describe committed results only. These are source timing constants, not measured LCD/audio performance. Physical gesture/readability/sound acceptance remains pending.

## Versions and saved health

Current care state uses **schema 14 / rules 11 / 600-byte snapshots** in service container **13**. Stats derive from validated native form IDs and numeric levels, never client-supplied values. The table has 276 forms. Schema-13/rules-10 snapshots were 576 bytes; migration preserves their old gameplay payload, active encounter rules and identities, adding default Normal pacing. The separate physical lifetime usage counter starts at zero and never imports old simulated steps.

Rules-1 through rules-10 histories replay under frozen executors before conversion. The earlier tier/HP conversions still apply only to their historical formats. An existing zero-event creature stays a creature and a pending egg stays an egg. The service archives original histories/receipts, retains exact available old Auto traces and rejects old pending request markers until explicit reconciliation; clients cannot relabel an old committed ID as a new command. This milestone did not open or migrate real service data. [Current API migration contracts](../service/API.md).

New practice duels remain **schema/rules 7 / 120-byte snapshots / store 8**. Existing rules-2 through rules-6 duels finish with their frozen rules, commitments and receipts (rules 2 uses 112 bytes; later versions use 120). Rules 2–4 retain 30 exchanges; rules 5–7 use 40. Nearby state is separate and deliberately not persisted across reboot. [Battle-mode persistence](BATTLE_MODES.md#persistence-api-and-replay) describes retries and bounded trace replay.

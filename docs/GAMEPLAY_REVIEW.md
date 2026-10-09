# Gameplay review — 7 October 2026

**Historical scope:** this page records the first gameplay pass through `16feedd`. The [final bounded Auto and outlier pass](FINAL_TUNING.md) supersedes its current-version, outcome and budget figures.

This pass builds on `aea1a41`. It changes actual battle names, capture decisions, practice pacing and nine evolution routes. A follow-up fixes mathematically forced practice draws. It preserves the earlier asset import, all numeric creature profiles, stable identities and old saved-battle behavior. It is a playable local software prototype; it does not establish physical handheld readiness or final game balance.

## What changed

- **Actual combat names:** 30 reviewed bindings across 26 forms, including 24 renamed slots and six existing matches. WarGreymon now uses Great Tornado and Terra Force; Wizardmon uses Thunder Cloud. Tactical reads the current native profile. Auto reads each saved trace's participant profile, so a historical battle retains its old names. Physical, Heavy and Magic remain visible categories with their existing damage rules. Unmapped slots retain authored names. These are damage-only adaptations, not implementations of every effect described in the franchise reference. [Names, action semantics and holds](research/battle-skill-bindings.json).
- **Capture:** newly started rules-8 encounters use `50 + 40 × (maxHP − 2 × HP) / maxHP`, truncated to an integer, once the enemy reaches half HP. That gives 50% at exactly half HP, 70% at one quarter and at most 89% with positive remaining HP in this roster. The native response exposes the actual percentage. Further weakening costs another exchange and can cause a defeat or retreat; three attempts remain the limit. Old encounters retain their original level-based odds.
- **Practice:** new rules-6 bouts allow 40 exchanges and use a raw damage floor of `max(4, ceil(intended defender maxHP / 20))` before the existing type, guard and reflection modifiers. HP, numeric profiles, card effects, hints and Auto's policy are unchanged. Saved rules-5 bouts keep their original floor and 40-exchange limit; rules-2/3/4 retain 30. Practice grants no care-state rewards. The 48-step trace allocation is unchanged.
- **Evolution:** nine reviewed ordinary routes bring the graph to 172 edges, preserving all 163 prior edges and gates. Examples include Dragomon→Neptunemon and Parrotmon→Crossmon. The selected fan game's explicit alternatives also support routes such as Monzaemon→Babamon. These are prototype routes with our level/bond gates, not exclusive canonical or exact World DS requirements. [Every source UUID, decision and remaining leaf](research/digigame-gameplay-evolutions.json).

The remaining 41 Rookie/Champion/Ultimate leaves comprise 17 intentionally independent or distinct-variant endings for now, 15 unsupported roster destinations, three missing intermediates, three identity holds, two fusion/missing-target cases and one mode/family hold. An empty upstream forward list is not proof that a species is canonically terminal.

## Why these changes

The earlier full-roster sample contained 23,392 seeded matchups and 233,920 fights. Its 97.1% Auto capture rate was much broader evidence than the older eight-starter/three-rival sample, which approached 100% in later levels. Neither sample estimates a human player's success probability or sets a target of 50% wins.

Paired capture policies exposed a concrete defect: with the old flat odds, waiting to quarter HP rescued **zero** captures and lost **1,869** compared with first-legal capture. With the selected 50→90 curve, waiting rescued **1,583** cases but lost **1,627** others. Aggregate capture rates were 87.51% for first-legal Auto and 87.32% for the public quarter-HP policy. The latter also suffered more retreats, 11.57% versus 6.54%. This creates a situational choice; it does not make delaying capture universally best. Auto retains its simple first-legal policy. Paired cases (historical local evidence omitted) · Candidate distributions (historical local evidence omitted).

Heavy was strongest in 77.1% of the original **Brace/Ward probes**, not all possible decisions. Counter reflects it, it costs six energy rather than two, and creature stats change the alternatives. A measured power reduction from 16 to 12 lowered full-collection wild wins from 81.65% to 73.23% and increased public-hint practice draws from 17.00% to 21.97%. Heavy therefore remains at 16. The near-uniform practice Auto policy also explains much of its gap from a public-information Tactical policy; that gap is not all due to hints. [Detailed balance interpretation](BALANCE.md).

For equal-level practice cases, cap 40 reduced public-hint Tactical draws from **17.21% to 7.86%**, and Auto draws from **32.66% to 18.57%**. Every previously decisive case stayed identical. Mean exchanges rose by 1.21 and 2.58 respectively; the 95th percentile rises to 40. Starting at 70% HP shortened bouts more, but changes HP displays, saved-start contracts and relative card strength, and still left many defensive Mega draws. That candidate was deferred. Paired practice pacing (historical local evidence omitted).

## Forced-draw repair and actual success rates

The cap-only change still left an objective defect. A Cannondramon level-15 mirror has 248 HP, but even perfect cooperation permits only 240 damage in 40 exchanges, or 245 with Spark. No policy can finish it by HP defeat. The same bound affected **216 legal mirror configurations across 46 forms**. Native proof (historical local evidence omitted).

Four isolated floor candidates were compared. Dividing HP by 24 improved the aggregate, but **32 sampled encounters still had fixed enemy sequences that forced a draw even with Spark**. The selected divisor 20 matches the 20 attack opportunities: against every guard there is an unhalved, unreflected move, so a neutral mirror can deal at least a full HP bar within those opportunities without relying on the enemy reflecting its own Heavy. At least one side of every type matchup has a modifier of 100% or more. This proves a decisive sequence is possible, not that the player knows the hidden moves or that Auto selects it. Fixed-sequence counterexamples (historical local evidence omitted).

| Whole stratified sample | Before this pass: cap30 | Cap40 only | Final practice6 |
| --- | ---: | ---: | ---: |
| Public-hint Tactical win / draw | 81.37% / 17.00% | 90.75% / 7.42% | **97.58% / 0.34%** |
| Phase-only Auto win / draw | 33.28% / 31.18% | 40.51% / 17.45% | **46.34% / 6.30%** |

Each policy uses the same 23,392 seeded cases; these are scripted strategies against representative opponents, not human win probabilities. At equal levels the final win rates are **98.17% Tactical / 49.42% Auto**. Mean exchanges are **18.09 / 23.25** over the full sample; p95 is **33 / 40**.

For equal-level Mega Bulwarks, Tactical draws fall **54.00%→1.33%** and Auto draws **73.50%→33.17%**. Mega Warden draws fall **27.50%→0.625%** and **63.33%→19.38%** respectively. Thus Auto remains poor at defensive matches even after removing the mathematical blocker; it chooses moves almost uniformly and does not optimize the public hints/profile. Practice offers no rewards to farm by stalling. Those residual strategy/duration choices are documented rather than replaced with an arbitrary winner or stronger AI.

The repair has a cost: unguarded Physical/Magic ties rise **8.87%→26.85%** in the probes, and the floor binds in about **33.46% of Tactical / 39.39% of Auto exchanges**. A stronger divisor16 would nearly erase draws but flatten still more stat distinctions; it was rejected. Higher damage above the floor, the type chart, guard counters and all wild combat remain unchanged. All candidate tradeoffs (historical local evidence omitted).

## Pacing and limits

All 172 routes were reached in 752 native, sanitized progression playthroughs using Tactical and Auto policies with two fixed seeds. There were no failed routes. The longest single edge took 40 battles and 176 care actions; the sample recorded 319 retreats. These are deterministic action counts, not minutes of walking or player time. Roots start as valid captured forms, so this test establishes route reachability after acquisition. Progression evidence (historical local evidence omitted).

Recovery is repetitive, not an infinite or XP-generating exploit. Rest restores 25 HP and 25 energy per action, immediately and without a currency/timer. A win-focused 96-path sample reaches levels 5/10/15/20 after 12/34/60/88 wins, with 12–15 / 34–45 / 60–76 / 88–106 encounters. Median cumulative recovery Rests are 34/113/197/290; level20 p95 is 420. These counts come from full-HP/80-energy recovery after each fight, not a minimum-action player policy. All 384 checkpoints across the 96 current paths match the retained baseline exactly.

Bond already reaches 117–140 at level5 and 200 at level10. One hundred Play/Rest pairs produce bond110 but XP0/level1; one thousand pairs still leave XP0/level1 and cap bond at200. Selecting a partner restores its saved HP, and evolution preserves the HP fraction along an acyclic graph, so neither creates a repeat-heal loop. Free care makes bond gates permissive; adding wait penalties or costs would be a new design choice, so none was added.

Wild matchup extremes remain: Lumen level10/20 and Pelagia level10 win none of their 40 full-collection probes each; Pelagia level10 captures in only 1/40 capture-first cases. These are preserved original-profile outliers, not representative of every named starter. Wild guard-aware victory is 81.65% overall, versus 72.66% ignoring guards and 63.96% excluding Heavy. Heavy uses 44.11% of guard-aware attack choices; it is useful, not a universal command to spam. The roster deliberately shares stat curves across roles.

## Saved state and resources

Current care uses schema 11/rules 8, catalog 4 and service container 10; new practice uses schema/rules 6 and practice storage wrapper 7. Frozen rules 7 keep the previous 163-edge graph, labels and capture behavior. Old pending commands, receipts, snapshots and Auto traces retain their original rule selection. A newly added route or renamed move cannot retroactively change a saved command's meaning.

The measured host care State remains 552 bytes, canonical snapshot 576 bytes and JSON buffer 8,192 bytes (widest sampled output 6,572 bytes). Practice State is 80 bytes and snapshots remain 120 bytes; the trace remains bounded to 48 steps. The actual Waveshare build is 1,224,976 bytes, leaving 1,920,752 bytes in its 3 MiB app slot; static DIRAM is 115,299 bytes, unchanged. This targeted repair adds only 112 app bytes on that profile. Runtime free heap, stack high-water and battery life are unmeasured. [Final build and resource measurements](ESP_BUILD.md) distinguish actual compiled flash/static RAM from unmeasured runtime heap, stack and power.

The earlier **241/255 private artwork packs** remain unchanged. The exact fourteen gaps are:

- Identity held: **Antylamon (168), Yatagaramon (213), BlackImperialdramon (220), Daemon (231), Justimon (248)**. Variant/mode or appearance evidence is insufficient; a similar name is not enough to assign pixels.
- Matching source absent: **Azulongmon (215), Baihumon (217), Barbamon (218), Belphemon (219), Chronomon DM (227), Ebonwumon (235), Granlocomon (241), Leviamon (250), Zhuqiaomon (267)**.

These forms remain playable with placeholders. Imported field poses do not become dedicated combat animations. No new downloader, asset source, paid service or cloud deployment was introduced. Private franchise pixels, local saves and credentials are excluded from source/binary release archives. [Import and rights scope](DIGIGAME_IMPORT.md).

## Run

```sh
npm run dev:direct
# Open http://127.0.0.1:8787
npm run test:direct
python3 scripts/simulate-evolution-progression.py
python3 scripts/run-world-ds-balance.py
npm run build:esp:waveshare
```

The provisional target remains Waveshare ESP32-S3-Touch-LCD-1.46, SKU29565. Target compilation does not implement or verify the physical LCD, two buttons, NFC, GPS, battery life, SD hot removal or brownout durability. Those require the selected board and bench work. No device was flashed.

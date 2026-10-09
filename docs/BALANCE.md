# Balance audit and native simulation

This report concerns deterministic game rules, not measured player enjoyment, physical walking accuracy, battery life, or hardware timing. Simulation uses the actual C++ game, combat, and practice implementations; it does not maintain a second combat formula in a reporting script.

Park follow-up: care rules 10 adds authored encounter rarity and one-confirm full recovery while retaining the combat, XP and capture rules below. The timed native session comparison (historical local evidence omitted) separates walking from assumed interaction time, native Rest events from confirmations, and roster choices from automatic release. It does not claim physical step accuracy or measured human timing.

## Current bounded pass: care rules 9 / Practice rules 7

The final pass corrects two objectively weak original Ultimate profiles and replaces phase-only Practice Auto with a shallow, seeded public-information policy. It changes no Heavy, capture, XP, evolution, recovery or art rules. The paired report (historical local evidence omitted) and exact evidence (historical local evidence omitted) supersede the Practice 6 aggregate figures below.

All candidates use immutable commit `16feeddf58f7637b51564c83dd24e1e6e9a2ba23`, the same 23,392 seeded cases per policy, and the real native rules. The final production implementation matches the accepted combined candidate **byte-for-byte across all 239,768 output rows, 3,641 profile rows, and 712 additional mirror/reciprocal cases**. These are stratified host simulations, not predictions of player win rates.

| Whole Practice sample | Before | Current |
| --- | ---: | ---: |
| Auto wins / draws | 46.34% / 6.30% | 86.35% / 3.40% |
| Public-hint Tactical wins / draws | 97.58% / 0.34% | 97.78% / 0.34% |
| Auto exchanges, mean / p95 | 23.25 / 40 | 20.33 / 39 |

Auto scores immediate clipped HP swing over all three unknown opponent moves using public profiles and HP. Only tied scores consume its separate seeded RNG. It cannot read the committed enemy move, opponent RNG, optional two-choice hint or future events, and does not play cards. Tactical retains an **11.44-point** win advantage. Auto attack choices remain varied (43.30% Physical, 11.57% Heavy, 45.13% Magic), but its expected-value defense strongly favors Counter (94.35%; Ward 5.65%, Brace 0%). That is a documented policy limitation, not another tuning target in this pass.

Defensive matchups still matter: equal-level Mega Bulwark Auto improves from 18.83% to 56.67% wins but draws 19.33%; Warden improves from 24.58% to 77.92% wins and draws 9.17%. A separate seed-222 mirror check across all 276 forms at entry/20 improves from 47 wins / 419 losses / 86 draws to 549 / 0 / 3. Cannondramon level 15 wins in 39 exchanges with 79 HP, where old Auto drew at 40. The three remaining mirror draws and adverse seed changes are listed in the report; no complete-balance claim is made.

Lumen and Pelagia had low Ultimate entry anchors, despite normal growth. Their new base HP/Attack/Defense/Magic/Resistance is **164/42/36/46/44** and **168/38/38/50/46**, respectively; both retain growth **4/1/1/2/2**. At level 20 this gives **204/52/46/66/64** and **208/48/48/70/66**. The family-derived anchors sit inside appropriate peer ranges and neither dominate nor are dominated by another Ultimate at levels 10/15/20. Pelagia's +4 weighted-budget premium over newer role profiles is explicit. Peer proof (historical local evidence omitted).

Only these two forms' 22 legal level rows change; 274 other forms remain identical. Across their 160 sampled cases, full-collection wild wins improve 12→131 and captures 42→140. One previous capture becomes a defeat win because of stronger damage. They also become tougher opponents in reversed-role tests. Saved old encounters/Practice use frozen profiles and outcomes; the separate compatibility suites verify that boundary.

Auto-only leaves every wild result unchanged. With the two profiles corrected, whole-sample capture is 87.93% and full-collection wins 82.16%. Mean energy/recovery burden changes little: capture-first energy 23.609→23.583 and Rests 3.958→3.944; full-collection energy 39.278→39.337 and Rests 5.076→5.068. This pass does not change or claim to repair repeated Rest input, free bond cycling, waiting, or the remaining art gaps.

Reproduce the frozen candidates and exact current comparison using the commands in the new report (historical local evidence omitted). The older reports below remain historical, with their original source hashes and counts intact.

## Historical full-roster measurements: care rules 8 / Practice rules 6

The selected changes are **HP-sensitive capture odds, a 40-exchange Practice cap, and a Practice-only minimum raw damage of `max(4, ceil(target max HP / 20))`, validated within 4–32**. The floor precedes type modifiers, matching guards and Counter; unsupported larger floors fail closed. All current legal profiles fit that bound. Heavy remains power 16 / energy 6. All 276 stat profiles, including the 66 preserved profiles, retain their numbers. Old encounter and Practice epochs retain their original outcomes.

The Practice 6 native report (historical local evidence omitted) and source-hashed JSON (historical local evidence omitted) cover 276 actors, 152 representative opponents, 3,641 legal stat rows, 187,136 wild fights and 46,784 Practice fights. Another 5,760 representative fights passed AddressSanitizer/UndefinedBehaviorSanitizer. Each of 5,848 matchups uses four shared deterministic seeds. Opponents represent available roles/types at the same combat tier with legal −2/0/+2 level offsets; illegal offsets are omitted. These are coverage samples, not live encounter probabilities or evidence that every matchup should win 50% of the time.

| Whole stratified sample | Before, `aea1a41` | Selected implementation |
| --- | ---: | ---: |
| Wild, capture-first Auto/public greedy: capture | 97.12% | 87.51% |
| Same context: defeat instead / retreat | 0.25% / 2.62% | 5.94% / 6.54% |
| Full collection, guard-aware defeat wins | 81.65% | 81.65% |
| Practice, public-hint policy: win / draw | 81.37% / 17.00% | 97.58% / 0.34% |
| Practice, phase-only Auto: win / draw | 33.28% / 31.18% | 46.34% / 6.30% |

The selection check (historical local evidence omitted) compares every final wild, Practice and damage row to the selected isolated candidates (capture50 for wild, divisor20 for Practice): all 239,768 rows match exactly. The baseline check (historical local evidence omitted) first verified the extended measurement harness reproduced every original row before adding the quarter-HP policy. Capture/Heavy/cap candidates were compiled from immutable commit `aea1a4188430feba807ad444a4d85ed55e0cd93a`; the subsequent Practice-floor candidates use immutable `c830072e5244462c0021678b02ff96a1c644e75f`. Production copies and measurement copies remain separate.

## Capture is now a choice with a cost

After the existing half-HP eligibility gate, new encounters use integer arithmetic:

```text
chancePercent = 50 + 40 * (wildMaxHp - 2 * wildHp) / wildMaxHp
```

This is 50% at exact half HP, 70% at exact quarter HP, approaching 90% as HP approaches zero. A living target normally tops out at 89% because of integer truncation. The three-attempt cap, one existing RNG draw per attempt, failed-attempt retaliation and reward rules are unchanged. The UI reads the native chance; it does not duplicate the formula. Saved encounters from rules 7 or earlier retain flat legacy odds.

The baseline gave further weakening no capture benefit. In 23,392 paired cases, a visible-HP policy that waited until quarter HP **never captured when first-legal capture failed**, while first-legal succeeded alone in 1,869 cases. That is a concrete sampled degeneracy, beyond a high aggregate capture rate.

Under the selected curve, quarter-HP capture succeeds alone in **1,583 cases** and first-legal succeeds alone in **1,627**. Aggregate capture rates are 87.32% versus 87.51%, respectively. Quarter-HP play sees a higher observed success per attempt (74.19% versus 54.16%) but pays extra retaliation: retreat rises to 11.57% from 6.54%, with 1.66 extra battle actions and 0.81 extra recovery Rests per case. Seventeen stage/role groups benefit in capture rate; others should try early. The policy uses visible guard/HP, avoids lethal weakening hits, and never sees RNG. Auto remains first-legal and simple.

The milder 65→90 curve also removed strict sampled dominance, but produced only 525 quarter-only successes against 1,754 first-only successes. The 50→90 curve was selected for its clearer tradeoff, not to force a target win rate. Paired evidence (historical local evidence omitted), all candidates and strata (historical local evidence omitted).

## Heavy and Practice pacing

Heavy is strongest in 77.09% of **Brace/Ward** probes; the corresponding unguarded figure is 70.79%. Neither figure means it is best in every turn. Counter reflects Heavy, and both native Auto and guard-aware Tactical use Heavy into Counter zero times in the full-collection sample. Heavy represents 44.11% of guard-aware attacking choices. Physical/Magic preference reverses across opponents in 672 of 1,176 actor/level/offset groups, rising to 879 when guards are included.

Reducing Heavy power from 16 to 12 lowered full-collection wins from 81.65% to 73.23%, increased mean battle actions from 10.24 to 11.04, and increased recovery Rests from 5.07 to 5.38. It also increased old-cap Practice draws from 17.00% to 21.97% for the public-hint policy and from 31.18% to 36.54% for Auto. This candidate was rejected: reducing the strongest immediate move made defensive stalls and recovery burden worse. Heavy still costs more energy, but shorter fights often save enough HP to outweigh that cost. Free recovery weakens long-term energy scarcity; the game does not claim otherwise.

The old near-100% public-hint / 83.5% Auto claim came from a smaller earlier sample of eight starters against three root rivals. It does not describe the full roster. At equal levels in the older cap-30 baseline, a profile/HP-aware policy **without** hints won 76.25%, the hint policy won 81.77%, and phase-only Auto won 35.55%. Most of that difference is decision quality and the shared counter matrix; hints add a measured 5.52 percentage points to this shallow scorer. None reads the committed enemy choice. No-hint comparison (historical local evidence omitted).

| Equal-level Practice, before the damage-floor repair | Old cap 30 | Intermediate cap 40 | Deferred starting HP 70% |
| --- | ---: | ---: | ---: |
| Public-hint / Auto draws | 17.21% / 32.66% | 7.86% / 18.57% | 6.45% / 15.65% |
| Mean exchanges, hint / Auto | 17.98 / 22.46 | 19.19 / 25.04 | 13.60 / 17.73 |
| p95 exchanges | 30 | 40 | 30 |

Cap 40 preserves **every formerly decisive result exactly**, converting only old draws. It was chosen over HP normalization to avoid changing HP/profile/card/display semantics. HP 70% shortened fights more, but still left 44.83% draws for equal-level Mega Bulwarks under the hint policy; it was not a complete balance fix. Pacing candidates (historical local evidence omitted).

## The further Practice repair addresses forced draws

The cap increase alone left a concrete defect, not merely slow defensive play. For Cannondramon level 15 mirrored against itself, HP is 248, maximum direct damage 10, and reflected Heavy damage 2. Even perfect cooperation over 20 attacks plus 20 defensive opportunities can remove at most `20*(10+2)=240` HP; one Spark raises that to only 245. No policy can finish that match through HP defeat in 40 exchanges. Native bounds found 228 impossible mirror form-level rows without a card, 216 even with Spark, plus 268 seeded cases in the actual matchup sample. Both measured policies drew every one of those 268. Native bounds (historical local evidence omitted).

We compared Practice-only divisors 28, 24, 20 and 16, retaining full HP, stats, the 40-exchange cap and the counter matrix. The public-hint scorer uses the **same native candidate resolver** as actual transitions. A weaker `/24` floor passed the loose mirror bound but still failed a tighter proof using actual enemy commitments: 38 of the 268 formerly impossible cases remained impossible without cards, and 32 even with Spark. For example, GrapLeomon level 20 versus Andromon level 20, seed 4245008520, has HP 226/280 but optimistic damage limits of only 224/260 (265 with Spark). Thus `/24` was rejected after initially appearing sufficient. The tested `/20` floor clears those fixed-seed impossibility bounds. Fixed-seed proof (historical local evidence omitted).

The divisor 20 has a mechanical rationale: a 40-exchange duel offers 20 player attacks. Against every guard there is an unhalved, unreflected attack. In neutral mirrors, 20 attacks at `ceil(target HP/20)` can reach the target's HP without requiring the opponent to choose Heavy and enable reflected damage. Type modifiers remain relevant; for a nonmirror, at least one direction has a modifier of 100% or greater. This removes the structural inability of either actor to reach zero HP; it does not guarantee a win for a particular policy. Explicit retreat remains available.

| Same 23,392 cases per policy | No floor, cap 40 | `/28` | Rejected `/24` | Selected `/20` | Rejected `/16` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Public-hint draws | 7.42% | 4.59% | 2.19% | 0.34% | 0% |
| Auto draws | 17.45% | 13.51% | 11.53% | 6.30% | 0.15% |
| Unguarded Physical/Magic ties | 8.87% | 13.13% | 18.07% | 26.85% | 34.22% |

The floor changes 33.46% of the hint policy's actual hit calculations and 39.39% of Auto's; stronger hits retain their original stat-based damage. It deliberately compresses differences between weak attacks. Divisor 16 flattens more of those differences and was unnecessary for the structural fix. Matching guards still halve, Counter still reflects half of Heavy, and Spark/Shield retain their existing placement and values. Wild combat and saved Practice rules 2–5 keep their old formulas. Paired native candidate measurements (historical local evidence omitted).

At the Practice 6 checkpoint, whole-sample counts were 22,826 wins / 486 losses / 80 draws for the public-hint policy and 10,839 / 11,080 / 1,473 for Auto, out of 23,392 each. At equal levels (11,456 each), wins/draws are 98.17%/0.33% and 49.42%/6.70%. Mean exchanges are 18.09 for the hint policy and 23.25 for Auto; p95 is 33/40. These high Tactical results reflect strong public hints and a profile-aware scorer, not hidden-intent access or a forced 50% balance target.

Defensive extremes remain: equal-level Mega Bulwarks win/draw 96.33%/1.33% with hints but 18.83%/33.17% with Auto (600 cases); Wardens are 96.25%/0.625% versus 24.58%/19.38% (480 cases). Of the 264 previously Spark-impossible sampled cases, the hint policy now wins 249 and draws 15; Auto wins 28, loses 36 and still draws 200. Cannondramon's level-15 mirror with seed 222 now has a 33-exchange Tactical win, while Auto still draws at 40. Auto is intentionally simpler and remains poor in these defensive matchups; this is not a claim of complete balance. Known-case outcomes (historical local evidence omitted).

Across 576 equally weighted form-level points, Practice 6 Auto win-rate p10/median/p90 is 20%/42.5%/75%, with four zero-win points; the hint policy is 92.5%/100%/100%. Its worst point is preserved Lumen level 10: 19 wins and 21 losses in 40 cases. These distributions are not encounter-frequency forecasts.

Some preserved original forms also remain weak in wild combat: Lumen level 10/20 and Pelagia level 10 win none of their full-collection probes. The Practice repair does not rewrite those profiles or wild difficulty.

## Historical care-8 progression and recovery action burden

The care-8 native progression run (historical local evidence omitted) exactly matches all 384 checkpoints and three care probes from the pinned baseline (historical local evidence omitted). It starts actual Eggs, hatches all eight starters and follows stay-Rookie / branch-A / branch-B paths over four seeds each. It uses real generated encounters and native care/evolution transitions, not injected enemies. All 96 paths reach level 20:

| Level | Required wins in these runs | Encounters, min–max | Recovery Rests, median / p95 |
| --- | ---: | ---: | ---: |
| 5 | 12 | 12–15 | 34 / 43 |
| 10 | 34 | 34–45 | 113 / 153 |
| 15 | 60 | 60–76 | 197 / 294 |
| 20 | 88 | 88–106 | 290 / 420 |

These same 96 starter paths were measured against both the old 163-edge and current 172-edge graphs, with identical results. The strategy seeks wins and recovers full HP / 80 energy after each fight; it is separate from the broader 172-edge progression verification (historical local evidence omitted). Rest counts are immediate button actions, not mandatory waiting or physical time. They expose repeated recovery friction; this pass does not increase it through a Heavy nerf.

Bond is already 117–140 at level 5 and 200 at level 10, so bond gates are permissive checks rather than independent pacing controls. One hundred Play/Rest pairs produce bond 110 but retain XP 0 / level 1. Repeating Feed or Rest at saturation stops additional useful-care bond. Care cannot replace battle XP, though free bond cycling remains possible.

## Reproduce the earlier focused measurements

```sh
# General current native harness (use a new report name):
python3 scripts/run-world-ds-balance.py --report-prefix world-ds-balance-latest
# Immutable pre-change copies and small numeric candidates:
python3 scripts/investigate-balance.py
python3 scripts/analyze-capture-pairs.py
python3 scripts/investigate-progression.py
python3 scripts/investigate-progression.py --current
python3 scripts/investigate-practice-nohint.py
python3 scripts/investigate-practice-pacing.py
python3 scripts/investigate-practice-floor.py
python3 scripts/verify-balance-selection.py
```

Candidate scripts pin the Git baseline and never edit production rules. Practice comparisons use the hash-verified retained baseline raw/report under ignored `build/balance-investigation/reference-*`; retain these when rerunning the current report. The `--reuse` options summarize already-built raw data without repeating fights. The current audit owns `build/world-ds-balance/`. No dependency installation, networking, ESP timing or physical hardware is involved.

The sections below preserve still earlier measurements and rejected proposals. They are not the current tuning recommendation.

## Previous baseline: `d4ada21`

The baseline has five stats: HP, Attack, Defense, Magic, and Resistance. It has no Speed, critical-hit, or accuracy stat. Physical/Magic power is 8 and Heavy power is 16. Damage is `max(4, offense + power - protection)`, then the creature-type modifier (80%, 100%, or 125%), with integer truncation. Matched Brace/Ward halves damage; Counter reflects half a Heavy hit. These are prototype numbers, not canonical Digimon stats.

The initial independent audit found:

- Bond alone set levels at 40 and 100. A fresh starter reached level 2 after eight Plays, or level 3 after sixteen Plays, one Rest, and four more Plays. Fifty Feeds also reached level 3. These native CLI observations involved no steps or encounters. Feed and Rest granted bond even when their care stats were already full.
- Wild rivals were always level 1 and always attacked physically. Resistance therefore had no defensive effect in wild encounters. Each starter consistently favored either Physical or Magic against all three rivals. For example, level-1 Impmon dealt 12 Physical, 20 Magic, or 20 Heavy damage to Flicker; Agumon dealt 20, 10, or 28.
- Capture odds were 75%, 80%, or 85% by level once wild HP reached half. Further weakening did not improve capture odds. The eight-member capacity had no release/replacement operation.
- Basic attacks remained legal at zero energy, and defeat returned the partner alive. These prevented an energy/death soft lock. Free care and uncapped repeated recovery still made real-time progression pacing undefined.
- Practice had no care-state rewards. It alternated attack and defense, exposed a two-choice hint after committing the rival's move, and ended after at most 30 exchanges. Its Auto policy used a separate deterministic stream and did not inspect hidden choices or hints. Wild Auto required explicit confirmation and had a 48-action bound.
- The service had a 10,000-event lifetime limit. Longer-term RPG play needs save-history compaction rather than treating that limit as progression design.

Sources: saved pre-RPG native roster (historical local evidence omitted), [care/wild rules](../core/game.cpp), [combat resolver](../core/combat.cpp), [practice](../core/practice_battle.cpp), and [service persistence](../service/server.ts). The baseline commit is the authority for historical behavior; these source links move with the implementation.

## Earlier RPG baseline measurements (916c0d8)

Run `bash scripts/run-balance-sim.sh` from the repository root. It compiles [the native harness](../scripts/balance-sim.cpp) with the actual rules, writes raw observations under ignored `build/balance/`, then runs a Python stdlib aggregator. No dependency installation, networking, or device connection is involved.

The measured sample contains **22,104 Wild fights, 14,592 Practice fights, 432 damage probes, 48 actor profiles, 40 progression checkpoints, and three care-cycle probes**. Eight Rookies are sampled at levels 1/5/10/15/20. The Agumon→Greymon→MetalGreymon→WarGreymon and Agumon→Tyrannomon→MetalTyrannomon→RustTyrannomon branches add level-appropriate examples. These are samples, not coverage of every form's balance. Each matchup uses sixteen fixed seeds and three rival lineages at equal level and ±2 levels, clamped to 1–20.

Practice's Tactical policy maximizes immediate expected HP swing over the two public possibilities using `resolveForms`; its input projection excludes RNG and hidden enemy choice. This is a reproducible, shallow baseline, not optimal play. Wild “greedy” selects the highest immediate damage and prefers a basic move on ties; “economy” excludes Heavy. Both attempt capture at the first legal opportunity. Native Auto retains its own policy. Separate full-collection cases force battles to end by defeat/retreat. Recovery is measured by actual Rest actions to restore full HP and the starting 80 energy; no physical seconds are inferred.

The source-hashed report (historical local evidence omitted) includes matchup breakdowns, all five sampled stats, action counts, and limits. Run output (historical local evidence omitted) provides a compact overview. Sixteen deterministic seeds are a regression sample, not population confidence intervals. Injected matchup fixtures must pass native state validation; progression runs instead start with a real Egg and execute native hatch, walk, battle, rest, and optional evolution actions. Rival forms remain Flicker/Rill/Cinder roots at every level; the service and ESP now match a new rival's level to the partner, but do not automatically choose evolved rival forms.

## Measured results and limits

Equal-level Rookie results, each cell based on 384 fights (eight starters × three rivals × sixteen seeds):

| Partner level | Practice Auto wins | Public-hint Tactical wins | Wild Auto captures | Wild greedy captures | Wild Auto median / p95 actions |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 259 / 384 | 384 / 384 | 378 / 384 | 383 / 384 | 5 / 8 |
| 5 | 304 / 384 | 384 / 384 | 384 / 384 | 384 / 384 | 5 / 8 |
| 10 | 346 / 384 | 384 / 384 | 384 / 384 | 384 / 384 | 5 / 7 |
| 15 | 352 / 384 | 384 / 384 | 384 / 384 | 384 / 384 | 5 / 7 |
| 20 | 342 / 384 | 384 / 384 | 384 / 384 | 384 / 384 | 5 / 7 |

This is deliberately gentle PvE, especially capture-focused play. At level 1 against level-3 rivals, Auto captured 311/384 and retreated 73/384, versus 378/384 captures at equal level. At level 20, the +2 probe clamps to level 20; it is **not** an above-cap difficulty measurement. Across the full ±2 sample, the public-hint Practice policy lost only five fights, all against higher-level enemies; its large advantage over Auto comes from using the published hints. Neither policy reads hidden commitments.

Alternating physical/magic wild retaliation now makes both Defense and Resistance relevant. **Outgoing move specialization remains fixed:** Impmon, Patamon, and Palmon prefer Magic, while the other five Rookies prefer Physical across all three equal-level rivals and all five sampled levels. Current rival Defense/Resistance gaps do not reverse that preference. This slice does not claim a rich weakness-selection game or introduce wild guard mechanics.

Heavy offers stronger immediate damage for more energy, but free recovery weakens the long-term cost. At level 5 with a full collection:

| Policy | Wins / 384 | Mean actions | Mean energy spent | Mean recovery Rest actions |
| --- | ---: | ---: | ---: | ---: |
| Native Auto | 349 | 6.83 | 25.68 | 2.95 |
| Greedy, Heavy allowed | 384 | 5.50 | 28.17 | 2.54 |
| Economy, basic moves only | 368 | 7.21 | 14.42 | 3.00 |

Shorter Heavy fights save enough HP to need fewer total recovery actions despite spending more energy. Isolated measurement-only copies tested Heavy costs 12 and 16 instead of 6; **neither candidate was applied**. Cost 12 still needed fewer Rest actions than economy (2.92 versus 3.00). Cost 16 finally increased that burden (3.50), but added many recovery clicks along level-20 progression without adding encounter variety. Keep cost 6 in this slice. Candidate evidence (historical local evidence omitted) records the experiment; its only gameplay change was the energy cost/legality threshold, with unchanged damage and Practice rules.

At level 5, the public-hint Practice policy already won all 384 no-card matches. Spark changed mean exchanges from 10.75 to 10.28 and mean remaining HP from 80.1% to 81.0%; Shelter retained 10.75 exchanges and raised remaining HP to 89.4%. Shelter is the stronger sampled survival aid, while Spark sometimes shortens fights. The policy does not optimize around a prepared card bonus, and these figures do not prove strict dominance. Flat +5/+12 card effects also become relatively smaller as stats grow.

## Progression pacing

Native win-focused play, recovering to full HP and at least 80 energy after each fight:

| Reached level | Cumulative XP threshold | Rookie encounters across eight starters | Agumon, either evolved branch |
| --- | ---: | ---: | ---: |
| 5 | 400 | 12–18 | 12 |
| 10 | 1,800 | 34–40 | 34 |
| 15 | 4,200 | 60–66 | 60 |
| 20 | 7,600 | 88–94 | 88 |

The Rookies with extra attempts were Impmon (six retreats before level 5) and Palmon (two). No sampled path stalled. Evolving either Agumon branch reduced total Rest actions by level 20 to 117–119, compared with 212 when retained as Rookie. That is a meaningful survival/progression benefit, not a hardware time estimate. The two branch roles remain different stat profiles; the evidence includes their exact five stats.

Bond reached 118–147 by level 5 and 200 by level 10 in these runs. Consequently the 20/50/80 bond gates were already satisfied before the level gates: they are permissive relationship checks, not independently meaningful pacing constraints. Two hundred Feeds at full care eventually stopped at bond 4; two hundred Rests stopped at bond 1. Repeated Play with necessary Rest reached bond 47, but all three care probes retained **XP 0, level 1**. Care no longer substitutes for battle XP. Free recovery/bond cycling remains possible by design.

No numeric tuning is recommended from this bounded pass. A next difficulty pass should first choose actual rival forms and desired encounter difficulty, then measure reciprocal Defense/Resistance roles. Simply inflating root growth can make an evolved legacy form weaker than its parent at the unlock, conflicting with preserved stat anchors. Changing the Practice counter matrix, introducing wild guards, or adding waiting/care punishment would exceed this slice.

## Persistence invariants for progression

Old events must run under frozen old rules before migration. Preserve member IDs, care values, old receipt IDs/bodies, and stable starter lineage separately from current form. Explicitly map old levels 1/2/3 to new levels 1/5/10 while retaining the old combat stats at migration. A new form or XP threshold must not silently reset or duplicate a member.

Award wild XP inside the same durable terminal encounter transition. Auto's internal trace is presentation data, not multiple reward opportunities. Replays, lost acknowledgements, reloads, invalid commands, and cancelled confirmations must award zero additional XP. Practice remains reward-free in this slice; a future reward crossing its separate save and the care save needs a durable unique-match claim or one atomic save boundary.

Saved practice profiles and Auto results must retain their original rules. Replaying a saved Auto result with newly tuned formulas can otherwise invalidate a legitimate save. Bound XP arithmetic, levels, form IDs, snapshot sizes, and JSON output. Specify proportional HP changes on evolution and level-up; reloading or switching forms must not become a free full heal.

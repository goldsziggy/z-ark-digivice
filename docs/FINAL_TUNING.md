# Final bounded balance pass — 7 October 2026

This historical care-rules9 / practice-rules7 pass is frozen after one selected stat correction and one selected Auto policy, compared against commit `16feedd`. It changes only Original Lumen/Pelagia base stats and new practice Auto decisions. Growth, types, skills, the 172 evolution routes, capture odds, wild Auto policy, recovery behavior and all artwork identities remain unchanged. Its contracts are care schema12/rules9/catalog5/container11 and practice schema/rules7/store8. The later [park-session milestone](PARK_PLAYTEST.md) adds rarity and recovery/collection flow without further combat tuning. Full measured comparison (historical local evidence omitted) · [Verification](VERIFICATION.md).

## Decisions and actual behavior

Lumen and Pelagia had normal growth but substantially low Ultimate entry stats. Using the diagnostic budget `HP/4 + Attack + Defense + Magic + Resistance`, their level 10 values were 154 and 170.5 versus 210 for the 48 new Ultimate profiles. Their corrected anchors use their own original Rookie base plus the existing Ultimate/Mystic bonuses, yielding 209 and 214. This is a diagnostic comparison, not a replacement damage formula. The other 274 forms remain byte-equivalent in numeric profiles, and the historical source data remains immutable.

| Original Ultimate | HP / Attack / Defense / Magic / Resistance before | After |
| --- | --- | --- |
| Lumen (3) | 136 / 32 / 26 / 32 / 30 | **164 / 42 / 36 / 46 / 44** |
| Pelagia (7) | 142 / 28 / 31 / 40 / 36 | **168 / 38 / 38 / 50 / 46** |

Practice Auto now scores immediate enemy HP lost minus own HP lost across all three possible opposing actions, clipping losses to current HP. It receives public profiles/HP and uses seeded ties; it cannot read commitments, future choices, opponent RNG or the two-option Tactical hint. It makes at most nine hypothetical resolver calls per exchange, 360 per 40-exchange battle, without heap allocation. The opponent commitment policy is unchanged. Tactical keeps narrower hints, individual decisions, card timing and retreat. Both Auto modes remain explicitly confirmed, wholly native and closed to mid-battle input. Wild Auto already used visible guards, affordable moves and cheaper basics on damage ties, so that policy was retained.

## Paired results

Every policy uses the same 23,392 seeded cases. These scripted samples do not estimate human win rates or natural encounter frequencies. All 239,768 production measurement rows exactly match the accepted combined candidate; 3,641 legal profile rows and 712 additional mirror/reciprocal cases also match.

| Measure | Before | Final |
| --- | ---: | ---: |
| Practice Auto win / draw | 46.34% / 6.30% | **86.35% / 3.40%** |
| Public-hint Tactical win / draw | 97.58% / 0.34% | **97.78% / 0.34%** |
| Auto mean / p95 exchanges | 23.25 / 40 | **20.33 / 39** |
| Wild Auto capture | 87.51% | **87.93%** |
| Wild Auto retreat, capture-first | 6.54% | **6.07%** |
| Wild Auto victory, full collection | 81.65% | **82.16%** |

Tactical retains an 11.44-point win advantage. The isolated Auto-only candidate changed no wild results; the modest wild changes above come entirely from the two stat corrections. The capture curve remains 50% at half HP, 70% at quarter HP and up to 89% at positive HP.

The original zero-win cases are repaired without making every matchup a win: Lumen level 10 goes 0→31 wins/40 and level 20 goes 0→32; Pelagia level 10 goes 0→28. Pelagia level 20 goes 12→40. Their capture counts are respectively 3→32,11→36,1→39 and27→33. Across these 160 actor cases, wild wins rise 12→131 and captures 42→140. The corrected forms also become stronger opponents: in 160 reciprocal cases, the reference Tactical wins 160→157 and wild Auto wins 160→139; the change is not a player-only bonus.

Type advantage still matters: practice Auto wins 97.12% with advantage, 87.58% neutral and 67.86% with disadvantage, compared with 80.11%/44.66%/18.44%. Attacks remain diverse: 43.30% Physical, 11.57% Heavy and 45.13% Magic. The 5,848 unguarded wild damage probes favour Physical/Magic in 2,769/2,569 cases with 510 ties, versus 2,767/2,562 with 519 ties. No move, type or energy-cost redesign was introduced.

A separate fixed-seed mirror set uses 276 forms at entry and level 20 (552 cases). Auto improves from 47 wins/419 losses/86 draws to 549 wins/three draws; Tactical wins all 552. The remaining Auto draws are Kabuterimon20, MegaKabuterimon Red10 and Ikkakumon20. Cannondramon15 seed222 now wins in39 exchanges with79HP, versus the earlier40-exchange draw with10HP. These are stress cases with one seed, not population probabilities.

## Remaining limits and migration

The simple expected-HP scorer strongly favours Counter: 94.35% of sampled Auto defences, with Ward 5.65% and Brace 0%. It does not use hints or adapt to past actions. Defensive Mega Auto still draws 19.33% for equal-level Bulwarks and 9.17% for Wardens. Also, 222 previous Auto wins become losses and 99 become draws; improvement is aggregate, not a guarantee for each seed. This pass stops here instead of tuning toward perfect results.

Repeated immediate Rest remains a future user ergonomics decision. It still restores 25HP/25energy without a timer or currency; care cannot generate XP, and partner switching/evolution does not create a repeat-heal loop. No waiting penalties, death, economy, new progression system or artwork guesses were added. The graph still has 41 intermediate-tier leaves, and the same 14 artwork gaps remain explicit in the [prior gameplay review](GAMEPLAY_REVIEW.md#saved-state-and-resources).

Old care histories replay through frozen rules 1–8. Home and inactive members scale affected HP by `ceil(oldHP × newMaxHP / oldMaxHP)` without changing IDs, XP or bond. An active old encounter keeps its original profiles/HP through damage, capture, rewards and retreat, then converts once when returning Home. Saved practice 2–6 retains its exact profiles, decisions, snapshots and retries. Completed old wild Auto playback is retained as bounded validated presentation data in the new service baseline; it does not alter authoritative game state or award rewards.

## Device budgets and running

All three ESP32-S3 profiles compile with the existing official ESP-IDF 5.3.6 and zero warnings. The Waveshare app is **1,255,904 bytes**, an increase of 30,928; static DIRAM remains **115,299 bytes**, and **1,889,824 bytes** remain in its 3 MiB app slot. Care State552/snapshot576, practice State80/snapshot120 and the 32-byte public policy view are measured host sizes. Runtime free heap, stack high-water, LCD/input, battery and physical peripherals remain unmeasured. Source-hashed builds (historical local evidence omitted).

```sh
npm run dev:direct
# Open http://127.0.0.1:8787
npm run test:direct
npm run test:browser:auto-tuning
npm run build:esp:waveshare
```

The browser test needs the existing Playwright/Chromium paths described in the playtest instructions. No native phone app, cloud service or live network is needed during a resolved battle. No device flashing, uploads, hardware purchases or CAD edits occurred. Private sprites, personal saves and credentials remain excluded from source/firmware archives.

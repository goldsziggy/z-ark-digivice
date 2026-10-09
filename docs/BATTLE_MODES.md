# Tactical and Auto battles

Battle mode determines who chooses combat actions. **Tactical** gives the player the supported choices; **Auto** commits to the core's replayable policy. Current care is schema 14/rules 11, while practice remains schema/rules 7. New walking encounters, frozen older encounters, practice and nearby have distinct policies described below. Wild/practice animations present already committed results; nearby advances its agreed volatile session exchange by exchange. [Current release and physical acceptance gates](WALKING_NEARBY_RELEASE.md).

## Choosing and committing a mode

| Battle | Tactical | Strict Auto |
| --- | --- | --- |
| Wild encounter | Choose attacks, a permitted field card and capture attempts under the existing wild rules. The wild opponent retaliates; this is not the practice counter/hint loop. | Confirm once, then the native policy resolves the encounter without manual attacks, cards, capture timing or mode changes. |
| Practice duel | Alternate attack and defence, use the committed rival's partial hint, and optionally prepare one field card. | Confirm once, then the native policy resolves the duel without player move/card choices. Pet-save values remain separate. |
| Nearby friendly duel | After both players agree, alternate attacker/defender choices against the other player, without cards. | Both agree to Auto; each attacker rolls Physical/Magic and its opponent rolls a guard. No care changes or rewards. |

For **wild encounters**, choose the saved mode at Home, before walking starts an encounter. The encounter locks that choice. Auto encounters still wait for an explicit start confirmation; selecting Auto is not permission for the browser to send an invisible command as soon as an opponent appears.

For **practice**, open Battle, choose Tactical or Auto, and confirm the new duel. The mode becomes part of that duel's start request and saved identity. Existing practice requests without a mode keep Tactical behavior. Back before submission can change the draft choice; once submitted, the request must resolve or be retried with the same identity. There is no mid-fight mode switch.

Auto resolves through one bounded native operation and is committed before acknowledgement. The later readable sequence of results does not introduce timed input windows, extra damage bonuses or new commands. Reloading or skipping presentation must not change the saved outcome. A pending request is retained; a network failure does not mean the battle was canceled.

## Wild policy, captures and care

Wild Auto reuses the existing Tactical transitions on a private candidate state. At each internal turn it:

1. Attempts capture if the collection has room, wild HP is at most half its maximum, and fewer than three attempts have been used.
2. For a new rules-11 walking encounter, independently rolls Physical or Magic with equal odds for this committed attack. Consecutive identical choices are legal; Heavy remains manual. An active older encounter retains its frozen policy: rules 5–10 score public damage, exclude reflected Heavy and prefer basics on energy ties.
3. Applies the normal attack/capture, energy cost, wild retaliation and terminal result. It never chooses a field card.

The policy stream derives from the saved seed, encounter count and starting sequence, separately from capture RNG and walking-target RNG. It does not inspect future capture rolls or choose the most damaging move in rules 11. It can defeat a weakened opponent before reaching a capture turn. Full collections or exhausted attempts lead back to attacks. Retries use the same decisions, not fresh random rolls. The historical `walk` harness still creates rules-10 encounters; production physical walking uses `explore`. [Native wild policy](../core/game.cpp)

New wild encounters expose Brace, Ward or Counter. Reflected Heavy replaces normal retaliation, and the guard advances only on resolved attacks or failed captures. Migrated active rules-4 encounters retain the old policy until completion.

Wild encounters freeze the opponent’s numeric level at the active partner’s level when the encounter starts; `wildLevel` is saved. Surviving attacks and failed captures receive alternating physical/magic retaliation. Changing the browser presentation cannot change that sequence.

Both modes use these current consequences:

| Outcome | Companion-save effect |
| --- | --- |
| Victory | Normal energy/HP changes, +8 bond and `20 + 6 × wildLevel` XP for the active partner; no captured member. |
| Capture | One identified member added, capture count increased once, and +12 bond plus `20 + 6 × wildLevel` XP for the active partner. |
| Forced retreat after lethal retaliation or the Auto turn limit | Return Home with `ceil(maxHP / 10)` health; no victory/capture reward or XP. |

Heavy spends six energy; physical/magic spend two, flooring remaining energy at zero. Capture permits three attempts. New encounters retain the rules8 HP-based chance: 50% at half HP, 70% at quarter HP, up to89% with positive HP in this roster. Saved encounters through rules7 keep their original level-based threshold. A failed attempt receives ordinary wild retaliation. Auto does not change these rates, double rewards or add a timing bonus. Bond remains capped at 200. Numeric level follows XP, independently of bond; level-up does not automatically change form. Care and practice award no XP. See [RPG progression](../README.md#levels-and-digivolution).

Resolution is bounded to **48 internal turns**. If combat remains unresolved, the core commits a gentle retreat with no XP or reward. Other invalid operations leave live state unchanged. A resolved encounter advances the outer game sequence **once**, including a single capture timestamp if applicable. Persisted mode stays Auto until changed at Home. Manual attacks/cards/capture commands are rejected during Auto. Physical lifetime steps may accumulate while waiting, but active battles receive no exploration credit. Rules-10/11 encounters permit an explicit release of an inactive companion to make room without resolving combat; the browser exposes that flow. Older encounters retain their restrictions.

## Practice policy and stakes

Tactical practice retains the attack/defence counter loop: Brace reduces physical damage, Reversal reflects heavy damage, and Rune Ward reduces magic damage. The rival commits before the player's input. Its two-option hint and revealed history help a player decide, but do not guarantee the correct counter. [Counter rules](PRACTICE_BATTLE.md) and [damage calculations](BATTLE_STATS.md) describe that existing resolver.

Auto practice can start only from an untouched full-health duel. Practice7 evaluates each legal attack or defence against all three possible opposing actions, using public profiles and current HP. It scores immediate enemy HP lost minus own HP lost, clips losses at current HP, and breaks equal scores with a separate seeded stream. The bounded policy makes at most nine resolver calls per exchange. It receives no committed move, excluded move, hidden RNG or future choice; it does not consume Tactical’s two-option hint or field cards. The rival’s seeded commitment policy stays unchanged. Tactical retains deliberate move choice, the narrower public hint, card timing and retreat. Saved practice2–6 retain their original phase-only Auto policy and profiles. [Native implementation](../core/practice_battle.cpp)

Both practice modes end on a defeat or at **40 exchanges**, when living opponents produce a draw. Tactical additionally permits explicit retreat. Auto uses no field cards or mid-duel retreat input. Practice has no energy cost and awards no XP, pet bond, captures, items or progression; its health and result never replace the companion's care state. A fresh duel starts at the frozen profiles’ maximum practice HP, independent of care HP. The rival is selected by the native stage-appropriate full-roster pool at the partner’s starting numeric level. Saved older duels keep their original rival and epoch.

## Persistence, API and replay

| Interface | Contract |
| --- | --- |
| Wild mode | Ordinary authenticated `/api/save-sync` event `{type:"mode",value:0}` for Tactical or `value:1` for Auto; only accepted at Home after hatching. |
| Wild Auto | Ordinary saved event `{type:"auto",value:0}`; requires Encounter phase with Auto mode and returns the terminal save plus `autoTrace`. Uses current request rules11; the encounter's stored `wildRules` selects its frozen/current policy. |
| Practice start | `/api/battle/start` uses `rulesVersion:7`, `expectedRevision` and `requestId` for new duels, with optional `mode:"tactical"` or `mode:"auto"`. Omitted mode remains Tactical. |
| Practice response | `{revision,mode,autoTrace,battle}`. Confirmed Auto starts return a finished duel, never an active duel waiting for browser-selected actions. |
| Practice actions | Existing `/api/battle/act` remains Tactical. A new manual action against a completed Auto duel is rejected with `409 battle_auto_complete`. |

Wild continues to use the existing durable batch IDs and revision checks. The same ID/body replays its original receipt and result, even after later progress; a different body under that ID is a conflict. There is no additional reward when a receipt or saved animation is replayed. A new stale request is rejected rather than applied to a different encounter.

Practice retains its existing request hashes when mode was omitted. An explicitly supplied mode participates in the hash, so a pending request must preserve whether the field was present and its exact value. A retry must not add `mode:"tactical"` to an older mode-less body or relabel a sent Auto request. Practice keeps 32 recent success receipts; an older request beyond that window fails its stale revision instead of replaying as a new duel. Mode, companion identity and the initial Auto snapshot are retained with the terminal snapshot and receipt.

Care state uses **schema14/rules11, 600-byte snapshots and service container13**. Rules1–10 histories first replay through frozen executors, then migrate. Schema13/576-byte migration retains all old gameplay fields and adds default Normal walking pacing. IDs, XP, bond, initializers, receipts and exact available Auto traces survive. Earlier profile conversions remain limited to their historical formats; rules10→11 needs no HP conversion. Old pending bodies must be reconciled, never relabeled. Existing zero-event pets do not become eggs. No real service data was opened or migrated for this source milestone.

New practice uses **schema/rules7, 120-byte snapshots and store8**. Existing practice2–6 finishes with frozen profiles, policies and exact receipts; rules2–4 retain30 exchanges and rules5–6 retain40. Version1 retains its established migration and reserved request IDs. Service Auto replay regenerates the bounded trace from the saved initial snapshot and checks exact terminal bytes, rather than storing an unbounded animation log. Wild traces regenerate through native accepted-event replay.

An `autoTrace` is bounded public presentation data: kind, actor profiles frozen at the start, sequence range, terminal outcome, and at most 48 wild turns or 40 current practice exchanges (30 for rules2–4). Each step reports resolved actions, HP before/after, reflection and capture flags. It exposes no raw snapshot, seed, RNG state or unrevealed intent. Clients validate the format and bounds before playback; a mismatch is an error, not permission to calculate a replacement outcome in JavaScript. See [service contracts](../service/API.md) and [trace validation](../service/battle-trace.ts).

Trace HP describes combat frames before Home recovery or XP-driven level growth. A retreat's last frame may show zero HP even though Home gently restores health. A victory or capture can award XP and bond, with an XP level-up scaling HP afterward. Replay uses the frozen combat profiles; returning Home uses the current care save.

## Browser controls and recovery

**Explore → Choose battle mode → Tactical/Auto** saves the wild preference before walking. An Auto encounter opens **Ready for Auto? → Start Auto battle**; Back here leaves the encounter waiting in the same locked mode. **Battle → Choose battle mode → Auto → Start Auto battle** starts a separate practice duel. A Tactical practice choice starts the existing manual flow.

Once a start has been sent, the saving screen permits exact retry, not cancellation or a new mode choice. After acknowledgement, progress screens show the already-saved turns with no combat buttons. Reduced motion or restoring a finished save can go directly to the result. **Replay saved battle** replays presentation only; **Return home** or **Back to practice** leaves the result. No replay control creates game events, new seeds or rewards.

## Firmware serial flow and verification

At Home, serial `mode auto` proposes a mode; `mode confirm` checkpoints it. `mode tactical`, `mode cancel` and `mode status` use the same confirmation state. An intervening sequence change invalidates the proposal. Native touch uses **Settings → Mode → Confirm**. Real eligible steps start the encounter; legacy serial `walk 100` remains a rules-10 test input. An explicit `auto`/native **Run Auto** resolves and checkpoints once. Practice remains available through `practice start <ID> <REV> tactical|auto`, confirmation and isolated NVS retry records; no native practice touch menu is added here. [Practice commands](ESP_PRACTICE.md).

The native touch path presents a bounded recorded wild trace after committing its result. The sequencer uses 1.2 seconds per actor and a 1.6-second summary, gates input and avoids skipping an actor on a late frame. It does not rerun gameplay or serialize the host-only 16 KiB trace JSON buffer. Measured current host sizes are **576 bytes for care State** and **600 bytes for its canonical snapshot**. Practice retains its separate 120-byte snapshot. These are not ESP free-heap/stack measurements. [Native collection, gesture and presentation scope](BATTLE_STATS.md#native-collection-progression-and-presentation).

The earlier battle-mode milestone’s service verification (historical local evidence omitted) records 16 passing tests and strict TypeScript checks, including exact retries, migration, mirror recovery and no duplicate capture. Native/firmware verification (historical local evidence omitted) covers the policy and durable serial flow. Browser verification (historical local evidence omitted) covers both modes, lost replies/restarts, blocked input during playback, and 12 geometry checks across six screens at two CSS sizes. Tactical regression comparison (historical local evidence omitted) preserves all 19 baseline cases. These are historical mode-baseline checks; current RPG validation and compiled sizes are tracked in [verification](VERIFICATION.md), [balance measurements](BALANCE.md) and [firmware build evidence](FIRMWARE.md). None establish physical display/input behavior or nearby-player transport.

## Nearby duels: agreement before play

The current native source implements **ESP-NOW friendly duels** behind explicit Home → Nearby entry. The challenger freezes its actual owned partner/form/level and requested mode; the recipient explicitly accepts the displayed matchup. Discovery alone is not consent. Rules 11/catalog 6 must match. Both derive the same profiles locally. This is a prototype with host protocol verification, pending two-board RF acceptance. It needs no phone, GPS or service.

A fresh open nonce, session ID, participant pair, mode and seed bind the volatile match. The challenger is host; the guest verifies each exact next deterministic result and its submitted move. The host cannot advance again before acknowledgment and the readable hold. Both Auto and subsequent Tactical exchanges wait at least 2.4 seconds from the first acknowledgment of the previous state; queued choices do not skip that hold.

Tactical alternates attackers, with Physical/Heavy/Magic against Brace/Counter/Ward. Auto independently rolls Physical/Magic with equal odds for either attacker and one of three guards for the defender. Duel HP/energy is isolated; no cards, capture, care changes, XP or rewards occur. A match ends within 40 exchanges. Duplicate commands/results remain stable. Reconnection resumes only the same live session; leaving, reboot, cancel or timeout aborts it, and terminal results cannot become active again.

Source MAC, nonce/session, sequence and CRC enforce framing and stale-session checks; they are **not cryptographic authentication or anti-cheat**. The host chooses the seed, so this is not a fair competitive matchmaking protocol. Keep it reward-free friendly play. [Protocol, resource bounds and test evidence](NEARBY_PROTOCOL.md).

## Platform boundary

The shared C++ rules are portable and independent of network timing. The current browser requires its local service to acknowledge game commands; browser playback is not fully offline gameplay. Practice persistence is separate from companion-save persistence. Physical ESP display, GPIO buttons, NFC, motion configuration, SD power-loss behavior and radio coexistence retain their existing bring-up gates. See [hardware facts](PARK_HARDWARE.md), [ESP build evidence](ESP_BUILD.md) and [service contracts](../service/API.md).

Changing wild mode does not open radio discovery. Nearby requires its own explicit screen entry. No cloud deployment or native phone app is part of this milestone.

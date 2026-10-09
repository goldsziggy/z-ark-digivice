# Tactical and Auto battles

Battle mode determines who chooses attacks. **Tactical** gives the player the supported choices; **Auto** uses a replayable attack policy. Current care is **schema 19/rules 13**. Wild Auto now pauses for an actual capture flick; practice remains schema/rules 7 and Nearby remains a separate friendly duel. Wild/practice animations present committed results, while Nearby advances its agreed volatile session exchange by exchange. See [manual Auto capture](AUTO_CAPTURE.md) for the new flow and host verification.

## Choosing and committing a mode

| Battle | Tactical | Auto |
| --- | --- | --- |
| Wild encounter | Choose attacks, a permitted field card and capture attempts under the encounter's rules. | Confirm attacks; at a capture opportunity, flick the ball or choose **Skip / Resume Fight**. No automatic throw, manual attack selection or mid-fight mode change. |
| Practice duel | Alternate attack and defence, use the committed rival's partial hint, and optionally prepare one field card. | Confirm once, then the native policy resolves the duel without player move/card choices. Pet-save values remain separate. |
| Nearby friendly duel | After both players agree, alternate attacker/defender choices against the other player, without cards. | Both agree to Auto; each attacker rolls Physical/Magic and its opponent rolls a guard. No care changes or rewards. |

For **wild encounters**, choose the saved mode at Home, before walking starts an encounter. The encounter locks that choice. Auto encounters still wait for an explicit start confirmation; selecting Auto is not permission for the browser to send an invisible command as soon as an opponent appears.

For **practice**, open Battle, choose Tactical or Auto, and confirm the new duel. The mode becomes part of that duel's start request and saved identity. Existing practice requests without a mode keep Tactical behavior. Back before submission can change the draft choice; once submitted, the request must resolve or be retried with the same identity. There is no mid-fight mode switch.

Each Auto attack chunk is bounded and committed before acknowledgement. Wild playback finishes the crossing attack and response before enabling the capture choice. The subsequent Flick is a separate saved action. Practice still resolves through one operation. Reloading or skipping presentation cannot reroll a committed result; a network failure does not mean the pending request was cancelled.

## Wild policy, captures and care

Current `auto-fight` reuses Tactical attack transitions on a private candidate. It:

1. Independently rolls Physical or Magic with equal odds for current encounters. Consecutive identical choices are legal; Heavy remains manual.
2. Applies the attack, energy cost and complete enemy response. It never chooses a field card or capture action.
3. Saves `AutoCapture::Awaiting` if a capture is legal: collection space, wild HP at most half maximum, and an attempt available. Otherwise it continues until the fight ends or the bound is reached.

The policy stream derives from the saved seed, encounter count and foreground sequence, separately from capture and walking-target RNG. It cannot inspect future capture rolls. A full collection or a lethal crossing turn can end the fight without a capture opportunity. Retries reproduce the same decisions. Physical walking uses `accrue-steps` and later `present-encounter`; the single queued foe retains its selected form/level. Current `walk` and `explore` also select rules 13 production foes. [Native implementation](../core/game.cpp)

Current encounters expose Brace, Ward or Counter. Reflected Heavy replaces normal retaliation. Attacks advance the enemy turn/guard; current calm capture attempts do not. Opponent level is frozen when the foe is selected, including while it waits in the walking queue.

While Awaiting, Flick uses the existing aim and chance calculations. A miss consumes an attempt without drawing capture RNG. An escaped throw leaves the choice open; the third failed attempt ends calmly. `auto-resume` skips capture and finishes with attacks only, without another prompt. Ordinary attacks, cards, plain `capture` and historical `auto` are rejected while Awaiting.

Both modes use these current consequences:

| Outcome | Companion-save effect |
| --- | --- |
| Victory | Normal energy/HP changes, +8 bond and `20 + 6 × wildLevel` XP for the active partner; no captured member. |
| Capture | One identified member added, capture count increased once, and +12 bond plus `20 + 6 × wildLevel` XP for the active partner. |
| Forced retreat after lethal retaliation or the Auto turn limit | Return Home with `ceil(maxHP / 10)` health; no victory/capture reward or XP. |
| Third failed calm capture attempt | Return Home without capture, reward, XP or extra retaliation. |

Heavy spends six energy; Physical/Magic spend two, flooring energy at zero. Current capture chance starts from the HP-based rate (50% at half HP, 70% at quarter), subtracts 10 percentage points for Uncommon or 20 for Rare, and subtracts five per opponent level above the partner, capped at five levels. The minimum chance is 10%. Auto adds no capture bonus. Bond remains capped at 200; numeric level follows XP and does not automatically change form. Care and practice award no XP. [Progression](../README.md#levels-and-digivolution).

Each `auto-fight` or `auto-resume` action is bounded to **48 exchanges** and advances the game sequence once. An unresolved fight reaches a gentle retreat. Flicks are separate events; invalid operations leave state unchanged. Saved mode stays Auto until changed at Home. Walking continues in menus and battles, with at most one durable pending encounter shown later at quiet Home. Rules 10+ permit explicit release of an inactive companion; older encounters retain their restrictions.

### Historical wild Auto replay

`Action::Auto`/`applyAuto` and the `auto` replay event retain the original complete-battle policy: capture when collection space, half-HP and attempt conditions allow, otherwise attack. Rules 11+ choose Physical/Magic equally; rules 5–10 score public damage, exclude reflected Heavy and prefer basics on energy ties; rules 4 retain their seeded move policy. The historical whole fight advances the outer sequence once and remains bounded to 48 internal turns. New player-facing flows use the additive actions above.

Historical encounters through rules 7 keep their level-based capture chance; rules 8–11 use the HP-based rate without the later rarity/level penalties. Their failed captures can retaliate. Rules 12+ retain calm throws and the three-failure exit. An older named encounter explicitly entering the new manual Auto flow keeps its frozen attack profiles and capture odds, with calm manual throws. Old saved events are not rewritten. Synthetic legacy foes are handled by the separate production-roster repair.

## Practice policy and stakes

Tactical practice retains the attack/defence counter loop: Brace reduces physical damage, Reversal reflects heavy damage, and Rune Ward reduces magic damage. The rival commits before the player's input. Its two-option hint and revealed history help a player decide, but do not guarantee the correct counter. [Counter rules](PRACTICE_BATTLE.md) and [damage calculations](BATTLE_STATS.md) describe that existing resolver.

Auto practice can start only from an untouched full-health duel. Practice7 evaluates each legal attack or defence against all three possible opposing actions, using public profiles and current HP. It scores immediate enemy HP lost minus own HP lost, clips losses at current HP, and breaks equal scores with a separate seeded stream. The bounded policy makes at most nine resolver calls per exchange. It receives no committed move, excluded move, hidden RNG or future choice; it does not consume Tactical’s two-option hint or field cards. The rival’s seeded commitment policy stays unchanged. Tactical retains deliberate move choice, the narrower public hint, card timing and retreat. Saved practice2–6 retain their original phase-only Auto policy and profiles. [Native implementation](../core/practice_battle.cpp)

Both practice modes end on a defeat or at **40 exchanges**, when living opponents produce a draw. Tactical additionally permits explicit retreat. Auto uses no field cards or mid-duel retreat input. Practice has no energy cost and awards no XP, pet bond, captures, items or progression; its health and result never replace the companion's care state. A fresh duel starts at the frozen profiles’ maximum practice HP, independent of care HP. The rival is selected by the native stage-appropriate full-roster pool at the partner’s starting numeric level. Saved older duels keep their original rival and epoch.

## Persistence, API and replay

| Interface | Contract |
| --- | --- |
| Wild mode | Ordinary authenticated `/api/save-sync` event `{type:"mode",value:0}` for Tactical or `value:1` for Auto; only accepted at Home after hatching. |
| Wild Auto attacks | Saved `{type:"auto-fight",value:0}` in Auto Encounter; returns a terminal result or durable Awaiting state with an attack-only `autoTrace` whose outcome is `none`. Current requests use rules 13. |
| Wild Auto capture choice | `{type:"flick",value:<quantized gesture>}` attempts capture; `{type:"auto-resume",value:0}` finishes with attacks only. Both require Awaiting. |
| Historical Wild Auto | `{type:"auto",value:0}` remains available for exact replay/backward compatibility, including automatic capture. The new UI does not select it. |
| Practice start | `/api/battle/start` uses `rulesVersion:7`, `expectedRevision` and `requestId` for new duels, with optional `mode:"tactical"` or `mode:"auto"`. Omitted mode remains Tactical. |
| Practice response | `{revision,mode,autoTrace,battle}`. Confirmed Auto starts return a finished duel, never an active duel waiting for browser-selected actions. |
| Practice actions | Existing `/api/battle/act` remains Tactical. A new manual action against a completed Auto duel is rejected with `409 battle_auto_complete`. |

Wild continues to use the existing durable batch IDs and revision checks. The same ID/body replays its original receipt and result, even after later progress; a different body under that ID is a conflict. There is no additional reward when a receipt or saved animation is replayed. A new stale request is rejected rather than applied to a different encounter.

Practice retains its existing request hashes when mode was omitted. An explicitly supplied mode participates in the hash, so a pending request must preserve whether the field was present and its exact value. A retry must not add `mode:"tactical"` to an older mode-less body or relabel a sent Auto request. Practice keeps 32 recent success receipts; an older request beyond that window fails its stale revision instead of replaying as a new duel. Mode, companion identity and the initial Auto snapshot are retained with the terminal snapshot and receipt.

Care uses **schema 19/rules 13, 660-byte snapshots and service container 15**. Earlier rule epochs replay through their frozen executors before migration. Schema 17/18 upgrades retain existing events, receipts and ordinary rules 13 behavior: schema 18 adds `receivedTrades`; schema 19 adds the Auto pause, defaulting to None for older saves. Existing IDs, XP, bond, collection and available historical traces survive. Pending request bodies must be reconciled, never relabeled; existing zero-event pets do not become eggs. [Trading persistence](NEARBY_TRADING.md).

New practice uses **schema/rules 7, 120-byte snapshots and store 8**. Existing practice2–6 finishes with frozen profiles, policies and exact receipts; rules 2–4 retain30 exchanges and rules 5–6 retain40. Version1 retains its established migration and reserved request IDs. Service Auto replay regenerates the bounded trace from the saved initial snapshot and checks exact terminal bytes, rather than storing an unbounded animation log. Wild traces regenerate through native accepted-event replay.

An `autoTrace` contains frozen actor profiles, sequence range, outcome and at most 48 wild turns or 40 current practice exchanges (30 for rules 2–4). A wild `none` outcome means a completed attack chunk awaiting manual capture; its last HP frame must match the saved battle and its end sequence must match `foregroundSequence`. It survives background walking but is cleared after a later foreground action. Steps expose resolved actions, HP, reflection and capture flags, without seeds, RNG or unrevealed intent. Clients validate before playback; they never calculate replacement outcomes. [Service contracts](../service/API.md) and [trace validation](../service/battle-trace.ts).

Trace HP describes combat frames before Home recovery or XP-driven level growth. A retreat's last frame may show zero HP even though Home gently restores health. A victory or capture can award XP and bond, with an XP level-up scaling HP afterward. Replay uses the frozen combat profiles; returning Home uses the current care save.

## Browser controls and recovery

**Explore → Choose battle mode → Tactical/Auto** saves the wild preference before walking. An Auto encounter opens **Ready for Auto? → Start Auto battle**; Back here leaves the encounter waiting in the same locked mode. **Battle → Choose battle mode → Auto → Start Auto battle** starts a separate practice duel. A Tactical practice choice starts the existing manual flow.

Once a start is sent, the saving screen permits exact retry, not a different mode or new request. Playback locks combat input. A completed wild chunk then opens the ball and **Skip / Resume Fight** choice; restoring an Awaiting save restores that opportunity. A finished battle can go directly to its result. Replay controls change presentation only and never create seeds, events or rewards.

## Firmware serial flow and verification

At Home, serial `mode auto` proposes a mode and `mode confirm` checkpoints it; Tactical/cancel/status retain the same confirmation flow. Native touch saves the mode before an encounter, starts `auto-fight` explicitly, and presents the ball only after its crossing animation ends. On the native serial console, `auto` is an alias for the new `auto-fight`; `auto-resume` continues without capture. The host core replay CLI accepts `auto-fight`, `flick <value>` and `auto-resume`, while its historical `auto` event retains the complete-battle resolver. Practice retains its existing commands, confirmation and isolated retry records. [Practice commands](ESP_PRACTICE.md).

Native presentation does not rerun gameplay or allocate the host-only 16 KiB trace JSON buffer. Input stays gated through every actor in the chunk; capture uses its separate saved throw/wiggle/reveal. Current measured host sizes are **632-byte State** and **660-byte snapshot**; practice retains its 120-byte snapshot. These are not ESP free-heap/stack measurements. [Current Auto checks and acceptance limits](AUTO_CAPTURE.md).

The earlier battle-mode milestone’s service verification (historical local evidence omitted) records 16 passing tests and strict TypeScript checks, including exact retries, migration, mirror recovery and no duplicate capture. Native/firmware verification (historical local evidence omitted) covers the policy and durable serial flow. Browser verification (historical local evidence omitted) covers both modes, lost replies/restarts, blocked input during playback, and 12 geometry checks across six screens at two CSS sizes. Tactical regression comparison (historical local evidence omitted) preserves all 19 baseline cases. These are historical mode-baseline checks; current RPG validation and compiled sizes are tracked in [verification](VERIFICATION.md), [balance measurements](BALANCE.md) and [firmware build evidence](FIRMWARE.md). None establish physical display/input behavior or nearby-player transport.

## Nearby duels: agreement before play

Native **ESP-NOW friendly duels** require explicit Nearby entry. The challenger freezes its owned partner/form/level and requested mode; the recipient accepts the displayed matchup. Discovery is not consent. Nearby rules 12/catalog6 must match; both derive profiles locally. This is independent of care rules 13 and the new capture actions. The combined trading/Auto update still needs physical RF/coexistence acceptance. Nearby requires no phone, GPS or service.

A fresh open nonce, session ID, participant pair, mode and seed bind the volatile match. The challenger is host; the guest verifies each exact next deterministic result and its submitted move. The host cannot advance again before acknowledgment and the readable hold. Both Auto and subsequent Tactical exchanges wait at least 2.4 seconds from the first acknowledgment of the previous state; queued choices do not skip that hold.

Tactical alternates attackers, with Physical/Heavy/Magic against Brace/Counter/Ward. Auto independently rolls Physical/Magic with equal odds for either attacker and one of three guards for the defender. Duel HP/energy is isolated; no cards, capture, care changes, XP or rewards occur. A match ends within 40 exchanges. Duplicate commands/results remain stable. Reconnection resumes only the same live session; leaving, reboot, cancel or timeout aborts it, and terminal results cannot become active again.

Source MAC, nonce/session, sequence and CRC enforce framing and stale-session checks; they are **not cryptographic authentication or anti-cheat**. The host chooses the seed, so this is not a fair competitive matchmaking protocol. Keep it reward-free friendly play. [Protocol, resource bounds and test evidence](NEARBY_PROTOCOL.md).

## Platform boundary

The shared C++ rules are portable and independent of network timing. The current browser requires its local service to acknowledge game commands; browser playback is not fully offline gameplay. Practice persistence is separate from companion-save persistence. Physical ESP display, GPIO buttons, NFC, motion configuration, SD power-loss behavior and radio coexistence retain their existing bring-up gates. See [hardware facts](PARK_HARDWARE.md), [ESP build evidence](ESP_BUILD.md) and [service contracts](../service/API.md).

Changing wild mode does not open radio discovery. Nearby requires its own explicit screen entry. No cloud deployment or native phone app is part of this milestone.

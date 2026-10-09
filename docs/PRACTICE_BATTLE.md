# Skill-counter practice battles

The **Battle** menu opens a separate PvE practice mode using the selected companion. Its rules are portable C++ in `core/`, invoked by the local service for this browser prototype. The browser renders choices and results; it does not calculate combat. There is no live AI, multiplayer matchmaking, reaction timer, wager or ranked reward.

Choose **Tactical** for the choices below, or confirm **Auto** to resolve both sides with a deterministic native policy and no attack/card/timing input. The mode stays fixed; saved trace replay is cosmetic. See [both battle modes](BATTLE_MODES.md) for selection, bounds and retry contracts.

## Tactical rules

Both sides begin at their saved form/level’s maximum practice HP. New rivals are Flicker, Rill or Cinder in their initial form at the selected partner’s starting numeric level. The first exchange lets the player attack; the next lets the player defend. Roles alternate after each resolved exchange. The enemy commits its move before input. A truthful hint names two possible moves, and the last two revealed enemy moves remain visible. Cards, invalid inputs, Back and retries cannot reroll that commitment.

Choose one of your companion's three named skills. Physical uses Attack against Defense with power 8; heavy uses the same stats with power 16; magic uses Magic against Resistance with power 8. Types and defensive choices then modify that damage. The [stat guide](BATTLE_STATS.md) gives the type chart, rounding order and original profile anchors; the current [form table](../core/forms.cpp) supplies each form’s stats and named moves.

| Defensive choice | Matching attack | Effect |
| --- | --- | --- |
| Brace | Physical | Halves damage, rounded down. |
| Reversal | Heavy | Reflects half damage to the attacker; defender takes none. |
| Rune Ward | Magic | Halves damage, rounded down. |

Other pairings leave damage unchanged. The resolver's minimum is 1 damage before cards. Heavy carries the risk of reflection, so its higher power is not a guaranteed better choice. Practice has no energy cost and no hidden accuracy or critical-hit roll. For example, Mote's Twig Tap hits Flicker for 14 normally or 7 against Brace; these values depend on those specific profiles.

Use one existing card per entire duel: Spark adds 5 to the next ordinary outgoing attack, or Shelter absorbs up to 12 incoming damage across hits. Spark does not boost reflections and remains prepared if your heavy hit is reflected. Shelter can block reflected damage you receive. A card is preparation, so it does not consume an exchange or change enemy intent. A battle ends in win, loss, retreat or a draw after 30 exchanges. No battle input has a time limit.

This is an original adaptation of attack/defense prediction. Dokapon's official manual describes Attack/Defend, Strike/Counter and offensive/defensive magic pairings; the numbers, names, cards and bounded practice loop here are our design. [Official battle manual](https://www.compileheart.com/dokaponkingdom/manual/img/page/page18.jpg).

## Save boundary

The selected member’s identity, explicit form and numeric level are frozen when a duel starts. Practice never writes pet health, care, XP, bond, form, steps, captures or collection. Wild encounters share the stat resolver but retain their own capture, energy and progression rules. Companion saves use **schema 7/rules 4, 500 bytes**; new practice duels use independent **schema/rules 3, 120 bytes**, in service container format 4. Existing 112-byte version-2 duels finish under frozen rules 2 with their original receipts and command bodies; they are not rebalanced mid-duel. [Migration and retry contracts](BATTLE_MODES.md#persistence-api-and-replay).

The local service stores a separate versioned practice snapshot and up to 32 exact success receipts per paired device. Commands require an expected revision and stable request ID. A lost reply is retried with the same command; it never becomes a new turn. Old commands outside the receipt window fail their stale revision instead of replaying. One data-directory writer protects both stores.

The browser retains an unsent/unacknowledged command before transmission. Pending commands block new battle input and identity changes. Corrupt or unknown pending data is retained for explicit recovery. Back leaves a committed battle available to resume; **Retreat** is an explicit core action. A new battle cannot silently replace an active one.

Hidden RNG state and enemy commitment exist only in the core snapshot kept by the local service; public responses contain hints and revealed outcomes. This is separation within a development prototype, not anti-cheat protection against the owner of the Mac.

## Current platform limit

The host module runs without a network dependency. The current browser invokes it through the loopback service, so browser choices need that service running. The practice module is now wired to a separate ESP NVS session and serial controls, including both modes, bounded replay and exact-command retries. [Serial guide and host proof](ESP_PRACTICE.md). Physical LVGL/display and GPIO controls remain absent; the round menus and sounds are browser implementations. Firmware remains unflashed.

Measured on this Mac: practice State **80 bytes**, canonical CRC snapshot **120 bytes**, fixed public JSON buffer **2,048 bytes**. These are host/source measurements, not Xtensa RAM, firmware image size or battery measurements. The service caps each practice-store copy at 1 MiB; the browser caps a pending command at 2 KiB and a public response including the bounded Auto trace at 32 KiB.

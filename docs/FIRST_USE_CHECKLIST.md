# First use — installed firmware 6e058e1

Both devices have their own unhatched save. The steps below describe the actual installed native screens. They are a hands-on acceptance checklist, not a claim that physical gestures or radio exchange have passed.

## Safe checks before choosing a partner

1. Tap **MEET PARTNER**. Swipe left/right across the creature area through **11 choices**: eight fixed and three saved Rookie offers.
2. Tap **CHOOSE** to open **READY TO HATCH?**. To keep the egg, tap **BACK**, then **BACK** again. **HATCH is the action that saves the chosen partner.** No hatch was performed during verification.
3. On the egg screen, leave the device still for **60 seconds**. Test touch wake, lift your finger, then make a fresh selection. Separately test movement wake. Idle is blocked while browsing or confirming a starter.

**Starter Info is not implemented in this build.** The carousel shows the creature, name and position. The later evolution carousel has **? INFO** for gates/stats/skills. Saved offers survive restart; browsing position restarts at the first choice.

## Local multiplayer, after deliberately hatching on each device

1. On each device, choose a partner and confirm **HATCH** only when ready. From **Home**, open **NEARBY** on both. No phone, service account, router or Internet connection is required.
2. On **one device only**, browse to the other partner, tap **CHALLENGE**, review the matchup/mode, then tap **CHALLENGE** again. On the recipient, check the offer and tap **ACCEPT**.
3. In **Tactical**, the attacker swipes through **Physical / Heavy / Magic** and the defender through **Brace / Counter / Ward**. Each uses a separate **upward swipe** to commit. Wait for both choices and the displayed exchange, then repeat as roles alternate. Heavy needs six duel energy.
4. For **Auto**, the challenger selects/confirms Auto in Settings before opening Nearby; the accepted offer displays that mode and the duel progresses automatically. **LEAVE** ends Nearby. Duels use separate battle HP/energy and grant no XP or care rewards.

Acceptance: each device should discover the other, show the same accepted matchup, agree on HP/turn/result, and return to local play after leaving. Discovery, acknowledged exchanges, live-session reconnect and Wi-Fi restoration still require real two-board testing. Reboot/leave ends a duel; persistent peer pairing is not implemented or required.

## Other installed controls, after hatching

- **Encounters:** walk while on Home/Explore. At an encounter, choose **BATTLE** for Tactical. Auto uses **AUTO BATTLE → RUN AUTO**. Browser/CLI simulated steps do not verify the physical pedometer.
- **Battle:** horizontal swipes select Physical/Heavy/Magic; a separate upward swipe commits. Wait while the saved exchange plays.
- **Capture:** at half target HP or lower and with collection room, tap **CATCH**. Touch the bottom orb and flick upward toward the ring. There are three committed throws; misses spend a throw. Actual odds appear after aimed throws, with no percentage shown for a miss. Wait for the saved result; rules12 throws cause no retaliation.
- **Collection/evolution:** **PARTNERS → swipe → STATS + EVOLVE**. Swipe through stat pages. Select an inactive creature as the partner before evolving it. **DIGIVOLVE → ? INFO** shows requirements/comparisons; swipe for skills. **SELECT** explains missing gates or opens a separate final confirmation. **BACK** cancels.
- **Screen idle:** **SETTINGS → SCREEN TIMEOUT** cycles **60 → 120 → 300 → Off → 30 seconds**. Touch and motion sampling remain active while the backlight is off. A wake contact is consumed; release before acting. Battle/capture/evolution/Nearby/setup and certain work or recovery states prevent idle.

## What is implemented versus verified

The installed [Nearby runtime](../firmware/main/handheld_nearby.cpp) connects discovery, challenge/accept, shared Tactical/Auto resolution and radio cleanup. [Protocol details](NEARBY_PROTOCOL.md) and host fault tests cover bounded retries and live-session reconnect. This is direct ESP-NOW multiplayer; **service account pairing and remote save synchronization are separate future work**.

The egg screen has no Nearby control, and the native entry requires Home plus an actual owned partner. The installed console has no radio-only discovery/transport command. Therefore preserving both unhatched eggs prevents an actual ESP-NOW test on this firmware. USB status/snapshot checks are not peer-radio exchange. No temporary partner, saved-state substitution, firmware patch or hatch was used to bypass this boundary.

Latest read-only preflight: Unit 1 answered and its own 636-byte egg snapshot was unchanged. Unit 2 returned a bootloader log but no application reply, including one bounded reopen; its current save could not be re-read. Its earlier verified release checkpoint remains valid as historical evidence. This is an open release reliability issue. Observe whether its screen responds before another USB open; do not repeat a blind reconnect or hatch to recover USB. The host opening correction is tested without devices but physical recovery remains unverified. [Diagnosis and bounded next step](USB_BOOT_RELIABILITY.md) · Preflight evidence (historical local evidence omitted).

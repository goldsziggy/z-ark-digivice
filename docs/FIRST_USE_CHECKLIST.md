# First use and acceptance — firmware 8be26c6

This checklist describes the installed `8be26c6` milestone. The prepared
60-Digimon / graded-ring source update is documented in [ROSTER60](ROSTER60.md)
and has not been flashed to either board.

Both devices retain their own existing profiles after the save-preserving update.
This guide describes the native `8be26c6` controls. Installation and storage checks
are separate from hands-on acceptance: audible output, counted-walk accuracy,
physical finger interaction and two-device radio behavior still need testing.
[Installation checkpoint](TWO_DEVICE_PREPARATION.md) · [Release and limits](LANYARD_NEARBY_AUDIO_RELEASE.md).

## Start with the retained profile

1. If the screen is blank from its saved screen timeout, tap to wake it, release,
   then make a fresh selection. Check that the existing partner or saved state is
   present; do not reset or hatch a replacement as part of testing the upgrade.
2. At **Home**, swipe horizontally or tap the side arrows to move among **CARE**,
   **PARTNERS**, **SETTINGS** and **NEARBY**. Tap the central action to open that
   panel; Nearby's action is **FIND NEARBY**.
3. If a saved battle or capture result is playing, let the presentation finish.
   Replaying its saved animation does not repeat the capture or reward. An Auto
   encounter waiting for capture requires a real flick or **SKIP / RESUME FIGHT**.

Only if a device is still an unhatched egg: tap **MEET PARTNER**, then swipe or use
side taps to browse the displayed choices. Current starter offers provide eleven
choices: eight fixed and three saved Rookie offers. Tap **CHOOSE** to review
**READY TO HATCH?**. **BACK** cancels; **HATCH** saves the chosen partner. The
starter carousel shows name and position; it has no Starter Info page. No hatch
is needed for a device that already has a partner.

## Nearby battles with both existing partners

1. Return both devices to Home and open **NEARBY → FIND NEARBY**. A real owned
   partner and no active wild battle are required. No phone, account, router or
   Internet connection is required; Wi-Fi is paused during Nearby.
2. On one device, browse to the other trainer and tap **BATTLE**. Review both
   partners, then tap **TACTICAL** or **AUTO**. The selected button has an arrow
   and highlighted border. Tap **CHALLENGE** separately to send the invitation.
3. On the recipient, check the matchup and the displayed **TACTICAL FRIENDLY
   DUEL** or **AUTO FRIENDLY DUEL**, then tap **ACCEPT** or **DECLINE**. A changed
   peer or offer requires a fresh review.
4. In Tactical, tap the side controls or swipe horizontally to choose
   **Physical / Heavy / Magic** when attacking or **Brace / Counter / Ward**
   when defending. Use a separate upward swipe to commit. Wait for both choices
   and the exchange animation, then repeat as roles alternate.
5. In Auto, attacks and defenses progress automatically after acceptance; no
   attack selection or capture gesture is required. **LEAVE** ends Nearby.

The Nearby mode choice starts at Tactical when Nearby is reopened and does not
change the saved wild-battle mode in Settings. Both invitation screens show the
agreed mode. Duels use separate HP/energy and award no XP, care rewards or captures.
For acceptance, verify that both displays agree on the matchup, mode, HP, turn
and result, then return to local play. Actual discovery, packet exchange,
live-session reconnect and Wi-Fi restoration still need two-board testing.
A brief disconnect may resume the same live duel; leave, reboot or timeout ends
it. Persistent peer pairing is not required. [Protocol and security limits](NEARBY_PROTOCOL.md).

## Walking and encounters

- Home always shows the retained **STEPS** lifetime total. **SENSOR RECOVERING**
  or **SENSOR UNAVAILABLE** describes current sampling; the total remains visible.
  Recovery retries automatically and does not estimate steps missed during an
  outage. Gyro tilt and stillness calibration are separate from step detection.
- Under **SETTINGS → ENCOUNTER SETTINGS**, choose **PAUSED**, **RELAXED**,
  **NORMAL** or **FREQUENT**. Paused stops new encounter progress while lifetime
  counting continues. A previously earned encounter can remain queued.
- With a healthy sensor and saved partner, fresh steps can advance encounter
  progress in menus and battles. One waiting encounter appears when Home is
  quiet; it does not interrupt a held touch or active duel. Screen idle keeps
  sampling active and is not deep sleep.
- Compare lifetime deltas with counted walks at slow/normal/brisk paces and with
  hand, pocket and neck-lanyard carry. Also check stationary handling and short
  stop/start walks. The detector is a forgiving estimate: rhythmic handling or
  swinging can count. No measured accuracy percentage is established. Browser
  or CLI step injection and generated traces do not validate physical accuracy.

## Sound and other controls

- **Sound:** open **SETTINGS → SOUND SETTINGS**. Tap the side arrows or swipe to
  choose **0 / 5 / 15 / 30 / 50 / 75 / 100%**. **MUTE** and **MUSIC** are separate;
  effects do not require music. Start at a low volume, check effects
  with music off, then enable music deliberately. Settings save as they change;
  **SOUND SETTINGS NOT SAVED** reports a persistence problem. Installation checks
  preserve existing preferences; they do not prove new preference writes or
  audible speaker output.
- **Wild battles:** **SETTINGS → MODE** changes the saved wild mode through a
  separate confirmation. At an encounter, tap **BATTLE** for Tactical or
  **AUTO BATTLE → RUN AUTO**. Tactical uses horizontal selection and a separate
  upward swipe to commit; wait while each saved exchange plays.
- **Capture:** when the target is at half HP or lower and there is collection
  room, Tactical offers **CATCH**. Touch the bottom orb and flick upward toward
  the ring. Auto pauses at its capture opportunity for the same manual flick;
  **SKIP / RESUME FIGHT** resumes fighting for the rest of that encounter.
  There are three committed throws; misses spend a throw. Capture throws cause
  no retaliation. This capture flow is for wild encounters only.
- **Collection/evolution:** open **PARTNERS → STATS + EVOLVE**. Swipe through
  stat pages. Set an inactive creature as partner before evolving it.
  **DIGIVOLVE → ? INFO** shows requirements/comparisons; swipe for skills.
  **SELECT** explains missing gates or opens a separate confirmation. **BACK**
  cancels.
- **Screen idle:** **SETTINGS → SCREEN TIMEOUT** cycles **60 → 120 → 300 → Off →
  30 seconds**. Use the device's saved timeout when testing wake. Touch/motion
  sampling remains active while the backlight is off; release the wake contact
  before acting. Battle, capture, evolution, Nearby, setup and work/recovery
  states can prevent idle.

## Verification boundary

Native Nearby uses direct ESP-NOW. Service account pairing and remote save sync
remain separate future work. An egg has no Nearby control; hatch only if that is
an intentional user choice, never as an installation or USB-recovery workaround.
The earlier egg-only preflight and USB-silence reports are retained as historical
evidence, not current profile status. [USB diagnosis and limits](USB_BOOT_RELIABILITY.md).

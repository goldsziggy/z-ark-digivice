# Touch controls and physical playtest readiness

**Two-device status:** Unit 1 runs **`8a420c7`**. Its app-only upgrade, all **261 destination SHA-256 checks**, eight starter DVA decodes, scene JPEG decode and display-transfer counters passed. Its own **576-byte saved egg remained unchanged and survived software reboot**; no hatch was performed. Unit 2 remains on **`90ee501`**, currently absent from both USB and serial enumeration after the user confirmed reconnection; a check with Unit 1’s working cable and Mac port is pending. Actual visible-screen, finger, sound, battery and hotspot acceptance remain pending. [Per-device status](TWO_DEVICE_PREPARATION.md) · Current evidence (historical local evidence omitted).

The current build uses **1,449,936 app bytes** and **145,419 static DIRAM bytes**, with **1,695,792 bytes free** in the unchanged 3 MiB app slot. Current build pins (historical local evidence omitted). These measurements and successful boot do not establish visible orientation, comfortable targets, actual finger coordinates or audible sound. Physical finger/visual/audio acceptance, battery behavior and hotspot connection remain unverified.

**Earlier touch checkpoint:** unit 2 `90ee501` reported valid LCD/touch/audio/IMU initialization, **148 touch samples / zero errors**, and **140.989 ms maximum observed frame cost**. Its own saved egg and canonical snapshot survived software reboot at that checkpoint. Unit 1's earlier `59bf076` measurements remain historical evidence (historical local evidence omitted); neither record completes the current physical acceptance.

## First acceptance check: keep the egg

Unit 1 has passed boot, asset hashes and decode/transfer checks, and the installation pause is released. Unit 2 must first reappear in USB enumeration and pass its boot/status check. These user observations remain pending; the checklist does not establish a pass.

1. **Screen:** check that the egg, text and controls are upright, readable and fully inside the circle. Note any blank screen, rotation, tearing or clipped controls.
2. **Touch:** tap **MEET PARTNER**, then **NEXT >** once, **< PREV** once and **BACK**. Each tap should select only its labeled control; the last tap should return to the egg. Leave **CHOOSE / HATCH** untouched during this first check.
3. **Audio:** listen for a quiet navigation cue during those taps. Report silence, distortion or excessive volume; the configured default is 15%, which does not establish the actual acoustic level.
4. **Setup:** tap the egg's **SETUP**, check that separate Wi-Fi, clock and server statuses are legible, then tap **CLOSE**. This check needs no password or network changes.
5. **Report:** `screen: …; touch: …; sound: …; setup opens/closes: …; egg still shown: …`. Report whether starter previews show detailed sprites or abstract fallback, but treat SD loading as unverified until diagnostics confirm the actual files were decoded.

The full playtest below deliberately hatches and changes the adventure. After the first check, verify the combined build's saved snapshot before proceeding to those saved actions. Hotspot entry is a separate step in [DEVICE_SETUP.md](DEVICE_SETUP.md); keep the password on the device.

## Physical touch playtest path

Use this path after the compatible playtest artifact has been installed and its boot has been checked. It changes the local adventure only at explicit saved actions; navigating or backing out of a review does not hatch or change mode.

1. From an existing saved egg, tap **MEET PARTNER**, browse **< PREV / NEXT >**, tap **CHOOSE**, then **HATCH** on the separate review. **BACK** cancels before hatch. Existing hatched saves open Home.
2. Tap **CARE**, then **FEED**, **PLAY** or **REST +25**; use **BACK**. Rest is one ordinary recovery action, not the browser's full-recovery flow.
3. Tap **EXPLORE → DEMO ENCOUNTER → ADD 100 STEPS**. The review explicitly says these are **simulated steps** added to the saved adventure. Physical step counting is disabled in this build, even when the raw IMU works.
4. Tap **BATTLE**. Use **ATTACK**, **MAGIC** or **HEAVY** to lower the wild creature to half HP or below. **CAPTURE** then becomes available unless another core limit applies.
5. Tap **CAPTURE**, touch the mint orb and flick upward toward the ring. **BACK** exits before a throw. A legal release emits one native `Flick` action; a miss can spend an attempt and cause retaliation. The result has **HOME**; the **PARTNERS** page can browse members and **MAKE PARTNER**.
6. In **SETTINGS**, tap **SOUND ON** to mute or **SOUND OFF** to unmute. Sound defaults on at 15% software volume. **GYRO OFF** enables cosmetic avatar tilt after a still-position recenter; **GYRO ON** disables it. Tilt never confirms a choice or throws a capture orb.
7. **MODE: TACTICAL / MODE: AUTO** opens a separate review. Only a new **CONFIRM** tap changes the saved mode; **BACK** cancels. Auto encounters use **AUTO BATTLE → RUN AUTO**. A changed save revision or input permission cancels a pending mode review.
8. In the combined setup build, the egg’s **SETUP** or Home’s **SETTINGS → WIFI SETUP** opens network setup without hatching or changing the game. Use **CLOSE** to return. Wi-Fi remains optional; follow the [setup guide](DEVICE_SETUP.md) to enter a hotspot password privately on the device.

The physical renderer retains original abstract avatars, bitmap text, blink/bob, attack flash and bounded orb-flight animation. The combined build can overlay a validated exact-form SD sprite and allowlisted scenery, with opaque controls and the original fallback when art is missing or invalid. Unit 1 mounts its exFAT card; all 261 file hashes, eight starter sprite decodes and scene JPEG decoding passed. Display-transfer counters advanced from 14 to 34 during starter browsing; that does not establish human-visible appearance or finger accuracy. Unit 2’s upgrade and copy remain pending. The [USB transfer route](USB_SD_TRANSFER.md) needs no Mac card reader; [per-device checks](TWO_DEVICE_PREPARATION.md) retain each unit’s separate status. This screen set covers onboarding, care, wild battles, capture, partners, settings and setup; additional catalog, evolution and practice screens remain in the browser/USB.

One acceptance pass: confirm the egg is upright, complete the touch path above, hear a quiet cue, then enable gyro and check gentle avatar motion while the board remains in your hand. Report a blank/rotated screen, mismatched touch coordinates or missing sound instead of resetting a save. These user observations remain pending. Unit 2 has valid raw IMU readings but incomplete calibration; hold it still before assessing optional tilt, and do not assume a passed calibration from the older unit 1 result.

## Browser controls

Run the existing local service with `npm start`, then open its local URL. Touch is the default. Tap the displayed choice, use **Prev / Next** to browse, and use the fixed **Back** control to return or cancel an unsent action. Swipe the details panel to read longer descriptions. Changing choice restores the panel to its top. The controls selector, or `?controls=buttons`, retains the previous two-button layout and keyboard behavior; choosing a mode updates the URL so reload retains it.

The preview uses a 412 × 412 CSS-pixel content area at a sufficiently wide browser viewport, matching the selected 1.46 panel's logical resolution. It is **not a calibrated life-size preview**. Main touch targets are approximately 70 pixels tall there, about 6.3 mm if mapped directly to a 37.1 mm active diameter. That calculation is an estimate, not proof of comfortable physical finger targets. The first physical UI uses 38–54-pixel-high controls, approximately 3.4–4.9 mm at that assumed active diameter. Their comfort and edge accessibility still require actual fingers on the panel; browser geometry does not establish that result.

Touch capture uses an original turquoise orb. Drag it and flick upward toward the creature; Cancel exits before release. The native core resolves the bounded landing point: an aim miss consumes one attempt and normal retaliation, while a hit uses the existing capture chance. Two-button mode keeps ordinary capture. [Input contract](FLICK_CAPTURE_CONTRACT.md) · [Sound, hardware and optional gyro plan](MOTION_AND_SOUND.md).

Auto battles still commit their result once before the animation. **Pause replay**, **Resume replay**, and **Show saved result** affect only its presentation; they do not pause, reroll or resend the saved battle. Pending-save screens keep their retry identity and prevent cancellation of an already sent action.

The connection screen pairs a browser identity with the local service. It is not device Wi-Fi provisioning. Browser file imports and optional text search remain in browser tools; in-round letter/stage filters provide a touch route through the catalog without text entry. The separate combined firmware now implements its own bounded native keyboard for Wi-Fi and service setup; browser pairing does not configure it.

## Verification

The tests use a fresh temporary service/store and isolated headless Chromium with touch enabled. Game commands pass through the real service/native core. DOM inspection checks geometry; it does not invoke hidden buttons. They do not use the foreground browser, user saves or private artwork.

- `tests/browser-touch.mjs`: first pairing, all eight eggs, review cancellation and hatch, primary menus, care, partner/stats, progression requirements/graph, catalog/filters/journal, settings and saved-playtest flows. Actual touch swipes check scrolling and fixed Back; an interrupted asset download resumes from its saved range. Evidence: `docs/evidence/touch-only-navigation.json`.
- `tests/browser-touch-gameplay.mjs`: genuine native-history fixtures exercise attack, card use, capture cancellation/failure/success, roster paging, partner selection, recovery, release and evolution. Wild Auto replay controls are exercised after one durable command. Fixture construction is distinguished from the actions tested through the UI. Evidence: `docs/evidence/touch-only-gameplay.json`.
- `DIGIVICE_TOUCH_ONLY=1 ./scripts/node.sh tests/browser-battle-modes.mjs`: wild Auto and tactical/Auto practice, cards, both combatants' stat pages, continuing the same duel, explicit confirmation, result dismissal, replay pause/resume (including the final turn) and exact retry following lost responses and service/browser restarts. The same test without the flag checks two-button mode.
- `tests/browser-touch-recovery.mjs`: rejected game-command and legacy practice-command recovery dialogs, cancellation preserving pending bytes, explicit discard/archive and unchanged authoritative saves. Evidence: `docs/evidence/touch-only-recovery.json`.
- `tests/browser-flick-capture.mjs`: actual touch drag/release, weak and wide aim misses, a hit/capture, cancellation, stale state, eligibility gates and durable retry. Evidence: `docs/evidence/flick-capture-browser.json`. Headless Chromium cannot prove native hidden-tab event delivery; the gesture unit tests cover the visibility branch.
- `tests/browser-audio.mjs`: actual PCM rendering and trusted-touch sound activation; `docs/evidence/audio-render.json` records measured samples and limits.
- `tests/browser-navigation.mjs` retains the earlier two-button/native-focus regression. Focused navigation, input and Auto trace module checks also run.

Set `PLAYWRIGHT_MODULE` to an existing Playwright `index.mjs` and optionally `PLAYWRIGHT_CHROMIUM` to an existing headless Chromium executable. No new browser/tool installation is required by these scripts. Recorded evidence is the source of truth for completed checks; a script's existence alone is not a pass.

The earlier touch navigation milestone’s consolidated verification record (historical local evidence omitted) identifies exact tested source hashes and logs. Its baseline commit records the checkout before this change; the per-file hashes identify the tested working tree. A `mode-placeholder` screen identifier has no product entry and is not an implemented game mode. The later flick and sound verification record (historical local evidence omitted) pins the current capture/audio code and regenerated gameplay proof; the earlier navigation record remains historical at commit `d9a1b20`.

## Actual ESP32-S3-Touch-LCD-1.46 implementation

| Capability | Source/build status | Physical acceptance |
|---|---|---|
| Shared core and durable saves | Existing copy → apply → verified checkpoint → publish path; no new save fields | Unit 1’s own egg and snapshot survived the post-copy `8a420c7` software reboot; earlier unit 2 own-snapshot reboot passed on `90ee501` |
| Display and touch | Vendor-derived SPD2010 QSPI driver, bounded shared-I²C touch reader, circular 412² renderer | Unit 2 initialization/transfers measured; visual/finger acceptance pending |
| Touch game UI | Egg/review, care, demo encounter, wild Tactical/Auto, flick capture, partners and settings | Host flow passes; actual fingers pending |
| Speaker | Single bounded I²S synthesis worker; mute, 15% default software volume, shutdown draining | Audibility/level and combined operation pending |
| Gyro / tilt | Timestamped acceleration/angular velocity, still calibration and optional cosmetic tilt | Unit 2 calibration incomplete; visible axes/neutral pose pending |
| Physical steps | Pedometer remains disabled; `stepsAvailable` is separate from raw IMU readiness | No walking-accuracy claim |
| SD artwork / Wi-Fi | Exact-form sprite/scenery renderer, USB-to-SD transfer and on-device setup keyboard compiled in `8a420c7` | Unit 1 exFAT mount, all 261 hashes and eight starter/JPEG decodes passed; human artwork/hotspot/HTTPS acceptance pending |
| NFC / GPIO buttons | No NFC reader fitted; both builds default NFC off | Unit 2 reports NFC unavailable; local play remains complete |
| Power | Existing PWR controller plus display/audio/IMU suspend barrier | Battery/current and physical power-loss tests pending |

The board-owned master I²C bus remains SCL10/SDA11. Touch is 0x53 with INT4/reset EXIO1; LCD reset is EXIO2. The shared expander owner preserves SD's P2 configuration; no second legacy I²C driver is installed. LCD QSPI uses the exact 1.46 map and a 40 MHz first-port clock. [Pin/source audit](PARK_HARDWARE.md) · [Driver provenance](../firmware/main/third_party/spd2010/PROVENANCE.json). No older 2.1-inch pins are substituted.

## Firmware verification and limits

`tests/device_ui_test.cpp` passes **532 checks**, covering starter/mode reviews, failed-save retention, legal actions, native flick packing, stale/paused/out-of-circle input, framebuffer bounds, exact-form/masked artwork and setup navigation. Setup keyboard checks pass **2,382**, network policy **757**, network adapter **57**, and `firmware/tests/audio_imu_test.cpp` **172**. UI sanitizer checks pass. Current transfer checks add **277 runtime, 27 USB installer, 318 FatFs and 8,663 transfer-engine checks**; the runtime harness uses device I/O doubles and does not directly test the audio worker’s pause acknowledgement. These host checks do not establish panel readability, actual finger input, audible sound, card mounting or hotspot connectivity.

The renderer uses a **339,488-byte PSRAM frame** and **13,184-byte internal DMA stripe**. Unit 1’s earlier source `59bf076` measured maximum frame/render/flush costs of **75,653 / 29,956 / 45,691 µs**, with **206,143 internal bytes** and **8,044,504 PSRAM bytes** free at the sampled checkpoint. These are separate recorded maxima and snapshot heaps, not FPS, touch latency or minimum lifetime heap. Unit 2’s earlier `90ee501` checkpoint recorded a 140,989 µs maximum frame cost; actual loaded artwork/network work still needs measurement. Touch I/O failure cancels a gesture and requires a fresh release before rearming. Physical evidence (historical local evidence omitted) · Current build record (historical local evidence omitted).

The earlier browser results above remain valid within their recorded scope. Browser visibility, DOM scrolling, private sprite rendering and network retry tests are not tests of the physical firmware's first screen set.

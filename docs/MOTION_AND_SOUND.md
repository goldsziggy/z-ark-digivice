# Capture, motion and feedback — historical bring-up

This page retains the earlier bring-up record. Its installed versions, disabled-step statement and mute-only sound controls are historical. Current f9 adds a software pedometer and saved sound controls; the next [lanyard, recovery and sound update](LANYARD_NEARBY_AUDIO_RELEASE.md) adds bounded recovery, 0–100% volume and a corrected DAC clock.

**Historical two-device status:** unit 2 runs **`90ee501`** after an independently verified 16 MiB backup and four verified flash-image hashes. Boot, command response, software reboot and restoration of its own 576-byte saved-egg snapshot passed. Unit 1 remains on **`569643d`**, awaiting USB reconnection after the known stall; no further flash or reset was attempted. Physical screen/touch/audio acceptance and SD artwork verification remain pending. [Per-device status](TWO_DEVICE_PREPARATION.md) · Evidence (historical local evidence omitted).

## Touch capture is the primary interaction

Choose Capture, touch the turquoise signal orb, and flick upward toward the creature. Pulling back first is optional. The final part of the gesture determines direction and strength. The orb and aiming ring are original procedural artwork; no Pokémon assets are imported.

Short taps, downward/insufficient movement, cancelled pointers, another finger, leaving the round screen, resizing, changed save state and backgrounding before release do not submit a throw. A valid armed release submits one durable `flick` command. A missed aim spends one existing attempt and receives normal retaliation; a hit runs the existing capture chance. There is no new ball inventory, critical-hit system or motion accuracy bonus. Lost acknowledgements retry the same command. Two-button mode retains ordinary capture. [Native input and replay contract](FLICK_CAPTURE_CONTRACT.md).

The browser draws the flight from bounded input values. The native core resolves the hit test and capture outcome; success is shown after its saved response. Network trouble retains the browser's pending throw for explicit retry. The measured `59bf076` implementation uses the same packed native `Flick` contract with local durable saves and a 412² circular touch renderer. Hardware initialization and timing are measured; user-observed screen/touch acceptance is pending. The physical screen uses **BACK** to cancel, and does not depend on browser pointer/visibility or network behavior. [Physical playtest path](TOUCH_READINESS.md#physical-touch-playtest-path).

## Implemented optional tilt

The implementation reads timestamped QMI8658 acceleration and angular velocity through the existing shared bus. The bounded filter requires at least one second and 40 still samples to calibrate/recenter; movement restarts that window. **SETTINGS → GYRO OFF** enables cosmetic avatar tilt, capped at eight screen pixels per axis; **GYRO ON** disables it. Gyro starts off. Stale/faulted or uncalibrated readings supply no tilt, and resume/recenter resets calibration. These paths are compiled and host-tested. Unit 1’s earlier `59bf076` checkpoint reached `calibrated=1`. Unit 2 `90ee501` has valid raw readings but remains `calibrated=0`: a sampled **6.95°/s** rate exceeds the **5°/s** stillness gate. Actual stillness was not observed; this does not establish a sensor defect. Hold the device still before assessing tilt. Visible axes and neutral pose remain unconfirmed.

Touch flick remains the only capture gesture in this physical screen set. Motion does not select, confirm, attack or spend an attempt. Raw IMU readiness does not enable a pedometer: the current profile has **physical steps disabled**, and **DEMO ENCOUNTER → ADD 100 STEPS** explicitly adds simulated steps to the save.

## Future motion ideas

The screen flick needs no gyroscope. Beyond the cosmetic tilt above, these remain proposals:

| Idea | Suggested scope |
|---|---|
| Tilt the scenery or battle camera slightly | Cosmetic parallax; a useful first motion feature because it changes no game outcomes. |
| Lean left/right to preview a move or guard | Optional Previous/Next selection, followed by touch confirmation. Never let motion commit an attack, release a companion or consume a capture attempt. |
| Optional capture aim adjustment | Gentle wrist rotation could offset a visual aim guide while a finger explicitly holds the orb. The touchscreen flick remains the throw, and touch-only use stays complete. |
| Timing guard or balance challenge | Future separate game mode with explicit rules and tests, rather than changing current Tactical/Auto outcomes implicitly. |

A gyro measures angular velocity; acceleration is needed to distinguish linear flicks or shaking. Any future motion navigation should build on the implemented still-position calibration and a physically verified sensor-to-screen transform. Candidate tilt tuning is 12° to trigger, 6° to rearm at neutral, 120 ms dwell and at most three selection events per second. These are assumptions awaiting physical testing. Suppress motion input during touch, confirmations, stale data, sensor faults and standby; recenter after resume. No physical throwing action is required or proposed.

## Sound and vibration hardware

The exact Waveshare ESP32-S3-Touch-LCD-1.46 board has a fitted 12 × 10 mm speaker at J4, a **PCM5101APWR DAC** at U9 and an **NS8002 amplifier** at U7, with R57 volume adjustment. I²S uses BCLK GPIO48, LRCK GPIO38 and DIN GPIO47; the pinned vendor configuration has no MCLK. J4's schematic speaker symbol is not a reason to buy another harness. [Official schematic/BOM](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.46/ESP32-S3-Touch-LCD-1.46.pdf), [fitted-speaker photograph](https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-s3-lcd-1.46-4_3.jpg), [vendor I²S configuration](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.46/blob/fda89ff2ff32eb1bce561901225c6a36fa684237/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/main/Audio_Driver/PCM5101.h).

The complete official schematic and BOM contain **no vibration motor or haptic driver**. No vibration toggle, hardware module, BOM or enclosure change was added. A phone browser's Vibration API would not establish vibration support on the Digivice.

The browser's original Web Audio cues include menu confirmation/back, physical and magic attacks, impact, capture arming/throw/wiggle/result and evolution. Mute, volume and explicit sound activation remain in Settings → Sound. Overlap and repeat rates are bounded, outcome cues take priority, and accompaniment ducks. The `crit` cue is available for the sound demo but is not played as a made-up critical game event. [Cue names, limits and provenance](AUDIO_CUES.md).

The physical build includes one bounded I²S TX worker for original synthesized cues at 22,050 Hz. Sound starts enabled at **15% software volume**; **SETTINGS → SOUND ON** mutes it and **SOUND OFF** unmutes it. The software volume cap is 50%; the physical menu currently exposes mute rather than a volume slider. Mute, stale requests and power suspension clear queued work, and shutdown waits for quiescence. No vendor MP3/voice stack or background music is included.

`firmware/tests/audio_imu_test.cpp` passes **172 host checks**, including synthesis bounds and IMU/filter behavior; the expanded physical UI/artwork suite passes **532**. Unit 2 reports valid audio initialization; the earlier unit 1 `59bf076` record reports `audio ready=1`, `volume=15`, `ESP_OK`, and accepts a sound-cue request, but audibility and comfortable acoustic level remain unconfirmed. That earlier unit 1 calibration passed; unit 2 calibration remains incomplete. Axis orientation, battery current and actual SD-artwork/network workload still need physical checks. The audio task/queue/DMA budget is approximately 8 KiB plus SDK overhead, an estimate rather than isolated runtime measurement. Physical evidence (historical local evidence omitted) · Build record (historical local evidence omitted) · [Touch playtest](TOUCH_READINESS.md) · [On-device setup](DEVICE_SETUP.md).
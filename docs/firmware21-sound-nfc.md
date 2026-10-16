# 2.1-inch sound and NFC repair

The buzzer worker could retry the shared I2C mutex at the same 20 ms phase as the IMU/touch tasks, losing every ON edge despite a mostly idle bus. The worker now waits for up to one 10 ms tick (at least one tick), checks cancellation and cue age under the lock, and preserves the masked P7 latch/direction transaction and OFF recovery. Initial readiness requires verified output state. Diagnostics distinguish queued, started, confirmed and completed cues and report confirmed edge times and maximum ON interval. Output-latch verification does not establish audible sound.

NFC Test no longer uses the gameplay card latch's 250 ms freshness limit as a reason to cancel an in-flight UART session. Its existing command/session deadlines remain bounded. The same runtime owner services diagnostics after rendering and between display-flush stripes. Sleep, navigation, cancellation, the gameplay freshness policy and the empty production UID allowlist remain enforced. SAM normal mode still precedes the firmware query. Bounded traces include discarded input, UART errors, poll gaps and the original failure separately from cleanup failure. An ACK timeout is never reported as no card.

The Waveshare 2.1 profile defaults reproduce the tested native USB build. GPIO43/44 stay assigned to the external PN532 UART; TCA9554 P7 remains the buzzer output. The 1.46 profile retains its PCM support. No pin probing, power switching, voltage change, card write, save-format change or asset migration is part of this repair.

## Build and checks

Use ESP-IDF 5.3.6 with the repository's previously validated I2C timeout and exFAT SDK adjustments (see `firmware/tests/test_i2c_nack_timeout.py` and `test_fatfs_exfat.py`). `scripts/build-esp.sh waveshare21` builds without accessing hardware. The version is `rules22-soundnfc-1016`.

Relevant host suites:

```sh
bash firmware/tests/test_buzzer.sh
bash firmware/tests/test_audio_worker.sh
bash firmware/tests/test_audio_settings.sh
bash firmware/tests/test_pn532_hsu.sh
bash firmware/tests/test_pn532_startup.sh
bash firmware/tests/test_nfc_test_screen.sh
bash firmware/tests/test_nfc_gameplay.sh
bash firmware/tests/test_idle_runtime.sh
bash firmware/tests/test_power_runtime.sh
```

The regressions run actual worker/HAL and UART adapter code against hardware doubles, including periodic I2C contention, cancellation during acquisition, stale requests, readback failures, delayed UART responses, 10–500 ms render stalls, discarded input, UART errors and failed cleanup. Simulated UIDs are test data, not physical card reads. Static Xtensa stack checks are in `scripts/check-esp-main-stack.py` and include the added display-flush callback path.

## Branch scope and deployment

The prerequisite commit imports the already-deployed rules22 native firmware/core and its synthetic regression fixtures because GitHub main predates that release. The repair is a separate commit on top of those prerequisites. Browser/service implementation and the user's unrelated dirty checkout are not part of this repair.

Deployment must identify the board by actual MAC, checkpoint and back up its current save/NVS/full flash, verify partition and protection settings, and write only the verified `ota_0` application partition. Verify app readback, unchanged non-app bytes, boot identity, saved progress/settings/assets, buzzer OFF and UART release. Never clone an older save or flash a smaller-board image. The generic IDF whole-image flashing command is not an app-only preservation procedure.

A working reader requires a real SAM ACK and firmware response; a card read additionally requires a real UID. Register counters and passing host tests do not establish either audible output or NFC hardware communication.

Hardware references: [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1), [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-2.1_schematic_diagram.pdf), [ELECHOUSE PN532 MINI manual](https://www.elechouse.com/wp-content/uploads/2024/07/Elechouse-Pn5321-Mini-Product-Manual-V1.pdf).

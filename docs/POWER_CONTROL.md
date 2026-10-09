# Onboard power control

The Waveshare ESP32-S3-Touch-LCD-1.46 standard-glass board (SKU29565) uses its **onboard PWR button**, separate from the proposed ACTION/BACK buttons. The case work is exposing this existing switch; an additional electrical switch is not planned.

## Gesture

| Action | Behavior |
| --- | --- |
| Start on battery | Press onboard PWR to start the hardware supply. Firmware asserts GPIO7 early in boot. Release PWR before another gesture is accepted. |
| Short press while running | Does not shut down. |
| Hold PWR continuously for three seconds | Shows a 3–2–1 countdown in the current serial status interface, then begins orderly shutdown. Release before the threshold to cancel. |
| Keep holding after the countdown | Saves and drains work, then waits for release. It does not drop the battery latch while PWR remains pressed. |
| Release after preparation succeeds | Firmware lowers GPIO7. Battery-powered hardware should switch off; this still requires physical verification. |
| Processor remains powered after latch release | Enters explicit quiet standby. This is expected with powered USB, but firmware does not claim to identify the power source. |
| Resume from quiet standby | A fresh PWR press and release restores the power hold before normal work resumes. |
| Shutdown preparation fails | Power stays on and status explains the reason. Resolve the reported condition before trying again. An uncertain save requires recovery, not repeated blind writes. |

The physical LCD renderer is still absent: **the countdown is currently visible on the USB serial console, not proven on the round screen**. `power status` reports the current phase, countdown, hold state and failure reason. Power state/countdown are separate from saved game state so a future display can render them. The host power demo uses the same portable controller and simulates the button and shutdown results; it does not operate hardware.

## Interactive screen demo

Start the local service with `npm start`, then open **`http://127.0.0.1:8787/power-demo.html`**, or use **Playtest tools → Open the power button demo**. Hold its PWR control to see the large 3–2–1 countdown on the existing browser round-screen renderer. Release early to cancel. The scenario controls demonstrate successful simulated saving, failed saving with power retained, battery-off and surviving-power standby. A fresh short press/release resumes standby. No pairing, game save or asset import is needed.

This is an isolated **browser presentation simulation**, not compiled firmware and not a connected-device console. It uses the existing `web/device-screen.js` renderer with a small presentation model. Save/drain timing and power results are simulated; no browser result proves SD durability, GPIO behavior or LCD operation. It never calls game APIs or stores game data. The host `npm run demo:power` continues to exercise the actual C++ power controller separately.

The browser check (historical local evidence omitted) exercised the countdown in real elapsed time, cancellation, held startup, save failure, battery restart, standby/wake, keyboard input and a 390px mobile layout. It observed no game/API requests or storage/data changes. Blur cancellation uses an explicitly simulated browser event because headless Chromium did not deliver native focus changes. Run `npm run test:browser:power` with the existing `PLAYWRIGHT_MODULE` and optional `PLAYWRIGHT_CHROMIUM` paths. Countdown screenshot (historical local evidence omitted) · failed-save screenshot (historical local evidence omitted) · standby screenshot (historical local evidence omitted).

The exact physical-display dependency is the **412 × 412 SPD2010 panel initialization and bounded pixel-transfer/flush path**, including reset through the board-owned TCA9554 expander and GPIO5 backlight. The profile declares the verified wiring, but `firmware/main/CMakeLists.txt` does not integrate the vendor LCD/LVGL driver, `board_hal.cpp` reports no display capability, and `HandheldRuntime::printPower()` only prints serial status. The compatibility reference is the pinned vendor `LCD_Driver/Display_SPD2010` and `LVGL_Driver` code identified in [the hardware audit](PARK_HARDWARE.md). First bring up that minimal panel path on the selected board, preserving shared-expander ownership and the SD-select bit; then render the existing power phase/countdown/reason over it. No speculative display framework or unverified panel code was added for this demo.

## Durability and standby

At the threshold, the main task stops accepting gameplay changes and new work. It pauses motion sampling, preserves confirmed pending steps, waits for active asset/network work, verifies the care save, and flushes initialized asset storage. A pending step batch must be committed before its acknowledgement. If the current game/practice state prevents a legal drain, shutdown is refused rather than losing confirmed steps. Practice commands already commit and verify their own records; unresolved practice recovery also prevents shutdown.

Cancelling an asset request alone is not proof that its worker has stopped. The shutdown path also waits for the worker/cache barrier. The controller observes a ten-second preparation deadline between ticks; it cannot preempt a blocking NVS commit or SD `fsync`. A timed-out HTTP worker must still finish before resume. Timeouts and uncertain saves/storage operations keep the latch asserted; no task is killed and no forced power cut bypasses the barrier. Holding the button after a successful save has no release deadline. Serial input collected across a shutdown/standby transition is discarded so an old partial command cannot execute after wake.

Quiet standby stops new gameplay, motion sampling and downloads and pauses the radio. It **is not ESP deep sleep**, and it does not establish a measured battery-current target or switch off a physical LCD. Idle background tasks and peripheral supply may remain. Resume reanchors motion; steps during standby are not reconstructed. A true battery power-off needs PWR for the next start, while USB can continue supplying the system regardless of GPIO7.

## Electrical boundary

The [official schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.46/ESP32-S3-Touch-LCD-1.46.pdf), page 1, maps GPIO6 to `Key_BAT` and GPIO7 to `BAT_Control` (called `SYS_OUT`/`SYS_EN` in the [Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46/FAQ)). PWR/Key1 closes ground to the shared D1/D4 cathode junction. GPIO6 is diode-isolated button sensing, not a substitute for that unpowered startup contact. J9 does not expose this junction or GPIO6/7.

USB_5V supplies VCC through D3 independently of the battery latch. The schematic does not provide a verified ESP32 VBUS-sense input. Seeing the CPU still run after lowering GPIO7 therefore means **power remains, source unknown**; USB enumeration and battery ADC voltage are not treated as reliable USB-power detectors. The generic serial profile does not configure power GPIOs.

## Verification and physical checks

All three ESP profiles compiled with zero warnings. The power controller passed 16,686 host checks; actual runtime/console integration passed 95, GPIO HAL 193, network adapter 25, SD power handling 20, and asset client 94 plus its disabled gate. The portable runtime suite, save migrations and all nine native core regression tests passed. Address/undefined-behavior sanitizers covered the power/runtime/HAL/network/SD checks. These counts include repeated assertions and boundary sweeps, not that many distinct scenarios. Build measurements (historical local evidence omitted) · integration and adapter evidence (historical local evidence omitted) · save/runtime evidence (historical local evidence omitted) · core regressions (historical local evidence omitted).

Run `npm run demo:power` for the cancel, battery-cut, powered-standby/wake and save-failure demonstrations. Run `npm run test:power`, `npm run test:handheld`, `npm run test:handheld:transport`, and the three [documented ESP builds](ESP_BUILD.md). The transport and network host tests use the already-installed mbedTLS/cJSON development libraries; their scripts support path overrides and install nothing. No test here opens a serial device, flashes a board, buys parts, or changes a save schema.

Before physical use, verify the exact delivered board, PWR location/travel and protected case access around the battery. Bench checks must cover battery startup/release, accidental taps/bounce, a held boot button, countdown cancellation, orderly SD/NVS shutdown, failed-save behavior, powered USB survival/resume, USB removal in standby, and restored saves after restart. Measure supply decay, actual current and storage power-loss behavior. Source inspection and successful builds do not establish those electrical measurements.

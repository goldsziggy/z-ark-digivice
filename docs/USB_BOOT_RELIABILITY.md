# USB boot reliability — unresolved physical acceptance

> Publication checkpoint: source and previously installed firmware are f74ee4c (schema 17 / rules 13). Both units passed bounded installation/save/SD/reboot checks; older entries below are historical. No hardware was accessed for this export. See [verified images and remaining acceptance](../releases/firmware-f74ee4c/README.md).

Unit 2 stopped replying through USB during the later rules-12 preflight. This is an **open release reliability issue**, even though both units previously passed the `6e058e1` installation, migration, asset, reboot and idle checkpoints. Those results remain historical evidence. No current save corruption, application crash or radio failure has been established.

## What the existing traces establish

The failed Unit 2 attempt reports `USB_UART_CHIP_RESET`, normal `SPI_FAST_FLASH_BOOT`, a loaded application image, then the bootloader's RNG-disable message at 333 ms. A successful boot of the **same installed image** continues with PSRAM messages at 334 ms and calls `app_main` at 816 ms. One later bounded reopen produced no received bytes. Probing stopped.

The last printed line is not proof of where the processor stopped. Either early application/flash/PSRAM startup failed, or USB visibility was lost while the application continued. Similar silence is recorded before this release, so the new care/capture/idle changes are not an established cause. Screen idle only blanks the display/backlight; this build does not enter CPU light/deep sleep there.

The startup source audit found no demonstrated application deadlock: USB input installation and GPIO/I²C infrastructure initialization precede the first project banner; NVS, SD, display and IMU initialization follow. The pinned USB stdout implementation has a bounded no-progress wait, not an indefinite host-enumeration wait. This does not prove the app executed during the silent attempt. Sanitized evidence (historical local evidence omitted).

An [upstream issue report](https://github.com/espressif/esp-idf/issues/18996) describes similar USB silence while an S3 application remains running. It is a useful hypothesis, **not a confirmed diagnosis for these units or our IDF version**. No low-level peripheral-reset workaround has been imported.

## Host opening correction

The old installer cached `DTR=False`, `RTS=False` before opening. Installed pySerial 3.5 first opens the OS descriptor, then applies DTR and RTS separately. Its [official API documentation](https://pyserial.readthedocs.io/en/latest/pyserial_api.html#serial.Serial.open) warns that the OS/driver can assert these lines on open and cause a transition when requested states differ. Therefore “False/False” did not mean “no control writes” or prove a passive open.

The [installer](../scripts/install-sd-usb.py) now follows the ordering in [Espressif monitor 1.10.0](https://github.com/espressif/esp-idf-monitor/blob/v1.10.0/esp_idf_monitor/base/serial_reader.py#L101): cache both asserted, open once, release RTS before DTR. It retains Espressif's unchanged-DTR reapplication after RTS for the Windows driver workaround. On failure it closes best effort and preserves the original error; there is no retry, reopening or deliberate reset.

This is a correction of a demonstrated host-side risk. **It has not been physically tested and cannot guarantee reset-free OS open/close behavior.** pySerial itself also discards queued input during open; later captured bytes cannot prove no earlier output occurred. Firmware and installed app hashes are unchanged. The existing `6e058e1` installer ZIP is an immutable historical package and does not contain this later host correction.

## Next bounded diagnostic

1. Keep Unit 1 idle. Observe Unit 2 without changing USB/BOOT/PWR. Record whether the screen is displaying and responding; if it is blank at the known egg screen, one touch/release can distinguish ordinary backlight idle from a failed wake. Preserve the hatch choice.
2. If the screen responds, a live application is established. Use one exact-serial-selected open with the corrected ordering, record a bounded receive window, then at most one framed identity request on that same descriptor. Retain received bytes privately; stop on error, disconnect or timeout. Do not invoke esptool or automatically reopen.
3. If the screen does not respond, that still does not uniquely diagnose a startup failure. Record that observation before selecting a different diagnostic. Do not repeat the identical cable reconnect, erase, reflash or substitute saves merely to obtain another log.

A private Unit 2-only single-open script is prepared in `/tmp/digivice-unit2-single-open-diagnostic.py`. Its default mode is a local plan with zero USB access. Execution requires the live-screen observation; it allows one open, an 8-second receive window, a discard fence and one identity request with a 15-second response deadline. It limits all received data to 128 KiB, refuses existing evidence paths, checks the complete enumerated identity and expected application reply, and sends no game/save/firmware-write commands. It has **not been executed against a device**. A successful identity reply would not by itself verify the current save or close reliability acceptance.

## Verification boundary

Thirty installer tests pass with temporary files and fake serial. They cover the opening order, open/control-line/interruption faults, cleanup, framing, identity, transfer conflicts and bounded retries within an existing connection. A separate check runs the installed pySerial POSIX implementation with every descriptor/control operation mocked and confirms the emitted ordering; no device is opened. Run the portable suite with:

```sh
python3 -m unittest discover -s tests -p 'test_install_sd_usb.py' -v
```

During this investigation: **zero hardware opens, flashes, resets or game commands**; Unit 1 remained idle. Both installed apps remain `6e058e1`. Unit 2's current save has not been re-read. Physical recovery and repeatability remain unverified. Nearby's separate egg/owned-partner testing gate remains in effect; cloud pairing is not a prerequisite for ESP-NOW.

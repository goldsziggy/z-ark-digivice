# USB boot reliability — successful installation, unresolved intermittent silence

> Current exported source: `66ceaaf` (schema21/rules14, 60 Digimon). [Current build and migration guide](ROSTER60.md) · [Publication validation](../PUBLICATION_VALIDATION.json). Installation statements below describe their named historical checkpoints; this export preparation does not assert a new physical installation.

Both devices passed the **`8be26c6`** save-preserving installation and bounded
reboot checks on 2026-10-09. First boot was captured on the same descriptor as
each app-only flash, followed by exact own-save/settings, 261-file SD and reboot
verification. No silent connection, recovery flash or blind retry occurred in
this run. All serial handles are closed. Earlier intermittent USB silence
remains unexplained; this success does not establish long-term reliability.
Current sanitized installation evidence (historical local evidence omitted).

## Previous f9 installation evidence

At that earlier checkpoint, both installed applications matched the approved 1,584,592-byte f9 image. Each device preserved its own 640-byte care payload through schema17 → schema19 migration, matched durable settings across upgrade and reboot, and restored an identical 660-byte snapshot after verification reboot. Both SD packs passed all 261 file checks with no asset payload writes. Final serial connections were closed and the installation owner confirmed no remaining port owners.

Unit 1 initially returned zero USB bytes after the app-only write. A later attempt to download the full application for diagnosis timed out; its failure evidence was retained. A subsequent bounded device-checksum verification and boot capture on the same open descriptor succeeded, with no recovery firmware rewrite. The full post-installation check then passed. Success on a later attempt does not establish why the earlier connections were silent.

Unit 2 completed its app-only upgrade, protected-region readback, boot capture and exact own-save migration on one descriptor. An already saved capture result replay temporarily returned a read-only BUSY response during inspection. One eight-second wait and deferred diagnostic completed successfully; no new capture or gameplay command was sent. This explicit playback refusal is separate from receiving no USB bytes.

The private observers omit serial `flush()`/`tcdrain()` calls because those can outlive the intended diagnostic deadline. Requests retain bounded writes and reads; faults, transport timeouts and disconnects stop rather than automatically reopen. The successful checks do not establish repeated-open reliability, physical audio, radio/trade acceptance or long-term stability.

## Earlier traces retained

Unit 2 also stopped replying during the earlier rules-12 preflight, after both units had passed the historical `6e058e1` installation, migration, asset, reboot and idle checkpoints. Those earlier passes and failures remain evidence; `6e058e1` is not the current installed version.

The failed Unit 2 attempt reports `USB_UART_CHIP_RESET`, normal `SPI_FAST_FLASH_BOOT`, a loaded application image, then the bootloader's RNG-disable message at 333 ms. A successful boot of the **same installed image** continues with PSRAM messages at 334 ms and calls `app_main` at 816 ms. One later bounded reopen produced no received bytes. Probing stopped.

The last printed line is not proof of where the processor stopped. Either early application/flash/PSRAM startup failed, or USB visibility was lost while the application continued. Similar silence is recorded before this release, so the new care/capture/idle changes are not an established cause. Screen idle only blanks the display/backlight; this build does not enter CPU light/deep sleep there.

The startup source audit found no demonstrated application deadlock: USB input installation and GPIO/I²C infrastructure initialization precede the first project banner; NVS, SD, display and IMU initialization follow. The pinned USB stdout implementation has a bounded no-progress wait, not an indefinite host-enumeration wait. This does not prove the app executed during the silent attempt. Sanitized evidence (historical local evidence omitted).

An [upstream issue report](https://github.com/espressif/esp-idf/issues/18996) describes similar USB silence while an S3 application remains running. It is a useful hypothesis, **not a confirmed diagnosis for these units or our IDF version**. No low-level peripheral-reset workaround has been imported.

## Host opening correction

The old installer cached `DTR=False`, `RTS=False` before opening. Installed pySerial 3.5 first opens the OS descriptor, then applies DTR and RTS separately. Its [official API documentation](https://pyserial.readthedocs.io/en/latest/pyserial_api.html#serial.Serial.open) warns that the OS/driver can assert these lines on open and cause a transition when requested states differ. Therefore “False/False” did not mean “no control writes” or prove a passive open.

The [installer](../scripts/install-sd-usb.py) now follows the ordering in [Espressif monitor 1.10.0](https://github.com/espressif/esp-idf-monitor/blob/v1.10.0/esp_idf_monitor/base/serial_reader.py#L101): cache both asserted, open once, release RTS before DTR. It retains Espressif's unchanged-DTR reapplication after RTS for the Windows driver workaround. On failure it closes best effort and preserves the original error; there is no retry, reopening or deliberate reset.

This corrects a demonstrated host-side risk. The ordering has now been exercised during the physical f9 checks, but **cannot guarantee reset-free OS open/close behavior**. pySerial itself also discards queued input during open; later captured bytes cannot prove no earlier output occurred. The host correction did not change firmware by itself. The existing `6e058e1` installer ZIP is an immutable historical package and does not contain this later host correction.

## If silence recurs

1. Observe the affected screen before another USB probe. Record whether it displays and responds; one touch/release can distinguish normal backlight idle from a failed wake. Keep the other unit idle and preserve each device's own save.
2. Use a separately reviewed, bounded diagnostic selected by the complete device identity. Keep identity, receive capture and any subsequent inspection on the same open descriptor where practical. Preserve each failed attempt in a separate private directory; stop on faults, disconnects or timeouts.
3. A silent connection or nonresponsive screen does not uniquely diagnose startup failure. Establish the next diagnostic from the evidence. Do not erase, substitute saves, roll back newer schemas or repeatedly reflash merely to obtain another log.

A successful identity reply alone does not verify save preservation or close reliability acceptance. The current checkpoint additionally verified the app, each own-save migration, durable settings, SD contents and reboot restoration. No further hardware diagnostic is pending for this installation.

## Verification boundary

Thirty installer tests pass with temporary files and fake serial. They cover the opening order, open/control-line/interruption faults, cleanup, framing, identity, transfer conflicts and bounded retries within an existing connection. A separate check runs the installed pySerial POSIX implementation with every descriptor/control operation mocked and confirms the emitted ordering; no device is opened. Run the portable suite with:

```sh
python3 -m unittest discover -s tests -p 'test_install_sd_usb.py' -v
```

The original host-opening investigation used no hardware and remains historical. The later f9 installation used authorized app-only writes, ROM/reset transitions, captured boots and software verification reboots; it performed no factory reset, NVS erase, save replacement, trade or new capture. Audio initialization and unchanged defaults passed, but changed-preference durability and acoustic behavior remain untested. The measured main-stack minimum free values were 8,944 / 8,848 bytes for units 1 / 2 during the bounded checks, not a trading-load or long-duration guarantee. Nearby's separate physical acceptance gates remain in effect; cloud pairing is not a prerequisite for ESP-NOW.

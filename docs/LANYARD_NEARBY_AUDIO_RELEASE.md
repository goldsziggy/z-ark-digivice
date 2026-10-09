# Lanyard steps, Nearby mode selection and sound

Clean source **`8be26c6607d1b670ee088958c97f4940a527b864`** is built, packaged and
installed on both playtest devices. The save-preserving app-only installation on
2026-10-09 retained each own complete 660-byte schema19/rules13 save, durable
settings and all 261 SD files; both passed verification reboot. All serial
handles are closed and both can be unplugged. No profile reset, gameplay,
preference change or public Pages update occurred.
Build and test evidence (historical local evidence omitted) ·
Sanitized physical installation evidence (historical local evidence omitted).

## Motion recovery and forgiving steps

The QMI8658 feeds a software acceleration detector. Its hardware pedometer
remains disabled; cosmetic gyro tilt and its stillness calibration are separate.
After three consecutive sensor read errors, the worker now retries identity,
configuration and register readback with 1, 2, 4 and then 8-second delays. The
delay stays capped at eight seconds. A fresh coherent sample is required before
the sensor is ready again. Transient configuration failures after an I2C handle
exists use the same worker; unsupported boards, missing buses and allocation
failures remain unavailable. The worker respects power/USB suspension and only
acknowledges quiescence after sensor shutdown is acknowledged.

Recovery retains the accepted counter and clears only unfinished stride
detection. It does not estimate steps during an unavailable interval. Home
keeps the lifetime total visible and separately indicates recovery or unavailable
sampling. A confirmed delta first observed during a fault can remain lifetime
only; recovery does not replay it as encounter credit.

The detector now accepts two plausible completed pulses rather than requiring
three evenly spaced ones. It credits that first pair together, then subsequent
pulses individually. A quiet baseline can arm the first pulse after a stop.

| Detector parameter | Current value |
| --- | --- |
| Configured sensor rate / worker delay | 62.5 Hz / 20 ms after work; at most about 50 worker samples/s |
| Gravity baseline / signal smoothing | 1,200 ms / 40 ms |
| Warm-up / quiet arming | 600 ms / 100 ms |
| Rise / return / valley | +0.045 g / +0.015 g / −0.020 g after filtering |
| Peak spacing / pulse width | 280–1,800 ms / 40–800 ms |
| Missing-sample continuity limit | 200 ms |

Generated three-axis lanyard, gentle/irregular walking, short starts, handling
and gesture traces exercise this policy. They are simulations, not recordings
or measured walking accuracy. Two rhythmic handling bumps can now count where the stricter policy rejected
them. Swing-only lanyard motion can count in both versions. These are explicit
tradeoffs for favoring plausible steps. A single isolated bump, rapid vibration, invalid
samples and large shocks still have rejection coverage. Modulated rapid shaking
can alias into accepted slower peaks; rejecting uniform vibration is not general
shake rejection. Carry position and
children's gait require counted physical trials; magnitude invariance alone
does not establish accuracy.

The same generated inputs were run against frozen `bad89ae` (the f9 detector)
and this update with ASan/UBSan. Counts below are detector outputs, not accuracy
percentages or physical trials:

| Synthetic input | Imposed cycles | f9 count | Updated count |
| --- | ---: | ---: | ---: |
| Regular walking | 24 | 23 | 24 |
| Slow, 1,600 or 1,800 ms spacing | 24 | 0 | 24 |
| Irregular cadence | 24 | 21 | 24 |
| Six two-cycle starts | 12 | 0 | 12 |
| Gentle 0.065 g motion | 24 | 0 | 24 |
| Lanyard gravity, swing and step motion | 24 | 23 | 24 |
| Swing-only handling, no walking | 24 swings | 24 | 25 |
| Two handling bumps, no walking | 2 bumps | 0 | 2 |
| Damped capture-like ringing, no walking | — | 3 | 4 |
| Alternating-strength rapid shake | 40 | 19 | 20 |

Single isolated impulses and uniform 180 ms shaking counted zero in both
versions. Six isolated one-cycle starts also counted zero; this is still a
two-pulse estimate. The 0.040 g and 2,000 ms traces remained uncounted. The full
22-trace comparison includes these limits rather than reporting only successes.

Lifetime usage remains separate from encounter progress. Normal operation saves
dirty lifetime counts every 64 steps or 30 seconds, with forced orderly drains.
These are batching targets, not hard power-loss bounds. Fresh post-starter steps
can advance encounters in menus and battles; one waiting encounter is retained,
and it appears only when Home is quiet. Off stops new encounter progress without
stopping lifetime counting. Screen idle keeps sampling active. Recovery, reboot
and packet retries must neither duplicate accepted counts nor reroll a waiting
encounter.

## Nearby battle choice

Select a nearby trainer, choose **Tactical** or **Auto** on the invitation review,
then send the invitation. The choice is local to Nearby and does not change the
saved wild-battle mode. Both the outgoing invitation and the recipient's accept
screen show the offered mode. Sending and accepting recheck the reviewed peer,
open nonce/session, fighter profiles and mode; a changed offer requires review.

Auto already uses seeded random Physical/Magic attacks with equal odds, random
existing defenses and alternating attackers. The recipient verifies the exact
next result. Retries resend the committed exchange, and the host waits for the
previous state to be acknowledged before the next paced turn. No PvP capture,
care damage, ownership change or progression reward is introduced.

The DGN1 protocol stays at rules12/catalog6 with a 116-byte match and a maximum
154-byte packet. Existing compatible f9 peers retain that wire contract. A brief
disconnect may resume the same live session; a 30-second timeout, exit or reboot
ends it. Duels are not saved into player profiles. New sessions require consent.
See [the complete protocol and security limits](NEARBY_PROTOCOL.md).

## Sound correction and controls

The earlier 50% maximum was a software limit. Sound now offers
**0 / 5 / 15 / 30 / 50 / 75 / 100%**, with the existing lower-level gains retained.
Mute and music remain independent. Defaults remain 15%, unmuted and music off.
Effects do not require music to be on. Sample generation remains bounded within
signed 16-bit output; digital headroom does not measure acoustic loudness.

The old output was 22,050 Hz, 16-bit stereo with no external master clock. That
produces a 705,600 Hz bit clock outside the DAC's documented BCLK-derived PLL
configurations. The output now uses 44,100 Hz and a 1,411,200 Hz bit clock,
matching [Waveshare's actual audio initialization](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.46/blob/fda89ff2ff32eb1bce561901225c6a36fa684237/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/main/Audio_Driver/PCM5101.c)
and the [TI PCM5101A clock specification](https://www.ti.com/lit/ds/symlink/pcm5101a.pdf).
This fixes a clock compatibility issue; it is not proof of the reported silence's
physical cause. Synthesis timing follows the new sample rate, preserving cue
durations and pitch.

The [board schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.46/ESP32-S3-Touch-LCD-1.46.pdf)
connects DIN/LRCK/BCLK to GPIO47/38/48. DAC SCK and FMT are grounded, XSMT is tied
high, and the NS8002 amplifier shutdown input is grounded. No missing software
amplifier-enable GPIO was found. The fitted speaker and physical R57 gain control
remain part of the hardware path.

The existing 16-byte audio record remains compatible going forward: saved 0–50%
values retain their meaning, and 100% persists through a simulated reboot.
Downgrading to f9 after saving a value above 50% causes that older audio reader to
reject the preference record; it does not justify erasing any device storage.
Care saves are unchanged. Music/FX, mute, level bounds, driver failures and
orientation-adjusted touch controls are covered separately from audible output.

The audio DMA allocation stays three buffers of 256 stereo frames. At 44.1 kHz
this holds about 17.4 ms; the synthesis sample workload doubles. Target CPU load,
speaker output and battery impact still need physical measurements.

## Build and verification

The existing official ESP-IDF 5.3.6 toolchain built the `waveshare` profile with
**zero compiler warnings**. The image descriptor is `8be26c6`; esptool verified
its checksum and validation hash without opening a device. Build with:

```sh
bash scripts/build-esp.sh waveshare
```

| Measured build output | Bytes | Change from installed f9 |
| --- | ---: | ---: |
| Application binary | 1,587,744 | +3,152 |
| Free in 3 MiB OTA slot | 1,557,984 | −3,152 |
| Static DIRAM | 178,671 | +80 |
| Linker-unassigned DIRAM | 163,089 | −80 |

The bootloader, partition table and initial OTA-data binaries are byte-identical
to the f9 build. The configured main stack remains 16 KiB. Linker-unassigned
memory is not a runtime heap measurement. The dedicated 16 KiB IRAM region is
almost full, with additional instructions placed in DIRAM by the existing linker
layout; this does not establish runtime stack or timing margin.

All 27 CTest targets passed, followed by the two affected UI/setup targets after
the final selection-marker correction. Focused ASan/UBSan harnesses passed for
motion recovery, 22 generated detector traces, Nearby packet faults/consent,
actual walking/Nearby runtime bodies and audio synthesis/persistence/worker
faults. Idle and USB/power integration also passed. The independent recovery
probe retained lifetime usage, the exact waiting encounter and care snapshot
across repeated simulated outages. Test counts and source pins are in the linked
evidence; loop iterations are checks, not distinct test scenarios.

The local package is
`../deliverables/lanyard-nearby-auto-20261009/firmware-8be26c6.zip` relative to the
repository root. It contains four build images, a hash manifest and a README;
it contains no private assets, saves or credentials and is not an installer.
Application SHA-256:
`390b9fe7f5547df4dbb5bb75385b8cb0989144d20c558503847625c802b7ec8d`.

## Next physical acceptance

The app-only installation and bounded device diagnostics passed; these do not
establish walking accuracy, audible output or Nearby RF behavior. Main-stack
minimum free values during the bounded post-reboot checks were 8,944 / 8,848
bytes on units 1 / 2, not a sustained-load guarantee.

Compare lifetime deltas against counted walks at slow/normal/brisk paces,
with hand, pocket and neck-lanyard carry; repeat short stop/start walks and
stationary menu/handling checks. Record signed count error and false steps per
minute separately rather than claiming accuracy from encounter frequency.

Saved audio preferences were retained: Unit 1 is unmuted at 50% with music on;
Unit 2 is unmuted at 15% with music off. Both reported `sound=ESP_OK` at boot and
`ready=1 error=ESP_OK` afterward. No supported diagnostic exposes the clock rate
or maximum UI level: those are established by the exact installed image and
source/host tests, not an electrical measurement or a physical 100% selection.

Start a listening check at a comfortable low volume, test effects with music off, then
test music explicitly enabled. Check the physical gain control and driver
diagnostics if silent. Finally exercise both trainers' Tactical/Auto invitations,
alternating turns, dropped connection, fresh-session consent and preserved care.
These physical checks have not been performed by the host test suite.

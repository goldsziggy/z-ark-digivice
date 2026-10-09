# Sound, trading and manual Auto capture

This source update adds saved native volume/mute, opt-in original music,
Nearby partner trading, and a manual capture pause during wild Auto battles.
It is compiled for the Waveshare 1.46 target, tested on the host, and installed
as `f9d8723` on both playtest devices. Both passed save-preserving installation
and bounded reboot verification on 2026-10-09. The user reported “Both wake.”
Sanitized physical evidence (historical local evidence omitted).

## Player flows

- **Settings → Sound:** choose 0/5/15/30/50% volume, independent mute and
  optional music. Defaults are 15%, unmuted, music off. Failed persistence is
  visible. Music quiets for idle and yields to effects.
- **Nearby → peer → Trade:** select a partner, review both offers and their
  stats, then confirm. Both players must agree to the exact exchange. A spare
  playable partner is required. An interrupted prepared exchange locks
  foreground play and asks for the same peer; walking continues.
- **Wild Auto:** confirm the fight, watch the committed attacks, then flick
  the orb when the creature is capture-eligible. A tap does not throw. Resume
  fighting skips capture for the remainder of that encounter. Capture odds,
  three throws, and result animations retain their existing rules.

The device owns offline care and battle decisions. The service continues to
validate browser event histories using the same C++ core. It does not import
arbitrary snapshots or accept native trade receipts. Native cloud enrollment,
outbox and save synchronization remain future work. No network or AI call is
required during a native battle.

## Persistence and resource bounds

Care schema19 retains rules13 and uses 660-byte snapshots. Migration preserves
the preceding fields; missing `receivedTrades` and `autoCapture` default to 0.
Historical Auto commands keep their original deterministic resolver. Current
UI uses the additive `auto-fight`, `flick` and `auto-resume` events.

| Resource | Current measurement or configured bound |
| --- | --- |
| ESP application image | 1,584,592 bytes; 34,080 more than prior f74 |
| OTA slot | 3,145,728 bytes; 1,561,136 bytes remaining |
| Static DIRAM | 178,591 bytes; 11,744 more than prior f74 |
| Linker remaining DIRAM | 163,169 bytes; not runtime free heap |
| Main / existing audio task stacks | 16,384 / 4,096 configured bytes |
| Main-stack minimum free, bounded device checks | Unit 1: 8,944 bytes; unit 2: 8,848 bytes |
| Host game State / snapshot | 632 / 660 bytes |
| Trade Session / protocol objects | 8,760 / 2,944 host bytes in static owners |
| Mirrored trade journal payload | 2 × 1,512 bytes, plus NVS overhead |
| Trade transaction packet | 172 bytes within the 240-byte transport cap |
| Audio settings record | 16 bytes, separate from care saves |
| Framebuffer | 339,488 bytes in PSRAM, unchanged |

Host object sizes depend on the ABI. The measured main-stack margin covers only
the captured boot and diagnostic workload, not extended trading or audio use.
CPU cost, speaker output and battery current have not been measured for this update.
The music uses the existing audio task and buffers; it adds no asset stream,
network request or task. The trade journal assumes honest compatible peers
and retained NVS. It does not protect against malicious firmware or complete
NVS rollback. A missing peer can leave a prepared exchange awaiting recovery.

## Run and verify

```sh
npm run build
npm start
# Open http://127.0.0.1:8787 and pair a virtual device.
ctest --test-dir build --output-on-failure
npm run test:modules
npm run test:browser:auto-capture
bash firmware/tests/run.sh
bash scripts/build-esp.sh waveshare
```

The ESP wrapper uses the existing official ESP-IDF5.3.6 installation and only
builds. Do not use an older install helper's source pin to install this update.
Current evidence includes 27 native CTest targets, 313 Node tests, sanitizer
tests for audio/trade/runtime, 512 frozen historical Auto replay comparisons,
and Chromium checks for manual flicks, retries, reload and capture playback.
The native renderer preview covers 74 states; it is not a physical panel test.

## Physical installation checkpoint

Both devices run the exact 1,584,592-byte application with SHA256
`9c16408faa9ff86aeb66dc2311e13435846c406cb2ced2c8b9e0205aea94f34e`.
Each app-only write preserved the protected flash region before first boot.
Each device's own schema17 652-byte snapshot migrated to schema19 660 bytes
with all 640 existing payload bytes unchanged. The migrated snapshot remained
byte-identical after verification reboot. No factory reset, save replacement,
cloning, trade or new capture was performed; NVS was not erased.

Idle, network and practice settings matched across upgrade and reboot;
lifetime steps did not decrease. Each device verified 261 SD files totaling
4,700,573 bytes, with zero payload writes and its inspection lease released.
Audio initialized healthy with the default 15% volume, unmuted, music off,
and the same values after reboot. No preference was changed: this does not
test durability of an audio preference write or actual speaker output.

The initial unit-1 USB silence and a later full-application read timeout remain
recorded. A subsequent device checksum and boot capture on one connection
succeeded without rewriting firmware for recovery. Unit 2 replayed an already
saved capture result and temporarily refused a read-only diagnostic; one
eight-second deferral completed the check without another capture or game
command. [USB investigation and remaining limits](USB_BOOT_RELIABILITY.md).

All final inspection connections were closed. The next acceptance pass needs
physical sound, changed audio preference persistence, real touch flicks, and
a two-device trade with disconnect/reboot recovery. Long-term USB reliability,
RF behavior and battery performance remain unproven. No factory reset is
needed for these playtests.

Detailed contracts: [audio](FIRMWARE_AUDIO_SETTINGS.md),
[trade recovery](NEARBY_TRADING.md), [Auto capture](AUTO_CAPTURE.md),
[service API](../service/API.md).

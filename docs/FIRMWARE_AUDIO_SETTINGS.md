# Firmware sound preferences and music

This document describes installed **`8be26c6`** on both playtest devices. The
save-preserving update retained each device's volume, mute and music preferences
through reboot; audio initialization reported `ESP_OK`. No setting was changed
by the installer. Installation evidence (historical local evidence omitted) ·
[Current release and physical-test limits](LANYARD_NEARBY_AUDIO_RELEASE.md).
Host tests exercise the real audio worker against deterministic NVS/RTOS/I2S
doubles. Physical loudness, speaker quality, audio-task stack high-water mark and
power consumption remain unmeasured.

## Controls and persistence

The default remains **15% volume, sound on, background music off**. The maximum is
now 100%, retaining the existing gain scale for 0–50%. The UI offers **0%, 5%, 15%,
30%, 50%, 75% and 100%**; mute is separate from volume so unmuting restores the
chosen level. Music is an independent opt-in preference. Volume zero silences
both music and effects; effects do not require music to be enabled.

`digivice::device::Audio` exposes:

| API | Behavior |
| --- | --- |
| `setVolume(uint8_t)` | Clamp to 0–100; apply in RAM; save preferences; return `esp_err_t` |
| `setMuted(bool)` | Immediately silence/resume permitted audio in RAM; save; return status |
| `setMusicEnabled(bool)` | Enable/disable the background loop; save; return status |
| `volume()`, `muted()`, `musicEnabled()` | Current RAM preferences |
| `preferencesWritable()`, `preferencesError()` | Whether settings can be saved and the last persistence result |
| `setMusicScene(MusicScene)` | Transient `Home`, `Battle` or `Quiet`; never writes NVS |
| `effectsQuiescent()` | No queued/active effects or effect DMA tail; background music does not block idle policy |
| `pause(bool)`, `quiescent()` | Existing power/USB pause contract; full I2S shutdown acknowledgement remains required |

Preferences live in the separate NVS namespace `digi_audio`, key `config`.
The 16-byte record contains `DAUD`, little-endian version 1, volume, strict boolean
mute/music flags, one reserved zero byte, and CRC32. No game save or identity is
modified. The save owner initializes shared NVS; this module never initializes,
erases, reformats, or migrates it. Missing settings use defaults without a write.
Repeated unchanged settings also do not write.

The record format remains version 1. Existing 0–50% records retain their meaning;
100% survives a simulated reboot in the host persistence test. Older f9 firmware
rejects a record with volume above 50%, so a downgrade can block audio-preference
writes for that boot. This does not justify erasing storage or changing care saves.

A successful change requires set, commit, and exact readback. Malformed, unknown,
oversized, unreadable or uncertain records block subsequent writes for that boot;
they are not overwritten. A failed write leaves the last verified stored settings
unchanged in the persistence object, although an acknowledged-lost commit may
have physically landed. A later boot validates what NVS actually contains.

**Controls still apply in RAM when saving fails**, especially mute. The caller
must show an unsaved-settings warning when a setter fails. This deliberately
separates immediate sound control from durable success. The audio driver's own
`lastError()` continues to describe I2S/driver errors rather than NVS errors.

## Original bounded synthesis

`firmware/runtime/music_synth.cpp` contains two original eight-step motifs composed
for this change. Home uses MIDI pitches `72,76,79,74,76,72,67,71` at 600 ms per step;
Battle uses `69,72,76,74,71,74,79,76` at 360 ms per step. Each has a sine lead and
sparse triangle bass. There are no samples, downloaded assets, copied franchise
melodies, external generators, network requests or new dependencies.

The existing one audio worker and four-request effect queue remain. The music
synth uses two fixed oscillator voices beside the existing bounded six-voice cue
synth; it creates no additional tasks, streams or dynamic allocations. Music
ducks to 22% of its normal gain during effects. Music on/off, ducking and scene
changes ramp over at most 40 ms; the shared master gain ramps over at most 20 ms.
Scenes fade out before changing tempo/notes. SFX priority and repeat cooldowns are
retained; queued requests older than 350 ms are dropped.

The selected board's output is now **44,100 Hz, stereo signed 16-bit I2S**, with
mono duplicated into both channels. Pins are unchanged. With no external master
clock, the resulting 1,411,200 Hz bit clock matches the DAC clock contract; see
the [vendor and schematic review](LANYARD_NEARBY_AUDIO_RELEASE.md#sound-correction-and-controls).
The mixer has a mathematical bound below **23,200 PCM magnitude** at maximum
volume, plus an explicit final int16 clamp. This digital bound is not an SPL
measurement or proof that the reported physical silence has been resolved.

`Quiet` fades music while allowing brief UI effects. Runtime selects it while
the screen is blank or the interface is paused, and selects Home/Battle when
active. Screen-idle eligibility uses `effectsQuiescent()` so music cannot keep
the screen awake. Full power/USB suspension still calls `pause(true)` and waits
for `quiescent()`; pausing cancels both audio types. Resuming starts a fresh loop,
without replaying missed music or old effects. Every DMA descriptor is cleared
before I2S restarts. If I2S disable fails, full quiescence remains false.

## Evidence and resource budget

Run:

```sh
bash firmware/tests/test_audio_settings.sh
bash firmware/tests/test_audio_worker.sh
```

Both pass with AddressSanitizer and UndefinedBehaviorSanitizer. Coverage includes
all preference combinations for integer volumes 0–100 and effect/music output at
the UI's seven levels, single-bit record corruption,
invalid sizes, missing records, unchanged-value writes, open/write/commit/readback
faults, lost commit acknowledgement, scene changes, volume clamp, mute, music
ducking, original cue mixing, bounded PCM, pause/resume, queued outcomes and I2S
write/disable/task-creation failure. The worker test compiles the actual
`device_audio.cpp` without replacing its logic.

Historical `sizeof` values from the original sound-preferences Mac host build:

| Object | Bytes |
| --- | ---: |
| Preference record | 16 |
| NVS settings owner | 16 |
| Music synth | 36 |
| Combined effect/music mixer | 196 |
| Audio owner | 224 |

These are host ABI measurements, not ESP runtime heap measurements. Configured
budgets remain one 4,096-byte task stack (containing the 1,024-byte stereo scratch
buffer and mixer), three 1,024-byte DMA audio payloads, and four fixed queue
records. The same DMA payloads cover about 17.4 ms at 44,100 Hz. Queue/driver/RTOS
bookkeeping is additional and SDK-dependent. The original mixer added 40 host
bytes over the earlier cue synth. The sample workload doubles from the previous
22,050 Hz version; no target CPU/current or physical sound measurement is claimed.
Original sound-preferences evidence (historical local evidence omitted)
retains its historical source hashes and test results; it does not pin the new
100% / 44,100 Hz release.

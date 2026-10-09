# Touch game sound cues

The browser simulator uses original oscillator effects in `web/audio-engine.js`.
No downloaded recordings, franchise samples, external generation service, or new
audio dependency is involved. Sound remains muted by default, music is separately
opt-in, and the existing 0–100% master volume and trusted-input unlock apply.

## Event contract

Call `audio.playCue(name)` once for the matching presentation event. It returns
`true` if notes were scheduled and `false` if muted, unavailable, hidden, throttled,
or superseded by a higher-priority sound. A `false` result must never block input,
gameplay, persistence, or a network request. Do not retry or queue skipped sounds.

| Cue | Event | Sound |
| --- | --- | --- |
| `menu-confirm` | Accept a menu choice | Short rising pair |
| `menu-back` | Navigate back / dismiss | Short falling pair |
| `attack-physical` | Physical attack animation | Brief square-wave sweep |
| `attack-magic` | Magic attack animation | Longer rising sine shimmer |
| `hit` | Confirmed damage impact | Low downward impact |
| `crit` | Explicit critical-hit event, if one exists | Sharp three-part impact |
| `capture-arm` | Explicit capture windup begins | Quiet rising pair |
| `capture-throw` | Capture is committed | Rising throw sweep and short fall |
| `capture-wiggle` | Capture-resolution anticipation | Small pitch wobble |
| `capture-success` | Authoritative captured outcome | Bright resolving fanfare |
| `capture-fail` | Authoritative failed-capture outcome | Gentle downward motif |
| `evolution` | Confirmed evolution | Six-note ascending fanfare |

The core currently has no critical-hit flag. `crit` is available for the sound
demo and future explicit critical events; do not infer a critical hit from a heavy
attack or unusually high damage. Capture presentation must use the accepted core
result for success/failure. A canceled, unsent capture has no result cue.

Existing `feed`, `play`, `rest`, `steps`, `encounter`, `card`, `win`, and `retreat`
names remain available. Older call sites retain these aliases:

| Alias | Canonical cue |
| --- | --- |
| `select` | `menu-confirm` |
| `attack` | `attack-physical` |
| `hurt` | `hit` |
| `capture-start` | `capture-arm` |
| `evolve` | `evolution` |

`AUDIO_CUES` exports the frozen list of accepted names for demonstrations/tests.
The existing `AudioEngine` methods and preference storage schema are unchanged.

## Overlap and resource limits

- At most **12 active or scheduled oscillator voices** and **two effect groups**.
  A new cue replaces unfinished notes in its own category. If a third category
  arrives, the oldest eligible group yields. Notes are never queued for later.
- Priority order is **outcome > capture phase > battle/card > menu/care > music**.
  A lower-priority cue is skipped while a higher-priority effect remains active.
  Capture phases and outcomes replace other effects. Outcomes therefore replace
  an unfinished throw/wiggle and are not interrupted by incidental menu taps.
- Repeat cooldowns apply to each canonical cue: menu **80 ms**, care/capture
  **120 ms**, battle **90 ms**, outcome **400 ms**. Aliases share the same cooldown.
- Accompaniment ducks to **22%** of its normal gain during effects, returning
  after the latest effect plus 80 ms. Music also yields oscillator slots when an
  effect needs them. Muting clears voices, the scheduler and duck automation.
- Notes retain the **1.2 second** lifetime cap and gain ceiling of **0.065** before
  master volume. The longest effect, evolution, lasts approximately **1.18 s**.
  Square-wave effects use lower gains than sine/triangle effects.
- A repeating **0, +3, −3, +1.5 cent** detune cycle adds slight variation to accepted
  effects. It uses no random generator, game state, network data, or extra voices.
- Visibility, stop, destroy and asynchronous suspension protections remain in
  place. No hidden-page catch-up effects are replayed.

## Verification and offline rendering

Run `./scripts/node.sh --test tests/audio-engine.test.mjs`. The 22 focused tests
cover synthesis bounds, distinct cues, alias compatibility, rapid-input throttling,
priority replacement, effect-group/voice limits, music ducking, mute, activation,
visibility and asynchronous lifecycle races. These are scheduling tests, not a
claim about a physical speaker's loudness or quality.

`tests/browser-audio.mjs` adds a real headless Chromium check using the existing
Playwright installation and isolated local service. Set `PLAYWRIGHT_MODULE` and,
if necessary, `PLAYWRIGHT_CHROMIUM` to installed paths, then run:

```sh
./scripts/node.sh tests/browser-audio.mjs
```

The passing run is recorded in `docs/evidence/audio-render.json`. It rendered all
**20 canonical cues** at **44.1 kHz mono**, with distinct PCM fingerprints, finite
nonzero samples, exact mute/zero-volume silence, and linear master-volume scaling.
At default 35% volume the largest measured individual-cue peak was **0.03859**
against a clipping threshold of 1. This is digital amplitude, not measured SPL.

The harness also creates a real `AudioContext`: a synthetic click is rejected, a
trusted touchscreen tap unlocks it, its audio clock advances and an analyser reads
nonzero samples. Touch mute suspends it and clears voices/music. An explicitly
injected visibility signal verifies real context suspension/resumption; native
background-tab event delivery is not claimed. Chromium uses `--mute-audio`, so
these checks do not take over the Mac's foreground audio.

The demo defaults to `../deliverables/audio-demo/` outside the repository: one
16-bit WAV per cue, a **32.85 second** concatenated `digivice-sound-cues.wav`, and
`cue-manifest.json` with hashes and offsets. Set `AUDIO_DEMO_DIR` to change that
destination. No human listening or physical speaker test is represented by this
evidence. Real rendering also caught and corrected an initial gain-automation
issue that unit mocks could not model; initial master gain now remains silent
even when time-zero automation is replaced.

An explicit test/demo harness can render the actual synthesis with the existing
constructor injection. This facade is necessary because `OfflineAudioContext`
starts rendering through `startRendering()`, rather than a real-time resume.
It does not change or bypass the production browser gesture policy.

```js
import { AudioEngine } from './audio-engine.js';

const offline = new OfflineAudioContext(1, 48000 * 2, 48000);
const facade = {
  get currentTime() { return offline.currentTime; },
  state: 'running',
  destination: offline.destination,
  createGain: () => offline.createGain(),
  createOscillator: () => offline.createOscillator(),
  resume: async () => {}, suspend: async () => {}, close: async () => {},
};
const engine = new AudioEngine({
  contextFactory: () => facade,
  userGesture: () => true, // Explicit offline harness only.
  storage: null,
  document: { hidden: false, addEventListener() {}, removeEventListener() {} },
});
await engine.unlock();
engine.setMuted(false);
engine.playCue('capture-success');
const buffer = await offline.startRendering();
engine.destroy();
// buffer.getChannelData(0) contains rendered PCM for analysis or WAV encoding.
```

Render separate cues in separate contexts, or advance an offline context's audio
clock between sequence events. Scheduling an entire sequence at time zero would
correctly trigger the overlap policy and replace earlier phases.

## Device boundary

These changes are browser audio. The selected Waveshare ESP32-S3-Touch-LCD-1.46
has a PCM5101 audio DAC, NS8002 amplifier and speaker connection; its firmware
audio output driver still needs implementation and bench verification. The older
2.1-inch buzzer discussion in `AUDIO.md` does not describe the selected 1.46-inch
board. No firmware audio, gyro, touch or haptic driver is added here, and no device
is flashed. Consult the current hardware audit for exact audio pins and haptic
availability before implementing an output driver.

The actual app integration also passes in `tests/browser-audio-integration.mjs`
(`npm run test:browser:audio-integration`). Trusted Settings/Sound touches enable
audio, change volume, toggle music, mute/suspend and re-enable it. A real
touchscreen flick schedules arm, throw, impact, wiggle and the native-confirmed
result through the app, with exactly one game command. Menu confirmation and Back
also schedule their cues. Source-pinned app evidence (historical local evidence omitted)
records real analyser output; headless OS output remains muted.

# Simulator audio

`web/audio-engine.js` synthesizes original sound effects and short background motifs with the browser's Web Audio API. It uses sine, triangle, and restrained square-wave oscillators with attack/release envelopes. It downloads no audio files, uses no external sample library, and contains no franchise recordings or copied game melodies.

## Controls and playback policy

Fresh preferences are **sound muted**, **music off**, and **35% master volume**. Preferences are stored locally under `digivice.audio.v1`. Storage denial, malformed data, and unknown preference versions fall back safely; they do not prevent gameplay.

Every page load starts locked, including when the saved preferences enable sound or music. The engine constructs no audio context and starts no scheduler until `unlock(event)` is called from a user input handler. Unlock does not override mute. A sound-enable control should await unlock, then unmute:

```js
import { AudioEngine } from './audio-engine.js';

const audio = new AudioEngine();

soundButton.addEventListener('click', async event => {
  if (await audio.unlock(event)) {
    audio.setMuted(false);
    audio.playCue('select');
  }
});

musicToggle.addEventListener('change', event => {
  audio.setMusicEnabled(event.target.checked);
});
volumeInput.addEventListener('input', event => {
  audio.setVolume(Number(event.target.value)); // Range 0..1.
});
```

Call unlock before awaiting network or game work. It checks transient browser user activation; older browsers without that API require a trusted click, pointer-up, touch-end, or key-down event. Synthetic events do not enable sound. The method returns `false` when audio is unavailable, input is not accepted, the page is hidden, or the context cannot resume. A later user gesture can retry.

Backgrounding the page cancels active and scheduled notes, clears the music timer, and suspends the context. Returning restores only audio already enabled by the user on this page. Muted, never-unlocked, and manually stopped playback stays silent. Pending suspend/resume operations are reconciled so a late browser callback cannot revive hidden audio or strand visible music in a suspended context.

## Public API

| Method | Behavior |
| --- | --- |
| `unlock(event?)` | Async user-gesture activation; returns success without changing saved mute/music preferences |
| `setMuted(boolean)` | Saves preference; muting immediately cancels all sound and the scheduler |
| `setVolume(number)` | Saves finite volume clamped to 0–1; zero suspends playback; invalid types/NaN/infinity are rejected |
| `setMusicEnabled(boolean)` | Saves the independent music preference; enabling does not bypass the activation or mute controls |
| `setScene('home' \| 'explore' \| 'battle')` | Chooses the original scene motif; changing scenes replaces accompaniment while preserving effects |
| `playCue(name)` | Plays a known effect only when enabled and visible; returns whether notes were scheduled |
| `getState()` | Returns supported/unlocked/muted/volume/musicEnabled/scene/playingMusic/hidden/destroyed/storageAvailable |
| `stop()` | Cancels sound and music until another explicit user-gesture unlock; preserves preferences |
| `destroy()` | Idempotently cancels playback, removes the visibility listener, disconnects nodes, and closes the context |

The constructor accepts optional `storage`, `contextFactory`, `document`, `timers`, and `userGesture` dependencies for deterministic tests. Production callers should keep the default gesture policy.

## Event cues

| Cue | Suggested presentation event |
| --- | --- |
| `feed`, `play`, `rest` | Successful matching care action |
| `steps` | Accepted walking input |
| `encounter` | Transition into an encounter |
| `card` | Accepted card effect |
| `attack` | Player attack animation |
| `hurt` | Confirmed player damage / opponent retaliation |
| `capture-start` | User starts the capture windup |
| `capture-success` | Successful capture returned by the game core |
| `capture-fail` | Failed capture returned by the game core |
| `win`, `retreat`, `evolve`, `select` | Matching confirmed outcome or companion selection |

Effects are presentation only. The engine does not change game state, decide battle outcomes, send requests, or assume that a capture windup succeeded. Use before/after states from the shared core to choose outcome cues. Canceling an unsent capture should not play a success/failure cue. Its brief starting tone ends naturally; `stop()` is a full playback pause, not a per-cue cancel operation.

## Resource limits and tests

- At most **12 active or scheduled oscillator voices**. Each voice is disconnected on completion; expired voices are also pruned before new scheduling.
- Each note has an explicit stop time and a maximum lifetime of **1.2 seconds**. Effects contain at most six notes. Per-voice gain is capped at 0.065 before the master volume.
- Music uses one **100 ms** interval with **180 ms** scheduling lookahead and a hard cap of eight musical steps per tick. Long browser stalls skip missed notes instead of replaying a backlog.
- Home, explore, and battle have different original eight-step motifs. Their step intervals are 600 ms, 480 ms, and 300 ms respectively. A lead and sparse bass form the accompaniment.
- Sound effects have priority over music voices. If effects already occupy the voice budget, another effect is skipped instead of allocating more voices.

Run the focused tests with:

```sh
./scripts/node.sh --test tests/audio-engine.test.mjs
```

The mocked Web Audio tests verify activation, remembered preferences without autoplay, valid/invalid cues, deterministic notes, volume/mute, voice lifetimes and limits, delayed scheduler recovery, visibility, manual stop, destruction, and asynchronous suspend/resume races. They verify scheduling and cleanup behavior; they are not an acoustic assessment of speakers or headphones.

## Hardware boundary

This richer sound is a **browser simulator feature**. The provisional ESP32-S3 board's stock EXIO8-controlled buzzer supports short on/off patterns, not PCM/WAV playback or this polyphonic soundtrack. See `docs/HARDWARE.md` and `docs/FEATURE_ROSTER.md` for the existing hardware boundary. This change adds no ESP audio driver, changes no firmware pins, and makes no claim about sound on an untested physical board.

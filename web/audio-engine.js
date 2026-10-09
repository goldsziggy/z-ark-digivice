// Original oscillator music and effects for the browser simulator. No audio assets,
// network requests, sample libraries, or game-state authority live in this module.
export const AUDIO_LIMITS = Object.freeze({
  maxVoices: 12,
  maxEffectGroups: 2,
  lookAheadSeconds: 0.18,
  schedulerIntervalMs: 100,
  maxNoteSeconds: 1.2,
  maxStepsPerTick: 8,
});

const STORAGE_KEY = 'digivice.audio.v1';
const DEFAULTS = Object.freeze({ muted: true, volume: 0.35, musicEnabled: false });
// [MIDI pitch, duration seconds, offset seconds, optional ending pitch]. These
// short motifs were composed for this project, independently of franchise music.
const CUES = Object.freeze({
  feed: { type: 'sine', notes: [[72, 0.12, 0], [76, 0.16, 0.09], [79, 0.14, 0.2]] },
  play: { type: 'triangle', notes: [[67, 0.09, 0], [74, 0.11, 0.1], [79, 0.16, 0.23], [76, 0.1, 0.4]] },
  rest: { type: 'sine', notes: [[67, 0.25, 0], [64, 0.3, 0.22], [60, 0.4, 0.47]] },
  steps: { type: 'triangle', notes: [[48, 0.035, 0], [55, 0.04, 0.075]] },
  encounter: { type: 'square', notes: [[55, 0.09, 0], [67, 0.09, 0.1], [74, 0.16, 0.21], [79, 0.22, 0.38]] },
  card: { type: 'triangle', notes: [[72, 0.08, 0], [79, 0.08, 0.09], [84, 0.1, 0.18], [88, 0.16, 0.27]] },
  'menu-confirm': { type: 'sine', notes: [[72, 0.04, 0], [79, 0.065, 0.04]] },
  'menu-back': { type: 'sine', notes: [[72, 0.04, 0], [67, 0.065, 0.04]] },
  'attack-physical': { type: 'square', notes: [[48, 0.09, 0, 79], [79, 0.065, 0.05, 43]] },
  'attack-magic': { type: 'sine', notes: [[67, 0.14, 0, 79], [74, 0.14, 0.07, 86], [86, 0.18, 0.16, 81]] },
  hit: { type: 'triangle', notes: [[55, 0.06, 0, 36], [43, 0.08, 0.04, 31]] },
  crit: { type: 'square', notes: [[84, 0.06, 0, 48], [91, 0.075, 0.04, 55], [48, 0.14, 0.1, 31]] },
  'capture-arm': { type: 'sine', notes: [[67, 0.09, 0], [74, 0.13, 0.1]] },
  'capture-throw': { type: 'triangle', notes: [[60, 0.17, 0, 91], [84, 0.12, 0.12, 72]] },
  'capture-wiggle': { type: 'sine', notes: [[67, 0.06, 0, 70], [70, 0.07, 0.065, 67]] },
  'capture-success': { type: 'triangle', notes: [[72, 0.12, 0], [76, 0.13, 0.12], [79, 0.15, 0.25], [84, 0.35, 0.43], [72, 0.3, 0.45]] },
  'capture-fail': { type: 'sine', notes: [[72, 0.1, 0], [68, 0.14, 0.12], [63, 0.24, 0.28]] },
  win: { type: 'triangle', notes: [[67, 0.1, 0], [72, 0.1, 0.12], [79, 0.18, 0.24], [84, 0.3, 0.44]] },
  retreat: { type: 'sine', notes: [[67, 0.2, 0], [64, 0.25, 0.2], [60, 0.35, 0.43]] },
  evolution: { type: 'triangle', notes: [[60, 0.12, 0], [64, 0.12, 0.12], [67, 0.12, 0.24], [72, 0.14, 0.36], [79, 0.2, 0.5], [84, 0.5, 0.68]] },
});

const ALIASES = Object.freeze({ select: 'menu-confirm', attack: 'attack-physical', hurt: 'hit',
  'capture-start': 'capture-arm', evolve: 'evolution' });
export const AUDIO_CUES = Object.freeze([...Object.keys(CUES), ...Object.keys(ALIASES)]);
// Priorities suppress lower-value sounds during outcomes; categories replace
// their own unfinished cues. A burst of taps never queues a backlog of beeps.
const CUE_POLICY = Object.freeze({
  ui: { priority: 1, cooldown: 0.08, peak: 0.036 },
  care: { priority: 1, cooldown: 0.12, peak: 0.055 },
  battle: { priority: 2, cooldown: 0.09, peak: 0.055 },
  capture: { priority: 3, cooldown: 0.12, peak: 0.055 },
  outcome: { priority: 4, cooldown: 0.4, peak: 0.06 },
});
function categoryFor(name) {
  if (name.startsWith('menu-')) return 'ui';
  if (['win', 'retreat', 'evolution', 'capture-success', 'capture-fail'].includes(name)) return 'outcome';
  if (name.startsWith('capture-')) return 'capture';
  if (['attack-physical', 'attack-magic', 'hit', 'crit', 'encounter', 'card'].includes(name)) return 'battle';
  return 'care';
}
const VARIATION_CENTS = [0, 3, -3, 1.5];

const MUSIC = Object.freeze({
  home: { step: 0.6, type: 'sine', lead: [72, 76, 79, 76, 74, 79, 76, 72], bass: [48, 53] },
  explore: { step: 0.48, type: 'triangle', lead: [74, 78, 81, 83, 81, 78, 76, 74], bass: [50, 55] },
  battle: { step: 0.3, type: 'triangle', lead: [69, 72, 76, 74, 69, 72, 76, 79], bass: [45, 41] },
});

const frequency = (midi) => 440 * 2 ** ((midi - 69) / 12);
function defaultStorage() {
  try { return globalThis.localStorage ?? null; } catch { return null; }
}
function trustedGesture(event) {
  // Transient activation may remain set after earlier input. An explicitly
  // synthetic event must not borrow that permission for a later unlock.
  if (event !== undefined && (typeof globalThis.Event !== 'function' ||
      !(event instanceof globalThis.Event) || event.isTrusted !== true)) return false;
  if (globalThis.navigator?.userActivation) return globalThis.navigator.userActivation.isActive === true;
  // Older browsers may omit UserActivation; their trusted input event remains
  // necessary. A programmatic dispatch or arbitrary unlock() call is insufficient.
  return typeof globalThis.Event === 'function' && event instanceof globalThis.Event &&
    event.isTrusted === true && ['click', 'pointerup', 'touchend', 'keydown'].includes(event.type);
}

export class AudioEngine {
  constructor(options = {}) {
    const Context = globalThis.AudioContext ?? globalThis.webkitAudioContext;
    this._factory = options.contextFactory ?? (Context ? () => new Context() : null);
    this._gesture = options.userGesture ?? trustedGesture;
    this._document = options.document ?? globalThis.document ?? null;
    this._timers = options.timers ?? {
      setInterval: (callback, delay) => globalThis.setInterval(callback, delay),
      clearInterval: (timer) => globalThis.clearInterval(timer),
    };
    this._storage = options.storage === undefined ? defaultStorage() : options.storage;
    this._storageAvailable = Boolean(this._storage);
    this._preferences = { ...DEFAULTS };
    this._context = null;
    this._suspending = null;
    this._master = null;
    this._musicGain = null;
    this._voices = new Set();
    this._lastCueAt = new Map();
    this._cueSequence = 0;
    this._duckUntil = 0;
    this._timer = null;
    this._scene = 'home';
    this._musicStep = 0;
    this._nextMusicTime = 0;
    this._unlocked = false;
    this._stopped = false;
    this._destroyed = false;
    this._hidden = Boolean(this._document?.hidden);
    this._generation = 0;
    this._readPreferences();
    this._visibilityListener = () => this._visibilityChanged();
    this._document?.addEventListener('visibilitychange', this._visibilityListener);
  }

  getState() {
    return {
      supported: typeof this._factory === 'function',
      unlocked: this._unlocked,
      ...this._preferences,
      scene: this._scene,
      playingMusic: this._timer !== null,
      hidden: this._hidden,
      destroyed: this._destroyed,
      storageAvailable: this._storageAvailable,
    };
  }

  // Invoke from a user input handler before awaiting any network or game work.
  // Remembered preferences alone never grant playback permission on a new page.
  async unlock(event) {
    if (this._destroyed || this._hidden || typeof this._factory !== 'function' || !this._gesture(event)) return false;
    try {
      if (!this._context) {
        this._context = this._factory();
        this._master = this._context.createGain();
        // Keep the intrinsic value silent too: cancelScheduledValues(0) can
        // remove the initial automation point before the first rendered frame.
        this._master.gain.value = 0;
        this._master.gain.setValueAtTime(0, this._context.currentTime);
        this._master.connect(this._context.destination);
        this._musicGain = this._context.createGain();
        this._musicGain.connect(this._master);
      }
      const generation = ++this._generation;
      if (this._suspending) await this._suspending;
      if (generation !== this._generation || this._destroyed || this._hidden) return false;
      if (this._context.state !== 'running') await this._context.resume();
      if (generation !== this._generation || this._destroyed || this._hidden || this._context.state !== 'running') {
        // A resume already queued in the browser may settle after hide/mute/stop.
        if (this._destroyed || this._hidden || this._stopped || this._preferences.muted) this._silence();
        return false;
      }
      this._unlocked = true;
      this._stopped = false;
      this._applyVolume();
      this._startMusic();
      return true;
    } catch {
      return false;
    }
  }

  setMuted(muted) {
    if (this._destroyed || typeof muted !== 'boolean') return false;
    this._preferences.muted = muted;
    this._savePreferences();
    if (muted) this._silence();
    else this._refreshPlayback();
    return true;
  }

  setVolume(volume) {
    if (this._destroyed || typeof volume !== 'number' || !Number.isFinite(volume)) return false;
    this._preferences.volume = Math.max(0, Math.min(1, volume));
    this._savePreferences();
    if (this._preferences.volume === 0) this._silence();
    else this._refreshPlayback();
    return true;
  }

  setMusicEnabled(enabled) {
    if (this._destroyed || typeof enabled !== 'boolean') return false;
    this._preferences.musicEnabled = enabled;
    this._savePreferences();
    if (!enabled) this._stopMusic();
    else this._refreshPlayback();
    return true;
  }

  setScene(scene) {
    if (this._destroyed || !Object.hasOwn(MUSIC, scene)) return false;
    if (this._scene !== scene) {
      this._scene = scene;
      this._stopMusic();
      this._startMusic();
    }
    return true;
  }

  playCue(name) {
    name = Object.hasOwn(ALIASES, name) ? ALIASES[name] : name;
    if (!Object.hasOwn(CUES, name) || !this._canPlay() || this._suspending || this._context?.state !== 'running') return false;
    const cue = CUES[name];
    const category = categoryFor(name);
    const policy = CUE_POLICY[category];
    const now = this._context.currentTime;
    if (now - (this._lastCueAt.get(name) ?? -Infinity) < policy.cooldown) return false;
    this._pruneVoices();
    const groups = new Map();
    for (const voice of this._voices) if (voice.bus === 'sfx') groups.set(voice.category, voice.priority);
    if ([...groups.values()].some(priority => priority > policy.priority)) return false;
    for (const voice of this._voices) {
      if (voice.bus === 'sfx' && (voice.category === category || policy.priority >= CUE_POLICY.capture.priority)) {
        this._releaseVoice(voice, true);
      }
    }
    groups.clear();
    for (const voice of this._voices) if (voice.bus === 'sfx') groups.set(voice.category, voice.priority);
    if (groups.size >= AUDIO_LIMITS.maxEffectGroups) {
      const oldestCategory = groups.keys().next().value;
      for (const voice of this._voices) if (voice.category === oldestCategory) this._releaseVoice(voice, true);
    }
    // Reserve the entire effect before allocating any nodes. Music yields first.
    for (const voice of this._voices) {
      if (this._voices.size + cue.notes.length <= AUDIO_LIMITS.maxVoices) break;
      if (voice.bus === 'music') this._releaseVoice(voice, true);
    }
    if (this._voices.size + cue.notes.length > AUDIO_LIMITS.maxVoices) return false;
    const start = now + 0.005;
    const cents = VARIATION_CENTS[this._cueSequence % VARIATION_CENTS.length];
    let played = false;
    for (const [pitch, duration, offset, endingPitch] of cue.notes) {
      played = this._note({ pitch, duration, start: start + offset, endingPitch,
        type: cue.type, peak: cue.type === 'square' ? policy.peak * 0.55 : policy.peak,
        bus: 'sfx', category, priority: policy.priority, cents }) || played;
    }
    if (played) {
      this._lastCueAt.set(name, now);
      this._cueSequence = (this._cueSequence + 1) % VARIATION_CENTS.length;
      this._duckUntil = Math.max(this._duckUntil, start + Math.max(...cue.notes.map(([, duration, offset]) => duration + offset)) + 0.08);
      this._musicGain.gain.cancelScheduledValues(now);
      this._musicGain.gain.setTargetAtTime(0.22, now, 0.015);
      this._musicGain.gain.setTargetAtTime(1, this._duckUntil, 0.06);
    }
    return played;
  }

  // Manual stop is distinct from a background-tab pause. Visibility changes and
  // preference changes cannot undo it; another explicit unlock is required.
  stop() {
    if (this._destroyed) return;
    this._stopped = true;
    this._silence();
  }

  destroy() {
    if (this._destroyed) return;
    this._destroyed = true;
    this._unlocked = false;
    this._silence();
    this._document?.removeEventListener('visibilitychange', this._visibilityListener);
    try { this._master?.disconnect(); } catch { /* Already disconnected. */ }
    try { this._musicGain?.disconnect(); } catch { /* Already disconnected. */ }
    try { Promise.resolve(this._context?.close()).catch(() => {}); } catch { /* Unsupported/closed context. */ }
  }

  _readPreferences() {
    try {
      const raw = this._storage?.getItem(STORAGE_KEY);
      if (!raw) return;
      const value = JSON.parse(raw);
      if (!value || (value.version !== undefined && value.version !== 1) ||
          typeof value.muted !== 'boolean' || typeof value.musicEnabled !== 'boolean' ||
          typeof value.volume !== 'number' || !Number.isFinite(value.volume) || value.volume < 0 || value.volume > 1) return;
      this._preferences = { muted: value.muted, musicEnabled: value.musicEnabled, volume: value.volume };
    } catch { this._storageAvailable = false; }
  }

  _savePreferences() {
    try {
      this._storage?.setItem(STORAGE_KEY, JSON.stringify({ version: 1, ...this._preferences }));
    } catch { this._storageAvailable = false; }
  }

  _canPlay() {
    return this._unlocked && !this._destroyed && !this._hidden && !this._stopped &&
      !this._preferences.muted && this._preferences.volume > 0;
  }

  _applyVolume() {
    if (!this._master) return;
    const now = this._context.currentTime;
    this._master.gain.cancelScheduledValues(now);
    this._master.gain.setTargetAtTime(this._canPlay() ? this._preferences.volume : 0, now, 0.015);
  }

  _refreshPlayback() {
    this._applyVolume();
    if (!this._canPlay()) return;
    if (!this._suspending && this._context?.state === 'running') this._startMusic();
    else void this._resumeEnabled();
  }

  async _resumeEnabled() {
    if (!this._canPlay() || !this._context) return;
    const generation = ++this._generation;
    try {
      if (this._suspending) await this._suspending;
      if (generation !== this._generation || !this._canPlay()) return;
      await this._context.resume();
      if (generation !== this._generation || !this._canPlay() || this._context.state !== 'running') {
        if (!this._canPlay()) this._silence();
        return;
      }
      this._applyVolume();
      this._startMusic();
    } catch { /* A browser may require another user gesture after interruption. */ }
  }

  _visibilityChanged() {
    if (this._destroyed) return;
    this._hidden = Boolean(this._document?.hidden);
    if (this._hidden) this._silence();
    else this._refreshPlayback();
  }

  _silence() {
    ++this._generation;
    this._stopMusic();
    for (const voice of this._voices) this._releaseVoice(voice, true);
    this._lastCueAt.clear();
    this._duckUntil = 0;
    if (this._musicGain) {
      this._musicGain.gain.cancelScheduledValues(this._context.currentTime);
      this._musicGain.gain.setValueAtTime(1, this._context.currentTime);
    }
    if (this._master) {
      const now = this._context.currentTime;
      this._master.gain.cancelScheduledValues(now);
      this._master.gain.setValueAtTime(0, now);
    }
    try {
      if (!this._suspending && this._context?.state === 'running') {
        const pending = Promise.resolve(this._context.suspend()).catch(() => {});
        this._suspending = pending;
        void pending.then(() => { if (this._suspending === pending) this._suspending = null; });
      }
    } catch { /* Browsers may close the context during navigation. */ }
  }

  _startMusic() {
    if (this._timer !== null || this._suspending || !this._canPlay() || !this._preferences.musicEnabled || this._context?.state !== 'running') return;
    this._musicStep = 0;
    this._nextMusicTime = this._context.currentTime + 0.025;
    this._timer = this._timers.setInterval(() => this._tick(), AUDIO_LIMITS.schedulerIntervalMs);
    this._tick();
  }

  _stopMusic() {
    if (this._timer !== null) {
      this._timers.clearInterval(this._timer);
      this._timer = null;
    }
    for (const voice of this._voices) if (voice.bus === 'music') this._releaseVoice(voice, true);
  }

  _tick() {
    if (!this._canPlay() || !this._preferences.musicEnabled || this._context?.state !== 'running') {
      this._stopMusic();
      return;
    }
    this._pruneVoices();
    const now = this._context.currentTime;
    const music = MUSIC[this._scene];
    // Skip time lost to a stalled/throttled browser. Never replay a backlog of notes.
    if (this._nextMusicTime < now - AUDIO_LIMITS.lookAheadSeconds) {
      const skipped = Math.floor((now - this._nextMusicTime) / music.step);
      this._musicStep = (this._musicStep + skipped) % music.lead.length;
      this._nextMusicTime = now + 0.025;
    }
    let count = 0;
    while (this._nextMusicTime < now + AUDIO_LIMITS.lookAheadSeconds && count < AUDIO_LIMITS.maxStepsPerTick) {
      const index = this._musicStep % music.lead.length;
      const start = Math.max(now + 0.005, this._nextMusicTime);
      this._note({ pitch: music.lead[index], duration: music.step * 0.65, start,
        type: music.type, peak: 0.029, bus: 'music' });
      if (index % 4 === 0) this._note({ pitch: music.bass[index / 4], duration: music.step * 1.3,
        start, type: 'sine', peak: 0.022, bus: 'music' });
      this._musicStep = (this._musicStep + 1) % music.lead.length;
      this._nextMusicTime += music.step;
      ++count;
    }
  }

  _note({ pitch, duration, start, endingPitch, type, peak, bus, category, priority = 0, cents = 0 }) {
    if (this._voices.size >= AUDIO_LIMITS.maxVoices || !this._canPlay()) return false;
    const held = Math.min(AUDIO_LIMITS.maxNoteSeconds, Math.max(0.02, duration));
    const end = start + held;
    let oscillator;
    let gain;
    let voice;
    try {
      oscillator = this._context.createOscillator();
      gain = this._context.createGain();
      oscillator.type = type;
      oscillator.detune.setValueAtTime(cents, start);
      oscillator.frequency.setValueAtTime(frequency(pitch), start);
      if (endingPitch !== undefined) oscillator.frequency.exponentialRampToValueAtTime(frequency(endingPitch), end);
      gain.gain.setValueAtTime(0, start);
      gain.gain.linearRampToValueAtTime(Math.min(0.065, peak), start + 0.006);
      gain.gain.setValueAtTime(Math.min(0.065, peak), start + held * 0.45);
      gain.gain.exponentialRampToValueAtTime(0.0001, end - 0.002);
      gain.gain.linearRampToValueAtTime(0, end);
      oscillator.connect(gain);
      gain.connect(bus === 'music' ? this._musicGain : this._master);
      voice = { oscillator, gain, end, bus, category, priority, released: false };
      this._voices.add(voice);
      oscillator.onended = () => this._releaseVoice(voice, false);
      oscillator.start(start);
      oscillator.stop(end);
      return true;
    } catch {
      if (voice) this._releaseVoice(voice, true);
      else {
        try { oscillator?.disconnect(); } catch { /* Partial creation. */ }
        try { gain?.disconnect(); } catch { /* Partial creation. */ }
      }
      return false;
    }
  }

  _pruneVoices() {
    const now = this._context?.currentTime ?? 0;
    for (const voice of this._voices) if (voice.end <= now) this._releaseVoice(voice, true);
  }

  _releaseVoice(voice, stop) {
    if (voice.released) return;
    voice.released = true;
    this._voices.delete(voice);
    voice.oscillator.onended = null;
    if (stop) {
      try { voice.oscillator.stop(this._context.currentTime); } catch { /* Already ended. */ }
    }
    try { voice.oscillator.disconnect(); } catch { /* Already disconnected. */ }
    try { voice.gain.disconnect(); } catch { /* Already disconnected. */ }
  }
}

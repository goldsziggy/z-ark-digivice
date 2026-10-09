import test from 'node:test';
import assert from 'node:assert/strict';
import { AudioEngine, AUDIO_LIMITS, AUDIO_CUES } from '../web/audio-engine.js';

const STORAGE_KEY = 'digivice.audio.v1';
const DEFAULTS = { muted: true, volume: 0.35, musicEnabled: false };
const CUES = AUDIO_CUES;
const gesture = { isTrusted: true, type: 'pointerdown' };

class MemoryStorage {
  constructor(value) { this.value = value; this.writes = []; }
  getItem(key) { assert.equal(key, STORAGE_KEY); return this.value ?? null; }
  setItem(key, value) { assert.equal(key, STORAGE_KEY); this.value = value; this.writes.push(value); }
}

class Visibility extends EventTarget {
  hidden = false;
  listeners = new Set();
  addEventListener(type, callback, options) {
    super.addEventListener(type, callback, options);
    if (type === 'visibilitychange') this.listeners.add(callback);
  }
  removeEventListener(type, callback, options) {
    super.removeEventListener(type, callback, options);
    if (type === 'visibilitychange') this.listeners.delete(callback);
  }
  setHidden(hidden) { this.hidden = hidden; this.dispatchEvent(new Event('visibilitychange')); }
}

class ManualTimers {
  nextId = 0;
  callbacks = new Map();
  setInterval = (callback, delay) => {
    assert.ok(Number.isFinite(delay) && delay > 0);
    const id = ++this.nextId; this.callbacks.set(id, { callback, delay }); return id;
  };
  clearInterval = id => { this.callbacks.delete(id); };
  tick() { for (const { callback } of [...this.callbacks.values()]) callback(); }
}

class Parameter {
  value = 0;
  events = [];
  record(method, ...args) { this.events.push([method, ...args]); return this; }
  setValueAtTime(...args) { this.value = args[0]; return this.record('set', ...args); }
  linearRampToValueAtTime(...args) { return this.record('linear', ...args); }
  exponentialRampToValueAtTime(...args) { return this.record('exponential', ...args); }
  setTargetAtTime(...args) { return this.record('target', ...args); }
  cancelScheduledValues(...args) { return this.record('cancel', ...args); }
  cancelAndHoldAtTime(...args) { return this.record('hold', ...args); }
}

class Node {
  connections = [];
  disconnected = false;
  connect(node) { this.connections.push(node); return node; }
  disconnect() { this.disconnected = true; this.connections.length = 0; }
}

class Oscillator extends Node {
  frequency = new Parameter();
  detune = new Parameter();
  type = 'sine';
  startAt = null;
  stopAt = Infinity;
  ended = false;
  onended = null;
  constructor(context) { super(); this.context = context; }
  start(time = 0) { assert.equal(this.startAt, null, 'a voice must start only once'); this.startAt = time; }
  stop(time = 0) { this.stopAt = time; }
  endIfDue() {
    if (!this.ended && this.stopAt <= this.context.currentTime) {
      this.ended = true; this.onended?.();
    }
  }
}

class FakeAudioContext {
  currentTime = 0;
  state = 'suspended';
  destination = new Node();
  oscillators = [];
  gains = [];
  calls = { resume: 0, suspend: 0, close: 0 };
  createGain() { const node = new Node(); node.gain = new Parameter(); this.gains.push(node); return node; }
  createOscillator() { const node = new Oscillator(this); this.oscillators.push(node); return node; }
  async resume() { ++this.calls.resume; this.state = 'running'; }
  async suspend() { ++this.calls.suspend; this.state = 'suspended'; }
  async close() { ++this.calls.close; this.state = 'closed'; }
  advance(seconds, emitEnded = true) {
    this.currentTime += seconds;
    if (emitEnded) for (const voice of this.oscillators) voice.endIfDue();
  }
  get liveVoices() {
    return this.oscillators.filter(voice => !voice.disconnected && !voice.ended && voice.stopAt > this.currentTime);
  }
}

function fixture(options = {}) {
  const context = new FakeAudioContext(), document = new Visibility(), timers = new ManualTimers();
  const storage = new MemoryStorage(options.saved);
  let creates = 0;
  const engine = new AudioEngine({ storage, document, timers,
    contextFactory: () => { ++creates; return context; }, userGesture: event => event?.isTrusted === true,
    ...options });
  return { engine, context, document, timers, storage, get creates() { return creates; } };
}

function preferences(engine) {
  const { muted, volume, musicEnabled } = engine.getState(); return { muted, volume, musicEnabled };
}

async function audible(f) {
  assert.equal(await f.engine.unlock(gesture), true);
  f.engine.setMuted(false);
  await settled();
}

async function settled() { await Promise.resolve(); await Promise.resolve(); }

function score(context) {
  return context.oscillators.map(voice => ({ type: voice.type, frequency: voice.frequency.value,
    automation: voice.frequency.events, detune: voice.detune.events, start: voice.startAt, stop: voice.stopAt }));
}

test('construction and saved enabled preferences cannot autoplay or allocate an audio context', async () => {
  for (const saved of [undefined, JSON.stringify({ muted: false, volume: 0.6, musicEnabled: true })]) {
    const f = fixture({ saved });
    assert.equal(f.creates, 0); assert.equal(f.timers.callbacks.size, 0);
    assert.equal(f.engine.getState().unlocked, false);
    assert.equal(f.engine.getState().playingMusic, false);
    assert.equal(f.engine.playCue('feed'), false);
    f.engine.setScene('battle'); f.engine.setMusicEnabled(true); f.engine.setMuted(false);
    assert.equal(f.creates, 0); assert.equal(f.timers.callbacks.size, 0);
    assert.equal(await f.engine.unlock({ isTrusted: false }), false);
    assert.equal(await f.engine.unlock(), false);
    assert.equal(f.creates, 0);
    f.engine.destroy();
  }
});

test('the production gesture policy rejects synthetic events and plain object imitations', async () => {
  const f = fixture({ userGesture: undefined });
  assert.equal(await f.engine.unlock(), false);
  assert.equal(await f.engine.unlock(new Event('click')), false);
  assert.equal(await f.engine.unlock({ type: 'click', isTrusted: true }), false);
  assert.equal(f.creates, 0);
  f.engine.destroy();
});

test('synthetic events cannot borrow transient activation left by an earlier gesture', async () => {
  const descriptor = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { userActivation: { isActive: true } } });
  const f = fixture({ userGesture: undefined });
  try {
    assert.equal(await f.engine.unlock(new Event('click')), false);
    assert.equal(await f.engine.unlock({ type: 'click', isTrusted: true }), false);
    assert.equal(f.creates, 0);
  } finally {
    f.engine.destroy();
    if (descriptor) Object.defineProperty(globalThis, 'navigator', descriptor);
    else delete globalThis.navigator;
  }
});

test('preferences validate and persist independently of audio availability', () => {
  const f = fixture();
  assert.deepEqual(preferences(f.engine), DEFAULTS);
  f.engine.setVolume(2); assert.equal(f.engine.getState().volume, 1);
  f.engine.setVolume(-3); assert.equal(f.engine.getState().volume, 0);
  f.engine.setVolume(0.6); f.engine.setMuted(false); f.engine.setMusicEnabled(true);
  const before = preferences(f.engine), writes = f.storage.writes.length;
  for (const invalid of [NaN, Infinity, -Infinity, '0.5', null, undefined]) {
    assert.equal(f.engine.setVolume(invalid), false);
  }
  assert.equal(f.engine.setMuted('false'), false);
  assert.equal(f.engine.setMusicEnabled(1), false);
  assert.equal(f.engine.setScene('unknown'), false);
  assert.deepEqual(preferences(f.engine), before); assert.equal(f.storage.writes.length, writes);
  assert.deepEqual(preferences(fixture({ saved: f.storage.value }).engine), before);
  assert.equal(f.creates, 0);
  f.engine.destroy();
});

test('corrupt or inaccessible preference storage falls back safely without constructor writes', () => {
  for (const saved of ['{', 'null', '[]', JSON.stringify({ muted: 'no', volume: 'loud', musicEnabled: 3 }),
    JSON.stringify({ version: 99, muted: false, volume: 0.9, musicEnabled: true })]) {
    const f = fixture({ saved });
    assert.deepEqual(preferences(f.engine), DEFAULTS);
    assert.equal(f.storage.writes.length, 0);
    f.engine.destroy();
  }
  const f = fixture({ storage: { getItem() { throw new Error('disabled'); }, setItem() { throw new Error('quota'); } } });
  assert.deepEqual(preferences(f.engine), DEFAULTS);
  assert.doesNotThrow(() => { f.engine.setVolume(0.5); f.engine.setMuted(false); });
  assert.equal(f.engine.getState().volume, 0.5);
  f.engine.destroy();
});

test('unlock requires a visible accepted gesture and a working context', async () => {
  const f = fixture();
  f.document.setHidden(true);
  assert.equal(await f.engine.unlock(gesture), false); assert.equal(f.creates, 0);
  f.document.setHidden(false);
  assert.equal(await f.engine.unlock(gesture), true);
  assert.equal(f.creates, 1); assert.equal(f.context.state, 'running');
  assert.equal(f.engine.getState().unlocked, true);
  assert.equal(f.engine.playCue('feed'), false, 'unlock does not override mute');
  f.engine.setMuted(false); await settled(); assert.equal(f.engine.playCue('feed'), true);
  assert.equal(await f.engine.unlock(gesture), true); assert.equal(f.creates, 1);
  f.engine.destroy();
  const unavailable = fixture({ contextFactory: () => { throw new Error('unavailable'); } });
  assert.equal(await unavailable.engine.unlock(gesture), false);
  assert.equal(unavailable.engine.playCue('feed'), false);
  unavailable.engine.destroy();
});

test('every cue has deterministic synthesis with finite bounded note lifetimes', async () => {
  for (const cue of CUES) {
    const first = fixture(), second = fixture();
    await audible(first); await audible(second);
    assert.equal(first.engine.playCue(cue), true, cue);
    assert.equal(second.engine.playCue(cue), true, cue);
    assert.deepEqual(score(first.context), score(second.context), cue);
    assert.ok(first.context.oscillators.length > 0, `${cue} creates notes`);
    assert.ok(first.context.oscillators.length <= AUDIO_LIMITS.maxVoices, cue);
    for (const voice of first.context.oscillators) {
      assert.ok(Number.isFinite(voice.startAt) && voice.startAt >= first.context.currentTime, cue);
      assert.ok(Number.isFinite(voice.stopAt) && voice.stopAt > voice.startAt, cue);
      assert.ok(voice.stopAt - voice.startAt <= AUDIO_LIMITS.maxNoteSeconds + 1e-9, cue);
      assert.ok(voice.frequency.value > 0 && Number.isFinite(voice.frequency.value), cue);
    }
    first.engine.destroy(); second.engine.destroy();
  }
});

test('unknown cues are inert and overlapping effects stay inside the voice limit', async () => {
  const f = fixture(); await audible(f);
  for (const cue of ['', 'unknown', '__proto__', 'constructor', null, undefined]) {
    assert.equal(f.engine.playCue(cue), false);
  }
  assert.equal(f.context.oscillators.length, 0);
  for (let i = 0; i < 100; ++i) {
    f.engine.playCue(CUES[i % CUES.length]);
    assert.ok(f.context.liveVoices.length <= AUDIO_LIMITS.maxVoices);
  }
  f.engine.setMuted(true);
  assert.equal(f.context.liveVoices.length, 0); assert.equal(f.timers.callbacks.size, 0);
  assert.equal(f.engine.playCue('attack'), false);
  f.engine.destroy();
});

test('ended notes release nodes and missed onended notifications cannot exhaust the voice pool', async () => {
  const f = fixture(); await audible(f); f.engine.playCue('capture-success');
  f.context.advance(10);
  assert.ok(f.context.oscillators.every(voice => voice.disconnected));
  f.engine.playCue('evolve');
  f.context.advance(10, false);
  assert.equal(f.engine.playCue('feed'), true);
  assert.ok(f.context.liveVoices.length > 0);
  assert.ok(f.context.liveVoices.length <= AUDIO_LIMITS.maxVoices);
  f.engine.destroy();
});

test('legacy aliases retain their sounds and share repeat throttling with canonical names', async () => {
  for (const [alias, canonical] of Object.entries({ select: 'menu-confirm', attack: 'attack-physical',
    hurt: 'hit', 'capture-start': 'capture-arm', evolve: 'evolution' })) {
    const first = fixture(), second = fixture(); await audible(first); await audible(second);
    assert.equal(first.engine.playCue(alias), true);
    assert.equal(second.engine.playCue(canonical), true);
    assert.deepEqual(score(first.context), score(second.context), alias);
    const count = first.context.oscillators.length;
    assert.equal(first.engine.playCue(canonical), false, 'alias cannot bypass cooldown');
    assert.equal(first.context.oscillators.length, count);
    first.engine.destroy(); second.engine.destroy();
  }
});

test('menu, damage, attack and capture phase cues have distinct scores', async () => {
  const scores = new Map();
  for (const cue of ['menu-confirm', 'menu-back', 'attack-physical', 'attack-magic', 'hit', 'crit',
    'capture-arm', 'capture-throw', 'capture-wiggle', 'capture-success', 'capture-fail', 'evolution']) {
    const f = fixture(); await audible(f); assert.equal(f.engine.playCue(cue), true);
    scores.set(cue, JSON.stringify(score(f.context))); f.engine.destroy();
  }
  assert.equal(new Set(scores.values()).size, scores.size);
});

test('rapid taps do not allocate notes or queue sound, and a later deliberate tap works', async () => {
  const f = fixture(); await audible(f);
  assert.equal(f.engine.playCue('menu-confirm'), true);
  const firstCount = f.context.oscillators.length;
  for (let i = 0; i < 200; ++i) assert.equal(f.engine.playCue('menu-confirm'), false);
  assert.equal(f.context.oscillators.length, firstCount);
  f.context.advance(0.09);
  assert.equal(f.engine.playCue('menu-confirm'), true);
  assert.equal(f.context.oscillators.length, firstCount * 2);
  assert.ok(f.context.oscillators.slice(0, firstCount).every(voice => voice.disconnected));
  f.engine.destroy();
});

test('an outcome replaces capture cues and cannot be interrupted by menu or battle sounds', async () => {
  const f = fixture(); await audible(f);
  assert.equal(f.engine.playCue('capture-arm'), true);
  const arming = [...f.context.liveVoices];
  assert.equal(f.engine.playCue('capture-throw'), true);
  assert.ok(arming.every(voice => voice.disconnected));
  const throwing = [...f.context.liveVoices];
  assert.equal(f.engine.playCue('capture-success'), true);
  assert.ok(throwing.every(voice => voice.disconnected));
  const count = f.context.oscillators.length;
  for (const name of ['menu-confirm', 'menu-back', 'feed', 'attack-magic', 'capture-wiggle']) {
    assert.equal(f.engine.playCue(name), false, name);
  }
  assert.equal(f.context.oscillators.length, count);
  f.context.advance(1);
  assert.equal(f.engine.playCue('menu-back'), true, 'controls become audible after outcome ends');
  f.engine.destroy();
});

test('effect groups stay bounded while music yields voices and ducks under effects', async () => {
  const f = fixture(); await audible(f); f.engine.setMusicEnabled(true);
  assert.equal(f.engine.playCue('feed'), true);
  assert.equal(f.engine.playCue('menu-confirm'), true);
  assert.equal(f.engine.playCue('attack-physical'), true);
  const groups = new Set([...f.engine._voices].filter(voice => voice.bus === 'sfx').map(voice => voice.category));
  assert.ok(groups.size <= AUDIO_LIMITS.maxEffectGroups);
  assert.deepEqual([...groups], ['ui', 'battle'], 'oldest lower-priority care group yields');
  const duckEvents = f.engine._musicGain.gain.events;
  assert.ok(duckEvents.some(([kind, volume]) => kind === 'target' && volume < 0.3));
  assert.ok(duckEvents.some(([kind, volume, time]) => kind === 'target' && volume === 1 && time > f.context.currentTime));
  for (let i = 0; i < 100; ++i) {
    f.context.advance(0.1); f.timers.tick();
    f.engine.playCue(i % 2 ? 'attack-physical' : 'attack-magic');
    assert.ok(f.context.liveVoices.length <= AUDIO_LIMITS.maxVoices);
  }
  f.engine.setMuted(true);
  assert.equal(f.context.liveVoices.length, 0);
  assert.equal(f.engine._musicGain.gain.value, 1, 'mute clears pending duck automation');
  f.engine.destroy();
});

test('subtle variation is bounded, repeatable and does not consume game randomness', async () => {
  const first = fixture(), second = fixture(); await audible(first); await audible(second);
  const detunes = [];
  for (let i = 0; i < 5; ++i) {
    for (const f of [first, second]) {
      assert.equal(f.engine.playCue('hit'), true); f.context.advance(0.2);
    }
    detunes.push(first.context.oscillators.at(-1).detune.value);
  }
  assert.deepEqual(score(first.context), score(second.context));
  assert.ok(new Set(detunes).size > 1);
  assert.ok(detunes.every(value => Math.abs(value) <= 3));
  assert.equal(detunes[0], detunes[4]);
  first.engine.destroy(); second.engine.destroy();
});

test('music is opt-in, uses distinct scene scores, and disabling it stops the scheduler', async () => {
  const scores = [];
  for (const scene of ['home', 'explore', 'battle']) {
    const f = fixture(); f.engine.setScene(scene); await audible(f);
    assert.equal(f.timers.callbacks.size, 0); assert.equal(f.engine.getState().playingMusic, false);
    f.engine.setMusicEnabled(true);
    assert.equal(f.engine.getState().scene, scene);
    assert.equal(f.engine.getState().playingMusic, true); assert.equal(f.timers.callbacks.size, 1);
    for (let i = 0; i < 12; ++i) { f.context.advance(0.1); f.timers.tick(); }
    assert.ok(f.context.oscillators.length > 0);
    scores.push(score(f.context));
    f.engine.setMusicEnabled(false);
    assert.equal(f.engine.getState().playingMusic, false); assert.equal(f.timers.callbacks.size, 0);
    f.engine.destroy();
  }
  assert.notDeepEqual(scores[0], scores[1]); assert.notDeepEqual(scores[1], scores[2]);
  assert.notDeepEqual(scores[0], scores[2]);
});

test('a delayed music tick skips backlog and schedules bounded work near the current audio time', async () => {
  const f = fixture(); await audible(f); f.engine.setMusicEnabled(true);
  assert.equal(f.timers.callbacks.size, 1);
  assert.equal([...f.timers.callbacks.values()][0].delay, AUDIO_LIMITS.schedulerIntervalMs);
  f.context.advance(3600, false);
  const before = f.context.oscillators.length;
  f.timers.tick();
  const added = f.context.oscillators.slice(before);
  assert.ok(added.length > 0, 'scheduler recovers after a long gap');
  assert.ok(added.length <= AUDIO_LIMITS.maxVoices, 'one tick cannot replay an hour of notes');
  for (const voice of added) {
    assert.ok(voice.startAt >= f.context.currentTime);
    assert.ok(voice.startAt <= f.context.currentTime + AUDIO_LIMITS.lookAheadSeconds + 1e-9);
  }
  assert.ok(f.context.liveVoices.length <= AUDIO_LIMITS.maxVoices);
  f.engine.destroy();
});

test('hidden pages silence audio; visibility restores only prior enabled playback', async () => {
  const f = fixture(); await audible(f); f.engine.setMusicEnabled(true); f.engine.playCue('attack');
  f.document.setHidden(true); await settled();
  assert.equal(f.engine.getState().hidden, true); assert.equal(f.context.state, 'suspended');
  assert.equal(f.context.liveVoices.length, 0); assert.equal(f.timers.callbacks.size, 0);
  assert.equal(f.engine.playCue('attack'), false);
  f.document.setHidden(false); await settled();
  assert.equal(f.context.state, 'running'); assert.equal(f.engine.getState().playingMusic, true);
  assert.equal(f.timers.callbacks.size, 1);
  f.engine.setMuted(true);
  const resumeCalls = f.context.calls.resume;
  f.document.setHidden(true); f.document.setHidden(false); await settled();
  assert.equal(f.engine.getState().playingMusic, false); assert.equal(f.timers.callbacks.size, 0);
  assert.equal(f.context.calls.resume, resumeCalls);
  f.engine.destroy();
});

test('manual stop remains paused across preference and visibility changes until another gesture', async () => {
  const f = fixture(); await audible(f); f.engine.setMusicEnabled(true);
  f.engine.stop();
  assert.equal(f.context.liveVoices.length, 0); assert.equal(f.timers.callbacks.size, 0);
  assert.equal(f.engine.playCue('feed'), false);
  f.engine.setScene('battle'); f.engine.setMusicEnabled(true); f.engine.setMuted(false);
  f.document.setHidden(true); f.document.setHidden(false); await settled();
  assert.equal(f.timers.callbacks.size, 0); assert.equal(f.engine.getState().playingMusic, false);
  assert.equal(await f.engine.unlock({ isTrusted: false }), false);
  assert.equal(f.engine.playCue('feed'), false);
  assert.equal(await f.engine.unlock(gesture), true);
  assert.equal(f.engine.playCue('feed'), true); assert.equal(f.timers.callbacks.size, 1);
  f.engine.destroy();
});

test('destroy is idempotent and releases listeners, nodes and interval without later resurrection', async () => {
  const f = fixture(); await audible(f); f.engine.setMusicEnabled(true); f.engine.playCue('win');
  assert.equal(f.document.listeners.size, 1);
  f.engine.destroy(); f.engine.destroy(); await settled();
  assert.equal(f.engine.getState().destroyed, true); assert.equal(f.engine.getState().playingMusic, false);
  assert.equal(f.document.listeners.size, 0); assert.equal(f.timers.callbacks.size, 0);
  assert.equal(f.context.calls.close, 1); assert.equal(f.context.liveVoices.length, 0);
  assert.ok(f.context.gains.every(node => node.disconnected));
  f.document.setHidden(true); f.document.setHidden(false); f.timers.tick(); await settled();
  assert.equal(await f.engine.unlock(gesture), false); assert.equal(f.engine.playCue('feed'), false);
  assert.equal(f.context.calls.close, 1); assert.equal(f.timers.callbacks.size, 0);
});

test('a pending unlock cannot revive audio after the page is hidden or the engine is destroyed', async () => {
  for (const transition of ['hidden', 'destroyed']) {
    const f = fixture();
    let finishResume;
    const pending = new Promise(resolve => { finishResume = resolve; });
    f.context.resume = async () => {
      ++f.context.calls.resume;
      await pending;
      if (f.context.state !== 'closed') f.context.state = 'running';
    };
    f.engine.setMuted(false); f.engine.setMusicEnabled(true);
    const unlocking = f.engine.unlock(gesture);
    assert.equal(f.context.calls.resume, 1);
    if (transition === 'hidden') f.document.setHidden(true);
    else f.engine.destroy();
    finishResume();
    assert.equal(await unlocking, false, transition);
    await settled();
    assert.equal(f.context.liveVoices.length, 0, transition);
    assert.equal(f.timers.callbacks.size, 0, transition);
    assert.equal(f.engine.getState().playingMusic, false, transition);
    assert.notEqual(f.context.state, 'running', transition);
    f.engine.destroy();
  }
});

test('visibility recovery waits for an earlier asynchronous suspension before resuming playback', async () => {
  const f = fixture(); await audible(f); f.engine.setMusicEnabled(true);
  let finishSuspend;
  const pending = new Promise(resolve => { finishSuspend = resolve; });
  f.context.suspend = async () => {
    ++f.context.calls.suspend;
    await pending;
    if (f.context.state !== 'closed') f.context.state = 'suspended';
  };
  f.document.setHidden(true);
  f.document.setHidden(false);
  finishSuspend();
  // Drain chained suspension/resumption promises, without advancing a real clock.
  for (let i = 0; i < 10; ++i) await settled();
  f.timers.tick();
  assert.equal(f.context.state, 'running');
  assert.equal(f.engine.getState().playingMusic, true);
  assert.equal(f.timers.callbacks.size, 1);
  assert.equal(f.engine.playCue('feed'), true);
  f.engine.destroy();
});

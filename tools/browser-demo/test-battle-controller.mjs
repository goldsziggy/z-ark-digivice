#!/usr/bin/env node
// The real shipped controller and WASM, with only browser services and canvas
// drawing mocked. Every fixture follows legal native commands; no saves, HP,
// rewards or RNG fields are fabricated. Playback must never become a command.
import assert from 'node:assert/strict';
import test from 'node:test';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import createDemoCore from '../../docs/play/runtime/demo-core.js';
import { createDeviceView } from '../../docs/play/device-view.js';
import { createDeviceTouchInput } from '../../docs/play/device-touch-input.js';
import { buildBattleFrames, createBattlePlayback } from '../../docs/play/battle-playback.js';
import * as party from '../../docs/play/shared/party.js';
import * as starter from '../../docs/play/shared/starter-onboarding.js';
import * as ring from '../../docs/play/shared/capture-ring.js';

const SAVE_KEY = 'zark.browser-demo.v1.rules15';
const source = await readFile(new URL('../../docs/play/app.js', import.meta.url), 'utf8');
assert.match(source, /\ninit\(\);\s*$/);
const controller = source.replace(/^import .+;\n/gm, '')
  .replace("const {default:createDemoCore}=await import('./runtime/demo-core.js');", 'const createDemoCore=globalThis.runtimeFactory;')
  .replace(/\ninit\(\);\s*$/, `
    globalThis.ui = { init, command, paint, setTab, openCapture, syncSaved,
      finishBattlePlayback, battlePlaybackSample, damagedSave,
      state: () => state, canPlay, captureOpen: () => captureMode,
      active: () => playback.active(), view: () => deviceView,
      snapshot: () => core.ccall('demo_snapshot', 'string', [], []) };
  `);

function eventTarget(target = {}) {
  const listeners = new Map();
  return Object.assign(target, {
    listeners,
    addEventListener(name, callback) {
      if (!listeners.has(name)) listeners.set(name, []);
      listeners.get(name).push(callback);
    },
    removeEventListener(name, callback) {
      listeners.set(name, (listeners.get(name) || []).filter(value => value !== callback));
    },
    async dispatch(name, properties = {}) {
      const event = { type: name, target: this, ...properties,
        preventDefault() { this.defaultPrevented = true; },
        stopImmediatePropagation() { this.stopped = true; } };
      for (const callback of [...(listeners.get(name) || [])]) {
        await callback(event);
        if (event.stopped) break;
      }
      // The storage listener intentionally schedules its lock without returning
      // the promise. Drain the lock/render microtasks, never a wall-clock timer.
      await Promise.resolve(); await Promise.resolve(); await Promise.resolve();
      return event;
    },
  });
}

async function fixture(initialRaw = null, { reducedMotion = false } = {}) {
  let raw = initialRaw, writes = 0, clock = 1000;
  const calls = [], elements = new Map();
  const window = eventTarget();
  const document = eventTarget({ hidden: false, defaultView: window });
  function element(tag = 'div') {
    const texts = [];
    const context = new Proxy({
      measureText: text => ({ width: String(text).length * 6 }),
      fillText(value) { texts.push(String(value)); if (texts.length > 2000) texts.shift(); },
      createLinearGradient: () => ({ addColorStop() {} }),
    }, { get: (target, key) => key in target ? target[key] : () => {} });
    return eventTarget({
      tag, texts, ownerDocument: document, isConnected: true,
      width: 824, height: 824, offsetWidth: 412, clientWidth: 412,
      clientHeight: 412, clientLeft: 0, clientTop: 0,
      textContent: '', innerHTML: '', hidden: false, disabled: false,
      dataset: {}, style: {}, attributes: new Map(), children: [], open: false,
      getContext: () => context,
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 412, height: 412 }),
      setPointerCapture() {}, contains: () => false,
      replaceChildren(...children) { this.children = children; },
      append(...children) { this.children.push(...children); },
      setAttribute(name, value) { this.attributes.set(name, value); },
      focus() {}, showModal() { this.open = true; }, close() { this.open = false; },
    });
  }
  Object.assign(document, {
    getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); },
    createElement: element, querySelectorAll: () => [],
    querySelector: selector => document.getElementById(selector),
  });
  const sandbox = {
    document, window, WebAssembly, console,
    matchMedia: () => ({ matches: reducedMotion }), requestAnimationFrame() {},
    performance: { now: () => clock },
    navigator: { locks: { async request(name, options, callback) {
      assert.equal(name, `${SAVE_KEY}.write`); assert.equal(options.mode, 'exclusive');
      return callback();
    } } },
    localStorage: {
      getItem(key) { assert.equal(key, SAVE_KEY); return raw; },
      setItem(key, value) { assert.equal(key, SAVE_KEY); raw = value; writes++; },
    },
    ...party, ...starter, ...ring, createDeviceView, buildBattleFrames, createBattlePlayback,
    createDeviceTouchInput: (surface, options) => createDeviceTouchInput(surface, { ...options, now: () => clock }),
    createCaptureRingInput: () => ({ refresh() {}, cancel() {} }),
    loadGameArt: async () => ({ prepare: async () => {}, drawForm: () => true,
      drawBackground: () => true, formStatus: () => 'ready' }),
    runtimeFactory: async () => {
      const core = await createDemoCore(), ccall = core.ccall;
      core.ccall = (name, type, types, args) => {
        calls.push({ name, args }); return ccall(name, type, types, args);
      };
      return core;
    },
  };
  vm.runInNewContext(controller, sandbox, { filename: 'app.js' });
  const ui = sandbox.ui;
  await ui.init();
  const mutatingCalls = () => calls.filter(call => ['demo_command', 'demo_reset', 'demo_load'].includes(call.name)).length;
  return {
    ui, document, window, elements, calls, raw: () => raw, writes: () => writes, mutatingCalls,
    async ready(mode = 1) {
      await ui.command('hatch', 2);
      if (!mode) await ui.command('mode', 0);
      await ui.command('demo-encounter');
      ui.view().activate('battle');
      assert.equal(ui.state().phase, 'encounter');
    },
    tick(milliseconds = 100) { clock += milliseconds; ui.paint(clock); },
    sample() { return ui.battlePlaybackSample(clock); },
    finish() {
      for (let i = 0; ui.active() && i < 3000; i++) this.tick(100);
      assert.equal(ui.active(), false, 'A bounded presentation must finish');
    },
    async hide() { document.hidden = true; await document.dispatch('visibilitychange'); },
    async reveal() { document.hidden = false; await document.dispatch('visibilitychange'); },
    async incomingSave(value) { raw = value; await window.dispatch('storage', { key: SAVE_KEY, newValue: value }); },
    async pointer(type, x, y, after = 0) {
      clock += after;
      return document.dispatch(type, { target: elements.get('screen'), pointerId: 9,
        button: 0, isPrimary: true, pointerType: 'touch', clientX: x, clientY: y });
    },
  };
}

test('Auto commits once, reveals both actors, and opens manual capture only after playback', async () => {
  const f = await fixture(); await f.ready();
  const calls = f.mutatingCalls(), writes = f.writes(), sequence = f.ui.state().sequence;
  await f.ui.command('auto-fight');
  assert.equal(f.ui.state().autoCapture, 1);
  assert.equal(f.ui.state().sequence, sequence + 1);
  assert.equal(f.mutatingCalls(), calls + 1);
  assert.equal(f.writes(), writes + 1, 'The canonical result is saved immediately, once');
  const saved = f.raw(), snapshot = f.ui.snapshot();
  assert.equal(f.ui.active(), true);
  assert.equal(f.ui.canPlay(), false);
  assert.equal(f.ui.captureOpen(), false, 'Auto capture waits for the response animation');
  await f.ui.command('auto-resume'); await f.ui.command('attack');
  f.ui.openCapture();
  assert.equal(f.ui.captureOpen(), false, 'Accessible capture entry cannot bypass playback');
  assert.equal(f.mutatingCalls(), calls + 1);
  const actors = new Set();
  for (let i = 0; f.ui.active() && i < 3000; i++) {
    const sample = f.ui.battlePlaybackSample(1000 + i * 100);
    if (sample?.actor) actors.add(sample.actor);
    f.tick();
  }
  assert.equal(f.ui.active(), false);
  assert(actors.size >= 2, `Both native actors must be presented: ${[...actors]}`);
  assert.equal(f.ui.captureOpen(), true);
  assert.equal(f.ui.canPlay(), true);
  assert.equal(f.ui.snapshot(), snapshot);
  assert.equal(f.raw(), saved);
  assert.equal(f.writes(), writes + 1);
  assert.equal(f.mutatingCalls(), calls + 1, 'Frame ticks cannot execute native commands');
});

test('A touch begun during playback cannot commit after playback ends', async () => {
  const f = await fixture(); await f.ready(0);
  await f.ui.command('attack');
  assert.equal(f.ui.active(), true);
  const calls = f.mutatingCalls(), snapshot = f.ui.snapshot();
  // Begin near the end so release is still within the native 40–1500 ms
  // gesture window. Timeout rejection must not mask a stale-input regression.
  for (let i = 0; i < 3000; i++) {
    const sample = f.sample();
    assert(sample, 'The final actor must remain visible before contact');
    if (sample.index === sample.total - 1 && sample.progress >= 0.84) break;
    f.tick(100);
    assert(i < 2999, 'Find the last visible actor');
  }
  await f.pointer('pointerdown', 206, 220);
  assert.equal(f.ui.canPlay(), false);
  f.finish();
  await f.pointer('pointerup', 206, 140, 100);
  assert.equal(f.mutatingCalls(), calls, 'An unarmed contact must not become a post-animation attack');
  assert.equal(f.ui.snapshot(), snapshot);
});

test('Reduced motion retains readable ordered turns and defers capture', async () => {
  const f = await fixture(null, { reducedMotion: true }); await f.ready();
  await f.ui.command('auto-fight');
  assert.equal(f.ui.active(), true, 'Reduced motion must not collapse all turn feedback');
  assert.equal(f.ui.captureOpen(), false);
  const first = f.sample();
  assert.equal(first.actor, 'player');
  f.tick(400);
  assert.equal(f.sample().index, first.index, 'Each actor retains a readable dwell');
  const calls = f.mutatingCalls(), saved = f.raw();
  f.finish();
  assert.equal(f.ui.captureOpen(), true);
  assert.equal(f.mutatingCalls(), calls); assert.equal(f.raw(), saved);
});

test('Navigation settles the committed chunk without opening or replaying capture', async () => {
  const f = await fixture(); await f.ready(); await f.ui.command('auto-fight');
  const saved = f.raw(), calls = f.mutatingCalls(), writes = f.writes();
  f.ui.setTab('box');
  assert.equal(f.ui.active(), false);
  assert.equal(f.ui.captureOpen(), false);
  assert.equal(f.ui.view().page(), 'collection');
  f.tick(120000); f.ui.finishBattlePlayback();
  assert.equal(f.ui.view().page(), 'collection', 'Stale completion cannot override navigation');
  assert.equal(f.raw(), saved); assert.equal(f.writes(), writes); assert.equal(f.mutatingCalls(), calls);
  f.ui.setTab('play'); f.ui.openCapture();
  assert.equal(f.ui.captureOpen(), true, 'The saved capture opportunity remains available');
});

test('Hidden and page-exit cancellation preserve save; reload never repeats the Auto command', async () => {
  for (const event of ['hidden', 'pagehide']) {
    const f = await fixture(); await f.ready(); await f.ui.command('auto-fight');
    const saved = f.raw(), snapshot = f.ui.snapshot(), calls = f.mutatingCalls(), writes = f.writes();
    if (event === 'hidden') await f.hide();
    else await f.window.dispatch('pagehide');
    assert.equal(f.ui.active(), false, `${event}: animation must stop`);
    assert.equal(f.ui.captureOpen(), false, `${event}: capture cannot auto-arm`);
    f.tick(120000); await f.reveal();
    assert.equal(f.raw(), saved); assert.equal(f.ui.snapshot(), snapshot);
    assert.equal(f.writes(), writes); assert.equal(f.mutatingCalls(), calls);
    const restored = await fixture(saved);
    assert.equal(restored.ui.active(), false, 'A reload starts from the durable state, without a trace replay');
    assert.equal(restored.ui.captureOpen(), true, 'The pending manual capture is restored');
    assert.equal(restored.ui.snapshot(), snapshot);
    assert.equal(restored.calls.filter(call => call.name === 'demo_command').length, 0);
    assert.equal(restored.writes(), 0);
  }
});

test('Terminal playback and repeated completion cannot grant a second reward', async () => {
  const f = await fixture(); await f.ready(); await f.ui.command('auto-fight'); f.finish();
  const calls = f.mutatingCalls(), writes = f.writes();
  await f.ui.command('auto-resume');
  assert.equal(f.ui.state().phase, 'home');
  assert.equal(f.ui.active(), true);
  const state = JSON.stringify(f.ui.state()), snapshot = f.ui.snapshot(), saved = f.raw();
  assert.equal(f.mutatingCalls(), calls + 1); assert.equal(f.writes(), writes + 1);
  f.finish(); f.ui.finishBattlePlayback(); f.ui.finishBattlePlayback(); f.tick(120000);
  assert.equal(f.ui.captureOpen(), false);
  assert.equal(f.ui.view().page(), 'result');
  assert.equal(JSON.stringify(f.ui.state()), state);
  assert.equal(f.ui.snapshot(), snapshot); assert.equal(f.raw(), saved);
  assert.equal(f.mutatingCalls(), calls + 1); assert.equal(f.writes(), writes + 1);
});

test('A newer tab save invalidates old playback and its pending capture reveal', async () => {
  const f = await fixture(); await f.ready(); await f.ui.command('auto-fight');
  const other = await fixture(f.raw()); await other.ui.command('ring-capture', 0);
  const newer = other.raw(), snapshot = other.ui.snapshot(), writes = f.writes();
  await f.incomingSave(newer);
  assert.equal(f.ui.active(), false);
  f.tick(120000); f.ui.finishBattlePlayback();
  assert.equal(f.ui.snapshot(), snapshot);
  assert.equal(f.raw(), newer); assert.equal(f.writes(), writes, 'Receiving a save must not rewrite it');
  assert.equal(f.ui.captureOpen(), false, 'Old completion cannot arm a new state');
});

test('Reset and damaged-save recovery invalidate playback without a delayed result', async () => {
  const f = await fixture(); await f.ready(); await f.ui.command('auto-fight');
  await f.elements.get('reset-open').dispatch('click');
  assert.equal(f.ui.active(), false);
  await f.elements.get('reset-confirm').dispatch('click');
  const saved = f.raw(), calls = f.mutatingCalls(), writes = f.writes();
  f.tick(120000); f.ui.finishBattlePlayback();
  assert.equal(f.ui.state().phase, 'egg'); assert.equal(f.ui.captureOpen(), false);
  assert.equal(f.raw(), saved); assert.equal(f.mutatingCalls(), calls); assert.equal(f.writes(), writes);
  await f.ready(); await f.ui.command('auto-fight');
  const committed = f.raw(), beforeError = f.mutatingCalls();
  f.ui.damagedSave(new Error('Unreadable test save'));
  assert.equal(f.ui.active(), false); assert.equal(f.ui.canPlay(), false);
  f.tick(120000); f.ui.finishBattlePlayback();
  assert.equal(f.ui.captureOpen(), false); assert.equal(f.raw(), committed);
  assert.equal(f.mutatingCalls(), beforeError);
});

#!/usr/bin/env node
// Exercise the round-screen presentation against the shipped native WASM.
// Fixtures use legal commands only; no save fields, HP, XP or RNG are patched.
import assert from 'node:assert/strict';
import test from 'node:test';
import createDemoCore from '../../docs/play/runtime/demo-core.js';
import { createDeviceView } from '../../docs/play/device-view.js';
import { sampleCaptureRing } from '../../docs/play/shared/capture-ring.js';

async function harness(initialSnapshot) {
  const core = await createDemoCore();
  const call = (name, values = []) => JSON.parse(core.ccall(name, 'string', values.map(v => typeof v === 'number' ? 'number' : 'string'), values));
  let state = call('demo_reset', [12345]).state, starter = 1, capture = false, busy = false, view;
  if (initialSnapshot) { const loaded = call('demo_load', [initialSnapshot]); assert(loaded.ok); state = loaded.state; }
  const commands = [], notices = [];
  const starters = call('demo_starters').starters;
  function direct(name, value = 0) {
    const result = call('demo_command', [name, value]);
    assert.equal(result.ok, true, `${name}: ${result.error}`);
    state = result.state; view?.sync(); return result;
  }
  const canCapture = () => !busy && state.phase === 'encounter' && state.wildCaptureChance > 0 && (state.battleMode === 'tactical' || state.autoCapture === 1);
  view = createDeviceView({
    getState: () => state, getStarters: () => starters, getStarter: () => starter,
    setStarter: id => { starter = id; }, canPlay: () => !busy, canCapture,
    command(name, value = 0) {
      if (busy) return;
      commands.push([name, value]); capture = false;
      const result = direct(name, value);
      if (name === 'hatch') direct('mode', 1);
      return result;
    },
    isCapture: () => capture,
    openCapture: () => { if (canCapture()) capture = true; },
    closeCapture: () => { capture = false; },
    notify: message => notices.push(message), changed: () => view?.sync(),
  });
  view.sync();
  return {
    view, commands, notices, direct, state: () => state,
    selected: () => starter, selectExternally: id => { starter = id; view.sync(); },
    openCapture() { assert(canCapture()); capture = true; view.sync(); },
    closeCapture() { capture = false; view.sync(); },
    busy(value) { busy = value; view.sync(); },
    snapshot: () => core.ccall('demo_snapshot', 'string', [], []),
    target(id) { const t = view.targets().find(t => t.id === id); assert(t, `Missing ${id} on ${view.page()}`); return t; },
    press(id) { const t = this.target(id); assert(t.enabled, `Disabled ${id} on ${view.page()}`); view.activate(id); },
    hatch() { this.press('choose'); this.press('hatch'); assert.equal(state.phase, 'home'); },
    encounter() { direct('demo-encounter'); assert.equal(view.page(), 'encounter'); this.press('battle'); },
    async captureMembers(count) {
      if (state.phase === 'egg') this.hatch();
      if (state.battleMode !== 'auto') direct('mode', 1);
      for (let round = 0; state.collection.length < count && round < 20; round++) {
        while (state.hp < state.combat.maxHp || state.energy < 75) direct('rest');
        direct('feed'); direct('play'); direct('demo-encounter'); direct('auto-fight');
        while (state.phase === 'encounter' && state.autoCapture === 1 && state.wildCaptureChance > 0) {
          let phase = 0; while (sampleCaptureRing(phase, state.wildFormId).grade !== 'green') phase++;
          direct('ring-capture', phase);
        }
        if (state.phase === 'encounter') direct('auto-resume');
      }
      assert(state.collection.length >= count, 'Legal capture fixture must obtain the requested members');
    },
    trainForEvolution() {
      if (state.phase === 'egg') this.hatch();
      if (state.battleMode !== 'auto') direct('mode', 1);
      for (let i = 0; i < 10; i++) { direct('feed'); direct('play'); }
      for (let round = 0; !state.evolution.options.some(o => o.eligible) && round < 30; round++) {
        while (state.hp < state.combat.maxHp || state.energy < 75) direct('rest');
        direct('demo-encounter'); direct('auto-fight');
        if (state.phase === 'encounter') direct('auto-resume');
      }
      assert(state.evolution.options.some(o => o.eligible), 'Legal training must reach an eligible route');
    },
  };
}

test('starter browse, Back, review and hatch require distinct labeled actions', async () => {
  const h = await harness(), before = h.snapshot();
  assert.equal(h.view.page(), 'starter');
  h.view.horizontal(false); assert.equal(h.selected(), 8);
  h.view.horizontal(true); assert.equal(h.selected(), 1);
  h.press('choose'); assert.equal(h.view.page(), 'starter-review');
  h.view.horizontal(true); assert.equal(h.selected(), 1);
  h.press('back'); assert.equal(h.view.page(), 'starter');
  h.press('back'); assert.equal(h.view.page(), 'egg');
  h.press('meet'); assert.equal(h.view.page(), 'starter');
  assert.equal(h.snapshot(), before, 'Review/navigation cannot mutate the native save');
  h.hatch(); assert.equal(h.state().battleMode, 'auto');
  assert.deepEqual(h.commands, [['hatch', 1]]);
});

test('care screen actions reach native rules and pending input is disabled', async () => {
  const h = await harness(); h.hatch(); h.press('home-open');
  assert.equal(h.view.page(), 'care');
  const sequence = h.state().sequence;
  h.press('feed'); h.press('play'); h.press('rest');
  assert.equal(h.state().sequence, sequence + 3);
  h.busy(true); assert(h.view.targets().every(t => !t.enabled));
  h.view.activate('feed'); assert.equal(h.state().sequence, sequence + 3);
  h.busy(false); h.press('back'); assert.equal(h.view.page(), 'home');
});

test('home panels, settings mode review and nearby notice retain native navigation', async () => {
  const h = await harness(); h.hatch();
  h.view.horizontal(true); assert.equal(h.target('home-open').label, 'PARTNERS');
  h.view.horizontal(true); h.press('home-open'); assert.equal(h.view.page(), 'settings');
  h.press('mode'); h.press('back'); assert.equal(h.state().battleMode, 'auto');
  h.press('mode'); h.press('mode-confirm'); assert.equal(h.state().battleMode, 'tactical');
  assert.equal(h.view.page(), 'settings'); h.press('back');
  h.view.horizontal(true); h.press('home-open'); assert.equal(h.view.page(), 'nearby');
  assert.deepEqual(h.view.targets().map(t => t.id), ['back']);
});

test('manual carousel selection does not attack; separate upward commit does', async () => {
  const h = await harness(); h.hatch(); h.direct('mode', 0); h.encounter();
  const before = h.snapshot();
  assert.equal(h.view.mode(), 'battle'); h.view.horizontal(true);
  assert.match(h.view.description(), /Heavy/); assert.equal(h.snapshot(), before);
  h.view.activate('heavy'); assert.equal(h.snapshot(), before, 'There is no center-tap attack action');
  h.view.battleCommit(); assert.equal(h.commands.at(-1)[0], 'heavy');
  assert.notEqual(h.snapshot(), before);
});

test('manual selection resets to Physical after a native state revision', async () => {
  const h = await harness(); h.hatch(); h.direct('mode', 0); h.encounter();
  h.view.horizontal(true); h.view.battleCommit();
  assert.equal(h.state().phase, 'encounter');
  assert.match(h.view.description(), /Physical/, 'Native update resets selection after a saved exchange');
});

test('Catch opens timing without native mutation, Back closes and cannot spend RNG', async () => {
  const h = await harness(); h.hatch(); h.direct('mode', 0); h.encounter();
  assert.equal(h.target('capture').enabled, false);
  h.view.activate('capture'); assert.equal(h.view.page(), 'battle');
  while (h.state().wildCaptureChance === 0 && h.state().phase === 'encounter') h.direct('magic');
  assert(h.state().wildCaptureChance > 0);
  const before = h.snapshot(); h.press('capture');
  assert.equal(h.view.page(), 'capture'); assert.equal(h.view.mode(), 'capture');
  assert.equal(h.snapshot(), before); h.press('back');
  assert.equal(h.view.page(), 'battle'); assert.equal(h.snapshot(), before);
});

test('Auto pause exposes only Skip/Resume in the capture footer', async () => {
  const h = await harness(); h.hatch(); h.encounter(); h.press('auto-fight');
  assert.equal(h.state().autoCapture, 1);
  // app.js opens the manual capture timing screen after the native Auto result.
  h.openCapture();
  assert.equal(h.view.page(), 'capture');
  assert.deepEqual(h.view.targets().map(t => t.id), ['auto-resume']);
  const footer = h.target('auto-resume');
  assert.deepEqual([footer.x, footer.y, footer.w, footer.h], [104, 348, 204, 38]);
  h.press('auto-resume'); assert.equal(h.commands.at(-1)[0], 'auto-resume');
  assert.equal(h.state().phase, 'home'); assert.equal(h.view.page(), 'result');
});

test('interrupted Auto capture can be reopened from the screen without a command', async () => {
  const h = await harness(); h.hatch(); h.encounter(); h.press('auto-fight');
  const before = h.snapshot(), commandCount = h.commands.length;
  h.press('capture'); assert.equal(h.view.page(), 'capture');
  h.closeCapture(); assert.equal(h.view.page(), 'battle');
  assert.equal(h.target('capture').label, 'AIM CAPTURE');
  assert(!h.view.targets().some(t => t.id === 'auto-fight'));
  h.press('capture'); assert.equal(h.view.page(), 'capture');
  assert.equal(h.commands.length, commandCount); assert.equal(h.snapshot(), before);
});

test('a restored Auto pause has a fresh surface route into capture', async () => {
  const source = await harness(); source.hatch(); source.encounter(); source.press('auto-fight');
  const before = source.snapshot(), restored = await harness(before);
  assert.equal(restored.view.page(), 'encounter');
  assert.equal(restored.target('capture').label, 'AIM CAPTURE');
  assert(!restored.view.targets().some(t => t.id === 'auto-fight'));
  restored.press('capture'); assert.equal(restored.view.page(), 'capture');
  assert.equal(restored.commands.length, 0); assert.equal(restored.snapshot(), before);
});

test('collection partner selection keeps the selected creature visible', async () => {
  const h = await harness(); await h.captureMembers(3);
  h.view.selectTab('box'); h.view.horizontal(true);
  const selected = h.view.artIds()[0];
  h.press('select');
  assert.equal(h.state().formId, selected);
  assert.deepEqual(h.view.artIds(), [selected], 'Reordering active-first collection must retain the viewed identity');
  assert.equal(h.target('select').enabled, false);
});

test('adding an XP companion preserves the viewed member through roster reorder', async () => {
  const h = await harness(); await h.captureMembers(3);
  h.view.selectTab('box'); h.view.horizontal(true); h.view.horizontal(true);
  const selected = h.view.artIds()[0];
  h.press('party');
  assert.deepEqual(h.view.artIds(), [selected], 'Pinning a companion must not silently browse a different creature');
  assert.equal(h.target('party').label, 'REMOVE XP COMPANION');
});

test('a stale release review is canceled and cannot release a changed state', async () => {
  const h = await harness(); await h.captureMembers(2);
  h.view.selectTab('box'); h.view.horizontal(true); h.press('stats'); h.press('release-review');
  h.direct('feed'); const before = h.snapshot();
  h.view.activate('release'); assert.equal(h.snapshot(), before);
  assert.equal(h.view.page(), 'stats', 'Native state revision dismisses a stale release review');
});

test('a stale mode review cannot invert another tab’s new preference', async () => {
  const h = await harness(); h.hatch(); h.view.horizontal(true); h.view.horizontal(true); h.press('home-open'); h.press('mode');
  h.direct('mode', 0); const before = h.snapshot();
  h.view.activate('mode-confirm');
  assert.equal(h.snapshot(), before, 'Stale confirmation must not flip the new mode back to Auto');
  assert.equal(h.view.page(), 'settings');
});

test('starter identity changes invalidate the prior review', async () => {
  const h = await harness(); h.press('choose');
  h.selectExternally(2); const before = h.snapshot(); h.view.activate('hatch');
  assert(h.snapshot() === before, 'Review of starter 1 must not authorize starter 2');
  assert.equal(h.view.page(), 'starter');
});

test('unmet evolution shows requirements and Back returns through stats', async () => {
  const h = await harness(); h.hatch(); h.view.selectTab('box'); h.press('stats'); h.press('evolution');
  const before = h.snapshot(); h.press('evolution-review');
  assert(h.notices.at(-1).includes('Requires level')); assert.equal(h.view.page(), 'evolution');
  assert.equal(h.snapshot(), before); h.press('back'); assert.equal(h.view.page(), 'stats');
});

test('eligible evolution requires a fresh review of the current native state', async () => {
  const h = await harness(); h.trainForEvolution();
  h.view.selectTab('box'); h.press('stats'); h.press('evolution');
  const beforeReview = h.snapshot(), intendedForm = h.view.artIds()[0], memberId = h.state().activeCreatureId;
  h.press('evolution-review'); assert.equal(h.view.page(), 'evolution-review');
  h.press('back'); assert.equal(h.snapshot(), beforeReview, 'Canceling a review cannot evolve');
  h.press('evolution-review'); h.direct('feed');
  const changed = h.snapshot(); h.view.activate('evolve');
  assert.equal(h.view.page(), 'evolution'); assert.equal(h.snapshot(), changed, 'Stale evolution review cannot commit');
  h.press('evolution-review'); h.press('evolve');
  assert.deepEqual(h.commands.at(-1), ['evolve', intendedForm]);
  assert.equal(h.state().formId, intendedForm); assert.equal(h.state().activeCreatureId, memberId);
});

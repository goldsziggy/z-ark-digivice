import test from 'node:test';
import assert from 'node:assert/strict';
import { createCaptureRingInput } from '../web/capture-ring-input.js';

function fixture(t, scale = 1) {
  const document = new EventTarget(), window = new EventTarget(), surface = new EventTarget(), button = new EventTarget();
  document.defaultView = window; document.hidden = false; document.body = {};
  surface.ownerDocument = document; surface.style = { touchAction: 'pan-y' }; surface.isConnected = true;
  surface.contains = target => target === surface; button.contains = target => target === button;
  for (const owner of [surface, button]) owner.setPointerCapture = () => {};
  let clock = 1000, revision = 1, ready = true;
  surface.getBoundingClientRect = () => ({ left: 30, top: 40, width: 412 * scale, height: 412 * scale });
  const samples = [], presses = [], cancelled = [];
  const input = createCaptureRingInput(surface, {
    actionButton: button, canArm: () => ready, getRevision: () => revision, now: () => clock,
    sample: time => { samples.push(time); return { phaseMs: time % 2400, grade: time === 1000 ? 'green' : 'red' }; },
    onPress: value => presses.push(value), onCancel: reason => cancelled.push(reason),
  });
  function send(type, values = {}, target = document) {
    const event = new Event(type, { cancelable: true });
    for (const [key, value] of Object.entries(values)) Object.defineProperty(event, key, { value });
    target.dispatchEvent(event); return event;
  }
  function pointer(type, time = clock, values = {}) {
    clock = time;
    return send(type, { target: surface, pointerId: 1, button: 0, isPrimary: true, clientX: 30 + 206 * scale, clientY: 40 + 176 * scale, ...values });
  }
  const point = (x, y) => ({ clientX: 30 + x * scale, clientY: 40 + y * scale });
  const key = (type, time, values = {}) => { clock = time; return send(type, { target: document.body, code: 'KeyD', key: 'd', ...values }); };
  t.after(() => input.destroy());
  return { input, document, window, surface, button, pointer, point, key, send, samples, presses, cancelled,
    disarm: () => { ready = false; }, arm: () => { ready = true; }, revise: () => { revision++; }, setTime: time => { clock = time; } };
}

test('fresh DOWN anywhere in the round main play area immediately commits, independent of radius and scale', t => {
  for (const scale of [.65, 1, 1.5]) for (const [x, y] of [[206, 80], [206, 330], [30, 206], [382, 206], [206, 176]]) {
    const f = fixture(t, scale);
    assert.equal(f.pointer('pointerdown', 1000, f.point(x, y)).defaultPrevented, true);
    assert.deepEqual(f.samples, [1000]); assert.equal(f.presses.length, 1, 'no release or animation frame is needed');
    f.pointer('pointermove', 1200, f.point(400, 400)); f.pointer('pointerup', 6000); f.pointer('pointerup', 7000);
    assert.equal(f.presses.length, 1); assert.equal(f.presses[0].sample.phaseMs, 1000); assert.equal(f.presses[0].sample.grade, 'green');
  }
});

test('top header, bottom navigation, outside round edge and actual DOM controls remain disjoint', t => {
  const f = fixture(t); const childControl = { closest: () => ({}) }; f.surface.contains = () => true;
  for (const extra of [f.point(206, 79), f.point(206, 331), f.point(0, 80), f.point(410, 329),
    { target: childControl }, { isPrimary: false }, { button: 2 }]) {
    assert.equal(f.pointer('pointerdown', 1000, extra).defaultPrevented, false);
    f.pointer('pointerup', 1100, extra);
  }
  assert.equal(f.presses.length, 0);
});

test('release, drag, second finger or interruption cannot undo or duplicate an already submitted DOWN', t => {
  for (const interrupt of [f => f.pointer('pointercancel', 1100), f => f.send('blur', {}, f.window),
    f => { f.document.hidden = true; f.send('visibilitychange'); }, f => f.input.cancel('navigation'),
    f => f.send('resize', {}, f.window), f => f.pointer('pointerdown', 1100, { pointerId: 2, isPrimary: false })]) {
    const f = fixture(t); f.pointer('pointerdown', 1000); interrupt(f); f.pointer('pointerup', 2000);
    assert.equal(f.presses.length, 1);
  }
});

test('held contact and quick double taps cannot spend a fresh retry across context/presentation changes', t => {
  const f = fixture(t); f.pointer('pointerdown', 1000); f.disarm(); f.revise(); f.arm();
  f.pointer('pointerdown', 1600); assert.equal(f.presses.length, 1, 'same held contact never becomes a new DOWN');
  f.pointer('pointerup', 1700); f.pointer('pointerdown', 1800); assert.equal(f.presses.length, 2);
  f.pointer('pointerup', 1810); f.revise(); f.pointer('pointerdown', 1900); f.pointer('pointerup', 1910);
  assert.equal(f.presses.length, 2, '450ms guard spans retry entries');
  f.pointer('pointerdown', 2250); assert.equal(f.presses.length, 3);
});

test('no input from a contact/key held before capture entry can be inherited', t => {
  const f = fixture(t); f.disarm(); f.pointer('pointerdown', 1000); f.arm();
  f.pointer('pointermove', 1500); f.pointer('pointerup', 1600); assert.equal(f.presses.length, 0);
  f.disarm(); f.key('keydown', 1700); f.arm(); f.key('keydown', 1800, { repeat: true }); f.key('keyup', 1900);
  assert.equal(f.presses.length, 0);
  f.key('keydown', 2000); assert.equal(f.presses.length, 1);
});

test('Action and D commit on DOWN once, including compatibility-click suppression after navigation', t => {
  const f = fixture(t); f.pointer('pointerdown', 1000, { target: f.button }); assert.equal(f.presses.length, 1);
  f.pointer('pointerup', 1100, { target: f.button });
  assert.equal(f.send('click', { target: {}, pointerId: 1, detail: 1 }).defaultPrevented, true);
  f.revise(); f.key('keydown', 1600); f.key('keydown', 1700, { repeat: true }); f.key('keyup', 1800);
  assert.equal(f.presses.length, 2);
});

test('the physical Confirm click that opened capture cannot throw; independent accessibility activation works', t => {
  const f = fixture(t); f.disarm();
  f.pointer('pointerdown', 1000, { target: f.button }); f.pointer('pointerup', 1100, { target: f.button });
  f.arm(); f.send('click', { target: f.button, detail: 1, pointerId: 1 }); assert.equal(f.presses.length, 0);
  f.setTime(1200); f.send('click', { target: f.button, detail: 0 }); assert.equal(f.presses.length, 1);
  f.send('click', { target: f.button, detail: 0 }); assert.equal(f.presses.length, 1);
});

test('existing outside contact, mixed keyboard input, hidden and readonly states block fresh submissions', t => {
  for (const block of [f => f.disarm(), f => { f.document.hidden = true; },
    f => f.pointer('pointerdown', 800, { pointerId: 2, target: {} }),
    f => { f.disarm(); f.key('keydown', 800); f.arm(); }]) {
    const f = fixture(t); block(f); f.pointer('pointerdown', 1000); f.pointer('pointerup', 1200);
    assert.equal(f.presses.length, 0);
  }
});

test('captured descendant blur is not a window interruption, genuine window blur stays blocked until focus', t => {
  const f = fixture(t); f.send('blur', { target: f.button }, f.window); f.pointer('pointerdown', 1000);
  assert.equal(f.presses.length, 1); f.pointer('pointerup', 1100); f.revise();
  f.send('blur', {}, f.window); f.send('focus', { target: f.surface }, f.window);
  f.pointer('pointerdown', 1600); f.pointer('pointerup', 1700); assert.equal(f.presses.length, 1);
  f.send('focus', {}, f.window); f.pointer('pointerdown', 1800); assert.equal(f.presses.length, 2);
});

test('invalid event geometry and clock cannot submit or consume a capture', t => {
  const f = fixture(t); f.pointer('pointerdown', 1000, { clientX: NaN }); f.pointer('pointerup');
  f.pointer('pointerdown', NaN); f.pointer('pointerup'); assert.equal(f.presses.length, 0);
  f.pointer('pointerdown', 1000); assert.equal(f.presses.length, 1);
});


test('canceled capture contact cannot swallow a new navigation click using the same mouse ID', t => {
  const f = fixture(t); f.pointer('pointerdown', 1000); f.pointer('pointercancel', 1100); f.disarm();
  f.pointer('pointerdown', 1600, { target: {} }); f.pointer('pointerup', 1700, { target: {} });
  assert.equal(f.send('click', { target: {}, pointerId: 1, detail: 1 }).defaultPrevented, false);
});

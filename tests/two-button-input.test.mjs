import test from 'node:test';
import assert from 'node:assert/strict';
import { setupTwoButtonInput } from '../web/two-button-input.js';

function fixture(t) {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const document = new EventTarget(); document.hidden = false; document.defaultView = new EventTarget();
  const left = new EventTarget(), right = new EventTarget(), calls = [];
  for (const button of [left, right]) { button.ownerDocument = document; button.disabled = false; button.setPointerCapture = () => {}; }
  let backAllowed = true;
  const input = setupTwoButtonInput({ left, right, onNext: () => calls.push('next'), onBack: () => calls.push('back'), onConfirm: () => calls.push('confirm'), canBack: () => backAllowed });
  function send(target, type, properties = {}) {
    const event = new Event(type, { cancelable: true });
    for (const [key, value] of Object.entries(properties)) Object.defineProperty(event, key, { value });
    target.dispatchEvent(event); return event;
  }
  const pointer = (button, type, extra = {}) => send(button, type, { pointerId: 1, button: 0, isPrimary: true, ...extra });
  const key = (type, value, extra = {}) => send(document, type, { key: value, code: `Key${value.toUpperCase()}`, ...extra });
  const release = button => { pointer(button, 'pointerup'); send(button, 'click'); };
  t.after(() => input.destroy());
  return { document, left, right, calls, input, send, pointer, key, release, blockBack: () => { backAllowed = false; } };
}

test('left tap selects once; hold goes Back once and never becomes Next on release', t => {
  const f = fixture(t);
  f.pointer(f.left, 'pointerdown'); t.mock.timers.tick(599); f.release(f.left);
  assert.deepEqual(f.calls, ['next']);
  f.pointer(f.left, 'pointerdown'); t.mock.timers.tick(600); t.mock.timers.tick(2400);
  assert.deepEqual(f.calls, ['next', 'back']); f.release(f.left);
  assert.deepEqual(f.calls, ['next', 'back']);
});

test('blocked Back still consumes a long press without selecting an item', t => {
  const f = fixture(t); f.blockBack();
  f.pointer(f.left, 'pointerdown'); t.mock.timers.tick(600); f.release(f.left);
  assert.deepEqual(f.calls, []);
});

test('right confirms only on release and suppresses the following native click', t => {
  const f = fixture(t);
  f.pointer(f.right, 'pointerdown'); t.mock.timers.tick(5000); assert.deepEqual(f.calls, []);
  f.release(f.right); assert.deepEqual(f.calls, ['confirm']);
  f.send(f.right, 'click'); assert.deepEqual(f.calls, ['confirm', 'confirm'], 'standalone accessibility click remains usable');
});

test('navigation cancellation prevents release from executing a newly selected command', t => {
  const f = fixture(t);
  for (const button of [f.left, f.right]) {
    f.pointer(button, 'pointerdown'); f.input.cancel(); t.mock.timers.tick(600); f.release(button);
  }
  assert.deepEqual(f.calls, []);
  f.pointer(f.right, 'pointerdown'); f.release(f.right); assert.deepEqual(f.calls, ['confirm']);
  f.pointer(f.left, 'pointerdown'); t.mock.timers.tick(600); f.input.cancel(); f.release(f.left);
  assert.deepEqual(f.calls, ['confirm', 'back']);
});

test('pointer cancel, lost capture, blur and hidden pages cannot leave live gestures', t => {
  const f = fixture(t);
  for (const cancel of [() => f.pointer(f.left, 'pointercancel'), () => f.pointer(f.left, 'lostpointercapture'),
    () => f.send(f.document.defaultView, 'blur'), () => { f.document.hidden = true; f.send(f.document, 'visibilitychange'); }]) {
    f.pointer(f.left, 'pointerdown'); cancel(); t.mock.timers.tick(600); f.document.hidden = false; f.release(f.left);
  }
  assert.deepEqual(f.calls, []);
});

test('A/D keys mirror tap and hold, ignore repeats and leave accessibility clicks usable', t => {
  const f = fixture(t);
  assert.equal(f.key('keydown', 'a').defaultPrevented, true); f.key('keydown', 'a', { repeat: true }); f.key('keyup', 'a');
  assert.deepEqual(f.calls, ['next']);
  f.key('keydown', 'a'); t.mock.timers.tick(600); f.key('keydown', 'a', { repeat: true }); f.key('keyup', 'a');
  f.key('keydown', 'd'); t.mock.timers.tick(1000); f.key('keydown', 'd', { repeat: true });
  assert.deepEqual(f.calls, ['next', 'back']); f.key('keyup', 'd');
  assert.deepEqual(f.calls, ['next', 'back', 'confirm']);
  f.send(f.right, 'click'); assert.deepEqual(f.calls, ['next', 'back', 'confirm', 'confirm']);
});

test('editable fields, composition and modified keys never become device commands', t => {
  const f = fixture(t);
  for (const extra of [{ target: { isContentEditable: true } }, { target: { closest: () => ({}) } }, { isComposing: true }, { ctrlKey: true }, { altKey: true }, { metaKey: true }, { shiftKey: true }]) {
    assert.equal(f.key('keydown', 'a', extra).defaultPrevented, false); t.mock.timers.tick(600); f.key('keyup', 'a', extra);
  }
  assert.deepEqual(f.calls, []);
  f.key('keydown', 'd'); f.key('keyup', 'd', { target: { isContentEditable: true } }); assert.deepEqual(f.calls, []);
});

test('disabled controls, extra pointers and overlapping keys cannot execute commands', t => {
  const f = fixture(t);
  f.pointer(f.right, 'pointerdown'); f.right.disabled = true; f.release(f.right); f.right.disabled = false;
  f.pointer(f.left, 'pointerdown'); f.pointer(f.right, 'pointerdown', { pointerId: 2 });
  f.key('keydown', 'd'); f.key('keyup', 'd'); f.pointer(f.right, 'pointerup', { pointerId: 2 }); f.send(f.right, 'click');
  f.release(f.left); assert.deepEqual(f.calls, ['next']);
  f.pointer(f.right, 'pointerdown', { isPrimary: false }); f.pointer(f.right, 'pointerup', { isPrimary: false });
  assert.deepEqual(f.calls, ['next']);
});

test('focused Enter/Space native keyboard lifecycle confirms once, never from repeats', t => {
  const f = fixture(t);
  for (const [key, code, button] of [['Enter', 'Enter', f.right], [' ', 'Space', f.left]]) {
    assert.equal(f.key('keydown', key, { code, target: button }).defaultPrevented, true);
    assert.equal(f.key('keydown', key, { code, target: button, repeat: true }).defaultPrevented, true);
    assert.equal(f.key('keyup', key, { code, target: button }).defaultPrevented, true);
  }
  assert.deepEqual(f.calls, ['confirm', 'next']);
});

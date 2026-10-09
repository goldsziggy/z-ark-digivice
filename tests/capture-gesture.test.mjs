import test from 'node:test';
import assert from 'node:assert/strict';
import { createCaptureGesture } from '../web/capture-gesture.js';

function fixture(t, { scale = 1, captureFails = false } = {}) {
  const document = new EventTarget(), window = new EventTarget(), surface = new EventTarget();
  document.hidden = false; document.defaultView = window;
  surface.ownerDocument = document; surface.isConnected = true; surface.style = { touchAction: 'pan-y' };
  let armed = true, revision = 7, rect = { left: 30, top: 45, width: 412 * scale, height: 412 * scale }, observer;
  window.ResizeObserver = class {
    constructor(callback) { observer = this; this.callback = callback; }
    observe(value) { this.target = value; }
    disconnect() { this.disconnected = true; }
  };
  const released = [], cancelled = [], previews = [], captures = [];
  surface.getBoundingClientRect = () => ({ ...rect });
  surface.setPointerCapture = id => { if (captureFails) throw new Error('removed'); captures.push(id); };
  surface.releasePointerCapture = id => { send(surface, 'lostpointercapture', { pointerId: id }); };
  const gesture = createCaptureGesture(surface, {
    canArm: () => armed, getRevision: () => revision,
    onPreview: value => previews.push(value), onRelease: value => released.push(value), onCancel: reason => cancelled.push(reason),
  });
  function send(target, type, properties = {}) {
    const event = new Event(type, { cancelable: true });
    for (const [key, value] of Object.entries(properties)) Object.defineProperty(event, key, { value });
    target.dispatchEvent(event); return event;
  }
  function pointer(type, x = 206, y = 300, timeStamp = 0, extra = {}, target = surface) {
    const properties = { pointerId: 1, pointerType: 'touch', button: 0, isPrimary: true,
      clientX: rect.left + x * scale, clientY: rect.top + y * scale, timeStamp, ...extra };
    // Node EventTarget has no capture/bubble tree. Explicitly deliver the document
    // capture phase before the surface, as the browser does for these events.
    if (['pointerdown', 'pointerup', 'pointercancel'].includes(type)) send(document, type, properties);
    return send(target, type, properties);
  }
  t.after(() => gesture.destroy());
  return { document, window, surface, gesture, released, cancelled, previews, captures, send, pointer,
    disarm: () => { armed = false; }, changeRevision: () => { revision++; },
    resize: () => { rect.width -= 2; observer.callback(); },
    observer: () => observer };
}

test('an upward touch flick captures the pointer and emits exactly one bounded release', t => {
  const f = fixture(t);
  assert.equal(f.pointer('pointerdown').defaultPrevented, true);
  f.pointer('pointermove', 212, 258, 40);
  f.pointer('pointerup', 218, 216, 80);
  f.pointer('pointerup', 218, 216, 90);
  f.send(f.surface, 'click');
  assert.deepEqual(f.captures, [1]); assert.equal(f.released.length, 1);
  const release = f.released[0];
  assert.equal(release.revision, 7); assert.equal(release.durationMs, 80);
  assert.ok(release.vy < 0 && release.vx > 0);
  assert.ok(Math.abs(release.vy + 84 / 412 / 0.08) < 1e-10);
  assert.deepEqual(f.cancelled, []);
  assert.deepEqual(f.previews.map(p => p.phase), ['start', 'move']);
});

test('off-center upward releases reach the caller so authoritative gameplay can debit misses', t => {
  const f = fixture(t);
  f.pointer('pointerdown'); f.pointer('pointermove', 267, 269, 30); f.pointer('pointerup', 330, 220, 60);
  assert.equal(f.released.length, 1); assert.ok(f.released[0].vx > 2);
});

test('holding before a flick does not dilute its final velocity', t => {
  const quick = fixture(t), held = fixture(t);
  quick.pointer('pointerdown'); quick.pointer('pointerup', 206, 250, 50);
  held.pointer('pointerdown'); held.pointer('pointermove', 206, 300, 2000); held.pointer('pointerup', 206, 250, 2050);
  assert.equal(held.released.length, 1);
  assert.ok(held.released[0].vy < -1, 'recent motion remains a useful flick after a long hold');
  assert.ok(held.released[0].vy <= quick.released[0].vy / 2 + 1e-10);
});

test('an optional backstroke followed by a flick can finish near the starting ball', t => {
  const f = fixture(t);
  f.pointer('pointerdown'); f.pointer('pointermove', 207, 336, 40); f.pointer('pointerup', 208, 298, 80);
  assert.equal(f.released.length, 1); assert.ok(f.released[0].vy < -2);
});

test('short taps, tiny motions, downward drags, and holding still before release do not throw', t => {
  for (const motion of [
    f => f.pointer('pointerup', 206, 300, 80),
    f => f.pointer('pointerup', 207, 292, 40),
    f => f.pointer('pointerup', 206, 330, 50),
    f => { f.pointer('pointermove', 206, 200, 100); f.pointer('pointerup', 206, 200, 400); },
    f => f.pointer('pointerup', 206, 285, 500),
  ]) {
    const f = fixture(t); f.pointer('pointerdown'); motion(f);
    assert.equal(f.released.length, 0); assert.deepEqual(f.cancelled, ['short-or-downward']);
  }
});

test('no backstroke is required and the same gesture scales with the screen', t => {
  const releases = [];
  for (const scale of [1, 0.72, 1.5]) {
    const f = fixture(t, { scale }); f.pointer('pointerdown'); f.pointer('pointerup', 210, 250, 80);
    assert.equal(f.released.length, 1); releases.push(f.released[0]);
  }
  for (const value of releases.slice(1)) {
    assert.ok(Math.abs(value.vx - releases[0].vx) < 1e-12);
    assert.ok(Math.abs(value.vy - releases[0].vy) < 1e-12);
  }
});

test('touches outside the ball, non-primary touches and secondary mouse buttons leave navigation alone', t => {
  const f = fixture(t);
  for (const [x, y, extra] of [[206, 360, {}], [20, 20, {}], [206, 300, { isPrimary: false }], [206, 300, { button: 2 }]]) {
    assert.equal(f.pointer('pointerdown', x, y, 0, extra).defaultPrevented, false);
    f.pointer('pointerup', 206, 200, 80, extra);
  }
  assert.equal(f.released.length, 0); assert.deepEqual(f.captures, []);
});

test('a footer control overlapping the ball region keeps its own touch interaction', t => {
  const f = fixture(t), target = { closest: () => ({ tagName: 'BUTTON' }) };
  assert.equal(f.pointer('pointerdown', 206, 310, 0, { target }).defaultPrevented, false);
  f.pointer('pointerup', 206, 240, 80, { target });
  assert.equal(f.released.length, 0); assert.equal(f.captures.length, 0);
});

test('a second finger, including one elsewhere in the document, cancels the first gesture', t => {
  for (const outside of [false, true]) {
    const f = fixture(t); f.pointer('pointerdown');
    f.pointer('pointerdown', 210, 300, 20, { pointerId: 2, isPrimary: false }, outside ? new EventTarget() : f.surface);
    f.pointer('pointerup', 206, 230, 60);
    f.pointer('pointerup', 210, 200, 80, { pointerId: 2, isPrimary: false });
    assert.equal(f.released.length, 0); assert.deepEqual(f.cancelled, ['multitouch']);
    f.pointer('pointerdown', 206, 300, 100); f.pointer('pointerup', 206, 230, 170);
    assert.equal(f.released.length, 1, 'a new independent gesture remains available');
  }
});

test('a second finger cannot arm a new gesture while the cancelled first finger is still down', t => {
  const f = fixture(t); f.pointer('pointerdown');
  f.pointer('pointerdown', 206, 300, 10, { pointerId: 2, isPrimary: false });
  f.pointer('pointerup', 206, 260, 20, { pointerId: 2, isPrimary: false });
  f.pointer('pointerdown', 206, 300, 30, { pointerId: 3, isPrimary: true });
  f.pointer('pointerup', 206, 220, 90, { pointerId: 3, isPrimary: true });
  assert.equal(f.released.length, 0); assert.equal(f.captures.length, 1);
});

test('cancel, lost capture, leaving the circle, resize, blur and hidden pages cannot throw', t => {
  const interruptions = [
    f => f.pointer('pointercancel', 206, 270, 20),
    f => f.pointer('lostpointercapture', 206, 270, 20),
    f => f.pointer('pointermove', 400, 20, 20), // Inside rectangle, outside circle.
    f => f.send(f.window, 'resize'),
    f => f.resize(),
    f => f.send(f.window, 'blur'),
    f => { f.document.hidden = true; f.send(f.document, 'visibilitychange'); },
    f => f.gesture.cancel('navigation'),
  ];
  for (const interrupt of interruptions) {
    const f = fixture(t); f.pointer('pointerdown'); interrupt(f); f.pointer('pointerup', 206, 220, 80);
    assert.equal(f.released.length, 0); assert.equal(f.cancelled.length, 1);
  }
});

test('pause, removed surfaces, and revision changes are checked before release and by refresh', t => {
  for (const immediate of [false, true]) for (const mutate of [f => f.disarm(), f => f.changeRevision(), f => { f.surface.isConnected = false; }]) {
    const f = fixture(t); f.pointer('pointerdown'); mutate(f);
    if (immediate) { f.gesture.refresh(); assert.equal(f.cancelled.length, 1); }
    f.pointer('pointerup', 206, 220, 80);
    assert.equal(f.released.length, 0); assert.equal(f.cancelled.length, 1);
  }
});

test('disabled or hidden surfaces never arm; failed pointer capture is safely cancelled', t => {
  for (const disable of [f => f.disarm(), f => { f.document.hidden = true; }, f => { f.surface.disabled = true; }]) {
    const f = fixture(t); disable(f); f.pointer('pointerdown'); f.pointer('pointerup', 206, 220, 80);
    assert.equal(f.captures.length, 0); assert.equal(f.released.length, 0);
  }
  const f = fixture(t, { captureFails: true }); f.pointer('pointerdown'); f.pointer('pointerup', 206, 220, 80);
  assert.equal(f.released.length, 0); assert.deepEqual(f.cancelled, ['capture-unavailable']);
});

test('refresh restores menu scrolling when capture is unavailable, even without an active touch', t => {
  const f = fixture(t);
  assert.equal(f.surface.style.touchAction, 'none');
  f.disarm(); f.gesture.refresh();
  assert.equal(f.surface.style.touchAction, 'pan-y');
});

test('releasing a secondary mouse button cannot finish an active primary drag', t => {
  const f = fixture(t);
  f.pointer('pointerdown', 206, 300, 0, { pointerType: 'mouse' });
  f.pointer('pointerup', 206, 250, 40, { button: 2, pointerType: 'mouse' });
  assert.equal(f.released.length, 0);
  f.pointer('pointerup', 206, 220, 70, { pointerType: 'mouse' });
  assert.equal(f.released.length, 1);
});

test('invalid samples and excessively long drags cancel rather than producing unbounded input', t => {
  for (const [x, y, time] of [[NaN, 250, 40], [206, 250, -1], [206, 250, 10001]]) {
    const f = fixture(t); f.pointer('pointerdown'); f.pointer('pointerup', x, y, time);
    assert.equal(f.released.length, 0); assert.equal(f.cancelled.length, 1);
  }
  const f = fixture(t); f.pointer('pointerdown'); f.pointer('pointerup', 300, 170, 1);
  assert.equal(f.released.length, 1); assert.equal(f.released[0].vx, 4); assert.equal(f.released[0].vy, -4);
});

test('coalesced movement samples preserve a recent backstroke and flick', t => {
  const f = fixture(t); f.pointer('pointerdown');
  f.pointer('pointermove', 206, 280, 100, { getCoalescedEvents: () => [
    { clientX: 236, clientY: 375, timeStamp: 40 },
    { clientX: 236, clientY: 355, timeStamp: 70 },
  ] });
  f.pointer('pointerup', 206, 260, 120);
  assert.equal(f.released.length, 1); assert.ok(f.released[0].vy < -2);
});

test('destroy releases input ownership, restores touch action and removes all listeners', t => {
  const f = fixture(t); f.pointer('pointerdown'); f.gesture.destroy(); f.gesture.destroy();
  assert.equal(f.surface.style.touchAction, 'pan-y'); assert.equal(f.observer().disconnected, true);
  assert.deepEqual(f.cancelled, ['destroyed']);
  f.pointer('pointerup', 206, 230, 50); f.pointer('pointerdown', 206, 300, 60); f.pointer('pointerup', 206, 230, 120);
  f.send(f.window, 'resize'); assert.equal(f.released.length, 0); assert.equal(f.captures.length, 1);
});

import test from 'node:test';
import assert from 'node:assert/strict';
import { createDeviceNavigation, handleDeviceKey, DEVICE_SCREENS } from '../web/device-navigation.js';

function fixture() {
  const menus = {
    home: [{ id: 'open-menu' }],
    menu: [{ id: 'care' }, { id: 'companions' }, { id: 'explore' }, { id: 'settings' }],
    care: [{ id: 'feed' }, { id: 'play' }, { id: 'rest' }],
    settings: [{ id: 'sound' }, { id: 'artwork' }, { id: 'connection' }],
    companions: [{ id: 'companion-1' }, { id: 'companion-2' }, { id: 'companion-3' }],
    battle: [{ id: 'attack' }, { id: 'capture', disabled: true }],
  };
  const disabledScreens = new Set();
  const changed = [];
  const nav = createDeviceNavigation({ getItems: screen => menus[screen] ?? [], canVisit: screen => !disabledScreens.has(screen), onChange: state => changed.push(state) });
  return { nav, menus, disabledScreens, changed };
}

test('nested Back restores per-screen focus while replacements do not grow history', () => {
  const { nav } = fixture();
  nav.go('menu'); nav.select(3); nav.go('settings'); nav.select(1); nav.go('artwork');
  assert.equal(nav.state().depth, 3);
  assert.equal(nav.back(), 'back'); assert.equal(nav.state().screen, 'settings'); assert.equal(nav.selected().id, 'artwork');
  nav.go('connection'); nav.setScreen('sound'); assert.equal(nav.state().depth, 3);
  nav.back(); nav.back(); assert.equal(nav.selected().id, 'settings');
  nav.go('care'); nav.select(2); nav.back(); nav.go('care'); assert.equal(nav.selected().id, 'rest');
  nav.go('care'); assert.equal(nav.state().depth, 2, 'repeated screen entry never adds a duplicate');
});

test('history is bounded and repeated Back safely ends at home', () => {
  const { nav } = fixture();
  for (let index = 0; index < 100; index++) nav.go(index % 2 ? 'care' : 'menu');
  assert.equal(nav.state().depth, 12);
  for (let index = 0; index < 20; index++) nav.back();
  assert.deepEqual(nav.state(), { screen: 'home', index: 0, selectedId: 'open-menu', depth: 0, canBack: false });
  assert.equal(nav.back(), 'home'); assert.equal(nav.go('arbitrary-screen'), false);
  assert.equal(nav.go('__proto__'), false);
});

test('modal Back requests cancellation only, and pending saves always block it', () => {
  const { nav, changed } = fixture();
  nav.go('menu'); nav.go('battle');
  const before = nav.snapshot(), calls = changed.length;
  assert.equal(nav.back({ captureWindup: true }), 'cancel-capture');
  assert.equal(nav.back({ captureWindup: true }), 'cancel-capture');
  assert.equal(nav.back({ captureWindup: true, pendingSync: true }), 'blocked');
  assert.equal(nav.back({ pendingSync: true }), 'blocked');
  assert.deepEqual(nav.snapshot(), before); assert.equal(changed.length, calls);
  assert.equal(nav.back(), 'back'); assert.equal(nav.state().screen, 'menu');
});

test('dynamic context keeps item identity, clamps vanished focus and removes inaccessible screens', () => {
  const { nav, menus, disabledScreens } = fixture();
  nav.go('menu'); nav.go('companions'); nav.select(1); nav.go('companion');
  menus.companions = [{ id: 'companion-3' }, { id: 'companion-1' }, { id: 'companion-2' }];
  nav.back(); assert.equal(nav.state().index, 2); assert.equal(nav.selected().id, 'companion-2');
  menus.companions = [{ id: 'companion-1' }]; nav.refresh();
  assert.equal(nav.state().index, 0); assert.equal(nav.selected().id, 'companion-1');
  nav.go('companion'); disabledScreens.add('companion'); disabledScreens.add('companions');
  nav.refresh(); assert.equal(nav.state().screen, 'menu'); assert.equal(nav.state().depth, 1);
  disabledScreens.add('menu'); nav.refresh(); assert.equal(nav.state().screen, 'home');
});

test('selection skips disabled items and rejects stale or malformed touch targets', () => {
  const { nav, menus } = fixture();
  nav.go('care'); menus.care[1].disabled = true;
  nav.move(1); assert.equal(nav.selected().id, 'rest');
  nav.move(1); assert.equal(nav.selected().id, 'feed');
  nav.move(-1); assert.equal(nav.selected().id, 'rest');
  assert.equal(nav.select(1), false); assert.equal(nav.select(8), false); assert.equal(nav.select(NaN), false);
  assert.equal(nav.move(Infinity), false);
  menus.care = [{ id: 'only', disabled: true }]; nav.refresh();
  assert.equal(nav.state().index, -1); assert.equal(nav.selected(), null); assert.equal(nav.move(1), false);
  menus.care = [{ id: 'duplicate' }, { id: 'duplicate' }]; assert.equal(nav.selected(), null);
  menus.care = Array.from({ length: 33 }, (_, index) => ({ id: String(index) })); assert.equal(nav.selected(), null);
});

test('valid navigation snapshots restore focus independently of identity and clamp stale context', () => {
  const { nav } = fixture(); nav.go('menu'); nav.select(1); nav.go('companions'); nav.select(2);
  const serialized = JSON.stringify(nav.snapshot());
  const restored = fixture(); restored.menus.companions = [{ id: 'companion-1' }];
  assert.equal(restored.nav.restore(serialized), true); assert.equal(restored.nav.selected().id, 'companion-1');
  restored.nav.back(); assert.equal(restored.nav.selected().id, 'companions');
  const data = restored.nav.snapshot(); data.stack.push('sound'); data.focus.menu.id = 'tampered';
  assert.equal(restored.nav.state().depth, 1); assert.equal(restored.nav.selected().id, 'companions', 'snapshots do not expose mutable internals');
  assert.deepEqual(Object.keys(JSON.parse(serialized)).sort(), ['focus', 'screen', 'stack', 'version']);
});

test('corrupt or oversized saved navigation returns home without touching identity storage', () => {
  const nav = fixture().nav;
  let storageAccess = 0;
  const descriptor = Object.getOwnPropertyDescriptor(globalThis, 'localStorage');
  Object.defineProperty(globalThis, 'localStorage', { configurable: true, get() { storageAccess++; throw new Error('Navigation must not access storage'); } });
  try {
    for (const invalid of [null, '{', ' '.repeat(8193), { version: 99 },
      { version: 1, screen: 'care', stack: [], focus: {}, token: 'must-not-be-used' },
      { version: 1, screen: 'care', stack: Array(13).fill('menu'), focus: {} },
      { version: 1, screen: 'care', stack: [], focus: { care: { id: 'feed', index: 32 } } },
      { version: 1, screen: 'care', stack: [], focus: { unknown: { id: 'feed', index: 0 } } },
    ]) {
      nav.go('care'); assert.equal(nav.restore(invalid), false); assert.equal(nav.state().screen, 'home'); assert.equal(nav.state().depth, 0);
    }
    assert.equal(storageAccess, 0);
  } finally {
    if (descriptor) Object.defineProperty(globalThis, 'localStorage', descriptor);
    else delete globalThis.localStorage;
  }
});

test('keyboard navigation ignores editing, modifiers, repeated commands and composing input', () => {
  const { nav } = fixture(); nav.go('care');
  const confirms = []; let backs = 0;
  function press(key, extras = {}) {
    let prevented = false;
    const event = { key, target: { closest: () => null, isContentEditable: false }, preventDefault() { prevented = true; }, ...extras };
    const handled = handleDeviceKey(event, nav, { confirm: item => confirms.push(item.id), back: () => { backs++; } });
    return { handled, prevented };
  }
  for (const extras of [{ ctrlKey: true }, { metaKey: true }, { altKey: true }, { shiftKey: true }, { isComposing: true },
    { defaultPrevented: true }, { target: { isContentEditable: true } }, { target: { closest: () => ({ tagName: 'INPUT' }) } }]) {
    assert.equal(press('ArrowDown', extras).handled, false);
  }
  assert.equal(nav.selected().id, 'feed');
  assert.deepEqual(press('ArrowDown', { repeat: true }), { handled: true, prevented: true });
  assert.deepEqual(press('Enter'), { handled: true, prevented: true }); assert.deepEqual(confirms, ['play']);
  for (const key of ['Enter', 'Escape', 'Backspace']) assert.deepEqual(press(key, { repeat: true }), { handled: true, prevented: true });
  assert.deepEqual(confirms, ['play']); assert.equal(backs, 0);
  press('Escape'); press('Backspace'); assert.equal(backs, 2);
  assert.equal(press('Tab').handled, false); assert.equal(press('x').handled, false);
});

test('reserved battle and pending presentation routes contain no game behavior', () => {
  const nav = createDeviceNavigation();
  for (const route of ['battle-mode', 'capture', 'saving', 'save-confirm']) {
    assert.ok(DEVICE_SCREENS.includes(route)); assert.equal(nav.setScreen(route), true); assert.equal(nav.selected(), null);
  }
  assert.equal(nav.state().depth, 0);
});

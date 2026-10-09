// Presentation state only. The app owns commands, identities, pending saves,
// capture cancellation and the items currently available on each screen.
export const DEVICE_SCREENS = Object.freeze([
  'home', 'starter-select', 'starter-review', 'starter-hatched', 'menu', 'care', 'companions', 'companion', 'explore', 'battle', 'capture', 'capture-aim', 'battle-mode',
  'wild-mode', 'wild-auto-confirm', 'wild-auto-progress', 'wild-auto-result', 'battle-select-mode', 'battle-auto-confirm', 'battle-auto-progress', 'battle-auto-result',
  'progression', 'evolution-options', 'evolution-preview', 'evolution-skills', 'evolution-requirements', 'evolution-confirm', 'evolution-result', 'evolution-tree', 'evolution-node', 'evolution-links', 'evolution-node-skills',
  'release-review', 'release-confirm', 'release-result', 'roster', 'roster-filters', 'roster-letters', 'roster-stages', 'roster-detail', 'roster-stats', 'roster-growth', 'roster-moves', 'roster-notes', 'journal',
  'cards', 'settings', 'sound', 'artwork', 'asset-packs', 'asset-progress', 'connection', 'saves', 'recover-confirm',
  'stats', 'skills', 'type-chart', 'practice-stats', 'recovery', 'recovery-confirm', 'battle-recovery', 'battle-recovery-confirm', 'mode-placeholder', 'save-confirm', 'saving', 'battle-choice', 'battle-resolve', 'battle-result', 'battle-cards',
]);
const SCREENS = new Set(DEVICE_SCREENS);
const MAX_DEPTH = 12, MAX_ITEMS = 32, MAX_SNAPSHOT = 8192;
const object = value => !!value && typeof value === 'object' && !Array.isArray(value);
const validId = value => typeof value === 'string' && value.length > 0 && value.length <= 96;
const exact = (value, keys) => object(value) && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));

export function createDeviceNavigation({ getItems = () => [], canVisit = () => true, onChange = () => {}, initialScreen = 'home' } = {}) {
  let screen = 'home', stack = [], focus = Object.create(null);
  function allowed(value) {
    if (!SCREENS.has(value)) return false;
    if (value === 'home') return true;
    try { return canVisit(value) === true; } catch { return false; }
  }
  function items(value = screen) {
    try {
      const result = getItems(value);
      if (!Array.isArray(result) || result.length > MAX_ITEMS) return [];
      const ids = new Set();
      for (const item of result) {
        if (!object(item) || !validId(item.id) || ids.has(item.id)) return [];
        ids.add(item.id);
      }
      return result;
    } catch { return []; }
  }
  function selection(value = screen, list = items(value)) {
    const remembered = focus[value];
    let index = remembered ? list.findIndex(item => item.id === remembered.id && !item.disabled) : -1;
    if (index < 0) {
      const start = Math.max(0, Math.min(remembered?.index ?? 0, list.length - 1));
      index = list.findIndex((item, position) => position >= start && !item.disabled);
      if (index < 0) for (let position = start - 1; position >= 0; position--) {
        if (!list[position].disabled) { index = position; break; }
      }
    }
    return { index, id: index >= 0 ? list[index].id : null };
  }
  function remember() { focus[screen] = selection(); }
  function state() {
    const current = selection();
    return { screen, index: current.index, selectedId: current.id, depth: stack.length, canBack: screen !== 'home' || stack.length > 0 };
  }
  function emit() { onChange(state()); }
  function refresh(notify = true) {
    while (!allowed(screen) && stack.length) screen = stack.pop();
    if (!allowed(screen)) screen = 'home';
    stack = stack.filter(allowed);
    remember();
    if (notify) emit();
    return state();
  }
  function setScreen(next, { replace = true } = {}) {
    if (!allowed(next)) return false;
    if (next === screen) { refresh(); return true; }
    remember();
    if (!replace) stack = [...stack, screen].slice(-MAX_DEPTH);
    screen = next;
    refresh();
    return true;
  }
  function select(index) {
    const list = items();
    if (!Number.isInteger(index) || index < 0 || index >= list.length || list[index].disabled) return false;
    focus[screen] = { index, id: list[index].id };
    emit(); return true;
  }
  function move(delta) {
    if (!Number.isSafeInteger(delta) || delta === 0) return false;
    const list = items(), enabled = list.flatMap((item, index) => item.disabled ? [] : [index]);
    if (!enabled.length) return false;
    const current = enabled.indexOf(selection(screen, list).index);
    const target = ((current + delta % enabled.length) % enabled.length + enabled.length) % enabled.length;
    return select(enabled[target]);
  }
  function back({ captureWindup = false, pendingSync = false } = {}) {
    // These are decisions, not effects. The caller cancels an unsent wind-up;
    // a pending save is never discarded or changed by navigation.
    if (pendingSync) return 'blocked';
    if (captureWindup) return 'cancel-capture';
    remember();
    screen = stack.pop() ?? 'home';
    refresh();
    return screen === 'home' ? 'home' : 'back';
  }
  function snapshot() {
    const savedFocus = { ...focus, [screen]: selection() };
    return { version: 1, screen, stack: [...stack], focus: Object.fromEntries(Object.entries(savedFocus).map(([key, value]) => [key, { ...value }])) };
  }
  function restore(value) {
    let valid = false;
    try {
      if (typeof value === 'string') {
        if (value.length > MAX_SNAPSHOT) throw new Error('too large');
        value = JSON.parse(value);
      }
      if (!exact(value, ['version', 'screen', 'stack', 'focus']) || value.version !== 1 || !SCREENS.has(value.screen) ||
        !Array.isArray(value.stack) || value.stack.length > MAX_DEPTH || !value.stack.every(entry => SCREENS.has(entry)) || !object(value.focus)) throw new Error('invalid state');
      const entries = Object.entries(value.focus);
      if (entries.length > DEVICE_SCREENS.length) throw new Error('too many screens');
      for (const [name, entry] of entries) {
        if (!SCREENS.has(name) || !exact(entry, ['id', 'index']) || !(entry.id === null || validId(entry.id)) ||
          !Number.isInteger(entry.index) || entry.index < -1 || entry.index >= MAX_ITEMS) throw new Error('invalid focus');
      }
      screen = value.screen; stack = [...value.stack];
      focus = Object.assign(Object.create(null), Object.fromEntries(entries.map(([name, entry]) => [name, { ...entry }])));
      valid = true;
    } catch { screen = 'home'; stack = []; focus = Object.create(null); }
    refresh(); return valid;
  }
  screen = allowed(initialScreen) ? initialScreen : 'home';
  refresh(false);
  return {
    state, selected: () => { const list = items(); return list[selection(screen, list).index] ?? null; },
    go: next => setScreen(next, { replace: false }), setScreen, back, move, select, refresh, snapshot, restore,
  };
}

// Keyboard is an optional simulator input, never a claim about device buttons.
export function handleDeviceKey(event, navigation, { confirm, back } = {}) {
  if (event.defaultPrevented || event.isComposing || event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return false;
  const target = event.target?.nodeType === 3 ? event.target.parentElement : event.target;
  if (target?.isContentEditable || target?.closest?.('input, textarea, select, [role="textbox"]')) return false;
  const directions = { ArrowUp: -1, ArrowLeft: -1, ArrowDown: 1, ArrowRight: 1 };
  if (Object.hasOwn(directions, event.key)) {
    event.preventDefault(); navigation.move(directions[event.key]); return true;
  }
  if (event.key === 'Enter') {
    if (typeof confirm !== 'function') return false;
    event.preventDefault();
    // Consume held keys as well: otherwise the focused HTML button could still
    // receive a repeated native click even though our callback was skipped.
    if (!event.repeat) { const item = navigation.selected(); if (item) confirm(item); }
    return true;
  }
  if (event.key === 'Escape' || event.key === 'Backspace') {
    if (typeof back !== 'function') return false;
    event.preventDefault(); if (!event.repeat) back(); return true;
  }
  return false;
}

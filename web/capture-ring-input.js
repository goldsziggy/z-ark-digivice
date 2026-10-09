// A fresh DOWN commits immediately. Release, movement or interruption cannot
// undo that command. All remaining input from the same contact is consumed.
export function createCaptureRingInput(surface, {
  actionButton, canArm, getRevision, sample, onPress, onCancel = () => {},
  now = () => performance.now(), repeatGuardMs = 450,
} = {}) {
  if (!surface?.ownerDocument || !actionButton ||
      ![canArm, getRevision, sample, onPress, onCancel, now].every(fn => typeof fn === 'function')) {
    throw new TypeError('Capture surface, action button and callbacks are required.');
  }
  const document = surface.ownerDocument, window = document.defaultView;
  const listeners = [], pointers = new Set(), keys = new Set(), ownedPointers = new Set(), ownedKeys = new Set(), blockedClicks = new Set();
  const oldTouchAction = surface.style.touchAction;
  let destroyed = false, blurred = false, blockedUntil = 0, submittedContext;
  const listen = (target, type, fn) => {
    target.addEventListener(type, fn, true);
    listeners.push(() => target.removeEventListener(type, fn, true));
  };
  const allowed = () => !destroyed && !blurred && !document.hidden && surface.isConnected !== false && canArm();
  const consume = event => { event.preventDefault(); event.stopImmediatePropagation(); };
  const buttonTarget = target => target === actionButton || actionButton.contains?.(target);
  function inPlayArea(event) {
    const r = surface.getBoundingClientRect();
    if (![r.left, r.top, r.width, r.height, event.clientX, event.clientY].every(Number.isFinite) || r.width <= 0 || r.height <= 0) return false;
    const x = (event.clientX - r.left) * 412 / r.width, y = (event.clientY - r.top) * 412 / r.height;
    return y >= 80 && y <= 330 && Math.hypot(x - 206, y - 206) <= 206;
  }
  function refresh() { surface.style.touchAction = allowed() ? 'none' : oldTouchAction; }
  function cancel(reason = 'cancelled') { onCancel(reason); refresh(); }
  function commit() {
    const time = now(), revision = getRevision();
    if (!allowed() || !Number.isFinite(time) || time < blockedUntil || Object.is(revision, submittedContext)) return false;
    const value = sample(time);
    if (!value || !allowed() || !Object.is(revision, getRevision())) return false;
    // Lock before handing control to the caller, which synchronously stores its
    // pending command and closes the aim screen. A new aim has a fresh context.
    submittedContext = revision; blockedUntil = time + repeatGuardMs;
    onPress({ sample: value, revision }); refresh(); return true;
  }
  listen(document, 'pointerdown', event => {
    const repeated = pointers.has(event.pointerId); pointers.add(event.pointerId);
    if (!repeated) blockedClicks.delete(event.pointerId); // Mouse IDs may be reused after a canceled contact.
    const isButton = buttonTarget(event.target);
    const onSurface = event.target === surface || surface.contains?.(event.target);
    if (!isButton && (!onSurface || event.target?.closest?.('button, a[href], input, select, textarea, [role="button"], [contenteditable="true"]'))) return;
    if (!allowed() || event.button !== 0 || event.isPrimary === false || repeated || pointers.size !== 1 || keys.size) return;
    if (!isButton && !inPlayArea(event)) return;
    consume(event);
    ownedPointers.add(event.pointerId); blockedClicks.add(event.pointerId);
    // Bound retained compatibility-click IDs even if a browser omits click.
    while (blockedClicks.size > 8) blockedClicks.delete(blockedClicks.values().next().value);
    try { (isButton ? actionButton : surface).setPointerCapture(event.pointerId); } catch { /* Release is not required to commit. */ }
    commit();
  });
  listen(document, 'pointermove', event => { if (ownedPointers.has(event.pointerId)) consume(event); });
  for (const type of ['pointerup', 'pointercancel']) listen(document, type, event => {
    pointers.delete(event.pointerId);
    if (ownedPointers.delete(event.pointerId)) consume(event);
  });
  const actionKey = event => !event.altKey && !event.ctrlKey && !event.metaKey && !event.isComposing
    && !event.target?.closest?.('input, textarea, select, [contenteditable="true"]')
    && (event.code === 'KeyD' || event.key?.toLowerCase() === 'd' || ['Enter', ' '].includes(event.key) && (buttonTarget(event.target) || event.target === surface || event.target === document.body));
  listen(document, 'keydown', event => {
    if (!actionKey(event)) return;
    const id = event.code || event.key, repeated = keys.has(id); keys.add(id);
    if (ownedKeys.has(id)) { consume(event); return; }
    if (!allowed()) return;
    consume(event);
    if (event.repeat || repeated || pointers.size || keys.size !== 1) return;
    ownedKeys.add(id); commit();
  });
  listen(document, 'keyup', event => {
    const id = event.code || event.key; keys.delete(id);
    if (ownedKeys.delete(id)) consume(event);
  });
  listen(document, 'click', event => {
    if (event.detail !== 0 && blockedClicks.delete(event.pointerId)) { consume(event); return; }
    // Physical clicks require a fresh handled DOWN. This excludes the click of
    // the Confirm press that entered capture and preserves independent AT input.
    if (event.detail !== 0 || !buttonTarget(event.target) || !allowed() || pointers.size || keys.size) return;
    consume(event); commit();
  });
  const clearContacts = () => { pointers.clear(); keys.clear(); ownedPointers.clear(); ownedKeys.clear(); };
  listen(window, 'resize', () => cancel('resize'));
  listen(window, 'blur', event => { if (event.target !== window) return; blurred = true; clearContacts(); cancel('blur'); });
  listen(window, 'focus', event => { if (event.target !== window) return; blurred = false; cancel('focus'); });
  listen(document, 'visibilitychange', () => { clearContacts(); cancel(document.hidden ? 'hidden' : 'visible'); });
  refresh();
  return { cancel, refresh, destroy() { destroyed = true; clearContacts(); blockedClicks.clear(); listeners.forEach(remove => remove()); surface.style.touchAction = oldTouchAction; } };
}

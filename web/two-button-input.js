// Input gestures only: the caller owns selection, navigation and game commands.
// Cancel whenever the screen changes so a held button cannot act on a new view.
export function setupTwoButtonInput({ left, right, onNext, onBack, onConfirm, canBack = () => true, holdMs = 600 }) {
  if (!left?.addEventListener || !right?.addEventListener || ![onNext, onBack, onConfirm, canBack].every(callback => typeof callback === 'function')) throw new TypeError('Two buttons and callbacks are required.');
  if (!Number.isFinite(holdMs) || holdMs < 100 || holdMs > 5000) throw new RangeError('Hold duration must be 100–5000 ms.');
  const document = left.ownerDocument, window = document.defaultView;
  const listeners = [], blockedClick = new Map([[left, false], [right, false]]);
  let active = null, destroyed = false;
  function listen(target, type, callback) { target.addEventListener(type, callback); listeners.push(() => target.removeEventListener(type, callback)); }
  function allowed(button) { return !destroyed && !document.hidden && !button.disabled; }
  function clearTimer(gesture) { if (gesture?.timer !== null) clearTimeout(gesture?.timer); }
  function cancel() {
    if (!active) return;
    clearTimer(active);
    if (active.source === 'pointer') blockedClick.set(active.button, true);
    active = null;
  }
  function begin(button, source, id, event) {
    if (source === 'pointer') blockedClick.set(button, true);
    if (active || !allowed(button)) return;
    // A/D have no native click; Enter/Space activation is prevented below.
    // Do not leave a flag that swallows the next accessibility click.
    if (source === 'keyboard') blockedClick.set(button, false);
    const gesture = { button, source, id, held: false, timer: null };
    active = gesture;
    if (button === left) gesture.timer = setTimeout(() => {
      if (active !== gesture) return;
      gesture.timer = null; gesture.held = true;
      if (allowed(button) && canBack()) onBack(event);
    }, holdMs);
  }
  function end(button, source, id, event) {
    const gesture = active;
    if (!gesture || gesture.button !== button || gesture.source !== source || gesture.id !== id) return;
    clearTimer(gesture); active = null;
    if (!gesture.held && allowed(button)) (button === left ? onNext : onConfirm)(event);
  }
  for (const button of [left, right]) {
    listen(button, 'pointerdown', event => {
      if (event.button !== 0 || event.isPrimary === false) return;
      event.preventDefault();
      begin(button, 'pointer', event.pointerId, event);
      try { button.setPointerCapture(event.pointerId); } catch { /* Removed/unsupported element: cancellation still protects release. */ }
    });
    listen(button, 'pointerup', event => {
      if (event.button !== 0) return;
      event.preventDefault(); end(button, 'pointer', event.pointerId, event);
    });
    for (const type of ['pointercancel', 'lostpointercapture']) listen(button, type, event => {
      if (active?.source === 'pointer' && active.id === event.pointerId && active.button === button) cancel();
    });
    listen(button, 'click', event => {
      event.preventDefault();
      if (blockedClick.get(button)) { blockedClick.set(button, false); return; }
      // Accessibility/programmatic clicks have no preceding pointer lifecycle.
      if (allowed(button) && !active) (button === left ? onNext : onConfirm)(event);
    });
  }
  function editing(target) {
    return target?.isContentEditable || target?.closest?.('input, textarea, select, [role="textbox"], [contenteditable="true"]');
  }
  function keyButton(event) {
    if (event.code === 'KeyA' || event.key?.toLowerCase() === 'a') return left;
    if (event.code === 'KeyD' || event.key?.toLowerCase() === 'd') return right;
    if ((event.key === 'Enter' || event.key === ' ') && [left, right].includes(event.target)) return event.target;
    return null;
  }
  listen(document, 'keydown', event => {
    if (event.defaultPrevented || event.isComposing || event.altKey || event.ctrlKey || event.metaKey || event.shiftKey || editing(event.target)) return;
    const button = keyButton(event); if (!button) return;
    event.preventDefault();
    if (!event.repeat) begin(button, 'keyboard', event.code || event.key, event);
  });
  listen(document, 'keyup', event => {
    const gesture = active;
    if (!gesture || gesture.source !== 'keyboard' || gesture.id !== (event.code || event.key)) return;
    event.preventDefault();
    if (event.isComposing || editing(event.target)) { cancel(); return; }
    end(gesture.button, 'keyboard', gesture.id, event);
  });
  listen(window, 'blur', cancel);
  listen(document, 'visibilitychange', () => { if (document.hidden) cancel(); });
  return { cancel, destroy() { cancel(); destroyed = true; for (const remove of listeners) remove(); } };
}

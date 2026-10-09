// Browser pointer adapter for firmware/runtime/device_ui.cpp's 412px touch
// contract. It owns contact interpretation only; callbacks own game commands.
// Register before capture-ring-input.js so the screen has exactly one owner.
export function createDeviceTouchInput(surface, {
  canInteract, getContext, getMode, getTargets, onTarget, onHorizontal,
  onBattleCommit, onCapture, getBrowseBand = () => [100, 280],
  now = () => performance.now(),
} = {}) {
  if (!surface?.ownerDocument || ![canInteract, getContext, getMode, getTargets,
    onTarget, onHorizontal, onBattleCommit, onCapture, getBrowseBand, now].every(fn => typeof fn === 'function'))
    throw new TypeError('A touch surface and input callbacks are required.');
  const document = surface.ownerDocument, window = document.defaultView;
  const listeners = [], pointers = new Set(), owned = new Set(), blockedClicks = new Set();
  const oldTouchAction = surface.style.touchAction;
  const oldSelect = surface.style.userSelect, oldWebkitSelect = surface.style.webkitUserSelect;
  let gesture = null, destroyed = false, blurred = false, captureAcceptedAt = -Infinity, legacyClickPending = false;
  const listen = (target, type, callback) => {
    const options = { capture: true, passive: false };
    target.addEventListener(type, callback, options);
    listeners.push(() => target.removeEventListener(type, callback, options));
  };
  const consume = event => { event.preventDefault(); event.stopImmediatePropagation(); };
  const onSurface = event => event.target === surface || surface.contains?.(event.target);
  const ready = () => !destroyed && !blurred && !document.hidden && surface.isConnected !== false && canInteract();
  const inside = ({x, y}) => x >= 0 && y >= 0 && x < 412 && y < 412 && (x - 206) ** 2 + (y - 206) ** 2 <= 204 ** 2;
  const hit = (target, point) => point.x >= target.x && point.x < target.x + target.w && point.y >= target.y && point.y < target.y + target.h;
  const targets = () => (getTargets() || []).filter(target => target && target.id != null &&
    [target.x, target.y, target.w, target.h].every(Number.isFinite) && target.w > 0 && target.h > 0);
  function point(event) {
    const rect = surface.getBoundingClientRect();
    if (![rect.left, rect.top, rect.width, rect.height, event.clientX, event.clientY].every(Number.isFinite) || rect.width <= 0 || rect.height <= 0) return null;
    // Canvas content coordinates, including CSS scaling but excluding its border.
    // The interactive canvas has no padding; callers can also border its wrapper.
    const scaleX = surface.offsetWidth > 0 ? rect.width / surface.offsetWidth : 1;
    const scaleY = surface.offsetHeight > 0 ? rect.height / surface.offsetHeight : 1;
    const left = rect.left + (surface.clientLeft || 0) * scaleX;
    const top = rect.top + (surface.clientTop || 0) * scaleY;
    const width = surface.clientWidth > 0 ? surface.clientWidth * scaleX : rect.width;
    const height = surface.clientHeight > 0 ? surface.clientHeight * scaleY : rect.height;
    return { x: Math.floor((event.clientX - left) * 412 / width), y: Math.floor((event.clientY - top) * 412 / height) };
  }
  function horizontalTap({x, y}, battle) {
    if (y >= 134 && y < 246) {
      if (x >= 30 && x < 84) return -1;
      if (x >= 328 && x < 382) return 1;
    }
    if (battle && y >= 248 && y < 287) {
      if (x >= 80 && x < 176) return -1;
      if (x >= 238 && x < 334) return 1;
    }
    return 0;
  }
  // Home's side targets are also browse origins in the firmware. Other button
  // origins (even disabled Catch) cannot become a swipe or a different action.
  const homeEdge = (target, band) => band[0] === 112 && band[1] === 282 && target &&
    [30, 328].includes(target.x) && target.y === 134 && target.w === 54 && target.h === 112;
  function refresh() {
    if (destroyed) return;
    surface.style.touchAction = 'none';
    surface.style.userSelect = 'none'; surface.style.webkitUserSelect = 'none';
    if (gesture && (!ready() || !Object.is(gesture.context, getContext()) || gesture.mode !== getMode())) gesture = null;
  }
  // Ownership is retained until release, preventing cancelled gestures from
  // becoming compatibility clicks on controls newly shown under the contact.
  function cancel() { gesture = null; refresh(); }
  function blockClick(id) {
    blockedClicks.add(id);
    while (blockedClicks.size > 16) blockedClicks.delete(blockedClicks.values().next().value);
  }
  listen(document, 'pointerdown', event => {
    const repeated = pointers.has(event.pointerId);
    pointers.add(event.pointerId);
    if (!repeated) blockedClicks.delete(event.pointerId);
    if (pointers.size > 1 || repeated) cancel();
    if (!onSurface(event)) { if (!repeated) legacyClickPending = false; return; }
    legacyClickPending = true;
    consume(event); owned.add(event.pointerId); blockClick(event.pointerId);
    try { surface.setPointerCapture(event.pointerId); } catch { /* Document listeners still consume the contact. */ }
    if (!ready() || event.button !== 0 || event.isPrimary === false || repeated || pointers.size !== 1) return;
    const p = point(event), time = now();
    if (!p || !inside(p) || !Number.isFinite(time)) return;
    const mode = getMode(), context = getContext();
    if (mode === 'capture' && p.y >= 80 && p.y <= 330) {
      if (time < captureAcceptedAt || time - captureAcceptedAt < 450) return;
      captureAcceptedAt = time;
      onCapture({time, context, point:p});
      refresh(); return;
    }
    const target = targets().find(candidate => hit(candidate, p));
    const band = getBrowseBand();
    const browse = mode === 'browse' && p.y >= band[0] && p.y < band[1] && (!target || homeEdge(target, band));
    const battle = mode === 'battle' && !target && p.y >= 130 && p.y < 336;
    gesture = { id:event.pointerId, mode, context, start:p, downAt:time, lastAt:time,
      moved:false, browse, battle, target:target ? {...target} : null };
  });
  function finish(event, ending) {
    if (!owned.has(event.pointerId)) return;
    consume(event);
    const current = gesture;
    if (!current || current.id !== event.pointerId) return;
    refresh();
    if (gesture !== current) return;
    const p = point(event), time = now();
    if (!p || !inside(p) || !Number.isFinite(time) || time < current.lastAt || time - current.downAt > 10000) { cancel(); return; }
    current.lastAt = time;
    const dx = p.x - current.start.x, dy = p.y - current.start.y;
    const distance = dx * dx + dy * dy;
    if (distance > 24 * 24) current.moved = true;
    if (!ending) {
      if (!current.browse && !current.battle && current.moved) cancel();
      return;
    }
    gesture = null;
    const elapsed = time - current.downAt, detail = {time, context:current.context, point:p};
    if (current.browse || current.battle) {
      if (elapsed < 40 || elapsed > 1500) return;
      const ax = Math.abs(dx), ay = Math.abs(dy);
      if (ax >= 40 && ax * 2 >= ay * 3) { onHorizontal(dx < 0 ? 1 : -1, detail); return; }
      if (current.battle && dy <= -40 && ay * 2 >= ax * 3) { onBattleCommit(detail); return; }
      const direction = horizontalTap(current.start, current.battle);
      if (!current.moved && direction && direction === horizontalTap(p, current.battle)) onHorizontal(direction, detail);
      return;
    }
    if (!current.target || current.target.enabled === false || current.moved || elapsed < 20 || elapsed > 1800 || distance > 24 * 24) return;
    const target = targets().find(candidate => candidate.id === current.target.id);
    if (target && target.enabled !== false && hit(target, p) &&
      ['x','y','w','h'].every(key => target[key] === current.target[key])) onTarget(target.id, detail);
  }
  listen(document, 'pointermove', event => finish(event, false));
  listen(document, 'pointerup', event => {
    finish(event, true);
    pointers.delete(event.pointerId); owned.delete(event.pointerId);
  });
  listen(document, 'pointercancel', event => {
    if (owned.has(event.pointerId)) consume(event);
    if (gesture?.id === event.pointerId) cancel();
    pointers.delete(event.pointerId); owned.delete(event.pointerId);
  });
  listen(document, 'lostpointercapture', event => { if (gesture?.id === event.pointerId) cancel(); });
  listen(document, 'click', event => {
    if (event.detail !== 0 && (blockedClicks.delete(event.pointerId) || onSurface(event) ||
      (event.pointerId === undefined && legacyClickPending))) {
      legacyClickPending = false; consume(event);
    }
  });
  listen(document, 'contextmenu', event => { if (onSurface(event)) consume(event); });
  const interrupt = () => { gesture = null; pointers.clear(); owned.clear(); };
  listen(window, 'blur', event => { if (event.target !== window) return; blurred = true; interrupt(); });
  listen(window, 'focus', event => { if (event.target !== window) return; blurred = false; refresh(); });
  listen(window, 'resize', cancel);
  listen(window, 'pagehide', interrupt);
  listen(document, 'visibilitychange', () => { interrupt(); refresh(); });
  refresh();
  return {
    cancel, refresh, contactActive: () => owned.size > 0,
    destroy() {
      destroyed = true; interrupt(); blockedClicks.clear(); legacyClickPending = false; listeners.forEach(remove => remove());
      surface.style.touchAction = oldTouchAction;
      surface.style.userSelect = oldSelect; surface.style.webkitUserSelect = oldWebkitSelect;
    },
  };
}

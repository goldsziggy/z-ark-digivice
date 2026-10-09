// Input only. The caller converts a release to one authoritative capture command.
// Coordinates are relative to the round surface; velocities are surface widths /
// heights per second. Call refresh() when paused, navigating, or changing revision.
export function createCaptureGesture(surface, {
  canArm, getRevision, onPreview = () => {}, onRelease, onCancel = () => {},
  ball = { x: 0.5, y: 300 / 412, radius: 28 / 412 },
} = {}) {
  if (!surface?.addEventListener || !surface.getBoundingClientRect ||
      ![canArm, getRevision, onPreview, onRelease, onCancel].every(fn => typeof fn === 'function')) {
    throw new TypeError('A capture surface and callbacks are required.');
  }
  if (![ball.x, ball.y, ball.radius].every(Number.isFinite) || ball.radius <= 0 ||
      ball.radius > 0.25 || ball.x < 0 || ball.x > 1 || ball.y < 0 || ball.y > 1) {
    throw new RangeError('Ball coordinates must fit the normalized capture surface.');
  }
  const document = surface.ownerDocument, window = document?.defaultView;
  if (!document?.addEventListener || !window?.addEventListener) throw new TypeError('The surface must have a document.');
  const listeners = [], downPointers = new Set();
  const oldTouchAction = surface.style?.touchAction;
  let active = null, destroyed = false;
  const listen = (target, type, fn, options) => {
    target.addEventListener(type, fn, options);
    listeners.push(() => target.removeEventListener(type, fn, options));
  };
  const rectNow = () => {
    const rect = surface.getBoundingClientRect();
    return { left: rect.left, top: rect.top, width: rect.width, height: rect.height };
  };
  const validRect = rect => Object.values(rect).every(Number.isFinite) && rect.width > 0 && rect.height > 0;
  const sameRect = (a, b) => Object.keys(a).every(key => Math.abs(a[key] - b[key]) < 0.5);
  const allowed = () => !destroyed && !document.hidden && surface.isConnected !== false && !surface.disabled && canArm();
  const point = (event, rect) => ({ x: (event.clientX - rect.left) / rect.width, y: (event.clientY - rect.top) / rect.height, t: event.timeStamp });
  const validPoint = p => [p.x, p.y, p.t].every(Number.isFinite);
  const inside = p => Math.hypot(p.x - 0.5, p.y - 0.5) <= 0.495;
  function detach() {
    const gesture = active;
    active = null; // Lost capture may dispatch synchronously when releasing it.
    if (gesture) {
      try { surface.releasePointerCapture?.(gesture.id); } catch { /* Already cancelled by the browser. */ }
    }
    return gesture;
  }
  function cancel(reason = 'cancelled') {
    if (!active) return;
    detach(); onCancel(reason);
  }
  function refresh() {
    // Touch action must be set before pointerdown. The caller refreshes when
    // entering/leaving aim; ordinary menu scrolling retains its previous policy.
    if (surface.style) surface.style.touchAction = allowed() ? 'none' : oldTouchAction;
    if (!active) return;
    if (!allowed()) cancel('disabled');
    else if (!Object.is(getRevision(), active.revision)) cancel('revision');
    else if (!sameRect(active.rect, rectNow())) cancel('resize');
  }
  function append(event) {
    refresh();
    if (!active) return null;
    const p = point(event, active.rect), previous = active.samples.at(-1);
    if (!validPoint(p) || p.t < previous.t) { cancel('invalid-sample'); return null; }
    if (!inside(p)) { cancel('outside'); return null; }
    if (p.t - active.start.t > 10000) { cancel('timeout'); return null; }
    // Preserve one sample before the 100 ms window for boundary interpolation.
    if (p.t === previous.t) active.samples[active.samples.length - 1] = p;
    else active.samples.push(p);
    while (active.samples.length > 2 && active.samples[1].t < p.t - 100) active.samples.shift();
    if (active.samples.length > 128) active.samples.splice(0, active.samples.length - 128);
    return p;
  }
  function preview(p, phase) {
    onPreview({ x: p.x, y: p.y, dx: p.x - active.start.x, dy: p.y - active.start.y, phase });
  }
  // Listen on the document in capture phase: a second finger outside the stage
  // must also invalidate a pending throw. It cannot inherit the first gesture.
  listen(document, 'pointerdown', event => {
    downPointers.add(event.pointerId);
    if (active && active.id !== event.pointerId) cancel('multitouch');
  }, true);
  for (const type of ['pointerup', 'pointercancel']) listen(document, type, event => {
    downPointers.delete(event.pointerId);
    if (type === 'pointercancel' && active?.id === event.pointerId) cancel('pointercancel');
  }, true);
  listen(surface, 'pointerdown', event => {
    downPointers.add(event.pointerId);
    if (active && active.id !== event.pointerId) { cancel('multitouch'); return; }
    if (active || downPointers.size > 1 || event.button !== 0 || event.isPrimary === false || !allowed()) return;
    // Footer buttons may overlap the ball's generous hit area at small sizes.
    // A tap on a real control always belongs to that control, never to the ball.
    if (event.target?.closest?.('button, a[href], input, select, textarea, [role="button"], [contenteditable="true"]')) return;
    const rect = rectNow();
    if (!validRect(rect)) return;
    const p = point(event, rect);
    if (!validPoint(p) || !inside(p) || Math.hypot((p.x - ball.x) * rect.width, (p.y - ball.y) * rect.height) > ball.radius * Math.min(rect.width, rect.height)) return;
    event.preventDefault();
    active = { id: event.pointerId, start: p, rect, revision: getRevision(), samples: [p] };
    try {
      if (typeof surface.setPointerCapture !== 'function') throw new Error('Pointer capture unavailable');
      surface.setPointerCapture(event.pointerId);
    } catch { cancel('capture-unavailable'); return; }
    preview(p, 'start');
  }, { passive: false });
  listen(surface, 'pointermove', event => {
    if (active?.id !== event.pointerId) return;
    event.preventDefault();
    // Coalesced samples keep the final flick responsive even at a low frame rate.
    for (const sample of event.getCoalescedEvents?.() || []) {
      if (sample.timeStamp > event.timeStamp || sample.timeStamp < active.samples.at(-1).t) continue;
      if (!append(sample)) return;
    }
    const p = append(event);
    if (p) preview(p, 'move');
  }, { passive: false });
  listen(surface, 'pointerup', event => {
    downPointers.delete(event.pointerId);
    if (active?.id !== event.pointerId) return;
    if (event.button !== 0) return;
    event.preventDefault();
    const end = append(event);
    if (!end) return;
    const gesture = active, samples = gesture.samples;
    const cutoff = end.t - 100;
    let first = samples[0];
    if (first.t < cutoff && samples.length > 1) {
      const next = samples[1], fraction = (cutoff - first.t) / (next.t - first.t);
      first = { x: first.x + (next.x - first.x) * fraction, y: first.y + (next.y - first.y) * fraction, t: cutoff };
    }
    // A backstroke is optional. Measure from the deepest point in the recent
    // window so a short downward pull followed by a flick also works naturally.
    for (const sample of samples) if (sample.t >= first.t && sample.y > first.y) first = sample;
    const upwardTravel = first.y - end.y;
    const elapsed = Math.max(16, end.t - first.t) / 1000;
    const clamp = value => Math.max(-4, Math.min(4, value));
    const vx = clamp((end.x - first.x) / elapsed), vy = clamp((end.y - first.y) / elapsed);
    if (upwardTravel < 12 / 412 || vy > -0.18) { cancel('short-or-downward'); return; }
    detach();
    onRelease({ x: end.x, y: end.y, dx: end.x - gesture.start.x, dy: end.y - gesture.start.y,
      vx, vy, speed: Math.hypot(vx, vy), durationMs: end.t - gesture.start.t, revision: gesture.revision });
  }, { passive: false });
  for (const type of ['pointercancel', 'lostpointercapture']) listen(surface, type, event => {
    if (type === 'pointercancel') downPointers.delete(event.pointerId);
    if (active?.id === event.pointerId) cancel(type);
  });
  listen(window, 'resize', () => cancel('resize'));
  listen(window, 'blur', () => { downPointers.clear(); cancel('blur'); });
  listen(document, 'visibilitychange', () => {
    if (document.hidden) { downPointers.clear(); cancel('hidden'); }
  });
  const observer = window.ResizeObserver ? new window.ResizeObserver(refresh) : null;
  observer?.observe(surface);
  refresh();
  return {
    cancel, refresh,
    destroy() {
      if (destroyed) return;
      cancel('destroyed'); destroyed = true;
      observer?.disconnect();
      for (const remove of listeners) remove();
      downPointers.clear();
      if (surface.style) surface.style.touchAction = oldTouchAction;
    },
  };
}

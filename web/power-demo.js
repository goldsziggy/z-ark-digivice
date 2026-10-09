// Browser-only interaction preview of firmware/runtime/power.cpp. The C++
// controller remains authoritative. No game state, storage, network or GPIO.
import { createDeviceScreen } from './device-screen.js';

const $ = id => document.getElementById(id);
const root = $('power-demo'), button = $('power-button'), source = $('power-source');
const failure = $('save-failure'), boot = $('boot-held'), releaseBoot = $('release-boot');
const screen = createDeviceScreen($('power-screen'), { activate() {}, select() {} });
const DEBOUNCE = 30, HOLD = 3000, MAX_GAP = 250, DRAIN = 650, SAVE = 350;
let phase, since, changedAt, lastTick, held, activeInput, initialHeld;
let wakeArmed, wakePressed, saveFails, countdown = 0, message = '', rendered = '';

function enter(next, now, detail = '') {
  phase = next; since = now; message = detail;
  if (next === 'failed' || next === 'standby') { wakeArmed = false; wakePressed = false; changedAt = now; }
}

function reset(startHeld = false) {
  const now = performance.now();
  held = initialHeld = startHeld; activeInput = null; changedAt = lastTick = now;
  wakeArmed = wakePressed = false; countdown = 0;
  enter('await-initial-release', now);
  render(now);
}

function press(input) {
  if (activeInput !== null || initialHeld || document.hidden) return;
  const now = performance.now();
  activeInput = input; held = true; changedAt = now;
  if (phase === 'off') {
    // A new battery startup requires release before shutdown can be armed.
    enter('await-initial-release', now, 'Simulated battery startup. Release PWR before a new shutdown hold.');
  }
  render(now);
}

function release(input = null, interrupted = false) {
  if (input !== null && input !== activeInput) return;
  const wasHeld = held, now = performance.now();
  held = initialHeld = false; activeInput = null; changedAt = now;
  if (phase === 'holding' || (phase === 'ready' && wasHeld)) {
    enter('await-initial-release', now, interrupted
      ? 'Hold interrupted. Shutdown cancelled; power remains on.'
      : 'Shutdown cancelled. Power remains on; hold for three seconds to try again.');
  } else if (interrupted && (phase === 'standby' || phase === 'failed')) {
    // A lost pointer/window is never permission to resume from standby/failure.
    wakeArmed = wakePressed = false;
  }
  render(now);
}

function tick(now) {
  if (now - lastTick > MAX_GAP) {
    if (phase === 'holding' || phase === 'ready') {
      // A suspended browser cannot prove a continuous physical hold.
      enter('await-initial-release', now, 'Preview paused. Release PWR, then start a fresh hold.');
      changedAt = now;
    }
    if (phase === 'standby' || phase === 'failed') { wakeArmed = wakePressed = false; changedAt = now; }
  }
  lastTick = now;
  const stable = now - changedAt >= DEBOUNCE;
  if (phase === 'await-initial-release' && !held && stable) enter('ready', now, message);
  else if (phase === 'ready' && held && stable) enter('holding', now);
  else if (phase === 'holding' && held && now - since >= HOLD) {
    saveFails = failure.checked;
    enter('draining', now);
  } else if (phase === 'draining' && now - since >= DRAIN) enter('saving', now);
  else if (phase === 'saving' && now - since >= SAVE) enter(saveFails ? 'failed' : 'await-release', now);
  else if (phase === 'await-release' && !held && stable) enter(source.value === 'battery' ? 'off' : 'standby', now);
  else if (phase === 'standby' || phase === 'failed') {
    if (!wakeArmed && !held && stable) wakeArmed = true;
    else if (wakeArmed && !wakePressed && held && stable) wakePressed = true;
    else if (wakePressed && !held && stable) enter('ready', now, 'Resumed. Power is held on; another full three-second hold is needed to shut down.');
  }
  countdown = phase === 'holding' ? Math.max(1, Math.ceil((HOLD - (now - since)) / 1000)) : 0;
  render(now);
  requestAnimationFrame(tick);
}

function render(now) {
  const views = {
    'await-initial-release': ['Release PWR', 'Release the startup press before shutdown can be armed.', 'STARTUP GUARD', '⏻', message || 'Startup guard: a held PWR button cannot shut down the device. Release it first.'],
    ready: ['Power is on', 'Hold PWR for three seconds. Let go early to cancel.', 'READY TO PLAY', '⏻', message || 'Ready. Try a short tap, or hold through the countdown.'],
    holding: ['Hold to power off', 'Release now to cancel.', 'HOLD CONTINUOUSLY', '', `${countdown || 3} ${countdown === 1 ? 'second' : 'seconds'} remaining. Release now to cancel shutdown.`],
    draining: ['Finishing work', 'Waiting for simulated pending writes to finish.', 'SHUTDOWN STARTED', '⋯', 'Shutdown started. Finishing simulated pending work; power stays on.'],
    saving: ['Saving safely', 'Checking a simulated save before power can be cut.', 'POWER STAYS ON', '⋯', 'Verifying the simulated save. No actual game save is read or written.'],
    'await-release': ['Saved', 'Release PWR to complete shutdown.', 'SIMULATED SAVE VERIFIED', '✓', 'Simulated save verified. Waiting for PWR release before dropping the power latch.'],
    standby: ['Quiet standby', 'Power remains. Press and release PWR to resume.', 'POWER SOURCE UNKNOWN', '☾', 'Simulated latch released, but power remains; its source is unknown to firmware. A fresh PWR press and release resumes. This is not deep sleep.'],
    failed: ['Power stays on', 'Simulated save failed. Press and release PWR to resume.', 'SHUTDOWN BLOCKED', '!', 'Simulated save failed: power was not cut. Press and release PWR to resume, clear the failure option, then try a fresh three-second hold.'],
    off: ['Powered off', 'Press PWR to simulate starting again.', 'BATTERY-ONLY SIMULATION', '⏻', 'Simulated battery power is off. Press PWR to boot again; release the startup press before another shutdown hold.'],
  };
  const [title, detail, footer, symbol, status] = views[phase];
  const busy = phase !== 'ready' || held;
  root.dataset.phase = phase;
  root.dataset.countdown = String(countdown);
  root.dataset.powerSource = source.value;
  root.dataset.latch = phase === 'off' || phase === 'standby' ? 'released' : 'held';
  button.setAttribute('aria-pressed', String(held));
  button.disabled = initialHeld;
  source.disabled = failure.disabled = boot.disabled = busy;
  releaseBoot.hidden = !initialHeld;
  boot.hidden = initialHeld;
  const signature = JSON.stringify([phase, title, detail, footer, symbol, status, countdown]);
  if (signature !== rendered) {
    rendered = signature;
    screen.render({ screen: 'power', title, eyebrow: 'POWER PREVIEW', detail, footer, items: [], index: 0 });
    $('power-symbol').textContent = symbol;
    $('power-symbol').hidden = phase === 'holding';
    $('power-countdown').hidden = phase !== 'holding';
    $('power-countdown').textContent = String(countdown || 3);
    $('power-status').textContent = status;
  }
  const fraction = phase === 'holding' ? Math.min(1, (now - since) / HOLD) : 0;
  $('power-ring-fill').style.strokeDashoffset = String(276.4602 * (1 - fraction));
}

button.addEventListener('pointerdown', event => {
  if (event.button !== 0 || activeInput !== null) return;
  event.preventDefault(); button.focus({ preventScroll: true });
  button.setPointerCapture(event.pointerId);
  press(`pointer:${event.pointerId}`);
});
button.addEventListener('pointerup', event => release(`pointer:${event.pointerId}`));
button.addEventListener('pointercancel', event => release(`pointer:${event.pointerId}`, true));
button.addEventListener('lostpointercapture', event => release(`pointer:${event.pointerId}`, true));
button.addEventListener('contextmenu', event => event.preventDefault());

window.addEventListener('keydown', event => {
  if (!['Space', 'KeyP'].includes(event.code) || event.repeat || event.altKey || event.ctrlKey || event.metaKey) return;
  if (event.target.closest('input, select, textarea, [contenteditable="true"]') || (event.target.closest('button, a') && event.target !== button)) return;
  event.preventDefault(); press(`key:${event.code}`);
});
window.addEventListener('keyup', event => {
  if (activeInput !== `key:${event.code}`) return;
  event.preventDefault(); release(`key:${event.code}`);
});
window.addEventListener('blur', () => release(null, true));
document.addEventListener('visibilitychange', () => { if (document.hidden) release(null, true); });
$('reset-demo').addEventListener('click', () => reset());
boot.addEventListener('click', () => reset(true));
releaseBoot.addEventListener('click', () => release());
source.addEventListener('change', () => render(performance.now()));
reset();
requestAnimationFrame(tick);

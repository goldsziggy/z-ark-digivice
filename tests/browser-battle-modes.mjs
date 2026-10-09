// Both combat modes use the native engines through an isolated local service.
// All browser choices use only left tap/hold and right Confirm. Direct HTTP
// below probes rejected operations; it never invents a game result or seed.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-battle-modes-'));
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
let app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const touchOnly = process.env.DIGIVICE_TOUCH_ONLY === '1';
const page = await browser.newPage({ viewport: { width: touchOnly ? 1280 : 390, height: 1000 }, hasTouch: true, reducedMotion: 'reduce' });
page.setDefaultTimeout(8000);
const screen = page.locator('#device-ui'), left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
const selected = () => screen.locator('[data-selected="true"]').getAttribute('data-device-action');
const errors = [], gamePosts = [], practicePosts = [];
const measurements = [], screenshots = [];
const evidence = process.env.BATTLE_MODES_EVIDENCE === '1';
let identity;
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (request.method() !== 'POST') return;
  if (request.url().endsWith('/api/save-sync')) gamePosts.push(request.postDataJSON());
  if (/\/api\/battle\/(start|act)$/.test(request.url())) practicePosts.push(request.postDataJSON());
});
async function choose(id, execute = true) {
  for (let count = 0; count < 32; count++) {
    if (await selected() === id) { if (execute) await confirm(); return; }
    if (touchOnly) await page.locator('#device-next').tap(); else await left.click();
  }
  throw new Error(`Two-button choice ${id} unavailable on ${await screen.getAttribute('data-screen')}`);
}
async function confirm() { if (touchOnly) await screen.locator('[data-selected="true"]').tap(); else await right.click(); }
async function back(expected) {
  if (touchOnly) { const control = page.locator('#device-back'); if (await control.count() && await control.isEnabled()) await control.tap(); }
  else await holdDeviceBack(page);
  if (expected) await onScreen(expected);
}
async function home() {
  for (let count = 0; count < 12 && await screen.getAttribute('data-screen') !== 'home'; count++) {
    if (touchOnly && await screen.getAttribute('data-screen') === 'wild-auto-result') await choose('wild-auto-done');
    else if (touchOnly && await screen.getAttribute('data-screen') === 'battle-auto-result') await choose('practice-auto-done');
    else await back();
  }
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await choose(id); await onScreen(id); }
async function read(path = '/api/save') {
  const response = await fetch(`${base}${path}`, { headers: { Authorization: `Bearer ${identity.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function gameCommand(id) {
  await choose(id, false);
  const reply = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.request().method() === 'POST' && response.status() === 200);
  await confirm(); const result = await (await reply).json();
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') === null && !document.querySelector('#device-confirm-button').disabled);
  return result;
}
async function practiceCommand(id) {
  await choose(id, false);
  const reply = page.waitForResponse(response => /\/api\/battle\/(start|act)$/.test(response.url()) && response.request().method() === 'POST' && response.status() === 200);
  await confirm(); return (await reply).json();
}
function nativeNext(save, type, value = 0) {
  assert.equal(save.baseSequence, 0);
  return JSON.parse(execFileSync(corePath, ['--replay-onboarding', String(save.seed)], {
    input: [...save.events, { type, value }].map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', timeout: 3000, maxBuffer: 65536,
  }));
}
async function rejectGame(events) {
  const before = await read();
  const response = await fetch(`${base}/api/save-sync`, { method: 'POST', headers: { Authorization: `Bearer ${identity.token}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ rulesVersion: before.state.rulesVersion, baseRevision: before.revision, batchId: crypto.randomUUID(), events }) });
  assert.equal(response.status, 422); assert.deepEqual(await read(), before);
}
async function restart() {
  await new Promise((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  app = await startServer({ dataDir, port, corePath, battleCorePath });
}
async function geometry(label) {
  const failures = await screen.evaluate(root => {
    const circle = document.querySelector('#screen-surface').getBoundingClientRect();
    const cx = circle.x + circle.width / 2, cy = circle.y + circle.height / 2, radius = circle.width / 2;
    const boxes = [...root.querySelectorAll('button')].filter(button => button.getClientRects().length).map(button => ({ label: button.textContent.trim(), box: button.getBoundingClientRect() }));
    const errors = [];
    for (const { label, box } of boxes) if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > radius + 2)) errors.push(`clipped: ${label}`);
    for (let i = 0; i < boxes.length; i++) for (let j = i + 1; j < boxes.length; j++) {
      const a = boxes[i].box, b = boxes[j].box;
      if (Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 1) errors.push(`overlap: ${boxes[i].label}/${boxes[j].label}`);
    }
    return errors;
  });
  assert.deepEqual(failures, [], label);
  const surface = await page.locator('#screen-surface').boundingBox();
  measurements.push({ label, width: surface.width, height: surface.height });
}
async function bothSizes(label) {
  await geometry(`${label} mobile`);
  if (evidence) {
    mkdirSync('docs/evidence', { recursive: true });
    const path = `docs/evidence/battle-modes-${label}.png`;
    await page.locator('#screen-surface').screenshot({ path }); screenshots.push(path);
  }
  if (touchOnly) { assert.ok(Math.abs(measurements.at(-1).width - 412) < 1); return; }
  await page.locator('.device-screen').evaluate(element => {
    element.style.setProperty('box-sizing', 'content-box', 'important');
    element.style.setProperty('width', '201.6px', 'important'); element.style.setProperty('height', '201.6px', 'important');
    element.style.setProperty('margin-inline', 'auto', 'important');
  });
  await geometry(`${label} nominal 2.1in`);
  assert.ok(Math.abs(measurements.at(-1).width - 201.6) < 1);
  await page.locator('.device-screen').evaluate(element => element.removeAttribute('style'));
}
function checkTrace(trace, kind) {
  assert.equal(trace.kind, kind); assert.equal(trace.mode, 'auto');
  assert.ok(trace.steps.length >= 1 && trace.steps.length <= (kind === 'wild' ? 48 : 40));
  assert.equal(trace.endSequence - trace.startSequence, kind === 'wild' ? 1 : trace.steps.length);
  assert.ok(!/"(?:rngState|seed|snapshotBase64|initialSnapshotBase64|excludedChoice|enemyChoice)":/.test(JSON.stringify(trace)));
  for (const [index, step] of trace.steps.entries()) {
    assert.equal(step.turn, index + 1);
    if (index) {
      assert.equal(step.playerHpBefore, trace.steps[index - 1].playerHpAfter);
      assert.equal(step.enemyHpBefore, trace.steps[index - 1].enemyHpAfter);
    }
    if (kind === 'practice') assert.equal(step.phase, index % 2 ? 'defend' : 'attack');
  }
}
async function noManualControls() {
  assert.equal(await screen.locator('[data-device-action="attack"], [data-device-action="heavy"], [data-device-action="magic"], [data-device-action="capture"], [data-device-action="cards"], [data-device-action^="practice-physical"], [data-device-action^="practice-card"]').count(), 0);
}
async function pauseResumeReplay() {
  await choose('auto-pause'); await page.waitForTimeout(800);
  const paused = await screen.getAttribute('data-auto-step');
  await page.waitForTimeout(800); assert.equal(await screen.getAttribute('data-auto-step'), paused);
  await choose('auto-pause');
}

try {
  await page.goto(`${base}/?controls=${touchOnly ? 'touch' : 'buttons'}`); await onScreen('home');
  await choose('connection'); await choose('start-pairing'); await choose('claim-device', false);
  const paired = page.waitForResponse(response => response.url().endsWith('/api/pairing/claim') && response.status() === 201);
  await confirm(); identity = await (await paired).json(); await onScreen('starter-select');
  await choose('starter-1'); await gameCommand('hatch-starter'); await onScreen('starter-hatched');
  await choose('meet-starter'); await onScreen('home');

  assert.equal((await read()).state.battleMode, 'tactical');
  // Browsing modes is local. Choosing wild Auto is one explicit durable mode
  // event, but walking only discovers the encounter and waits for confirmation.
  await menu('explore');
  const beforePicker = await read(), pickerPosts = gamePosts.length;
  await choose('wild-mode'); await onScreen('wild-mode'); await choose('wild-auto', false);
  await bothSizes('wild-picker'); await back('explore');
  assert.equal(gamePosts.length, pickerPosts); assert.deepEqual(await read(), beforePicker);
  await choose('wild-mode'); await gameCommand('wild-auto'); await onScreen('explore');
  assert.equal((await read()).state.battleMode, 'auto');
  await gameCommand('walk'); await onScreen('wild-auto-confirm'); const encounter = await read();
  assert.equal(encounter.state.phase, 'encounter'); assert.equal(encounter.state.wildHp, encounter.state.wildMaxHp);
  assert.equal(encounter.state.lastAutoBattle, null); assert.equal(encounter.autoTrace, null);
  await bothSizes('wild-confirm'); await noManualControls();
  const confirmationPosts = gamePosts.length;
  await back(); assert.equal(gamePosts.length, confirmationPosts); assert.deepEqual(await read(), encounter);
  await home(); await choose('wild-auto-confirm'); await onScreen('wild-auto-confirm');
  for (const event of [{ type: 'mode', value: 0 }, { type: 'attack', value: 0 }, { type: 'card', value: 1 }, { type: 'capture', value: 0 }]) await rejectGame([event]);
  assert.equal(gamePosts.length, confirmationPosts, 'none of the browser navigation started Auto');

  // Server commits while the acknowledgement is lost. Both input releases
  // occur during the first request, then Back and reload preserve that batch.
  let wildCommitted;
  await page.route('**/api/save-sync', async route => {
    const response = await route.fetch(); wildCommitted = await response.json();
    await page.waitForTimeout(180); await route.abort('failed');
  });
  await choose('wild-auto-start', false); if (touchOnly) await confirm(); else await right.dblclick({ delay: 25 }); await onScreen('saving');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="retry"]')?.disabled);
  assert.equal(gamePosts.length, confirmationPosts + 1);
  const wildPending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  assert.deepEqual(JSON.parse(wildPending).events, [{ type: 'auto', value: 0 }]);
  assert.deepEqual(wildCommitted.state, nativeNext(encounter, 'auto'));
  assert.equal(wildCommitted.revision, encounter.revision + 1);
  assert.equal(wildCommitted.state.sequence, encounter.state.sequence + 1);
  checkTrace(wildCommitted.autoTrace, 'wild');
  // This seeded native fixture really captures, so the retry exercises the
  // collection/reward boundary rather than only an ordinary victory.
  assert.equal(wildCommitted.autoTrace.outcome, 'captured');
  assert.equal(wildCommitted.state.collection.length, encounter.state.collection.length + 1);
  await back('saving');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), wildPending);
  await page.unroute('**/api/save-sync'); await restart(); await page.reload(); await onScreen('saving');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), wildPending);
  const wildRetried = await gameCommand('retry'); assert.deepEqual(wildRetried, wildCommitted);
  assert.deepEqual(gamePosts.at(-1), gamePosts.at(-2));
  const recoveredWild = await read();
  for (const key of Object.keys(wildCommitted)) assert.deepEqual(recoveredWild[key], wildCommitted[key]);
  // Equal-revision receipt after reload may go Home instead of replaying old
  // animation. The saved native result remains explicitly reachable.
  await home(); await choose('wild-auto-result'); await onScreen('wild-auto-result');
  await bothSizes('wild-result'); await noManualControls();
  await choose('wild-auto-done'); await onScreen('home');
  console.log('PASS wild: confirmation/Back, immutable mode, native capture, durable exact retry across restart, one collection award.');

  // Practice selectors and their Back path do not create even an initial fight.
  const petBeforePractice = await read(), practiceBefore = await read('/api/battle');
  await menu('battle-mode'); const beforePracticePosts = practicePosts.length;
  await choose('practice-start'); await onScreen('battle-select-mode');
  await choose('practice-auto'); await onScreen('battle-auto-confirm');
  await bothSizes('practice-confirm'); await noManualControls();
  await back('battle-select-mode'); await back('battle-mode');
  assert.equal(practicePosts.length, beforePracticePosts); assert.deepEqual(await read('/api/battle'), practiceBefore);

  // Tactical still advances one exchange per command; a new mode cannot replace
  // an active fight, and a mode field cannot be smuggled into an action.
  await choose('practice-start'); const tactical = await practiceCommand('practice-tactical'); await onScreen('battle-choice');
  assert.equal(tactical.mode, 'tactical'); assert.equal(tactical.autoTrace, null); assert.equal(tactical.battle.sequence, 0);
  let forbidden = await fetch(`${base}/api/battle/start`, { method: 'POST', headers: { Authorization: `Bearer ${identity.token}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ rulesVersion: tactical.battle.rulesVersion, expectedRevision: tactical.revision, requestId: crypto.randomUUID(), mode: 'auto' }) });
  assert.equal(forbidden.status, 409); assert.deepEqual(await read('/api/battle'), tactical);
  forbidden = await fetch(`${base}/api/battle/act`, { method: 'POST', headers: { Authorization: `Bearer ${identity.token}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ rulesVersion: tactical.battle.rulesVersion, expectedRevision: tactical.revision, requestId: crypto.randomUUID(), mode: 'auto', action: { type: 'physical', value: 0 } }) });
  assert.equal(forbidden.status, 400); assert.deepEqual(await read('/api/battle'), tactical);
  await choose('practice-stats'); await onScreen('practice-stats'); await bothSizes('practice-stats');
  await choose('practice-player-stats'); await onScreen('stats'); await bothSizes('practice-player-stats');
  await choose('skills'); await onScreen('skills'); await bothSizes('practice-player-skills');
  await back('stats'); await back('practice-stats');
  await choose('practice-enemy-stats'); await onScreen('stats'); await bothSizes('practice-enemy-stats');
  await back('practice-stats'); await back('battle-choice');
  assert.deepEqual(await read('/api/battle'), tactical, 'Inspecting both combatants never advances the duel');
  await choose('battle-cards'); await onScreen('battle-cards'); await bothSizes('practice-cards');
  const carded = await practiceCommand('practice-card-shelter'); await onScreen('battle-result');
  assert.equal(carded.battle.cardUsed, true); assert.equal(carded.battle.sequence, 1);
  await choose('practice-continue'); await onScreen('battle-choice');
  const exchanged = await practiceCommand('practice-physical'); await onScreen('battle-result');
  assert.equal(exchanged.battle.sequence, 2); assert.equal(exchanged.battle.phase, 'defend');
  await choose('practice-continue'); await onScreen('battle-choice'); await back('battle-mode');
  assert.deepEqual(await read('/api/battle'), exchanged);
  await choose('practice-resume'); await onScreen('battle-choice'); await bothSizes('practice-resumed');
  assert.deepEqual(await read('/api/battle'), exchanged, 'Resume retains the same saved duel');
  await back('battle-mode');
  await practiceCommand('practice-retreat'); await onScreen('battle-result'); await choose('practice-continue'); await onScreen('battle-mode');
  assert.deepEqual(await read(), petBeforePractice);

  // Whole-fight Auto is one durable service command. Losing its reply across a
  // restart must recover the terminal trace, never start another seeded duel.
  await choose('practice-start'); await choose('practice-auto'); await onScreen('battle-auto-confirm');
  let practiceCommitted;
  const autoPracticePosts = practicePosts.length;
  await page.route('**/api/battle/start', async route => {
    const response = await route.fetch(); practiceCommitted = await response.json();
    await page.waitForTimeout(180); await route.abort('failed');
  });
  await choose('practice-auto-start', false); if (touchOnly) await confirm(); else await right.dblclick({ delay: 25 }); await onScreen('battle-resolve');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="practice-retry"]')?.disabled);
  assert.equal(practicePosts.length, autoPracticePosts + 1);
  const practicePending = await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1'));
  assert.ok(practicePending, 'Auto start must have a durable pending wrapper');
  assert.equal(practiceCommitted.mode, 'auto'); assert.equal(practiceCommitted.battle.phase, 'finished');
  assert.equal(practiceCommitted.battle.cardUsed, false); checkTrace(practiceCommitted.autoTrace, 'practice');
  assert.ok(!/"(?:rngState|seed|snapshotBase64|initialSnapshotBase64|excludedChoice)":/.test(JSON.stringify(practiceCommitted)));
  await back('battle-resolve'); assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), practicePending);
  await page.unroute('**/api/battle/start'); await restart(); await page.reload(); await onScreen('battle-resolve');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), practicePending);
  const replayedPractice = await practiceCommand('practice-retry'); assert.deepEqual(replayedPractice, practiceCommitted);
  await onScreen('battle-auto-result'); await bothSizes('practice-result'); await noManualControls();
  assert.deepEqual(practicePosts.at(-1), practicePosts.at(-2)); assert.deepEqual(await read('/api/battle'), practiceCommitted);
  assert.deepEqual(await read(), petBeforePractice);

  console.log('PASS practice: Tactical exchange/retreat and Auto terminal lost-reply recovery; pet save unchanged.');

  // Playback is cosmetic. Repeated buttons and held Back cannot send combat
  // requests while the native committed trace is animating.
  await page.emulateMedia({ reducedMotion: 'no-preference' });
  await page.reload(); await onScreen('home'); await menu('battle-mode');
  await choose('practice-auto-result'); await onScreen('battle-auto-result');
  const playbackPosts = practicePosts.length;
  await choose('practice-auto-replay'); await onScreen('battle-auto-progress'); await noManualControls();
  await bothSizes('practice-progress'); await pauseResumeReplay(); await back('battle-auto-progress');
  assert.equal(practicePosts.length, playbackPosts);
  await page.waitForFunction(() => {
    const ui = document.querySelector('#device-ui'); return ui?.dataset.screen === 'battle-auto-progress' && ui.dataset.autoStep === ui.dataset.autoTotal;
  }, null, { timeout: 32000 });
  await choose('auto-pause'); await page.waitForTimeout(900);
  assert.equal(await screen.getAttribute('data-screen'), 'battle-auto-progress', 'Final-turn Pause must remain paused');
  assert.equal(await screen.getAttribute('data-auto-step'), await screen.getAttribute('data-auto-total'));
  await choose('auto-finish'); await onScreen('battle-auto-result');
  assert.equal(practicePosts.length, playbackPosts, 'Pause/resume/skip must never resubmit the saved battle');
  assert.deepEqual(await read('/api/battle'), practiceCommitted);
  assert.deepEqual(await read(), petBeforePractice); assert.deepEqual(errors, []);
  console.log('PASS native Tactical/Auto browser: explicit starts, fixed modes, real captured reward once, two lost ACK/server+browser restart exact retries, Tactical exchange/retreat, mid-replay and final-turn pause/resume/skip without writes, unchanged practice pet.');
  console.log(JSON.stringify({ input: touchOnly ? 'touch taps only' : 'two buttons', measurements, screenshots, wildTurns: wildCommitted.autoTrace.steps.length, practiceTurns: practiceCommitted.autoTrace.steps.length, physicalScale: touchOnly ? '412 logical CSS pixels; not a physical finger-fit measurement' : '201.6 CSS px nominal at 96 CSS px/in; not a calibrated hardware measurement' }, null, 2));
} finally {
  await browser.close(); if (app.server.listening) await new Promise(resolve => app.server.close(resolve)); app.close();
  rmSync(dataDir, { recursive: true, force: true });
}

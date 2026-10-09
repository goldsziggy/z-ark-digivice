// Actual Chromium touch input against an isolated service and the native core.
// API fixture preparation is explicit; no test injects HP, RNG or DOM gestures.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { touchDevice } from './touch-browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
const hash = data => createHash('sha256').update(data).digest('hex');
const sourcePaths = ['web/app.js', 'web/device-screen.js', 'web/device-navigation.js', 'web/styles.css', 'web/capture-gesture.js', 'web/capture-trajectory.js', 'core/game.cpp', 'core/game.hpp', 'core/cli.cpp', 'service/server.ts', 'tests/browser-flick-capture.mjs'];
const sources = () => sourcePaths.map(path => ({ path, sha256: hash(readFileSync(path)) }));
const initialSources = sources(), dataDir = mkdtempSync(join(tmpdir(), 'digivice-flick-browser-'));
let app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
let base = `http://127.0.0.1:${app.server.address().port}`, pairedFixtures = 0, storeGeneration = 0;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const event = (type, value = 0) => ({ type, value });
const eligible = [event('hatch', 1), event('walk', 100), event('magic'), event('heavy'), event('magic')];
const checks = [], gestures = [], fixtures = [], posts = [], inputSessions = [], screenshots = [], errors = [];
const limitations = ['No physical ESP display/touch, sensor, audio or haptic proof.', 'Fixture APIs create genuine native histories; they are not player input.', 'Target geometry is measured in CSS pixels, not physical finger accuracy.'];
let context, page, ui, cdp, identity, caseName, failure, gestureBox, outcome = 'FAIL';
function native(events, seed = 12345) {
  return JSON.parse(execFileSync(corePath, ['--replay-onboarding', String(seed)], {
    input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', timeout: 3000, maxBuffer: 65536, stdio: ['pipe', 'pipe', 'pipe'],
  }));
}
const trajectory = value => JSON.parse(execFileSync(corePath, ['--flick-trajectory', String(value)], { encoding: 'utf8' }));
async function http(path = '/api/save', body) {
  const response = await fetch(base + path, { method: body ? 'POST' : 'GET', headers: {
    ...(identity ? { Authorization: `Bearer ${identity.token}` } : {}), ...(body ? { 'Content-Type': 'application/json' } : {}),
  }, ...(body ? { body: JSON.stringify(body) } : {}) });
  assert.ok(response.ok, `${path}: ${response.status} ${await response.clone().text()}`); return response.json();
}
async function seed(events, purpose) {
  const before = await http(); let saved = before;
  for (let offset = 0; offset < events.length; offset += 100) saved = await http('/api/save-sync', {
    rulesVersion: saved.state.rulesVersion, baseRevision: saved.revision, batchId: crypto.randomUUID(), events: events.slice(offset, offset + 100),
  });
  fixtures.push({ case: caseName, storeGeneration, purpose, events, beforeRevision: before.revision, afterRevision: saved.revision }); return saved;
}
async function auditSession() {
  if (page && !page.isClosed()) inputSessions.push({ case: caseName, ...await page.evaluate(() => window.__flickAudit) });
}
async function sizeScreen(size = 412) {
  await page.locator('.device-screen').evaluate((element, size) => {
    element.style.setProperty('box-sizing', 'content-box', 'important');
    for (const dimension of ['width', 'height']) element.style.setProperty(dimension, `${size}px`, 'important');
    element.style.setProperty('margin-inline', 'auto', 'important');
  }, size);
}
async function fixture(label, events = eligible, mode = 'touch') {
  if (context) { await auditSession(); await context.close(); }
  // Respect the service's eight-device cap; a second isolated store supplies
  // remaining independent test cases without weakening production limits.
  if (pairedFixtures === 8) {
    await new Promise(resolve => app.server.close(resolve)); app.close();
    app = await startServer({ dataDir: join(dataDir, `batch-${++storeGeneration}`), port: 0, corePath, battleCorePath });
    base = `http://127.0.0.1:${app.server.address().port}`; pairedFixtures = 0;
  }
  caseName = label; identity = null;
  const pair = await http('/api/pairing/start', {}); identity = await http('/api/pairing/claim', { code: pair.code });
  pairedFixtures++;
  const saved = await seed(events, 'Genuine commands replayed by the service/native core; no edited state or RNG.');
  assert.deepEqual(saved.state, native(events));
  context = await browser.newContext({ viewport: { width: 1000, height: 1000 }, hasTouch: true, reducedMotion: 'reduce' });
  await context.addInitScript(identity => {
    localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity));
    window.__flickAudit = { keyboard: 0, externalControls: 0, pointerEvents: [], visibility: [], targetRingDraws: 0 };
    const arc = CanvasRenderingContext2D.prototype.arc;
    CanvasRenderingContext2D.prototype.arc = function (...args) {
      // Transparent rendering observation: preserve every draw and its arguments.
      if (this.canvas.id === 'display' && args[0] === 206 && args[1] === 120 && args[2] === 48) window.__flickAudit.targetRingDraws++;
      return Reflect.apply(arc, this, args);
    };
    document.addEventListener('keydown', () => window.__flickAudit.keyboard++);
    document.addEventListener('click', e => { if (e.target.closest('#device-back-button, #device-confirm-button')) window.__flickAudit.externalControls++; });
    for (const type of ['pointerdown', 'pointermove', 'pointerup', 'pointercancel']) document.addEventListener(type, e => {
      window.__flickAudit.pointerEvents.push({ type, pointerType: e.pointerType, trusted: e.isTrusted, x: e.clientX, y: e.clientY, time: e.timeStamp });
    }, true);
    document.addEventListener('visibilitychange', e => window.__flickAudit.visibility.push({ state: document.visibilityState, trusted: e.isTrusted }));
  }, identity);
  page = await context.newPage(); page.setDefaultTimeout(10000); ui = touchDevice(page);
  cdp = await context.newCDPSession(page);
  page.on('pageerror', e => errors.push({ case: label, message: e.message }));
  page.on('request', request => {
    if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) posts.push({ case: label, raw: request.postData(), body: request.postDataJSON() });
  });
  await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
  await page.goto(`${base}/?controls=${mode}`); await ui.onScreen(saved.state.phase === 'encounter' ? saved.state.battleMode === 'auto' ? 'wild-auto-confirm' : 'battle' : 'home'); await sizeScreen();
  return http();
}
async function arm() { await ui.tap('capture', 'capture-aim'); }
async function screenCheck(label) {
  const geometry = await ui.geometry(label);
  for (const button of geometry.buttons) assert.ok(button.box.width >= 69.9 && button.box.height >= 69.9, `${label}: small ${button.id}`);
  checks.push({ name: label, geometry });
}
async function photo(name) {
  const path = `docs/evidence/flick-capture-${name}.png`; await page.locator('#screen-surface').screenshot({ path });
  screenshots.push({ path, sha256: hash(readFileSync(path)), privateArtwork: false });
}
async function touch(type, points = []) {
  const box = gestureBox || await page.locator('#screen-surface').boundingBox(); assert.ok(box);
  await cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map(([x, y, id = 1]) => ({ x: box.x + x * box.width / 412, y: box.y + y * box.height / 412, id, radiusX: 3, radiusY: 3, force: 1 })) });
}
async function start(x = 206, y = 300) { gestureBox = await page.locator('#screen-surface').boundingBox(); await touch('touchStart', [[x, y]]); }
async function move(x, y) { await page.waitForTimeout(20); await touch('touchMove', [[x, y]]); }
async function finish() { await touch('touchEnd'); gestureBox = null; }
async function flick(kind) {
  // Real frame-sized delays produce trusted pointer events. No timestamp, event
  // object, release velocity or DOM handler is forged by this test.
  await start(); const began = performance.now();
  // Compensate for real CDP dispatch latency by moving at a wall-clock speed,
  // rather than assuming each awaited frame takes exactly 20ms.
  for (let n = 1; n <= 3; n++) {
    await page.waitForTimeout(20); const elapsed = performance.now() - began;
    assert.ok(elapsed < (kind === 'wide' ? 165 : 200), 'headless input dispatch is fast enough for this bounded gesture');
    await touch('touchMove', [[206 + (kind === 'wide' ? .7 * elapsed : 0), 300 - (kind === 'weak' ? .35 : 1.2) * elapsed]]);
  }
  await finish();
}
async function noAttempt(label, action, { sameSave = true } = {}) {
  const saved = await http(), count = posts.length; await action(); await page.waitForTimeout(150);
  assert.equal(posts.length, count, `${label}: no browser command`);
  if (sameSave) assert.deepEqual(await http(), saved, `${label}: save unchanged`);
  checks.push({ name: label, noBrowserCommand: true, ...(sameSave ? { saveUnchanged: true } : {}) });
}
async function waitSettled() {
  await page.waitForFunction(() => {
    const root = document.querySelector('#device-ui');
    return root && !['capture-aim', 'saving'].includes(root.dataset.screen) && root.dataset.layout !== 'feedback' && !!root.querySelector('button:not([disabled])');
  });
}
async function noCaptureOverlay(label) {
  // Allow a queued frame to settle, then inspect the real canvas drawing path.
  // A leaked captureFlight draws this target ring continuously after UI recovery.
  await page.waitForTimeout(150);
  const before = await page.evaluate(() => window.__flickAudit.targetRingDraws);
  await page.waitForTimeout(200);
  const after = await page.evaluate(() => window.__flickAudit.targetRingDraws);
  assert.equal(after, before, `${label}: old capture overlay must stop drawing`);
  checks.push({ name: label, targetRingDrawsBefore: before, targetRingDrawsAfter: after, staleCaptureOverlayAbsent: true });
}
async function assertThrow(kind, expectedHit) {
  const before = await http(), count = posts.length;
  const response = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.request().method() === 'POST' && r.status() === 200);
  await flick(kind); const saved = await (await response).json(); await waitSettled();
  assert.equal(posts.length, count + 1); const command = posts.at(-1).body.events;
  assert.equal(command.length, 1); assert.equal(command[0].type, 'flick');
  const decoded = trajectory(command[0].value); assert.equal(decoded.hit, expectedHit, `${kind}: measured browser trajectory`);
  assert.equal(saved.state.sequence, before.state.sequence + 1);
  assert.deepEqual(saved.state, native([...before.events, command[0]], before.seed));
  if (!expectedHit) {
    assert.equal(saved.state.captureAttempts, before.state.captureAttempts + 1);
    assert.equal(saved.state.rngState, before.state.rngState);
    assert.deepEqual(saved.state.collection.map(m => m.id), before.state.collection.map(m => m.id));
  }
  gestures.push({ case: caseName, kind, value: command[0].value, decoded, nativeOutcomeMatches: true, oneAttempt: true, beforeAttempts: before.state.captureAttempts, afterAttempts: saved.state.captureAttempts, captured: saved.state.collection.length > before.state.collection.length });
  return saved;
}

try {
  mkdirSync('docs/evidence', { recursive: true });
  assert.equal((await http('/api/health')).capabilities.captureFlick, 1);
  assert.ok(native(eligible).wildCaptureChance > 0);
  await fixture('cancellation and interruptions'); await arm(); await screenCheck('capture aim with reachable Cancel');
  await page.waitForFunction(() => window.__flickAudit.targetRingDraws > 0); await photo('aim');
  await noAttempt('Short tap on ball', async () => { await start(); await page.waitForTimeout(40); await finish(); });
  await noAttempt('Downward drag', async () => { await start(); await move(206, 325); await finish(); });
  await noAttempt('Touch cancel / pointer cancel', async () => { await start(); await move(206, 280); await touch('touchCancel'); });
  await noAttempt('Second finger cancels first throw', async () => {
    await start(); await move(206, 280); await touch('touchStart', [[206, 280, 1], [285, 260, 2]]); await touch('touchMove', [[206, 180, 1], [285, 240, 2]]); await finish();
  });
  await noAttempt('Outside circular edge cancels', async () => { await start(); await move(410, 410); await move(206, 190); await finish(); });
  await noAttempt('Starting outside launch ball cannot throw', async () => { await start(100, 280); await move(100, 180); await finish(); });
  await noAttempt('Resize during drag cancels', async () => { await start(); await move(206, 280); await sizeScreen(380); await move(206, 200); await finish(); await sizeScreen(); });
  await noAttempt('Tab activation during held gesture produces no command', async () => {
    await cdp.send('Emulation.setFocusEmulationEnabled', { enabled: false });
    await start(); await move(206, 280);
    const other = await context.newPage(); await other.goto('about:blank'); await other.bringToFront();
    await page.waitForTimeout(150); const hidden = await page.evaluate(() => document.hidden);
    await page.bringToFront();
    await other.close(); await cdp.send('Emulation.setFocusEmulationEnabled', { enabled: true }); await finish();
    if (hidden) assert.ok((await page.evaluate(() => window.__flickAudit.visibility)).some(e => e.state === 'hidden' && e.trusted));
    else limitations.push('Actual hidden-tab cancellation could not be reproduced: headless-shell kept document.hidden=false after a sibling tab activated with focus emulation disabled. The gesture unit suite covers visibilitychange; this run does not claim browser hidden-state proof.');
  });
  await noAttempt('Cancel button is a simple touch', async () => { await ui.back('battle'); });
  await arm(); await noAttempt('Navigation during drag cancels', async () => {
    await start(); await move(206, 280); await auditSession(); await page.reload(); await ui.onScreen('battle'); await sizeScreen(); await finish();
  });
  await arm();
  // A real authoritative revision update plus page refresh must discard the
  // prior touch session, even though the encounter remains capture-eligible.
  const revisionBefore = await http(), revisionPosts = posts.length;
  await start(); await move(206, 280); await seed([event('card', 2)], 'Concurrent native revision change while a gesture is held.');
  const revised = await http(); await auditSession(); await page.reload(); await ui.onScreen('battle'); await sizeScreen(); await finish();
  assert.ok(revised.revision > revisionBefore.revision); assert.equal(revised.state.captureAttempts, revisionBefore.state.captureAttempts);
  assert.equal(posts.length, revisionPosts); assert.deepEqual(await http(), revised);
  checks.push({ name: 'Revision refresh while held', noBrowserCommand: true, attemptsUnchanged: true, revisionIncreased: true });

  await fixture('stale revision rejects release'); await arm(); const stalePosts = posts.length;
  await start(); await move(206, 280); await seed([event('card', 2)], 'Concurrent native revision change before releasing a stale gesture.');
  const current = await http();
  const rejected = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 409);
  for (let n = 2; n <= 5; n++) await move(206, 300 - n * 20); await finish(); await rejected;
  await ui.onScreen('saving'); assert.equal(posts.length, stalePosts + 1); assert.deepEqual(await http(), current);
  checks.push({ name: 'Stale revision rejected by service', oldBaseRequestRejected: true, nativeAttemptsUnchanged: true, authoritativeSaveUnchanged: true, localRequestRetainedForReview: true });
  await noCaptureOverlay('Rejected flick stops target drawing while request awaits review');
  await ui.tap('open-recovery', 'recovery'); await ui.tap('recovery-confirm', 'recovery-confirm');
  await ui.tap('discard-local'); await waitSettled(); await noCaptureOverlay('Same-page conflict discard restores ordinary battle canvas');
  assert.deepEqual(await http(), current); assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), null);
  await photo('conflict-cleared');

  await fixture('weak legitimate miss'); await arm(); await assertThrow('weak', false); await photo('weak-miss');
  await fixture('wide legitimate miss'); await arm(); await assertThrow('wide', false); await photo('wide-miss');
  await fixture('straight hit'); await arm(); const hit = await assertThrow('straight', true);
  assert.equal(hit.state.collection.length, 2, 'this native seeded hit captures a real new companion'); await photo('captured');

  await fixture('same-page lost reply and retry'); await arm();
  const samePageBefore = await http(), samePagePosts = posts.length; let samePageCommitted;
  await page.route('**/api/save-sync', async route => {
    const response = await route.fetch(); samePageCommitted = await response.json(); await route.abort('failed');
  }, { times: 1 });
  await flick('straight'); await ui.onScreen('saving');
  await page.waitForFunction(() => document.querySelector('[data-device-action="retry"]')?.disabled === false);
  const samePageRaw = posts.at(-1).raw, samePageCommand = posts.at(-1).body.events[0];
  assert.deepEqual(samePageCommitted.state, native([...samePageBefore.events, samePageCommand], samePageBefore.seed));
  assert.equal(posts.length, samePagePosts + 1);
  await noCaptureOverlay('Lost-ACK flick stops target drawing while exact retry remains pending');
  const samePageResponse = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await ui.tap('retry'); const samePageRetried = await (await samePageResponse).json(); await waitSettled();
  assert.deepEqual(samePageRetried, samePageCommitted); assert.equal(posts.at(-1).raw, samePageRaw);
  assert.equal(posts.length, samePagePosts + 2); assert.equal((await http()).revision, samePageBefore.revision + 1);
  await noCaptureOverlay('Same-page exact retry restores ordinary canvas without reload'); await photo('same-page-retry');
  checks.push({ name: 'Same-page uncertain response recovery', pageNotReloaded: true, identicalPayload: true, oneNativeCommit: true, nativeOutcomeMatches: true });

  await fixture('lost reply and durable retry'); await arm();
  const beforeLost = await http(), lostPosts = posts.length; let committed;
  await page.route('**/api/save-sync', async route => {
    const response = await route.fetch(); committed = await response.json(); await route.abort('failed');
  }, { times: 1 });
  await flick('straight'); await ui.onScreen('saving');
  await page.waitForFunction(() => document.querySelector('[data-device-action="retry"]')?.disabled === false);
  assert.ok(committed); assert.equal(posts.length, lostPosts + 1);
  const raw = posts.at(-1).raw, command = posts.at(-1).body.events[0]; assert.equal(command.type, 'flick');
  assert.deepEqual(committed.state, native([...beforeLost.events, command], beforeLost.seed));
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  await noAttempt('Second swipe while awaiting retry is ignored', () => flick('straight'));
  assert.equal(await ui.screen.locator('#device-back').isDisabled(), true);
  const backBox = await ui.screen.locator('#device-back').boundingBox(); assert.ok(backBox);
  await page.touchscreen.tap(backBox.x + backBox.width / 2, backBox.y + backBox.height / 2); await ui.onScreen('saving');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  await auditSession(); await page.reload(); await ui.onScreen('saving'); await sizeScreen();
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  const retriedResponse = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await ui.tap('retry'); const retried = await (await retriedResponse).json(); await waitSettled();
  assert.equal(posts.at(-1).raw, raw); assert.deepEqual(retried, committed); assert.equal(posts.length, lostPosts + 2);
  assert.equal((await http()).revision, beforeLost.revision + 1);
  checks.push({ name: 'Lost reply, reload and exact retry', oneNativeCommit: true, identicalPayload: true, secondSwipeIgnored: true, nativeOutcomeMatches: true });

  await fixture('strong opponent disabled', [event('hatch', 1), event('walk', 100)]);
  assert.equal(await ui.control('capture').isDisabled(), true);
  await noAttempt('Strong opponent cannot arm from ball swipe', () => flick('straight'));
  await fixture('Auto encounter disabled', [event('hatch', 1), event('mode', 1), event('walk', 100)]);
  assert.equal(await ui.control('capture').count(), 0);
  await noAttempt('Auto encounter cannot arm from ball swipe', () => flick('straight'));

  const fullHistory = [event('hatch', 1), event('mode', 1)]; let full = native(fullHistory);
  for (let n = 0; n < 60 && full.collection.length < 8; n++) {
    fullHistory.push(...Array.from({ length: full.recoveryRestCount }, () => event('rest')), event('walk', 100), event('auto')); full = native(fullHistory);
  }
  assert.equal(full.collection.length, 8);
  fullHistory.push(event('mode', 0), ...Array.from({ length: full.recoveryRestCount }, () => event('rest')), event('walk', 100)); full = native(fullHistory);
  for (let n = 0; n < 30 && full.wildHp > full.wildMaxHp / 2; n++) {
    const options = ['attack', 'magic', ...(full.energy >= 6 ? ['heavy'] : [])].map(type => ({ action: event(type), next: native([...fullHistory, event(type)]) }));
    const chosen = options.filter(row => row.next.phase === 'encounter').sort((a, b) => a.next.wildHp - b.next.wildHp)[0]; assert.ok(chosen);
    fullHistory.push(chosen.action); full = chosen.next;
  }
  assert.equal(full.phase, 'encounter'); assert.ok(full.wildHp <= full.wildMaxHp / 2);
  await fixture('full roster disabled', fullHistory); assert.equal(await ui.control('capture').isDisabled(), true);
  await noAttempt('Full roster cannot arm from ball swipe', () => flick('straight'));

  const old = await fixture('two-button alternative', eligible, 'buttons');
  for (let n = 0; n < 10; n++) {
    if (await ui.screen.locator('[data-selected=true]').getAttribute('data-device-action') === 'capture') break;
    await page.locator('#device-back-button').tap();
  }
  assert.equal(await ui.screen.locator('[data-selected=true]').getAttribute('data-device-action'), 'capture');
  const oldPosts = posts.length, oldResponse = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await page.locator('#device-confirm-button').tap(); await ui.onScreen('capture');
  const legacy = await (await oldResponse).json(); await waitSettled(); assert.equal(posts.length, oldPosts + 1);
  assert.deepEqual(posts.at(-1).body.events, [event('capture')]); assert.deepEqual(legacy.state, native([...old.events, event('capture')], old.seed));
  checks.push({ name: 'Preserved two-button capture alternative', originalCaptureEvent: true, nativeOutcomeMatches: true });
  await auditSession();
  assert.deepEqual(errors, []);
  for (const input of inputSessions) {
    assert.equal(input.keyboard, 0);
    if (input.case !== 'two-button alternative') assert.equal(input.externalControls, 0);
    assert.ok(input.pointerEvents.every(e => e.trusted && e.pointerType === 'touch'));
  }
  assert.deepEqual(sources(), initialSources, 'source files stayed fixed during the run');
  outcome = 'PASS'; console.log(`PASS flick browser: ${checks.length} lifecycle/eligibility checks; trusted CDP touches; native weak/wide misses and straight capture; exact lost-reply retry; unchanged two-button capture.`);
} catch (error) { if (page && !page.isClosed()) await auditSession(); failure = String(error.stack || error); throw error; }
finally {
  mkdirSync('docs/evidence', { recursive: true });
  writeFileSync('docs/evidence/flick-capture-browser.json', JSON.stringify({ outcome, failure, sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(), sourceFiles: initialSources,
    binaries: [corePath, battleCorePath].map(path => ({ name: path.split('/').at(-1), sha256: hash(readFileSync(path)) })),
    scope: 'Headless Chromium hasTouch, trusted CDP touch events with real 20ms waits, 412 CSS pixels, original placeholder artwork, isolated temporary service saves.',
    checks, gestures, fixtures, gameCommands: posts.map(({ case: label, body }) => ({ case: label, events: body.events, baseRevision: body.baseRevision })), inputSessions, screenshots, errors,
    limitations,
  }, null, 2) + '\n');
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

// Real Chromium gestures against isolated saves and the same native game core.
// Fixtures are created through public service commands, never edited snapshots.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { touchDevice } from './touch-browser-tools.mjs';
import { CAPTURE_RING, sampleCaptureRing, captureRingChance } from '../web/capture-ring.js';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
const sources = ['web/app.js', 'web/auto-battle.js', 'web/capture-ring.js', 'web/capture-ring-input.js', 'web/capture-trajectory.js', 'tests/browser-ring-capture.mjs'];
const hash = data => createHash('sha256').update(data).digest('hex');
const sourceHashes = () => sources.map(path => ({ path, sha256: hash(readFileSync(path)) }));
const initialHashes = sourceHashes(), dataDir = mkdtempSync(join(tmpdir(), 'digivice-ring-capture-'));
const evidenceDir = resolve(process.env.DIGIVICE_BROWSER_EVIDENCE || '../deliverables/quality-capture-20261009/browser');
mkdirSync(evidenceDir, { recursive: true });
let fixtureSeed = 12345, pairedFixtures = 0, storeGeneration = 0;
let app = await startServer({ seedSource: () => fixtureSeed, dataDir, port: 0, corePath, battleCorePath });
let base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const event = (type, value = 0) => ({ type, value });
const encounter = [event('hatch', 1), event('mode', 1), event('walk', 100)];
const paused = [...encounter, event('auto-fight')];
const checks = [], posts = [], errors = [], screenshots = [], sessions = [], limitations = [], clips = [];
let usingPrivateArtwork = false;
let lastThrowAt = 0, lastPaintedCue;
let identity, context, page, ui, cdp, label, gestureBox, outcome = 'FAIL', failure;
async function http(path = '/api/save', body) {
  const r = await fetch(base + path, { method: body ? 'POST' : 'GET', headers: { ...(body ? { 'Content-Type': 'application/json' } : {}), ...(identity ? { Authorization: `Bearer ${identity.token}` } : {}) }, ...(body ? { body: JSON.stringify(body) } : {}) });
  assert.ok(r.ok, `${path}: ${r.status} ${await r.clone().text()}`); return r.json();
}
function native(events) {
  return JSON.parse(execFileSync(corePath, ['--replay-onboarding', String(fixtureSeed)], { input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', timeout: 3000 }));
}
async function fixture(name, events = encounter, mode = 'touch', motion = 'no-preference', dropWorldReply = false, seed = 12345, productionArt = false, timingCapability = true) {
  if (context) { sessions.push({ label, ...await page.evaluate(() => window.__autoCaptureAudit) }); await context.close(); }
  label = name; identity = null; fixtureSeed = seed; usingPrivateArtwork = productionArt;
  if (pairedFixtures === 8) {
    await new Promise(resolve => app.server.close(resolve)); app.close();
    app = await startServer({ seedSource: () => fixtureSeed, dataDir: join(dataDir, `batch-${++storeGeneration}`), port: 0, corePath, battleCorePath });
    base = `http://127.0.0.1:${app.server.address().port}`; pairedFixtures = 0;
  }
  pairedFixtures++;
  const pairing = await http('/api/pairing/start', {}); identity = await http('/api/pairing/claim', { code: pairing.code });
  let saved = await http('/api/save-sync', { rulesVersion: 13, baseRevision: identity.revision, batchId: crypto.randomUUID(), events: productionArt ? [event('hatch', 1)] : events });
  if (productionArt) {
    // Same valid profile2/world18 fixture used by the native production preview.
    fixtureSeed = 18; const initialized = await http('/api/world/seed', {}); fixtureSeed = seed;
    saved = await http('/api/save-sync', { rulesVersion: 13, baseRevision: initialized.revision, batchId: crypto.randomUUID(), events: [event('mode', 1), event('explore', 1000), event('auto-fight')] });
    assert.deepEqual(saved.state, native((await http()).events));
  } else assert.deepEqual(saved.state, native(events));
  context = await browser.newContext({ viewport: { width: 1000, height: 1000 }, hasTouch: true, reducedMotion: motion });
  await context.addInitScript(identity => {
    localStorage.setItem('digivice.dev.identity.v1', JSON.stringify({ deviceId: identity.deviceId, token: identity.token }));
    window.__autoCaptureAudit = { pointerEvents: [], views: [], texts: [], rings: [], focus: [], targetDraws: 0, targetStyle: null, paintedCues: [], ringColors: [], heroDraws: 0, heroBounds: null, backgroundPaint: -1 };
    for (const type of ['blur', 'focus']) window.addEventListener(type, e => window.__autoCaptureAudit.focus.push({ type, trusted: e.isTrusted, time: performance.now() }));
    const drawImage = CanvasRenderingContext2D.prototype.drawImage;
    CanvasRenderingContext2D.prototype.drawImage = function (...args) {
      if (this.canvas.id === 'display' && args.length === 5 && args[0] instanceof ImageBitmap && args[3] === 480 && args[4] === 480) window.__autoCaptureAudit.backgroundPaint = performance.now();
      if (this.canvas.id === 'display' && args.length === 9 && Math.max(args[7], args[8]) === 176) {
        window.__autoCaptureAudit.heroDraws++;
        window.__autoCaptureAudit.heroBounds = { source: args.slice(1, 5), destination: args.slice(5) };
      }
      return Reflect.apply(drawImage, this, args);
    };
    const fillText = CanvasRenderingContext2D.prototype.fillText;
    CanvasRenderingContext2D.prototype.fillText = function (...args) {
      if (this.canvas.id === 'display' && /^(RED|ORANGE|GREEN) \d+% - TAP PLAY AREA$/.test(args[0])) {
        window.__autoCaptureAudit.paintedCues.push({ text: args[0], time: performance.now() });
        if (window.__autoCaptureAudit.paintedCues.length > 2000) window.__autoCaptureAudit.paintedCues.shift();
      }
      return Reflect.apply(fillText, this, args);
    };
    const stroke = CanvasRenderingContext2D.prototype.stroke;
    CanvasRenderingContext2D.prototype.stroke = function (...args) {
      if (this.canvas.id === 'display' && this.lineWidth === 4 && ['#f6534a', '#ffa43c', '#54ea86'].includes(this.strokeStyle) && !window.__autoCaptureAudit.ringColors.includes(this.strokeStyle)) window.__autoCaptureAudit.ringColors.push(this.strokeStyle);
      return Reflect.apply(stroke, this, args);
    };
    const arc = CanvasRenderingContext2D.prototype.arc;
    CanvasRenderingContext2D.prototype.arc = function (...args) {
      if (this.canvas.id === 'display' && args[0] === 206 && args[1] === 176 && this.lineWidth === 24) { window.__autoCaptureAudit.targetDraws++; window.__autoCaptureAudit.targetStyle = this.strokeStyle; }
      if (this.canvas.id === 'display' && args[0] === 206 && args[1] === 176 && this.lineWidth === 7) {
        window.__autoCaptureAudit.rings.push({ radius: args[2], time: performance.now(), backgroundPaint: window.__autoCaptureAudit.backgroundPaint });
        if (window.__autoCaptureAudit.rings.length > 2000) window.__autoCaptureAudit.rings.shift();
      }
      return Reflect.apply(arc, this, args);
    };
    for (const type of ['pointerdown', 'pointermove', 'pointerup']) document.addEventListener(type, e => window.__autoCaptureAudit.pointerEvents.push({ type, id: e.pointerId, target: e.target.id, x: e.clientX, y: e.clientY, time: performance.now(), pointerType: e.pointerType, trusted: e.isTrusted }), true);
    document.addEventListener('DOMContentLoaded', () => {
      const audit = () => {
        const root = document.querySelector('#device-ui'); if (!root) return;
        const row = { screen: root.dataset.screen, layout: root.dataset.layout, text: root.textContent, time: performance.now() };
        const previous = window.__autoCaptureAudit.views.at(-1);
        if (!previous || previous.screen !== row.screen || previous.layout !== row.layout || previous.text !== row.text) window.__autoCaptureAudit.views.push(row);
      };
      new MutationObserver(audit).observe(document.querySelector('#device-ui'), { subtree: true, childList: true, attributes: true }); audit();
    });
  }, identity);
  page = await context.newPage(); page.setDefaultTimeout(12000); ui = touchDevice(page); cdp = await context.newCDPSession(page);
  page.on('pageerror', error => errors.push({ label, message: error.message }));
  page.on('request', r => { if (r.method() === 'POST' && r.url().endsWith('/api/save-sync')) posts.push({ label, body: r.postDataJSON(), raw: r.postData() }); });
  await page.route('**/api/roster/art/*', route => {
    const formId = Number(new URL(route.request().url()).pathname.split('/').at(-1));
    return productionArt && [11, 18].includes(formId) ? route.continue()
      : route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' });
  });
  if (!timingCapability) await page.route('**/api/health', async route => { const response = await route.fetch(); const data = await response.json(); delete data.capabilities.captureTimingQuality; await route.fulfill({ response, json: data }); });
  if (dropWorldReply === 'reject') await page.route('**/api/world/seed', route => route.fulfill({ status: 503, contentType: 'application/json', body: '{"error":"injected-seed-service-unavailable"}' }));
  else if (dropWorldReply) await page.route('**/api/world/seed', async route => { await route.fetch(); await route.abort('failed'); }, { times: 1 });
  await page.goto(`${base}/?controls=${mode}`);
  await ui.onScreen(saved.state.autoCapture === 1 && timingCapability ? 'capture-aim' : saved.state.phase === 'home' ? 'home' : saved.state.battleMode === 'auto' ? 'wild-auto-confirm' : 'battle');
  await page.locator('.device-screen').evaluate(element => {
    element.style.setProperty('box-sizing', 'content-box', 'important');
    for (const key of ['width', 'height']) element.style.setProperty(key, '412px', 'important');
  });
  await page.waitForFunction(() => !document.querySelector('#device-confirm-button').disabled || document.querySelector('#device-ui')?.dataset.screen !== 'capture-aim');
  if (dropWorldReply === 'reject') { await page.waitForTimeout(200); const uninitialized = await http(); assert.equal(uninitialized.state.worldSeed, 0); return uninitialized; }
  await page.waitForFunction(({ previous, productionArt }) => Number(document.querySelector('#revision').textContent) >= previous + (productionArt ? 0 : 1), { previous: saved.revision, productionArt });
  const initialized = await http(); assert.equal(initialized.state.worldSeed, productionArt ? 18 : fixtureSeed);
  assert.equal(initialized.events.filter(e => e.type === 'world-seed').length, 1);
  assert.deepEqual(initialized.state, native(initialized.events));
  return initialized;
}
async function photo(name) {
  await page.waitForTimeout(40); // Let rAF paint the actual application.
  const path = join(evidenceDir, `${name}.png`); await page.locator('#screen-surface').screenshot({ path });
  screenshots.push({ path, sha256: hash(readFileSync(path)), privateArtwork: usingPrivateArtwork });
}
async function touch(type, points = []) {
  const box = gestureBox || await page.locator('#screen-surface').boundingBox(); assert.ok(box);
  await cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map(([x, y, id = 1]) => ({ x: box.x + x * box.width / 412, y: box.y + y * box.height / 412, id, radiusX: 3, radiusY: 3, force: 1 })) });
}
async function ringDown(kind = 'red', button = false, point = [206, 176]) {
  // Deliberate next attempts respect the documented450ms anti-double-tap guard.
  const guardRemaining = 460 - (performance.now() - lastThrowAt);
  if (guardRemaining > 0) await page.waitForTimeout(guardRemaining);
  const saved = await http(); const target = CAPTURE_RING.targetRadii[saved.state.wildFormId % 4];
  await page.waitForFunction(({ kind, target }) => {
    const ring = window.__autoCaptureAudit.rings.at(-1);
    return ring && performance.now() - ring.time < 150 && (kind === 'red' ? Math.abs(ring.radius - target) > 27 : kind === 'orange' ? Math.abs(ring.radius - target - 18) < 2 : Math.abs(ring.radius - target) < 3);
  }, { kind, target });
  lastPaintedCue = await page.evaluate(() => window.__autoCaptureAudit.paintedCues.at(-1)?.text);
  if (button) {
    const box = await page.locator('#device-confirm-button').boundingBox();
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: box.x + box.width / 2, y: box.y + box.height / 2, id: 1 }] });
  } else await touch('touchStart', [point]);
  lastThrowAt = performance.now();
}
async function throwRing(kind = 'red', button = false, point = [206, 176]) {
  await ringDown(kind, button, point);
  await touch('touchEnd');
}

async function noCommand(name, fn) {
  const before = await http(), count = posts.length; await fn(); await page.waitForTimeout(160);
  assert.equal(posts.length, count, name); assert.deepEqual(await http(), before, name); checks.push({ name, commands: 0, saveUnchanged: true });
}
async function throwAndWait(kind, expectedScreen) {
  const before = await http(), count = posts.length;
  const response = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await throwRing(kind); const result = await (await response).json(); await ui.onScreen(expectedScreen);
  if (expectedScreen === 'capture-aim') await page.waitForFunction(() => !document.querySelector('#device-back').disabled);
  else await page.waitForFunction(() => !['feedback', 'capture-flight'].includes(document.querySelector('#device-ui').dataset.layout) && document.querySelector('[data-device-action="menu"]')?.disabled === false);
  assert.equal(posts.length, count + 1); const command = posts.at(-1).body.events;
  assert.equal(command.length, 1); assert.equal(command[0].type, 'ring-capture');
  assert.deepEqual(result.state, native([...before.events, ...command]));
  assertTimingCommand(command, before, result, kind);
  checks.push({ name: `${label}: ${kind} throw`, beforeAttempts: before.state.captureAttempts, afterAttempts: result.state.captureAttempts, oneThrow: true, nativeStateMatches: true });
  return result;
}
function assertTimingCommand(command, before, result, grade) {
  assert.equal(command.length, 1); assert.equal(command[0].type, 'ring-capture');
  assert.ok(Number.isInteger(command[0].value) && command[0].value >= 0 && command[0].value < CAPTURE_RING.cycleMs);
  const sampled = sampleCaptureRing(command[0].value, before.state.wildFormId);
  assert.equal(sampled.grade, grade);
  const chance = captureRingChance(before.state.wildCaptureChance, grade);
  assert.equal(result.state.lastCapture.chance, chance);
  assert.equal(lastPaintedCue, `${grade.toUpperCase()} ${chance}% - TAP PLAY AREA`, 'visible pre-throw odds equal the committed core chance');
  assert.notEqual(result.state.lastCapture.result, 'miss', 'every timing quality connects and rolls the documented nonzero chance');
  assert.deepEqual(result.state, native([...before.events, ...command]), 'same phase event yields identical actual native roll/state');
}
async function productionPreview() {
  const before = await fixture('authorized Agumon production capture', paused, 'touch', 'no-preference', false, 2, true);
  assert.equal(before.state.wildFormId, 18); assert.equal(before.state.wildHp, 49); assert.equal(before.state.wildCaptureChance, 52);
  const art = await http('/api/roster/art/18');
  assert.equal(art.formId, 18); assert.equal(art.sha256, '779cd8dce46b6721b0bda2d46b1d5de6e92298db49a7a1d1ed3fb6425a4de2e9');
  assert.equal(hash(Buffer.from(art.packText)), art.sha256);
  await page.waitForFunction(() => window.__autoCaptureAudit.heroDraws > 0 && document.querySelector('#screen-surface').dataset.backgroundVisible === 'meadow');
  const heroBounds = await page.evaluate(() => window.__autoCaptureAudit.heroBounds);
  assert.deepEqual(heroBounds.source, [21, 37, 22, 27], 'all three genuine idle frames fit together without padded-canvas shrinkage');
  const [heroX, heroY, heroWidth, heroHeight] = heroBounds.destination;
  assert.equal(heroX + heroWidth / 2, 206); assert.equal(heroY + heroHeight / 2, 176); assert.equal(heroHeight, 176);
  assert.equal(await ui.screen.locator('button').filter({ hasText: /^throw$/i }).count(), 0);
  const clipStart = await page.evaluate(() => performance.now());
  const recording = await page.evaluate(async () => {
    const canvas = document.querySelector('#display'), stream = canvas.captureStream(60);
    const mimeType = ['video/webm;codecs=vp9', 'video/webm;codecs=vp8', 'video/webm'].find(type => MediaRecorder.isTypeSupported(type));
    if (!mimeType) { stream.getTracks().forEach(track => track.stop()); return null; }
    const chunks = [], recorder = new MediaRecorder(stream, { mimeType, videoBitsPerSecond: 2500000 });
    const ended = new Promise(resolve => { recorder.onstop = resolve; });
    recorder.ondataavailable = event => { if (event.data.size) chunks.push(event.data); };
    recorder.start(); await new Promise(resolve => setTimeout(resolve, 5000)); recorder.stop(); await ended;
    const endedAt = performance.now();
    stream.getTracks().forEach(track => track.stop());
    return { mimeType, endedAt, bytes: Array.from(new Uint8Array(await new Blob(chunks, { type: mimeType }).arrayBuffer())) };
  });
  assert.ok(recording, 'installed Chrome must support canvas video recording');
  const clipPath = join(evidenceDir, 'capture-agumon-motion.webm'), clipBytes = Buffer.from(recording.bytes); writeFileSync(clipPath, clipBytes);
  clips.push({ path: clipPath, sha256: hash(clipBytes), bytes: clipBytes.length, mimeType: recording.mimeType, privateArtwork: true,
    content: 'Actual480×480 application canvas for5seconds; DOM heading/footer are visible in companion screenshot, not in canvas recording.' });
  const frames = await page.evaluate(({ start, end }) => window.__autoCaptureAudit.rings.filter(r => r.time >= start && r.time <= end), { start: clipStart, end: recording.endedAt });
  const intervals = frames.slice(1).map((frame, index) => frame.time - frames[index].time).sort((a, b) => a - b);
  const medianFrameMs = intervals[Math.floor(intervals.length / 2)], p95FrameMs = intervals[Math.floor(intervals.length * .95)];
  assert.ok(medianFrameMs < 40); assert.ok(frames.length > 120);
  assert.deepEqual(await http(), before, 'recording never changes the game or spends an attempt');
  checks.push({ name: 'Private production preview shows actual encountered Agumon with its meadow scene', formId: 18, wildHp: 49, chance: 52, worldSeed: 18, profileSeed: 2,
    artSha256: art.sha256, background: 'meadow', heroBounds, frames: frames.length, medianFrameMs, p95FrameMs, noAttemptSpent: true });
  // Ready metadata can precede painting, or a cosmetic pack replacement.
  // Capture evidence only after the actual scene bitmap reached the canvas.
  await page.waitForFunction(() => {
    const recent = window.__autoCaptureAudit.rings.slice(-3);
    return document.querySelector('#screen-surface').dataset.backgroundVisible === 'meadow' && recent.length === 3 && recent.every(frame => frame.time - frame.backgroundPaint < 30);
  });
  for (const [grade, chance] of [['red', 5], ['orange', 26], ['green', 52]]) {
    await page.waitForFunction(({ grade, chance }) => window.__autoCaptureAudit.paintedCues.at(-1)?.text === `${grade.toUpperCase()} ${chance}% - TAP PLAY AREA`, { grade, chance });
    await photo(`capture-agumon-meadow-${grade}`);
  }
  const visibleGrades = await page.evaluate(() => ({ colors: window.__autoCaptureAudit.ringColors, targetStyle: window.__autoCaptureAudit.targetStyle, cues: [...new Set(window.__autoCaptureAudit.paintedCues.map(c => c.text))] }));
  assert.deepEqual(visibleGrades.colors.sort(), ['#f6534a', '#ffa43c', '#54ea86'].sort());
  assert.equal(visibleGrades.targetStyle, 'rgba(84, 234, 134, 0.75)');
  assert.deepEqual(visibleGrades.cues.sort(), ['RED 5% - TAP PLAY AREA', 'ORANGE 26% - TAP PLAY AREA', 'GREEN 52% - TAP PLAY AREA'].sort());
  checks.push({ name: 'All three live timing colors and exact odds are visible over the production scene', ...visibleGrades });
  const response = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await ringDown('green', false, [350, 220]); const committed = await (await response).json();
  const command = posts.at(-1).body.events; assertTimingCommand(command, before, committed, 'green');
  assert.deepEqual(committed.state, native([...before.events, ...command]));
  assert.equal(committed.state.lastCapture.result, 'captured'); assert.equal(committed.state.lastCapture.chance, 52);
  await touch('touchEnd'); await ui.onScreen('home');
  await page.waitForFunction(() => document.querySelector('#device-ui').dataset.layout !== 'feedback');
  await photo('agumon-captured');
  checks.push({ name: 'Broad-area direct DOWN captures the actual production foe with unchanged saved odds', beforeRelease: true, point: [350, 220], result: 'captured', chance: 52 });
  sessions.push({ label, ...await page.evaluate(() => window.__autoCaptureAudit) });
  assert.deepEqual(errors, []); assert.deepEqual(sourceHashes(), initialHashes);
  outcome = 'PASS'; console.log(`PASS private production preview: ${frames.length}paintedframes, median${medianFrameMs.toFixed(2)}ms; actual Agumon capture52%.`);
}
try {
  if (process.env.DIGIVICE_CAPTURE_PRODUCTION_PREVIEW === '1') await productionPreview();
  else {
  assert.equal((await http('/api/health')).capabilities.manualAutoCapture, 1);
  assert.equal((await http('/api/health')).capabilities.captureTimingQuality, 1);
  await fixture('full crossing playback and explicit decline');
  const count = posts.length; await ui.tap('wild-auto-start', 'wild-auto-progress');
  await photo('attack-playback'); await ui.onScreen('capture-aim');
  const saved = await http(); assert.equal(saved.state.autoCapture, 1); assert.equal(saved.autoTrace.outcome, 'none');
  assert.equal(posts.length, count + 1); assert.deepEqual(posts.at(-1).body.events, [event('auto-fight')]);
  const observed = await page.evaluate(() => window.__autoCaptureAudit.views);
  const captureIndex = observed.findIndex(v => v.screen === 'capture-aim'); assert.ok(captureIndex > 0);
  const progress = observed.slice(0, captureIndex).filter(v => v.screen === 'wild-auto-progress');
  assert.ok(progress.some(v => v.text.includes(`TURN ${saved.autoTrace.steps.length} / ${saved.autoTrace.steps.length}`)), 'complete crossing turn presented before ball');
  assert.ok(observed[captureIndex].time - progress.at(-1).time >= 500, 'last crossing step gets its presentation interval');
  assert.equal(await ui.screen.locator('#device-back').textContent(), 'Resume fighting');
  checks.push({ name: 'Committed crossing turn finishes before manual capture', steps: saved.autoTrace.steps.length, lastStepMs: observed[captureIndex].time - progress.at(-1).time, noAutomaticCapture: true, geometry: await ui.geometry('auto capture prompt') });
  await photo('capture-prompt');
  await noCommand('Shrinking ring resets without spending an attempt', () => page.waitForTimeout(2700));
  const cycle = await page.evaluate(() => window.__autoCaptureAudit.rings);
  const small = cycle.findIndex(r => r.radius < 25); assert.ok(small >= 0 && cycle.slice(small + 1).some(r => r.radius > 95));
  const ringTimes = cycle.slice(-120).map((r, i, all) => i ? r.time - all[i - 1].time : 0).filter(n => n > 0).sort((a, b) => a - b);
  const medianFrameMs = ringTimes[Math.floor(ringTimes.length / 2)];
  assert.ok(medianFrameMs < 40, `capture ring is painted faster than the former80ms throttle: ${medianFrameMs}ms`);
  checks.push({ name: 'Capture aim paints on animation frames with elapsed-time geometry', medianFrameMs, samples: ringTimes.length });
  await noCommand('Header and clipped corners do not capture', async () => {
    for (const point of [[206, 50], [3, 90]]) { await touch('touchStart', [point]); await touch('touchEnd'); }
  });
  await noCommand('A second finger cannot inherit a contact begun outside the play area', async () => {
    await touch('touchStart', [[206, 50]]); await touch('touchStart', [[206, 50, 1], [206, 176, 2]]); await touch('touchEnd');
  });
  await noCommand('Changing controls or resizing does not itself capture', async () => {
    await page.locator('#device-input-mode').selectOption('buttons'); await page.locator('#device-input-mode').selectOption('touch');
    await ui.onScreen('capture-aim'); await page.setViewportSize({ width: 950, height: 950 }); await page.setViewportSize({ width: 1000, height: 1000 });
  });
  await photo('ring-prompt');
  await ui.back('wild-auto-progress'); await ui.onScreen('wild-auto-result');
  assert.deepEqual(posts.at(-1).body.events, [event('auto-resume')]);
  const resumed = await http(); assert.equal(resumed.state.phase, 'home'); assert.equal(resumed.state.autoCapture, 0);
  assert.ok(resumed.autoTrace.steps.every(s => s.action !== 'capture')); await photo('resumed-result');
  await noCommand('Declining does not reopen the capture prompt', () => page.waitForTimeout(1200));
  assert.equal(await ui.screen.getAttribute('data-screen'), 'wild-auto-result');

  await fixture('reload and three red escapes in buttons mode', paused, 'buttons', 'reduce', false, 1);
  await noCommand('Button mode never throws without a press', () => page.waitForTimeout(1500));
  await page.reload(); await ui.onScreen('capture-aim');
  checks.push({ name: 'Saved Auto pause restores ring after reload without an action', saveUnchanged: (await http()).state.autoCapture === 1 });
  const beforeDown = await http(), countBeforeDown = posts.length;
  const downResponse = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await ringDown('red', false, [60, 206]);
  const downCommitted = await (await downResponse).json();
  assert.equal(downCommitted.state.captureAttempts, 1); assert.equal(downCommitted.state.lastCapture.result, 'escaped');
  assert.equal(posts.length, countBeforeDown + 1); assertTimingCommand(posts.at(-1).body.events, beforeDown, downCommitted, 'red');
  assert.deepEqual(downCommitted.state, native([...beforeDown.events, ...posts.at(-1).body.events]));
  await ui.onScreen('capture-aim');
  await noCommand('Held contact and movement cannot spend the next attempt after playback', async () => { await touch('touchMove', [[350, 206]]); await page.waitForTimeout(500); });
  await touch('touchEnd');
  checks.push({ name: 'Off-center main-area DOWN commits before release and consumes one attempt', commandsBeforeRelease: 1, referencePoint: [60, 206] });
  for (let n = 2; n <= 3; n++) {
    const result = await throwAndWait('red', n === 3 ? 'home' : 'capture-aim');
    assert.equal(result.state.lastCapture.result, 'escaped'); assert.equal(result.state.lastCapture.attempt, n);
    assert.equal(result.state.phase, n === 3 ? 'home' : 'encounter');
  }
  await photo('three-red-escapes-home');

  await fixture('red timing retains a real capture chance', paused, 'touch', 'reduce');
  for (let n = 1; n <= 3; n++) {
    const red = await throwAndWait('red', n === 3 ? 'home' : 'capture-aim');
    assert.equal(red.state.lastCapture.chance, 5);
    assert.equal(red.state.lastCapture.result, n === 3 ? 'captured' : 'escaped');
  }
  checks.push({ name: 'Red timing really captures at its nonzero 5% chance on a deterministic native roll', chance: 5, result: 'captured' });

  await fixture('connected throw cinematic', paused);
  const connected = await throwAndWait('green', 'home'); assert.equal(connected.state.lastCapture.result, 'captured');
  const views = await page.evaluate(() => window.__autoCaptureAudit.views);
  assert.ok(views.some(v => v.text.includes('Connected…'))); assert.ok(views.some(v => v.text.includes('Holding…')));
  checks.push({ name: 'On-target timed tap preserves impact, wiggles, existing probability and saved result', result: connected.state.lastCapture.result }); await photo('captured-home');

  await fixture('lost auto pause response and exact retry', encounter, 'touch', 'reduce');
  let committed;
  await page.route('**/api/save-sync', async route => { const response = await route.fetch(); committed = await response.json(); await route.abort('failed'); }, { times: 1 });
  await ui.tap('wild-auto-start', 'saving');
  await page.waitForFunction(() => document.querySelector('[data-device-action="retry"]')?.disabled === false);
  assert.equal(committed.state.autoCapture, 1); const raw = posts.at(-1).raw;
  await noCommand('Pending recovery owns the screen and cannot throw automatically', async () => { assert.equal(await page.locator('#screen-surface').getAttribute('data-capture-aim'), 'false'); await page.waitForTimeout(250); });
  await photo('pending-recovery'); await page.reload(); await ui.onScreen('saving');
  await ui.tap('retry', 'capture-aim'); assert.equal(posts.at(-1).raw, raw);
  const restored = await http(); assert.equal(restored.revision, committed.revision); assert.deepEqual(restored.state, committed.state); assert.deepEqual(restored.autoTrace, committed.autoTrace);
  checks.push({ name: 'Lost response/reload/retry keeps request identity and one commit', exactRetry: true });
  let committedFlick;
  await page.route('**/api/save-sync', async route => { const response = await route.fetch(); committedFlick = await response.json(); await route.abort('failed'); }, { times: 1 });
  await throwRing('red'); await ui.onScreen('saving');
  await page.waitForFunction(() => document.querySelector('[data-device-action="retry"]')?.disabled === false);
  const ringDraws = await page.evaluate(() => window.__autoCaptureAudit.targetDraws); await page.waitForTimeout(250);
  assert.equal(await page.evaluate(() => window.__autoCaptureAudit.targetDraws), ringDraws, 'uncertain response clears capture overlay');
  const flickRaw = posts.at(-1).raw; assert.equal(committedFlick.state.captureAttempts, 1);
  await noCommand('Uncertain throw disables the capture interaction', async () => { assert.equal(await page.locator('#screen-surface').getAttribute('data-capture-aim'), 'false'); await page.waitForTimeout(250); });
  await ui.tap('retry', 'capture-aim'); assert.equal(posts.at(-1).raw, flickRaw);
  assert.equal((await http()).revision, restored.revision + 1); assert.equal((await http()).state.captureAttempts, 1);
  checks.push({ name: 'Lost timing throw response retries exactly once and restores the remaining throws', exactRetry: true, attempts: 1 });

  await fixture('legacy Flick outbox retry preserves its original meaning', paused, 'touch', 'reduce');
  const legacyBefore = await http();
  const legacyPending = { deviceId: identity.deviceId, rulesVersion: 13, baseRevision: legacyBefore.revision,
    batchId: crypto.randomUUID(), events: [event('flick', 0)] };
  await page.evaluate(value => localStorage.setItem('digivice.dev.pending.v1', JSON.stringify(value)), legacyPending);
  await page.reload(); await ui.onScreen('saving'); await ui.tap('retry', 'capture-aim');
  const legacyAfter = await http();
  assert.deepEqual(posts.at(-1).body.events, [event('flick', 0)]);
  assert.equal(legacyAfter.state.lastCapture.result, 'miss'); assert.equal(legacyAfter.state.lastCapture.chance, 0);
  assert.deepEqual(legacyAfter.state, native([...legacyBefore.events, event('flick', 0)]));
  checks.push({ name: 'Previously queued legacy Flick retries unchanged, including old zero-chance misses', unchangedWire: true, chance: 0 });

  await fixture('tactical button capture remains available', [event('hatch', 1), event('walk', 100), event('magic'), event('heavy'), event('magic')], 'buttons', 'reduce');
  const tacticalBefore = await http();
  for (let i = 0; i < 10 && await ui.screen.locator('[data-selected=true]').getAttribute('data-device-action') !== 'capture'; i++) await page.locator('#device-back-button').tap();
  assert.equal(await ui.screen.locator('[data-selected=true]').getAttribute('data-device-action'), 'capture');
  await page.locator('#device-confirm-button').tap(); await ui.onScreen('capture-aim');
  const tacticalResponse = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await throwRing('green', true);
  const tacticalAfter = await (await tacticalResponse).json();
  assertTimingCommand(posts.at(-1).body.events, tacticalBefore, tacticalAfter, 'green'); assert.deepEqual(tacticalAfter.state, native([...tacticalBefore.events, ...posts.at(-1).body.events]));
  checks.push({ name: 'Tactical action button uses immediate DOWN and the original core odds', nativeStateMatches: true });
  await fixture('eligible exploration steps display', [event('hatch', 1)], 'touch', 'reduce');
  assert.equal(await page.locator('#steps').textContent(), '0');
  assert.match(await ui.screen.textContent(), /0 EXPLORE STEPS/);
  await ui.menu('explore');
  const walkingResponse = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await ui.tap('walk'); const walking = await (await walkingResponse).json(); await ui.onScreen('battle');
  assert.deepEqual(posts.at(-1).body.events, [event('explore', 100)]);
  assert.equal(walking.state.walking.eligibleSteps, 100); assert.equal(walking.state.steps, 0);
  assert.equal(await page.locator('#steps').textContent(), '100');
  assert.match(await page.locator('#display').getAttribute('aria-label'), /100 eligible exploration steps/);
  await ui.menu('explore'); assert.match(await ui.screen.textContent(), /100 EXPLORE STEPS/);
  checks.push({ name: 'Browser labels show eligible exploration steps without changing legacy or physical counters', eligibleSteps: 100, legacySteps: 0, oneExplicitExplore: true });
  await fixture('strong target remains ineligible; lost world setup response', [event('hatch', 1), event('walk', 100)], 'touch', 'reduce', true);
  assert.equal(await ui.control('capture').isDisabled(), true);
  await noCommand('A full-health target keeps capture disabled and cannot arm automatically', async () => { assert.equal(await page.locator('#screen-surface').getAttribute('data-capture-aim'), 'false'); await page.waitForTimeout(150); });
  await fixture('world setup failure blocks only new exploration', [event('hatch', 1)], 'touch', 'reduce', 'reject');
  await ui.menu('explore');
  await noCommand('Failed world setup cannot queue a fallback-seeded walk', async () => { await ui.tap('walk'); await page.waitForTimeout(300); });
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), null);
  await page.unroute('**/api/world/seed');
  const initializedWalk = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await ui.tap('walk'); const afterInitialization = await (await initializedWalk).json();
  assert.equal(afterInitialization.state.worldSeed, 12345);
  const initializedHistory = await http();
  assert.equal(initializedHistory.events.filter(e => e.type === 'world-seed').length, 1);
  assert.deepEqual(afterInitialization.state, native(initializedHistory.events));
  checks.push({ name: 'Exploration waits for durable independent world setup, then one normal action succeeds', worldSeedKnown: true });
  await fixture('old service without timing-quality capability', paused, 'touch', 'reduce', false, 12345, false, false);
  assert.equal(await ui.control('capture').isDisabled(), true);
  await noCommand('Missing capability fails closed without mapping grades to legacy Flick', async () => { assert.equal(await page.locator('#screen-surface').getAttribute('data-capture-aim'), 'false'); await ui.control('capture').evaluate(button => button.click()); });
  await fixture('orange timing uses half the eligible chance', paused, 'touch', 'reduce', false, 1);
  const orange = await throwAndWait('orange', 'capture-aim');
  assert.equal(orange.state.lastCapture.chance, 16); assert.equal(orange.state.lastCapture.result, 'escaped');
  checks.push({ name: 'Orange display and saved roll use exactly half of the same eligible base', chance: 16 });
  await fixture('on-target throw can escape', paused, 'touch', 'no-preference', false, 1);
  const escaped = await throwAndWait('green', 'capture-aim');
  assert.equal(escaped.state.lastCapture.result, 'escaped'); assert.equal(escaped.state.lastCapture.chance, 32);
  assert.equal(escaped.state.lastCapture.attempt, 1); assert.equal(escaped.state.collection.length, 1);
  checks.push({ name: 'Correct timing still permits an escape at the unchanged 32% saved probability', chance: 32, result: 'escaped' });
  await photo('on-target-escaped');
  sessions.push({ label, ...await page.evaluate(() => window.__autoCaptureAudit) });
  for (const session of sessions) assert.ok(session.pointerEvents.every(e => e.trusted && e.pointerType === 'touch'));
  assert.deepEqual(errors, []); assert.deepEqual(sourceHashes(), initialHashes);
  outcome = 'PASS'; console.log(`PASS direct timing capture browser: ${checks.length} checks, trusted gestures, complete playback, resume, reload, three red escapes and exact retry.`);
  }
} catch (error) { failure = String(error.stack || error); if (page && !page.isClosed()) { sessions.push({ label, ...await page.evaluate(() => window.__autoCaptureAudit) }); await photo('failure'); console.error(await page.locator('#device-ui').innerText()); } throw error; }
finally {
  writeFileSync(join(evidenceDir, 'evidence.json'), JSON.stringify({ outcome, failure, sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(), sourceFiles: initialHashes,
    coreSha256: hash(readFileSync(corePath)), checks, screenshots, clips, errors, commands: posts.map(({ label, body }) => ({ label, baseRevision: body.baseRevision, events: body.events })), sessions,
    limitations: [...limitations, 'Headless Chromium and synthetic service identities only; no physical device access.', 'Regression screenshots use neutral placeholders; separately marked production preview uses authorized private project art and stays local.', 'No native nearby opponent capture path exists in this browser harness.'],
  }, null, 2) + '\n');
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

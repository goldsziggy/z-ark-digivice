// Compatibility check only: an isolated local store and installed browser.
// No sensor proof, physical firmware acceptance or multiplayer claim.
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { openPlaytestTools, hatchFirstEgg } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-rules12-browser-'));
const app = await startServer({ dataDir, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, port: 0 });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
try {
  const page = await browser.newPage({ viewport: { width: 1200, height: 1000 }, reducedMotion: 'reduce' });
  const errors = []; page.on('pageerror', error => errors.push(error.message)); page.setDefaultTimeout(10_000);
  await page.goto(base); await openPlaytestTools(page); await page.locator('#start-pairing').click();
  const claimed = page.waitForResponse(r => r.url().endsWith('/api/pairing/claim') && r.status() === 201);
  await page.locator('#claim-device').click(); const identity = await (await claimed).json();
  assert.deepEqual([identity.state.schemaVersion, identity.state.rulesVersion], [17, 13]);
  await page.waitForFunction(() => document.querySelectorAll('[data-device-action^="starter-"]').length === 11);
  const offersBefore = await (await fetch(`${base}/api/save`, { headers: { authorization: `Bearer ${identity.token}` } })).json();
  assert.equal(offersBefore.state.onboarding.offers.length, 3); assert.equal(new Set(offersBefore.state.onboarding.offers).size, 3);
  assert.ok(offersBefore.state.onboarding.offerSeed); assert.equal(offersBefore.revision, 1);
  await page.reload(); await openPlaytestTools(page);
  await page.waitForFunction(() => document.querySelectorAll('[data-device-action^="starter-"]').length === 11);
  const offersReloaded = await (await fetch(`${base}/api/save`, { headers: { authorization: `Bearer ${identity.token}` } })).json();
  assert.deepEqual(offersReloaded, offersBefore, 'reload must not re-seed or advance revision');
  await hatchFirstEgg(page);
  const fed = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await page.locator('[data-event="feed"]').click(); let saved = await (await fed).json();
  assert.ok(saved.state.care.offenseBonus >= 1); assert.ok(saved.state.care.protectionBonus >= 1);
  assert.equal(saved.state.phase, 'home'); assert.equal(saved.state.walking.name, 'Normal');
  const sync = async (batchId, events) => {
    const response = await fetch(`${base}/api/save-sync`, { method: 'POST', headers: { authorization: `Bearer ${identity.token}`, 'content-type': 'application/json' },
      body: JSON.stringify({ rulesVersion: 13, baseRevision: saved.revision, batchId, events }) });
    assert.equal(response.status, 200); saved = await response.json(); return saved;
  };
  await sync('browser-walking-threshold', [{ type: 'encounter-seed', value: 123456789 }, { type: 'mode', value: 1 }, { type: 'explore', value: 1000 }]);
  assert.equal(saved.state.wildRules, 13);
  await page.reload(); await openPlaytestTools(page);
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), saved.revision);
  await sync('browser-walking-auto-result', [{ type: 'auto', value: 0 }]);
  assert.ok(saved.autoTrace.steps.length); const foregroundSequence = saved.state.foregroundSequence;
  await sync('browser-background-after-auto', [{ type: 'accrue-steps', value: 1 }]);
  assert.equal(saved.state.foregroundSequence, foregroundSequence); assert.ok(saved.state.sequence > foregroundSequence);
  const resolved = structuredClone(saved);
  await page.reload(); await openPlaytestTools(page);
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), saved.revision);
  await page.waitForFunction(() => document.querySelector('#device-ui')?.dataset.screen === 'wild-auto-result');
  const previous = { deviceId: identity.deviceId, rulesVersion: 11, baseRevision: saved.revision, batchId: 'browser-keeps-old-request', events: [{ type: 'feed', value: 0 }] };
  const originalPending = JSON.stringify(previous);
  await page.evaluate(value => localStorage.setItem('digivice.dev.pending.v1', value), originalPending);
  await page.reload(); await openPlaytestTools(page);
  await page.locator('#retry-sync').waitFor({ state: 'visible' });
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), originalPending);
  const rejected = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 409);
  await page.locator('#retry-sync').click(); assert.equal((await (await rejected).json()).error, 'migration_required');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), originalPending, 'old command stays exact, never relabeled');
  const response = await fetch(`${base}/api/save`, { headers: { authorization: `Bearer ${identity.token}` } }), current = await response.json();
  assert.equal(current.revision, resolved.revision); assert.deepEqual(current.state, resolved.state); assert.deepEqual(current.autoTrace, resolved.autoTrace);
  // An uninitialized migrated egg with an old queued action cannot be seeded
  // behind that action's expected revision, even during first UI display.
  const post = async (path, body, token) => {
    const response = await fetch(`${base}${path}`, { method: 'POST', headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) }, body: JSON.stringify(body) });
    assert.ok(response.ok); return response.json();
  };
  const pairStart = await post('/api/pairing/start', {}), egg = await post('/api/pairing/claim', { code: pairStart.code });
  const oldEggPending = JSON.stringify({ deviceId: egg.deviceId, rulesVersion: 11, baseRevision: 0, batchId: 'old-egg-pending-kept', events: [{ type: 'hatch', value: 1 }] });
  await page.evaluate(({ egg, pending }) => { localStorage.setItem('digivice.dev.identity.v1', JSON.stringify({ deviceId: egg.deviceId, token: egg.token })); localStorage.setItem('digivice.dev.pending.v1', pending); }, { egg, pending: oldEggPending });
  await page.reload(); await openPlaytestTools(page); await page.locator('#retry-sync').waitFor({ state: 'visible' });
  const waitingEgg = await (await fetch(`${base}/api/save`, { headers: { authorization: `Bearer ${egg.token}` } })).json();
  assert.equal(waitingEgg.revision, 0); assert.equal(waitingEgg.state.onboarding.offerSeed, 0);
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), oldEggPending);

  // Fresh isolated identity: show the actual post-throw chance through all
  // three cosmetic wiggle stages, then reload the committed capture result.
  const captureStart = await post('/api/pairing/start', {}), captureIdentity = await post('/api/pairing/claim', { code: captureStart.code });
  let captureSave = captureIdentity;
  const captureSync = async (events) => captureSave = await post('/api/save-sync', { rulesVersion: 13, baseRevision: captureSave.revision, batchId: crypto.randomUUID(), events }, captureIdentity.token);
  await captureSync([{ type: 'hatch', value: 1 }, { type: 'explore', value: 1000 }]);
  for (let i = 0; !captureSave.state.wildCaptureChance && i < 8; i++) await captureSync([{ type: i % 2 ? 'attack' : 'magic', value: 0 }]);
  assert.ok(captureSave.state.wildCaptureChance);
  await page.evaluate(value => { localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(value)); localStorage.removeItem('digivice.dev.pending.v1'); }, { deviceId: captureIdentity.deviceId, token: captureIdentity.token });
  await page.emulateMedia({ reducedMotion: 'no-preference' }); await page.reload(); await openPlaytestTools(page);
  await page.locator('#device-input-mode').selectOption('buttons');
  await page.locator('[data-event="capture"]').waitFor({ state: 'visible' });
  assert.doesNotMatch(await page.locator('[data-event="capture"]').textContent(), /\d+%/);
  const thrown = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await page.locator('[data-event="capture"]').click(); captureSave = await (await thrown).json();
  assert.ok(captureSave.state.lastCapture.chance >= 10 && captureSave.state.lastCapture.chance <= 90);
  await page.waitForFunction(chance => document.querySelector('#device-ui').textContent.includes(`${chance}% throw chance`), captureSave.state.lastCapture.chance);
  await page.reload(); await openPlaytestTools(page);
  const restoredCapture = await (await fetch(`${base}/api/save`, { headers: { authorization: `Bearer ${captureIdentity.token}` } })).json();
  assert.deepEqual(restoredCapture.state, captureSave.state, 'interrupted presentation never rerolls the saved throw');
  assert.deepEqual(errors, []);
  console.log('PASS rules12 browser: eleven durable egg choices, reload without reroll, hatch/care effects, walking save reload, Auto trace reload, frozen old pending request retained and rejected without mutation; pending egg seeding blocked, actual postthrow chance shown, interrupted reveal restored; no page errors.');
} finally {
  await browser.close(); await new Promise((resolve, reject) => app.server.close(error => error ? reject(error) : resolve())); app.close();
  rmSync(dataDir, { recursive: true, force: true });
}

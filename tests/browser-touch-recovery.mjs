// Real service migration/recovery, isolated historical stores, and trusted
// on-screen touches only. No keyboard, external buttons, or fabricated stats.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { BATTLE_PENDING_KEY, BATTLE_ARCHIVE_KEY } from '../web/battle-client.js';
import { touchDevice } from './touch-browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const sourcePaths = ['web/app.js', 'web/device-screen.js', 'web/styles.css', 'web/device-navigation.js', 'web/battle-client.js', 'tests/browser-touch-recovery.mjs', 'tests/touch-browser-tools.mjs'];
const sources = () => sourcePaths.map(path => ({ path, sha256: hash(readFileSync(path)) }));
const sourceFiles = sources();
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/park/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/park/digivice-battle');
const binaries = [corePath, battleCorePath].map(path => ({ path, sha256: hash(readFileSync(path)) }));
const fixturePath = 'tests/fixtures/auto-tuning-baseline16-service.json';
const fixture = JSON.parse(readFileSync(fixturePath));
const care = fixture.care.checkpoints.afterCare;
const practice = fixture.practice.find(entry => entry.mode === 'auto');
const identity = { deviceId: fixture.care.deviceId, token: Buffer.alloc(32, 88).toString('base64url') };
assert.equal(identity.deviceId, practice.deviceId);
const careBody = fixture.care.commands.find(command => command.body.events.some(event => event.type === 'auto')).body;
const carePending = JSON.stringify({ deviceId: identity.deviceId, ...careBody });
const CARE_PENDING = 'digivice.dev.pending.v1', IDENTITY = 'digivice.dev.identity.v1';
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-touch-recovery-'));
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(care.store));
for (const name of ['battle-store.json', 'battle-store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(practice.store));
const app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const checks = [], geometry = [], inputs = [], errors = [], posts = [], gets = [];
let outcome = 'FAIL', failure;

async function client(pendingKey, pending) {
  const context = await browser.newContext({ viewport: { width: 1000, height: 1000 }, hasTouch: true, reducedMotion: 'reduce' });
  await context.addInitScript(({ identity, pendingKey, pending, identityKey }) => {
    if (location.protocol !== 'http:') return;
    localStorage.setItem(identityKey, JSON.stringify(identity));
    localStorage.setItem(pendingKey, pending);
    window.__touchRecovery = { keyboard: 0, externalControls: 0, touches: [] };
    document.addEventListener('keydown', () => window.__touchRecovery.keyboard++);
    document.addEventListener('click', e => { if (e.target.closest('#device-back-button, #device-confirm-button')) window.__touchRecovery.externalControls++; });
    document.addEventListener('touchstart', e => {
      const target = e.target.closest('#device-ui button');
      if (target) window.__touchRecovery.touches.push({ screen: document.querySelector('#device-ui').dataset.screen, action: target.dataset.deviceAction || target.id, trusted: e.isTrusted });
    });
  }, { identity, pendingKey, pending, identityKey: IDENTITY });
  const page = await context.newPage(); page.setDefaultTimeout(10000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => {
    const path = new URL(request.url()).pathname;
    if (request.method() === 'POST' && /^\/api\/(save-sync|battle\/(start|act))$/.test(path)) posts.push({ path, raw: request.postData() });
    if (request.method() === 'GET' && ['/api/save', '/api/battle'].includes(path)) gets.push(path);
  });
  await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
  const ui = touchDevice(page);
  async function check(label) {
    const result = await ui.geometry(label);
    for (const button of result.buttons) assert.ok(button.box.width >= 69.9 && button.box.height >= 69.9, `${label}: ${button.id} needs a 70px target at 412px`);
    geometry.push(result);
  }
  async function command(id, path, status) {
    await ui.reveal(id);
    const response = page.waitForResponse(r => r.url().endsWith(path) && r.request().method() === 'POST' && r.status() === status)
      // Battle client intentionally cancels rejected response bodies. Its
      // status plus the recovery UI is the observable contract, not that body.
      .then(response => path.startsWith('/api/battle/') && status >= 400 ? { status: response.status() } : response.json());
    await ui.tap(id); return response;
  }
  await page.goto(base);
  await page.locator('.device-screen').evaluate(element => {
    element.style.setProperty('box-sizing', 'content-box', 'important');
    for (const dimension of ['width', 'height']) element.style.setProperty(dimension, '412px', 'important');
    element.style.setProperty('margin-inline', 'auto', 'important');
  });
  return { context, page, ui, check, command, stored: key => page.evaluate(key => localStorage.getItem(key), key) };
}
async function read(path = '/api/save') {
  const response = await fetch(base + path, { headers: { Authorization: `Bearer ${identity.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function captureInputs(client) {
  const value = await client.page.evaluate(() => window.__touchRecovery);
  assert.equal(value.keyboard, 0); assert.equal(value.externalControls, 0);
  assert.ok(value.touches.length > 0 && value.touches.every(touch => touch.trusted)); inputs.push(value);
}

try {
  const c = await client(CARE_PENDING, carePending), { ui } = c;
  await ui.onScreen('saving'); await c.check('saving: historical request kept');
  const saveBefore = await read(), battleBefore = await read('/api/battle');
  assert.equal(posts.length, 0); assert.equal(await c.stored(CARE_PENDING), carePending);
  assert.equal(await ui.screen.locator('#device-back').isDisabled(), true);
  const rejected = await c.command('retry', '/api/save-sync', 409);
  assert.equal(rejected.error, 'migration_required');
  await ui.tap('open-recovery', 'recovery'); await c.check('recovery: rejected request review');
  await ui.back('saving'); assert.equal(await c.stored(CARE_PENDING), carePending);
  await ui.tap('open-recovery', 'recovery'); await ui.tap('recovery-confirm', 'recovery-confirm'); await c.check('recovery-confirm: explicit removal review');
  await ui.back('recovery'); assert.equal(await c.stored(CARE_PENDING), carePending);
  await ui.tap('recovery-confirm', 'recovery-confirm'); await ui.tap('recovery-back', 'recovery');
  assert.equal(await c.stored(CARE_PENDING), carePending); assert.equal(posts.length, 1);
  await c.command('recovery-retry', '/api/save-sync', 409); await ui.onScreen('recovery'); await ui.back('saving');
  assert.equal(posts.length, 2); assert.equal(posts[0].raw, posts[1].raw); assert.deepEqual(JSON.parse(posts[0].raw), careBody);
  assert.deepEqual(await read(), saveBefore); assert.deepEqual(await read('/api/battle'), battleBefore);
  await ui.tap('open-recovery', 'recovery'); await ui.tap('recovery-confirm', 'recovery-confirm');
  const freshReads = gets.filter(path => path === '/api/save').length;
  await ui.tap('discard-local', 'wild-auto-result');
  assert.equal(await c.stored(CARE_PENDING), null);
  assert.equal(gets.filter(path => path === '/api/save').length, freshReads + 1);
  assert.deepEqual(await read(), saveBefore); assert.deepEqual(await read('/api/battle'), battleBefore);
  assert.equal(posts.length, 2); assert.ok(await c.stored(IDENTITY) === JSON.stringify(identity));
  checks.push({ name: 'Rejected care request', states: ['saving', 'recovery', 'recovery-confirm'], historicalFixture: true,
    startupPosts: 0, backAndKeepPreservePending: true, checkAgainUsesExactBody: true, rejectedPosts: 2,
    explicitRemovalFetchesFreshSave: true, pendingClearedOnlyAfterConfirmation: true, authoritativeCareAndBattleUnchanged: true });
  await captureInputs(c); await c.context.close();

  // Genuine earlier-rules body shape with a distinct, uncommitted request ID.
  // This only seeds browser pending storage, never game stats or service state.
  const battleBody = JSON.stringify({ ...practice.commands[0].body, expectedRevision: battleBefore.revision, requestId: 'touch_uncommitted_legacy_01' });
  const battlePending = JSON.stringify({ formatVersion: 1, rulesVersion: 6, deviceId: identity.deviceId, path: '/api/battle/start', body: battleBody });
  const b = await client(BATTLE_PENDING_KEY, battlePending), bui = b.ui;
  await bui.onScreen('battle-resolve'); await b.check('battle-resolve: uncommitted legacy request');
  const postsBefore = posts.length;
  assert.equal(await bui.screen.locator('#device-back').isDisabled(), true);
  const oldRejected = await b.command('practice-retry', '/api/battle/start', 409);
  assert.equal(oldRejected.status, 409); assert.equal(posts.at(-1).raw, battleBody);
  await bui.tap('practice-legacy-review', 'battle-recovery'); await b.check('battle-recovery: archive review');
  await bui.back('battle-resolve'); assert.equal(await b.stored(BATTLE_PENDING_KEY), battlePending);
  await bui.tap('practice-legacy-review', 'battle-recovery'); await bui.tap('practice-recovery-back', 'battle-resolve');
  await bui.tap('practice-legacy-review', 'battle-recovery'); await bui.tap('practice-legacy-confirm', 'battle-recovery-confirm');
  await b.check('battle-recovery-confirm: explicit archive review');
  await bui.back('battle-recovery'); assert.equal(await b.stored(BATTLE_PENDING_KEY), battlePending);
  await bui.tap('practice-legacy-confirm', 'battle-recovery-confirm'); await bui.tap('practice-legacy-review', 'battle-recovery');
  assert.equal(await b.stored(BATTLE_PENDING_KEY), battlePending); assert.equal(await b.stored(BATTLE_ARCHIVE_KEY), null);
  assert.equal(posts.length, postsBefore + 1); assert.deepEqual(await read('/api/battle'), battleBefore);
  await bui.tap('practice-legacy-confirm', 'battle-recovery-confirm');
  const battleReads = gets.filter(path => path === '/api/battle').length;
  await bui.tap('practice-archive-legacy', 'battle-mode');
  assert.equal(gets.filter(path => path === '/api/battle').length, battleReads + 1);
  assert.equal(await b.stored(BATTLE_PENDING_KEY), null); assert.equal(await b.stored(BATTLE_ARCHIVE_KEY), battlePending);
  assert.equal(posts.length, postsBefore + 1); assert.deepEqual(await read(), saveBefore); assert.deepEqual(await read('/api/battle'), battleBefore);
  assert.ok(await b.stored(IDENTITY) === JSON.stringify(identity));
  await bui.back('home'); await b.check('home: recovery finished through touch');
  checks.push({ name: 'Uncommitted earlier-rules battle request', states: ['battle-resolve', 'battle-recovery', 'battle-recovery-confirm'],
    fixture: 'Historical rules-6 start shape with a new request ID; current expected revision; pending browser storage only.',
    startupPosts: 0, explicitRetryReceivesRealMigrationRejection: true, rejectedRequestBytesPreserved: true, backAndKeepPreservePending: true,
    archiveFetchesFreshBattle: true, archivePreservesExactPendingBytes: true, noCommandReplayedDuringArchive: true, authoritativeCareAndBattleUnchanged: true });
  await captureInputs(b); await b.context.close();
  assert.deepEqual(errors, []); assert.deepEqual(sources(), sourceFiles, 'Source changed during run');
  for (const binary of binaries) assert.equal(hash(readFileSync(binary.path)), binary.sha256, 'Native binary changed during run');
  outcome = 'PASS';
} catch (error) { failure = { message: error.message, stack: error.stack }; throw error; }
finally {
  const report = { outcome, result: outcome, sourceFiles, binaries, fixture: { path: fixturePath, sha256: hash(readFileSync(fixturePath)) },
    checks, geometry, inputSessions: inputs, pageErrors: errors, ...(failure ? { failure } : {}),
    boundary: 'Isolated temporary service and synthetic public fixture identity, removed after execution. No personal saves, credentials, private artwork, hardware, or GUI interaction. Actual trusted touch events on a 412px round CSS surface; not physical display or touchscreen verification.' };
  mkdirSync('docs/evidence', { recursive: true }); writeFileSync('docs/evidence/touch-only-recovery.json', JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ result: outcome, checks: checks.map(check => check.name), geometryChecks: geometry.length, touches: inputs.reduce((sum, value) => sum + value.touches.length, 0), pageErrors: errors, ...(failure ? { failure: failure.message } : {}) }));
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

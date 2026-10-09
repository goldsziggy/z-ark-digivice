// Touch-only navigation against the native core and an isolated service/store.
// No foreground browser, user save, private art, keyboard or physical controls.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { touchDevice } from './touch-browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/park/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/park/digivice-battle');
const hash = data => createHash('sha256').update(data).digest('hex');
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-touch-only-'));
const app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1000, height: 1000 }, hasTouch: true, reducedMotion: 'reduce' });
const page = await context.newPage(); page.setDefaultTimeout(10000);
const ui = touchDevice(page), errors = [], posts = [], checks = [], screenshots = [], actions = [], scrollChecks = [], assetRanges = [], saveChecks = [];
const evidence = process.env.TOUCH_EVIDENCE === '1';
let identity, failure, outcome = 'FAIL';
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) {
    const body = request.postDataJSON(); posts.push({ events: body.events, baseRevision: body.baseRevision });
  }
});
await page.addInitScript(() => {
  window.__touchAudit = { keyboard: 0, externalControls: 0 };
  document.addEventListener('keydown', () => window.__touchAudit.keyboard++);
  document.addEventListener('click', event => {
    if (event.target.closest('#device-back-button, #device-confirm-button')) window.__touchAudit.externalControls++;
  });
});
await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
// Stop automatic expansion warmup before it populates the isolated cache.
// The later explicit touch download uses real signed catalog/pack bytes.
await page.route('**/api/assets/packs/tide-v1/1', route => route.abort('internetdisconnected'));

async function check(label, options) { checks.push(await ui.geometry(label, options)); }
async function photo(name) {
  if (!evidence) return;
  mkdirSync('docs/evidence', { recursive: true });
  const path = `docs/evidence/touch-only-${name}.png`;
  await page.locator('#screen-surface').screenshot({ path }); screenshots.push({ path, sha256: hash(readFileSync(path)), privateArtwork: false });
}
async function choices(label) {
  const ids = await ui.screen.locator('[data-device-index]').evaluateAll(buttons => buttons.filter(button => !button.disabled).map(button => button.dataset.deviceAction));
  for (const id of ids) { await ui.reveal(id); actions.push({ screen: await ui.screen.getAttribute('data-screen'), id }); await check(`${label}: ${id}`); }
  return ids;
}
async function currentSave(credential = identity) {
  const response = await fetch(`${base}/api/save`, { headers: { Authorization: `Bearer ${credential.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function swipeDetails() {
  const panel = ui.screen.locator('.screen-content');
  const before = await panel.evaluate(element => ({ scrollTop: element.scrollTop, scrollHeight: element.scrollHeight, clientHeight: element.clientHeight }));
  assert.ok(before.scrollHeight > before.clientHeight + 20, 'fixture must have scrollable details');
  const box = await panel.boundingBox(), session = await context.newCDPSession(page);
  const x = box.x + box.width / 2, startY = box.y + box.height - 14, endY = box.y + 20;
  try {
    await session.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y: startY }] });
    for (let step = 1; step <= 8; step++) {
      await session.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x, y: startY + (endY - startY) * step / 8 }] });
      await page.waitForTimeout(25);
    }
    await session.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await page.waitForTimeout(400);
    const after = await panel.evaluate(element => element.scrollTop);
    assert.ok(after > before.scrollTop + 10, 'actual touchscreen swipe scrolls details');
    scrollChecks.push({ screen: await ui.screen.getAttribute('data-screen'), before, after, input: 'CDP actual touchStart/move/end; no DOM scroll assignment' });
  } finally { await session.detach(); }
}

try {
  await page.goto(base); await ui.onScreen('home');
  // Measure the application's actual default CSS, without a sizing override.
  // This remains a logical preview, not physical finger-fit proof.
  await check('unpaired home'); await ui.tap('connection', 'connection'); await check('pairing entry');
  await ui.tap('start-pairing');
  await page.waitForFunction(() => !!document.querySelector('[data-device-action="claim-device"]'));
  await check('pairing code');
  const claimed = page.waitForResponse(response => response.url().endsWith('/api/pairing/claim') && response.status() === 201);
  await ui.tap('claim-device', 'starter-select'); identity = await (await claimed).json();
  await page.waitForFunction(() => document.querySelectorAll('[data-device-action^="starter-"]').length === 8);
  const eggs = await choices('egg selector'); assert.equal(eggs.length, 8);
  assert.equal(posts.length, 0); await photo('eight-eggs');
  for (const id of eggs) {
    await ui.tap(id, 'starter-review'); await check(`review and cancel ${id}`); await ui.back('starter-select');
    assert.equal(await ui.control(id).getAttribute('data-selected'), 'true', 'Back retains the reviewed egg');
  }
  assert.equal(posts.length, 0, 'review and Back must not hatch or change the save');
  await ui.tap('starter-1', 'starter-review'); await check('egg review');
  const hatched = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 200);
  await ui.tap('hatch-starter', 'starter-hatched'); const save = await (await hatched).json();
  assert.equal(save.state.onboarding.starterId, 1); assert.equal(posts.length, 1);
  await page.waitForFunction(() => document.querySelector('[data-device-action="meet-starter"]')?.disabled === false);
  await check('hatch result'); await ui.tap('meet-starter', 'home'); await check('partner home');
  const baseline = await currentSave();

  await ui.tap('menu', 'menu'); const menuItems = await choices('main menu');
  assert.deepEqual(menuItems, ['care', 'companions', 'explore', 'cards', 'settings', 'battle-mode', 'stats', 'progression', 'type-chart', 'roster', 'journal']);
  await photo('menu');
  for (const name of menuItems) {
    await ui.menu(name); await check(`primary ${name}`); await choices(`primary ${name}`);
  }
  await ui.menu('care'); await ui.reveal('rest'); await check('care final choice');
  await swipeDetails(); await ui.back('menu');
  await ui.tap('care', 'care'); await swipeDetails();
  await ui.screen.locator('#device-next').tap();
  assert.equal(await ui.screen.locator('.screen-content').evaluate(element => element.scrollTop), 0, 'paging after a swipe restores the next touch action into view');
  await check('care paging after touch scroll');
  await swipeDetails(); await ui.screen.locator('#device-previous').tap();
  assert.equal(await ui.screen.locator('.screen-content').evaluate(element => element.scrollTop), 0, 'previous paging also restores its action into view');
  await check('care previous after touch scroll'); await ui.back('menu');
  await ui.menu('companions'); await ui.tap('member-1', 'companion'); await choices('companion details'); await photo('partner');
  await ui.tap('member-stats', 'stats'); await choices('member stats'); await ui.tap('skills', 'skills'); await check('member skills'); await ui.back('stats');
  await ui.menu('progression'); await ui.tap('evolution-options', 'evolution-options');
  const paths = await choices('evolution paths'); assert.ok(paths.length > 0);
  await ui.tap(paths[0], 'evolution-preview'); await choices('evolution preview');
  await ui.tap('evolution-requirements', 'evolution-requirements'); await check('locked evolution requirements'); await ui.back('evolution-preview');
  await ui.tap('evolution-skills', 'evolution-skills'); await choices('evolution moves');
  await ui.menu('progression'); await ui.tap('evolution-tree', 'evolution-tree');
  await page.waitForFunction(() => !!document.querySelector('[data-device-action^="tree-"]'));
  const nodes = await choices('evolution graph'); await ui.tap(nodes.find(id => id.startsWith('tree-')), 'evolution-node');
  await choices('graph node'); await ui.tap('evolution-links', 'evolution-links'); await choices('graph routes'); await ui.back('evolution-node');
  await ui.tap('evolution-node-skills', 'evolution-node-skills'); await choices('graph moves');

  await ui.menu('settings'); await choices('settings'); await ui.tap('sound', 'sound'); await choices('sound controls');
  await ui.tap('toggle-sound'); await ui.tap('volume-up'); await ui.tap('volume-down'); await ui.tap('toggle-music'); await ui.tap('toggle-music'); await ui.tap('toggle-sound');
  await check('sound controls after touches'); await ui.back('settings');
  await ui.tap('connection', 'connection'); await check('connection status');
  assert.equal(await ui.screen.locator('input').count(), 0, 'current browser connection screen has no physical Wi-Fi credential entry');
  await ui.back('settings'); await ui.tap('artwork', 'artwork'); await choices('artwork settings');
  await ui.tap('open-assets', 'asset-packs');
  await page.waitForFunction(() => !!document.querySelector('[data-device-action^="pack-"]'));
  const packs = await choices('asset packs'); await ui.tap(packs[0], 'asset-progress'); await choices('pack detail'); await check('asset pack detail');
  await ui.back('asset-packs'); await ui.tap('pack-tide-v1', 'asset-progress');
  await page.unroute('**/api/assets/packs/tide-v1/1');
  let releaseRange, paused = false;
  const heldRange = new Promise(resolve => { releaseRange = resolve; });
  await page.route('**/api/assets/packs/tide-v1/1', async route => {
    const range = route.request().headers().range;
    assetRanges.push({ phase: paused ? 'resume' : 'initial', range });
    if (!paused && range === 'bytes=16384-32767') await heldRange;
    try { await route.continue(); } catch { /* The deliberate Pause aborted this held request. */ }
  });
  await ui.tap('asset-install');
  await page.waitForFunction(() => document.querySelector('#asset-progress').value >= 16384);
  await check('download pause action'); await ui.tap('asset-pause');
  await page.waitForFunction(() => document.querySelector('#device-screen-title').textContent === 'Download paused');
  await check('download paused'); paused = true; releaseRange();
  await ui.tap('asset-install');
  await page.waitForFunction(() => document.querySelector('#device-screen-title').textContent === 'Artwork is ready');
  assert.equal(assetRanges.find(entry => entry.phase === 'resume')?.range, 'bytes=16384-32767', 'touch Resume reuses the first persisted16KiB range');
  await check('download resumed and verified'); await page.unroute('**/api/assets/packs/tide-v1/1');
  await ui.menu('settings'); await ui.tap('saves', 'saves'); await choices('saved playtests');
  await ui.tap('save-confirm', 'save-confirm'); await check('new save confirmation'); await ui.tap('back', 'saves');
  assert.equal((await currentSave()).revision, baseline.revision, 'browsing and cancelled dialogs send no game change');

  await ui.menu('roster'); await page.waitForFunction(() => !!document.querySelector('[data-device-action^="roster-form-"]'));
  const forms = await choices('catalog first page');
  await ui.tap(forms.find(id => id.startsWith('roster-form-')), 'roster-detail');
  await page.waitForFunction(() => document.querySelector('[data-device-action="roster-stats"]')?.disabled === false);
  const detailActions = await choices('catalog details ready');
  assert.ok(detailActions.length >= 5 && detailActions.includes('roster-stats') && detailActions.includes('roster-graph'), 'loaded catalog details expose their actual actions');
  await ui.tap('roster-stats', 'roster-stats'); await choices('catalog stats'); await ui.tap('roster-growth', 'roster-growth'); await choices('catalog growth');
  await ui.tap('roster-moves', 'roster-moves'); await choices('catalog moves'); await ui.back('roster-growth'); await ui.back('roster-stats'); await ui.back('roster-detail');
  await ui.tap('roster-notes', 'roster-notes'); await choices('catalog notes'); await ui.back('roster-detail'); await ui.back('roster');
  await ui.tap('roster-filters', 'roster-filters'); await choices('catalog filters'); await ui.tap('roster-letters', 'roster-letters');
  const letters = await choices('catalog letters'); assert.equal(letters.length, 27); await ui.back('roster-filters');
  await ui.tap('roster-stages', 'roster-stages'); await choices('catalog stages');
  await ui.menu('journal'); await page.waitForFunction(() => !!document.querySelector('[data-device-action^="roster-form-"]'));
  await choices('discovery journal'); await photo('journal'); await ui.home();
  for (const width of [390, 320]) {
    await page.setViewportSize({ width, height: 844 });
    await check(`phone${width} home`, { expectedWidth: null });
    await ui.menu('care'); await check(`phone${width} care`, { expectedWidth: null });
    await ui.back('menu'); await check(`phone${width} menu`, { expectedWidth: null });
    await ui.tap('settings', 'settings'); await check(`phone${width} settings`, { expectedWidth: null });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `phone${width} has no horizontal document overflow`);
    await ui.home();
  }
  assert.deepEqual(await currentSave(), baseline, 'all navigation after hatch leaves the persisted game exactly unchanged');
  assert.equal(posts.length, 1);
  await page.setViewportSize({ width: 1000, height: 1000 });
  await ui.menu('settings'); await ui.tap('saves', 'saves'); await ui.tap('save-confirm', 'save-confirm');
  const newClaim = page.waitForResponse(response => response.url().endsWith('/api/pairing/claim') && response.status() === 201);
  await ui.tap('new-playtest', 'starter-select'); const secondIdentity = await (await newClaim).json();
  assert.notEqual(secondIdentity.deviceId, identity.deviceId);
  await page.waitForFunction(() => document.querySelectorAll('[data-device-action^="starter-"]').length === 8);
  await check('new saved playtest egg'); await ui.tap('starter-2', 'starter-review'); await check('new saved playtest review');
  const secondHatch = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 200);
  await ui.tap('hatch-starter', 'starter-hatched'); const secondSave = await (await secondHatch).json();
  assert.equal(secondSave.state.onboarding.starterId, 2);
  await page.waitForFunction(() => document.querySelector('[data-device-action="meet-starter"]')?.disabled === false);
  await check('new saved playtest hatched'); await ui.tap('meet-starter', 'home');
  const secondBaseline = await currentSave(secondIdentity);
  assert.deepEqual(secondBaseline.state, secondSave.state);
  await ui.menu('settings'); await ui.tap('saves', 'saves'); await ui.reveal(`load-${identity.deviceId}`); await check('load original saved playtest');
  await ui.tap(`load-${identity.deviceId}`);
  await page.waitForFunction(id => document.querySelector('#device-id').textContent === id, identity.deviceId);
  await ui.onScreen('saves'); await ui.home();
  await check('original saved playtest restored');
  assert.deepEqual(await currentSave(), baseline, 'creating and loading another playtest preserves the original save');
  assert.deepEqual(await currentSave(secondIdentity), secondBaseline, 'loading the original also preserves the second save');
  assert.equal(await page.locator('#saved-playtest-select option').count(), 2);
  saveChecks.push({ createdSecondPlaytestThroughTouch: true, secondStarterId: 2, originalLoadedThroughTouch: true, bothSavesPreserved: true, savedSlots: 2 });
  assert.equal(posts.length, 2); assert.deepEqual(errors, []);
  assert.deepEqual(await page.evaluate(() => window.__touchAudit), { keyboard: 0, externalControls: 0 });
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  outcome = 'PASS'; console.log('PASS: touch-only pairing, all eight egg reviews, cancellation/hatch, every primary menu, partner/stats, progression/locked evolution/graph, settings/sound/artwork/packs, create/hatch/load saved playtests, loaded catalog details/filter alphabet/stages/journal; actual touch scroll and paging; asset Pause/Resume retains16KiB; default412px and390/320phone target checks; zero keyboard or physical-button events; both playtest saves preserved.');
} catch (error) { failure = String(error.stack || error); throw error; }
finally {
  if (evidence) {
    mkdirSync('docs/evidence', { recursive: true });
    const sourcePaths = ['web/app.js', 'web/device-screen.js', 'web/styles.css', 'tests/browser-touch.mjs', 'tests/touch-browser-tools.mjs'];
    writeFileSync('docs/evidence/touch-only-navigation.json', JSON.stringify({ outcome, failure, sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
      sourceFiles: sourcePaths.map(path => ({ path, sha256: hash(readFileSync(path)) })), binaries: [corePath, battleCorePath].map(path => ({ name: path.split('/').at(-1), sha256: hash(readFileSync(path)) })),
      scope: 'Headless browser only; 412 CSS pixels; minimum 44 CSS-pixel targets; original placeholders; not physical touch, Wi-Fi provisioning or firmware display proof.',
      checks, actions, scrollChecks, assetRanges, saveChecks, screenshots, errors, gameCommands: posts,
      limitations: ['Physical1.46in panel comfort is unmeasured;44 display pixels correspond to about4mm.', 'No physical Wi-Fi entry exists in this browser UI.', 'Battle, capture, Auto playback, recovery and hardware touch are covered separately.'] }, null, 2) + '\n');
  }
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

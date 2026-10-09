// Actual native rules and isolated HTTP store. All onboarding navigation uses
// the two device buttons; fixture injection supplies only an old saved identity.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-onboarding-browser-'));
const legacy = { deviceId: `dv_${'6'.repeat(24)}`, token: Buffer.alloc(32, 6).toString('base64url'), seed: 12345 };
const originalStore = { formatVersion: 3, gameSchemaVersion: 4, rulesVersion: 3, devices: [{
  deviceId: legacy.deviceId, tokenHash: createHash('sha256').update(legacy.token).digest('hex'),
  seed: legacy.seed, revision: 0, legacy: null, events: [], receipts: [],
}] };
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(originalStore));
let app = await startServer({ dataDir, port: 0 });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' });
const page = await context.newPage(); page.setDefaultTimeout(8000);
const screen = page.locator('#device-ui'), left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
const errors = [], posts = [], sizes = [], screenshots = [];
const evidence = process.env.ONBOARDING_EVIDENCE === '1';
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) posts.push(request.postDataJSON()); });
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
const selected = () => screen.locator('[data-selected="true"]').getAttribute('data-device-action');
async function choose(id, execute = true) {
  for (let count = 0; count < 16; count++) {
    if (await selected() === id) { if (execute) await right.click(); return; }
    await left.click();
  }
  throw new Error(`Two-button selection cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function read(identity, path = '/api/save') {
  const response = await fetch(`${base}${path}`, { headers: { Authorization: `Bearer ${identity.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function practice(id) {
  await choose(id, false);
  const response = page.waitForResponse(response => /\/api\/battle\/(start|act)$/.test(response.url()) && response.status() === 200 && response.request().method() === 'POST');
  await right.click(); return (await response).json();
}
async function geometry(label) {
  const result = await screen.evaluate(root => {
    const surface = root.closest('.device-screen').querySelector('#screen-surface').getBoundingClientRect();
    const cx = surface.x + surface.width / 2, cy = surface.y + surface.height / 2, radius = surface.width / 2;
    const buttons = [...root.querySelectorAll('button')].filter(button => button.getClientRects().length);
    const boxes = buttons.map(button => ({ label: button.textContent.trim(), box: button.getBoundingClientRect() }));
    const failures = [];
    for (const { label, box } of boxes) if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > radius + 2)) failures.push(`clipped: ${label}`);
    for (const button of buttons.filter(button => button.dataset.starterId)) {
      const parent = button.getBoundingClientRect();
      for (const element of button.querySelectorAll('[data-starter-name], [data-starter-stage], .screen-choice-count, .starter-result, .starter-egg')) {
        const box = element.getBoundingClientRect();
        if (box.left < parent.left - 1 || box.right > parent.right + 1 || box.top < parent.top - 1 || box.bottom > parent.bottom + 1)
          failures.push(`starter content leaves card: ${element.textContent.trim() || element.className}`);
      }
    }
    for (let i = 0; i < boxes.length; i++) for (let j = i + 1; j < boxes.length; j++) {
      const a = boxes[i].box, b = boxes[j].box;
      if (Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 1) failures.push(`overlap: ${boxes[i].label}/${boxes[j].label}`);
    }
    const canvas = document.querySelector('#display');
    return { failures, surface: [surface.width, surface.height], canvas: [canvas.width, canvas.height] };
  });
  assert.deepEqual(result.failures, [], label); assert.deepEqual(result.canvas, [480, 480]);
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  sizes.push({ label, surface: result.surface, canvas: result.canvas });
}
async function nominalSize() {
  await page.locator('.device-screen').evaluate(element => {
    element.style.setProperty('box-sizing', 'content-box', 'important');
    element.style.setProperty('width', '201.6px', 'important'); element.style.setProperty('height', '201.6px', 'important');
    element.style.setProperty('margin-inline', 'auto', 'important');
  });
  const box = await page.locator('#screen-surface').boundingBox(); assert.ok(Math.abs(box.width - 201.6) < 1);
}
async function photo(name) {
  if (!evidence) return;
  mkdirSync('docs/evidence', { recursive: true });
  const path = `docs/evidence/onboarding-${name}.png`;
  await page.locator('#screen-surface').screenshot({ path }); screenshots.push(path);
}
const native = (mode, events = []) => JSON.parse(execFileSync(resolve('build/digivice-core'), [mode, '12345'], {
  input: events.map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', timeout: 2000, maxBuffer: 32768,
}));

try {
  const oldBefore = await read(legacy);
  assert.equal(oldBefore.revision, 0); assert.equal(oldBefore.state.sequence, 0);
  assert.equal(oldBefore.state.creature, 'Mote'); assert.deepEqual(oldBefore.state, native('--replay'));
  assert.deepEqual(oldBefore.state.onboarding, { completed: true, starterId: null });

  await page.goto(`${base}/?controls=buttons`); await onScreen('home');
  await choose('connection'); await choose('start-pairing'); await choose('claim-device', false);
  const claim = page.waitForResponse(response => response.url().endsWith('/api/pairing/claim') && response.status() === 201);
  await right.click(); const identity = await (await claim).json(); await onScreen('starter-select');
  const egg = await read(identity);
  assert.equal(egg.revision, 0); assert.equal(egg.state.sequence, 0); assert.equal(egg.state.phase, 'egg');
  assert.equal(egg.state.creature, null); assert.equal(egg.state.combat, null); assert.deepEqual(egg.state.collection, []);
  assert.deepEqual(egg.state, native('--replay-onboarding'));
  const catalogResponse = await fetch(`${base}/api/starters`); assert.equal(catalogResponse.status, 200);
  const catalog = await catalogResponse.json();
  assert.deepEqual(catalog.starters.map(entry => entry.name), ['Impmon', 'Agumon', 'Gabumon', 'Patamon', 'Tentomon', 'Palmon', 'Gomamon', 'Renamon']);
  await page.waitForFunction(() => document.querySelectorAll('[data-device-action^="starter-"]').length === 8);

  // All eight choices and their confirmation screens are reachable without
  // changing any game state. Holding Back retains the exact highlighted egg.
  for (const starter of catalog.starters) {
    await choose(`starter-${starter.id}`, false);
    const chosen = screen.locator('[data-selected="true"]');
    assert.equal(await chosen.getAttribute('data-starter-id'), String(starter.id));
    assert.equal(await chosen.locator('[data-starter-name]').textContent(), starter.name);
    assert.ok((await chosen.locator('[data-starter-stage]').textContent()).includes('Rookie'));
    await geometry(`starter ${starter.id} mobile selection`);
    if (starter.id === 1) await photo('egg-carousel-mobile');
    await right.click(); await onScreen('starter-review');
    assert.equal(await screen.getAttribute('data-starter-id'), String(starter.id));
    assert.ok((await screen.textContent()).includes(starter.name)); await geometry(`starter ${starter.id} mobile review`);
    if (starter.id === 1) await photo('impmon-review-mobile');
    await holdDeviceBack(page); await onScreen('starter-select');
    assert.equal(await selected(), `starter-${starter.id}`, 'held Back release must not advance the restored choice');
  }
  assert.equal(posts.length, 0); assert.deepEqual(await read(identity), egg);
  await holdDeviceBack(page); await onScreen('starter-select'); assert.equal(posts.length, 0);
  await nominalSize();
  for (const starter of catalog.starters) {
    await choose(`starter-${starter.id}`, false); await geometry(`starter ${starter.id} nominal2.1in selection`);
    if (starter.id === 1) await photo('egg-carousel-nominal-2.1in');
    await right.click(); await onScreen('starter-review'); await geometry(`starter ${starter.id} nominal2.1in review`);
    if (starter.id === 1) await photo('impmon-review-nominal-2.1in');
    await holdDeviceBack(page); await onScreen('starter-select');
  }
  assert.equal(posts.length, 0); assert.deepEqual(await read(identity), egg);

  // The actual server commits once while the browser loses the acknowledgment.
  // Double press and Back cannot replace or discard the durable request.
  await choose('starter-8'); await onScreen('starter-review');
  let committed;
  await page.route('**/api/save-sync', async route => {
    const response = await route.fetch(); committed = await response.json();
    // Keep the first request genuinely in flight across both button releases.
    // Otherwise an immediate lost reply can expose the legitimate Retry action
    // before the second click, which is a different input state.
    await page.waitForTimeout(180); await route.abort('failed');
  });
  await choose('hatch-starter', false); await right.dblclick({ delay: 25 }); await onScreen('saving');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="retry"]')?.disabled);
  assert.equal(posts.length, 1, 'a double confirm sends one hatch request');
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  assert.deepEqual(JSON.parse(pending).events, [{ type: 'hatch', value: 8 }]);
  assert.equal(committed.revision, 1); assert.equal(committed.state.sequence, 1);
  assert.deepEqual(committed.state, native('--replay-onboarding', [{ type: 'hatch', value: 8 }]));
  assert.deepEqual(committed.state.combat, catalog.starters[7].combat);
  await holdDeviceBack(page); await onScreen('saving');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  await page.unroute('**/api/save-sync');
  await new Promise((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  app = await startServer({ dataDir, port });
  await page.reload(); await onScreen('saving');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  const retry = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 200);
  await choose('retry'); const replayed = await (await retry).json(); assert.deepEqual(replayed, committed);
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') === null);
  assert.deepEqual(posts[1], posts[0], 'exact request body and batch identity survive browser and server restart');
  assert.equal(posts.length, 2); assert.equal((await read(identity)).revision, 1);
  await page.waitForFunction(() => ['starter-hatched', 'home'].includes(document.querySelector('#device-ui')?.dataset.screen));
  // Reload already fetched the committed state; an equal-revision receipt may
  // correctly skip its old presentation rather than replaying the hatch.
  if (await screen.getAttribute('data-screen') === 'starter-hatched') {
    await geometry('committed hatch result'); await choose('meet-starter');
  }
  await onScreen('home');
  assert.ok((await screen.textContent()).includes('Renamon'));
  await page.reload(); await onScreen('home'); assert.deepEqual((await read(identity)).state, committed.state);
  assert.equal(posts.length, 2, 'reload never automatically hatches a completed save');
  await geometry('Renamon placeholder home mobile'); await photo('renamon-home-mobile');
  await nominalSize(); await geometry('Renamon placeholder home nominal2.1in'); await photo('renamon-home-nominal-2.1in');

  // The new species must pass the real browser battle validator, execute a
  // native exchange and retreat without touching the companion's saved state.
  const beforePractice = await read(identity), gamePosts = posts.length;
  await choose('menu'); await choose('battle-mode'); await onScreen('battle-mode');
  await choose('practice-start'); await onScreen('battle-select-mode');
  const started = await practice('practice-tactical'); await onScreen('battle-choice');
  assert.equal(started.battle.companion.species, 'renamon');
  assert.deepEqual(started.battle.playerCombat, committed.state.combat); await geometry('Rookie practice choice nominal2.1in');
  const exchanged = await practice('practice-physical'); await onScreen('battle-result');
  assert.equal(exchanged.battle.exchanges, 1); assert.equal(exchanged.battle.phase, 'defend');
  assert.ok(exchanged.battle.lastTurn); await geometry('Rookie practice result nominal2.1in');
  await choose('practice-continue'); await onScreen('battle-choice'); await holdDeviceBack(page); await onScreen('battle-mode');
  const ended = await practice('practice-retreat'); assert.equal(ended.battle.status, 'retreated');
  await onScreen('battle-result'); await choose('practice-continue'); await onScreen('battle-mode');
  assert.deepEqual(await read(identity), beforePractice); assert.equal(posts.length, gamePosts);

  // A second fresh playtest supplies the normally acknowledged hatch/result
  // presentation, while proving its creation preserves the first companion.
  for (let count = 0; count < 12 && await screen.getAttribute('data-screen') !== 'home'; count++) await holdDeviceBack(page);
  await onScreen('home'); await choose('menu'); await choose('settings'); await choose('saves'); await choose('save-confirm');
  await choose('new-playtest', false);
  const newClaim = page.waitForResponse(response => response.url().endsWith('/api/pairing/claim') && response.status() === 201);
  await right.click(); const secondIdentity = await (await newClaim).json(); await onScreen('starter-select');
  await choose('starter-1'); await onScreen('starter-review'); await choose('hatch-starter', false);
  const normalHatch = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 200);
  await right.click(); const normalResult = await (await normalHatch).json();
  assert.equal(normalResult.state.creature, 'Impmon'); assert.equal(normalResult.state.onboarding.starterId, 1);
  await onScreen('starter-hatched');
  await page.waitForFunction(() => document.querySelector('[data-device-action="meet-starter"]')?.disabled === false);
  await geometry('Impmon acknowledged hatch nominal2.1in'); await photo('impmon-hatched-nominal-2.1in');
  await page.locator('.device-screen').evaluate(element => { element.style.cssText = ''; });
  await geometry('Impmon acknowledged hatch mobile'); await photo('impmon-hatched-mobile');
  await choose('meet-starter'); await onScreen('home');
  assert.deepEqual((await read(secondIdentity)).state, normalResult.state); assert.deepEqual(await read(identity), beforePractice);

  // An old zero-event identity has a real Mote and skips first-run setup, even
  // though its revision/sequence are both zero and the browser profile is new.
  const oldContext = await browser.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' });
  await oldContext.addInitScript(identity => { if (!localStorage.getItem('digivice.dev.identity.v1')) localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)); }, legacy);
  const oldPage = await oldContext.newPage(); let oldPosts = 0;
  oldPage.on('request', request => { if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) oldPosts++; });
  oldPage.on('pageerror', error => errors.push(error.message));
  await oldPage.goto(base); await oldPage.waitForFunction(() => document.querySelector('#device-ui')?.dataset.screen === 'home' && document.querySelector('#device-screen-title')?.textContent.includes('Mote'));
  assert.equal(await oldPage.locator('[data-device-action="hatch-starter"]').count(), 0);
  await oldPage.reload(); await oldPage.waitForFunction(() => document.querySelector('#device-screen-title')?.textContent.includes('Mote'));
  assert.equal(oldPosts, 0); assert.deepEqual(await read(legacy), oldBefore); await oldContext.close();
  assert.deepEqual(errors, []); assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  console.log('PASS: actual two-button eight-starter selection/review; Back makes zero game POSTs and retains choice; all eight at mobile and nominal 201.6 CSS px / 480 canvas; double confirm sends one hatch; committed lost ACK survives browser+service restart; exact retry/home; no second hatch; Rookie practice start/native exchange/retreat leaves pet save unchanged; new playtest preserves first partner; normal Impmon hatch/result/home; old zero-event Mote skips onboarding unchanged.');
  console.log(JSON.stringify({ geometryChecks: sizes.length, nominalSizing: '201.6 CSS px equals 2.1 in at nominal 96 CSS px/in; uncalibrated physical preview, not a device measurement.', sizes, screenshots }));
} finally {
  await browser.close(); if (app.server.listening) await new Promise(resolve => app.server.close(resolve)); app.close();
  rmSync(dataDir, { recursive: true, force: true });
}

// Risk-based real-browser coverage for individual companions and versioned retries.
// Uses an existing Playwright installation; it is not a runtime dependency.
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { openPlaytestTools } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-collection-browser-'));
const app = await startServer({ dataDir, port: 0 });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1280, height: 1050 }, reducedMotion: 'reduce' });
const page = await context.newPage();
const errors = [];
page.on('pageerror', error => errors.push(error.message));
let current;
let identity;

async function action(selector, expectedRevision) {
  const response = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await page.locator(selector).click();
  current = await (await response).json();
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), expectedRevision ?? current.revision);
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') === null);
  return current.state;
}

async function select(id) {
  if (current.state.activeCreatureId !== id) await action(`[data-select-member="${id}"]`);
  assert.equal(current.state.activeCreatureId, id);
  assert.equal(await page.locator(`#collection-grid [data-member-id="${id}"]`).getAttribute('data-active'), 'true');
}

async function encounter() {
  await action('[data-event="rest"]');
  await action('[data-event="walk"]');
  assert.equal(current.state.phase, 'encounter');
  assert.match(await page.locator('#encounter-copy').textContent(), new RegExp(`wild ${current.state.wildName}`));
  for (const button of await page.locator('[data-select-member]').all()) assert.equal(await button.isDisabled(), true);
  await action('#swipe-card');
  while (current.state.phase === 'encounter' && current.state.wildHp > Math.floor(current.state.wildMaxHp / 2)) {
    await action('[data-event="attack"]');
  }
  for (let attempt = 0; attempt < 3 && current.state.phase === 'encounter'; attempt++) {
    await action('[data-event="capture"]');
  }
  while (current.state.phase === 'encounter') await action('[data-event="attack"]');
}

try {
  await page.goto(base);
  await openPlaytestTools(page);
  await page.locator('#start-pairing').click();
  const claim = page.waitForResponse(r => r.url().endsWith('/api/pairing/claim') && r.status() === 201);
  await page.locator('#claim-device').click();
  identity = await (await claim).json(); // Keep the development token in memory; never print it.
  current = identity;
  await page.waitForFunction(() => document.querySelectorAll('#collection-grid [data-member-id]').length === 1);
  assert.equal(identity.state.schemaVersion, 4);
  assert.equal(identity.state.rulesVersion, 3);
  assert.equal(await page.locator('#collection-count').textContent(), '1 / 8');

  // Downloaded original packs must render both active and wild family art.
  for (const packId of ['tide-v1', 'ember-v1']) {
    await page.locator('#asset-pack-select').selectOption(packId);
    await page.locator('#asset-download').click();
    await page.waitForFunction(() => document.querySelector('#asset-pack-detail').textContent.includes('Saved locally'));
  }

  for (let count = 0; count < 12 && !['rill', 'cinder'].every(species => current.state.collection.some(member => member.species === species)); count++) {
    // Freshly captured members stay at level one, making a capture-friendly
    // attack possible while checking selection through the actual UI.
    await select(current.state.collection.at(-1).id);
    await encounter();
  }
  const rill = current.state.collection.find(member => member.species === 'rill');
  const cinder = current.state.collection.find(member => member.species === 'cinder');
  assert.ok(rill && cinder, 'the deterministic encounter cycle must produce both playable families');

  await select(rill.id);
  await action('[data-event="feed"]');
  await action('[data-event="play"]');
  const caredRill = structuredClone(current.state.collection.find(member => member.id === rill.id));
  assert.equal(await page.locator('#creature-name').textContent(), caredRill.name);
  assert.match(await page.locator('#pack-name').textContent(), /tide-v1/);
  assert.equal(await page.locator('#companion-art-status').isVisible(), false);

  await select(cinder.id);
  await action('[data-event="feed"]');
  const caredCinder = structuredClone(current.state.collection.find(member => member.id === cinder.id));
  assert.deepEqual(current.state.collection.find(member => member.id === rill.id), caredRill, 'care for Cinder must not modify Rill');
  assert.match(await page.locator('#pack-name').textContent(), /ember-v1/);
  await select(rill.id);
  assert.equal(current.state.bond, caredRill.bond);
  assert.equal(current.state.energy, caredRill.energy);
  assert.deepEqual(current.state.collection.find(member => member.id === cinder.id), caredCinder);

  const beforeReload = structuredClone(current.state.collection);
  await page.reload();
  await openPlaytestTools(page);
  await page.waitForFunction(name => document.querySelector('#creature-name').textContent === name, caredRill.name);
  assert.equal(await page.locator(`#collection-grid [data-member-id="${rill.id}"]`).getAttribute('data-active'), 'true');
  assert.equal(await page.locator('#bond').textContent(), String(caredRill.bond));
  const restored = await (await fetch(`${base}/api/save`, { headers: { Authorization: `Bearer ${identity.token}` } })).json();
  assert.deepEqual(restored.state.collection, beforeReload);
  current = restored;

  // A member selection can commit even when its reply is lost. Keep its rules,
  // ID and exact batch across reload and retry rather than selecting twice.
  let committedSelection;
  await page.route('**/api/save-sync', async route => {
    const result = await route.fetch(); committedSelection = await result.json(); await route.abort('failed');
  });
  await page.locator(`[data-select-member="${cinder.id}"]`).click();
  await page.locator('#retry-sync').waitFor({ state: 'visible' });
  const savedPending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  assert.equal(JSON.parse(savedPending).rulesVersion, 3);
  await page.unroute('**/api/save-sync');
  await page.reload();
  await openPlaytestTools(page);
  await page.locator('#retry-sync').waitFor({ state: 'visible' });
  await action('#retry-sync');
  assert.equal(current.revision, committedSelection.revision);
  assert.equal(current.state.activeCreatureId, cinder.id);

  // Missing old rules marker is preserved verbatim. It cannot be replayed as
  // a rules-3 event. Only explicit discard may unblock the current save.
  const legacy = { deviceId: identity.deviceId, baseRevision: current.revision, batchId: crypto.randomUUID(), events: [{ type: 'feed', value: 0 }] };
  const legacyRaw = JSON.stringify(legacy);
  await page.evaluate(raw => localStorage.setItem('digivice.dev.pending.v1', raw), legacyRaw);
  await page.reload();
  await openPlaytestTools(page);
  await page.locator('#retry-sync').waitFor({ state: 'visible' });
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), legacyRaw);
  assert.equal(await page.locator('[data-event="feed"]').isDisabled(), true);
  const rejected = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 409);
  await page.locator('#retry-sync').click(); await rejected;
  await page.locator('#discard-conflict').waitFor({ state: 'visible' });
  assert.match(await page.locator('#notice-text').textContent(), /earlier-rules|not replayed/);
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), legacyRaw);
  assert.equal(await page.locator('#revision').textContent(), String(current.revision));
  await page.locator('#discard-conflict').click();
  await page.locator('#discard-conflict').waitFor({ state: 'hidden' });
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), null);

  // Capacity is bounded at eight actual individuals. A full collection keeps
  // existing members and permits battles, but offers no misleading capture.
  for (let attempts = 0; attempts < 24 && current.state.collection.length < 8; attempts++) {
    await select(current.state.collection.at(-1).id);
    await encounter();
  }
  assert.equal(current.state.collection.length, 8);
  assert.equal(await page.locator('#collection-grid [data-member-id]').count(), 8);
  assert.equal(await page.locator('#collection-count').textContent(), '8 / 8');
  await action('[data-event="walk"]');
  assert.equal(await page.locator('[data-event="capture"]').isDisabled(), true);
  assert.match(await page.locator('#encounter-copy').textContent(), /collection is full/);
  while (current.state.phase === 'encounter') await action('[data-event="attack"]');
  assert.equal(current.state.collection.length, 8);
  if (process.env.COLLECTION_SCREENSHOT_DIR) {
    mkdirSync(process.env.COLLECTION_SCREENSHOT_DIR, { recursive: true });
    await page.screenshot({ path: join(process.env.COLLECTION_SCREENSHOT_DIR, 'collection-desktop.png'), fullPage: true });
    await page.locator('.collection-panel').screenshot({ path: join(process.env.COLLECTION_SCREENSHOT_DIR, 'collection-cards.png') });
  }
  await page.setViewportSize({ width: 390, height: 844 });
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'collection cards must fit mobile width');
  if (process.env.COLLECTION_SCREENSHOT_DIR) await page.screenshot({ path: join(process.env.COLLECTION_SCREENSHOT_DIR, 'collection-mobile.png'), fullPage: true });
  assert.deepEqual(errors, []);
  console.log('PASS: individual Rill/Cinder capture and selection; independent care; reload persistence; original family artwork; selection lost-response retry; old pending reconciliation; fixed-eight capacity; mobile collection layout.');
} finally {
  await browser.close();
  await new Promise(resolve => app.server.close(resolve));
  app.close();
  rmSync(dataDir, { recursive: true, force: true });
}

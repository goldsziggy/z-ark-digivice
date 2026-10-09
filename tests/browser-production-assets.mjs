// Normal gameplay must neither request nor reactivate prototype creature packs.
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Use an existing PLAYWRIGHT_MODULE installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-production-art-'));
const includeTestFixtures = process.argv.includes('--test-fixtures');
const expectedPacks = includeTestFixtures ? 11 : 8;
const app = await startServer({ dataDir, port: 0, includeTestFixtures });
const base = `http://127.0.0.1:${app.server.address().port}`;
let browser;
try {
  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
  const page = await browser.newPage();
  const errors = [], toyRequests = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (/\/api\/(?:assets\/packs\/)?(?:starter-v[12]|tide-v1|ember-v1)/.test(request.url())) toyRequests.push(request.url()); });
  await page.goto(base);
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const cached = await page.evaluate(async () => {
      const { IndexedDBAssetStore } = await import('/asset-store.js');
      const store = new IndexedDBAssetStore();
      try { return (await store.read()).packs.length; }
      finally { store.close(); }
    });
    if (cached === expectedPacks) break;
    await page.waitForTimeout(100);
  }
  const state = await page.evaluate(async () => {
    const { default: builtin } = await import('/builtin-pack.js');
    const { IndexedDBAssetStore } = await import('/asset-store.js');
    const store = new IndexedDBAssetStore();
    try { return { builtin: Object.keys(builtin.sprites),
      options: [...document.querySelector('#asset-pack-select').options].map(option => option.value),
      status: document.querySelector('#asset-status').textContent, cached: (await store.read()).packs.map(record => record.entry.id) }; }
    finally { store.close(); }
  });
  assert.deepEqual(state.builtin, []);
  assert.equal(state.options.length, expectedPacks);
  assert.equal(state.cached.length, expectedPacks);
  if (includeTestFixtures) {
    for (const id of ['starter-v2', 'tide-v1', 'ember-v1']) assert.ok(state.cached.includes(id));
    assert.ok(toyRequests.length > 0);
  } else {
    assert.ok(state.options.every(id => id.startsWith('scene-')));
    assert.ok(state.cached.every(id => id.startsWith('scene-')));
    assert.deepEqual(toyRequests, []);
  }
  if (includeTestFixtures) {
    // Model turning off the server's explicit fixture capability after an old
    // browser has saved all three toy packs. Preserve the same real IndexedDB.
    await page.route('**/api/health', async route => {
      const response = await route.fetch(), health = await response.json();
      delete health.capabilities.testFixtures;
      await route.fulfill({ response, json: health });
    });
    toyRequests.length = 0;
    await page.reload();
    await page.waitForFunction(() => document.querySelector('#asset-pack-select')?.options.length === 8);
    const retained = await page.evaluate(async () => {
      const { IndexedDBAssetStore } = await import('/asset-store.js');
      const store = new IndexedDBAssetStore();
      try { return (await store.read()).packs.map(record => record.entry.id); }
      finally { store.close(); }
    });
    assert.equal(retained.length, 11, 'old verified cache is retained, never erased');
    for (const id of ['starter-v2', 'tide-v1', 'ember-v1']) assert.ok(retained.includes(id));
    assert.deepEqual(await page.locator('#asset-pack-select option').evaluateAll(options => options.filter(option => !option.value.startsWith('scene-')).map(option => option.value)), []);
    assert.deepEqual(toyRequests, [], 'fixture-disabled restart must not refresh toy packs');
  }
  assert.deepEqual(errors, []);
  console.log(includeTestFixtures ? 'PASS explicit fixture browser startup: eleven verified packs; disabling fixtures hides all three retained toy packs without deleting IndexedDB data; no page errors.' : 'PASS normal browser startup: eight verified scenes, no toy pack request, empty builtin creature registry, no page errors.');
} finally {
  await browser?.close();
  await new Promise(resolve => app.server.close(resolve)); app.close();
  rmSync(dataDir, { recursive: true, force: true });
}

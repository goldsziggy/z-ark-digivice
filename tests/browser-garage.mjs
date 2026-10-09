// Real browser + local HTTP boundary, with an in-memory Garage adapter only.
// The generated square is original synthetic art; no private files or live
// Garage credentials, buckets, or objects are accessed by this test.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { openPlaytestTools } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const frame = Buffer.alloc(32 * 32 / 2, 0x11).toString('base64');
const animations = Object.fromEntries(['idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate'].map(name => [name, { frameMs: 200, frames: [frame] }]));
const pack = {
  formatVersion: 1, packId: 'personal-garage-test', version: 1,
  license: 'LicenseRef-Personal-Use-Restrictions', paletteEncoding: 'rgb565',
  sprites: { mote: { name: 'Original Garage square', family: 'synthetic', stage: 1, width: 32, height: 32,
    palette: [0, 2016, ...Array(14).fill(0)], transparentIndex: 0, animations } }, effects: {}, icons: {},
};
const packText = JSON.stringify(pack);
const hash = createHash('sha256').update(packText).digest('hex');
const coverage = Object.fromEntries(Object.keys(animations).map(name => [name, {
  kind: name === 'idle' ? 'genuine' : 'reused-fallback', sourceAnimation: 'idle',
  originalFrameCount: name === 'idle' ? 1 : 0, outputFrameCount: 1,
  sourceRects: name === 'idle' ? [[0, 0, 32, 32]] : [], transforms: [],
}]));
const provenanceText = JSON.stringify({
  formatVersion: 1, packId: pack.packId, version: 1, packBytes: Buffer.byteLength(packText), packSha256: hash, decodedBytes: 32 * 32 * 4 * 6,
  provenance: { creator: 'Project test', sourceUrl: 'https://example.com/original-test', game: 'Original synthetic fixture', rights: 'Original test artwork', reuseScope: 'Local test only' },
  coverage: { mote: coverage }, sources: [{ path: 'original-test.png', sha256: 'a'.repeat(64), bytes: 100, width: 32, height: 32 }], conversion: {},
});
const entry = { id: pack.packId, version: 1, kind: 'personal', bytes: Buffer.byteLength(packText), sha256: hash };
const calls = [];
let mode = 'good';
const garageAssets = {
  async list() {
    calls.push('list');
    if (mode === 'unavailable') throw new Error('Synthetic private backend unavailable');
    return { status: 'available', packs: [entry] };
  },
  async fetchPack() { throw new Error('Original pack proxy is not used by this fixture'); },
  async fetchPersonal(id, version) {
    calls.push('personal');
    assert.equal(id, pack.packId); assert.equal(version, 1);
    if (mode === 'unavailable') throw new Error('Synthetic private backend unavailable');
    return { packText: mode === 'corrupt' ? packText.replace('Original Garage square', 'Original Garage squarX') : packText,
      provenanceText: mode === 'bad-provenance' ? '{}' : provenanceText };
  },
};
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-garage-browser-'));
const app = await startServer({ dataDir, port: 0, garageAssets });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1280, height: 1000 }, reducedMotion: 'reduce' });
const page = await context.newPage();
const errors = [], unexpectedRequests = [], garageRequests = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  const url = new URL(request.url());
  if (url.origin !== base || (request.method() !== 'GET' && !['/api/pairing/start', '/api/pairing/claim', '/api/save-sync'].includes(url.pathname))) unexpectedRequests.push(`${request.method()} ${url.pathname}`);
  if (url.pathname.startsWith('/api/garage/')) garageRequests.push({ method: request.method(), authenticated: Boolean(request.headers().authorization) });
});
const statusIncludes = text => page.waitForFunction(text => document.querySelector('#garage-status').textContent.includes(text), text);
const readPersonal = () => page.evaluate(async () => {
  const { IndexedDBAssetStore } = await import('/asset-store.js');
  const store = new IndexedDBAssetStore({ name: 'digivice-personal-art-v1' });
  try { return (await store.read()).personal; } finally { store.close(); }
});

try {
  await page.goto(base);
  await openPlaytestTools(page);
  await page.locator('#garage-check').waitFor();
  await page.locator('#garage-check').click();
  await statusIncludes('Pair a virtual device');
  assert.deepEqual(calls, []); assert.deepEqual(garageRequests, []);
  await page.locator('#start-pairing').click(); await page.locator('#claim-device').click();
  await page.locator('#setup-paired').waitFor({ state: 'visible' });
  const saved = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 200);
  await page.locator('[data-event="feed"]').click();
  const careSave = await (await saved).json();
  const identityBefore = await page.evaluate(() => localStorage.getItem('digivice.dev.identity.v1'));
  await page.locator('#garage-check').evaluate(button => { button.click(); button.click(); });
  await statusIncludes('Choose a personal pack'); assert.deepEqual(calls, ['list']);
  const usePack = page.locator('#garage-packs button');
  await usePack.evaluate(button => { button.click(); button.click(); });
  await statusIncludes('Private artwork saved');
  assert.deepEqual(calls, ['list', 'personal']);
  assert.equal(await page.locator('#creature-name').textContent(), pack.sprites.mote.name);
  const previous = await readPersonal();
  assert.equal(previous.packText, packText); assert.equal(previous.provenanceText, provenanceText); assert.equal(previous.enabled, true);
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.identity.v1')), identityBefore);
  assert.equal(await page.locator('#revision').textContent(), String(careSave.revision));

  // Installed appearances survive reload without a catalog or object read.
  await page.reload();
  await openPlaytestTools(page);
  await page.waitForFunction(name => document.querySelector('#creature-name').textContent === name, pack.sprites.mote.name);
  await page.locator('#setup-paired').waitFor({ state: 'visible' });
  assert.deepEqual(calls, ['list', 'personal']);
  assert.equal(await page.locator('#revision').textContent(), String(careSave.revision));

  mode = 'unavailable';
  await page.locator('#garage-check').click(); await statusIncludes('Existing artwork is retained');
  assert.deepEqual(await readPersonal(), previous);
  assert.equal(await page.locator('#creature-name').textContent(), pack.sprites.mote.name);
  mode = 'good';
  await page.locator('#garage-check').click(); await statusIncludes('Choose a personal pack');
  for (const fault of ['unavailable', 'corrupt', 'bad-provenance']) {
    mode = fault;
    await usePack.click(); await statusIncludes('Existing artwork is retained');
    assert.equal(await usePack.isEnabled(), true);
    assert.deepEqual(await readPersonal(), previous);
    assert.equal(await page.locator('#creature-name').textContent(), pack.sprites.mote.name);
    assert.equal(await page.locator('#revision').textContent(), String(careSave.revision));
    assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.identity.v1')), identityBefore);
  }
  // A successful-looking but oversized HTTP body never reaches the importer.
  await page.route('**/api/garage/personal/**', route => route.fulfill({ status: 200, contentType: 'application/json', body: 'x'.repeat(700 * 1024) }));
  await usePack.click(); await statusIncludes('exceeds its limit');
  assert.deepEqual(await readPersonal(), previous);
  await page.unroute('**/api/garage/personal/**');
  mode = 'good';
  await usePack.click(); await statusIncludes('Private artwork saved');

  const credential = JSON.parse(identityBefore);
  const response = await fetch(`${base}/api/save`, { headers: { Authorization: `Bearer ${credential.token}` } });
  assert.equal(response.status, 200);
  const restored = await response.json();
  assert.equal(restored.revision, careSave.revision); assert.deepEqual(restored.state, careSave.state);
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), null);
  assert.ok(garageRequests.length > 0 && garageRequests.every(request => request.method === 'GET' && request.authenticated));
  assert.deepEqual(unexpectedRequests, []); assert.deepEqual(errors, []);
  console.log('PASS: Garage is explicit and paired; repeated buttons make one request; original synthetic personal pack installs and survives reload without remote reads; unavailable, corrupt, invalid provenance and oversized results preserve artwork, identity and game save; retry succeeds; no uploads or third-party network.');
} finally {
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

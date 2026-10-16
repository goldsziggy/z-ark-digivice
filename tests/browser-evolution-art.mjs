// Presentation fixture only: intercepted read responses supply two GrapLeomon
// targets and synthetic green artwork. Native save bytes and rules never change.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, writeFileSync, rmSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { namedFixture } from './legacy-fixture.ts';
if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to the local Playwright module.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const fixture = namedFixture(), identity = fixture.identities[0];
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-evolution-art-'));
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(fixture.store));
const app = await startServer({ dataDir, port: 0, corePath: resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core'), battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const errors = [], results = [];
const save = async () => { const response = await fetch(`${base}/api/save`, { headers: { Authorization: `Bearer ${identity.token}` } }); assert(response.ok); return response.json(); };
let saved, lastPage;

function artFixture(id) {
  const key = `ds-form-${id}`, frames = [Buffer.alloc(512, 0x11).toString('base64')];
  const animations = Object.fromEntries(['idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate'].map(name => [name, { frameMs: 200, frames }]));
  const pack = { formatVersion: 1, packId: `personal-${key}`, version: 1, license: 'LicenseRef-Personal-Use-Restrictions', paletteEncoding: 'rgb565', sprites: {
    [key]: { name: 'Synthetic green square', family: 'synthetic', stage: 1, width: 32, height: 32, palette: [0, 2016, ...Array(14).fill(0)], transparentIndex: 0, animations }
  }, effects: {}, icons: {} };
  const packText = JSON.stringify(pack), sha256 = createHash('sha256').update(packText).digest('hex');
  const coverage = Object.fromEntries(Object.keys(animations).map(name => [name, { kind: name === 'idle' ? 'genuine' : 'reused-fallback', sourceAnimation: 'idle', originalFrameCount: name === 'idle' ? 1 : 0, outputFrameCount: 1, sourceRects: name === 'idle' ? [[0, 0, 32, 32]] : [], transforms: [] }]));
  const provenance = { formatVersion: 1, packId: pack.packId, version: 1, packBytes: Buffer.byteLength(packText), packSha256: sha256, decodedBytes: 32 * 32 * 4 * 6,
    provenance: { creator: 'Project test', sourceUrl: 'https://example.com/original-test', game: 'Original synthetic fixture', rights: 'Original test artwork', reuseScope: 'Local test only' },
    coverage: { [key]: coverage }, sources: [{ path: 'original-test.png', sha256: 'a'.repeat(64), bytes: 100, width: 32, height: 32 }], conversion: {} };
  return { formId: id, artId: key, version: 1, sha256, packText, provenanceText: JSON.stringify(provenance) };
}
function presentationSave() {
  const copy = structuredClone(saved), s = copy.state, active = s.collection.find(m => m.id === s.activeCreatureId);
  // Deliberately synthetic UI fixture; no evolution is submitted or tested here.
  Object.assign(s, { formId: 29, creature: 'Leomon', species: 'gabumon', stage: 'Champion', worldSeed: 12345 });
  Object.assign(active, { formId: 29, name: 'Leomon', species: 'gabumon', stage: 'Champion' });
  assert.equal(s.evolution.options.length, 2);
  s.evolution.options = s.evolution.options.map((option, index) => ({ ...option, formId: index ? 357 : 30, name: 'GrapLeomon', stage: 'Ultimate', artId: null, eligible: false }));
  return copy;
}
try {
  saved = await save();
  for (const [requested, source] of [[30, 357], [357, 30]]) {
    const context = await browser.newContext({ viewport: { width: 1000, height: 1100 }, reducedMotion: 'reduce' });
    await context.addInitScript(identity => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)), identity);
    const page = await context.newPage(); lastPage = page; page.setDefaultTimeout(8000); page.on('pageerror', error => errors.push(error.message));
    const requests = [], writes = [];
    page.on('request', request => { if (request.method() === 'POST' && request.url().includes('/api/')) writes.push(request.url()); });
    await page.route('**/api/save', route => route.fulfill({ status: 200, json: presentationSave() }));
    await page.route('**/api/roster/art/*', async route => {
      const id = Number(route.request().url().split('/').at(-1)); requests.push(id);
      await route.fulfill(id === source ? { status: 200, json: artFixture(source) } : { status: 404, json: { error: 'fixture-art-absent' } });
    });
    const screen = page.locator('#device-ui'), left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
    const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
    const choose = async id => {
      for (let n = 0; n < 32; n++) {
        if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) { await right.click(); return; }
        await left.click();
      }
      throw new Error(`Unreachable action ${id}`);
    };
    await page.goto(`${base}/?controls=buttons`);
    await page.waitForFunction(id => document.querySelector('#device-id')?.textContent === id, identity.deviceId);
    await onScreen('home');
    await page.waitForFunction(() => document.querySelector('#device-screen-title')?.textContent === 'Leomon');
    await page.locator('[data-device-action="menu"]').click(); await onScreen('menu');
    await choose('progression'); await onScreen('progression'); await choose('evolution-options'); await onScreen('evolution-options');
    const beforeRequests = requests.length;
    await choose(`evolve-${requested}`); await onScreen('evolution-preview');
    await page.waitForFunction(() => {
      const canvas = document.querySelector('#device-ui .screen-portrait'); if (!canvas) return false;
      const pixels = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
      for (let n = 0; n < pixels.length; n += 4) if (pixels[n] === 0 && pixels[n + 1] === 255 && pixels[n + 2] === 0 && pixels[n + 3] === 255) return true;
      return false;
    });
    const targetRequests = requests.slice(beforeRequests);
    assert(targetRequests.includes(requested), 'Visible evolution target is actually requested');
    assert(targetRequests.includes(source), 'Missing exact art fetches the verified alias');
    assert.match(await page.locator('#device-ui .screen-portrait').getAttribute('aria-label'), /GrapLeomon artwork/);
    assert.match(await screen.textContent(), requested === 30 ? /GUARDIAN FORM/i : /MYSTIC FORM/i);
    assert.deepEqual(writes, []); assert.deepEqual(await save(), saved, 'UI-only fixture never mutates native save');
    results.push({ requested, source, targetRequests, sourcePixelsDrawn: true, noSaveWrites: true });
    await context.close();
  }
  assert.deepEqual(errors, []);
  console.log('PASS evolution artwork in browser: both target IDs requested, missing-only alias pixels rendered under logical identity, distinct profile labels, no save writes.');
  if (process.env.DIGIVICE_ART_EVIDENCE) { mkdirSync(process.env.DIGIVICE_ART_EVIDENCE, { recursive: true }); writeFileSync(join(process.env.DIGIVICE_ART_EVIDENCE, 'browser-evolution-art.json'), JSON.stringify({ results, errors, fixture: 'Synthetic UI response and green square; not device save or actual Digimon artwork' }, null, 2) + '\n'); }
} catch (error) {
  console.error({ errors, ui: lastPage ? await lastPage.evaluate(() => ({ title: document.querySelector('#device-screen-title')?.textContent, screen: document.querySelector('#device-ui')?.dataset.screen, notice: document.querySelector('#notice-text')?.textContent, text: document.querySelector('#device-ui')?.textContent })) : null });
  throw error;
} finally { await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true }); }

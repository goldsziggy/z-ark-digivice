// Run with PERSONAL_PACK_DIR pointing to a private generated pack. The test and
// screenshot never include third-party artwork in tracked evidence or exports.
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { openPlaytestTools } from './browser-tools.mjs';
if (!process.env.PLAYWRIGHT_MODULE || !process.env.PERSONAL_PACK_DIR) throw new Error('Set PLAYWRIGHT_MODULE and PERSONAL_PACK_DIR.');
const directory = resolve(process.env.PERSONAL_PACK_DIR);
const packPath = join(directory, 'pack.json'); const provenancePath = join(directory, 'provenance.json');
const inputPack = JSON.parse(readFileSync(packPath, 'utf8'));
const expectedName = inputPack.sprites.mote.name;
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-personal-browser-'));
const app = await startServer({ dataDir, port: 0 });
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1280, height: 1000 } });
const page = await context.newPage();
const errors = []; const unexpectedPosts = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (request.method() === 'POST' && !['/api/pairing/start','/api/pairing/claim','/api/save-sync'].includes(new URL(request.url()).pathname)) unexpectedPosts.push(request.url());
});
try {
  await page.goto(`http://127.0.0.1:${app.server.address().port}`);
  await openPlaytestTools(page);
  await page.locator('#start-pairing').click(); await page.locator('#claim-device').click();
  await page.locator('#setup-paired').waitFor({ state: 'visible' });
  assert.equal(await page.locator('#creature-name').textContent(), 'Mote');
  await page.locator('#personal-files').setInputFiles([packPath, provenancePath]);
  await page.locator('#personal-import').click();
  await page.waitForFunction(name => document.querySelector('#creature-name').textContent === name, expectedName);
  await page.locator('#personal-animation').selectOption('sleep');
  assert.match(await page.locator('#personal-coverage').textContent(), /reuses idle/);
  assert.match(await page.locator('#creature-description').textContent(), /Mote.*rules|rules.*Mote/);
  const saved = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await page.locator('[data-event="feed"]').click();
  const result = await (await saved).json();
  assert.equal(result.state.creature, 'Mote'); assert.equal(result.state.collection.length, 1);
  await page.reload();
  await openPlaytestTools(page);
  await page.waitForFunction(name => document.querySelector('#creature-name').textContent === name, expectedName);
  assert.equal(await page.locator('#revision').textContent(), String(result.revision));
  // Tampered local bytes are rejected before activation; last good art survives.
  const changed = Buffer.from(readFileSync(packPath, 'utf8') + ' ');
  await page.locator('#personal-files').setInputFiles([{ name: 'pack.json', mimeType: 'application/json', buffer: changed }, { name: 'provenance.json', mimeType: 'application/json', buffer: readFileSync(provenancePath) }]);
  await page.locator('#personal-import').click();
  await page.waitForFunction(() => document.querySelector('#personal-status').textContent.includes('Previous artwork is retained'));
  assert.equal(await page.locator('#creature-name').textContent(), expectedName);
  await page.locator('#personal-use').uncheck();
  await page.waitForFunction(() => document.querySelector('#creature-name').textContent === 'Mote');
  await page.locator('#personal-use').check();
  await page.waitForFunction(name => document.querySelector('#creature-name').textContent === name, expectedName);
  for (const id of ['mote','glint','lumen'].filter(id => inputPack.sprites[id])) {
    await page.locator('#personal-creature').selectOption(id);
    await page.locator('#personal-animation').selectOption('idle');
    assert.equal(await page.locator('#personal-name').textContent(), inputPack.sprites[id].name);
  }
  await page.locator('#personal-creature').selectOption('mote');
  await page.setViewportSize({ width: 390, height: 844 });
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  await page.setViewportSize({ width: 1280, height: 1000 });
  mkdirSync(directory, { recursive: true });
  await page.locator('.personal-library').screenshot({ path: join(directory, 'personal-library-preview.png') });
  await page.locator('#device-shell').screenshot({ path: join(directory, 'personal-device-preview.png') });
  await page.locator('#personal-remove').click();
  await page.waitForFunction(() => document.querySelector('#creature-name').textContent === 'Mote');
  assert.equal(await page.locator('#revision').textContent(), String(result.revision));
  await page.reload();
  await openPlaytestTools(page);
  await page.locator('#setup-paired').waitFor({ state: 'visible' });
  assert.equal(await page.locator('#creature-name').textContent(), 'Mote');
  assert.deepEqual(unexpectedPosts, []); assert.deepEqual(errors, []);
  console.log('PASS: private64px import/provenance and explicit fallback; appearance-only care; persistence; tamper rejection retains old art; toggle/removal preserves save; no personal upload; mobile fit.');
} catch (error) {
  console.error({ personalStatus: await page.locator('#personal-status').textContent(), errors }); throw error;
} finally {
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

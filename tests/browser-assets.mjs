// Optional real-browser integration using an existing Playwright installation.
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { openPlaytestTools } from './browser-tools.mjs';
if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-asset-browser-'));
const app = await startServer({ dataDir, port: 0, includeTestFixtures: true });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1280, height: 1000 } });
const page = await context.newPage();
const errors = [];
page.on('pageerror', error => errors.push(error.message));
const waitSaved = () => page.waitForFunction(() => document.querySelector('#asset-pack-detail').textContent.includes('Saved locally'));
try {
  await page.goto(base);
  await openPlaytestTools(page);
  await waitSaved();
  assert.match(await page.locator('#pack-name').textContent(), /starter-v2/);
  await page.locator('#start-pairing').click();
  await page.locator('#claim-device').click();
  await page.locator('#setup-paired').waitFor({ state: 'visible' });
  async function action(selector) {
    const saved = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
    await page.locator(selector).click();
    const result = await (await saved).json();
    await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), result.revision);
    return result;
  }
  await action('[data-event="feed"]');
  await action('[data-event="walk"]');
  await action('#swipe-card');
  await action('[data-event="attack"]');
  const capture = await action('[data-event="capture"]');
  assert.equal(capture.state.captures, 1);

  // Keep one 16KiB range, lose the next, then reload: real IndexedDB resumes.
  const tideRanges = [];
  await page.route('**/api/assets/packs/tide-v1/1', async route => {
    const range = route.request().headers().range;
    tideRanges.push(range);
    if (range === 'bytes=0-16383') await route.continue();
    else await route.abort('internetdisconnected');
  });
  await page.locator('#asset-pack-select').selectOption('tide-v1');
  await page.locator('#asset-download').click();
  await page.waitForFunction(() => document.querySelector('#asset-status').textContent.includes('you can retry'));
  assert.deepEqual(tideRanges, ['bytes=0-16383', 'bytes=16384-32767']);
  await page.unroute('**/api/assets/packs/tide-v1/1');
  await page.reload();
  await openPlaytestTools(page);
  await waitSaved();
  const resumed = [];
  await page.route('**/api/assets/packs/tide-v1/1', async route => { resumed.push(route.request().headers().range); await route.continue(); });
  await page.locator('#asset-pack-select').selectOption('tide-v1');
  await page.locator('#asset-download').click();
  await waitSaved();
  assert.equal(resumed[0], 'bytes=16384-32767');
  await page.unroute('**/api/assets/packs/tide-v1/1');
  await page.locator('#asset-creature-select').selectOption('pelagia');
  await page.locator('#asset-animation-select').selectOption('attack');
  assert.equal(await page.locator('#asset-creature-name').textContent(), 'Pelagia');

  await page.locator('#asset-pack-select').selectOption('ember-v1');
  await page.locator('#asset-download').click();
  await waitSaved();
  await page.locator('#asset-creature-select').selectOption('pyrel');
  await page.locator('#asset-animation-select').selectOption('celebrate');
  // Block all asset network after reload. Starter and expansion come from IDB;
  // game commands still use the explicitly separate local native-core service.
  let assetRequests = 0;
  await page.route('**/api/assets/**', async route => { ++assetRequests; await route.abort('internetdisconnected'); });
  await page.reload();
  await openPlaytestTools(page);
  await waitSaved();
  await page.locator('#asset-pack-select').selectOption('ember-v1');
  await page.locator('#asset-creature-select').selectOption('pyrel');
  await page.locator('#asset-animation-select').selectOption('celebrate');
  assert.equal(await page.locator('#asset-creature-name').textContent(), 'Pyrel');
  await action('[data-event="play"]');
  assert.equal(assetRequests, 0, 'normal art rendering and gameplay must not fetch packs');
  assert.match(await page.locator('#pack-name').textContent(), /starter-v2/);
  // An actually disconnected loaded page keeps animating cached artwork.
  await context.setOffline(true);
  const canvasBefore = await page.locator('#asset-display').evaluate(canvas => canvas.toDataURL());
  await page.waitForTimeout(260);
  const canvasAfter = await page.locator('#asset-display').evaluate(canvas => canvas.toDataURL());
  assert.notEqual(canvasBefore, canvasAfter);
  await context.setOffline(false);
  mkdirSync('docs/evidence', { recursive: true });
  await page.screenshot({ path: 'docs/evidence/asset-simulator-desktop.png', fullPage: true });
  await page.locator('section[aria-labelledby="asset-heading"]').screenshot({ path: 'docs/evidence/asset-library.png' });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: 'docs/evidence/asset-simulator-mobile.png', fullPage: true });
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  assert.deepEqual(errors, []);
  console.log('PASS: real IndexedDB range resume after reload; three packs; animated art offline; starter remains pinned; native-core capture/save and no asset fetch during play; mobile layout.');
} catch (error) {
  console.error({ assetStatus: await page.locator('#asset-status').textContent(), assetDetail: await page.locator('#asset-pack-detail').textContent(), pageErrors: errors });
  throw error;
} finally {
  await browser.close();
  await new Promise(resolve => app.server.close(resolve));
  app.close();
  rmSync(dataDir, { recursive: true, force: true });
}

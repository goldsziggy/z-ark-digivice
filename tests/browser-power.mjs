// Real-time interactions with the isolated browser simulation, not proof of LCD
// rendering or physical power behavior. No game pairing or saved events.
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-power-browser-'));
const diskSnapshot = (root = dataDir) => Object.fromEntries(readdirSync(root, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name)).map(entry =>
  [entry.name, entry.isDirectory() ? diskSnapshot(join(root, entry.name)) : readFileSync(join(root, entry.name))]));
const checks = [], errors = [], requests = [];
let app, browser, failure;
const evidence = resolve('docs/evidence');
mkdirSync(evidence, { recursive: true });
try {
  app = await startServer({ dataDir, port: 0 });
  const initialDisk = diskSnapshot();
  const base = `http://127.0.0.1:${app.server.address().port}`;
  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
  const context = await browser.newContext({ viewport: { width: 1100, height: 1000 }, reducedMotion: 'reduce' });
  await context.addInitScript(() => {
    // Observe browser persistence APIs without supplying application state.
    window.__powerDemoStorageCalls = [];
    for (const method of ['getItem', 'setItem', 'removeItem', 'clear', 'key']) {
      const original = Storage.prototype[method];
      Storage.prototype[method] = function (...args) {
        window.__powerDemoStorageCalls.push(`Storage.${method}`);
        return original.apply(this, args);
      };
    }
    for (const method of ['open', 'deleteDatabase']) {
      const original = IDBFactory.prototype[method];
      IDBFactory.prototype[method] = function (...args) {
        window.__powerDemoStorageCalls.push(`indexedDB.${method}`);
        return original.apply(this, args);
      };
    }
    if (window.CacheStorage) for (const method of ['open', 'match', 'delete', 'keys', 'has']) {
      const original = CacheStorage.prototype[method];
      CacheStorage.prototype[method] = function (...args) {
        window.__powerDemoStorageCalls.push(`caches.${method}`);
        return original.apply(this, args);
      };
    }
  });
  const page = await context.newPage();
  page.setDefaultTimeout(7000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => requests.push({ method: request.method(), path: new URL(request.url()).pathname }));
  await page.goto(`${base}/power-demo.html`);
  const state = page.locator('#power-demo');
  const button = page.locator('#power-button');
  const screen = page.locator('#power-screen');
  const phase = expected => page.waitForFunction(expected => document.querySelector('#power-demo')?.dataset.phase === expected, expected);
  const count = expected => page.waitForFunction(expected => document.querySelector('#power-countdown')?.textContent.trim() === String(expected), expected);
  const readPhase = () => state.getAttribute('data-phase');
  const screenshot = name => page.screenshot({ path: join(evidence, `power-browser-${name}.png`), fullPage: true });
  async function press() {
    await button.scrollIntoViewIfNeeded();
    const box = await button.boundingBox();
    assert.ok(box, 'PWR is visible');
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
  }
  async function release() { await page.mouse.up(); }
  async function tap() { await press(); await page.waitForTimeout(110); await release(); }
  async function reset() { await page.locator('#reset-demo').click(); await phase('ready'); }
  async function fullHold() { await press(); await phase('holding'); await phase('draining'); }

  await phase('ready');
  assert.match(await page.locator('body').innerText(), /simulat/i, 'Demo must visibly identify itself as a simulation');
  await page.locator('#boot-held').click();
  await phase('await-initial-release');
  await page.waitForTimeout(3300);
  assert.equal(await readPhase(), 'await-initial-release', 'Starting while PWR is held never starts shutdown');
  await page.locator('#release-boot').click(); await phase('ready');
  await tap(); await phase('ready');
  await press(); await count(3); await count(2); await release(); await phase('ready');
  assert.match(await page.locator('#power-status').innerText(), /cancel|ready|on/i);
  checks.push('Boot-held guard, short tap and early release keep power on.');

  // Do not advance browser clocks or inject the phase: this takes a real 3s hold.
  const holdStarted = Date.now();
  await press(); await count(3); await count(2);
  assert.match(await screen.innerText(), /hold/i, 'Power feedback uses the existing device screen renderer');
  assert.equal(await page.locator('#power-countdown').isVisible(), true, 'The countdown is visibly overlaid on the round display');
  await screenshot('countdown');
  await count(1); await phase('draining');
  assert.ok(Date.now() - holdStarted >= 3000, 'Shutdown cannot precede a real three-second hold');
  await phase('saving'); await phase('await-release');
  assert.match(await screen.innerText(), /release/i);
  await page.waitForTimeout(150);
  assert.equal(await readPhase(), 'await-release', 'Completed save still waits for held PWR to release');
  await release(); await phase('off');
  await press(); await phase('await-initial-release'); await release(); await phase('ready');
  checks.push('Actual 3→2→1 countdown, drain/save, release-before-cut, battery off and fresh-press restart.');

  await page.locator('#save-failure').check();
  await fullHold(); await phase('failed');
  assert.equal(await state.getAttribute('data-latch'), 'held', 'Save failure keeps the simulated latch asserted');
  assert.match(await screen.innerText(), /save|fail/i);
  assert.match(await page.locator('#power-status').innerText(), /fail|blocked/i);
  await screenshot('failure');
  await release(); await page.waitForTimeout(150);
  assert.equal(await readPhase(), 'failed', 'A failed save cannot cut power on release');
  await tap(); await phase('ready');
  await page.locator('#save-failure').uncheck();
  checks.push('Save failure visibly blocks cutoff; release preserves the failure and a fresh press/release resumes.');

  await page.locator('#power-source').selectOption('usb');
  await fullHold(); await release(); await phase('standby');
  assert.match(await screen.innerText(), /standby/i);
  assert.match(await page.locator('#power-status').innerText(), /unknown|cannot infer USB/i, 'Remaining power is not misrepresented as measured VBUS');
  assert.equal(await state.getAttribute('data-latch'), 'released');
  await screenshot('standby');
  await tap(); await phase('ready');
  checks.push('Simulated continued USB power enters explicitly unknown-source standby and a fresh tap resumes.');

  await press(); await phase('holding');
  // Headless Chromium does not deliver native tab/window focus changes here.
  // Deliver only the blur event; never inject controller state or elapsed time.
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  await phase('ready'); await release();
  checks.push('Explicit browser blur-event simulation cancels an unfinished real pointer hold.');

  await reset();
  await page.setViewportSize({ width: 390, height: 844 });
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'Mobile layout must not overflow horizontally');
  // Reach PWR with real Tab navigation, then hold/release Space. No DOM clicks.
  for (let attempt = 0; attempt < 20 && await button.evaluate(el => el !== document.activeElement); attempt++) await page.keyboard.press('Tab');
  assert.equal(await button.evaluate(el => el === document.activeElement), true, 'PWR is keyboard reachable');
  const scrollBefore = await page.evaluate(() => scrollY);
  await page.keyboard.down('Space'); await count(3); await count(2);
  await page.keyboard.up('Space'); await phase('ready');
  assert.equal(await page.evaluate(() => scrollY), scrollBefore, 'Holding Space on PWR must not scroll');
  checks.push('390px mobile layout fits; keyboard-only PWR hold shows countdown and release cancels without scrolling.');

  assert.deepEqual(errors, [], 'No browser errors');
  assert.deepEqual(requests.filter(r => !['GET', 'HEAD'].includes(r.method) || r.path.startsWith('/api/')), [], 'Demo must never call game APIs or make state-changing requests');
  assert.deepEqual(await page.evaluate(() => window.__powerDemoStorageCalls), [], 'Demo does not read/write browser storage');
  assert.equal(await page.evaluate(() => localStorage.length + sessionStorage.length), 0, 'Browser storage remains empty');
  assert.deepEqual(diskSnapshot(), initialDisk, 'No service data changes during demo');
  checks.push('No game/API requests, browser storage access, data changes or JavaScript errors.');
} catch (error) {
  failure = error;
} finally {
  if (browser) await browser.close();
  if (app) await new Promise(resolve => app.server.close(resolve));
  rmSync(dataDir, { recursive: true, force: true });
  const result = { result: failure ? 'FAIL' : 'PASS', scope: 'Browser simulation only; no physical LCD/power claim', checks,
    screenshots: ['countdown', 'failure', 'standby'].map(name => `power-browser-${name}.png`),
    ...(failure ? { error: failure.stack } : {}) };
  writeFileSync(join(evidence, 'power-browser.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result, null, 2));
}
if (failure) throw failure;

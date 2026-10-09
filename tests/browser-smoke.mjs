// Optional visual/e2e check. Uses an existing Playwright installation, not a runtime dependency.
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { openPlaytestTools, hatchFirstEgg } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-browser-'));
const app = await startServer({ dataDir, port: 0 });
const port = app.server.address().port;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1365, height: 1180 }, reducedMotion: 'reduce' });
const page = await context.newPage();
const errors = [];
page.on('pageerror', error => errors.push(error.message));
let current;
async function clickAndSave(selector, displayedRevision) {
  const response = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 200);
  await page.locator(selector).click();
  current = await (await response).json();
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), displayedRevision ?? current.revision);
  return current;
}
try {
  await page.goto(`http://127.0.0.1:${port}/`);
  await openPlaytestTools(page);
  await page.locator('#start-pairing').click();
  const claimResponse = page.waitForResponse(r => r.url().endsWith('/api/pairing/claim') && r.status() === 201);
  await page.locator('#claim-device').click();
  // Do not log the pairing response: it contains a development bearer token.
  const identity = await (await claimResponse).json();
  await page.locator('#setup-paired').waitFor({ state: 'visible' });
  await hatchFirstEgg(page);
  await clickAndSave('[data-event="feed"]');
  await clickAndSave('[data-event="play"]');
  await clickAndSave('[data-event="walk"]');
  assert.equal(current.state.phase, 'encounter');
  await clickAndSave('#swipe-card');
  while (current.state.phase === 'encounter' && current.state.wildHp > current.state.wildMaxHp / 2) {
    await clickAndSave('[data-event="attack"]');
  }
  for (let attempt = 0; attempt < 3 && current.state.phase === 'encounter'; attempt++) {
    await clickAndSave('[data-event="capture"]');
  }
  assert.equal(current.state.phase, 'home');
  assert.equal(current.state.captures, 1);

  // The server commits, but the browser loses the reply. The saved batch must survive reload.
  let lostAcknowledgement;
  await page.route('**/api/save-sync', async route => {
    const response = await route.fetch();
    lostAcknowledgement = await response.json();
    await route.abort('failed');
  });
  await page.locator('[data-event="rest"]').click();
  await page.locator('#retry-sync').waitFor({ state: 'visible' });
  await page.unroute('**/api/save-sync');
  // A later authoritative batch can exist when an old receipt is retried.
  const later = await fetch(`http://127.0.0.1:${port}/api/save-sync`, {
    method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${identity.token}` },
    body: JSON.stringify({ rulesVersion: 3, baseRevision: lostAcknowledgement.revision, batchId: crypto.randomUUID(), events: [{ type: 'feed', value: 0 }] }),
  });
  assert.equal(later.status, 200);
  const laterSave = await later.json();
  await page.reload();
  await openPlaytestTools(page);
  await page.locator('#retry-sync').waitFor({ state: 'visible' });
  const retried = await clickAndSave('#retry-sync', laterSave.revision);
  assert.equal(retried.revision, lostAcknowledgement.revision);
  assert.equal(retried.state.sequence, lostAcknowledgement.state.sequence);
  assert.equal(retried.state.captures, 1);
  await page.locator('#retry-sync').waitFor({ state: 'hidden' });
  await page.waitForTimeout(120); // Allow the bounded 12.5fps canvas draw to catch up.

  mkdirSync('docs/evidence', { recursive: true });
  await page.screenshot({ path: 'docs/evidence/simulator-desktop.png', fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: 'docs/evidence/simulator-mobile.png', fullPage: true });
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), 'mobile layout should not scroll horizontally');
  const second = await page.context().newPage();
  await second.goto(`http://127.0.0.1:${port}/`);
  await openPlaytestTools(second);
  await second.locator('#notice').waitFor({ state: 'visible' });
  assert.match(await second.locator('#notice-text').textContent(), /another.*tab/i);
  assert.equal(await second.locator('[data-event="feed"]').isDisabled(), true);
  await page.close();
  await second.locator('#reconnect').click();
  await second.waitForFunction(() => !document.querySelector('[data-event="feed"]').disabled);
  await second.evaluate(() => localStorage.setItem('digivice.dev.pending.v1', '{broken'));
  await second.reload();
  await openPlaytestTools(second);
  await second.locator('#notice').waitFor({ state: 'visible' });
  assert.match(await second.locator('#notice-text').textContent(), /unreadable|recover/i);
  assert.equal(await second.locator('[data-event="feed"]').isDisabled(), true);
  assert.equal(await second.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), '{broken');
  assert.deepEqual(errors, []);
  console.log(JSON.stringify({ browser: 'Chromium', result: 'pass', captures: retried.state.captures, lostAcknowledgementRetry: 'same revision and sequence', oldReceipt: 'newer display preserved', writerTabs: 'exclusive with takeover', corruptPending: 'preserved and play blocked', screenshots: 2 }));
} finally {
  await browser.close();
  await new Promise(resolve => app.server.close(resolve));
  rmSync(dataDir, { recursive: true, force: true });
}

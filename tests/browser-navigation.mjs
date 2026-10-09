// Round-device UI integration against the real local service/core. Temporary
// identities and original synthetic artwork only; no physical hardware or cloud.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';
import { legacyFixture } from './legacy-fixture.ts';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-navigation-browser-'));
const { store, identities } = legacyFixture();
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(store));
const app = await startServer({ dataDir, port: 0 });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1280, height: 1000 }, hasTouch: true, reducedMotion: 'reduce' });
const page = await context.newPage();
await page.addInitScript(() => {
  const NativeAudio = window.AudioContext || window.webkitAudioContext;
  window.__navigationAudio = [];
  if (NativeAudio) window.AudioContext = class extends NativeAudio {
    constructor(...args) { super(...args); window.__navigationAudio.push(this); }
  };
});
const errors = [], sent = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) sent.push(request.postDataJSON()); });
const screen = page.locator('#device-ui');
let credential, current;
const control = action => screen.locator(`[data-device-action="${action}"]`);
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
async function reveal(action) {
  await control(action).waitFor({ state: 'attached' });
  // Expanded round menus are carousels: reveal a choice through actual input
  // before touching/focusing it, rather than invoking a hidden DOM control.
  for (let step = 0; step < 24; step++) {
    if (await control(action).isVisible()) return;
    await page.locator('#screen-surface').focus(); await page.keyboard.press('ArrowDown');
  }
  throw new Error(`Navigation could not reveal ${action} on ${await screen.getAttribute('data-screen')}`);
}
async function touch(action) { await reveal(action); await control(action).tap(); }
async function latest() {
  const response = await fetch(`${base}/api/save`, { headers: { Authorization: `Bearer ${credential.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function action(name, repeated = false) {
  await reveal(name);
  const response = page.waitForResponse(result => result.url().endsWith('/api/save-sync') && result.status() === 200);
  if (repeated) await control(name).evaluate(button => { button.click(); button.click(); });
  else await touch(name);
  current = await (await response).json();
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), current.revision);
  return current;
}
async function closeTools() { await page.locator('#playtest-tools').evaluate(details => { details.open = false; }); }
async function backToHome() {
  for (let count = 0; count < 12 && await screen.getAttribute('data-screen') !== 'home'; count++) await page.locator('#device-back').tap();
  await onScreen('home');
}
async function chooseFromMenu(name) { await backToHome(); await touch('menu'); await touch(name); await onScreen(name); }
async function circleControlsFit() {
  const failures = await screen.evaluate(root => {
    const surface = root.closest('.device-screen').getBoundingClientRect();
    const cx = surface.left + surface.width / 2, cy = surface.top + surface.height / 2;
    const radius = Math.min(surface.width, surface.height) / 2;
    const buttons = [...root.querySelectorAll('button')].filter(button => button.getClientRects().length && getComputedStyle(button).visibility !== 'hidden');
    const errors = [];
    for (const button of buttons) {
      const box = button.getBoundingClientRect();
      for (const [x, y] of [[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]]) {
        if (Math.hypot(x - cx, y - cy) > radius + 2) { errors.push(`clipped: ${button.textContent.trim()}`); break; }
      }
    }
    for (let i = 0; i < buttons.length; i++) for (let j = i + 1; j < buttons.length; j++) {
      const a = buttons[i].getBoundingClientRect(), b = buttons[j].getBoundingClientRect();
      if (Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 1) errors.push(`overlap: ${buttons[i].textContent.trim()} / ${buttons[j].textContent.trim()}`);
    }
    return errors;
  });
  assert.deepEqual(failures, []);
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
}

function originalAppearance() {
  const frame = Buffer.alloc(32 * 32 / 2, 0x11).toString('base64');
  const names = ['idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate'];
  const animations = Object.fromEntries(names.map(name => [name, { frameMs: 200, frames: [frame] }]));
  const pack = { formatVersion: 1, packId: 'personal-navigation-test', version: 1, license: 'LicenseRef-Personal-Use-Restrictions', paletteEncoding: 'rgb565',
    sprites: { mote: { name: 'Original navigation square', family: 'synthetic', stage: 1, width: 32, height: 32, palette: [0, 2016, ...Array(14).fill(0)], transparentIndex: 0, animations } }, effects: {}, icons: {} };
  const packText = JSON.stringify(pack);
  const provenance = { formatVersion: 1, packId: pack.packId, version: 1, packBytes: Buffer.byteLength(packText), packSha256: createHash('sha256').update(packText).digest('hex'), decodedBytes: 32 * 32 * 4 * names.length,
    provenance: { creator: 'Project test', sourceUrl: 'https://example.com/original-test', game: 'Original synthetic fixture', rights: 'Original artwork', reuseScope: 'Local test only' },
    coverage: { mote: Object.fromEntries(names.map(name => [name, { kind: name === 'idle' ? 'genuine' : 'reused-fallback', sourceAnimation: 'idle', originalFrameCount: name === 'idle' ? 1 : 0, outputFrameCount: 1, sourceRects: name === 'idle' ? [[0, 0, 32, 32]] : [], transforms: [] }])) },
    sources: [{ path: 'original-test.png', sha256: 'a'.repeat(64), bytes: 100, width: 32, height: 32 }], conversion: {} };
  return [{ name: 'pack.json', mimeType: 'application/json', buffer: Buffer.from(packText) }, { name: 'provenance.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(provenance)) }];
}

try {
  await page.goto(`${base}/?controls=buttons`);
  await onScreen('home');
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  await touch('connection'); await onScreen('connection');
  await touch('start-pairing');
  const paired = page.waitForResponse(response => response.url().endsWith('/api/pairing/claim') && response.status() === 201);
  await touch('claim-device'); const newlyPaired = await (await paired).json();
  await onScreen('starter-select');
  assert.equal(newlyPaired.state.phase, 'egg'); assert.equal(newlyPaired.revision, 0);
  assert.equal(sent.length, 0);
  // Pairing now correctly creates an Egg. The remaining appearance/navigation
  // regression reopens a genuine old zero-event Mote identity; its historical
  // store fixture is migrated by the real service, never relabelled as new.
  credential = identities[0];
  await page.evaluate(identity => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)), credential);
  await page.reload(); await onScreen('home');
  await page.waitForFunction(() => document.querySelector('#creature-name').textContent === 'Mote');
  current = await latest(); assert.equal(current.revision, 0);
  assert.deepEqual(current.state.onboarding, { completed: true, starterId: null });

  // Native DOM focus and the highlighted choice must agree. Pressing Enter on
  // Back may not accidentally execute the highlighted Feed command.
  await touch('menu'); await onScreen('menu');
  await touch('care'); await onScreen('care');
  await page.locator('#device-back').focus(); await page.keyboard.press('Enter'); await onScreen('menu');
  assert.equal(await control('care').getAttribute('data-selected'), 'true');
  await page.locator('#device-confirm-button').tap(); await onScreen('care');
  await holdDeviceBack(page); await onScreen('menu');
  await page.locator('#screen-surface').focus(); await page.keyboard.press('ArrowDown');
  assert.equal(await control('companions').getAttribute('data-selected'), 'true');
  await page.keyboard.press('Enter'); await onScreen('companions');
  await touch('member-1'); await onScreen('companion');
  await page.keyboard.press('Escape'); await onScreen('companions');
  await page.keyboard.press('Escape'); await onScreen('menu');
  assert.equal(await control('companions').getAttribute('data-selected'), 'true');
  await page.keyboard.press('Backspace'); await onScreen('home');
  await page.keyboard.press('Enter'); await onScreen('menu');
  await reveal('settings'); await control('settings').focus(); await page.keyboard.press('Enter'); await onScreen('settings');
  assert.equal(sent.length, 0, 'navigation, Back and native focus may not send game commands');

  // Sound settings control the real browser AudioContext with explicit consent.
  assert.equal(await page.evaluate(() => window.__navigationAudio.length), 0);
  await touch('sound'); await onScreen('sound');
  await touch('toggle-sound');
  await page.waitForFunction(() => window.__navigationAudio[0]?.state === 'running');
  const volume = await page.evaluate(() => JSON.parse(localStorage.getItem('digivice.audio.v1')).volume);
  await touch('volume-up');
  assert.ok(await page.evaluate(value => JSON.parse(localStorage.getItem('digivice.audio.v1')).volume > value, volume));
  await touch('toggle-music');
  assert.equal(await page.evaluate(() => JSON.parse(localStorage.getItem('digivice.audio.v1')).musicEnabled), true);
  await touch('toggle-music'); await touch('toggle-sound');
  await page.waitForFunction(() => window.__navigationAudio[0]?.state === 'suspended');
  await page.locator('#device-back').tap(); await onScreen('settings');

  // Import uses the explicit browser tools, then round-screen preferences persist.
  await touch('artwork'); await onScreen('artwork'); await touch('open-personal');
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), true);
  await page.locator('#personal-files').setInputFiles(originalAppearance());
  await page.locator('#personal-import').click();
  await page.waitForFunction(() => document.querySelector('#creature-name').textContent === 'Original navigation square');
  await closeTools();
  await touch('toggle-personal');
  await page.waitForFunction(() => document.querySelector('#creature-name').textContent === 'Mote');
  await page.reload(); await onScreen('home');
  await page.waitForFunction(() => document.querySelector('#personal-use').disabled === false);
  assert.equal(await page.locator('#personal-use').isChecked(), false);
  assert.equal(await page.evaluate(() => window.__navigationAudio.length), 0, 'reload does not autoplay remembered audio settings');
  await chooseFromMenu('settings'); await touch('artwork'); await touch('toggle-personal');
  await page.waitForFunction(() => document.querySelector('#creature-name').textContent === 'Original navigation square');
  await touch('toggle-personal');
  await page.waitForFunction(() => document.querySelector('#creature-name').textContent === 'Mote');
  assert.equal(sent.length, 0);

  // Check every interactive screen on a small viewport, including circular
  // clipping and pairwise button overlap, not just document scroll width.
  await page.setViewportSize({ width: 390, height: 844 });
  await backToHome(); await circleControlsFit();
  await touch('menu'); await circleControlsFit();
  for (const name of ['care', 'companions', 'explore', 'cards', 'settings']) {
    await touch(name); await onScreen(name); await circleControlsFit();
    if (name === 'settings') {
      for (const detail of ['sound', 'artwork', 'connection', 'saves']) {
        await touch(detail); await onScreen(detail); await circleControlsFit();
        if (detail === 'saves') {
          await touch('save-confirm'); await onScreen('save-confirm'); await circleControlsFit();
          await page.locator('#device-back').tap(); await onScreen('saves');
          assert.equal(await page.locator('#device-id').textContent(), credential.deviceId);
          assert.equal(sent.length, 0);
        }
        await page.locator('#device-back').tap(); await onScreen('settings');
      }
    }
    await page.locator('#device-back').tap(); await onScreen('menu');
  }
  await touch('care');
  const beforeFeed = sent.length;
  await action('feed', true); await onScreen('care');
  assert.equal(sent.length, beforeFeed + 1); assert.equal(current.state.sequence, 1);
  await chooseFromMenu('explore'); await action('walk'); await onScreen('battle');
  await circleControlsFit();
  await touch('cards'); await action('card-spark'); await onScreen('battle');
  await action('attack'); await onScreen('battle');
  while (current.state.wildHp > Math.floor(current.state.wildMaxHp / 2)) { await action('attack'); await onScreen('battle'); }

  const beforeCapture = await latest(), beforeCapturePosts = sent.length;
  await touch('capture'); await onScreen('capture'); await circleControlsFit();
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), null);
  await holdDeviceBack(page); await onScreen('battle');
  await page.waitForTimeout(1350);
  assert.equal(sent.length, beforeCapturePosts); assert.deepEqual(await latest(), beforeCapture);

  // Once the request is sent, Back cannot undo it or erase its retry identity.
  let committed;
  await page.route('**/api/save-sync', async route => { const response = await route.fetch(); committed = await response.json(); await route.abort('failed'); });
  await touch('capture'); await onScreen('saving');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="retry"]')?.disabled);
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  assert.equal(JSON.parse(pending).events[0].type, 'capture');
  await holdDeviceBack(page);
  assert.equal(await screen.getAttribute('data-screen'), 'saving', 'held Back cannot erase a sent capture');
  await page.locator('#screen-surface').focus(); await page.keyboard.press('Escape');
  assert.equal(await screen.getAttribute('data-screen'), 'saving');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  await circleControlsFit();
  await page.unroute('**/api/save-sync'); await page.reload(); await onScreen('saving');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  await action('retry'); await onScreen('home');
  assert.equal(current.revision, committed.revision); assert.equal(current.state.collection.length, 2);
  assert.equal(sent.at(-1).batchId, JSON.parse(pending).batchId);

  const captured = current.state.collection.find(member => member.id !== 1);
  await touch('menu'); await touch('companions'); await touch(`member-${captured.id}`); await onScreen('companion');
  await circleControlsFit(); await action('select-companion');
  assert.equal(current.state.activeCreatureId, captured.id);
  const selectedRevision = current.revision;
  await page.reload(); await onScreen('home');
  await page.waitForFunction(name => document.querySelector('#creature-name').textContent === name, captured.name);
  assert.equal(await page.locator('#revision').textContent(), String(selectedRevision));
  assert.equal((await latest()).state.activeCreatureId, captured.id);
  assert.equal(await page.locator('#personal-use').isChecked(), false);
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  assert.deepEqual(errors, []);
  console.log('PASS: round-only setup; touch, keyboard and two-button navigation; native focus/Back without game POST; real audio controls; persistent art preference; small circular layout; exact-once care; capture Back before send; durable retry after send/reload; real companion selection survives reload.');
} finally {
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

// Real UI/core integration: presentation must never become a second game engine.
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { openPlaytestTools } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-playtest-browser-'));
const app = await startServer({ dataDir, port: 0 });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1365, height: 1100 } });
const page = await context.newPage();
// Observe real browser audio contexts/oscillators. Native creation, scheduling,
// suspend/resume and playback remain intact; only one resume can be delayed.
await page.addInitScript(() => {
  const NativeAudio = window.AudioContext || window.webkitAudioContext;
  window.__audioProbe = { contexts: [], notes: 0, hold: false, waiters: [] };
  if (!NativeAudio) return;
  window.AudioContext = class ObservedAudioContext extends NativeAudio {
    constructor(...args) { super(...args); window.__audioProbe.contexts.push(this); }
    createOscillator() {
      const oscillator = super.createOscillator();
      const start = oscillator.start.bind(oscillator);
      oscillator.start = (...args) => { window.__audioProbe.notes += 1; return start(...args); };
      return oscillator;
    }
    resume() {
      if (!window.__audioProbe.hold) return super.resume();
      return new Promise((resolve, reject) => window.__audioProbe.waiters.push(() => super.resume().then(resolve, reject)));
    }
  };
});
const errors = [];
const sent = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (request.url().endsWith('/api/save-sync') && request.method() === 'POST') sent.push(request.postDataJSON()); });
let current;
let firstIdentity;

async function ready() {
  await page.waitForFunction(() => !document.querySelector('[data-event="feed"]').disabled || !document.querySelector('[data-event="attack"]').disabled);
}
async function action(selector) {
  const response = page.waitForResponse(result => result.url().endsWith('/api/save-sync') && result.status() === 200);
  await page.locator(selector).click(); current = await (await response).json();
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), current.revision);
  await ready(); return current;
}
async function latest(credential = firstIdentity) {
  const response = await fetch(`${base}/api/save`, { headers: { Authorization: `Bearer ${credential.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function seed(credential, events) {
  let saved = { revision: 0 };
  for (let index = 0; index < events.length; index += 100) {
    const response = await fetch(`${base}/api/save-sync`, {
      method: 'POST', headers: { Authorization: `Bearer ${credential.token}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ rulesVersion: 3, baseRevision: saved.revision, batchId: crypto.randomUUID(), events: events.slice(index, index + 100) }),
    });
    assert.equal(response.status, 200); saved = await response.json();
  }
  return saved;
}
// Derive short valid histories with the native current rules. The browser must
// show the resulting branch, not an obsolete rules-2 damage/HP golden value.
function nativeRetreatFixture() {
  const replay = events => {
    const result = spawnSync(resolve('build/digivice-core'), ['--replay', '12345'], {
      input: events.map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', timeout: 2000, maxBuffer: 32768,
    });
    assert.equal(result.status, 0, result.stderr); return JSON.parse(result.stdout);
  };
  const events = []; let state = replay(events);
  for (let step = 0; step < 96; step++) {
    const event = state.phase === 'home' ? { type: 'walk', value: 100 } : { type: 'attack', value: 0 };
    const next = replay([...events, event]);
    if (/gentle retreat/i.test(next.message)) return { events, before: state, expected: next, event };
    events.push(event); state = next;
  }
  throw new Error('Native rules did not reach the bounded retreat fixture.');
}

async function verifyMigratedCaptureMiss() {
  // Fresh rules-3 seed 12345 succeeds on all seven available capture slots.
  // Exercise a miss through a genuine rules-2 history migrated by the service;
  // preserve its RNG continuation instead of patching a snapshot or game state.
  const events = [['walk', 100], ['card', 1], ['attack', 0], ['capture', 0], ['walk', 100], ['attack', 0], ['attack', 0]].map(([type, value]) => ({ type, value }));
  const native = (args, input) => JSON.parse(execFileSync(resolve('build/digivice-core'), args, { input, encoding: 'utf8', timeout: 2000, maxBuffer: 32768 }));
  const migrated = native(['--migrate-v2', '12345'], events.map(event => `${event.type} ${event.value}\n`).join(''));
  const expected = native(['--replay-snapshot', migrated.snapshotBase64], 'capture 0\n');
  assert.equal(expected.captureAttempts, 1); assert.equal(expected.captures, migrated.state.captures);
  const directory = mkdtempSync(join(tmpdir(), 'digivice-migrated-miss-browser-'));
  const credential = { deviceId: `dv_${randomBytes(12).toString('hex')}`, token: randomBytes(32).toString('base64url') };
  const hash = value => createHash('sha256').update(value).digest('hex');
  const store = { formatVersion: 2, gameSchemaVersion: 3, rulesVersion: 2, devices: [{ ...credential, tokenHash: hash(credential.token), seed: 12345, revision: 1, legacy: null,
    events, receipts: [{ batchId: randomUUID(), bodyHash: hash(JSON.stringify({ rulesVersion: 2, baseRevision: 0, events })), revision: 1, eventEnd: events.length }] }] };
  delete store.devices[0].token; // Only its hash belongs in the service store.
  writeFileSync(join(directory, 'store.json'), JSON.stringify(store), { mode: 0o600 });
  let migratedApp, isolated;
  try {
    migratedApp = await startServer({ dataDir: directory, port: 0 });
    isolated = await browser.newContext({ reducedMotion: 'reduce' });
    await isolated.addInitScript(identity => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)), credential);
    const migratedPage = await isolated.newPage(); migratedPage.on('pageerror', error => errors.push(error.message));
    await migratedPage.goto(`http://127.0.0.1:${migratedApp.server.address().port}`); await openPlaytestTools(migratedPage);
    await migratedPage.waitForFunction(() => !document.querySelector('[data-event="capture"]').disabled);
    const response = migratedPage.waitForResponse(result => result.url().endsWith('/api/save-sync') && result.status() === 200);
    await migratedPage.locator('[data-event="capture"]').click();
    const saved = await (await response).json(); assert.deepEqual(saved.state, expected);
    await migratedPage.waitForFunction(() => !document.querySelector('[data-event="attack"]').disabled);
    assert.ok((await migratedPage.locator('#playtest-title').textContent()).includes(`${migrated.state.wildName} slipped free`));
    assert.ok((await migratedPage.locator('#playtest-detail').textContent()).includes(`${migrated.state.hp - expected.hp} companion health lost`));
  } finally {
    await isolated?.close();
    if (migratedApp) { await new Promise(resolve => migratedApp.server.close(resolve)); migratedApp.close(); }
    rmSync(directory, { recursive: true, force: true });
  }
}
async function freshPlaytest() {
  await page.locator('#new-playtest').click();
  const response = page.waitForResponse(result => result.url().endsWith('/api/pairing/claim') && result.status() === 201);
  await page.locator('#confirm-new-playtest').click();
  const credential = await (await response).json(); await ready(); return credential;
}

try {
  await page.goto(base);
  await openPlaytestTools(page);
  await page.locator('#start-pairing').click();
  const claimed = page.waitForResponse(result => result.url().endsWith('/api/pairing/claim') && result.status() === 201);
  await page.locator('#claim-device').click(); firstIdentity = await (await claimed).json(); current = firstIdentity;
  await ready();
  await page.waitForFunction(() => document.querySelector('#asset-pack-detail').textContent.includes('Saved locally'));
  assert.equal(await page.locator('#sound-toggle').textContent(), 'Enable sound');
  assert.equal(await page.evaluate(() => window.__audioProbe.contexts.length), 0, 'no audio context before a user gesture');
  await page.locator('#sound-toggle').click();
  await page.waitForFunction(() => document.querySelector('#sound-toggle').textContent === 'Sound on');
  await page.waitForFunction(() => window.__audioProbe.contexts[0]?.state === 'running');
  await page.locator('.sound-options summary').click();
  await page.locator('#sound-music').check();
  await page.waitForFunction(() => window.__audioProbe.notes > 0);
  await page.locator('#sound-toggle').click();
  await page.waitForFunction(() => window.__audioProbe.contexts[0]?.state === 'suspended');
  assert.equal(await page.locator('#sound-toggle').textContent(), 'Sound off');
  await page.locator('#sound-toggle').click();
  await page.waitForFunction(() => window.__audioProbe.contexts[0]?.state === 'running');
  await page.evaluate(() => { Object.defineProperty(document, 'hidden', { configurable: true, value: true }); document.dispatchEvent(new Event('visibilitychange')); });
  await page.waitForFunction(() => window.__audioProbe.contexts[0]?.state === 'suspended');
  await page.evaluate(() => { delete document.hidden; document.dispatchEvent(new Event('visibilitychange')); });
  await page.waitForFunction(() => window.__audioProbe.contexts[0]?.state === 'running');

  // A later uncheck must win over a still-pending earlier music unlock. Resume
  // is delayed, not replaced; the real native AudioContext handles completion.
  await page.locator('#sound-music').uncheck();
  await page.evaluate(async () => { await window.__audioProbe.contexts[0].suspend(); window.__audioProbe.hold = true; });
  const quietNotes = await page.evaluate(() => window.__audioProbe.notes);
  await page.locator('#sound-music').check();
  await page.waitForFunction(() => window.__audioProbe.waiters.length > 0);
  await page.locator('#sound-music').uncheck();
  await page.evaluate(() => { window.__audioProbe.hold = false; window.__audioProbe.waiters.splice(0).forEach(release => release()); });
  await page.waitForFunction(() => window.__audioProbe.contexts[0].state === 'running');
  await page.waitForTimeout(220);
  assert.equal(await page.locator('#sound-music').isChecked(), false);
  assert.equal(await page.evaluate(() => JSON.parse(localStorage.getItem('digivice.audio.v1')).musicEnabled), false);
  assert.equal(await page.evaluate(() => window.__audioProbe.notes), quietNotes);

  // Two synchronous clicks must still mean one care command.
  const feedResponse = page.waitForResponse(result => result.url().endsWith('/api/save-sync') && result.status() === 200);
  const beforeFeed = sent.length;
  await page.locator('[data-event="feed"]').evaluate(button => { button.click(); button.click(); });
  current = await (await feedResponse).json(); await ready();
  assert.equal(sent.length, beforeFeed + 1);
  assert.equal(current.state.sequence, 1);
  await action('[data-event="walk"]');
  assert.equal(current.state.steps, 100);
  assert.equal(current.state.wildName, 'Flicker');
  assert.match(await page.locator('#journal').textContent(), /Flicker appeared/);
  await action('#swipe-card');
  assert.match(await page.locator('#battle-modifiers').textContent(), /Spark ready/);
  const beforeAttack = structuredClone(current.state);
  await action('[data-event="attack"]');
  assert.match(await page.locator('#journal').textContent(), new RegExp(`${beforeAttack.wildHp - current.state.wildHp} wild health lost`));
  assert.match(await page.locator('#journal').textContent(), new RegExp(`${current.state.hp} companion health remaining`));
  while (current.state.wildHp > Math.floor(current.state.wildMaxHp / 2)) await action('[data-event="attack"]');

  // Cancel is real: no batch is written or sent, no attempt or revision changes.
  const beforeCancel = await latest(); const beforeCancelRequests = sent.length;
  await page.locator('[data-event="capture"]').click();
  await page.locator('#cancel-capture').waitFor({ state: 'visible' });
  assert.equal(await page.locator('[data-event="attack"]').isDisabled(), true);
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), null);
  mkdirSync('docs/evidence', { recursive: true });
  await page.screenshot({ path: 'docs/evidence/playtest-capture-windup.png', fullPage: true });
  await page.locator('#cancel-capture').click();
  await page.waitForTimeout(1350);
  assert.equal(sent.length, beforeCancelRequests);
  assert.deepEqual(await latest(), beforeCancel);

  // Hide the page during an unsent wind-up. It must cancel, not fire a delayed
  // command when returning to the browser. Only this browser lifecycle flag is mocked.
  await page.locator('[data-event="capture"]').click();
  await page.evaluate(() => { Object.defineProperty(document, 'hidden', { configurable: true, value: true }); document.dispatchEvent(new Event('visibilitychange')); });
  await page.waitForTimeout(1350);
  await page.evaluate(() => { delete document.hidden; document.dispatchEvent(new Event('visibilitychange')); });
  await ready();
  assert.equal(sent.length, beforeCancelRequests);
  assert.deepEqual(await latest(), beforeCancel);

  // The next capture commits, but loses its reply. After send there is no cancel
  // or rollback; the exact durable request survives and resolves idempotently.
  let committedCapture;
  await page.route('**/api/save-sync', async route => {
    const result = await route.fetch(); committedCapture = await result.json(); await route.abort('failed');
  });
  await page.locator('[data-event="capture"]').evaluate(button => { button.click(); button.click(); button.click(); });
  await page.locator('#retry-sync').waitFor({ state: 'visible' });
  assert.equal(sent.length, beforeCancelRequests + 1);
  assert.equal(await page.locator('#cancel-capture').isVisible(), false);
  assert.equal(await page.locator('#new-playtest').isDisabled(), true);
  assert.equal(await page.locator('#load-playtest').isDisabled(), true);
  const pending = await page.evaluate(() => JSON.parse(localStorage.getItem('digivice.dev.pending.v1')));
  assert.equal(pending.events[0].type, 'capture');
  await page.unroute('**/api/save-sync');
  await action('#retry-sync');
  assert.equal(sent.at(-1).batchId, pending.batchId);
  assert.equal(current.revision, committedCapture.revision);
  assert.equal(current.state.sequence, committedCapture.state.sequence);
  assert.equal(current.state.captures, 1);
  assert.equal(current.state.collection.length, 2);
  assert.match(await page.locator('#journal').textContent(), /Flicker joined your collection/);

  // A battle won by attacks is distinct from a capture and adds no member.
  await action('[data-event="walk"]');
  while (current.state.phase === 'encounter') await action('[data-event="attack"]');
  assert.equal(current.state.collection.length, 2);
  assert.match(await page.locator('#playtest-title').textContent(), /battle won/);
  const originalSave = await latest();

  // New playtest requires explicit confirmation and preserves the original save.
  await page.locator('#new-playtest').click();
  await page.locator('#new-playtest-confirm').waitFor({ state: 'visible' });
  await page.locator('#cancel-new-playtest').click();
  assert.deepEqual(await latest(), originalSave);
  await page.locator('#new-playtest').click();
  const newClaimed = page.waitForResponse(result => result.url().endsWith('/api/pairing/claim') && result.status() === 201);
  await page.locator('#confirm-new-playtest').click();
  const secondIdentity = await (await newClaimed).json();
  await ready();
  assert.notEqual(secondIdentity.deviceId, firstIdentity.deviceId);
  assert.equal(await page.locator('#collection-count').textContent(), '1 / 8');
  assert.equal(await page.locator('#saved-playtest-select option').count(), 2);
  await page.locator('#saved-playtest-select').selectOption(firstIdentity.deviceId);
  await page.locator('#load-playtest').click();
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), originalSave.revision);
  await ready();
  assert.equal(await page.locator('#collection-count').textContent(), '2 / 8');
  assert.deepEqual(await latest(), originalSave);

  // Seed real event histories through the authenticated service, then exercise
  // each remaining result using one actual UI command. No state fixture injection.
  await verifyMigratedCaptureMiss();

  const evolutionIdentity = await freshPlaytest();
  current = await seed(evolutionIdentity, Array.from({ length: 7 }, () => ({ type: 'play', value: 0 })));
  await page.reload(); await openPlaytestTools(page); await ready();
  await action('[data-event="play"]');
  assert.equal(current.state.creature, 'Glint');
  assert.equal(current.state.bond, 40);
  assert.equal(await page.locator('#playtest-feedback').getAttribute('data-stage'), 'evolution');
  assert.match(await page.locator('#journal').textContent(), /Mote evolved into Glint/);

  const retreatIdentity = await freshPlaytest();
  const retreat = nativeRetreatFixture();
  current = await seed(retreatIdentity, retreat.events);
  await page.reload(); await openPlaytestTools(page); await ready();
  assert.equal(current.state.hp, retreat.before.hp);
  await action('[data-event="attack"]');
  assert.equal(current.state.phase, 'home');
  assert.deepEqual(current.state, retreat.expected);
  assert.ok(current.state.hp > 0, 'a lost encounter gently returns a living companion');
  assert.equal(await page.locator('#playtest-feedback').getAttribute('data-stage'), 'retreat');
  assert.match(await page.locator('#playtest-title').textContent(), /Safely back home/);

  // With reduced motion, the same rules and controls work without movement;
  // reloading never auto-starts remembered sound preferences.
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await page.reload(); await openPlaytestTools(page); await ready();
  assert.equal(await page.locator('#sound-toggle').textContent(), 'Enable sound');
  assert.equal(await page.locator('#saved-playtest-select option').count(), 4);
  await action('[data-event="rest"]');
  await page.screenshot({ path: 'docs/evidence/playtest-desktop.png', fullPage: true });
  await page.locator('.workspace').screenshot({ path: 'docs/evidence/playtest-screen.png' });
  await page.setViewportSize({ width: 390, height: 844 });
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  await page.screenshot({ path: 'docs/evidence/playtest-mobile.png', fullPage: true });
  assert.deepEqual(errors, []);
  const verification = {
    suite: 'browser-playtest', result: 'pass', verifiedAt: new Date().toISOString(), browser: 'Chromium', schemaVersion: 4, rulesVersion: 3,
    checks: ['double-tap lock', 'exploration', 'actual damage and retaliation', 'card modifier', 'capture cancellation without POST',
      'hidden wind-up cancellation', 'committed capture lost-response retry', 'capture success', 'capture miss', 'battle win', 'gentle retreat',
      'evolution', 'new playtest preserves old save', 'saved playtest loading', 'native AudioContext opt-in', 'oscillator background music',
      'mute suspends context', 'visibility suspends and resumes context', 'latest music intent wins delayed resume', 'no autoplay on reload',
      'reduced motion', 'mobile width'],
    screenshots: ['playtest-capture-windup.png', 'playtest-desktop.png', 'playtest-screen.png', 'playtest-mobile.png'],
    artwork: 'original-only', limitations: ['Temporary local service; no physical device tested.', 'Visibility lifecycle flag simulated; actual native AudioContext observed.'],
  };
  writeFileSync('docs/evidence/playtest-verification.json', `${JSON.stringify(verification, null, 2)}\n`);
  console.log(`PASS: ${verification.checks.join('; ')}.`);
} finally {
  await browser.close();
  await new Promise(resolve => app.server.close(resolve));
  app.close(); rmSync(dataDir, { recursive: true, force: true });
}

// Synthetic public-command histories only; actual touchscreen and two-button
// inputs exercise the UI. Private artwork is blocked before it can be fetched.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { touchDevice } from './touch-browser-tools.mjs';
import { gameReady } from './manual-auto-browser-tools.mjs';
import { holdDeviceBack } from './browser-tools.mjs';
import { orderedMembers } from '../web/party.js';

const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const sources = ['web/app.js', 'web/party.js', 'web/device-screen.js', 'web/styles.css', 'web/form-art.js', 'web/walking-state.js',
  'web/starter-onboarding.js', 'web/progression.js', 'web/roster-client.js', 'service/server.ts', 'service/roster-service.ts',
  'core/game.cpp', 'core/game.hpp', 'core/cli.cpp', 'tests/browser-party.mjs'].map(path => ({ path, sha256: digest(path) }));
const binaries = [corePath, battleCorePath].map(path => ({ path, sha256: digest(path) }));
const evidenceDir = resolve(process.env.PARTY_EVIDENCE_DIR || 'docs/evidence/party-browser');
mkdirSync(evidenceDir, { recursive: true });
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-party-browser-'));
let app = await startServer({ seedSource: () => 12345, dataDir, port: 0, corePath, battleCorePath });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
let context, page, ui, identity, failure;
const errors = [], posts = [], checks = [], geometry = [], screenshots = [], artRequests = [];
const event = (type, value = 0) => ({ type, value });
const pendingKey = 'digivice.dev.pending.v1';
const native = events => JSON.parse(execFileSync(corePath, ['--replay-onboarding', '12345'], {
  input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', timeout: 5000, maxBuffer: 65536,
}));
async function http(path = '/api/save', body) {
  const response = await fetch(base + path, { method: body ? 'POST' : 'GET', headers: {
    ...(identity ? { Authorization: `Bearer ${identity.token}` } : {}), ...(body ? { 'Content-Type': 'application/json' } : {}),
  }, ...(body ? { body: JSON.stringify(body) } : {}) });
  assert.ok(response.ok, `${path}: ${response.status} ${await response.clone().text()}`); return response.json();
}
async function seed(events) {
  let saved = await http();
  for (let offset = 0; offset < events.length; offset += 100) saved = await http('/api/save-sync', {
    rulesVersion: saved.state.rulesVersion, baseRevision: saved.revision, batchId: crypto.randomUUID(), events: events.slice(offset, offset + 100),
  });
  return saved;
}
async function open(storageState, mode = 'touch') {
  context = await browser.newContext({ viewport: { width: 1000, height: 1000 }, hasTouch: true, reducedMotion: 'reduce', ...(storageState ? { storageState } : {}) });
  if (!storageState) await context.addInitScript(value => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(value)), identity);
  page = await context.newPage(); page.setDefaultTimeout(15000); ui = touchDevice(page);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => {
    if (/\/api\/roster\/art\//.test(request.url())) artRequests.push(new URL(request.url()).pathname);
    if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) posts.push({ raw: request.postData(), body: request.postDataJSON() });
  });
  await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
  await page.goto(`${base}/?controls=${mode}`);
  await page.locator('.device-screen').evaluate(element => {
    element.style.setProperty('box-sizing', 'content-box', 'important');
    for (const dimension of ['width', 'height']) element.style.setProperty(dimension, '412px', 'important');
    element.style.setProperty('margin-inline', 'auto', 'important');
  });
}
async function shot(label) {
  geometry.push(await ui.geometry(label)); const path = join(evidenceDir, `${label}.png`);
  await page.locator('#screen-surface').screenshot({ path }); screenshots.push(path);
}
async function command(id) {
  await ui.reveal(id);
  const response = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.request().method() === 'POST' && r.status() === 200);
  await ui.tap(id); const saved = await (await response).json(); await gameReady(page); return saved;
}
async function member(id) {
  if (await ui.screen.getAttribute('data-screen') !== 'companions') await ui.menu('companions');
  for (let index = 0; index < 20; index++) {
    if (await ui.control(`member-${id}`).count()) { await ui.tap(`member-${id}`, 'companion'); return; }
    await ui.tap('companions-next');
  }
  throw new Error(`Missing member ${id}`);
}
async function reload(expected = 'home') { await page.reload(); await ui.onScreen(expected); }
async function pageIds() { return ui.screen.locator('button[data-member-id]').evaluateAll(rows => rows.map(row => Number(row.dataset.memberId))); }
async function assertParty(ids) { const saved = await http(); assert.deepEqual(saved.state.partyMemberIds, ids); return saved; }

try {
  const pair = await http('/api/pairing/start', {}); identity = await http('/api/pairing/claim', { code: pair.code });
  await seed([event('hatch', 1)]); await http('/api/world/seed', {});
  const history = [event('hatch', 1), event('world-seed', 12345)], suffix = [];
  let simulated = native(history);
  const append = (type, value = 0) => { const action = event(type, value); history.push(action); suffix.push(action); simulated = native(history); };
  append('mode', 1);
  for (let fight = 0; fight < 600 && simulated.collection.length < 60; fight++) {
    for (let rests = simulated.recoveryRestCount; rests > 0; rests--) append('rest');
    append('walk', 100); append('auto');
  }
  assert.equal(simulated.collection.length, 60); append('mode', 0);
  for (let rests = simulated.recoveryRestCount; rests > 0; rests--) append('rest');
  const full = await seed(suffix); assert.deepEqual(full.state, simulated); assert.deepEqual(full.state.partyMemberIds, []);
  await open(); await ui.onScreen('home'); await ui.menu('companions');
  const seen = [], durations = [], initialPosts = posts.length, initialArt = artRequests.length; let portraits = 0;
  for (let index = 0; index < 20; index++) {
    seen.push(...await pageIds()); portraits = Math.max(portraits, await ui.screen.locator('canvas.roster-portrait').count());
    assert.equal(await page.locator('#collection-grid canvas').count(), 0);
    const start = performance.now(); await ui.tap('companions-next'); durations.push(performance.now() - start);
  }
  assert.deepEqual(seen, orderedMembers(full.state).map(m => m.id)); assert.equal(new Set(seen).size, 60);
  assert.equal(portraits, 3); assert.equal(posts.length, initialPosts); assert.equal(artRequests.length, initialArt);
  checks.push({ name: 'All sixty box entries ordered and bounded', pages: 20, maxMainPortraits: portraits, hiddenToolPortraits: 0, newArtRequests: 0, stateJsonBytes: Buffer.byteLength(JSON.stringify(full.state)),
    pageNavigationMs: { max: Math.max(...durations), median: [...durations].sort((a, b) => a - b)[10] } });
  await shot('box-newest-first');

  for (const id of [5, 50, 2]) {
    await member(id); await ui.reveal('party-add'); if (id === 5) await shot('add-xp-companion');
    await command('party-add'); await ui.onScreen('companion'); assert.equal(await ui.screen.getAttribute('data-member-id'), String(id));
    assert.ok(Number(await ui.screen.getAttribute('data-party-slot')) > 0);
    if (id === 5) { await ui.reveal('party-remove'); await shot('selected-xp-companion'); }
    await ui.back('companions'); assert.equal(await ui.screen.locator('[data-selected=true]').getAttribute('data-member-id'), String(id));
  }
  await assertParty([5, 50, 2]); await member(59); assert.equal(await ui.control('party-add').isDisabled(), true);
  assert.match(await ui.screen.locator('.screen-detail').textContent(), /All 3 XP slots/); assert.equal(await ui.screen.getByText('XP FULL 3/3', { exact: true }).isVisible(), true); await shot('xp-party-full');
  const fullPosts = posts.length; await ui.control('party-add').evaluate(button => button.click()); assert.equal(posts.length, fullPosts);
  await member(50); await command('party-remove'); await assertParty([5, 2]);
  checks.push({ name: 'Explicit add/remove up to three, stable selected ID and clear full state', selectedInOrder: [5, 50, 2], rejectedFourthPosts: 0 });

  await member(60); await ui.reveal('party-add');
  let committed;
  await page.route('**/api/save-sync', async route => { const response = await route.fetch(); committed = await response.json(); await route.abort(); }, { times: 1 });
  await ui.tap('party-add'); await ui.onScreen('saving');
  await page.waitForFunction(() => !document.querySelector('[data-device-action=retry]')?.disabled);
  const pending = await page.evaluate(key => localStorage.getItem(key), pendingKey), request = posts.at(-1).raw;
  assert.deepEqual(committed.state.partyMemberIds, [5, 2, 60]); assert.ok(pending);
  const storage = await context.storageState(); await context.close();
  await new Promise(resolve => app.server.close(resolve)); app.close();
  app = await startServer({ seedSource: () => 12345, dataDir, port, corePath, battleCorePath });
  await open(storage); await ui.onScreen('saving');
  assert.equal(await page.evaluate(key => localStorage.getItem(key), pendingKey), pending);
  const retried = await command('retry'); await ui.onScreen('companion');
  assert.deepEqual(retried, committed); assert.equal(posts.at(-1).raw, request); await assertParty([5, 2, 60]);
  assert.equal(await ui.screen.getAttribute('data-member-id'), '60'); await ui.menu('companions');
  assert.equal(await ui.screen.locator('[data-selected=true]').getAttribute('data-member-id'), '60');
  checks.push({ name: 'Lost acknowledgement, browser reopen and service restart preserve exact pending command', exactRetryReceipt: true, selectedId: 60, party: [5, 2, 60] });

  let saved = await http(); const rests = Array.from({ length: saved.state.recoveryRestCount }, () => event('rest'));
  await seed([...rests, event('walk', 100)]); saved = await http(); assert.equal(saved.state.phase, 'encounter');
  const beforeReward = saved; await reload('battle');
  // Collection access is still permitted, but assignment during wild play isn't.
  await ui.tap('make-room', 'companions'); await member(5);
  assert.equal(await ui.control('party-remove').isDisabled(), true); assert.match(await ui.screen.locator('.screen-detail').textContent(), /XP companions stay locked until Home/);
  await ui.back('companions'); await ui.back('battle');
  for (let turn = 0; turn < 40 && saved.state.phase === 'encounter'; turn++) {
    const options = ['attack', 'magic', ...(saved.state.energy >= 6 ? ['heavy'] : [])].map(type => ({ type,
      next: native([...saved.events, event(type)]) })).sort((a, b) => b.next.xp - a.next.xp || a.next.wildHp - b.next.wildHp);
    await command(options[0].type); saved = await http();
  }
  assert.equal(saved.state.phase, 'home'); const baseXP = 20 + 6 * beforeReward.state.wildLevel;
  const recipients = [beforeReward.state.activeCreatureId, ...beforeReward.state.partyMemberIds];
  for (const member of beforeReward.state.collection) {
    const after = saved.state.collection.find(next => next.id === member.id);
    assert.equal(after.xp, recipients.includes(member.id) ? Math.min(7600, member.xp + baseXP) : member.xp);
  }
  checks.push({ name: 'Actual wild victory gives each selected Digimon the full reward exactly once', baseXP, recipients,
    partnerWasCapped: beforeReward.state.xp === 7600, nonselectedXPUnchanged: true });
  await member(60); await ui.tap('member-progression', 'progression');
  assert.equal(Number(await ui.screen.getAttribute('data-xp')), saved.state.collection.find(m => m.id === 60).xp);
  await shot('companion-earned-xp');

  await ui.menu('battle-mode'); await ui.tap('practice-start', 'battle-select-mode'); await ui.tap('practice-tactical', 'battle-choice');
  const practiceSave = await http(); await member(5);
  assert.equal(await ui.control('party-remove').isDisabled(), true); assert.match(await ui.screen.locator('.screen-detail').textContent(), /Finish or retreat from practice/);
  await ui.menu('battle-mode'); await ui.tap('practice-retreat'); await gameReady(page); assert.deepEqual(await http(), practiceSave);
  checks.push({ name: 'Wild and practice assignments locked; practice awards no pet XP', unchangedSave: true });

  await member(5); const promoted = await command('select-companion'); assert.deepEqual(promoted.state.partyMemberIds, [2, 60]);
  assert.equal(promoted.state.activeCreatureId, 5); await ui.back('companions'); assert.deepEqual(await pageIds(), [5, 2, 60]);
  await shot('pinned-party-after-promotion');
  await member(2); await ui.tap('release-review', 'release-review'); await ui.tap('release-confirmation', 'release-confirm');
  const released = await command('confirm-release'); assert.deepEqual(released.state.partyMemberIds, [60]);
  assert.equal(released.state.collection.length, 59); assert.equal(released.state.nextMemberId, promoted.state.nextMemberId);
  await ui.tap('release-done', 'companions'); await reload(); await ui.menu('companions');
  assert.deepEqual(await pageIds(), [5, 60, 59]); await assertParty([60]);
  checks.push({ name: 'Promotion removes extra without adding former partner; release cleans selection; reload keeps order', active: 5, party: [60], firstPage: [5, 60, 59] });

  // Verify the same explicit assignment/removal through the two physical-button
  // simulator inputs, without hidden-control activation.
  const buttonStorage = await context.storageState(); await context.close(); await open(buttonStorage, 'buttons'); await ui.onScreen('home');
  const left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
  async function choose(id) {
    for (let index = 0; index < 32; index++) { if (await ui.screen.locator('[data-selected=true]').getAttribute('data-device-action') === id) { await right.click(); return; } await left.click(); }
    throw new Error(`Cannot reach ${id} with two buttons`);
  }
  await choose('menu'); await choose('companions'); await choose('member-59'); await choose('party-add'); await gameReady(page);
  await assertParty([60, 59]); await choose('party-remove'); await gameReady(page); await assertParty([60]);
  await holdDeviceBack(page); await ui.onScreen('companions'); assert.equal(await ui.screen.locator('[data-selected=true]').getAttribute('data-member-id'), '59');
  checks.push({ name: 'Two buttons reach add/remove and retain member ID after reorder', commands: 2, retainedId: 59 });

  const beforeLegacy = await http(), oldPending = { deviceId: identity.deviceId, rulesVersion: 14,
    baseRevision: beforeLegacy.revision, batchId: 'party-preserved-legacy14', events: [event('feed')] };
  const oldBytes = JSON.stringify(oldPending);
  await page.evaluate(({ key, value }) => localStorage.setItem(key, value), { key: pendingKey, value: oldBytes });
  await reload('saving');
  const conflict = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.status() === 409);
  await choose('retry'); await conflict;
  await page.waitForFunction(() => document.querySelector('[data-device-action=open-recovery]'));
  assert.equal(posts.at(-1).raw, JSON.stringify({ rulesVersion: 14, baseRevision: oldPending.baseRevision, batchId: oldPending.batchId, events: oldPending.events }));
  assert.equal(await page.evaluate(key => localStorage.getItem(key), pendingKey), oldBytes); assert.deepEqual(await http(), beforeLegacy);
  checks.push({ name: 'Earlier rules14 pending request is preserved through rules15 reconciliation', oldRule: 14, currentRule: 15, exactPendingBytes: true, noReplayOrRelabel: true });

  assert.deepEqual(errors, []); for (const file of [...sources, ...binaries]) assert.equal(digest(file.path), file.sha256, `Frozen source changed: ${file.path}`);
} catch (error) { failure = error; if (page && !page.isClosed()) await page.screenshot({ path: join(evidenceDir, 'failure.png') }).catch(() => {}); }
finally {
  const report = { result: failure ? 'FAIL' : 'PASS', failure: failure?.stack, sources, binaries, checks, geometry, screenshots, pageErrors: errors,
    controls: 'Actual on-screen taps and two device simulator buttons; one disabled accessibility click additionally probes the full-party guard.',
    resourceNote: 'Measured desktop DOM counts and navigation latency; no claim about physical device RAM or timings.',
    privacy: 'Fresh synthetic identities/saves, legitimate public-command histories, private artwork requests blocked; credentials and raw saves omitted.' };
  writeFileSync(join(evidenceDir, 'evidence.json'), JSON.stringify(report, null, 2) + '\n'); console.log(JSON.stringify(report, null, 2));
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}
if (failure) throw failure;

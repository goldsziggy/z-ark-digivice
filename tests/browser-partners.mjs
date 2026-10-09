// The browser navigates only with the two physical-button simulators. The full
// roster fixture is a real bounded C++ event history submitted through HTTP;
// no creature/save fields, damage values or RNG state are fabricated in JS.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';
import { legacyFixture } from './legacy-fixture.ts';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-partners-browser-'));
const { store, identities } = legacyFixture();
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(store));
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const app = await startServer({ dataDir, port: 0, corePath, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' });
page.setDefaultTimeout(8000);
const screen = page.locator('#device-ui');
const left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
const control = id => screen.locator(`[data-device-action="${id}"]`);
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
const errors = [], posts = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) posts.push(request.postDataJSON()); });
const identity = identities[0];
let current;

async function choose(id, execute = true) {
  for (let count = 0; count < 16; count++) {
    if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) {
      if (execute) await right.click(); return;
    }
    await left.click();
  }
  throw new Error(`Two-button selection cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function back(expected) { await holdDeviceBack(page); if (expected) await onScreen(expected); }
async function home() {
  for (let count = 0; count < 12 && await screen.getAttribute('data-screen') !== 'home'; count++) await back();
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await choose(id); await onScreen(id); }
async function roster() { await menu('companions'); }
async function member(id) {
  if (await screen.getAttribute('data-screen') !== 'companions') await roster();
  for (let count = 0; count < 3; count++) {
    if (await control(`member-${id}`).count()) { await choose(`member-${id}`); await onScreen('companion'); return; }
    await choose('companions-next');
  }
  throw new Error(`Member ${id} is unreachable through roster paging.`);
}
async function read(path = '/api/save') {
  const response = await fetch(`${base}${path}`, { headers: { Authorization: `Bearer ${identity.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function command(id, path = '/api/save-sync') {
  await choose(id, false);
  const response = page.waitForResponse(response => response.url().endsWith(path) && response.status() === 200 && response.request().method() === 'POST');
  await right.click(); const result = await (await response).json();
  if (path === '/api/save-sync') {
    current = result;
    await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), result.revision);
    await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') === null && !document.querySelector('#device-confirm-button').disabled);
  }
  return result;
}
async function seedFullRoster() {
  const events = [];
  let state = current.state;
  function append(type, value = 0) {
    events.push({ type, value });
    state = JSON.parse(execFileSync(corePath, ['--replay', String(current.seed)], {
      input: events.map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', timeout: 2000, maxBuffer: 32768,
    }));
  }
  append('mode', 1);
  const duplicateCaptured = () => state.collection.filter(member => member.id !== 1).some((member, index, all) => all.slice(index + 1).some(other => other.species === member.species));
  // Let the native Auto policy build real outcomes across the expanded roster.
  // Keep the founder plus six captures while seeking a duplicate individual;
  // releases intentionally exercise stable IDs beyond collection capacity.
  for (let encounters = 0; encounters < 40 && (state.collection.length < 8 || !duplicateCaptured()); encounters++) {
    if (state.collection.length === 8) append('release', state.collection.at(-1).id);
    for (let count = 0; count < 8; count++) append('rest');
    append('walk', 100); append('auto');
    assert.equal(state.phase, 'home');
  }
  assert.ok(duplicateCaptured(), 'bounded real captures include same-species individuals');
  append('mode', 0);
  assert.equal(state.collection.length, 8);
  // Same-species individuals receive different genuine care histories.
  for (const captured of state.collection) {
    append('select', captured.id);
    for (let count = 0; count < captured.id % 3; count++) append('feed');
    for (let count = 0; count < captured.id % 2; count++) append('play');
  }
  append('select', 1);
  assert.ok(events.length <= 200, 'fixture remains a small bounded native history');
  for (let offset = 0; offset < events.length; offset += 100) {
    const response = await fetch(`${base}/api/save-sync`, {
      method: 'POST', headers: { Authorization: `Bearer ${identity.token}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ rulesVersion: current.state.rulesVersion, baseRevision: current.revision, batchId: crypto.randomUUID(), events: events.slice(offset, offset + 100) }),
    });
    assert.equal(response.status, 200); current = await response.json();
  }
  assert.deepEqual(current.state, state);
}
async function statsMatch(captured) {
  for (const key of ['maxHp', 'attack', 'defense', 'magic', 'resistance']) assert.equal(Number(await screen.locator(`[data-stat="${key}"]`).textContent()), captured.combat[key]);
  assert.equal(await screen.getAttribute('data-creature-type'), captured.combat.type);
}
async function layout() {
  const failures = await screen.evaluate(root => {
    const circle = document.querySelector('#screen-surface').getBoundingClientRect();
    const cx = circle.x + circle.width / 2, cy = circle.y + circle.height / 2, radius = circle.width / 2;
    const buttons = [...root.querySelectorAll('button')].filter(button => button.getClientRects().length);
    const boxes = buttons.map(button => ({ label: button.textContent.trim(), box: button.getBoundingClientRect() }));
    const errors = [];
    for (const { label, box } of boxes) if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > radius + 2)) errors.push(`clipped: ${label}`);
    for (let i = 0; i < boxes.length; i++) for (let j = i + 1; j < boxes.length; j++) {
      const a = boxes[i].box, b = boxes[j].box;
      if (Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 1) errors.push(`overlap: ${boxes[i].label}/${boxes[j].label}`);
    }
    return errors;
  });
  assert.deepEqual(failures, [], `round ${await screen.getAttribute('data-screen')} layout, member ${await screen.getAttribute('data-member-id') ?? 'none'}`);
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
}

try {
  // The defensive unpaired UI stays empty and offers setup. Then reopen a real
  // pre-onboarding zero-event Mote save, which already has exactly one founder.
  await page.goto(`${base}/?controls=buttons`); await onScreen('home');
  assert.equal(await page.locator('#collection-count').textContent(), '0 / 8');
  assert.equal(await screen.locator('[data-device-action^="member-"]').count(), 0);
  assert.equal(await page.locator('[data-select-member]').count(), 0);
  await page.evaluate(identity => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)), identity);
  await page.reload(); await onScreen('home');
  await page.waitForFunction(() => document.querySelector('#device-screen-title')?.textContent.includes('Mote'));
  current = await read();
  assert.equal(current.revision, 0); assert.equal(current.state.creature, 'Mote');
  assert.deepEqual(current.state.onboarding, { completed: true, starterId: null });
  await roster(); assert.equal(await screen.locator('[data-device-action^="member-"]').count(), 1);
  await member(1); await layout();
  assert.equal(await control('select-companion').isDisabled(), true);
  assert.match(await control('select-companion').textContent(), /current|partner|active/i);
  const beforeCurrent = await read(), beforeCurrentPosts = posts.length;
  await choose('member-stats'); await statsMatch(current.state.collection[0]);
  await back('companion');
  assert.deepEqual(await read(), beforeCurrent); assert.equal(posts.length, beforeCurrentPosts, 'viewing current partner never posts select');

  await seedFullRoster(); await page.reload(); await onScreen('home');
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), current.revision);
  const original = structuredClone(current.state.collection);
  const duplicate = original.find(captured => captured.id !== 1 && original.some(other => other.id !== 1 && other.id !== captured.id && other.species === captured.species));
  assert.ok(duplicate);
  const sameSpecies = original.filter(captured => captured.id !== 1 && captured.species === duplicate.species);
  assert.ok(sameSpecies.length >= 2); assert.notEqual(sameSpecies[0].id, sameSpecies[1].id);
  const first = sameSpecies[0], second = sameSpecies[1];
  assert.ok(['hp', 'energy', 'fullness', 'mood', 'bond', 'level'].some(key => first[key] !== second[key]), 'duplicate-species individuals have distinct real care histories');

  await roster();
  const visited = new Set();
  for (let pageIndex = 0; pageIndex < 3; pageIndex++) {
    const cards = screen.locator('[data-device-action^="member-"]');
    const ids = await cards.evaluateAll(buttons => buttons.map(button => Number(button.dataset.deviceAction.slice(7))));
    assert.ok(ids.length >= 1 && ids.length <= 3); await layout();
    for (const id of ids) {
      assert.equal(visited.has(id), false, 'paging must not repeat or omit an individual'); visited.add(id);
      const captured = original.find(entry => entry.id === id);
      const cardText = await control(`member-${id}`).textContent();
      assert.ok(cardText.includes(captured.name));
      assert.equal(await control(`member-${id}`).locator('.roster-id').textContent(), `#${String(id).padStart(2, '0')}`, 'same-species cards expose their individual IDs');
      assert.ok(cardText.includes(`HP ${captured.hp} / ${captured.combat.maxHp}`));
      assert.equal(await control(`member-${id}`).getAttribute('data-member-id'), String(id));
      assert.equal(await control(`member-${id}`).getAttribute('data-current'), String(id === current.state.activeCreatureId));
      assert.equal(/current|partner/i.test(cardText), id === current.state.activeCreatureId);
      await member(id); await layout();
      assert.equal(await screen.getAttribute('data-member-id'), String(id));
      assert.equal(await screen.getAttribute('data-current'), String(id === current.state.activeCreatureId));
      const care = await screen.locator('.screen-stats').evaluate(root => Object.fromEntries([...root.querySelectorAll('div')].map(row => [row.querySelector('dt').textContent, Number(row.querySelector('dd').textContent)])));
      assert.deepEqual(care, { Energy: captured.energy, Fullness: captured.fullness, Mood: captured.mood, Bond: captured.bond });
      assert.ok((await screen.locator('.member-family').textContent()).includes(captured.stage || `Level ${captured.level}`));
      assert.ok((await screen.locator('.roster-health').textContent()).includes(`HP ${captured.hp} / ${captured.combat.maxHp}`));
      assert.equal(await control('select-companion').isDisabled(), id === current.state.activeCreatureId);
      await choose('member-stats'); await statsMatch(captured); await layout();
      await choose('skills');
      for (const skill of Object.values(captured.combat.skills)) assert.ok((await screen.textContent()).includes(skill));
      await back('stats'); await back('companion'); await back('companions');
      assert.equal(await screen.locator('[data-selected="true"]').getAttribute('data-device-action'), `member-${id}`, 'detail Back restores the exact individual');
    }
    await choose('companions-next');
  }
  assert.deepEqual([...visited].sort((a, b) => a - b), original.map(entry => entry.id));
  assert.ok(await control('member-1').count(), 'last page wraps to the first page');
  assert.deepEqual((await read()).state.collection, original);

  // Normal acknowledgment updates only the active ID/projection, never any
  // individual's care, combat stats, capture metadata, or stored moves.
  await member(first.id); const beforeSwap = await read();
  await command('select-companion');
  assert.equal(current.state.activeCreatureId, first.id); assert.equal(current.revision, beforeSwap.revision + 1);
  assert.deepEqual(current.state.collection, original);
  for (const key of ['hp', 'energy', 'fullness', 'mood', 'bond', 'level', 'xp', 'xpToNext', 'formId', 'combat']) assert.deepEqual(current.state[key], first[key]);
  assert.equal(await control('select-companion').isDisabled(), true);
  assert.match(await screen.textContent(), /current partner/i);

  // Same species, different ID: lose the actual committed acknowledgment.
  await back('companions'); await member(second.id);
  let committed;
  await page.route('**/api/save-sync', async route => { const response = await route.fetch(); committed = await response.json(); await route.abort('failed'); });
  await choose('select-companion'); await onScreen('saving');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="retry"]')?.disabled);
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  assert.equal(JSON.parse(pending).events[0].value, second.id);
  assert.deepEqual(JSON.parse(pending).events, [{ type: 'select', value: second.id }]);
  await back('saving'); assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  await page.unroute('**/api/save-sync'); await page.reload(); await onScreen('saving');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  await command('retry'); assert.equal(current.revision, committed.revision); assert.deepEqual(current.state, committed.state);
  await onScreen('companion'); assert.equal(await screen.getAttribute('data-member-id'), String(second.id));
  assert.equal(await screen.getAttribute('data-current'), 'true');
  assert.equal(posts.at(-1).batchId, JSON.parse(pending).batchId);
  assert.deepEqual(current.state.collection, original);
  await page.reload(); await onScreen('home'); await roster(); await member(second.id);
  assert.equal(await control('select-companion').isDisabled(), true); assert.match(await screen.textContent(), /current partner/i);
  assert.equal((await read()).state.activeCreatureId, second.id);

  // Wild encounters and active practice both lock the partner with a truthful
  // reason. Browsing other individual stats remains available without writes.
  await menu('explore'); await command('walk'); await onScreen('battle');
  const wildSave = await read(), wildPosts = posts.length;
  await roster(); await member(first.id);
  assert.equal(await control('select-companion').isDisabled(), true);
  assert.match(await screen.textContent(), /encounter|wild|battle/i);
  await choose('member-stats'); await statsMatch(first); await back('companion');
  assert.deepEqual(await read(), wildSave); assert.equal(posts.length, wildPosts);
  await home(); await choose('battle');
  for (let count = 0; count < 30 && current.state.phase === 'encounter'; count++) await command('attack');
  assert.equal(current.state.phase, 'home');
  await menu('battle-mode'); await choose('practice-start'); await onScreen('battle-select-mode'); await command('practice-tactical', '/api/battle/start'); await onScreen('battle-choice');
  const practiceSave = await read(), practiceBattle = await read('/api/battle'), practicePosts = posts.length;
  await roster(); await member(first.id);
  assert.equal(await control('select-companion').isDisabled(), true);
  assert.match(await screen.textContent(), /practice/i);
  await choose('member-stats'); await statsMatch(practiceSave.state.collection.find(entry => entry.id === first.id)); await back('companion');
  assert.deepEqual(await read(), practiceSave); assert.deepEqual(await read('/api/battle'), practiceBattle); assert.equal(posts.length, practicePosts);
  await menu('battle-mode'); await command('practice-retreat', '/api/battle/act');
  await roster(); await member(first.id); assert.equal(await control('select-companion').isDisabled(), false);
  await command('select-companion'); assert.equal(current.state.activeCreatureId, first.id);
  assert.deepEqual(current.state.collection, practiceSave.state.collection);

  // Another authenticated client starts practice after this UI cached its idle
  // status. The server rejection must retain the selection without inventing a
  // newer care revision or reporting a completed partner change.
  await back('companions'); await member(second.id);
  assert.equal(await control('select-companion').isDisabled(), false);
  const beforeRace = await read(), idlePractice = await read('/api/battle');
  const started = await fetch(`${base}/api/battle/start`, { method: 'POST', headers: { Authorization: `Bearer ${identity.token}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ rulesVersion: 7, expectedRevision: idlePractice.revision, requestId: crypto.randomUUID() }) });
  assert.equal(started.status, 200);
  const rejection = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 409);
  await choose('select-companion');
  assert.equal((await (await rejection).json()).error, 'partner_locked'); await onScreen('saving');
  await page.waitForFunction(() => document.querySelector('#notice-text').textContent.includes('practice'));
  assert.doesNotMatch(await page.locator('#notice-text').textContent(), /newer save|older save/i);
  assert.deepEqual(await read(), beforeRace);
  const rejectedPending = await page.evaluate(() => JSON.parse(localStorage.getItem('digivice.dev.pending.v1')));
  assert.deepEqual(rejectedPending.events, [{ type: 'select', value: second.id }]);
  assert.equal(rejectedPending.baseRevision, beforeRace.revision);
  assert.deepEqual(errors, []); assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  console.log('PASS: two-button unpaired/one/eight roster; three-card paging and exact focus; duplicate species with distinct IDs/care; every detail/stats/moves; current partner badge/no POST; all member state preserved by Set Partner; committed lost ACK/exact retry/reload; wild and active-practice partner locks; unlock after retreat; new-practice race reports true rejection and preserves pending selection.');
} finally {
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

// Real native rules + isolated HTTP service. Every UI choice uses the two device
// buttons: left tap Next, left hold Back, right release Confirm. No arrow keys,
// screen-choice clicks, injected game state, remote writes, or JS damage model.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { legacyFixture } from './legacy-fixture.ts';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-stats-browser-'));
const { store, identities } = legacyFixture();
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(store));
const app = await startServer({ dataDir, port: 0 });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' });
page.setDefaultTimeout(8000);
const errors = [], petPosts = [], battlePosts = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (request.method() !== 'POST') return;
  if (request.url().endsWith('/api/save-sync')) petPosts.push(request.postDataJSON());
  if (/\/api\/battle\/(start|act)$/.test(request.url())) battlePosts.push(request.postDataJSON());
});
const screen = page.locator('#device-ui');
const left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
const selected = () => screen.locator('[data-selected="true"]').getAttribute('data-device-action');
const next = () => left.click();
const confirm = () => right.click();
const identity = identities[0];
let save, battle;
async function choose(id, execute = true) {
  for (let attempt = 0; attempt < 32; attempt++) {
    if (await selected() === id) { if (execute) await confirm(); return; }
    await next();
  }
  throw new Error(`Two-button selection could not reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function hold(button = left, milliseconds = 660) {
  const bounds = await button.boundingBox(); assert.ok(bounds);
  await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
  await page.mouse.down(); await page.waitForTimeout(milliseconds); await page.mouse.up();
}
async function home() {
  for (let count = 0; count < 12 && await screen.getAttribute('data-screen') !== 'home'; count++) await hold();
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await onScreen('menu'); await choose(id); }
async function read(path = '/api/save') {
  const response = await fetch(`${base}${path}`, { headers: { Authorization: `Bearer ${identity.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function pet(id) {
  await choose(id, false);
  const response = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 200);
  await confirm(); save = await (await response).json();
  await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), save.revision);
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') === null && !document.querySelector('#device-confirm-button').disabled);
  return save;
}
async function practice(id) {
  await choose(id, false);
  const response = page.waitForResponse(response => /\/api\/battle\/(start|act)$/.test(response.url()) && response.request().method() === 'POST' && response.status() === 200);
  await confirm(); battle = await (await response).json(); return battle;
}
async function shownStats(combat) {
  for (const key of ['maxHp', 'attack', 'defense', 'magic', 'resistance']) {
    assert.equal(Number(await screen.locator(`[data-stat="${key}"]`).textContent()), combat[key], `displayed ${key} comes from the authoritative profile`);
  }
  assert.equal(await screen.getAttribute('data-creature-type'), combat.type);
}
function nativeNext(saved, type, value = 0) {
  assert.equal(saved.baseSequence, 0, 'only this legacy zero-event identity is replayed by the native oracle');
  return JSON.parse(execFileSync(resolve('build/digivice-core'), ['--replay', String(saved.seed)], {
    input: [...saved.events, { type, value }].map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', timeout: 2000, maxBuffer: 32768,
  }));
}
async function geometry() {
  const failures = await screen.evaluate(root => {
    const circle = root.closest('.device-screen').getBoundingClientRect();
    const cx = circle.x + circle.width / 2, cy = circle.y + circle.height / 2, radius = circle.width / 2;
    return [...root.querySelectorAll('button')].filter(button => button.getClientRects().length).flatMap(button => {
      const box = button.getBoundingClientRect();
      return [[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > radius + 2) ? [button.textContent.trim()] : [];
    });
  });
  assert.deepEqual(failures, [], 'controls fit the 390px circular viewport');
}

try {
  // Import only the credential of a genuine pre-onboarding saved identity.
  // New pairing starts an egg; these regression scenarios retain their Mote.
  await page.addInitScript(identity => {
    if (!localStorage.getItem('digivice.dev.identity.v1')) localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity));
  }, identity);
  await page.goto(`${base}/?controls=buttons`); await onScreen('home');
  await page.waitForFunction(() => document.querySelector('#device-screen-title')?.textContent.includes('Mote'));
  save = await read();
  assert.equal(save.state.schemaVersion, 7); assert.equal(save.state.rulesVersion, 4);
  assert.equal(save.revision, 0); assert.equal(save.state.creature, 'Mote');
  assert.deepEqual(save.state.onboarding, { completed: true, starterId: null });
  const initialCombat = structuredClone(save.state.combat);
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);

  // Previously only Care was reachable. Cycle every current menu branch
  // using the left button; holding Back must not change restored selection.
  await choose('menu'); await onScreen('menu');
  for (const id of ['care', 'companions', 'explore', 'cards', 'settings', 'battle-mode', 'stats', 'type-chart']) {
    await choose(id); await onScreen(id); await geometry(); await hold(); await onScreen('menu');
    assert.equal(await selected(), id, 'hold-release must not advance the restored menu choice');
  }
  assert.equal(petPosts.length, 0); assert.equal(battlePosts.length, 0);
  await choose('companions'); await choose('member-1'); await choose('member-stats'); await onScreen('stats');
  await shownStats(initialCombat); await geometry();
  await choose('skills'); await onScreen('skills'); await geometry();
  for (const skill of Object.values(initialCombat.skills)) assert.ok((await screen.textContent()).includes(typeof skill === 'string' ? skill : skill.name));
  await hold(); await onScreen('stats'); await choose('type-chart'); await onScreen('type-chart'); await geometry();

  await menu('care');
  const beforeFeed = petPosts.length;
  await choose('feed', false); await hold(right, 800);
  await page.waitForFunction(() => document.querySelector('#revision').textContent === '1');
  save = await read(); assert.equal(petPosts.length, beforeFeed + 1, 'right hold confirms once on release');

  // Both wild move families are executed by the native game engine.
  await menu('explore'); await pet('walk'); await onScreen('battle');
  await choose('wild-stats'); await onScreen('stats'); await shownStats(save.state.wildCombat); await hold(); await onScreen('battle');
  const beforePhysical = await read(), expectedPhysical = nativeNext(beforePhysical, 'attack');
  await pet('attack'); assert.deepEqual(save.state, expectedPhysical);
  if (save.state.phase !== 'encounter') { await menu('explore'); await pet('walk'); }
  const beforeMagic = await read(), expectedMagic = nativeNext(beforeMagic, 'magic');
  await pet('magic'); assert.deepEqual(save.state, expectedMagic);

  // Find a capture-eligible state through native replay, never a JS damage
  // calculation. Then hold Back before the 1200ms wind-up sends any request.
  if (save.state.phase !== 'encounter') { await menu('explore'); await pet('walk'); }
  for (let step = 0; step < 8 && save.state.wildHp > Math.floor(save.state.wildMaxHp / 2); step++) {
    const current = await read();
    const options = ['attack', 'magic', ...(current.state.energy >= 6 ? ['heavy'] : [])].map(type => ({ type, state: nativeNext(current, type) })).filter(option => option.state.phase === 'encounter');
    options.sort((a, b) => a.state.wildHp - b.state.wildHp);
    assert.ok(options.length, 'balance offers a nonlethal capture preparation');
    await pet(options[0].type); assert.deepEqual(save.state, options[0].state);
  }
  assert.equal(save.state.phase, 'encounter'); assert.ok(save.state.wildHp <= Math.floor(save.state.wildMaxHp / 2));
  const beforeCapture = await read(), beforeCapturePosts = petPosts.length;
  await choose('capture'); await onScreen('capture'); await hold(); await onScreen('battle');
  await page.waitForTimeout(700);
  assert.equal(petPosts.length, beforeCapturePosts); assert.deepEqual(await read(), beforeCapture);
  for (let attempt = 0; attempt < 3 && save.state.phase === 'encounter'; attempt++) await pet('capture');
  assert.equal(save.state.phase, 'home'); assert.equal(save.state.collection.length, 2);
  const captured = save.state.collection.find(member => member.id !== 1);
  await menu('companions'); await choose(`member-${captured.id}`); await choose('member-stats'); await shownStats(captured.combat);

  // Care keeps XP/level unchanged. A second real wild battle supplies the
  // experience needed for growth; every chosen move is checked against C++.
  const beforeCare = { xp: save.state.xp, level: save.state.level, formId: save.state.formId };
  await menu('care');
  await pet('play');
  for (let count = 0; count < 8 && save.state.hp < save.state.combat.maxHp; count++) await pet('rest');
  assert.deepEqual({ xp: save.state.xp, level: save.state.level, formId: save.state.formId }, beforeCare);
  assert.equal(save.state.hp, save.state.combat.maxHp);
  await menu('explore'); await pet('walk'); await onScreen('battle');
  for (let count = 0; count < 20 && save.state.phase === 'encounter'; count++) {
    const current = await read();
    const options = ['attack', 'magic', ...(current.state.energy >= 6 ? ['heavy'] : [])].map(type => ({ type, state: nativeNext(current, type) }));
    options.sort((a, b) => Number(b.state.xp > current.state.xp) - Number(a.state.xp > current.state.xp)
      || Number(b.state.phase === 'encounter') - Number(a.state.phase === 'encounter') || a.state.wildHp - b.state.wildHp);
    await pet(options[0].type); assert.deepEqual(save.state, options[0].state);
  }
  assert.equal(save.state.phase, 'home'); assert.ok(save.state.xp > beforeCare.xp);
  assert.ok(save.state.level > 1); assert.notDeepEqual(save.state.combat, initialCombat);
  assert.equal(save.state.formId, beforeCare.formId, 'XP growth does not silently choose a Digivolution');
  await menu('companions'); await choose('member-1'); await choose('member-stats'); await shownStats(save.state.combat);
  await page.reload(); await onScreen('home');
  assert.deepEqual((await read()).state.combat, save.state.combat);

  // Practice owns separate HP/state. A committed finishing action with a lost
  // reply survives reload, then routes to Result rather than illegal choices.
  const beforePractice = await read(), petCount = petPosts.length;
  await menu('battle-mode'); await choose('practice-start'); await onScreen('battle-select-mode'); await practice('practice-tactical'); await onScreen('battle-choice');
  assert.equal(battle.battle.rulesVersion, 7);
  assert.equal(battle.battle.enemyLevel, beforePractice.state.level);
  await geometry();
  await choose('practice-stats'); await onScreen('practice-stats');
  await choose('practice-player-stats'); await shownStats(battle.battle.playerCombat); await hold(); await onScreen('practice-stats');
  await choose('practice-enemy-stats'); await shownStats(battle.battle.enemyCombat); await hold(); await onScreen('practice-stats');
  await hold(); await onScreen('battle-choice');
  const hint = structuredClone(battle.battle.enemyHint);
  await choose('battle-cards'); await practice('practice-card-shelter'); await onScreen('battle-result');
  assert.deepEqual(battle.battle.enemyHint, hint); assert.equal(battle.battle.exchanges, 0);
  await choose('practice-continue'); await onScreen('battle-choice');
  await practice('practice-heavy'); await onScreen('battle-result');
  assert.equal(battle.battle.phase, 'defend'); assert.equal(battle.battle.exchanges, 1);
  assert.ok((await screen.textContent()).includes(`You −${battle.battle.lastTurn.playerDamage} HP`));
  await choose('practice-continue'); await practice('practice-counter'); await onScreen('battle-result');
  assert.equal(battle.battle.phase, 'attack'); await choose('practice-continue'); await hold(); await onScreen('battle-mode');
  let committed;
  await page.route('**/api/battle/act', async route => { const response = await route.fetch(); committed = await response.json(); await route.abort('failed'); });
  await choose('practice-retreat'); await onScreen('battle-resolve');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="practice-retry"]').disabled);
  assert.equal(committed.battle.status, 'retreated');
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1'));
  await hold(); await onScreen('battle-resolve');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), pending);
  await page.unroute('**/api/battle/act'); await page.reload(); await onScreen('battle-resolve');
  await practice('practice-retry'); await onScreen('battle-result');
  assert.deepEqual(battle, committed); assert.equal(battlePosts.at(-1).requestId, JSON.parse(JSON.parse(pending).body).requestId);
  assert.equal(await screen.locator('[data-device-action="practice-physical"]').count(), 0);
  await choose('practice-continue'); await onScreen('battle-mode');
  assert.equal(petPosts.length, petCount); assert.deepEqual(await read(), beforePractice);

  // A command from the old practice rules is preserved across review/Back.
  // Explicit archival keeps exact original bytes and never replays the action.
  const oldCommand = JSON.stringify({ formatVersion: 1, rulesVersion: 1, deviceId: identity.deviceId, path: '/api/battle/act',
    body: JSON.stringify({ expectedRevision: 0, requestId: 'old_practice_request_01', action: { type: 'retreat', value: 0 } }) });
  const beforeArchive = await read('/api/battle'), beforeArchivePosts = battlePosts.length;
  await page.evaluate(raw => localStorage.setItem('digivice.dev.battle.pending.v1', raw), oldCommand);
  await page.reload(); await onScreen('battle-resolve');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="practice-legacy-review"]')?.disabled);
  await choose('practice-legacy-review'); await onScreen('battle-recovery');
  await choose('practice-legacy-confirm'); await onScreen('battle-recovery-confirm'); await geometry();
  await hold(); await onScreen('battle-recovery');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), oldCommand);
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.archived.v1')), null);
  await choose('practice-legacy-confirm'); await choose('practice-archive-legacy'); await onScreen('battle-mode');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), null);
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.archived.v1')), oldCommand);
  assert.equal(battlePosts.length, beforeArchivePosts); assert.deepEqual(await read('/api/battle'), beforeArchive);
  assert.deepEqual(await read(), beforePractice);
  assert.deepEqual(errors, []);
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  console.log('PASS: real two-button menu reachability; hold-release suppression; authoritative stats/types/skills; native physical/magic wild moves; capture cancellation and collection; care preserves XP and native wild XP growth/reload retains form; separate same-level practice stats/cards/counter; finishing lost reply/reload exact retry reaches Result; old command review/Back preserves bytes and explicit archival sends no POST.');
} finally {
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

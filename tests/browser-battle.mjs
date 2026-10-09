// Practice uses its real C++ core and independent local service store. This
// test never changes fixtures, remote assets, or an existing user's pet save.
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';
import { legacyFixture } from './legacy-fixture.ts';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-battle-browser-'));
const { store, identities } = legacyFixture();
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(store));
const app = await startServer({ dataDir, port: 0 });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, reducedMotion: 'reduce' });
const page = await context.newPage();
const errors = [], commands = [], petCommands = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (request.method() !== 'POST') return;
  if (/\/api\/battle\/(start|act)$/.test(request.url())) commands.push(request.postDataJSON());
  if (request.url().endsWith('/api/save-sync')) petCommands.push(request.postDataJSON());
});
const screen = page.locator('#device-ui');
const control = id => screen.locator(`[data-device-action="${id}"]`);
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
async function selectChoice(id) {
  for (let count = 0; count < 32; count++) {
    if (await control(id).isVisible()) return;
    await page.locator('#device-back-button').tap();
  }
  throw new Error(`Cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
const touch = async id => { await selectChoice(id); await control(id).tap(); };
const credential = identities[0];
let current;
async function read(path) {
  const response = await fetch(`${base}${path}`, { headers: { Authorization: `Bearer ${credential.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function command(id, duplicate = false) {
  await selectChoice(id);
  const response = page.waitForResponse(result => /\/api\/battle\/(start|act)$/.test(result.url()) && result.request().method() === 'POST' && result.status() === 200);
  if (duplicate) await control(id).evaluate(button => { button.click(); button.click(); });
  else await touch(id);
  current = await (await response).json();
  return current;
}
async function continueTurn() {
  await page.waitForFunction(() => ['battle-choice', 'battle-result', 'battle-cards'].includes(document.querySelector('#device-ui')?.dataset.screen));
  if (await control('practice-continue').isVisible()) await touch('practice-continue');
}
async function checkLayout() {
  const failures = await screen.evaluate(root => {
    const surface = root.closest('.device-screen').getBoundingClientRect();
    const cx = surface.left + surface.width / 2, cy = surface.top + surface.height / 2, radius = surface.width / 2;
    const boxes = [...root.querySelectorAll('button')].filter(button => button.getClientRects().length).map(button => ({ label: button.textContent.trim(), box: button.getBoundingClientRect() }));
    const errors = [];
    for (const { label, box } of boxes) if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > radius + 2)) errors.push(`clipped: ${label}`);
    for (let i = 0; i < boxes.length; i++) for (let j = i + 1; j < boxes.length; j++) {
      const a = boxes[i].box, b = boxes[j].box;
      if (Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 1) errors.push(`overlap: ${boxes[i].label} / ${boxes[j].label}`);
    }
    return errors;
  });
  assert.deepEqual(failures, []);
}
function assertPublic(battle) {
  for (const key of ['snapshotBase64', 'snapshot', 'rngState', 'seed', 'committedChoice', 'enemyChoice', 'excludedChoice']) assert.equal(Object.hasOwn(battle, key), false, `private current intent ${key} must not be public`);
  assert.ok(battle.enemyHint.length === 0 || battle.enemyHint.length === 2);
}

try {
  // Keep the migrated historical Mote identity; new duels use current rules.
  await page.addInitScript(identity => {
    if (!localStorage.getItem('digivice.dev.identity.v1')) localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity));
  }, credential);
  await page.goto(`${base}/?controls=buttons`); await onScreen('home');
  const originalPet = await read('/api/save');
  await touch('menu'); await touch('battle-mode'); await onScreen('battle-mode');
  const beforeStart = commands.length;
  await touch('practice-start'); await onScreen('battle-select-mode');
  await command('practice-tactical', true); await onScreen('battle-choice');
  assert.equal(commands.length, beforeStart + 1);
  assert.equal(current.battle.schemaVersion, 7); assert.equal(current.battle.rulesVersion, 7);
  assert.equal(current.battle.phase, 'attack'); assert.equal(current.battle.status, 'active');
  assert.deepEqual(current.battle.companion, { id: 1, species: 'mote', name: 'Mote', level: 1, formId: 1 });
  assertPublic(current.battle);
  await checkLayout();

  // A card may prepare a turn; it must not reroll the already-published hint.
  const beforeCard = structuredClone(current);
  await touch('battle-cards'); await onScreen('battle-cards'); await checkLayout();
  await command('practice-card-spark'); await continueTurn();
  assert.deepEqual(current.battle.enemyHint, beforeCard.battle.enemyHint);
  assert.equal(current.battle.phase, beforeCard.battle.phase); assert.equal(current.battle.exchanges, 0);
  assert.equal(current.battle.cardUsed, true); assert.equal(current.battle.attackBoost, 5);

  // Back only leaves the view. Resuming reads the same duel, not a new seed.
  const beforeBack = await read('/api/battle'), beforeBackCommands = commands.length;
  await page.locator('#device-back').tap(); await onScreen('battle-mode');
  await touch('practice-resume'); await onScreen('battle-choice');
  assert.deepEqual(await read('/api/battle'), beforeBack); assert.equal(commands.length, beforeBackCommands);

  const beforeAttack = structuredClone(current.battle), beforeAttackCommands = commands.length;
  await command('practice-physical', true);
  assert.equal(commands.length, beforeAttackCommands + 1);
  assert.equal(current.battle.phase, 'defend'); assert.equal(current.battle.exchanges, 1);
  assert.equal(beforeAttack.enemyHp - current.battle.enemyHp, current.battle.lastTurn.enemyDamage);
  assert.equal(beforeAttack.playerHp - current.battle.playerHp, current.battle.lastTurn.playerDamage);
  assert.ok(current.battle.lastTurn.enemyDamage > 0); assertPublic(current.battle);
  await onScreen('battle-result'); await checkLayout();
  assert.ok((await screen.locator('.screen-detail').textContent()).includes(`You −${current.battle.lastTurn.playerDamage} HP · Rival −${current.battle.lastTurn.enemyDamage} HP`));
  await continueTurn(); await onScreen('battle-choice');

  // The real service commits defense, then its reply is deliberately dropped.
  // Back, care and identity switching must not erase the durable exact command.
  const beforeDefense = structuredClone(current.battle);
  let committed;
  await page.route('**/api/battle/act', async route => { const response = await route.fetch(); committed = await response.json(); await route.abort('failed'); });
  await touch('practice-brace'); await onScreen('battle-resolve');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="practice-retry"]')?.disabled);
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1'));
  assert.ok(pending); assert.equal(JSON.parse(JSON.parse(pending).body).action.type, 'brace');
  await holdDeviceBack(page);
  assert.equal(await screen.getAttribute('data-screen'), 'battle-resolve', 'held Back cannot discard a sent practice command');
  await page.locator('#screen-surface').focus(); await page.keyboard.press('Escape');
  assert.equal(await screen.getAttribute('data-screen'), 'battle-resolve');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), pending);
  assert.equal(await page.locator('#new-playtest').isDisabled(), true);
  assert.equal(await page.locator('[data-event="feed"]').isDisabled(), true);
  await checkLayout();
  await page.unroute('**/api/battle/act');
  await page.reload(); await onScreen('battle-resolve');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), pending);
  await command('practice-retry');
  assert.equal(commands.at(-1).requestId, JSON.parse(JSON.parse(pending).body).requestId);
  assert.equal(current.revision, committed.revision); assert.deepEqual(current.battle, committed.battle);
  assert.equal(current.battle.phase, 'attack'); assert.equal(current.battle.exchanges, 2);
  assert.equal(beforeDefense.playerHp - current.battle.playerHp, current.battle.lastTurn.playerDamage);
  assert.equal(beforeDefense.enemyHp - current.battle.enemyHp, current.battle.lastTurn.enemyDamage);
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), null);
  await continueTurn();

  await page.locator('#device-back').tap(); await onScreen('battle-mode');
  await command('practice-retreat');
  assert.equal(current.battle.status, 'retreated'); assert.equal(current.battle.phase, 'finished');
  assertPublic(current.battle);
  // The result offers either a lobby return or the next duel directly.
  if (await control('practice-continue').isVisible()) await touch('practice-continue');
  if (await screen.getAttribute('data-screen') !== 'battle-mode') await page.locator('#device-back').tap();
  await onScreen('battle-mode');
  const previousRevision = current.revision;
  await touch('practice-start'); await onScreen('battle-select-mode');
  await command('practice-tactical'); await onScreen('battle-choice');
  assert.equal(current.revision, previousRevision + 1);
  assert.equal(current.battle.sequence, 0);
  assert.equal(current.battle.playerHp, current.battle.playerCombat.maxHp);
  assert.equal(current.battle.enemyHp, current.battle.enemyCombat.maxHp);
  assert.deepEqual(await read('/api/save'), originalPet);
  assert.deepEqual(petCommands, []); assert.deepEqual(errors, []);
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  console.log('PASS: round Battle start; real alternating damage/results; card preserves committed hint; Back/resume keeps duel; duplicate input coalesces; committed lost reply survives navigation/reload and retries once; pending blocks care/save switching; retreat/new duel; private intent omitted; entire pet save unchanged.');
} finally {
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

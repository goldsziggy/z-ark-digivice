// Real native progression through isolated HTTP persistence. UI navigation is
// exclusively the two device buttons; HTTP fixtures contain only legal events.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-rpg-browser-'));
let app = await startServer({ dataDir, port: 0 });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const errors = [], posts = [], geometryChecks = [], screenshots = [];
const evidence = process.env.RPG_EVIDENCE === '1';
let context, page, screen, left, right, identity;
async function http(path, body, credential = identity) {
  const response = await fetch(`${base}${path}`, { method: body === undefined ? 'GET' : 'POST',
    headers: { ...(credential ? { Authorization: `Bearer ${credential.token}` } : {}), ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  assert.ok(response.ok, `${path}: ${response.status} ${response.ok ? '' : await response.text()}`); return response.json();
}
const read = () => http('/api/save');
async function seed(events) {
  const saved = await read();
  return http('/api/save-sync', { rulesVersion: saved.state.rulesVersion, baseRevision: saved.revision, batchId: crypto.randomUUID(), events });
}
async function open(credential, state) {
  identity = credential;
  context = await browser.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce', ...(state ? { storageState: state } : {}) });
  page = await context.newPage(); page.setDefaultTimeout(8000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) posts.push(request.postDataJSON()); });
  if (!state) await page.addInitScript(value => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(value)), credential);
  screen = page.locator('#device-ui'); left = page.locator('#device-back-button'); right = page.locator('#device-confirm-button');
  await page.goto(`${base}/?controls=buttons`);
  await page.waitForFunction(id => document.querySelector('#device-id')?.textContent === id, credential.deviceId);
}
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
async function choose(id, execute = true) {
  for (let tries = 0; tries < 32; tries++) {
    if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) { if (execute) await right.click(); return; }
    await left.click();
  }
  throw new Error(`Unreachable ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function back(expected) { await holdDeviceBack(page); if (expected) await onScreen(expected); }
async function home() {
  if (await screen.getAttribute('data-screen') === 'wild-auto-result') await choose('wild-auto-done');
  if (await screen.getAttribute('data-screen') === 'evolution-result') await choose('evolution-home');
  for (let tries = 0; tries < 14 && await screen.getAttribute('data-screen') !== 'home'; tries++) await back();
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await choose(id); await onScreen(id); }
async function gameCommand(id) {
  await choose(id, false);
  const received = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 200 && response.request().method() === 'POST');
  await right.click(); const saved = await (await received).json();
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') === null && !document.querySelector('#device-confirm-button').disabled);
  return saved;
}
async function practiceCommand(id) {
  await choose(id, false);
  const received = page.waitForResponse(response => /\/api\/battle\/(start|act)$/.test(response.url()) && response.status() === 200 && response.request().method() === 'POST');
  await right.click(); return (await received).json();
}
async function geometry(label) {
  const result = await screen.evaluate(root => {
    const surface = document.querySelector('#screen-surface').getBoundingClientRect();
    const cx = surface.x + surface.width / 2, cy = surface.y + surface.height / 2, r = surface.width / 2;
    const boxes = [...root.querySelectorAll('button')].filter(element => element.getClientRects().length).map(element => ({ text: element.textContent, box: element.getBoundingClientRect() }));
    const failures = [];
    for (const { text, box } of boxes) if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > r + 2)) failures.push(`clipped ${text}`);
    for (let a = 0; a < boxes.length; a++) for (let b = a + 1; b < boxes.length; b++) {
      const x = boxes[a].box, y = boxes[b].box;
      if (Math.min(x.right, y.right) - Math.max(x.left, y.left) > 1 && Math.min(x.bottom, y.bottom) - Math.max(x.top, y.top) > 1) failures.push(`overlap ${boxes[a].text} / ${boxes[b].text}`);
    }
    return { failures, size: surface.width };
  });
  assert.deepEqual(result.failures, [], label); geometryChecks.push({ label, size: result.size });
}
async function scales(label) {
  await geometry(`${label} mobile`);
  const previous = await page.locator('.device-screen').evaluate(element => {
    const previous = element.style.cssText;
    element.style.setProperty('box-sizing', 'content-box', 'important'); element.style.setProperty('width', '201.6px', 'important'); element.style.setProperty('height', '201.6px', 'important'); element.style.setProperty('margin-inline', 'auto', 'important'); return previous;
  });
  await geometry(`${label} nominal 2.1in`);
  await page.locator('.device-screen').evaluate(element => { element.style.setProperty('width', '140.16px', 'important'); element.style.setProperty('height', '140.16px', 'important'); });
  await geometry(`${label} nominal 1.46in`);
  await page.locator('.device-screen').evaluate((element, previous) => { element.style.cssText = previous; }, previous);
}
async function photo(name) {
  if (!evidence) return;
  mkdirSync('docs/evidence', { recursive: true });
  const path = `docs/evidence/rpg-${name}.png`; await page.locator('#screen-surface').screenshot({ path }); screenshots.push(path);
}
async function grow() {
  let saved = await read();
  if (saved.state.battleMode !== 'auto') saved = await seed([{ type: 'mode', value: 1 }]);
  for (let fight = 0; fight < 180 && !saved.state.evolution.options.some(option => option.eligible); fight++) {
    // Start healthy using the actual native care result; one Rest does not
    // necessarily restore all damage from the previous encounter.
    for (let rest = 0; rest < 20 && (saved.state.hp < saved.state.combat.maxHp || saved.state.energy < 100); rest++) saved = await seed([{ type: 'rest', value: 0 }]);
    assert.equal(saved.state.hp, saved.state.combat.maxHp);
    saved = await seed([{ type: 'walk', value: 100 }, { type: 'auto', value: 0 }]);
  }
  assert.ok(saved.state.evolution.options.some(option => option.eligible), 'legal native battles must reach the next form gate');
  return saved;
}
async function preview(formId) { await menu('progression'); await choose('evolution-options'); await choose(`evolve-${formId}`); await onScreen('evolution-preview'); }

try {
  const catalog = await http('/api/starters', undefined, null);
  const identities = [];
  for (const starter of catalog.starters) {
    const pairing = await http('/api/pairing/start', {}, null);
    const claimed = await http('/api/pairing/claim', { code: pairing.code }, null);
    const credential = { deviceId: claimed.deviceId, token: claimed.token }; identity = credential;
    await seed([{ type: 'hatch', value: starter.id }]); identities.push(credential);
  }

  // Every lineage and both branches are reachable with only Next / Back / Confirm.
  for (const [index, starter] of catalog.starters.entries()) {
    await open(identities[index]); await menu('progression');
    const before = await read(), count = posts.length;
    assert.equal(await screen.getAttribute('data-xp'), String(before.state.xp));
    assert.equal(await screen.getAttribute('data-form-id'), String(before.state.formId));
    if (index === 0) { await scales('rookie progression'); await photo('rookie-progression'); }
    await choose('evolution-options');
    for (const option of before.state.evolution.options) {
      await choose(`evolve-${option.formId}`); await onScreen('evolution-preview');
      for (const key of ['maxHp', 'attack', 'defense', 'magic', 'resistance']) assert.equal(await screen.locator(`[data-stat="${key}"]`).textContent(), `${before.state.combat[key]} → ${option.combat[key]}`);
      if (index === 0) { await scales(`branch ${option.name}`); await photo(`preview-${option.formId}`); }
      await choose('evolution-requirements');
      assert.ok((await screen.textContent()).includes(`1 / ${option.requiredLevel}`));
      assert.ok((await screen.textContent()).includes(`0 / ${option.requiredBond}`));
      assert.equal(await screen.locator('[data-device-action="evolution-review"]').isDisabled(), true);
      if (index === 0) { await scales('locked requirements'); await photo('requirements-locked'); }
      await back('evolution-preview'); await choose('evolution-skills');
      for (const key of ['physical', 'heavy', 'magic']) {
        await choose(`evo-skill-${key}`, false);
        assert.ok((await screen.locator('[data-selected="true"]').textContent()).includes(`${before.state.combat.skills[key]} → ${option.combat.skills[key]}`));
      }
      await back('evolution-preview'); await back('evolution-options');
    }
    await back('progression'); await choose('evolution-tree');
    const tree = await http(`/api/evolution-graph?formId=${before.state.formId}&offset=0&limit=8`);
    await page.waitForFunction(count => document.querySelectorAll('[data-device-action^="tree-"]').length === count, tree.forms.length);
    for (const form of tree.forms) {
      await choose(`tree-${form.formId}`, false);
      assert.ok((await screen.locator('[data-selected="true"]').textContent()).includes(form.name));
    }
    await right.click(); await onScreen('evolution-node');
    if (index === 0) { await scales('lineage node'); await photo('lineage-mega'); }
    assert.equal(posts.length, count); assert.deepEqual(await read(), before, 'browsing never changes a save');
    await context.close();
  }

  identity = identities[0]; await grow(); await open(identity); await home();
  const eligible = (await read()).state.evolution.options[0];
  // Encounter and practice locks permit reference browsing but no form command.
  await menu('explore'); await gameCommand('walk'); await onScreen('wild-auto-confirm');
  await preview(eligible.formId); await choose('evolution-requirements');
  assert.match(await screen.textContent(), /Finish the wild encounter/);
  assert.equal(await screen.locator('[data-device-action="evolution-review"]').isDisabled(), true);
  await home(); await choose('wild-auto-confirm'); await gameCommand('wild-auto-start'); await home();
  await menu('battle-mode'); await choose('practice-start'); await practiceCommand('practice-tactical');
  await preview(eligible.formId); await choose('evolution-requirements');
  assert.match(await screen.textContent(), /Finish or retreat from practice/);
  assert.equal(await screen.locator('[data-device-action="evolution-review"]').isDisabled(), true);
  await menu('battle-mode'); await practiceCommand('practice-retreat'); await choose('practice-continue');

  await preview(eligible.formId); await choose('evolution-review'); await onScreen('evolution-confirm');
  const before = await read(), beforePosts = posts.length;
  await scales('evolution confirmation'); await photo('confirm'); await back('evolution-preview');
  assert.equal(posts.length, beforePosts); assert.deepEqual(await read(), before);
  await choose('evolution-review');
  let committed, release;
  await page.route('**/api/save-sync', async route => {
    const response = await route.fetch(); committed = await response.json();
    await new Promise(resolve => { release = resolve; }); await route.abort('failed');
  });
  const point = await right.boundingBox();
  await page.mouse.click(point.x + point.width / 2, point.y + point.height / 2);
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') !== null);
  await page.mouse.click(point.x + point.width / 2, point.y + point.height / 2);
  for (let tries = 0; tries < 100 && !release; tries++) await page.waitForTimeout(10);
  assert.ok(release); release();
  await onScreen('saving'); await page.waitForFunction(() => !document.querySelector('[data-device-action="retry"]')?.disabled);
  assert.equal(posts.length, beforePosts + 1);
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  assert.equal(JSON.parse(pending).subjectMemberId, 1); assert.equal(JSON.parse(pending).events[0].type, 'evolve');
  await back('saving'); assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  const storageState = await context.storageState(); await context.close();
  await new Promise(resolve => app.server.close(resolve)); app.close(); app = await startServer({ dataDir, port });
  await open(identity, storageState); await onScreen('saving');
  const retried = await gameCommand('retry'); await onScreen('evolution-result');
  assert.deepEqual(retried, committed); assert.deepEqual(await read(), { ...(await read()), state: committed.state });
  assert.equal(posts.at(-1).batchId, JSON.parse(pending).batchId);
  assert.equal(committed.state.formId, eligible.formId); assert.equal(committed.state.stage, 'Champion');
  assert.equal(committed.state.xp, before.state.xp); assert.equal(committed.state.bond, before.state.bond);
  assert.deepEqual(committed.state.collection.slice(1), before.state.collection.slice(1));
  assert.equal(committed.state.activeCreatureId, before.state.activeCreatureId);
  await scales('Champion result'); await photo('champion-result');
  await page.reload(); await home(); assert.equal((await read()).state.formId, eligible.formId);

  for (const stage of ['Ultimate', 'Mega']) {
    const ready = await grow(); await page.reload(); await home();
    const option = ready.state.evolution.options[0]; assert.equal(option.stage, stage);
    await preview(option.formId); await choose('evolution-review');
    const saved = await gameCommand('confirm-evolution'); await onScreen('evolution-result');
    assert.equal(saved.state.stage, stage); assert.equal(saved.state.formId, option.formId);
    assert.equal(saved.state.xp, ready.state.xp); assert.equal(saved.state.bond, ready.state.bond);
    assert.deepEqual(saved.state.collection.slice(1), ready.state.collection.slice(1));
    await scales(`${stage} result`); await photo(`${stage.toLowerCase()}-result`);
  }
  const mega = await read(); await choose('evolution-done');
  assert.equal(await screen.locator('[data-device-action="evolution-options"]').isDisabled(), true);
  const native = JSON.parse(execFileSync(resolve('build/digivice-core'), ['--replay-onboarding', String(mega.seed)], {
    input: mega.events.map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', timeout: 5000, maxBuffer: 65536,
  }));
  assert.deepEqual(mega.state, native);
  await menu('battle-mode'); await choose('practice-start'); await choose('practice-auto');
  const auto = await practiceCommand('practice-auto-start'); await onScreen('battle-auto-result');
  assert.equal(auto.battle.rulesVersion, 7); assert.equal(auto.battle.playerFormId, mega.state.formId);
  assert.equal(auto.battle.playerFormName, mega.state.creature); assert.equal(auto.battle.playerLevel, mega.state.level);
  assert.deepEqual(await read(), mega, 'practice uses the form but awards no care XP');
  assert.deepEqual(errors, []);
  console.log('PASS: all eight native graph entry pages and both branches; exact XP/stats/moves/requirements; two-button confirmation and Back; wild/practice locks; lost evolution ACK plus browser/service restart exact retry; no duplicate evolution; real battles to Champion/Ultimate/Mega; stable member XP/bond/other members; evolved-form Auto practice; original placeholders; zero page errors.');
  console.log(JSON.stringify({ geometryChecks, screenshots, finalForm: mega.state.creature, level: mega.state.level, xp: mega.state.xp, physicalSizing: '201.6 and 140.16 CSS px nominal at 96px/in; uncalibrated, not physical LCD readability proof' }));
} finally {
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

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
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-graph-browser-'));
let app = await startServer({ dataDir, port: 0 });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const errors = [], posts = [], geometryChecks = [], screenshots = [];
const evidence = process.env.GRAPH_EVIDENCE === '1';
let context, page, screen, left, right, identity;
const graphRequests = [];
async function http(path, body, credential = identity) {
  const response = await fetch(`${base}${path}`, { method: body === undefined ? 'GET' : 'POST',
    headers: { ...(credential ? { Authorization: `Bearer ${credential.token}` } : {}), ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  assert.ok(response.ok, `${path}: ${response.status} ${response.ok ? '' : await response.text()}`); return response.json();
}
const read = () => http('/api/save');
async function seed(events) {
  const saved = await read();
  return http('/api/save-sync', { rulesVersion: 7, baseRevision: saved.revision, batchId: crypto.randomUUID(), events });
}
async function open(credential, state) {
  identity = credential;
  context = await browser.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce', ...(state ? { storageState: state } : {}) });
  page = await context.newPage(); page.setDefaultTimeout(8000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.url().includes('/api/evolution-graph')) graphRequests.push(new URL(request.url()).searchParams); });
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
  const path = `docs/evidence/graph-${name}.png`; await page.locator('#screen-surface').screenshot({ path }); screenshots.push(path);
}
async function create(starter) {
  const pairing = await http('/api/pairing/start', {}, null);
  const claim = await http('/api/pairing/claim', { code: pairing.code }, null);
  identity = { deviceId: claim.deviceId, token: claim.token };
  await seed([{ type: 'hatch', value: starter }]); return identity;
}
async function graphPage(offset) {
  await page.waitForFunction(offset => { const el = document.querySelector('#device-ui'); return el?.dataset.screen === 'evolution-tree' && Number(el.dataset.graphOffset) === offset && Number(el.dataset.graphTotal) > 0; }, offset);
}
async function graphNode(id) {
  await page.waitForFunction(id => document.querySelector('#device-ui')?.dataset.graphNodeId === String(id) && document.querySelector('[data-device-action="evolution-links"]'), id);
}
async function grow(target) {
  let saved = await read();
  if (saved.state.battleMode !== 'auto') saved = await seed([{ type: 'mode', value: 1 }]);
  for (let fight = 0; fight < 180 && !saved.state.evolution.options.some(option => option.formId === target && option.eligible); fight++) {
    for (let rest = 0; rest < 20 && (saved.state.hp < saved.state.combat.maxHp || saved.state.energy < 100); rest++) saved = await seed([{ type: 'rest', value: 0 }]);
    saved = await seed([{ type: 'walk', value: 100 }, { type: 'auto', value: 0 }]);
  }
  assert.ok(saved.state.evolution.options.some(option => option.formId === target && option.eligible)); return saved;
}
try {
  await create(1); await open(identity); await menu('progression');
  const initial = await read(), count = posts.length;
  await choose('evolution-tree'); await graphPage(0);
  assert.equal(await screen.getAttribute('data-graph-total'), '21');
  await scales('connected graph page one'); await photo('component-page-one');
  await choose('graph-next'); await graphPage(8); await scales('connected graph page two');
  await choose('graph-next'); await graphPage(16); await scales('connected graph final page'); await photo('component-page-three');
  assert.equal(await screen.locator('[data-device-action^="tree-"]').count(), 5);
  assert.equal(await screen.locator('[data-device-action="graph-next"]').count(), 0);
  await choose('graph-previous'); await graphPage(8); await choose('graph-previous'); await graphPage(0);
  await choose('tree-15'); await graphNode(15); await scales('Devimon native preview');
  await choose('evolution-links'); await onScreen('evolution-links');
  for (const id of [11, 84, 116]) { await choose(`graph-parent-${id}`, false); await scales(`incoming parent ${id}`); }
  await photo('three-parent-junction');
  await choose('graph-parent-84'); await graphNode(84); assert.match(await screen.textContent(), /DemiDevimon/);
  await choose('evolution-links'); await choose('graph-child-15', false);
  assert.match(await screen.locator('[data-selected="true"]').textContent(), /Level 5.*Bond 20/);
  await right.click(); await graphNode(15); await back('evolution-tree'); await graphPage(0);
  await choose('graph-next'); await graphPage(8);
  const secondPage = await http('/api/evolution-graph?formId=11&offset=8&limit=8');
  assert.ok(secondPage.forms.some(form => form.formId === 69));
  await choose('tree-69'); await graphNode(69); await choose('evolution-links');
  await choose('graph-child-76', false); const babyEdge = secondPage.forms.find(form => form.formId === 69).edges.find(edge => edge.toFormId === 76);
  assert.equal(babyEdge.requiredBond, 10); assert.ok((await screen.locator('[data-selected="true"]').textContent()).includes(`Level ${babyEdge.requiredLevel} · Bond ${babyEdge.requiredBond}`));
  await scales('baby edge requires Bond 10'); await photo('edge-gates');
  await right.click(); await graphNode(76); await choose('evolution-links');
  await choose('graph-child-32', false); const nextBaby = await http('/api/roster/76'); const rookieEdge = nextBaby.form.evolution.edges.find(edge => edge.toFormId === 32);
  assert.equal(rookieEdge.requiredBond, 20); assert.equal(nextBaby.form.requiredBond, 0);
  assert.ok((await screen.locator('[data-selected="true"]').textContent()).includes(`Level ${rookieEdge.requiredLevel} · Bond ${rookieEdge.requiredBond}`));
  await back('evolution-node'); await back('evolution-tree');
  assert.equal(posts.length, count); assert.deepEqual(await read(), initial, 'all graph browsing is read-only');
  assert.ok(graphRequests.every(query => query.get('limit') === '8'), 'no full-component request');
  await home(); await menu('progression');
  await page.route('**/api/evolution-graph?**', route => route.fulfill({ status: 503, contentType: 'application/json', body: '{"error":"unavailable"}' }));
  await choose('evolution-tree'); await page.waitForFunction(() => document.querySelector('#device-ui')?.textContent.includes('graph unavailable'));
  await scales('graph unavailable'); await photo('unavailable');
  assert.deepEqual(await read(), initial);
  await page.unroute('**/api/evolution-graph?**'); await choose('reload-evolution-tree'); await graphPage(0);
  await menu('roster');
  const rosterPage = await http('/api/roster?offset=0&limit=8'); const listedId = rosterPage.entries[0].formId;
  await page.waitForFunction(id => document.querySelector(`[data-device-action="roster-form-${id}"]`), listedId);
  await choose(`roster-form-${listedId}`); await page.waitForFunction(() => document.querySelector('[data-device-action="roster-graph"]'));
  const listedDetail = await http(`/api/roster/${listedId}`);
  assert.match(await screen.textContent(), new RegExp(`Routes${listedDetail.form.evolution.children.length}`));
  await choose('roster-graph'); await graphPage(0); assert.equal(await screen.getAttribute('data-graph-focus-id'), String(listedId));
  await scales('catalog to actual graph'); await back('roster-detail');
  assert.deepEqual(await read(), initial); assert.equal(posts.length, count);
  await context.close();

  // Legal native battles earn the two gates; no snapshot/stat injection.
  await create(2); await grow(19); await seed([{ type: 'evolve', value: 19 }]); await grow(208); await open(identity);
  await menu('progression'); await choose('evolution-options'); await choose('evolve-208'); await onScreen('evolution-preview');
  const before = await read(), option = before.state.evolution.options.find(form => form.formId === 208);
  assert.equal(option.eligible, true); assert.equal(option.name, 'SkullGreymon');
  for (const key of ['maxHp', 'attack', 'defense', 'magic', 'resistance']) assert.equal(await screen.locator(`[data-stat="${key}"]`).textContent(), `${before.state.combat[key]} → ${option.combat[key]}`);
  await choose('evolution-requirements'); await scales('cross-lineage requirements');
  await choose('evolution-review'); await onScreen('evolution-confirm'); await scales('cross-lineage confirmation'); await photo('cross-lineage-confirm');
  const beforePosts = posts.length; await back('evolution-requirements'); assert.equal(posts.length, beforePosts); assert.deepEqual(await read(), before);
  await choose('evolution-review');
  let committed, release;
  await page.route('**/api/save-sync', async route => { const response = await route.fetch(); committed = await response.json(); await new Promise(resolve => { release = resolve; }); await route.abort('failed'); });
  const point = await right.boundingBox();
  await page.mouse.click(point.x + point.width / 2, point.y + point.height / 2);
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') !== null);
  await page.mouse.click(point.x + point.width / 2, point.y + point.height / 2);
  for (let tries = 0; tries < 100 && !release; tries++) await page.waitForTimeout(10);
  assert.ok(release); release();
  await onScreen('saving'); await page.waitForFunction(() => !document.querySelector('[data-device-action="retry"]')?.disabled);
  assert.equal(posts.length, beforePosts + 1);
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  assert.deepEqual(JSON.parse(pending).events, [{ type: 'evolve', value: 208 }]);
  const storageState = await context.storageState(); await context.close();
  await new Promise(resolve => app.server.close(resolve)); app.close(); app = await startServer({ dataDir, port });
  await open(identity, storageState); await onScreen('saving');
  const retried = await gameCommand('retry'); await onScreen('evolution-result');
  assert.deepEqual(retried, committed); assert.equal(posts.at(-1).batchId, JSON.parse(pending).batchId);
  assert.equal(committed.state.formId, 208); assert.equal(committed.state.species, 'skullgreymon');
  assert.equal(committed.state.activeCreatureId, before.state.activeCreatureId);
  assert.equal(committed.state.xp, before.state.xp); assert.equal(committed.state.bond, before.state.bond);
  assert.deepEqual(committed.state.collection.slice(1), before.state.collection.slice(1));
  assert.equal(committed.revision, before.revision + 1);
  await scales('cross-lineage result'); await photo('cross-lineage-result');
  await choose('evolution-done'); await choose('evolution-tree'); await graphPage(0);
  assert.equal(await screen.getAttribute('data-graph-focus-id'), '208');
  assert.equal(errors.length, 0, errors.join('\n'));
  console.log('PASS: 21-form graph over three bounded pages; Devimon three incoming routes; off-page parent/child navigation; per-edge baby Bond 10/20 gates; no browsing mutations; graph503/retry; native-earned cross-lineage SkullGreymon evolution, review Back, in-flight double-confirm prevention, lost ACK plus browser/service restart exact batch retry, stable companion/XP/bond, no duplicate evolution, zero page errors.');
  console.log(JSON.stringify({ geometryChecks, screenshots, physicalSizing: '201.6 and 140.16 CSS px nominal at 96px/in; uncalibrated, not physical LCD readability proof', graphRequests: graphRequests.length, result: { formId: committed.state.formId, species: committed.state.species, revision: committed.revision } }, null, 2));
} finally { await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true }); }

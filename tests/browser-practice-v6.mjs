// Current Tactical/Auto through the real service and two buttons, plus a genuine
// historical v5 mirror fixture. The historical practice companion is a fixture,
// not a claim that this browser raised/acquired Cannondramon. No care injection.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, writeFileSync, rmSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { legacyFixture } from './legacy-fixture.ts';
import { holdDeviceBack } from './browser-tools.mjs';
import { BATTLE_PENDING_KEY } from '../web/battle-client.js';

const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
const nativeBinaries = [corePath, battleCorePath].map(path => ({ path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex') }));
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-practice6-browser-'));
const { store, identities } = legacyFixture(); const identity = identities[0];
const frozenFile = 'tests/fixtures/practice-service-v5.json';
const frozen = JSON.parse(readFileSync(frozenFile, 'utf8')).cases.find(x => x.mode === 'auto');
assert.equal(frozen.expectedCurrent.battle.status, 'draw'); assert.equal(frozen.expectedCurrent.battle.exchanges, 40);
const battleStore = structuredClone(frozen.store); battleStore.devices[0].deviceId = identity.deviceId;
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(store));
for (const name of ['battle-store.json', 'battle-store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(battleStore));
let app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
const context = await browser.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' });
const page = await context.newPage(); page.setDefaultTimeout(12000);
const oldBody = JSON.stringify(frozen.commands[0].body);
const oldPending = JSON.stringify({ formatVersion: 1, rulesVersion: 5, deviceId: identity.deviceId, path: '/api/battle/start', body: oldBody });
await page.addInitScript(({ identity, oldPending, key }) => {
  if (location.protocol !== 'http:') return;
  if (!localStorage.getItem('practice6-test-fixture-loaded')) {
    localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity));
    localStorage.setItem(key, oldPending); localStorage.setItem('practice6-test-fixture-loaded', '1');
  }
}, { identity, oldPending, key: BATTLE_PENDING_KEY });
await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
const screen = page.locator('#device-ui'), left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
const posts = [], errors = [], geometryChecks = [], screenshots = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (request.method() === 'POST' && /\/api\/(battle\/(start|act)|save-sync)$/.test(request.url())) posts.push({ path: new URL(request.url()).pathname, body: request.postData() }); });
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
async function choose(id, execute = true) {
  for (let count = 0; count < 32; count++) {
    if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) { if (execute) await right.click(); return; }
    await left.click();
  }
  throw new Error(`Unreachable ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function read(path) { const r = await fetch(base + path, { headers: { Authorization: `Bearer ${identity.token}` } }); assert.equal(r.status, 200); return r.json(); }
async function command(id, endpoint = '/api/battle/act') {
  await choose(id, false); const pending = page.waitForResponse(r => r.url().endsWith(endpoint) && r.request().method() === 'POST' && r.status() === 200);
  await right.click(); const result = await (await pending).json();
  await page.waitForFunction(() => !document.querySelector('#device-confirm-button').disabled); return result;
}
async function openPractice() { await onScreen('home'); await choose('menu'); await choose('battle-mode'); }
async function evidence(label) {
  const oldStyle = await page.locator('.device-screen').evaluate(e => e.style.cssText);
  for (const width of [null, 201.6, 140.16]) {
    if (width !== null) await page.locator('.device-screen').evaluate((e, width) => { e.style.setProperty('box-sizing', 'content-box', 'important'); e.style.setProperty('width', `${width}px`, 'important'); e.style.setProperty('height', `${width}px`, 'important'); e.style.setProperty('margin-inline', 'auto', 'important'); }, width);
    const check = await screen.evaluate(root => {
      const r = document.querySelector('#screen-surface').getBoundingClientRect(), x = r.x + r.width / 2, y = r.y + r.height / 2;
      const boxes = [...root.querySelectorAll('button,.screen-detail')].filter(e => e.getClientRects().length).map(e => ({ label: e.textContent, box: e.getBoundingClientRect() })); const failures = [];
      for (const { label, box } of boxes) if ([[box.left,box.top],[box.right,box.top],[box.left,box.bottom],[box.right,box.bottom]].some(([a,b]) => Math.hypot(a-x,b-y) > r.width/2+2)) failures.push(`clipped:${label}`);
      for (let a=0;a<boxes.length;a++) for(let b=a+1;b<boxes.length;b++){const p=boxes[a].box,q=boxes[b].box;if(Math.min(p.right,q.right)-Math.max(p.left,q.left)>1&&Math.min(p.bottom,q.bottom)-Math.max(p.top,q.top)>1)failures.push(`overlap:${boxes[a].label}/${boxes[b].label}`);}
      return { width: r.width, failures };
    });
    assert.deepEqual(check.failures, [], label); geometryChecks.push({ label, width: check.width });
    if (width === null && process.env.PRACTICE6_EVIDENCE === '1') { const path = `docs/evidence/practice6-${label}.png`; await page.locator('#screen-surface').screenshot({ path }); screenshots.push({ path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex'), privateCharacterArtwork: false }); }
  }
  await page.locator('.device-screen').evaluate((e, css) => { e.style.cssText=css; }, oldStyle);
}
try {
  mkdirSync('docs/evidence', { recursive: true });
  await page.goto(`${base}/?controls=buttons`); await onScreen('battle-resolve'); await page.waitForFunction(() => document.querySelector('[data-device-action="practice-retry"]') && !document.querySelector('#device-confirm-button').disabled); const pet = await read('/api/save');
  assert.deepEqual(await read('/api/battle'), frozen.expectedCurrent);
  assert.equal(posts.length, 0); assert.equal(await page.evaluate(key => localStorage.getItem(key), BATTLE_PENDING_KEY), oldPending);
  const restored = await command('practice-retry', '/api/battle/start'); await onScreen('battle-auto-result');
  assert.deepEqual(restored, frozen.expectedCurrent); assert.equal(posts[0].body, oldBody); assert.match(await screen.innerText(), /An even match\./);
  assert.equal(await page.evaluate(key => localStorage.getItem(key), BATTLE_PENDING_KEY), null);
  await evidence('frozen5-draw'); await choose('practice-auto-done');
  await choose('practice-start'); await onScreen('battle-select-mode'); const before = posts.length;
  await holdDeviceBack(page); assert.equal(posts.length, before); await choose('practice-start');
  const tactical = await command('practice-tactical', '/api/battle/start'); await onScreen('battle-choice');
  assert.equal(tactical.battle.rulesVersion, 6); assert.equal(tactical.battle.maxExchanges, 40);
  for (const action of ['physical','heavy','magic']) { await choose(`practice-${action}`, false); assert.ok((await screen.locator('[data-selected="true"]').textContent()).includes(tactical.battle.playerCombat.skills[action])); }
  const hit = await command('practice-magic'); await onScreen('battle-result'); assert.ok((await screen.innerText()).includes(hit.battle.playerCombat.skills.magic));
  await evidence('tactical-result'); await choose('practice-continue'); await onScreen('battle-choice');
  const defend = await command('practice-ward'); await onScreen('battle-result'); assert.ok((await screen.innerText()).includes(defend.battle.enemyCombat.skills[defend.battle.lastTurn.enemyChoice]));
  await choose('practice-continue'); await holdDeviceBack(page); await command('practice-retreat'); await onScreen('battle-result'); await choose('practice-continue');
  await choose('practice-start'); await choose('practice-auto'); await onScreen('battle-auto-confirm');
  const beforeAuto = posts.length; let committed;
  await page.route('**/api/battle/start', async route => { const upstream = await route.fetch(); committed = await upstream.json(); await new Promise(resolve => setTimeout(resolve, 150)); await route.abort(); }, { times: 1 });
  await choose('practice-auto-start'); await onScreen('battle-resolve');
  await page.waitForFunction(() => document.querySelector('[data-device-action="practice-retry"]') && !document.querySelector('#device-confirm-button').disabled);
  assert.equal(posts.length, beforeAuto + 1); assert.equal(committed.battle.rulesVersion, 6); assert.equal(committed.battle.phase, 'finished');
  const pending = await page.evaluate(key => localStorage.getItem(key), BATTLE_PENDING_KEY); assert.equal(JSON.parse(pending).rulesVersion, 6);
  await page.goto('about:blank'); await new Promise(resolve => app.server.close(resolve)); app.close();
  app = await startServer({ dataDir, port, corePath, battleCorePath }); await page.goto(`${base}/?controls=buttons`); await onScreen('battle-resolve'); await page.waitForFunction(() => document.querySelector('[data-device-action="practice-retry"]') && !document.querySelector('#device-confirm-button').disabled);
  assert.equal(posts.length, beforeAuto + 1); assert.equal(await page.evaluate(key => localStorage.getItem(key), BATTLE_PENDING_KEY), pending);
  assert.deepEqual(await read('/api/battle'), committed);
  const retry = await command('practice-retry', '/api/battle/start'); await onScreen('battle-auto-result');
  assert.deepEqual(retry, committed); assert.equal(posts.at(-1).body, posts.at(-2).body); assert.equal(await page.evaluate(key => localStorage.getItem(key), BATTLE_PENDING_KEY), null);
  await evidence('auto-result'); assert.deepEqual(await read('/api/save'), pet); assert.equal(posts.filter(p => p.path === '/api/save-sync').length, 0); assert.deepEqual(errors, []);
  for (const binary of nativeBinaries) assert.equal(createHash('sha256').update(readFileSync(binary.path)).digest('hex'), binary.sha256, 'Native build changed during browser verification');
  const report = { result: 'PASS', practiceRules: 6, nativeBinaries, relatedEvidence: { clientCompatibility: 'practice6-web-client-tests.txt', browserOutput: 'practice6-browser-tests.txt' }, currentCompanion: { name: pet.state.creature, formId: pet.state.formId, level: pet.state.level },
    historicalFixture: { path: frozenFile, sourceCommit: 'c830072e5244462c0021678b02ff96a1c644e75f', companion: frozen.companion, rules: 5, exchanges: 40, status: 'draw', exactResponseAndRetryPreserved: true },
    tactical: { namedAttack: hit.battle.playerCombat.skills.magic, revealedEnemyAttack: defend.battle.enemyCombat.skills[defend.battle.lastTurn.enemyChoice], maxExchanges: 40 },
    auto: { status: retry.battle.status, exchanges: retry.battle.exchanges, maxExchanges: retry.battle.maxExchanges, lostReplyServiceAndBrowserRestartExactRetry: true },
    careStateUnchanged: true, twoButtonsOnly: true, geometryChecks, screenshots, pageErrors: 0,
    boundary: 'Current practice starts use the real selected Mote companion. Cannondramon is a genuine historical service fixture used only for unchanged v5 restore/retry. New v6 mirror balance is tested separately by native tests. CSS sizes are nominal, not physical LCD measurements.' };
  writeFileSync('docs/evidence/practice6-browser-verification.json', JSON.stringify(report, null, 2)+'\n'); console.log(JSON.stringify(report, null, 2));
} finally { await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true }); }

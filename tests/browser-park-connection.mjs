// Real baseline service/native core, fresh isolated identity, and two physical
// browser buttons. Network faults affect only this temporary browser context.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, mkdtempSync, readFileSync, writeFileSync, rmSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { holdDeviceBack } from './browser-tools.mjs';

const rootDir = resolve(process.env.DIGIVICE_TEST_ROOT || '.');
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/final-auto/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/final-auto/digivice-battle');
const expectFixed = process.env.PARK_CONNECTION_EXPECT_FIXED === '1';
const output = process.env.PARK_CONNECTION_EVIDENCE || (expectFixed ? 'docs/evidence/park-fixed-connection.json' : 'docs/evidence/park-baseline-connection.json');
const sourceCommit = process.env.DIGIVICE_TEST_SOURCE_COMMIT || 'e6f352300ee6c049b7e21598009db4cf52cca0a6';
const { startServer } = await import(pathToFileURL(join(rootDir, 'service/server.ts')).href);
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const identityKey = 'digivice.dev.identity.v1', pendingKey = 'digivice.dev.pending.v1';
const binaries = [corePath, battleCorePath].map(path => ({ path, bytes: readFileSync(path).length, sha256: hash(readFileSync(path)) }));
const sourceFiles = ['web/app.js', 'web/care-actions.js', 'web/device-screen.js', 'web/styles.css', 'service/server.ts'].filter(path => existsSync(join(rootDir, path))).map(path => ({ path, sha256: hash(readFileSync(join(rootDir, path))) }));
const dirs = [], apps = [], errors = [], findings = [], checks = [], careSteps = [], geometryChecks = [], screenshots = [];
const temporaryDirectory = () => { const path = mkdtempSync(join(tmpdir(), 'digivice-park-connection-')); dirs.push(path); return path; };
async function service(dataDir, port = 0) { const app = await startServer({ rootDir, dataDir, port, corePath, battleCorePath }); apps.push(app); return app; }
async function stop(app) { if (app.server.listening) await new Promise(resolve => app.server.close(resolve)); app.close(); }
const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
async function client(base, identity) {
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' });
  if (identity) await context.addInitScript(({ identityKey, identity }) => { if (location.protocol === 'http:' && !localStorage.getItem(identityKey)) localStorage.setItem(identityKey, JSON.stringify(identity)); }, { identityKey, identity });
  const page = await context.newPage(); page.setDefaultTimeout(10000);
  page.on('pageerror', error => errors.push(error.message));
  await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
  const posts = [];
  page.on('request', request => { if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) posts.push(request.postData()); });
  const screen = page.locator('#device-ui'), left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
  const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
  const ready = () => page.waitForFunction(() => !document.querySelector('#device-confirm-button').disabled);
  async function choose(id, execute = true) {
    await screen.locator(`[data-device-action="${id}"]`).waitFor({ state: 'attached' });
    for (let n = 0; n < 32; n++) {
      if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) {
        if (execute) { await ready(); await right.click(); } return;
      }
      await left.click();
    }
    throw new Error(`Two-button navigation cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
  }
  async function command(id, path = '/api/save-sync', status = 200) {
    await choose(id, false); const response = page.waitForResponse(r => r.url().endsWith(path) && r.request().method() === 'POST' && r.status() === status);
    await right.click(); return (await response).json();
  }
  async function home() { for (let n = 0; n < 15 && await screen.getAttribute('data-screen') !== 'home'; n++) { if (await screen.getAttribute('data-screen') === 'wild-auto-result') await choose('wild-auto-done'); else await holdDeviceBack(page); } await onScreen('home'); }
  async function menu(id) { await home(); await choose('menu'); await choose(id); await onScreen(id); }
  const stored = key => page.evaluate(key => localStorage.getItem(key), key);
  const snapshot = () => screen.evaluate(root => ({ screen: root.dataset.screen, title: root.querySelector('#device-screen-title')?.textContent || '', detail: root.querySelector('.screen-detail')?.textContent || '', actions: [...root.querySelectorAll('[data-device-action]')].map(button => ({ id: button.dataset.deviceAction, label: button.textContent.trim(), disabled: button.disabled })) }));
  const fault = route => route.fulfill({ status: 503, contentType: 'application/json', body: '{"error":"test_service_unavailable","message":"The isolated local service is temporarily unavailable."}' });
  return { context, page, posts, screen, right, onScreen, ready, choose, command, home, menu, stored, snapshot, fault, base };
}
async function read(base, identity) { const response = await fetch(`${base}/api/save`, { headers: { Authorization: `Bearer ${identity.token}` } }); assert.equal(response.status, 200); return response.json(); }
async function recoveryGeometry(client, label) {
  const frame = client.page.locator('.device-screen');
  const previous = await frame.evaluate(element => element.style.cssText);
  try {
    for (const requestedWidth of [306, 201.6, 140.16]) {
      await frame.evaluate((element, width) => { element.style.setProperty('box-sizing', 'content-box', 'important'); element.style.setProperty('width', `${width}px`, 'important'); element.style.setProperty('height', `${width}px`, 'important'); element.style.setProperty('margin-inline', 'auto', 'important'); }, requestedWidth);
      const observed = await client.screen.evaluate(root => {
        const surface = document.querySelector('#screen-surface').getBoundingClientRect();
        const cx = surface.x + surface.width / 2, cy = surface.y + surface.height / 2;
        const visible = element => element.getClientRects().length && getComputedStyle(element).visibility !== 'hidden';
        const nodes = [...root.querySelectorAll('.screen-detail,.screen-stats,.screen-portrait,.screen-choice:not([hidden]),.screen-footer')].filter(visible).map(element => ({ text: element.textContent.trim() || element.className, box: element.getBoundingClientRect() }));
        // The heading is a rounded pill: its enclosing square can extend beyond
        // the circle without clipping painted text. Measure its text itself.
        for (const element of [...root.querySelectorAll('.screen-heading h2,.screen-heading>span')].filter(visible)) {
          const text = document.createRange(); text.selectNodeContents(element);
          nodes.push({ text: element.textContent.trim(), box: text.getBoundingClientRect() });
        }
        const failures = [];
        const overlap = (a, b) => Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 1;
        for (const { text, box } of nodes) if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > surface.width / 2 + 2)) failures.push(`outside screen circle: ${text}`);
        for (let a = 0; a < nodes.length; a++) for (let b = a + 1; b < nodes.length; b++) if (overlap(nodes[a].box, nodes[b].box)) failures.push(`screen blocks overlap: ${nodes[a].text}/${nodes[b].text}`);
        for (const button of [...root.querySelectorAll('.screen-choice:not([hidden])')].filter(visible)) {
          const outer = button.getBoundingClientRect();
          const contents = [...button.querySelectorAll('.screen-choice-icon,.screen-choice-label,small')].filter(visible).map(element => ({ text: element.textContent, box: element.getBoundingClientRect() }));
          for (const { text, box } of contents) if (box.left < outer.left - 1 || box.right > outer.right + 1 || box.top < outer.top - 1 || box.bottom > outer.bottom + 1) failures.push(`action content outside card: ${text}`);
          for (let a = 0; a < contents.length; a++) for (let b = a + 1; b < contents.length; b++) if (overlap(contents[a].box, contents[b].box)) failures.push(`action text overlaps: ${contents[a].text}/${contents[b].text}`);
        }
        const canvas = document.querySelector('#display');
        return { width: surface.width, height: surface.height, canvas: [canvas.width, canvas.height], selected: root.querySelector('[data-selected="true"]')?.dataset.deviceAction, measuredBlocks: nodes.map(({ text }) => text), failures };
      });
      geometryChecks.push({ screen: label, requestedWidth, ...observed });
      if (requestedWidth === 306 && process.env.PARK_CONNECTION_SCREENSHOTS === '1') {
        const path = `docs/evidence/park-recover-${label}.png`; mkdirSync('docs/evidence', { recursive: true });
        await client.page.locator('#screen-surface').screenshot({ path }); screenshots.push({ path, sha256: hash(readFileSync(path)), privateArtwork: false, width: observed.width });
      }
      assert.ok(Math.abs(observed.width - requestedWidth) < 1, 'Measure the actual screen surface, not its outer bezel');
      assert.deepEqual(observed.canvas, [480, 480]);
      assert.ok(await client.page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    }
  } finally { await frame.evaluate((element, css) => { element.style.cssText = css; }, previous); }
}
let result = 'FAIL', failure;
try {
  const dataDir = temporaryDirectory(); let app = await service(dataDir);
  const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
  const c = await client(base), { page } = c;
  await page.route('**/api/**', c.fault);
  await page.goto(`${base}/?controls=buttons`); await c.onScreen('home'); await c.choose('connection'); await c.onScreen('connection');
  await c.choose('reconnect', false); await c.ready();
  const unavailable = await c.snapshot();
  assert.equal(unavailable.title, 'Service unavailable');
  assert.equal(unavailable.actions.find(action => action.id === 'start-pairing').disabled, true);
  assert.equal(await c.stored(identityKey), null); assert.equal(c.posts.length, 0);
  checks.push({ name: 'Fresh unpaired boot with API 503', identityCreated: false, gameCommands: 0, reconnectReachableWithTwoButtons: true });
  await page.unroute('**/api/**', c.fault); await c.choose('reconnect');
  await page.waitForFunction(() => document.querySelector('#device-screen-title')?.textContent === 'Service connected');

  await page.route('**/api/pairing/start', c.fault, { times: 1 });
  await c.command('start-pairing', '/api/pairing/start', 503); await c.ready();
  const pairingFailure = await c.snapshot();
  assert.equal(await c.stored(identityKey), null);
  if (expectFixed) {
    assert.notEqual(pairingFailure.title, 'Service connected', 'Failed pairing should not describe the connection as healthy');
    checks.push({ name: 'Failed pairing status describes the error', observed: pairingFailure });
  }
  if (pairingFailure.title === 'Service connected') findings.push({ id: 'connection-503-status', kind: 'misleading-status', scenario: 'Pairing returns HTTP 503 after a successful health check', observed: pairingFailure, expectation: 'Service status should distinguish a failed service request from usable game connectivity; retry remains available.' });
  if (pairingFailure.actions.find(action => action.id === 'start-pairing')?.disabled) {
    await c.choose('reconnect');
    await page.waitForFunction(() => document.querySelector('[data-device-action="start-pairing"]')?.disabled === false);
  }
  await c.command('start-pairing', '/api/pairing/start', 201);
  await page.route('**/api/pairing/claim', c.fault, { times: 1 });
  await c.command('claim-device', '/api/pairing/claim', 503); await c.ready();
  assert.equal(await c.stored(identityKey), null);
  const identity = await c.command('claim-device', '/api/pairing/claim', 201); await c.onScreen('starter-select');
  const rawIdentity = await c.stored(identityKey), egg = await read(base, identity);
  assert.equal(egg.state.phase, 'egg'); assert.equal(egg.revision, 0); assert.equal(c.posts.length, 0);
  checks.push({ name: 'Pairing start and claim each retry after API 503', freshEgg: true, careRevision: 0, duplicateGameCommands: 0 });
  await c.choose('starter-2'); await c.onScreen('starter-review'); await holdDeviceBack(page); await c.onScreen('starter-select'); assert.equal(c.posts.length, 0);
  await c.choose('starter-2'); await c.command('hatch-starter'); await c.onScreen('starter-hatched'); await c.choose('meet-starter'); await c.onScreen('home');
  const hatched = await read(base, identity); assert.equal(hatched.state.creature, 'Agumon'); assert.equal(hatched.revision, 1);
  checks.push({ name: 'Egg review Back and acknowledged hatch', reviewCommands: 0, founder: 'Agumon', hatchCommands: 1 });

  // A 503 cannot commit. The exact pending command remains recoverable, while
  // a subsequent lost acknowledgment proves the genuinely committed branch.
  await c.menu('care'); await page.route('**/api/save-sync', c.fault, { times: 1 });
  await c.command('play', '/api/save-sync', 503); await c.onScreen('saving'); await c.ready();
  const failedPending = await c.stored(pendingKey); assert.ok(failedPending);
  assert.ok(await c.stored(identityKey) === rawIdentity, 'Browser identity must remain byte-for-byte unchanged'); assert.deepEqual(await read(base, identity), hatched);
  await holdDeviceBack(page); assert.equal(await c.stored(pendingKey), failedPending); await c.onScreen('saving');
  await c.command('retry'); await c.onScreen('care');
  assert.equal(c.posts.at(-1), c.posts.at(-2)); assert.equal(await c.stored(pendingKey), null);
  checks.push({ name: 'Loaded paired action receives API 503', identityPreserved: true, pendingPreserved: true, backCannotDiscard: true, sameRequestRetried: true, serviceRevisionUnchangedBeforeRetry: true });
  const beforeLost = await read(base, identity); let committed;
  await page.route('**/api/save-sync', async route => { const response = await route.fetch(); committed = await response.json(); await route.abort('failed'); }, { times: 1 });
  await c.choose('rest'); await c.onScreen('saving'); await c.ready();
  const lostPending = await c.stored(pendingKey), lostBody = c.posts.at(-1), commandCount = c.posts.length;
  assert.ok(lostPending); assert.equal(committed.revision, beforeLost.revision + 1);
  const committedSave = await read(base, identity);
  assert.deepEqual(committedSave.state, committed.state);
  assert.ok(await c.stored(identityKey) === rawIdentity, 'Browser identity must remain byte-for-byte unchanged');
  await page.goto('about:blank'); await stop(app); app = await service(dataDir, port);
  // Shell assets still load. Only API calls are unavailable during reload;
  // this is not a claim that an entirely offline browser can boot the app.
  await page.route('**/api/**', c.fault); await page.goto(`${base}/?controls=buttons`); await c.onScreen('saving'); await c.ready();
  assert.ok(await c.stored(identityKey) === rawIdentity, 'Browser identity must remain byte-for-byte unchanged'); assert.equal(await c.stored(pendingKey), lostPending); assert.equal(c.posts.length, commandCount);
  await holdDeviceBack(page); assert.equal(await c.stored(pendingKey), lostPending);
  await page.unroute('**/api/**', c.fault); const retried = await c.command('retry');
  assert.deepEqual(retried, committed); assert.equal(c.posts.at(-1), lostBody);
  await page.waitForFunction(key => localStorage.getItem(key) === null, pendingKey); await c.onScreen('home');
  assert.deepEqual(await read(base, identity), committedSave); assert.ok(await c.stored(identityKey) === rawIdentity, 'Browser identity must remain byte-for-byte unchanged');
  checks.push({ name: 'Committed lost ACK plus service restart and API-unavailable browser reload', identityPreserved: true, exactPendingBytesPreserved: true, reloadCommands: 0, retryEqualsOriginalReceipt: true, duplicateAppliedEvents: 0 });

  // Reload with no pending request highlights the paired-but-not-yet-loaded
  // first screen. Its actual physical controls must lead back to the same save.
  await page.route('**/api/**', c.fault); await page.reload(); await c.onScreen('home'); await c.choose('connection'); await c.ready();
  assert.ok(await c.stored(identityKey) === rawIdentity, 'Browser identity must remain byte-for-byte unchanged');
  const pairedUnavailable = await c.snapshot(); assert.equal(pairedUnavailable.actions[0].id, 'reconnect');
  await holdDeviceBack(page); await c.onScreen('home'); const unavailableHome = await c.snapshot();
  if (expectFixed) {
    assert.equal(unavailableHome.title, 'Save not loaded');
    assert.equal(unavailableHome.actions.find(action => action.id === 'connection').label, 'Restore saved game');
    checks.push({ name: 'Paired unloaded home describes restoring the existing save', observed: unavailableHome });
  }
  if (unavailableHome.actions.some(action => action.id === 'connection' && action.label === 'Set up device')) findings.push({ id: 'paired-offline-setup-label', kind: 'setup-friction', scenario: 'Existing paired browser reloads while API is unavailable, with no pending command', observed: unavailableHome, expectation: 'Home should describe restoring the existing paired save, rather than offering Set up device for an already paired identity.' });
  await c.choose('connection'); await page.unroute('**/api/**', c.fault); await c.choose('reconnect'); await c.ready(); await c.home();
  assert.deepEqual(await read(base, identity), committedSave); assert.ok(await c.stored(identityKey) === rawIdentity, 'Browser identity must remain byte-for-byte unchanged');
  checks.push({ name: 'Paired reload with API unavailable and no pending request', sameIdentityRestored: true, sameSaveRestored: true, automaticGameCommands: 0 });
  await c.context.close(); await stop(app);

  // This fixture is a real baseline-native Auto result, not fabricated HP.
  const fixturePath = resolve('tests/fixtures/park-baseline-service.json');
  const fixture = JSON.parse(readFileSync(fixturePath));
  const careData = temporaryDirectory();
  for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(careData, name), JSON.stringify(fixture.care.checkpoints.autoResult.store));
  let careApp = await service(careData);
  const carePort = careApp.server.address().port, careBase = `http://127.0.0.1:${carePort}`;
  const careIdentity = { deviceId: fixture.care.deviceId, token: Buffer.alloc(32, 88).toString('base64url') };
  const p = await client(careBase, careIdentity); await p.page.goto(`${careBase}/?controls=buttons`); await p.onScreen('wild-auto-result'); await p.choose('wild-auto-done'); await p.menu('care');
  let current = await read(careBase, careIdentity); const careBefore = { hp: current.state.hp, maxHp: current.state.combat.maxHp, energy: current.state.energy, revision: current.revision };
  if (expectFixed) {
    assert.equal(current.state.schemaVersion, 14); assert.equal(current.state.rulesVersion, 11);
    const count = current.state.recoveryRestCount, beforeRecovery = structuredClone(current);
    assert.ok(Number.isInteger(count) && count > 1 && count <= 40, 'The real damaged fixture needs multiple native-counted ordinary rests');
    const expectedEvents = Array.from({ length: count }, () => ({ type: 'rest', value: 0 }));
    await p.choose('recover-review', false); await recoveryGeometry(p, 'care');
    await p.choose('recover-review'); await p.onScreen('recover-confirm');
    assert.ok((await p.snapshot()).detail.includes(`${count} ordinary rests`));
    await recoveryGeometry(p, 'confirm');
    assert.deepEqual(geometryChecks.flatMap(check => check.failures.map(failure => `${check.screen} ${check.width}px: ${failure}`)), [], 'Recovery screens fit the actual circle with no overlapping controls');
    await holdDeviceBack(p.page); await p.onScreen('care');
    assert.equal(p.posts.length, 0); assert.deepEqual(await read(careBase, careIdentity), beforeRecovery);
    await p.choose('recover-review'); await p.onScreen('recover-confirm'); await p.choose('confirm-recovery', false);
    let releaseResponse, savedRecovery;
    const holdResponse = new Promise(resolve => { releaseResponse = resolve; });
    let commitDone;
    const commit = new Promise(resolve => { commitDone = resolve; });
    await p.page.route('**/api/save-sync', async route => {
      const response = await route.fetch(); savedRecovery = await response.json(); commitDone();
      await holdResponse; await route.abort('failed');
    }, { times: 1 });
    try {
      await p.right.dblclick({ delay: 25 }); await commit; await p.onScreen('saving');
      assert.equal(p.posts.length, 1, 'Double confirm while the response is held sends one recovery batch');
      assert.equal(await p.right.isDisabled(), true);
      await holdDeviceBack(p.page); await p.onScreen('saving'); assert.equal(p.posts.length, 1);
    } finally { releaseResponse(); }
    await p.ready();
    const recoveryPending = await p.stored(pendingKey), recoveryBody = p.posts[0];
    const pending = JSON.parse(recoveryPending), body = JSON.parse(recoveryBody);
    assert.deepEqual(pending.events, expectedEvents); assert.deepEqual(body.events, expectedEvents);
    assert.equal(pending.rulesVersion, 11); assert.equal(body.rulesVersion, 11); assert.equal(body.baseRevision, beforeRecovery.revision);
    assert.equal(savedRecovery.revision, beforeRecovery.revision + 1);
    assert.equal(savedRecovery.state.sequence, beforeRecovery.state.sequence + count);
    assert.equal(savedRecovery.state.hp, savedRecovery.state.combat.maxHp); assert.equal(savedRecovery.state.energy, 100);
    assert.equal(savedRecovery.state.recoveryRestCount, 0);
    assert.equal(savedRecovery.state.activeCreatureId, beforeRecovery.state.activeCreatureId);
    const progression = state => state.collection.map(({ id, species, name, formId, xp, level, capturedAtSequence }) => ({ id, species, name, formId, xp, level, capturedAtSequence }));
    assert.deepEqual(progression(savedRecovery.state), progression(beforeRecovery.state));
    assert.deepEqual(savedRecovery.state.collection.filter(member => member.id !== beforeRecovery.state.activeCreatureId), beforeRecovery.state.collection.filter(member => member.id !== beforeRecovery.state.activeCreatureId));
    const savedFullRecovery = await read(careBase, careIdentity);
    const identityBeforeRecoveryRestart = await p.stored(identityKey);
    await p.page.goto('about:blank'); await stop(careApp); careApp = await service(careData, carePort);
    await p.page.goto(`${careBase}/?controls=buttons`); await p.onScreen('saving'); await p.ready();
    assert.equal(p.posts.length, 1, 'Restart does not automatically resend a recovery');
    assert.equal(await p.stored(pendingKey), recoveryPending); assert.ok(await p.stored(identityKey) === identityBeforeRecoveryRestart, 'Recovery restart must preserve the browser identity');
    const exactRetry = await p.command('retry');
    assert.deepEqual(exactRetry, savedRecovery); assert.equal(p.posts[1], recoveryBody);
    await p.page.waitForFunction(key => localStorage.getItem(key) === null, pendingKey);
    await p.home(); await p.menu('care');
    assert.equal(await p.screen.locator('[data-device-action="recover-review"]').isDisabled(), true);
    assert.ok((await p.snapshot()).actions.some(action => action.id === 'recover-review' && action.disabled));
    await holdDeviceBack(p.page); await p.home(); await p.page.reload(); await p.onScreen('home'); await p.menu('care');
    assert.equal(await p.screen.locator('[data-device-action="recover-review"]').isDisabled(), true);
    assert.equal(p.posts.length, 2, 'Full recovery has only the original batch and its exact retry, with no extra automatic actions');
    current = await read(careBase, careIdentity); assert.deepEqual(current, savedFullRecovery);
    checks.push({ name: 'Recover fully uses one reviewed native-counted ordinary-Rest batch', fixture: 'tests/fixtures/park-baseline-service.json', fixtureSha256: hash(readFileSync(fixturePath)), before: careBefore, after: { hp: current.state.hp, maxHp: current.state.combat.maxHp, energy: current.state.energy, revision: current.revision }, nativeRestCount: count, exactOrdinaryRestEvents: body.events, reviewBackGameCommands: 0, inFlightDoubleConfirmGameCommands: 1, totalHttpCommandsIncludingExactRetry: 2, atomicRevisionIncrease: 1, sequenceIncrease: count, xpAndMemberIdentityUnchanged: true, otherMembersUnchanged: true, lostAckRestartPendingPreserved: true, exactReceiptAndBodyRetry: true, disabledWhenAlreadyFull: true, extraAutomaticActions: 0 });
  } else while (current.state.hp < current.state.combat.maxHp || current.state.energy < 100) {
    assert.ok(careSteps.length < 25, 'Ordinary rest should finish within a bounded number of confirmations');
    const before = current; current = await p.command('rest'); await p.onScreen('care'); await p.ready();
    careSteps.push({ confirm: careSteps.length + 1, hpBefore: before.state.hp, hpAfter: current.state.hp, energyBefore: before.state.energy, energyAfter: current.state.energy, hpGain: current.state.hp - before.state.hp, energyGain: current.state.energy - before.state.energy, ordinaryRestEvents: JSON.parse(p.posts.at(-1)).events.length });
    assert.deepEqual(JSON.parse(p.posts.at(-1)).events, [{ type: 'rest', value: 0 }]);
  }
  const careAfter = { hp: current.state.hp, maxHp: current.state.combat.maxHp, energy: current.state.energy, revision: current.revision };
  if (!expectFixed) checks.push({ name: 'Actual damaged Auto-result companion, repeated ordinary Rest via two buttons', fixture: 'tests/fixtures/park-baseline-service.json', fixtureSha256: hash(readFileSync(fixturePath)), before: careBefore, after: careAfter, confirmations: careSteps.length, steps: careSteps });
  if (careSteps.length > 1) findings.push({ id: 'repeated-rest-friction', kind: 'care-friction', scenario: 'Recover the real native Auto result to full HP and 100 energy', before: careBefore, after: careAfter, restConfirmations: careSteps.length, note: 'Each confirmation sends exactly one existing ordinary Rest event. No alternative healing rule or Recover fully control was implemented.' });
  assert.deepEqual(errors, []);
  for (const binary of binaries) assert.equal(hash(readFileSync(binary.path)), binary.sha256, 'Selected native binary changed during run');
  for (const file of sourceFiles) assert.equal(hash(readFileSync(join(rootDir, file.path))), file.sha256, 'Frozen source changed during run');
  result = 'PASS';
} catch (error) { failure = { message: error.message, stack: error.stack }; throw error; }
finally {
  const report = { result, mode: expectFixed ? 'fixed-regression' : 'baseline', sourceCommit, sourceBasis: expectFixed ? 'Working tree changes based on sourceCommit; source and binary hashes identify the tested checkpoint.' : 'Frozen sourceCommit baseline.', rootDir, binaries, sourceFiles, twoPhysicalButtonsOnly: true, isolatedPortAndStores: true, privateArtworkLoaded: false, checks, findings, geometryChecks, screenshots, pageErrors: errors, ...(failure ? { failure } : {}), limits: 'API failure simulations leave shell assets reachable. Browser gameplay requires the local service; native device offline play is a separate path. Public fixed test identity is used only for the genuine damaged-save fixture. Screen dimensions are nominal CSS sizes, not calibrated physical LCD readability. No live preview or saved user identity was touched.' };
  mkdirSync(resolve(output, '..'), { recursive: true }); writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
  await browser.close(); for (const app of apps) await stop(app); for (const path of dirs) rmSync(path, { recursive: true, force: true });
}

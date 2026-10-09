// Actual on-screen touches against the native core. Legal native histories only
// accelerate fixture setup; their API commands are reported separately from UI.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { autoCheckpoint, resumeFighting, exploreEncounter } from './manual-auto-browser-tools.mjs';
import { touchDevice } from './touch-browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/park/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/park/digivice-battle');
const hash = data => createHash('sha256').update(data).digest('hex');
const sourcePaths = ['web/app.js', 'web/device-screen.js', 'web/styles.css', 'tests/browser-touch-gameplay.mjs', 'tests/touch-browser-tools.mjs', 'web/capture-gesture.js', 'web/capture-trajectory.js', 'core/game.cpp', 'core/game.hpp', 'core/cli.cpp', 'service/server.ts'];
const sourceFiles = () => sourcePaths.map(path => ({ path, sha256: hash(readFileSync(path)) }));
const initialSources = sourceFiles();
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-touch-gameplay-'));
const app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 1000, height: 1000 }, hasTouch: true, reducedMotion: 'reduce' });
const page = await context.newPage(); page.setDefaultTimeout(10000);
const cdp = await context.newCDPSession(page);
const ui = touchDevice(page), errors = [], posts = [], checks = [], fixtures = [], inputSessions = [], assertions = [];
let identity, failure, outcome = 'FAIL';
const event = (type, value = 0) => ({ type, value });
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) {
    const body = request.postDataJSON(); posts.push({ events: body.events, baseRevision: body.baseRevision });
  }
});
await page.addInitScript(() => {
  window.__touchGameplay = { keyboard: 0, externalControls: 0, touches: [], pointerEvents: [] };
  document.addEventListener('keydown', () => window.__touchGameplay.keyboard++);
  document.addEventListener('click', e => { if (e.target.closest('#device-back-button, #device-confirm-button')) window.__touchGameplay.externalControls++; });
  for (const type of ['pointerdown', 'pointermove', 'pointerup', 'pointercancel']) document.addEventListener(type, e => {
    window.__touchGameplay.pointerEvents.push({ type, pointerType: e.pointerType, trusted: e.isTrusted, x: e.clientX, y: e.clientY, time: e.timeStamp });
  }, true);
  document.addEventListener('touchstart', e => {
    const target = e.target.closest('#device-ui button');
    if (target) window.__touchGameplay.touches.push({ screen: document.querySelector('#device-ui').dataset.screen, action: target.dataset.deviceAction || target.id, trusted: e.isTrusted });
  });
});
await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
async function http(path = '/api/save', body) {
  const response = await fetch(base + path, { method: body ? 'POST' : 'GET', headers: {
    ...(identity ? { Authorization: `Bearer ${identity.token}` } : {}), ...(body ? { 'Content-Type': 'application/json' } : {}),
  }, ...(body ? { body: JSON.stringify(body) } : {}) });
  assert.ok(response.ok, `${path}: ${response.status} ${await response.clone().text()}`); return response.json();
}
async function seed(events, purpose) {
  const before = await http(); let saved = before;
  for (let offset = 0; offset < events.length; offset += 100) saved = await http('/api/save-sync', {
    rulesVersion: saved.state.rulesVersion, baseRevision: saved.revision, batchId: crypto.randomUUID(), events: events.slice(offset, offset + 100),
  });
  fixtures.push({ purpose, beforeRevision: before.revision, afterRevision: saved.revision, events }); return saved;
}
function native(events, seed = 12345) {
  return JSON.parse(execFileSync(corePath, ['--replay-onboarding', String(seed)], {
    input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', timeout: 3000, maxBuffer: 65536, stdio: ['pipe', 'pipe', 'pipe'],
  }));
}
function predict(saved, action) { assert.equal(saved.baseSequence, 0); return native([...saved.events, action], saved.seed); }
function bestHit(saved) {
  const s = saved.state;
  return ['attack', 'magic', ...(s.energy >= 6 ? ['heavy'] : [])].map(type => ({ action: event(type), state: predict(saved, event(type)) }))
    .sort((a, b) => Number(b.state.phase === 'encounter') - Number(a.state.phase === 'encounter') || b.state.wildCaptureChance - a.state.wildCaptureChance || a.state.wildHp - b.state.wildHp)[0].action;
}
async function check(label) {
  const observed = await ui.geometry(label);
  for (const button of observed.buttons) assert.ok(button.box.width >= 69.9 && button.box.height >= 69.9, `${label}: ${button.id} needs a 70px target at 412px`);
  checks.push(observed);
}
async function sizeScreen() {
  await page.locator('.device-screen').evaluate(element => {
    element.style.setProperty('box-sizing', 'content-box', 'important');
    for (const dimension of ['width', 'height']) element.style.setProperty(dimension, '412px', 'important');
    element.style.setProperty('margin-inline', 'auto', 'important');
  });
}
async function reload(expected) {
  inputSessions.push(await page.evaluate(() => window.__touchGameplay));
  await page.reload(); await ui.onScreen(expected); await sizeScreen();
}
async function flick() {
  const box = await page.locator('#screen-surface').boundingBox(); assert.ok(box);
  const touch = (type, points = []) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map(([x, y]) => ({
    x: box.x + x * box.width / 412, y: box.y + y * box.height / 412, id: 1, radiusX: 3, radiusY: 3, force: 1,
  })) });
  await touch('touchStart', [[206, 300]]); const began = performance.now();
  // Use actual elapsed time: awaited CDP frames do not all take exactly 20 ms.
  // No synthetic event, timestamp, velocity or product handler is injected.
  for (let n = 1; n <= 3; n++) {
    await page.waitForTimeout(20); const elapsed = performance.now() - began;
    assert.ok(elapsed < 200, 'headless touch dispatch stays inside the round display');
    await touch('touchMove', [[206, 300 - 1.15 * elapsed]]);
  }
  await touch('touchEnd');
}
async function command(id) {
  await ui.reveal(id);
  const response = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.request().method() === 'POST' && r.status() === 200);
  if (id === 'capture') { await ui.tap(id, 'capture-aim'); await flick(); }
  else await ui.tap(id);
  const saved = await (await response).json();
  if (id === 'capture') {
    const [sent] = posts.at(-1).events;
    assert.equal(posts.at(-1).events.length, 1); assert.equal(sent.type, 'flick');
    const decoded = JSON.parse(execFileSync(corePath, ['--flick-trajectory', String(sent.value)], { encoding: 'utf8' }));
    assert.equal(decoded.hit, true, 'actual straight touch release lands on the native target');
    assertions.push({ name: 'Trusted touchscreen flick', value: sent.value, nativeTrajectory: decoded, oneCommand: true });
  }
  await page.waitForFunction(() => {
    const root = document.querySelector('#device-ui');
    return !['saving', 'capture-aim'].includes(root?.dataset.screen) && root?.dataset.layout !== 'feedback' && !!root?.querySelector('button:not([disabled])');
  });
  return saved;
}
async function member(id) {
  await ui.menu('companions');
  for (let n = 0; n < 3; n++) {
    if (await ui.control(`member-${id}`).count()) { await ui.tap(`member-${id}`, 'companion'); return; }
    await ui.tap('companions-next');
  }
  throw new Error(`Missing carried member ${id}`);
}

try {
  const pair = await http('/api/pairing/start', {}); identity = await http('/api/pairing/claim', { code: pair.code });
  await seed([event('hatch', 1)], 'Fresh isolated identity and native hatch; onboarding itself is covered by browser-touch.mjs.');
  await context.addInitScript(identity => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)), identity);
  await page.goto(base); await ui.onScreen('home'); await sizeScreen();
  await ui.menu('explore'); const {saved: walked} = await exploreEncounter(command); await ui.onScreen('battle'); await check('wild tactical opening');
  assert.equal(walked.state.wildCaptureChance, 0); assert.equal(await ui.control('capture').isDisabled(), true);
  const beforeAttack = await http(), attacked = await command('attack');
  assert.deepEqual(attacked.state, predict(beforeAttack, event('attack'))); await ui.onScreen('battle'); await check('wild tactical attack and retaliation');
  assertions.push({ name: 'Wild tactical attack', nativeOutcomeMatches: true, playerHp: attacked.state.hp, wildHp: attacked.state.wildHp });
  await ui.tap('cards', 'cards'); await check('wild card choices');
  const beforeCard = await http(), card = await command('card-shelter');
  assert.deepEqual(card.state, predict(beforeCard, event('card', 2)));
  assert.equal(card.state.cardUsed, true); assert.ok(card.state.shield > 0);
  await ui.onScreen('battle'); assert.equal(await ui.control('cards').isDisabled(), true); await check('wild field card consumed');
  assertions.push({ name: 'On-screen simulated card event', card: 'Shelter', nativeEvent: event('card', 2), usedOnce: true, physicalNfc: false });

  // Find a deterministic native miss without fabricating HP, chance or RNG.
  // Only the setup suffix is sent via API; the reviewed failed attempt is flicked.
  const origin = await http(); let history = [...origin.events], simulated = origin.state, missed;
  for (let fight = 0; fight < 18 && !missed; fight++) {
    if (simulated.phase !== 'encounter') {
      history.push(...Array.from({ length: simulated.recoveryRestCount }, () => event('rest')), event('walk', 100));
      simulated = native(history, origin.seed);
    }
    for (let turn = 0; turn < 40 && simulated.phase === 'encounter'; turn++) {
      const saved = { ...origin, events: history, state: simulated };
      const action = simulated.wildCaptureChance > 0 ? event('capture') : bestHit(saved);
      const next = native([...history, action], origin.seed);
      if (action.type === 'capture' && next.phase === 'encounter' && next.captureAttempts > simulated.captureAttempts) { missed = { before: simulated, after: next }; break; }
      history.push(action); simulated = next;
    }
  }
  assert.ok(missed, 'bounded native setup reaches a genuine failed capture');
  const seeded = await seed(history.slice(origin.events.length), 'Reach a legal capture which the deterministic core will reject; no fabricated state.');
  assert.deepEqual(seeded.state, missed.before); await reload('battle');
  await ui.reveal('capture'); assert.doesNotMatch(await ui.control('capture').textContent(), /\d+%/); assert.match(await ui.control('capture').textContent(), /Odds after throwing/); await check('legal capture chance');
  const unchanged = await http(), postCount = posts.length;
  await ui.tap('capture', 'capture-aim'); await check('capture aim with visible Cancel'); await ui.back('battle');
  assert.deepEqual(await http(), unchanged); assert.equal(posts.length, postCount);
  await ui.tap('capture', 'capture-aim'); await check('rearmed capture aim'); await ui.back('battle');
  assert.deepEqual(await http(), unchanged); assert.equal(posts.length, postCount);
  const failed = await command('capture'); await ui.onScreen('battle');
  assert.deepEqual(failed.state, predict(unchanged, posts.at(-1).events[0]));
  assert.deepEqual(failed.state, missed.after, 'a native hit uses the existing capture-zero policy');
  assert.deepEqual(failed.state.collection.map(member => member.id), unchanged.state.collection.map(member => member.id));
  await check('failed capture returned to battle');
  assertions.push({ name: 'Capture cancellation and genuine failure', visibleCancelWithoutPost: true, rearmedCancelWithoutPost: true, chance: missed.before.wildCaptureChance, attemptsAfter: failed.state.captureAttempts, collectionIdentityUnchanged: true, partnerHpBefore: unchanged.state.hp, partnerHpAfter: failed.state.hp });

  let captured, captureAttempts = 1;
  for (let fight = 0; fight < 8 && !captured; fight++) {
    let saved = await http();
    if (saved.state.phase !== 'encounter') {
      await ui.menu('care');
      if (saved.state.recoveryRestCount > 0) { await ui.tap('recover-review', 'recover-confirm'); await check('recovery confirmation'); await command('confirm-recovery'); }
      await ui.menu('explore'); saved = (await exploreEncounter(command)).saved; await ui.onScreen('battle');
    }
    for (let turn = 0; turn < 40 && saved.state.phase === 'encounter'; turn++) {
      saved = await http(); // Read includes replay history; POST receipts omit it.
      const action = saved.state.wildCaptureChance > 0 ? event('capture') : bestHit(saved);
      const expected = predict(saved, action), membersBefore = saved.state.collection.length;
      const before = saved; saved = await command(action.type);
      assert.deepEqual(saved.state, predict(before, posts.at(-1).events[0]));
      assert.deepEqual(saved.state, expected);
      if (action.type === 'capture') captureAttempts++;
      if (saved.state.collection.length > membersBefore) { captured = saved.state.collection.at(-1); break; }
    }
  }
  assert.ok(captured, 'real touch commands capture a new companion'); await ui.home(); await check('captured companion result dismissed to home');
  assertions.push({ name: 'Capture success through touch commands', attempts: captureAttempts, id: captured.id, formId: captured.formId, nativeOutcomesMatch: true });
  // A legal Play event guarantees missing energy, even if a future capture
  // reward fills HP. The recovery review/Back/confirm path is always exercised.
  await seed([event('play')], 'Guarantee missing energy through ordinary native Play before mandatory recovery review.');
  await reload('home'); await ui.menu('care');
  const beforeRecovery = await http(), recoveryPosts = posts.length;
  assert.ok(beforeRecovery.state.recoveryRestCount > 0);
  assert.ok(beforeRecovery.state.energy < 100);
  const recoveryEvents = Array.from({ length: beforeRecovery.state.recoveryRestCount }, () => event('rest'));
  await ui.tap('recover-review', 'recover-confirm'); await check('mandatory recovery review'); await ui.back('care');
  assert.deepEqual(await http(), beforeRecovery); assert.equal(posts.length, recoveryPosts);
  await ui.tap('recover-review', 'recover-confirm'); const recovered = await command('confirm-recovery'); await ui.onScreen('care'); await check('recovery saved return');
  assert.deepEqual(posts.at(-1).events, recoveryEvents);
  assert.deepEqual(recovered.state, native([...beforeRecovery.events, ...recoveryEvents], beforeRecovery.seed));
  assert.equal(recovered.state.hp, recovered.state.combat.maxHp); assert.equal(recovered.state.energy, 100);
  assert.equal(recovered.state.xp, beforeRecovery.state.xp); assert.equal(posts.length, recoveryPosts + 1);
  assertions.push({ name: 'Mandatory Recover fully review, Back and commit', restCount: recoveryEvents.length, before: { hp: beforeRecovery.state.hp, energy: beforeRecovery.state.energy }, after: { hp: recovered.state.hp, energy: recovered.state.energy }, backWithoutPost: true, exactNativeRestBatch: true, xpUnchanged: true });
  await member(captured.id); await check('captured member details'); const beforePartner = await http();
  const selected = await command('select-companion'); await ui.onScreen('companion');
  assert.equal(selected.state.activeCreatureId, captured.id); assert.deepEqual(selected.state.collection, beforePartner.state.collection);
  await member(1); await command('select-companion');

  // Fill the carried roster and train the founder with genuine native Auto and
  // ordinary care actions, then review every paged member and an eligible route.
  let grown = await http(), trainingBatches = 0;
  for (let n = 0; n < 100 && (grown.state.collection.length < 8 || !grown.state.evolution.options.some(o => o.eligible)); n++) {
    const gate = grown.state.evolution.options[0]; assert.ok(gate);
    const actions = grown.state.collection.length === 8 && grown.state.level >= gate.requiredLevel
      ? [event('play'), event('rest')]
      : [...Array.from({ length: grown.state.recoveryRestCount }, () => event('rest')), event('mode', 1), event('walk', 100), event('auto')];
    grown = await seed(actions, 'Native care/Auto training and captures for full-roster paging and legal evolution.'); trainingBatches++;
  }
  assert.equal(grown.state.collection.length, 8); const option = grown.state.evolution.options.find(o => o.eligible); assert.ok(option);
  await reload('home'); await ui.menu('companions'); const beforeBrowse = await http(), rosterPosts = posts.length, seen = [];
  for (let pageIndex = 0; pageIndex < 8 && seen.length < grown.state.collection.length; pageIndex++) {
    const ids = await ui.screen.locator('[data-device-action^="member-"]').evaluateAll(nodes => nodes.map(node => node.dataset.deviceAction));
    assert.ok(ids.length > 0);
    for (const id of ids) { await ui.reveal(id); await check(`roster page ${pageIndex + 1}: ${id}`); seen.push(Number(id.slice(7))); }
    await ui.tap('companions-next');
  }
  assert.deepEqual(seen.sort((a, b) => a - b), grown.state.collection.map(m => m.id).sort((a, b) => a - b));
  assert.equal(posts.length, rosterPosts); assert.deepEqual(await http(), beforeBrowse);
  const nonactive = grown.state.collection.find(m => m.id !== grown.state.activeCreatureId);
  await member(nonactive.id); await ui.tap('release-review', 'release-review'); await check('release review');
  await ui.tap('release-confirmation', 'release-confirm'); await check('release confirmation'); await ui.back('release-review');
  assert.equal(posts.length, rosterPosts); assert.deepEqual(await http(), beforeBrowse);
  assertions.push({ name: 'Paged carried roster, partner swap and cancelled release', ids: seen, partnerSwapPreservesIndividuals: true, cancelledReleaseWithoutPost: true });
  await ui.tap('release-confirmation', 'release-confirm'); const released = await command('confirm-release'); await ui.onScreen('release-result'); await check('committed release result');
  assert.deepEqual(released.state, predict(beforeBrowse, event('release', nonactive.id)));
  assert.deepEqual(released.state.collection, beforeBrowse.state.collection.filter(member => member.id !== nonactive.id));
  assert.deepEqual(released.state.journal, beforeBrowse.state.journal);
  assert.equal(released.state.activeCreatureId, beforeBrowse.state.activeCreatureId);
  assert.equal(posts.length, rosterPosts + 1); await ui.tap('release-done', 'companions'); await check('release result returns to roster');
  assertions.push({ name: 'Committed nonpartner release and result return', releasedId: nonactive.id, remainingCount: released.state.collection.length, journalRetained: true, activePartnerRetained: true, nativeOutcomeMatches: true });

  await ui.menu('progression'); await ui.tap('evolution-options', 'evolution-options'); await ui.tap(`evolve-${option.formId}`, 'evolution-preview'); await check('eligible evolution preview');
  await ui.tap('evolution-skills', 'evolution-skills');
  for (const key of ['physical', 'heavy', 'magic']) {
    await ui.reveal(`evo-skill-${key}`); assert.ok((await ui.control(`evo-skill-${key}`).textContent()).includes(`${grown.state.combat.skills[key]} → ${option.combat.skills[key]}`)); await check(`evolution ${key} comparison`);
  }
  await ui.back('evolution-preview'); await ui.tap('evolution-review', 'evolution-confirm'); await check('eligible evolution confirmation');
  const beforeEvolution = await http(), evolutionPosts = posts.length;
  await ui.back('evolution-preview'); assert.deepEqual(await http(), beforeEvolution); assert.equal(posts.length, evolutionPosts);
  await ui.tap('evolution-review', 'evolution-confirm'); const evolved = await command('confirm-evolution'); await ui.onScreen('evolution-result'); await check('evolution saved result');
  assert.deepEqual(evolved.state, predict(beforeEvolution, event('evolve', option.formId)));
  assert.equal(evolved.state.activeCreatureId, grown.state.activeCreatureId); assert.equal(evolved.state.formId, option.formId);
  await ui.tap('evolution-done', 'progression'); await check('evolution result progression'); await ui.home();
  assertions.push({ name: 'Eligible evolution', nativeTrainingBatches: trainingBatches, cancelledReviewWithoutPost: true, id: evolved.state.activeCreatureId, formId: evolved.state.formId, coreOutcomeMatches: true });

  // The freed slot remains available for a genuine wild Auto encounter. Start
  // commits once; all progress, pause, resume and replay controls are cosmetic.
  await page.emulateMedia({ reducedMotion: 'no-preference' }); await reload('home');
  await ui.menu('explore'); await ui.tap('wild-mode', 'wild-mode'); await command('wild-auto'); await ui.onScreen('explore');
  await exploreEncounter(command); await ui.onScreen('wild-auto-confirm'); await check('wild Auto start review');
  const beforeAuto = await http(), autoPosts = posts.length;
  await ui.back(); await ui.home(); assert.deepEqual(await http(), beforeAuto); assert.equal(posts.length, autoPosts);
  await ui.tap('wild-auto-confirm', 'wild-auto-confirm');
  const auto = await command('wild-auto-start'); await ui.onScreen('wild-auto-progress');
  assert.deepEqual(auto.state, predict(beforeAuto, event('auto-fight')));
  const savedAuto = await http(); assert.equal(posts.length, autoPosts + 1);
  await ui.tap('auto-pause'); await page.waitForTimeout(800);
  const pausedStep = await ui.screen.getAttribute('data-auto-step');
  await page.waitForTimeout(800); assert.equal(await ui.screen.getAttribute('data-auto-step'), pausedStep); await check('wild Auto paused progress');
  assert.deepEqual(await http(), savedAuto);
  await ui.tap('auto-pause');
  await page.waitForFunction(step => {
    const root = document.querySelector('#device-ui'); return ['capture-aim','wild-auto-result'].includes(root?.dataset.screen) || root?.dataset.autoStep !== step;
  }, pausedStep);
  if (await ui.screen.getAttribute('data-screen') === 'wild-auto-progress') await ui.tap('auto-finish');
  await autoCheckpoint(page);
  let finalAuto=auto, finalSaved=savedAuto, expectedPosts=autoPosts+1;
  if(auto.state.autoCapture===1){
    await ui.onScreen('capture-aim');await check('wild Auto manual capture pause');
    const pausePosts=posts.length;await page.waitForTimeout(800);assert.equal(posts.length,pausePosts);assert.deepEqual(await http(),savedAuto);
    finalAuto=await resumeFighting(page,()=>ui.back());finalSaved=await http();expectedPosts++;
    assert.deepEqual(finalAuto.state,predict(savedAuto,event('auto-resume')));
  }
  await ui.onScreen('wild-auto-result'); await check('wild Auto saved result');
  await ui.tap('wild-auto-replay', 'wild-auto-progress'); await ui.tap('auto-pause'); await check('wild Auto replay paused');
  await ui.tap('auto-finish', 'wild-auto-result'); await check('wild Auto replay finished'); await ui.tap('wild-auto-done', 'home');
  assert.equal(posts.length, expectedPosts); assert.deepEqual(await http(), finalSaved);
  assertions.push({ name: 'Wild Auto start, playback pause/resume, manual capture decision and replay', backBeforeStartWithoutPost: true, turns: auto.autoTrace.steps.length, outcome: finalAuto.autoTrace.outcome, explicitStartAndOptionalResumeOnly: true, pausedStep, replayDoesNotChangeSave: true, nativeOutcomeMatches: true });
  inputSessions.push(await page.evaluate(() => window.__touchGameplay));
  assert.deepEqual(errors, []);
  for (const input of inputSessions) { assert.equal(input.keyboard, 0); assert.equal(input.externalControls, 0); assert.ok(input.touches.every(touch => touch.trusted)); assert.ok(input.pointerEvents.every(pointer => pointer.trusted && pointer.pointerType === 'touch')); }
  assert.ok(inputSessions.flatMap(s => s.touches).length > 0);
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  assert.deepEqual(sourceFiles(), initialSources, 'source files stayed fixed throughout this run');
  outcome = 'PASS'; console.log(`PASS touch gameplay: native attack/card/flick capture, repeated visible aim cancellations, mandatory recovery review/Back/commit, eight-member paging, partner swap, release cancel/commit/result, legal evolution, wild Auto start/pause/resume/replay (${checks.length} round-screen geometry checks).`);
} catch (error) { failure = String(error.stack || error); throw error; }
finally {
  mkdirSync('docs/evidence', { recursive: true });
  writeFileSync(process.env.TOUCH_GAMEPLAY_REPORT || '../deliverables/manual-auto-regression-20261009/touch-only-gameplay.json', JSON.stringify({ outcome, failure, sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    sourceFiles: initialSources, binaries: [corePath, battleCorePath].map(path => ({ name: path.split('/').at(-1), sha256: hash(readFileSync(path)) })),
    scope: 'Headless hasTouch browser, only trusted on-screen taps and CDP touchscreen drag/release; capture values decoded and replayed by native core; 412 CSS-pixel circular display with minimum 70px targets; public placeholder artwork. Native histories prepare fixtures separately.',
    assertions, checks, gameCommandsFromTouch: posts, fixtureSetup: fixtures, inputSessions, errors,
    limitations: ['No physical ESP touch/display, NFC reader or gyro proof.', '70 CSS-pixel targets at 412px are geometry checks, not calibrated physical finger-fit measurements.', 'Pairing/onboarding, Practice and interrupted-save recovery are covered by separate touch scripts.'],
  }, null, 2) + '\n');
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
}

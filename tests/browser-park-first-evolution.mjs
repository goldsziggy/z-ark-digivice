// One fresh native journey. All gameplay commands originate from the browser
// buttons; the harness uses authenticated GET /api/save only for assertions.
// Navigation follows browser-gameplay.mjs; hold uses the real pointer lifecycle.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { gameReady, autoCheckpoint, flickBall, exploreEncounter } from './manual-auto-browser-tools.mjs';
import { holdDeviceBack } from './browser-tools.mjs';

const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
const sourceCommit = execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
const hash = value => createHash('sha256').update(value).digest('hex');
const binaries = [corePath, battleCorePath].map(path => ({ path, sha256: hash(readFileSync(path)) }));
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-first-evolution-'));
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const started = Date.now();
const app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, hasTouch: true, reducedMotion: 'reduce' });
page.setDefaultTimeout(10000);
const cdp=await page.context().newCDPSession(page);
const screen = page.locator('#device-ui');
const left = page.locator('#device-back-button');
const right = page.locator('#device-confirm-button');
const errors = [], posts = [], milestones = [], encounters = [], recovery = [], checks = [], log = [];
const buttons = { leftTapNext: 0, leftHoldBack: 0, rightConfirm: 0 };
let identity, lastSave, failure, outcome = 'FAIL', reads = 0, evolution;
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (request.method() !== 'POST') return;
  const path = new URL(request.url()).pathname;
  const body = request.postDataJSON();
  posts.push({ path, ...(path === '/api/save-sync' ? { events: body.events, baseRevision: body.baseRevision } : {}) });
});
const line = message => { log.push(message); console.log(message); };
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
const ready = () => page.waitForFunction(() => !document.querySelector('#device-confirm-button').disabled);
async function confirm() { await ready(); buttons.rightConfirm++; await right.click(); }
async function choose(id, execute = true) {
  await screen.locator(`[data-device-action="${id}"]`).waitFor({ state: 'attached' });
  for (let n = 0; n < 32; n++) {
    if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) {
      if (execute) await confirm();
      return;
    }
    buttons.leftTapNext++; await left.click();
  }
  throw new Error(`Cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function back() { buttons.leftHoldBack++; await holdDeviceBack(page); }
async function home() {
  for (let n = 0; n < 14 && await screen.getAttribute('data-screen') !== 'home'; n++) {
    const current = await screen.getAttribute('data-screen');
    if (current === 'wild-auto-result') await choose('wild-auto-done');
    else if (current === 'evolution-result') await choose('evolution-home');
    else await back();
  }
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await onScreen('menu'); await choose(id); await onScreen(id); }
async function command(id, endpoint = '/api/save-sync', status = 200) {
  await choose(id, false);
  const response = page.waitForResponse(r => r.url().endsWith(endpoint) && r.request().method() === 'POST');
  await confirm();
  const received = await response;
  assert.equal(received.status(), status, `${id}: ${await received.text()}`);
  const result = await received.json();
  await gameReady(page);
  return result;
}
async function read() {
  reads++;
  const response = await fetch(`${base}/api/save`, { headers: { Authorization: `Bearer ${identity.token}` } });
  assert.equal(response.status, 200);
  lastSave = await response.json(); return lastSave;
}
function summary(saved) {
  const s = saved.state, member = s.collection.find(item => item.id === s.activeCreatureId);
  return { revision: saved.revision, sequence: s.sequence, phase: s.phase, rulesVersion: s.rulesVersion,
    activeCreatureId: s.activeCreatureId, name: s.creature, formId: s.formId, level: s.level,
    xp: member?.xp, bond: s.bond, hp: s.hp, maxHp: s.combat?.maxHp, energy: s.energy,
    steps: s.steps+s.walking.eligibleSteps, legacySteps:s.steps, eligibleSteps:s.walking.eligibleSteps, encounters: s.encounters, captures: s.captures, battleMode: s.battleMode,
    collection: s.collection.map(m => ({ id: m.id, name: m.name, formId: m.formId, xp: m.xp, level: m.level, bond: m.bond })),
    evolution: s.evolution.options.map(o => ({ formId: o.formId, name: o.name, requiredLevel: o.requiredLevel, requiredBond: o.requiredBond, eligible: o.eligible })) };
}
async function snapshot() {
  return screen.evaluate(root => ({ screen: root.dataset.screen,
    title: root.querySelector('#device-screen-title')?.textContent || '',
    detail: root.querySelector('.screen-detail')?.textContent || '',
    actions: [...root.querySelectorAll('[data-device-action]')].map(a => ({ id: a.dataset.deviceAction, label: a.textContent.trim(), disabled: a.disabled })) }));
}
async function milestone(name, save) {
  save ||= await read();
  const entry = { name, elapsedSeconds: Math.round((Date.now() - started) / 1000), state: summary(save), screen: await snapshot() };
  milestones.push(entry); line(`${name}: ${JSON.stringify(entry.state)}`); return save;
}
async function recoverFully(label) {
  const before = await read();
  if (!before.state.recoveryRestCount) return before;
  await menu('care'); await choose('recover-review'); await onScreen('recover-confirm');
  const review = await snapshot();
  const postCount = posts.length;
  assert.deepEqual(await read(), before, 'Recovery review must preserve the whole save');
  await command('confirm-recovery'); await onScreen('care');
  const after = await read();
  assert.equal(after.state.hp, after.state.combat.maxHp);
  assert.equal(after.state.energy, 100);
  assert.equal(after.state.activeCreatureId, before.state.activeCreatureId);
  assert.equal(after.state.collection[0].xp, before.state.collection[0].xp);
  assert.equal(posts.length, postCount + 1, 'Recover fully is one confirmed browser batch');
  assert.equal(posts.at(-1).events.length, before.state.recoveryRestCount);
  assert.ok(posts.at(-1).events.every(e => e.type === 'rest' && e.value === 0));
  recovery.push({ label, rests: before.state.recoveryRestCount, review, before: summary(before), after: summary(after) });
  if (recovery.length === 1) await milestone('Recover fully fills health and energy', after);
  return after;
}
try {
  await page.goto(`${base}/?controls=buttons`); await onScreen('home');
  await choose('connection'); await onScreen('connection');
  await command('start-pairing', '/api/pairing/start', 201);
  identity = await command('claim-device', '/api/pairing/claim', 201); await onScreen('starter-select');
  const egg = await milestone('Fresh paired egg');
  assert.equal(egg.revision, 1); assert.ok(egg.state.onboarding.offerSeed > 0); assert.equal(egg.state.phase, 'egg');
  assert.equal(posts.filter(p => p.path === '/api/save-sync').length, 0);
  await choose('starter-2'); await onScreen('starter-review');
  await command('hatch-starter'); await onScreen('starter-hatched'); await choose('meet-starter'); await onScreen('home');
  const hatched = await milestone('Hatched Agumon');
  assert.equal(hatched.state.creature, 'Agumon'); assert.equal(hatched.state.level, 1);
  assert.ok(hatched.state.evolution.options.some(o => o.requiredLevel === 5 && o.requiredBond > 0));

  await menu('progression'); await choose('evolution-options');
  await choose(`evolve-${hatched.state.evolution.options[0].formId}`); await onScreen('evolution-preview');
  await choose('evolution-requirements'); await onScreen('evolution-requirements');
  assert.equal(await screen.locator('[data-device-action="evolution-review"]').isDisabled(), true);
  assert.deepEqual(await read(), hatched, 'Locked Digivolution preview preserves the whole save');
  checks.push({ name: 'Fresh starter cannot Digivolve before level and bond requirements', screen: await snapshot(), wholeSaveUnchanged: true });

  await menu('explore'); await choose('wild-mode'); await onScreen('wild-mode');
  await command('wild-auto'); await onScreen('explore');
  const mode = await milestone('Auto selected before walking'); assert.equal(mode.state.battleMode, 'auto');

  for (let index = 1; index <= 20; index++) {
    const before = await recoverFully(`before encounter ${index}`);
    await menu('explore'); const walked = await exploreEncounter(command); await onScreen('wild-auto-confirm');
    const waiting = await read();
    assert.equal(waiting.state.walking.eligibleSteps, before.state.walking.eligibleSteps + walked.inputs * 100);
    assert.equal(waiting.state.steps, before.state.steps);
    assert.equal(waiting.state.encounters, before.state.encounters + 1);
    assert.equal(waiting.state.phase, 'encounter');
    const postCount = posts.length;
    const review = await snapshot();
    await back(); await home();
    assert.equal(posts.length, postCount, 'Back from Auto confirmation does not commit');
    assert.deepEqual(await read(), waiting, 'Waiting Auto encounter remains unchanged until explicit start');
    await choose('wild-auto-confirm'); await onScreen('wild-auto-confirm');
    let result = await command('wild-auto-start'); await autoCheckpoint(page);
    assert.deepEqual(posts.at(-1).events, [{type:'auto-fight',value:0}]);
    const partial = result.autoTrace, throws=[];
    if(result.state.autoCapture===1){
      const pause=await read(),pausePosts=posts.length;await page.waitForTimeout(250);assert.deepEqual(await read(),pause);assert.equal(posts.length,pausePosts);
      for(let attempt=0;attempt<3&&result.state.autoCapture===1;attempt++){
        await onScreen('capture-aim');const captureBefore=await read(),capturePosts=posts.length;
        const response=page.waitForResponse(r=>r.url().endsWith('/api/save-sync')&&r.request().method()==='POST'&&r.status()===200);
        await flickBall(page,cdp);result=await(await response).json();await gameReady(page);
        const events=posts.at(-1).events;assert.equal(events.length,1);assert.equal(events[0].type,'flick');
        const trajectory=JSON.parse(execFileSync(corePath,['--flick-trajectory',String(events[0].value)],{encoding:'utf8'}));assert.equal(trajectory.hit,true);
        assert.equal(posts.length,capturePosts+1);assert.equal(result.state.lastCapture.attempt,captureBefore.state.captureAttempts+1);
        throws.push({value:events[0].value,result:result.state.lastCapture.result,attempt:result.state.lastCapture.attempt,chance:result.state.lastCapture.chance,nativeTrajectory:trajectory});
      }
      assert.equal(result.state.phase,'home');await onScreen('home');
    } else await onScreen('wild-auto-result');
    const after = await read();
    assert.equal(after.state.phase, 'home');
    assert.equal(after.state.activeCreatureId, hatched.state.activeCreatureId);
    assert.equal(after.state.formId, hatched.state.formId);
    assert.equal(posts.length, postCount + 1 + throws.length, 'Only explicit Auto start and physical flicks submit battle actions');
    assert.equal(posts.at(-1).events[0].type, throws.length ? 'flick' : 'auto-fight');
    const trace = partial;
    const entry = { encounter: index, walkInputs: walked.inputs, review, before: summary(before), waiting: summary(waiting), after: summary(after),
      result: await snapshot(), autoTrace: trace ? { outcome: throws.length ? result.state.lastCapture.result === 'captured' ? 'captured' : 'capture-exhausted' : trace.outcome, steps: trace.steps.length,
        captureAttempts: 0 } : null,
      manualThrows:throws, explicitStartAndPhysicalFlicksOnly: true, addedMembers: after.state.collection.filter(m => !before.state.collection.some(p => p.id === m.id)).map(m => ({ id: m.id, name: m.name, formId: m.formId })) };
    encounters.push(entry);
    line(`Encounter ${index}: ${JSON.stringify({ outcome: entry.autoTrace?.outcome, ...summary(after) })}`);
    if(await screen.getAttribute('data-screen')==='wild-auto-result') await choose('wild-auto-done'); await onScreen('home');
    if (index === 1) await milestone('First explicit Auto battle completed', after);
    if (after.state.evolution.options.some(o => o.eligible)) break;
    // Play raises the real companion's bond through the care menu when level
    // is sufficient; no training events are injected outside the device UI.
    const gate = after.state.evolution.options.filter(o => o.requiredLevel === 5).sort((a, b) => a.requiredBond - b.requiredBond)[0];
    if (gate && after.state.level >= gate.requiredLevel && after.state.bond < gate.requiredBond) {
      await menu('care');
      for (let play = 0; play < 40 && lastSave.state.bond < gate.requiredBond; play++) {
        await command('play'); await onScreen('care'); await read();
      }
      if (lastSave.state.evolution.options.some(o => o.eligible)) break;
    }
  }

  const grown = await recoverFully('before first Digivolution');
  await milestone('Progression after bounded Auto encounters', grown);
  const option = grown.state.evolution.options.find(o => o.requiredLevel === 5 && o.eligible);
  if (!option) {
    outcome = 'BLOCKED'; failure = `No eligible first Lv5 Digivolution after ${encounters.length}/20 encounters; level ${grown.state.level}, bond ${grown.state.bond}, XP ${summary(grown).xp}.`;
  } else {
    await menu('progression'); await choose('evolution-options'); await choose(`evolve-${option.formId}`); await onScreen('evolution-preview');
    await choose('evolution-requirements'); await onScreen('evolution-requirements');
    const requirements = await snapshot();
    assert.equal(await screen.locator('[data-device-action="evolution-review"]').isDisabled(), false);
    await choose('evolution-review'); await onScreen('evolution-confirm');
    const before = await read(), postCount = posts.length;
    await back(); await onScreen('evolution-requirements');
    assert.equal(posts.length, postCount); assert.deepEqual(await read(), before, 'Back from evolution confirmation preserves the whole save');
    await choose('evolution-review'); await onScreen('evolution-confirm');
    const confirmation = await snapshot();
    await command('confirm-evolution'); await onScreen('evolution-result');
    const after = await milestone('First legal Digivolution saved');
    assert.equal(after.state.formId, option.formId); assert.equal(after.state.activeCreatureId, before.state.activeCreatureId);
    assert.equal(summary(after).xp, summary(before).xp); assert.equal(after.state.bond, before.state.bond);
    assert.equal(after.state.collection.length, before.state.collection.length);
    assert.deepEqual(after.state.collection.slice(1), before.state.collection.slice(1));
    evolution = { option: { formId: option.formId, name: option.name, requiredLevel: option.requiredLevel, requiredBond: option.requiredBond },
      before: summary(before), after: summary(after), requirements, confirmation, identityXpBondPreserved: true,
      backPreservesWholeSave: true, otherMembersUnchanged: true };
    const finalPosts = posts.length; await choose('evolution-home'); await onScreen('home'); await page.reload(); await onScreen('home');
    assert.equal(posts.length, finalPosts); assert.deepEqual(await read(), after, 'Reload preserves saved evolution without another command');
    checks.push({ name: 'Evolution survives browser reload without duplicate commands', wholeSaveUnchanged: true });
    outcome = 'PASS';
  }
  assert.deepEqual(errors, []);
  assert.ok(encounters.length <= 20);
  assert.ok(posts.every(p => ['/api/pairing/start', '/api/pairing/claim', '/api/starter-offers', '/api/save-sync'].includes(p.path)));
} catch (error) {
  outcome = 'FAIL';
  failure = `${error.stack || error}`;
  try { if (identity) await read(); } catch { /* Preserve earlier evidence if service failed. */ }
} finally {
  const gamePosts = posts.filter(p => p.path === '/api/save-sync');
  const eventCounts = gamePosts.flatMap(p => p.events).reduce((counts, event) => ({ ...counts, [event.type]: (counts[event.type] || 0) + 1 }), {});
  const report = { result: outcome, failure, sourceCommit, binaries, isolatedPort: app.server.address()?.port,
    elapsedSeconds: Math.round((Date.now() - started) / 1000), encounterCap: 20,
    method: { freshIdentity: true, twoButtonNavigationPlusPhysicalCaptureFlick: true, directGamePosts: 0, authenticatedSaveGets: reads,
      stateSeeding: false, browserStorageMutation: false, nativePrediction: false, reducedMotion: true,
      note: 'Two-button navigation follows browser-gameplay.mjs; capture always uses a trusted touchscreen flick, with native trajectory validation. No automatic throws.' },
    buttons, counts: { pairingPosts: posts.filter(p=>p.path.startsWith('/api/pairing/')).length, starterOfferPosts:posts.filter(p=>p.path==='/api/starter-offers').length, gameCommandBatches: gamePosts.length,
      nativeEvents: gamePosts.reduce((n, p) => n + p.events.length, 0), eventCounts, encounters: encounters.length,
      autoCaptureAttempts: encounters.reduce((n, e) => n + (e.autoTrace?.captureAttempts || 0), 0), manualFlicks:encounters.reduce((n,e)=>n+e.manualThrows.length,0),
      captures: lastSave?.state.captures, steps: lastSave ? lastSave.state.steps+lastSave.state.walking.eligibleSteps : null, recoverFullyConfirmations: recovery.length },
    milestones, encounters, recovery, evolution, checks, posts, finalState: lastSave ? summary(lastSave) : null,
    finalSaveSha256: lastSave ? hash(JSON.stringify(lastSave)) : null, pageErrors: errors };
  mkdirSync('docs/evidence', { recursive: true });
  const output = process.env.FIRST_EVOLUTION_REPORT || 'docs/evidence/park-readiness-first-evolution';
  writeFileSync(`${output}.json`, `${JSON.stringify(report, null, 2)}\n`);
  line(`RESULT ${outcome}: ${JSON.stringify({ counts: report.counts, buttons, failure, pageErrors: errors })}`);
  writeFileSync(`${output}.txt`, `${log.join('\n')}\n`);
  await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(dataDir, { recursive: true, force: true });
  if (outcome !== 'PASS') process.exitCode = 1;
}

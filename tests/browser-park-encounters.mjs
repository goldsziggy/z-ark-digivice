// Real native histories accelerate fixture setup; every reviewed interaction
// uses the two device buttons. No HP, collection, RNG or rarity is fabricated.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';

const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const binaries = [corePath, battleCorePath].map(path => ({ path, sha256: digest(path) }));
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-park-encounters-'));
let app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
let context, page, screen, left, right, identity;
const posts = [], errors = [], checks = [], sizes = [], screenshots = [];
const pendingKey = 'digivice.dev.pending.v1';
const native = events => JSON.parse(execFileSync(corePath, ['--replay-onboarding', '12345'], {
  input: events.map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', timeout: 3000, maxBuffer: 65536,
}));
const event = (type, value = 0) => ({ type, value });
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
async function open(storageState) {
  context = await browser.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce', ...(storageState ? { storageState } : {}) });
  if (!storageState) await context.addInitScript(identity => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)), identity);
  page = await context.newPage(); page.setDefaultTimeout(12000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST' && /\/api\/(save-sync|battle\/(start|act))$/.test(request.url())) posts.push({ path: new URL(request.url()).pathname, raw: request.postData(), body: request.postDataJSON() }); });
  await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
  screen = page.locator('#device-ui'); left = page.locator('#device-back-button'); right = page.locator('#device-confirm-button');
  await page.goto(`${base}/?controls=buttons`);
}
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
async function choose(id, activate = true) {
  await screen.locator(`[data-device-action="${id}"]`).waitFor({ state: 'attached' });
  for (let n = 0; n < 32; n++) {
    if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) { if (activate) await right.click(); return; }
    await left.click();
  }
  throw new Error(`Cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function back() { await holdDeviceBack(page); }
async function home() {
  for (let n = 0; n < 16 && await screen.getAttribute('data-screen') !== 'home'; n++) {
    const name = await screen.getAttribute('data-screen');
    if (name === 'wild-auto-result') await choose('wild-auto-done'); else await back();
  }
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await choose(id); await onScreen(id); }
async function command(id) {
  await choose(id, false); const reply = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.request().method() === 'POST' && r.status() === 200);
  await right.click(); const saved = await (await reply).json();
  await page.waitForFunction(() => !document.querySelector('#device-confirm-button').disabled); return saved;
}
async function member(id) {
  for (let n = 0; n < 3; n++) { if (await screen.locator(`[data-device-action="member-${id}"]`).count()) { await choose(`member-${id}`); await onScreen('companion'); return; } await choose('companions-next'); }
  throw new Error(`Missing carried member ${id}`);
}
async function geometry(label) {
  const before = await page.locator('.device-screen').evaluate(e => e.style.cssText);
  for (const size of [null, 201.6, 140.16]) {
    if (size !== null) await page.locator('.device-screen').evaluate((e, size) => { e.style.setProperty('box-sizing', 'content-box', 'important'); for (const key of ['width', 'height']) e.style.setProperty(key, `${size}px`, 'important'); e.style.setProperty('margin-inline', 'auto', 'important'); }, size);
    const result = await screen.evaluate(root => {
      const c = document.querySelector('#screen-surface').getBoundingClientRect(), x = c.x + c.width / 2, y = c.y + c.height / 2;
      const boxes = [...root.querySelectorAll('button,.screen-detail')].filter(e => e.getClientRects().length).map(e => ({ text: e.textContent, b: e.getBoundingClientRect() })), failures = [];
      for (const { text, b } of boxes) if ([[b.left,b.top],[b.right,b.top],[b.left,b.bottom],[b.right,b.bottom]].some(([a,z]) => Math.hypot(a-x,z-y)>c.width/2+2)) failures.push(`clipped:${text}`);
      for (let a=0;a<boxes.length;a++)for(let b=a+1;b<boxes.length;b++){const p=boxes[a].b,q=boxes[b].b;if(Math.min(p.right,q.right)-Math.max(p.left,q.left)>1&&Math.min(p.bottom,q.bottom)-Math.max(p.top,q.top)>1)failures.push(`overlap:${boxes[a].text}/${boxes[b].text}`);}
      return { width: c.width, failures };
    });
    assert.deepEqual(result.failures, [], label); sizes.push({ label, width: result.width });
  }
  await page.locator('.device-screen').evaluate((e, css) => { e.style.cssText = css; }, before);
  if (process.env.PARK_EVIDENCE === '1') { const path = `docs/evidence/park-${label}.png`; await page.locator('#screen-surface').screenshot({ path }); screenshots.push(path); }
}
const encounterFields = state => Object.fromEntries(['wildFormId','wildName','wildHp','wildMaxHp','wildRules','wildRarity','wildGuard','wildTurn','wildCombat','captureAttempts','attackBoost','shield','cardUsed','hp','energy','activeCreatureId','journal','nextMemberId','steps','stepCredit','queuedEncounters','stepsToNextEncounter'].map(key => [key,state[key]]));

try {
  mkdirSync('docs/evidence', { recursive: true });
  const pair = await http('/api/pairing/start', {}); identity = await http('/api/pairing/claim', { code: pair.code });
  // Find an actual native miss; choosing setup moves is fixture generation, not
  // a player-facing policy or a test-only RNG/health override.
  const history = [event('hatch', 1)]; let simulated = native(history), missed;
  for (let fight=0;fight<18&&!missed;fight++) {
    history.push(...Array.from({length:simulated.recoveryRestCount},()=>event('rest')),event('walk',100)); simulated=native(history);
    for(let turn=0;turn<40&&simulated.phase==='encounter';turn++) {
      let chosen;
      if(simulated.wildCaptureChance>0) chosen=event('capture');
      else {
        const options=['attack','magic',...(simulated.energy>=6?['heavy']:[])].map(type=>({action:event(type),next:native([...history,event(type)])}));
        chosen=options.sort((a,b)=>Number(b.next.phase==='encounter')-Number(a.next.phase==='encounter')||b.next.wildCaptureChance-a.next.wildCaptureChance||a.next.wildHp-b.next.wildHp)[0].action;
      }
      const next=native([...history,chosen]);
      if(chosen.type==='capture'&&next.phase==='encounter'&&next.captureAttempts>simulated.captureAttempts){missed={before:simulated,after:next};break;}
      history.push(chosen);simulated=next;
    }
  }
  assert.ok(missed,'bounded native history contains a failed capture');
  await seed(history);await open();await onScreen('battle');
  assert.equal(await screen.getAttribute('data-encounter-rarity'),missed.before.wildRarity);
  await choose('capture',false);assert.match(await screen.locator('[data-selected=true]').textContent(),new RegExp(`${missed.before.wildCaptureChance}%`));await geometry('capture-rarity');
  const failure=await command('capture');assert.deepEqual(failure.state,missed.after);history.push(event('capture'));simulated=missed.after;
  checks.push({name:'Native capture failure',chance:missed.before.wildCaptureChance,rarity:missed.before.wildRarity,hpBefore:missed.before.hp,hpAfter:missed.after.hp,attemptsAfter:missed.after.captureAttempts,unchangedCollection:true});
  // Finish through valid commands, then fill the carried team with native Auto.
  let suffix=[];
  const append=(type,value=0)=>{const action=event(type,value);history.push(action);suffix.push(action);simulated=native(history);};
  for(let n=0;n<40&&simulated.phase==='encounter';n++)append('attack');
  append('mode',1);
  for(let fight=0;fight<60&&simulated.collection.length<8;fight++){for(let n=simulated.recoveryRestCount;n>0;n--)append('rest');append('walk',100);append('auto');}
  assert.equal(simulated.collection.length,8);append('mode',0);for(let n=simulated.recoveryRestCount;n>0;n--)append('rest');append('walk',200);
  await seed(suffix);let full=await http();assert.deepEqual(full.state,simulated);await page.reload();await onScreen('battle');
  assert.equal(full.state.queuedEncounters,1);assert.equal(full.state.stepsToNextEncounter,0);
  assert.equal(await screen.locator('[data-device-action=capture]').isDisabled(),true);await choose('make-room',false);await geometry('full-roster-make-room');
  const noWrite=posts.length;await choose('make-room');await onScreen('companions');await member(full.state.activeCreatureId);assert.equal(await screen.locator('[data-device-action=release-review]').isDisabled(),true);
  await back();const released=full.state.collection.find(member=>member.id!==full.state.activeCreatureId);await member(released.id);await choose('release-review');await choose('release-confirmation');await geometry('release-waiting-encounter');await back();assert.deepEqual(await http(),full);assert.equal(posts.length,noWrite);await choose('release-confirmation');
  let committed,finish;
  await page.route('**/api/save-sync',async route=>{const response=await route.fetch();committed=await response.json();await new Promise(resolve=>{finish=resolve;});await route.abort();},{times:1});
  const box=await right.boundingBox();await page.mouse.click(box.x+box.width/2,box.y+box.height/2);
  for(let n=0;n<100&&!finish;n++)await page.waitForTimeout(10);assert.ok(finish);await page.mouse.click(box.x+box.width/2,box.y+box.height/2);finish();await onScreen('saving');await page.waitForFunction(()=>!document.querySelector('[data-device-action=retry]').disabled);
  const pending=await page.evaluate(key=>localStorage.getItem(key),pendingKey),request=posts.at(-1).raw;assert.equal(posts.length,noWrite+1);await back();assert.equal(await page.evaluate(key=>localStorage.getItem(key),pendingKey),pending);
  const storage=await context.storageState();await context.close();await new Promise(resolve=>app.server.close(resolve));app.close();app=await startServer({dataDir,port,corePath,battleCorePath});await open(storage);await onScreen('saving');const retried=await command('retry');await onScreen('release-result');assert.deepEqual(retried,committed);assert.equal(posts.at(-1).raw,request);
  assert.deepEqual(encounterFields(retried.state),encounterFields(full.state));assert.deepEqual(retried.state.collection,full.state.collection.filter(member=>member.id!==released.id));await choose('release-done');await onScreen('battle');
  checks.push({name:'Full roster Make room',releaseId:released.id,activePartnerKept:true,allEncounterFieldsUnchanged:true,backNoPost:true,doubleConfirmOnePost:true,lostReplyAndRestartExact:true,returnToEncounter:true});
  history.push(event('release',released.id));simulated=native(history);
  let captureAttemptsAfterRoom=0;
  for(let turn=0;turn<30&&simulated.phase==='encounter';turn++) {
    let action;
    if(simulated.wildCaptureChance>0){action=event('capture');captureAttemptsAfterRoom++;await choose('capture',false);assert.equal(await screen.locator('[data-device-action=capture]').isDisabled(),false);}
    else {const options=['attack','magic',...(simulated.energy>=6?['heavy']:[])].map(type=>({action:event(type),next:native([...history,event(type)])}));action=options.sort((a,b)=>Number(b.next.phase==='encounter')-Number(a.next.phase==='encounter')||b.next.wildCaptureChance-a.next.wildCaptureChance||a.next.wildHp-b.next.wildHp)[0].action;}
    const next=await command(action.type);history.push(action);simulated=native(history);assert.deepEqual(next.state,simulated);
  }
  assert.ok(captureAttemptsAfterRoom>0,'freeing one slot re-enables actual capture');assert.equal(simulated.phase,'home');
  checks.push({name:'Capture after Make room',captureAttempts:captureAttemptsAfterRoom,captured:simulated.collection.some(member=>member.id===full.state.nextMemberId),newMemberId:simulated.collection.some(member=>member.id===full.state.nextMemberId)?full.state.nextMemberId:null,oldReleasedIdNotReused:simulated.collection.every(member=>member.id!==released.id)});
  await home();await menu('explore');
  assert.equal(await screen.getAttribute('data-queued-encounters'),String(simulated.queuedEncounters));assert.equal(await screen.getAttribute('data-steps-to-next-encounter'),'0');assert.match(await screen.textContent(),/next real step/);await geometry('queued-exploration');
  const walked=await command('walk');assert.equal(walked.state.steps,simulated.steps+100);assert.deepEqual(posts.at(-1).body.events,[event('walk',100)]);assert.equal(walked.state.phase,'encounter');
  checks.push({name:'Queued credit',queued:simulated.queuedEncounters,remaining:0,actualNextInput:100,noFabricatedStepOrAutoStart:true});
  // Finish, refill through real captures, then verify Auto Make room before start.
  history.push(event('walk',100));simulated=native(history);suffix=[];for(let n=0;n<40&&simulated.phase==='encounter';n++)append('attack');append('mode',1);
  for(let fight=0;fight<60&&simulated.collection.length<8;fight++){for(let n=simulated.recoveryRestCount;n>0;n--)append('rest');append('walk',100);append('auto');}
  assert.equal(simulated.collection.length,8);for(let n=simulated.recoveryRestCount;n>0;n--)append('rest');append('walk',100);await seed(suffix);await page.reload();await onScreen('wild-auto-confirm');await choose('make-room',false);await geometry('auto-before-start-make-room');
  const beforeAuto=await http(),count=posts.length;await choose('make-room');await member(beforeAuto.state.collection.find(member=>member.id!==beforeAuto.state.activeCreatureId).id);await choose('release-review');await choose('release-confirmation');await back();assert.deepEqual(await http(),beforeAuto);assert.equal(posts.length,count);
  await choose('release-confirmation');await command('confirm-release');await onScreen('release-result');await choose('release-done');await onScreen('wild-auto-confirm');
  const auto=await command('wild-auto-start');await onScreen('wild-auto-result');assert.ok(auto.autoTrace);assert.equal(posts.at(-1).body.events[0].type,'auto');
  await page.emulateMedia({reducedMotion:'no-preference'});await page.reload();await onScreen('wild-auto-result');const playedBefore=await http(),playedPosts=posts.length;await choose('wild-auto-replay');await onScreen('wild-auto-progress');assert.equal(await right.isDisabled(),true);assert.equal(await screen.locator('button[data-device-action]').count(),0);
  const press=async control=>{const b=await control.boundingBox();await page.mouse.click(b.x+b.width/2,b.y+b.height/2);};await press(right);await press(left);await back();await page.waitForFunction(()=>document.querySelector('#device-ui').dataset.screen==='wild-auto-result',null,{timeout:40000});assert.equal(posts.length,playedPosts);assert.deepEqual(await http(),playedBefore);
  checks.push({name:'Auto remains explicit and strict',makeRoomBeforeStart:true,noCommandsDuringPlayback:true,noDuplicateRewards:true,steps:auto.autoTrace.steps.length,outcome:auto.autoTrace.outcome});
  assert.deepEqual(errors,[]);for(const binary of binaries)assert.equal(digest(binary.path),binary.sha256);
  const report={result:'PASS',binaries,twoButtonsOnly:true,checks,sizes,screenshots,pageErrors:0,fixtureNote:'Legitimate native event histories accelerated setup; no gameplay state or RNG injection. Public screenshots contain only original artwork/placeholders.',physicalSizeNote:'Nominal CSS sizes only; no calibrated physical LCD claim.'};
  writeFileSync(process.env.PARK_REPORT || 'docs/evidence/park-browser-encounters.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
}finally{await browser.close();await new Promise(resolve=>app.server.close(resolve));app.close();rmSync(dataDir,{recursive:true,force:true});}

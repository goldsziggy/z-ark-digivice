// Real native histories accelerate fixture setup; gameplay uses the two device
// buttons. Full-roster guards also receive disabled accessibility activation and
// an inert screen click. No HP, collection, RNG or rarity is fabricated.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { gameReady, autoCheckpoint, resumeFighting, exploreEncounter } from './manual-auto-browser-tools.mjs';
import { holdDeviceBack } from './browser-tools.mjs';
import { CAPTURE_RING } from '../web/capture-ring.js';
import { orderedMembers } from '../web/party.js';

const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const binaries = [corePath, battleCorePath].map(path => ({ path, sha256: digest(path) }));
const sources = ['web/app.js', 'web/party.js', 'web/auto-battle.js', 'web/game-message.js', 'web/walking-state.js', 'web/starter-onboarding.js',
  'web/progression.js', 'web/roster-client.js', 'web/form-art.js', 'web/device-screen.js', 'service/server.ts', 'tests/browser-park-encounters.mjs']
  .map(path => ({ path, sha256: digest(path) }));
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-park-encounters-'));
let app = await startServer({ seedSource: () => 12345, dataDir, port: 0, corePath, battleCorePath });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
let context, page, screen, left, right, identity;
const posts = [], errors = [], checks = [], sizes = [], screenshots = [], artRequests = [];
const capacity = 60;
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
  await context.addInitScript(() => {
    const arc = CanvasRenderingContext2D.prototype.arc;
    CanvasRenderingContext2D.prototype.arc = function (...args) {
      if (this.canvas.id === 'display' && args[0] === 206 && args[1] === 176 && this.lineWidth === 7)
        window.__parkRing = { radius: args[2], time: performance.now() };
      return Reflect.apply(arc, this, args);
    };
  });
  page = await context.newPage(); page.setDefaultTimeout(12000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (/\/api\/roster\/art\//.test(request.url())) artRequests.push(new URL(request.url()).pathname); });
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
  await choose(id, false);
  if (id === 'capture') {
    await right.click(); await onScreen('capture-aim'); await gameReady(page);
    const before = await http(), target = CAPTURE_RING.targetRadii[before.state.wildFormId % 4];
    // Observe the actual painted ring and use the real right-button DOWN. No
    // saved chance, timestamp or product handler is injected by this test.
    await page.waitForFunction(target => {
      const ring = window.__parkRing;
      return ring && performance.now() - ring.time < 150 && Math.abs(ring.radius - target) < 3
        && !document.querySelector('#device-confirm-button').disabled;
    }, target);
  }
  const reply = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.request().method() === 'POST' && r.status() === 200);
  await right.click(); const saved = await (await reply).json();
  await gameReady(page);
  if (id === 'capture') {
    const events = posts.at(-1).body.events;
    assert.equal(events.length, 1); assert.equal(events[0].type, 'ring-capture');
    if (saved.state.phase === 'encounter') { await back(); await onScreen('battle'); }
  }
  return saved;
}
async function member(id) {
  for (let n = 0; n < 20; n++) { if (await screen.locator(`[data-device-action="member-${id}"]`).count()) { await choose(`member-${id}`); await onScreen('companion'); return; } await choose('companions-next'); }
  throw new Error(`Missing carried member ${id}`);
}
async function auditSixtyPages(full) {
  const seen = [], durations = [], beforePosts = posts.length, beforeArt = artRequests.length;
  let maxRows = 0, maxPortraits = 0;
  for (let index = 0; index < 20; index++) {
    assert.match(await screen.textContent(), new RegExp(`PAGE ${index + 1} / 20`));
    const rows = await screen.locator('[data-member-id]').evaluateAll(nodes => nodes.map(node => Number(node.dataset.memberId)));
    assert.equal(rows.length, 3); seen.push(...rows); maxRows = Math.max(maxRows, rows.length);
    maxPortraits = Math.max(maxPortraits, await screen.locator('canvas.roster-portrait').count());
    assert.equal(await page.locator('#collection-grid canvas').count(), 0, 'closed developer panel owns no hidden portraits');
    const started = performance.now(); await choose('companions-next'); durations.push(performance.now() - started);
  }
  assert.deepEqual(seen, orderedMembers(full.state).map(member => member.id));
  assert.equal(new Set(seen).size, 60); assert.ok(maxPortraits <= 3);
  assert.match(await screen.textContent(), /PAGE 1 \/ 20/);
  assert.equal(posts.length, beforePosts); assert.deepEqual(await http(), full);
  assert.equal(artRequests.length, beforeArt, 'paging does not preload carried sprite packs');
  // The optional tools still show every textual record, but only visible small
  // portraits are painted and closing the panel releases all of its canvases.
  await page.locator('#playtest-tools').evaluate(details => { details.open = true; });
  await page.waitForFunction(() => document.querySelectorAll('#collection-grid [data-member-id]').length === 60);
  await page.locator('#collection-grid [data-member-id]').first().scrollIntoViewIfNeeded();
  await page.waitForFunction(() => document.querySelectorAll('#collection-grid [data-portrait-painted]').length > 0);
  const paintedAtTop = await page.locator('#collection-grid [data-portrait-painted]').count(); assert.ok(paintedAtTop < 60);
  await page.locator('#collection-grid [data-member-id]').last().scrollIntoViewIfNeeded();
  await page.waitForFunction(() => document.querySelector('#collection-grid [data-member-id]:last-child [data-portrait-painted]'));
  const paintedAtBottom = await page.locator('#collection-grid [data-portrait-painted]').count(); assert.ok(paintedAtBottom < 60);
  await page.locator('#playtest-tools').evaluate(details => { details.open = false; });
  await page.waitForFunction(() => document.querySelectorAll('#collection-grid canvas').length === 0);
  await page.locator('#device-shell').scrollIntoViewIfNeeded();
  durations.sort((a,b) => a-b);
  checks.push({name:'Sixty carried Digimon across twenty bounded pages',pages:20,uniqueMembers:seen.length,maxRenderedRows:maxRows,maxRoundPortraits:maxPortraits,closedToolsPortraits:0,newSpriteRequests:artRequests.length-beforeArt,
    visibleToolsPortraits:{top:paintedAtTop,bottom:paintedAtBottom},pageNavigationMs:{median:durations[10],max:durations.at(-1)},commands:0,stateJsonBytes:Buffer.byteLength(JSON.stringify(full.state)),
    measurement:'Headless desktop browser DOM counts and actual two-button navigation latency, not device RAM or display timing.'});
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
  await seed([event('hatch', 1)]); await http('/api/world/seed', {});
  // Find an actual eligible encounter; choosing setup moves is fixture generation, not
  // a player-facing policy or a test-only RNG/health override.
  const history = [event('hatch', 1), event('world-seed', 12345)]; let simulated = native(history), missed;
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
  await seed(history.slice(2));await open();await onScreen('battle');
  assert.equal(await screen.getAttribute('data-encounter-rarity'),missed.before.wildRarity);
  await choose('capture',false);assert.doesNotMatch(await screen.locator('[data-selected=true]').textContent(),/\d+%/);assert.match(await screen.locator('[data-selected=true]').textContent(),/Green gives full eligible odds/);await geometry('capture-rarity');
  const firstThrow=await command('capture');history.push(...posts.at(-1).body.events);simulated=native(history);assert.deepEqual(firstThrow.state,simulated);
  checks.push({name:'Native graded capture',chance:firstThrow.state.lastCapture.chance,rarity:missed.before.wildRarity,hpBefore:missed.before.hp,hpAfter:firstThrow.state.hp,oneTimingCommand:true,nativeStateMatches:true});
  // Finish through valid commands, then fill the carried team with native Auto.
  let suffix=[];
  const append=(type,value=0)=>{const action=event(type,value);history.push(action);suffix.push(action);simulated=native(history);};
  for(let n=0;n<40&&simulated.phase==='encounter';n++)append('attack');
  append('mode',1);
  for(let fight=0;fight<600&&simulated.collection.length<capacity;fight++){for(let n=simulated.recoveryRestCount;n>0;n--)append('rest');append('walk',100);append('auto');}
  assert.equal(simulated.collection.length,capacity);append('mode',0);for(let n=simulated.recoveryRestCount;n>0;n--)append('rest');append('walk',200);
  await seed(suffix);let full=await http();assert.deepEqual(full.state,simulated);await page.reload();await onScreen('battle');
  assert.equal(full.state.queuedEncounters,1);assert.equal(full.state.stepsToNextEncounter,0);
  assert.equal(full.state.wildCaptureChance,0);
  assert.equal(await screen.locator('[data-device-action=capture]').isDisabled(),true);
  const fullPosts=posts.length;await screen.locator('[data-device-action=capture]').evaluate(button=>button.click());
  const surface=await page.locator('#screen-surface').boundingBox();await page.mouse.click(surface.x+surface.width/2,surface.y+surface.height/2);
  await page.waitForTimeout(200);assert.equal(await page.locator('#screen-surface').getAttribute('data-capture-aim'),'false');assert.equal(posts.length,fullPosts);assert.deepEqual(await http(),full);
  checks.push({name:'Full roster cannot throw',count:capacity,capacity,commands:0,attemptsRngRevisionAndMembersUnchanged:true,aimNeverArmed:true});
  await choose('make-room',false);await geometry('full-roster-make-room');
  const noWrite=posts.length;await choose('make-room');await onScreen('companions');await auditSixtyPages(full);await member(full.state.activeCreatureId);assert.equal(await screen.locator('[data-device-action=release-review]').isDisabled(),true);
  await back();const released=full.state.collection.find(member=>member.id!==full.state.activeCreatureId);await member(released.id);await choose('release-review');await choose('release-confirmation');await geometry('release-waiting-encounter');await back();assert.deepEqual(await http(),full);assert.equal(posts.length,noWrite);await choose('release-confirmation');
  let committed,finish;
  await page.route('**/api/save-sync',async route=>{const response=await route.fetch();committed=await response.json();await new Promise(resolve=>{finish=resolve;});await route.abort();},{times:1});
  const box=await right.boundingBox();await page.mouse.click(box.x+box.width/2,box.y+box.height/2);
  for(let n=0;n<100&&!finish;n++)await page.waitForTimeout(10);assert.ok(finish);await page.mouse.click(box.x+box.width/2,box.y+box.height/2);finish();await onScreen('saving');await page.waitForFunction(()=>!document.querySelector('[data-device-action=retry]').disabled);
  const pending=await page.evaluate(key=>localStorage.getItem(key),pendingKey),request=posts.at(-1).raw;assert.equal(posts.length,noWrite+1);await back();assert.equal(await page.evaluate(key=>localStorage.getItem(key),pendingKey),pending);
  const storage=await context.storageState();await context.close();await new Promise(resolve=>app.server.close(resolve));app.close();app=await startServer({seedSource:()=>12345,dataDir,port,corePath,battleCorePath});await open(storage);await onScreen('saving');const retried=await command('retry');await onScreen('release-result');assert.deepEqual(retried,committed);assert.equal(posts.at(-1).raw,request);
  assert.deepEqual(encounterFields(retried.state),encounterFields(full.state));assert.deepEqual(retried.state.collection,full.state.collection.filter(member=>member.id!==released.id));await choose('release-done');await onScreen('battle');
  checks.push({name:'Full roster Make room',releaseId:released.id,activePartnerKept:true,allEncounterFieldsUnchanged:true,backNoPost:true,doubleConfirmOnePost:true,lostReplyAndRestartExact:true,returnToEncounter:true});
  history.push(event('release',released.id));simulated=native(history);
  let captureAttemptsAfterRoom=0;
  for(let turn=0;turn<30&&simulated.phase==='encounter';turn++) {
    let action;
    if(simulated.wildCaptureChance>0){action=event('capture');captureAttemptsAfterRoom++;await choose('capture',false);assert.equal(await screen.locator('[data-device-action=capture]').isDisabled(),false);}
    else {const options=['attack','magic',...(simulated.energy>=6?['heavy']:[])].map(type=>({action:event(type),next:native([...history,event(type)])}));action=options.sort((a,b)=>Number(b.next.phase==='encounter')-Number(a.next.phase==='encounter')||b.next.wildCaptureChance-a.next.wildCaptureChance||a.next.wildHp-b.next.wildHp)[0].action;}
    const next=await command(action.type);history.push(...posts.at(-1).body.events);simulated=native(history);assert.deepEqual(next.state,simulated);
  }
  assert.ok(captureAttemptsAfterRoom>0,'freeing one slot re-enables actual capture');assert.equal(simulated.phase,'home');
  checks.push({name:'Capture after Make room',captureAttempts:captureAttemptsAfterRoom,captured:simulated.collection.some(member=>member.id===full.state.nextMemberId),newMemberId:simulated.collection.some(member=>member.id===full.state.nextMemberId)?full.state.nextMemberId:null,oldReleasedIdNotReused:simulated.collection.every(member=>member.id!==released.id)});
  await home();await menu('explore');
  assert.equal(await screen.getAttribute('data-queued-encounters'),String(simulated.queuedEncounters));assert.equal(await screen.getAttribute('data-steps-to-next-encounter'),'0');assert.match(await screen.textContent(),/next real step/);await geometry('queued-exploration');
  const walkPosts=posts.length,{saved:walked,inputs:walkInputs}=await exploreEncounter(command);assert.equal(walked.state.walking.eligibleSteps,simulated.walking.eligibleSteps+100*walkInputs);assert.equal(walked.state.steps,simulated.steps);assert.ok(posts.slice(walkPosts).every(p=>JSON.stringify(p.body.events)===JSON.stringify([event('explore',100)])));assert.equal(walked.state.phase,'encounter');assert.equal(walked.state.queuedEncounters,simulated.queuedEncounters);
  checks.push({name:'Queued credit',queued:simulated.queuedEncounters,remaining:0,actualInputs:walkInputs,stepsPerInput:100,legacyCreditRetained:true,noFabricatedStepOrAutoStart:true});
  // Finish, refill through real captures, then verify Auto Make room before start.
  history.push(...Array.from({length:walkInputs},()=>event('explore',100)));simulated=native(history);suffix=[];for(let n=0;n<40&&simulated.phase==='encounter';n++)append('attack');append('mode',1);
  for(let fight=0;fight<600&&simulated.collection.length<capacity;fight++){for(let n=simulated.recoveryRestCount;n>0;n--)append('rest');append('walk',100);append('auto');}
  assert.equal(simulated.collection.length,capacity);for(let n=simulated.recoveryRestCount;n>0;n--)append('rest');append('walk',100);await seed(suffix);await page.reload();await onScreen('wild-auto-confirm');await choose('make-room',false);await geometry('auto-before-start-make-room');
  assert.match(await screen.textContent(),/Collection full.*without a capture pause/s);
  const fullAuto=await http(),fullAutoPosts=posts.length;await page.waitForTimeout(200);assert.equal(await page.locator('#screen-surface').getAttribute('data-capture-aim'),'false');assert.equal(posts.length,fullAutoPosts);assert.deepEqual(await http(),fullAuto);
  checks.push({name:'Full Auto explains capture limit before explicit Start',noAutomaticCommands:true,aimNeverArmed:true});
  const beforeAuto=await http(),count=posts.length;await choose('make-room');await member(beforeAuto.state.collection.find(member=>member.id!==beforeAuto.state.activeCreatureId).id);await choose('release-review');await choose('release-confirmation');await back();assert.deepEqual(await http(),beforeAuto);assert.equal(posts.length,count);
  await choose('release-confirmation');await command('confirm-release');await onScreen('release-result');await choose('release-done');await onScreen('wild-auto-confirm');
  let auto=await command('wild-auto-start');await autoCheckpoint(page);assert.ok(auto.autoTrace);assert.equal(posts.at(-1).body.events[0].type,'auto-fight');
  if(auto.state.autoCapture===1){const pauseSave=await http(),pausePosts=posts.length;await page.waitForTimeout(700);assert.deepEqual(await http(),pauseSave);assert.equal(posts.length,pausePosts);await geometry('auto-capture-pause');auto=await resumeFighting(page,back);}
  await page.emulateMedia({reducedMotion:'no-preference'});await page.reload();await onScreen('wild-auto-result');const playedBefore=await http(),playedPosts=posts.length;await choose('wild-auto-replay');await onScreen('wild-auto-progress');assert.deepEqual(await screen.locator('button[data-device-action]').evaluateAll(nodes=>nodes.map(n=>n.dataset.deviceAction).sort()),['auto-finish','auto-pause']);
  const press=async control=>{const b=await control.boundingBox();await page.mouse.click(b.x+b.width/2,b.y+b.height/2);};await press(right);await page.waitForTimeout(750);const pausedStep=await screen.getAttribute('data-auto-step');await page.waitForTimeout(750);assert.equal(await screen.getAttribute('data-auto-step'),pausedStep);await press(right);await back();await page.waitForFunction(()=>document.querySelector('#device-ui').dataset.screen==='wild-auto-result',null,{timeout:40000});assert.equal(posts.length,playedPosts);assert.deepEqual(await http(),playedBefore);
  checks.push({name:'Auto remains explicit and strict',makeRoomBeforeStart:true,noCommandsDuringPlayback:true,noDuplicateRewards:true,steps:auto.autoTrace.steps.length,outcome:auto.autoTrace.outcome});
  const beforeLegacyRetry=await http(),legacyPending={deviceId:identity.deviceId,rulesVersion:13,baseRevision:beforeLegacyRetry.revision,batchId:'roster60-preserved-legacy13',events:[event('feed')]};
  const legacyBytes=JSON.stringify(legacyPending);await page.evaluate(({key,value})=>localStorage.setItem(key,value),{key:pendingKey,value:legacyBytes});
  await page.reload();await onScreen('saving');await choose('retry',false);
  const legacyReply=page.waitForResponse(r=>r.url().endsWith('/api/save-sync')&&r.status()===409);await right.click();await legacyReply;
  await page.waitForFunction(()=>document.querySelector('[data-device-action=recovery]')||document.querySelector('#discard-conflict')?.hidden===false);
  assert.equal(posts.at(-1).raw,JSON.stringify({rulesVersion:13,baseRevision:legacyPending.baseRevision,batchId:legacyPending.batchId,events:legacyPending.events}));
  assert.equal(await page.evaluate(key=>localStorage.getItem(key),pendingKey),legacyBytes);assert.deepEqual(await http(),beforeLegacyRetry);
  checks.push({name:'Rules13 pending action stays byte-identical through rules15 reconciliation',requestRule:13,currentRule:15,pendingBytesUnchanged:true,latestSaveUnchanged:true,noRelabelOrReplay:true});
  assert.deepEqual(errors,[]);for(const file of [...binaries,...sources])assert.equal(digest(file.path),file.sha256);
  const report={result:'PASS',binaries,sources,controls:'Two device buttons for gameplay; disabled accessibility activation and an inert screen click additionally probe full-roster guards.',checks,sizes,screenshots,pageErrors:0,fixtureNote:'Legitimate native event histories accelerated setup; no gameplay state or RNG injection. Public screenshots contain only original artwork/placeholders.',physicalSizeNote:'Nominal CSS sizes only; no calibrated physical LCD claim.'};
  writeFileSync(process.env.PARK_REPORT || 'docs/evidence/park-browser-encounters.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
}finally{await browser.close();await new Promise(resolve=>app.server.close(resolve));app.close();rmSync(dataDir,{recursive:true,force:true});}

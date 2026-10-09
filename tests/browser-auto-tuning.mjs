// Real baseline service fixtures + real current native outcomes. All gameplay
// controls use Next / held Back / Confirm; no stat, RNG or policy injection.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, writeFileSync, rmSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';
import { autoStepText } from '../web/auto-battle.js';
import { BATTLE_PENDING_KEY } from '../web/battle-client.js';

const CORE_RULES = 10, PRACTICE_RULES = 7, CARE_PENDING = 'digivice.dev.pending.v1';
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const binaries = [corePath, battleCorePath].map(path => ({ path, sha256: sha(readFileSync(path)) }));
const sourcePath = 'tests/fixtures/auto-tuning-baseline16-service.json';
const fixture = JSON.parse(readFileSync(sourcePath));
const care = fixture.care.checkpoints.afterCare, oldPractice = fixture.practice.find(entry => entry.mode === 'auto');
const identity = { deviceId: fixture.care.deviceId, token: Buffer.alloc(32, 88).toString('base64url') };
assert.equal(identity.deviceId, oldPractice.deviceId);
const oldCareBody = fixture.care.commands.find(command => command.body.events.some(event => event.type === 'auto')).body;
const oldCarePending = JSON.stringify({ deviceId: identity.deviceId, ...oldCareBody });
const oldPracticeBody = JSON.stringify(oldPractice.commands[0].body);
const oldPracticePending = JSON.stringify({ formatVersion: 1, rulesVersion: 6, deviceId: identity.deviceId, path: '/api/battle/start', body: oldPracticeBody });
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-auto-policy-browser-'));
for (const path of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, path), JSON.stringify(care.store));
for (const path of ['battle-store.json', 'battle-store.backup.json']) writeFileSync(join(dataDir, path), JSON.stringify(oldPractice.store));
let app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, reducedMotion: 'no-preference' });
page.setDefaultTimeout(12000);
await page.addInitScript(({ identity, pending, key }) => {
  if (location.protocol !== 'http:' || localStorage.getItem('auto-policy-fixture-loaded')) return;
  localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)); localStorage.setItem(key, pending);
  localStorage.setItem('auto-policy-fixture-loaded', '1');
}, { identity, pending: oldCarePending, key: CARE_PENDING });
await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
const screen = page.locator('#device-ui'), left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
const posts = [], errors = [], replays = [], geometryChecks = [], screenshots = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (request.method() === 'POST' && /\/api\/(save-sync|battle\/(start|act))$/.test(request.url())) posts.push({ path: new URL(request.url()).pathname, raw: request.postData(), body: request.postDataJSON() }); });
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
const ready = () => page.waitForFunction(() => !document.querySelector('#device-confirm-button').disabled);
async function choose(id, execute = true) {
  await ready();
  for (let index=0;index<32;index++) { if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) { if (execute) await right.click(); return; } await left.click(); }
  throw new Error(`Unreachable ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function read(path = '/api/save') { const response = await fetch(base + path, { headers: { Authorization: `Bearer ${identity.token}` } }); assert.equal(response.status, 200); return response.json(); }
async function command(id, path = '/api/save-sync', status = 200) {
  await choose(id, false); const pending = page.waitForResponse(r => r.url().endsWith(path) && r.request().method() === 'POST' && r.status() === status);
  await right.click(); return (await pending).json();
}
async function home() {
  for (let n=0;n<14&&await screen.getAttribute('data-screen')!=='home';n++) { const current=await screen.getAttribute('data-screen'); if(current==='wild-auto-result')await choose('wild-auto-done');else if(current==='battle-auto-result')await choose('practice-auto-done');else await holdDeviceBack(page); }
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await choose(id); await onScreen(id); }
async function evidence(label) {
  const previous = await page.locator('.device-screen').evaluate(e => e.style.cssText);
  for (const px of [null,201.6,140.16]) {
    if(px!==null)await page.locator('.device-screen').evaluate((e,px)=>{e.style.setProperty('box-sizing','content-box','important');e.style.setProperty('width',`${px}px`,'important');e.style.setProperty('height',`${px}px`,'important');e.style.setProperty('margin-inline','auto','important');},px);
    const result=await screen.evaluate(root=>{const r=document.querySelector('#screen-surface').getBoundingClientRect(),cx=r.x+r.width/2,cy=r.y+r.height/2;const nodes=[...root.querySelectorAll('button,.screen-detail')].filter(e=>e.getClientRects().length).map(e=>({text:e.textContent,box:e.getBoundingClientRect()})),failures=[];for(const n of nodes)if([[n.box.left,n.box.top],[n.box.right,n.box.top],[n.box.left,n.box.bottom],[n.box.right,n.box.bottom]].some(([x,y])=>Math.hypot(x-cx,y-cy)>r.width/2+2))failures.push(`clipped:${n.text}`);for(let a=0;a<nodes.length;a++)for(let b=a+1;b<nodes.length;b++){const p=nodes[a].box,q=nodes[b].box;if(Math.min(p.right,q.right)-Math.max(p.left,q.left)>1&&Math.min(p.bottom,q.bottom)-Math.max(p.top,q.top)>1)failures.push(`overlap:${nodes[a].text}/${nodes[b].text}`);}return{width:r.width,failures};});
    assert.deepEqual(result.failures,[],label);geometryChecks.push({label,width:result.width});
    if(px===null&&process.env.AUTO_TUNING_EVIDENCE==='1'){const path=`docs/evidence/auto-policy-${label}.png`;await page.locator('#screen-surface').screenshot({path});screenshots.push({path,sha256:sha(readFileSync(path)),privateCharacterArtwork:false});}
  }
  await page.locator('.device-screen').evaluate((e,css)=>{e.style.cssText=css;},previous);
}
async function rawPress(control) { const box=await control.boundingBox();await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.mouse.up(); }
async function replay(kind, trace, label) {
  const prefix=kind==='wild'?'wild':'practice',progress=kind==='wild'?'wild-auto-progress':'battle-auto-progress',result=kind==='wild'?'wild-auto-result':'battle-auto-result';
  const before=posts.length,save=await read(),battle=await read('/api/battle');
  await page.evaluate(()=>{window.__autoPolicyLines=[];window.__autoPolicyObserver?.disconnect();window.__autoPolicyObserver=new MutationObserver(()=>{const r=document.querySelector('#device-ui');if(['wild-auto-progress','battle-auto-progress'].includes(r?.dataset.screen)){const s=r.querySelector('.screen-detail')?.textContent;if(s&&!window.__autoPolicyLines.includes(s))window.__autoPolicyLines.push(s);}});window.__autoPolicyObserver.observe(document.querySelector('#device-ui'),{subtree:true,childList:true,characterData:true});});
  await choose(`${prefix}-auto-replay`);await onScreen(progress);
  assert.equal(await right.isDisabled(),true);assert.equal(await screen.locator('button[data-device-action]').count(),0);
  await rawPress(right);await rawPress(right);await rawPress(left);await holdDeviceBack(page);
  assert.equal(posts.length,before);await onScreen(result); // Page default timeout is expanded below for bounded replay.
  const lines=await page.evaluate(()=>window.__autoPolicyLines);for(const step of trace.steps)assert.ok(lines.includes(autoStepText(step,trace)),`${label}:missing recorded step ${step.turn}`);
  assert.deepEqual(await read(),save);assert.deepEqual(await read('/api/battle'),battle);assert.equal(posts.length,before);
  replays.push({label,kind,steps:trace.steps.length,noManualControls:true,noCommandsOrRewards:true,frozenNames:true});
}
async function restart() { await page.goto('about:blank');await new Promise(resolve=>app.server.close(resolve));app.close();app=await startServer({dataDir,port,corePath,battleCorePath});await page.goto(`${base}/?controls=buttons`); }
async function loseReply(id,path,pendingKey,routeName) {
  let committed;const count=posts.length;
  await page.route(`**${path}`,async route=>{const response=await route.fetch();committed=await response.json();await new Promise(resolve=>setTimeout(resolve,160));await route.abort();},{times:1});
  await choose(id);await onScreen(routeName);await ready();assert.equal(posts.length,count+1);
  const raw=await page.evaluate(key=>localStorage.getItem(key),pendingKey);assert.ok(raw);
  await restart();await onScreen(routeName);await ready();assert.equal(posts.length,count+1);assert.equal(await page.evaluate(key=>localStorage.getItem(key),pendingKey),raw);
  return{committed,raw,count};
}
try {
  mkdirSync('docs/evidence',{recursive:true});
  page.setDefaultTimeout(45000); // Maximum40-step presentation is650ms/step.
  await page.goto(`${base}/?controls=buttons`);await onScreen('saving');await ready();const migrated=await read();
  assert.equal(migrated.state.rulesVersion,CORE_RULES);assert.equal(migrated.state.schemaVersion,13);assert.deepEqual(migrated.autoTrace,care.response.body.autoTrace);assert.equal(migrated.revision,care.response.body.revision);
  assert.equal(posts.length,0);const rejected=await command('retry','/api/save-sync',409);assert.equal(rejected.error,'migration_required');await ready();
  assert.deepEqual(posts[0].body,oldCareBody);assert.equal(await page.evaluate(key=>localStorage.getItem(key),CARE_PENDING),oldCarePending);assert.deepEqual(await read(),migrated);
  await choose('open-recovery');await choose('recovery-confirm');const rejectedCount=posts.length;await holdDeviceBack(page);assert.equal(posts.length,rejectedCount);assert.equal(await page.evaluate(key=>localStorage.getItem(key),CARE_PENDING),oldCarePending);
  await choose('recovery-confirm');await choose('discard-local');await onScreen('wild-auto-result');assert.deepEqual(await read(),migrated);await replay('wild',migrated.autoTrace,'frozen-care8');await evidence('frozen-care8');
  await page.evaluate(({key,raw})=>localStorage.setItem(key,raw),{key:BATTLE_PENDING_KEY,raw:oldPracticePending});await page.reload();await onScreen('battle-resolve');await ready();const old=await command('practice-retry','/api/battle/start');await onScreen('battle-auto-result');
  assert.deepEqual(old,oldPractice.expectedCurrent);assert.equal(posts.at(-1).raw,oldPracticeBody);await replay('practice',old.autoTrace,'frozen-practice6');
  await menu('explore');const beforeWalk=posts.length;await command('walk');await onScreen('wild-auto-confirm');const encounter=await read();assert.equal(encounter.state.wildRules,CORE_RULES);assert.equal(posts.length,beforeWalk+1);
  const beforeConfirm=posts.length;await holdDeviceBack(page);assert.equal(posts.length,beforeConfirm);assert.deepEqual(await read(),encounter);await home();await choose('wild-auto-confirm');await onScreen('wild-auto-confirm');
  const wildLost=await loseReply('wild-auto-start','/api/save-sync',CARE_PENDING,'saving');assert.equal(JSON.parse(wildLost.raw).rulesVersion,CORE_RULES);
  const wild=await command('retry');await onScreen('wild-auto-result');assert.deepEqual(wild,wildLost.committed);assert.equal(posts.at(-1).raw,posts.at(-2).raw);await replay('wild',wild.autoTrace,'current-care10');await evidence('current-wild');
  const pet=await read();await menu('battle-mode');await choose('practice-start');const beforeMode=posts.length;await choose('practice-auto');await onScreen('battle-auto-confirm');await holdDeviceBack(page);assert.equal(posts.length,beforeMode);
  await choose('practice-tactical',false);const tactical=await command('practice-tactical','/api/battle/start');await onScreen('battle-choice');assert.equal(tactical.battle.rulesVersion,PRACTICE_RULES);await holdDeviceBack(page);await command('practice-retreat','/api/battle/act');await onScreen('battle-result');await choose('practice-continue');
  await choose('practice-start');await choose('practice-auto');await onScreen('battle-auto-confirm');
  const practiceLost=await loseReply('practice-auto-start','/api/battle/start',BATTLE_PENDING_KEY,'battle-resolve');assert.equal(JSON.parse(practiceLost.raw).rulesVersion,PRACTICE_RULES);
  const practice=await command('practice-retry','/api/battle/start');await onScreen('battle-auto-result');assert.deepEqual(practice,practiceLost.committed);assert.equal(practice.battle.rulesVersion,PRACTICE_RULES);assert.equal(practice.battle.maxExchanges,40);assert.equal(posts.at(-1).raw,posts.at(-2).raw);
  await replay('practice',practice.autoTrace,'current-practice7');await evidence('current-practice');assert.deepEqual(await read(),pet);assert.deepEqual(errors,[]);
  for(const binary of binaries)assert.equal(sha(readFileSync(binary.path)),binary.sha256,'Native build changed during browser verification');
  const report={result:'PASS',careRules:CORE_RULES,practiceRules:PRACTICE_RULES,binaries,baseline:{path:sourcePath,sha256:sha(readFileSync(sourcePath)),sourceCommit:fixture.sourceCommit,oldCare8PendingRejectedWithoutReplay:true,oldPractice6ReceiptExact:true},replays,newWild:{outcome:wild.autoTrace.outcome,steps:wild.autoTrace.steps.length},newPractice:{outcome:practice.autoTrace.outcome,steps:practice.autoTrace.steps.length},bothLostRepliesRestartedAndRetriedExactly:true,practiceLeavesCareUnchanged:true,twoButtonsOnly:true,geometryChecks,screenshots,pageErrors:0,limits:'Cannondramon historical practice is a genuine service fixture; current fights use the selected saved Impmon. This checks presentation/commands, not tactical optimality or calibrated physical LCD readability.'};
  writeFileSync(process.env.AUTO_TUNING_REPORT || 'docs/evidence/auto-policy-browser-verification.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
}finally{await browser.close();await new Promise(resolve=>app.server.close(resolve));app.close();rmSync(dataDir,{recursive:true,force:true});}

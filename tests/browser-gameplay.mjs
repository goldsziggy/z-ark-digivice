// Real native outcomes through two-button UI. Legal HTTP events only accelerate
// training; no stat/RNG injection. Screenshots use original placeholders.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';
import { autoStepText } from '../web/auto-battle.js';

const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-gameplay-'));
const corePath = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
const battleCorePath = resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle');
let app = await startServer({ dataDir, port: 0, corePath, battleCorePath });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const browser = await chromium.launch({ headless: true, executablePath: process.env.PLAYWRIGHT_CHROMIUM });
const page = await browser.newPage({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' });
page.setDefaultTimeout(10000);
const screen = page.locator('#device-ui'), left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
const errors = [], posts = [], shots = [], geometryChecks = [], captureChances = [], recoveryObservations = [];
let identity;
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (request.method() === 'POST' && /\/api\/(save-sync|battle\/(act|start))$/.test(request.url())) posts.push({ path: new URL(request.url()).pathname, body: request.postDataJSON() }); });
await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
const onScreen = value => page.waitForFunction(value => document.querySelector('#device-ui')?.dataset.screen === value, value);
async function choose(id, execute = true) {
  for (let i = 0; i < 32; i++) {
    if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) { if (execute) await right.click(); return; }
    await left.click();
  }
  throw new Error(`Cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function back() { await holdDeviceBack(page); }
async function home() {
  const state = await screen.getAttribute('data-screen');
  if (state === 'evolution-result') await choose('evolution-home');
  if (state === 'wild-auto-result') await choose('wild-auto-done');
  if (state === 'battle-auto-result') await choose('practice-auto-done');
  for (let i = 0; i < 14 && await screen.getAttribute('data-screen') !== 'home'; i++) await back();
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await choose(id); await onScreen(id); }
async function http(path = '/api/save', body) {
  const response = await fetch(base + path, { method: body ? 'POST' : 'GET', headers: { Authorization: `Bearer ${identity.token}`, ...(body ? { 'Content-Type': 'application/json' } : {}) }, ...(body ? { body: JSON.stringify(body) } : {}) });
  assert.equal(response.status, 200, `${path}: ${await response.clone().text()}`); return response.json();
}
async function seed(events) { const before = await http(); return http('/api/save-sync', { rulesVersion: before.state.rulesVersion, baseRevision: before.revision, batchId: crypto.randomUUID(), events }); }
async function command(id, endpoint = '/api/save-sync') {
  await choose(id, false);
  const received = page.waitForResponse(r => r.url().endsWith(endpoint) && r.request().method() === 'POST' && r.status() === 200);
  await right.click(); const result = await (await received).json();
  await page.waitForFunction(() => !document.querySelector('#device-confirm-button').disabled);
  return result;
}
function predict(save, type) {
  assert.equal(save.baseSequence, 0);
  try { return JSON.parse(execFileSync(corePath, ['--replay-onboarding', String(save.seed)], { input: [...save.events, { type, value: 0 }].map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', timeout: 3000, maxBuffer: 65536, stdio: ['pipe', 'pipe', 'pipe'] })); } catch { return null; }
}
async function geometry(label) {
  const result = await screen.evaluate(root => {
    const r = document.querySelector('#screen-surface').getBoundingClientRect(), x = r.x + r.width / 2, y = r.y + r.height / 2;
    const nodes = [...root.querySelectorAll('button,.screen-detail')].filter(e => e.getClientRects().length).map(e => ({ text: e.textContent, box: e.getBoundingClientRect() }));
    const failures = [];
    for (const n of nodes) if ([[n.box.left,n.box.top],[n.box.right,n.box.top],[n.box.left,n.box.bottom],[n.box.right,n.box.bottom]].some(([a,b])=>Math.hypot(a-x,b-y)>r.width/2+2)) failures.push(`clipped:${n.text}`);
    for(let a=0;a<nodes.length;a++)for(let b=a+1;b<nodes.length;b++){const p=nodes[a].box,q=nodes[b].box;if(Math.min(p.right,q.right)-Math.max(p.left,q.left)>1&&Math.min(p.bottom,q.bottom)-Math.max(p.top,q.top)>1)failures.push(`overlap:${nodes[a].text}/${nodes[b].text}`);}
    return { failures, width: r.width };
  });
  assert.deepEqual(result.failures, [], label); geometryChecks.push({ label, width: result.width });
}
async function evidence(label) {
  await geometry(`${label} mobile`);
  if (process.env.GAMEPLAY_EVIDENCE === '1') { mkdirSync('docs/evidence', { recursive: true }); const path = `docs/evidence/gameplay-${label}.png`; await page.locator('#screen-surface').screenshot({ path }); shots.push(path); }
  const previous = await page.locator('.device-screen').evaluate(e => e.style.cssText);
  for (const px of [201.6, 140.16]) { await page.locator('.device-screen').evaluate((e,px) => { e.style.setProperty('box-sizing','content-box','important'); e.style.setProperty('width',`${px}px`,'important'); e.style.setProperty('height',`${px}px`,'important'); e.style.setProperty('margin-inline','auto','important'); },px); await geometry(`${label} nominal ${px}px`); }
  await page.locator('.device-screen').evaluate((e,css) => { e.style.cssText=css; }, previous);
}
async function member(id) { await menu('companions'); for(let i=0;i<3;i++){if(await screen.locator(`[data-device-action="member-${id}"]`).count()){await choose(`member-${id}`);await onScreen('companion');return;}await choose('companions-next');}throw new Error('Member missing'); }
try {
  await page.goto(`${base}/?controls=buttons`); await onScreen('home'); await choose('connection'); await choose('start-pairing'); await choose('claim-device', false);
  const paired = page.waitForResponse(r => r.url().endsWith('/api/pairing/claim') && r.status() === 201); await right.click(); identity = await (await paired).json();
  await onScreen('starter-select'); await choose('starter-2'); await command('hatch-starter'); await onScreen('starter-hatched'); await choose('meet-starter');
  assert.equal((await http()).state.rulesVersion, 11);
  let captured, cancelled = false;
  for (let encounter = 0; encounter < 6 && !captured; encounter++) {
    await menu('care'); const recoveryBefore = (await http()).state; let restConfirmations = 0;
    for(let n=0;n<12;n++){const save=await http();if(save.state.hp===save.state.combat.maxHp&&save.state.energy>=80)break;await command('rest');restConfirmations++;}
    const recoveryAfter = (await http()).state; recoveryObservations.push({before:{hp:recoveryBefore.hp,maxHp:recoveryBefore.combat.maxHp,energy:recoveryBefore.energy},after:{hp:recoveryAfter.hp,energy:recoveryAfter.energy},restConfirmations});
    await menu('explore'); await command('walk'); await onScreen('battle');
    assert.equal((await http()).state.wildCaptureChance, 0); assert.equal(await screen.locator('[data-device-action="capture"]').isDisabled(), true);
    for(let turn=0;turn<30;turn++){
      const saved=await http(), s=saved.state;if(s.phase!=='encounter')break;
      const possible=['attack','magic','heavy'].map(type=>({type,state:predict(saved,type)})).filter(r=>r.state?.phase==='encounter'&&r.state.wildHp>0&&r.state.hp>0);
      const best=possible.sort((a,b)=>b.state.wildCaptureChance-a.state.wildCaptureChance||a.state.wildHp-b.state.wildHp)[0];
      if(s.wildCaptureChance>0&&(!best||best.state.wildCaptureChance<=s.wildCaptureChance)){
        await choose('capture',false);assert.match(await screen.locator('[data-selected="true"]').textContent(),new RegExp(`${s.wildCaptureChance}%`));captureChances.push(s.wildCaptureChance);
        if(!cancelled){await evidence('capture-chance');const count=posts.length;await right.click();await onScreen('capture');await back();await onScreen('battle');assert.equal(posts.length,count);assert.deepEqual(await http(),saved);cancelled=true;}
        const result=await command('capture');if(result.state.collection.length>1){captured=result.state.collection.at(-1);break;}
      } else {
        const type=best?.type||'attack';const label=s.combat.skills[type==='attack'?'physical':type];await choose(type,false);assert.ok((await screen.locator('[data-selected="true"]').textContent()).includes(label));await command(type);
      }
    }
  }
  assert.ok(captured,'real native encounter captured a second individual');
  await member(captured.id);const beforePartner=await http();await command('select-companion');await onScreen('companion');const partner=await http();assert.equal(partner.state.activeCreatureId,captured.id);assert.deepEqual(partner.state.collection,beforePartner.state.collection);await evidence('captured-partner');
  // Exercise a reviewed branch of the selected companion when available.
  if(!partner.state.evolution.options.length){await member(1);await command('select-companion');}
  let grown=await http();
  for(let fight=0;fight<100&&!grown.state.evolution.options.some(o=>o.eligible);fight++){
    const gate=grown.state.evolution.options[0];assert.ok(gate);
    if(grown.state.level>=gate.requiredLevel){grown=await seed([{type:'play',value:0},{type:'rest',value:0}]);continue;}
    const rest=[];for(let n=0;n<12;n++)rest.push({type:'rest',value:0});grown=await seed([...rest,{type:'mode',value:1},{type:'walk',value:100},{type:'auto',value:0}]);
  }
  const option=grown.state.evolution.options.find(o=>o.eligible);assert.ok(option,'actual care/battle history reaches legal branch');
  await page.reload();await onScreen('home');await menu('progression');await choose('evolution-options');await choose(`evolve-${option.formId}`);await choose('evolution-skills');
  for(const key of ['physical','heavy','magic']){await choose(`evo-skill-${key}`,false);assert.ok((await screen.locator('[data-selected="true"]').textContent()).includes(`${grown.state.combat.skills[key]} → ${option.combat.skills[key]}`));}
  await back();await choose('evolution-review');await onScreen('evolution-confirm');const beforeEvolution=await http();await back();assert.deepEqual(await http(),beforeEvolution);await choose('evolution-review');const evolved=await command('confirm-evolution');await onScreen('evolution-result');assert.equal(evolved.state.formId,option.formId);assert.equal(evolved.state.activeCreatureId,grown.state.activeCreatureId);await evidence('evolution');await choose('evolution-home');
  const beforePractice=await http();await menu('battle-mode');await choose('practice-start');const tactical=await command('practice-tactical','/api/battle/start');await onScreen('battle-choice');assert.equal(tactical.battle.rulesVersion,7);
  for(const type of ['physical','heavy','magic']){await choose(`practice-${type}`,false);assert.ok((await screen.locator('[data-selected="true"]').textContent()).includes(tactical.battle.playerCombat.skills[type]));}
  await evidence('tactical-named-move');const attack=await command('practice-magic','/api/battle/act');await onScreen('battle-result');assert.ok((await screen.textContent()).includes(attack.battle.playerCombat.skills.magic));await choose('practice-continue');await onScreen('battle-choice');
  const defend=await command('practice-ward','/api/battle/act');await onScreen('battle-result');assert.ok((await screen.textContent()).includes(defend.battle.enemyCombat.skills[defend.battle.lastTurn.enemyChoice]));await choose('practice-continue');await back();await command('practice-retreat','/api/battle/act');await onScreen('battle-result');await choose('practice-continue');
  await choose('practice-start');await choose('practice-auto');const auto=await command('practice-auto-start','/api/battle/start');await onScreen('battle-auto-result');assert.deepEqual(await http(),beforePractice);
  // Replay saved frozen names in normal motion; recording/DOM observation adds
  // no combat command and cannot alter the already committed outcome.
  await page.emulateMedia({reducedMotion:'no-preference'});await page.reload();await onScreen('home');await menu('battle-mode');await choose('practice-auto-result');
  const count=posts.length;await page.evaluate(()=>{window.__gameplayNarration=[];new MutationObserver(()=>{const root=document.querySelector('#device-ui');if(root?.dataset.screen==='battle-auto-progress'){const text=root.querySelector('.screen-detail')?.textContent;if(text&&!window.__gameplayNarration.includes(text))window.__gameplayNarration.push(text);}}).observe(document.querySelector('#device-ui'),{subtree:true,childList:true,characterData:true});});
  await choose('practice-auto-replay');await onScreen('battle-auto-progress');await evidence('auto-named-attack');
  const defenseStep=auto.autoTrace.steps.find(step=>step.phase==='defend');assert.ok(defenseStep);
  const defenseText=autoStepText(defenseStep,auto.autoTrace);
  await page.waitForFunction(text=>document.querySelector('#device-ui .screen-detail')?.textContent===text,defenseText);
  assert.ok(defenseText.includes(auto.autoTrace.enemy.combat.skills[defenseStep.opponentAction]));
  await evidence('auto-named-defense');
  await page.waitForFunction(()=>document.querySelector('#device-ui')?.dataset.screen==='battle-auto-result',null,{timeout:45000});
  const narration=await page.evaluate(()=>window.__gameplayNarration);for(const step of auto.autoTrace.steps)assert.ok(narration.includes(autoStepText(step,auto.autoTrace)),`Missing frozen step ${step.turn}`);
  assert.equal(posts.length,count);assert.deepEqual(await http(),beforePractice);assert.deepEqual(errors,[]);
  const report={result:'PASS',captureChances,recoveryObservations,captured:{id:captured.id,formId:captured.formId,name:captured.name},evolved:{id:evolved.state.activeCreatureId,formId:evolved.state.formId,name:evolved.state.creature},practiceRules:tactical.battle.rulesVersion,autoSteps:auto.autoTrace.steps.length,frozenNames:true,captureCancelNoPost:true,twoButtonsOnly:true,geometryChecks,screenshots:shots,pageErrors:0,physicalSizeNote:'Nominal CSS size only; not calibrated physical LCD readability.'};
  mkdirSync('docs/evidence',{recursive:true});writeFileSync(process.env.GAMEPLAY_REPORT || 'docs/evidence/gameplay-browser-verification.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
} finally {await browser.close();await new Promise(resolve=>app.server.close(resolve));app.close();rmSync(dataDir,{recursive:true,force:true});}

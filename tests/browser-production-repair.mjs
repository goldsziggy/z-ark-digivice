// Real Chromium + synthetic frozen rules12 history; no personal data or hardware.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, writeFileSync, rmSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { openPlaytestTools } from './browser-tools.mjs';
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir=mkdtempSync(join(tmpdir(),'digivice-production-repair-'));
const token=Buffer.alloc(32,58).toString('base64url'), deviceId=`dv_${'e'.repeat(24)}`;
const hash=s=>createHash('sha256').update(s).digest('hex');
const events=[{type:'hatch',value:1},{type:'mode',value:1},{type:'walk',value:100}];
const old={formatVersion:14,gameSchemaVersion:16,rulesVersion:12,devices:[{deviceId,tokenHash:hash(token),seed:12345,initialMode:'onboarding',revision:1,legacy:null,events,receipts:[{batchId:'frozen-test-battle',revision:1,eventEnd:3,bodyHash:hash(JSON.stringify({rulesVersion:12,baseRevision:0,events}))}]}]};
const retainedToken=Buffer.alloc(32,59).toString('base64url'), retainedId=`dv_${'f'.repeat(24)}`;
const retainedEvents=[...events,{type:'auto',value:0},{type:'rest',value:0}];
old.devices.push({deviceId:retainedId,tokenHash:hash(retainedToken),seed:12345,initialMode:'onboarding',revision:1,legacy:null,events:retainedEvents,receipts:[{batchId:'retained-owned-test-member',revision:1,eventEnd:5,bodyHash:hash(JSON.stringify({rulesVersion:12,baseRevision:0,events:retainedEvents}))}]});
const original=JSON.stringify(old,null,2)+'\n';
for(const name of ['store.json','store.backup.json'])writeFileSync(join(dataDir,name),original);
const app=await startServer({dataDir,port:0});
const base=`http://127.0.0.1:${app.server.address().port}`;
const browser=await chromium.launch({headless:true,...(process.env.PLAYWRIGHT_CHROMIUM?{executablePath:process.env.PLAYWRIGHT_CHROMIUM}:{})});
try {
 const read=async()=>await(await fetch(`${base}/api/save`,{headers:{authorization:`Bearer ${token}`}})).json();
 const before=await read(); assert.equal(before.revision,1);assert.equal(before.state.wildFormId,4);
 const page=await browser.newPage({viewport:{width:1200,height:1000},reducedMotion:'reduce'});page.setDefaultTimeout(15000);
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(identity=>{if(!localStorage.getItem('digivice.dev.identity.v1'))localStorage.setItem('digivice.dev.identity.v1',JSON.stringify(identity));},{deviceId,token});
 let attempts=0, firstBody;
 await page.route('**/api/save-sync',async route=>{attempts++;const body=route.request().postDataJSON();if(!firstBody)firstBody=body;else assert.deepEqual(body,firstBody);if(attempts===1)await route.fulfill({status:503,contentType:'application/json',body:JSON.stringify({error:'synthetic_offline',message:'Synthetic retry check'})});else await route.continue();});
 await page.goto(base);await openPlaytestTools(page);
 await page.locator('#retry-sync').waitFor({state:'visible'});
 assert.deepEqual(firstBody.events,[{type:'resolve-test-encounter',value:0}]);assert.equal(firstBody.rulesVersion,13);assert.equal(firstBody.baseRevision,1);
 const pending=await page.evaluate(()=>localStorage.getItem('digivice.dev.pending.v1'));assert.ok(pending);assert.deepEqual((await read()).state,before.state);
 assert.doesNotMatch(await page.locator('#device-ui').textContent(),/Flicker/);
 assert.equal(await page.locator('#battle-actions').isVisible(),false);
 const accepted=page.waitForResponse(r=>r.url().endsWith('/api/save-sync')&&r.status()===200);
 await page.locator('#retry-sync').click();await accepted;
 await page.waitForFunction(()=>localStorage.getItem('digivice.dev.pending.v1')===null);
 const after=await read();assert.equal(after.revision,2);assert.equal(after.state.phase,'home');assert.equal(after.state.wildFormId,0);
 for(const key of ['hp','energy','fullness','mood','bond','xp','captures','encounters','rngState','journal','steps'])assert.deepEqual(after.state[key],before.state[key],key);
 assert.deepEqual(after.state.collection.map(({care,...member})=>member),before.state.collection.map(({care,...member})=>member));assert.equal(after.state.care.offenseBonus,1);
 await page.reload();await openPlaytestTools(page);await page.waitForFunction(()=>document.querySelector('#revision')?.textContent==='2');
 assert.equal(attempts,2);assert.deepEqual(await read(),after);
 // A retained old ownership record cannot poison named journal pages or become
 // a selectable new partner. Read the actual legacy history, never mutate it.
 const retainedRead=async()=>await(await fetch(`${base}/api/save`,{headers:{authorization:`Bearer ${retainedToken}`}})).json();
 const retainedBefore=await retainedRead();assert.deepEqual(retainedBefore.state.journal.obtainedFormIds,[4,11]);assert.equal(retainedBefore.state.collection[1].formId,4);
 await page.evaluate(identity=>localStorage.setItem('digivice.dev.identity.v1',JSON.stringify(identity)),{deviceId:retainedId,token:retainedToken});
 await page.goto(`${base}/?controls=buttons`);await openPlaytestTools(page);await page.waitForFunction(id=>document.querySelector('#device-id')?.textContent===id,retainedId);
 assert.equal(await page.locator('[data-select-member="2"]').isDisabled(),true);
 const choose=async id=>{for(let n=0;n<32;n++){const selected=page.locator('#device-ui [data-selected="true"]');await selected.waitFor();if(await selected.getAttribute('data-device-action')===id){await page.locator('#device-confirm-button').click();return;}await page.locator('#device-back-button').click();}throw new Error(`Action not reachable: ${id}`);};
 await choose('menu');await choose('journal');await page.locator('[data-device-action="roster-form-11"]').waitFor({state:'attached'});
 assert.equal(await page.locator('[data-device-action="roster-form-4"]').count(),0);assert.match(await page.locator('#device-ui').textContent(),/1 FORMS DISCOVERED/);await choose('roster-form-11');await page.waitForFunction(()=>document.querySelector('#device-ui')?.dataset.catalogFormId==='11');
 assert.deepEqual(await retainedRead(),retainedBefore);assert.equal(attempts,2);
 assert.equal(readFileSync(join(dataDir,'store.rules-v12.json'),'utf8'),original);assert.deepEqual(errors,[]);
 console.log('PASS production browser repair: original test foe hidden, failed repair durable, exact retry once, no reward/cost, reload does not repeat, original archive unchanged; retained test ownership cannot be selected or break the named journal; no page errors.');
} finally {await browser.close();await new Promise((resolve,reject)=>app.server.close(e=>e?reject(e):resolve()));app.close();rmSync(dataDir,{recursive:true,force:true});}

// Read-only navigation against a native-backed fixture. Optional screenshots
// show real UI state; private art is not required for the meter/target checks.
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { namedFixture } from './legacy-fixture.ts';
if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir=mkdtempSync(join(tmpdir(),'digivice-care-browser-'));
const fixture=namedFixture(),identity=fixture.identities[0];
for(const name of ['store.json','store.backup.json'])writeFileSync(join(dataDir,name),JSON.stringify(fixture.store));
const app=await startServer({dataDir,port:0,corePath:resolve(process.env.DIGIVICE_TEST_CORE_PATH||'build/digivice-core'),battleCorePath:process.env.DIGIVICE_TEST_BATTLE_PATH});
const base=`http://127.0.0.1:${app.server.address().port}`;
const browser=await chromium.launch({headless:true,...(process.env.PLAYWRIGHT_CHROMIUM?{executablePath:process.env.PLAYWRIGHT_CHROMIUM}:{})});
const errors=[],results=[];
const readSave=async()=>{const response=await fetch(`${base}/api/save`,{headers:{Authorization:`Bearer ${identity.token}`}});assert(response.ok);return response.json();};
try {
 for(const diameter of [412,480]) {
  const context=await browser.newContext({viewport:{width:1100,height:1200},hasTouch:true,reducedMotion:'reduce'});
  await context.addInitScript(identity=>localStorage.setItem('digivice.dev.identity.v1',JSON.stringify(identity)),identity);
  const page=await context.newPage();page.setDefaultTimeout(8000);page.on('pageerror',error=>errors.push(error.message));
  const writes=[];page.on('request',r=>{if(r.method()==='POST'&&r.url().includes('/api/'))writes.push(r.url());});
  await page.goto(base);await page.waitForFunction(()=>document.querySelector('#device-ui')?.dataset.screen==='home');
  await page.locator('.device-screen').evaluate((node,size)=>{node.style.setProperty('box-sizing','content-box','important');for(const key of ['width','height'])node.style.setProperty(key,`${size}px`,'important');node.style.setProperty('max-width','none','important');},diameter);
  const action=id=>page.locator(`[data-device-action="${id}"]`).filter({visible:true});
  const saved=await readSave(),writeCount=writes.length;
  await action('care').tap();await page.waitForFunction(()=>document.querySelector('#device-ui')?.dataset.screen==='care');
  const meters=await page.locator('.care-bars progress').evaluateAll(nodes=>nodes.map(n=>({value:n.value,max:n.max,label:n.getAttribute('aria-label')})));
  assert.equal(meters.length,4);assert.match(meters[0].label,/Health: \d+ of \d+ HP/);
  const numbers=meters[0].label.match(/Health: (\d+) of (\d+) HP/).slice(1).map(Number);
  assert.deepEqual([meters[0].value,meters[0].max],numbers);
  assert.deepEqual(numbers,[saved.state.hp,saved.state.combat.maxHp],'Meter uses active partner health from native save');
  assert.deepEqual(meters.slice(1).map(m=>m.label),['Food','Mood','Energy']);
  const geometry=await page.locator('#device-ui').evaluate(root=>{
   const c=root.getBoundingClientRect(),cx=c.x+c.width/2,cy=c.y+c.height/2,r=c.width/2;
   const rect=node=>{const b=node.getBoundingClientRect();return {x:b.x-c.x,y:b.y-c.y,w:b.width,h:b.height,inside:[[b.left,b.top],[b.right,b.top],[b.left,b.bottom],[b.right,b.bottom]].every(([x,y])=>Math.hypot(x-cx,y-cy)<=r+1)};};
   return {panel:rect(root.querySelector('.care-bars')),feedback:root.querySelector('.screen-detail') ? rect(root.querySelector('.screen-detail')) : null,needs:[...root.querySelectorAll('.care-bars [data-need]')].map(rect),buttons:[...root.querySelectorAll('button')].filter(n=>n.getClientRects().length&&!n.hidden).map(rect)};
  });
  assert(geometry.panel.inside);assert(geometry.panel.h<diameter*.21,'Compact status panel');
  assert(geometry.needs.every(n=>Math.abs(n.y-geometry.needs[0].y)<1),'Needs share one row');
  if(geometry.feedback){assert(geometry.feedback.y>=geometry.panel.y+geometry.panel.h,'Care feedback does not cover meters');assert(geometry.buttons.every(b=>b.y>=geometry.feedback.y+geometry.feedback.h),'Care feedback does not cover controls');}
  for(const b of geometry.buttons){assert(b.inside);assert(b.w>=44&&b.h>=44);assert(b.y>=geometry.panel.y+geometry.panel.h,'Meters do not cover touch targets');}
  if(process.env.DIGIVICE_SCREENSHOTS){mkdirSync(process.env.DIGIVICE_SCREENSHOTS,{recursive:true});await page.locator('#screen-surface').screenshot({path:join(process.env.DIGIVICE_SCREENSHOTS,`care-${diameter}.png`)});}
  await action('back').tap();await page.waitForFunction(()=>document.querySelector('#device-ui')?.dataset.screen==='home');
  assert.deepEqual(await readSave(),saved);assert.equal(writes.length,writeCount,'Navigation and rendering never write save');
  results.push({diameter,meters,geometry});await context.close();
 }
 assert.deepEqual(errors,[]);console.log('PASS Care at412/480: current/max HP, independent needs, compact grid, contained >=44px targets, navigation leaves save unchanged.');
 if(process.env.DIGIVICE_SCREENSHOTS)writeFileSync(join(process.env.DIGIVICE_SCREENSHOTS,'care-verification.json'),JSON.stringify({results,errors},null,2)+'\n');
} finally {await browser.close();await new Promise(resolve=>app.server.close(resolve));app.close();rmSync(dataDir,{recursive:true,force:true});}

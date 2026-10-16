// Real service/browser Home regression; fixtures and storage are disposable.
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {startServer} from '../service/server.ts';
import {namedFixture} from './legacy-fixture.ts';
if(!process.env.PLAYWRIGHT_MODULE)throw Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const {chromium}=await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const dataDir=mkdtempSync(join(tmpdir(),'digivice-home-bars-')),fixture=namedFixture();
for(const name of ['store.json','store.backup.json'])writeFileSync(join(dataDir,name),JSON.stringify(fixture.store));
const app=await startServer({dataDir,port:0,corePath:resolve(process.env.DIGIVICE_TEST_CORE_PATH||'build/digivice-core'),battleCorePath:resolve(process.env.DIGIVICE_TEST_BATTLE_PATH||'build/digivice-battle')});
const base=`http://127.0.0.1:${app.server.address().port}`,browser=await chromium.launch({headless:true}),errors=[];
const identity=fixture.identities[0];
try {
 for(const inputMode of ['touch','buttons'])for(const size of [412,480]) {
  const context=await browser.newContext({viewport:{width:1000,height:1000},hasTouch:true,reducedMotion:'reduce'});
  await context.addInitScript(identity=>localStorage.setItem('digivice.dev.identity.v1',JSON.stringify(identity)),identity);
  const page=await context.newPage();page.on('pageerror',e=>errors.push(String(e)));
  await page.goto(base+'?controls='+inputMode);await page.waitForSelector('#device-ui[data-screen="home"] .home-hp progress');
  await page.locator('#screen-surface').evaluate((element,size)=>{element.style.width=size+'px';element.style.height=size+'px';},size);
  const readSave=async()=>{const res=await fetch(base+'/api/save',{headers:{Authorization:`Bearer ${identity.token}`}});assert(res.ok);return res.json();};
  const saved=await readSave();
  for(let i=0;i<5;i++) {
   const bars=await page.locator('.home-meter progress').evaluateAll(nodes=>nodes.map(n=>({value:n.value,max:n.max,label:n.getAttribute('aria-label')})));
   assert.equal(bars.length,2);assert.equal(bars[0].value,0);assert.match(bars[0].label,/XP/);
   assert.equal(bars[1].value,saved.state.hp);assert.equal(bars[1].max,saved.state.combat.maxHp);assert.match(bars[1].label,/HP/);
   assert.match(await page.locator('.home-level').textContent(),/Level 1/);
   assert.doesNotMatch(await page.locator('.screen-heading').textContent(),/DIGIVICE/i);
   const xp=await page.locator('.home-xp').boundingBox(),hp=await page.locator('.home-hp').boundingBox();
   assert(xp.y+xp.height<=hp.y && Math.abs(xp.x-hp.x)<1,'XP stacks above HP');
   assert.match(await page.locator('.home-lifetime').textContent(),/Lifetime steps/);
   await page.locator(inputMode==='touch'?'#device-next':'#device-back-button').click();
  }
  assert.deepEqual(await readSave(),saved,'Home paging does not mutate saves');
  await page.locator('[data-device-action="care"]').click();await page.waitForSelector('#device-ui[data-screen="care"]');
  assert.equal(await page.locator('.home-meter').count(),0,'Home meters are confined to Home');
  await context.close();
 }
 assert.deepEqual(errors,[]);console.log('PASS: actual service Home in touch/buttons modes at 412/480, XP/HP semantics, all five panels, Level/lifetime preservation, Care navigation, no save mutations.');
} finally {await browser.close();await new Promise(r=>app.server.close(r));app.close();rmSync(dataDir,{recursive:true,force:true});}

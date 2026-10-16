import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
const rootDir=resolve(import.meta.dirname,'..');
const corePath=process.env.DIGIVICE_TEST_CORE_PATH??join(rootDir,'build/digivice-core');
const token=Buffer.alloc(32,94).toString('base64url');
const sha=(value:string)=>createHash('sha256').update(value).digest('hex');
const golden=JSON.parse(readFileSync(join(rootDir,'tests/fixtures/rules21-service-migration.json'),'utf8'));
type Event={type:string;value:number};
const receipt=(rulesVersion:number,baseRevision:number,events:Event[],batchId:string)=>({batchId,bodyHash:sha(JSON.stringify({rulesVersion,baseRevision,events})),revision:baseRevision+1,eventEnd:events.length});
const gameplay=({schemaVersion,rulesVersion,...state}:any)=>state;
const native=(args:string[],events:Event[]=[])=>JSON.parse(execFileSync(corePath,args,{input:events.map(e=>`${e.type} ${e.value}\n`).join(''),encoding:'utf8',maxBuffer:1024*1024}));
const identity={deviceId:`dv_${'d4'.repeat(12)}`,tokenHash:sha(token),seed:1,initialMode:'onboarding'};
// The native-valid synthetic Home checkpoint has sequence 2. These archived
// events provide matching receipt accounting; this is not an earned campaign.
const histories=[19,20].map((rulesVersion,index)=>{
 const events=[{type:index?'play':'hatch',value:index?0:1}];
 return{rulesVersion,events,receipts:[receipt(rulesVersion,index,events,`old-${rulesVersion}-prefix`)]};
});
const deviceFor=(events:Event[])=>({...identity,revision:2+(events.length?1:0),legacy:{histories,snapshotBase64:golden.snapshotBase64},events,receipts:events.length?[receipt(21,2,events,'rules21-suffix')]:[]});
async function fixture(t:any,device:any){
 const dataDir=await mkdtemp(join(tmpdir(),'digivice-rules22-'));
 const original=JSON.stringify({formatVersion:23,gameSchemaVersion:29,rulesVersion:21,devices:[device]},null,2)+'\n';
 for(const name of ['store.json','store.backup.json'])await writeFile(join(dataDir,name),original);
 const options={rootDir,corePath,battleCorePath:process.env.DIGIVICE_TEST_BATTLE_PATH,dataDir,port:0};
 let app=await startServer(options);
 const close=async()=>{await new Promise<void>((done,reject)=>app.server.close(e=>e?reject(e):done()));app.close();};
 t.after(async()=>{if(app.server.listening)await close();await rm(dataDir,{recursive:true,force:true});});
 const request=async(body?:unknown)=>{const response=await fetch(`http://127.0.0.1:${(app.server.address() as any).port}${body===undefined?'/api/save':'/api/save-sync'}`,{method:body===undefined?'GET':'POST',headers:{authorization:`Bearer ${token}`,...(body===undefined?{}:{'content-type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body)});return{status:response.status,body:await response.json() as any};};
 return{request,dataDir,original,restart:async()=>{await close();app=await startServer(options);}};
}
for(const entry of golden.cases)test(`rules21 ${entry.label} migrates with exact state, trace, histories and restart`,async t=>{
 const old=deviceFor(entry.events),f=await fixture(t,old),saved=await f.request();
 assert.equal(saved.status,200);assert.deepEqual([saved.body.state.rulesVersion,saved.body.state.schemaVersion],[22,30]);
 assert.deepEqual(gameplay(saved.body.state),gameplay(entry.expected.state));
 assert.deepEqual(saved.body.autoTrace,entry.expected.trace);assert.equal(saved.body.revision,old.revision);assert.deepEqual(saved.body.events,[]);
 const store=JSON.parse(await readFile(join(f.dataDir,'store.json'),'utf8'));
 assert.deepEqual([store.formatVersion,store.gameSchemaVersion,store.rulesVersion],[24,30,22]);
 const current=store.devices[0];assert.equal(current.deviceId,old.deviceId);assert.equal(current.tokenHash,old.tokenHash);
 assert.deepEqual(current.legacy.histories,entry.events.length?[...histories,{rulesVersion:21,events:entry.events,receipts:old.receipts}]:histories);
 assert.deepEqual(current.events,[]);assert.deepEqual(current.receipts,[]);
 const bytes=Buffer.from(current.legacy.snapshotBase64,'base64');assert.equal(bytes.length,6876);assert.equal(bytes.readUInt16LE(4),30);assert.equal(bytes.readUInt32LE(8),22);
 assert.equal(await readFile(join(f.dataDir,'store.rules-v21.json'),'utf8'),f.original);
 await f.restart();assert.deepEqual(await f.request(),saved);
 if(entry.events.length){
  const pending={rulesVersion:21,baseRevision:2,batchId:'rules21-suffix',events:entry.events};const raw=JSON.stringify(pending);
  assert.equal((await f.request(pending)).body.error,'migration_required');assert.equal(JSON.stringify(pending),raw);
  assert.equal((await f.request({...pending,rulesVersion:22})).body.error,'legacy_batch_requires_reconciliation');
  assert.deepEqual(await f.request(),saved);
 }
});
test('migrated tier intermission advances exactly once with its frozen foe and key count',async t=>{
 const entry=golden.cases.find((value:any)=>value.label==='intermission-tier1');
 const f=await fixture(t,deviceFor(entry.events)),before=(await f.request()).body;
 const body={rulesVersion:22,baseRevision:before.revision,batchId:'current-next-tier',events:[{type:'next-expedition',value:0}]};
 const next=await f.request(body);assert.equal(next.status,200,JSON.stringify(next.body));
 assert.equal(next.body.state.expeditions.tier,2);assert.equal(next.body.state.expeditions.dungeonKeys,before.state.expeditions.dungeonKeys);
 assert.equal(next.body.state.wildFormId,golden.cases.find((value:any)=>value.label==='encounter-tier2').expected.state.wildFormId);assert.equal(next.body.state.wildRules,22);
 assert.deepEqual(await f.request(body),next);const checkpoint=await f.request();await f.restart();assert.deepEqual(await f.request(),checkpoint);
 assert.equal((await f.request({...body,batchId:'stale-next-tier'})).status,409);assert.deepEqual(await f.request(),checkpoint);
});
test('unhatched rules21 onboarding identity migrates without creating or resetting a partner',async t=>{
 const f=await fixture(t,{...identity,seed:9876,revision:0,legacy:null,events:[],receipts:[]});
 const saved=await f.request();assert.equal(saved.status,200);assert.equal(saved.body.state.phase,'egg');
 assert.deepEqual(gameplay(saved.body.state),gameplay(native(['--replay-v21-onboarding-trace','9876']).state));
 assert.equal(saved.body.state.sequence,0);await f.restart();assert.deepEqual(await f.request(),saved);
});

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
const token=Buffer.alloc(32,93).toString('base64url');
const sha=(text:string)=>createHash('sha256').update(text).digest('hex');
const golden=JSON.parse(readFileSync(join(rootDir,'tests/fixtures/rules20-dungeon-golden.json'),'utf8'));
type Event={type:string;value:number};
const receipt=(rulesVersion:number,baseRevision:number,events:Event[],batchId:string,eventEnd=events.length)=>({batchId,bodyHash:sha(JSON.stringify({rulesVersion,baseRevision,events})),revision:baseRevision+1,eventEnd});
const native=(args:string[],events:Event[]=[])=>JSON.parse(execFileSync(corePath,args,{input:events.map(e=>`${e.type} ${e.value}\n`).join(''),encoding:'utf8',maxBuffer:1024*1024}));
const comparable=({schemaVersion,rulesVersion,expeditions,...state}:any)=>({...state,expeditions:Object.fromEntries(Object.entries(expeditions).filter(([key])=>!['active','scenarioIndex','tier','tiers','awaitingNext'].includes(key)))});
async function fixture(t:any,device:any){
 const dataDir=await mkdtemp(join(tmpdir(),'digivice-rules21-'));
 const original=JSON.stringify({formatVersion:22,gameSchemaVersion:28,rulesVersion:20,devices:[device]},null,2)+'\n';
 for(const name of ['store.json','store.backup.json'])await writeFile(join(dataDir,name),original);
 const options={rootDir,corePath,battleCorePath:process.env.DIGIVICE_TEST_BATTLE_PATH,dataDir,port:0};
 let app=await startServer(options);
 const close=async()=>{await new Promise<void>((done,reject)=>app.server.close(e=>e?reject(e):done()));app.close();};
 t.after(async()=>{if(app.server.listening)await close();await rm(dataDir,{recursive:true,force:true});});
 const request=async(body?:unknown,path=body===undefined?'/api/save':'/api/save-sync')=>{const response=await fetch(`http://127.0.0.1:${(app.server.address() as any).port}${path}`,{method:body===undefined?'GET':'POST',headers:{authorization:`Bearer ${token}`,...(body===undefined?{}:{'content-type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body)});return{status:response.status,body:await response.json() as any};};
 return{request,dataDir,original,restart:async()=>{await close();app=await startServer(options);},url:()=>`http://127.0.0.1:${(app.server.address() as any).port}`};
}
const identity={deviceId:`dv_${'d3'.repeat(12)}`,tokenHash:sha(token),initialMode:'onboarding'};
test('rules20 replay migrates once with exact historical trace and preserved old/outstanding receipts',async t=>{
 const original=golden.replays[0];
 const events:Event[]=original.events.trim().split('\n').map((line:string)=>{const [type,value]=line.split(/\s+/);return{type,value:Number(value??0)};});
 const receipts=[];for(let start=0;start<events.length;start+=100){const chunk=events.slice(start,start+100);receipts.push(receipt(20,receipts.length,chunk,`rules20-batch-${receipts.length}`,start+chunk.length));}
 const f=await fixture(t,{...identity,seed:original.seed,revision:receipts.length,legacy:null,events,receipts});
 const saved=await f.request();assert.equal(saved.status,200);assert.deepEqual(comparable(saved.body.state),comparable(original.expected.state));assert.deepEqual(saved.body.autoTrace,original.expected.trace);
 assert.deepEqual([saved.body.state.rulesVersion,saved.body.state.schemaVersion],[22,30]);assert.equal(saved.body.state.expeditions.active,false);assert.deepEqual(saved.body.events,[]);
 const store=JSON.parse(await readFile(join(f.dataDir,'store.json'),'utf8'));assert.deepEqual([store.formatVersion,store.gameSchemaVersion,store.rulesVersion],[24,30,22]);assert.deepEqual(store.devices[0].legacy.histories,[{rulesVersion:20,events,receipts}]);assert.equal(await readFile(join(f.dataDir,'store.rules-v20.json'),'utf8'),f.original);
 const pending={rulesVersion:20,baseRevision:0,batchId:receipts[0].batchId,events:events.slice(0,100)};assert.equal((await f.request(pending)).body.error,'migration_required');assert.equal((await f.request({...pending,rulesVersion:22})).body.error,'legacy_batch_requires_reconciliation');assert.deepEqual(await f.request(),saved);
 await f.restart();assert.deepEqual(await f.request(),saved);
 const route=await fetch(f.url()+'/expedition-presentation.js');assert.equal(route.status,200);assert.match(await route.text(),/expedition/);
});
test('a schema28 baseline with rules19 history keeps its separate rules20 suffix and currency',async t=>{
 const prefix=[{type:'hatch',value:1}],suffix=[{type:'play',value:0},{type:'rest',value:0}];
 const old=golden.saves[0];const original=native(['--replay-v20-snapshot-trace',old.snapshotBase64],suffix);
 const archived=receipt(19,0,prefix,'rules19-prefix'),newer=receipt(20,1,suffix,'rules20-suffix');
 const f=await fixture(t,{...identity,seed:12345,revision:2,legacy:{histories:[{rulesVersion:19,events:prefix,receipts:[archived]}],snapshotBase64:old.snapshotBase64},events:suffix,receipts:[newer]});
 const saved=await f.request();assert.equal(saved.status,200);assert.deepEqual(comparable(saved.body.state),comparable(original.state));
 const store=JSON.parse(await readFile(join(f.dataDir,'store.json'),'utf8'));assert.deepEqual(store.devices[0].legacy.histories,[{rulesVersion:19,events:prefix,receipts:[archived]},{rulesVersion:20,events:suffix,receipts:[newer]}]);
 assert.equal(saved.body.state.expeditions.dungeonKeys,1);assert.equal(saved.body.state.expeditions.bossSteps,2345);
 await f.restart();assert.deepEqual(await f.request(),saved);
});
test('new dungeon intermission persists and retries never spend another key or advance twice',async t=>{
 const prefix=[{type:'hatch',value:1}],old=golden.saves[0];
 const f=await fixture(t,{...identity,seed:12345,revision:1,legacy:{histories:[{rulesVersion:19,events:prefix,receipts:[receipt(19,0,prefix,'rules19-checkpoint')]}],snapshotBase64:old.snapshotBase64},events:[],receipts:[]});
 let current=(await f.request()).body,serial=0;
 const send=async(type:string,value=0)=>{const body={rulesVersion:22,baseRevision:current.revision,batchId:`dungeon-flow-${++serial}`,events:[{type,value}]};const result=await f.request(body);assert.equal(result.status,200,JSON.stringify(result.body));current=result.body;return{body,result};};
 await send('start-expedition');assert.equal(current.state.expeditions.tier,1);assert.equal(current.state.expeditions.dungeonKeys,0);
 for(let n=0;n<30&&current.state.phase==='encounter';++n)await send('attack');
 assert.equal(current.state.phase,'expedition');assert.equal(current.state.expeditions.awaitingNext,true);assert.equal(current.state.expeditions.dungeonClears,0);assert.equal(current.state.expeditions.dungeonWins,4);
 const checkpoint=await f.request();await f.restart();assert.deepEqual(await f.request(),checkpoint);
 const {body,result}=await send('next-expedition');assert.equal(current.state.expeditions.tier,2);assert.equal(current.state.expeditions.dungeonKeys,0);assert.deepEqual(await f.request(body),result);
 const advanced=await f.request();await f.restart();assert.deepEqual(await f.request(),advanced);
 const before=await f.request();assert.equal((await f.request({...body,batchId:'stale-next-expedition'})).status,409);assert.deepEqual(await f.request(),before);
});

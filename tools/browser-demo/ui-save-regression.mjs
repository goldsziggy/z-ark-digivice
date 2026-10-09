// Exercise the actual browser controller with real WASM and a minimal DOM.
// Only browser services, drawing and the runtime import are replaced. Native
// command, snapshot, validation and the controller's save branches stay real.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import { createDeviceView } from '../../docs/play/device-view.js';
import { buildBattleFrames, createBattlePlayback } from '../../docs/play/battle-playback.js';
import createDemoCore from '../../docs/play/runtime/demo-core.js';
import { validParty } from '../../docs/play/shared/party.js';
import { validateStarterCatalog } from '../../docs/play/shared/starter-onboarding.js';

const SAVE_KEY = 'zark.browser-demo.v1.rules15';
const source = await readFile(new URL('../../docs/play/app.js', import.meta.url), 'utf8');
assert.equal((source.match(/const \{default:createDemoCore\}=await import\('\.\/runtime\/demo-core\.js'\);/g) || []).length, 1);
assert.match(source, /\ninit\(\);\s*$/);
const controller = source.replace(/^import .+;\n/gm, '')
  .replace("const {default:createDemoCore}=await import('./runtime/demo-core.js');", 'const createDemoCore=globalThis.runtimeFactory;')
  .replace(/\ninit\(\);\s*$/, `
    render = () => {};
    globalThis.ui = { init, command, openCapture, syncSaved, finishBattlePlayback, captureOpen: () => captureMode,
      state: () => state, needsReset: () => needsReset,
      setReleaseIntent: id => { releaseId=id; releaseIntendedState=state; },
      snapshot: () => core.ccall('demo_snapshot', 'string', [], []) };
  `);
const native = await createDemoCore();
const call = (name, types=[], args=[]) => JSON.parse(native.ccall(name, 'string', types, args));
const act = (command, value=0) => call('demo_command', ['string','number'], [command,value]);
const snapshot = () => native.ccall('demo_snapshot','string',[],[]);
const envelope = hex => JSON.stringify({format:1,rulesVersion:15,schemaVersion:22,snapshot:hex});

async function fixture(initialRaw) {
  let stored = initialRaw, writes = 0, beforeNextLock;
  const calls = [], elements = new Map();
  const element = () => ({ textContent:'',innerHTML:'',hidden:false,dataset:{},style:{},disabled:false,listeners:new Map(),open:false,
    getContext:()=>({}),addEventListener(name,callback){this.listeners.set(name,callback);},replaceChildren(){},setAttribute(){},focus(){},showModal(){this.open=true;},close(){this.open=false;} });
  const document = { getElementById(id){ if(!elements.has(id))elements.set(id,element());return elements.get(id); },
    createElement:()=>element(),querySelectorAll:()=>[],addEventListener(){},hidden:false };
  const sandbox = {
    document,window:{addEventListener(){}},WebAssembly,
    matchMedia:()=>({matches:true}),requestAnimationFrame(){},performance:{now:()=>0},
    navigator:{locks:{async request(name, options, callback){
      assert.equal(name,`${SAVE_KEY}.write`);assert.equal(options.mode,'exclusive');
      const hook=beforeNextLock;beforeNextLock=null;if(hook)hook();return callback();
    }}},
    localStorage:{getItem(key){assert.equal(key,SAVE_KEY);return stored;},setItem(key,value){assert.equal(key,SAVE_KEY);stored=value;writes++;}},
    validParty,validateStarterCatalog,
    loadGameArt:async()=>({prepare:async()=>{},drawForm:()=>false,drawBackground:()=>false,formStatus:()=>'missing'}),
    buildBattleFrames, createBattlePlayback, createDeviceView, createDeviceTouchInput:()=>({refresh(){},cancel(){},contactActive:()=>false}),
    createCaptureRingInput:()=>({refresh(){},cancel(){}}),
    runtimeFactory:async()=>{
      const module=await createDemoCore();
      const ccall=module.ccall;
      module.ccall=(name,type,types,args)=>{calls.push({name,args});return ccall(name,type,types,args);};
      return module;
    },
  };
  vm.runInNewContext(controller,sandbox,{filename:'app.js'});
  await sandbox.ui.init();
  return {ui:sandbox.ui,calls,elements,raw:()=>stored,writes:()=>writes,
    updateBeforeNextLock(raw, syncFirst=false){beforeNextLock=()=>{stored=raw;if(syncFirst)sandbox.ui.syncSaved();};}};
}

// A real auto encounter pauses for manual capture. Another tab consumes that
// capture opportunity while this tab's sampled input waits for the save lock.
call('demo_reset',['number'],[12345]);
assert.equal(act('hatch',5).ok,true);
assert.equal(act('mode',1).ok,true);
assert.equal(act('demo-encounter').ok,true);
assert.equal(act('auto-fight').state.autoCapture,1);
const beforeCapture=snapshot();
assert.equal(act('ring-capture',0).ok,true);
const afterCapture=snapshot();
assert.notEqual(beforeCapture,afterCapture);
// Opening the reset confirmation must disarm the globally handled capture key.
const modal=await fixture(envelope(beforeCapture));
modal.ui.openCapture();
assert.equal(modal.ui.captureOpen(),true);
modal.elements.get('reset-open').listeners.get('click')();
assert.equal(modal.ui.captureOpen(),false);
assert.equal(modal.elements.get('reset-dialog').open,true);
assert.equal(modal.calls.filter(c=>c.name==='demo_command').length,0);

for(const [command,value] of [['ring-capture',1000],['release',2],['select',2],['evolve',40]]) {
  const f=await fixture(envelope(beforeCapture));
  assert.equal(f.ui.needsReset(),false);
  const commandsBefore=f.calls.filter(c=>c.name==='demo_command').length;
  f.updateBeforeNextLock(envelope(afterCapture));
  await f.ui.command(command,value);
  assert.equal(f.calls.filter(c=>c.name==='demo_command').length,commandsBefore,`${command}: stale intent must not reach native core`);
  assert.equal(f.ui.snapshot(),afterCapture,`${command}: newer state must remain exact`);
  assert.equal(f.raw(),envelope(afterCapture),`${command}: newer save must remain untouched`);
  assert.equal(f.writes(),0,`${command}: stale intent must not save`);
  assert.match(f.elements.get('game-status').textContent,/Review the latest state/);
}

// A storage-event callback may acquire its lock before the queued command and
// already load the new state. The intent still belongs to the older state.
const precedingSync=await fixture(envelope(beforeCapture));
precedingSync.updateBeforeNextLock(envelope(afterCapture),true);
await precedingSync.ui.command('ring-capture',1000);
assert.equal(precedingSync.calls.filter(c=>c.name==='demo_command').length,0);
assert.equal(precedingSync.ui.snapshot(),afterCapture);
assert.equal(precedingSync.writes(),0);
assert.match(precedingSync.elements.get('game-status').textContent,/Review the latest state/);

// A reset confirmation belongs to the state shown when it was opened. A change
// in another tab while the dialog is open must not erase the newer adventure.
const resetModal=await fixture(envelope(beforeCapture));
resetModal.elements.get('reset-open').listeners.get('click')();
const resetsBefore=resetModal.calls.filter(c=>c.name==='demo_reset').length;
resetModal.updateBeforeNextLock(envelope(afterCapture));
await resetModal.elements.get('reset-confirm').listeners.get('click')();
assert.equal(resetModal.calls.filter(c=>c.name==='demo_reset').length,resetsBefore);
assert.equal(resetModal.raw(),envelope(afterCapture));
assert.equal(resetModal.writes(),0);

// A release confirmation must not target a different adventure that has loaded
// since the dialog opened, even if its member IDs happen to be reused.
const releaseModal=await fixture(envelope(beforeCapture));
releaseModal.ui.setReleaseIntent(2);
releaseModal.updateBeforeNextLock(envelope(afterCapture),true);
await releaseModal.ui.command('ring-capture',1000); // performs the prior-tab sync only
releaseModal.elements.get('release-confirm').listeners.get('click')();
assert.equal(releaseModal.calls.filter(c=>c.name==='demo_command').length,0);
assert.equal(releaseModal.raw(),envelope(afterCapture));

// Empty string is an existing corrupt save, not the absent-key sentinel. Preserve
// it (and other invalid saves) until the user explicitly chooses Reset demo.
for(const raw of ['', '{', '{}', envelope('0'.repeat(5928))]) {
  const f=await fixture(raw);
  assert.equal(f.ui.needsReset(),true,`Invalid save must require reset: ${JSON.stringify(raw.slice(0,20))}`);
  assert.equal(f.raw(),raw);
  assert.equal(f.writes(),0);
  // Explicit reset of the same corrupt save remains available.
  f.elements.get('reset-open').listeners.get('click')();
  await f.elements.get('reset-confirm').listeners.get('click')();
  assert.equal(f.ui.needsReset(),false);
  assert.equal(f.ui.state().phase,'egg');
  assert.equal(f.writes(),1);
}

// The guards do not break a genuinely new adventure or an ordinary fresh action.
const fresh=await fixture(null);
assert.equal(fresh.ui.needsReset(),false);
assert.equal(fresh.ui.state().phase,'egg');
assert.equal(fresh.writes(),1);
await fresh.ui.command('hatch',1);
assert.equal(fresh.ui.state().phase,'home');
assert.equal(fresh.ui.state().creature,'Impmon');
assert.equal(fresh.ui.state().battleMode,'auto','New browser partners default to Auto');
assert.equal(fresh.writes(),2);
assert.equal(fresh.calls.filter(c=>c.name==='demo_command').length,2);
const autoRestored=await fixture(fresh.raw());
assert.equal(autoRestored.ui.state().battleMode,'auto');
assert.equal(autoRestored.writes(),0,'Loading an existing save does not rewrite it');
await fresh.ui.command('mode',0);
const manualRestored=await fixture(fresh.raw());
assert.equal(manualRestored.ui.state().battleMode,'tactical','Explicit Manual choice survives reload');
assert.equal(manualRestored.writes(),0);
manualRestored.elements.get('reset-open').listeners.get('click')();
await manualRestored.elements.get('reset-confirm').listeners.get('click')();
await manualRestored.ui.command('hatch',8);
assert.equal(manualRestored.ui.state().battleMode,'auto','Reset adventures default to Auto at hatch');
await autoRestored.ui.command('demo-encounter');
await autoRestored.ui.command('auto-fight');
assert.equal(autoRestored.ui.state().autoCapture,1,'Default Auto stops for manual timing capture');
autoRestored.ui.finishBattlePlayback();
const pendingCapture=autoRestored.ui.snapshot();
autoRestored.ui.openCapture();
assert.equal(autoRestored.ui.snapshot(),pendingCapture,'Opening the ring cannot consume capture RNG');
await autoRestored.ui.command('ring-capture',0);
assert.notEqual(autoRestored.ui.snapshot(),pendingCapture,'Timing input executes a native capture attempt');
console.log('PASS: save races and corrupt-save recovery; new/reset adventures default Auto; saved Auto/Manual choices persist; Auto waits for manual timing capture.');

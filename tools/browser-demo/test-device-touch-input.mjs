#!/usr/bin/env node
// Pointer adapter contract from firmware/runtime/device_ui.cpp, exercised through
// events. No browser, game-state shortcuts, services, hardware or save files.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createDeviceTouchInput } from '../../docs/play/device-touch-input.js';

const BACK = {id:'back',x:144,y:348,w:124,h:38,enabled:true};
const CATCH = {id:'catch',x:274,y:287,w:76,h:44,enabled:true};
function fixture(t, {mode='browse', scale=1, border=0, band=[100,280]} = {}) {
  const document = new EventTarget(), window = new EventTarget(), surface = new EventTarget();
  document.defaultView = window; document.hidden = false;
  surface.ownerDocument = document; surface.isConnected = true;
  surface.style = {touchAction:'pan-y',userSelect:'text',webkitUserSelect:'text'};
  surface.contains = target => target === surface;
  surface.offsetWidth = surface.offsetHeight = 412 + border * 2;
  surface.clientWidth = surface.clientHeight = 412;
  surface.clientLeft = surface.clientTop = border;
  surface.getBoundingClientRect = () => ({left:30,top:40,width:(412+2*border)*scale,height:(412+2*border)*scale});
  surface.setPointerCapture = () => {};
  let clock=1000, context=1, enabled=true, currentMode=mode, currentTargets=[], currentBand=band;
  const calls=[];
  const input = createDeviceTouchInput(surface, {
    canInteract:()=>enabled, getContext:()=>context, getMode:()=>currentMode,
    getTargets:()=>currentTargets, getBrowseBand:()=>currentBand, now:()=>clock,
    onTarget:(id,detail)=>calls.push({type:'target',id,...detail}),
    onHorizontal:(delta,detail)=>calls.push({type:'horizontal',delta,...detail}),
    onBattleCommit:detail=>calls.push({type:'battle',...detail}),
    onCapture:detail=>calls.push({type:'capture',...detail}),
  });
  function send(type, values={}, owner=document) {
    const event = new Event(type,{cancelable:true});
    for (const [key,value] of Object.entries(values)) Object.defineProperty(event,key,{value});
    owner.dispatchEvent(event); return event;
  }
  function pointer(type,x=206,y=180,time=clock,extra={}) {
    clock=time;
    // Small epsilon avoids floating-point roundoff during CSS scale inversion.
    return send(type,{target:surface,pointerId:1,button:0,isPrimary:true,pointerType:'touch',
      clientX:30+(border+x+.00001)*scale,clientY:40+(border+y+.00001)*scale,...extra});
  }
  function tap(x,y,elapsed=100,extra={}) {
    const time=clock; pointer('pointerdown',x,y,time,extra); pointer('pointerup',x,y,time+elapsed,extra);
  }
  function swipe(x0,y0,x1,y1,elapsed=100,extra={}) {
    const time=clock; pointer('pointerdown',x0,y0,time,extra);
    pointer('pointermove',x1,y1,time+elapsed/2,extra); pointer('pointerup',x1,y1,time+elapsed,extra);
  }
  t.after(()=>input.destroy());
  return {input,document,window,surface,calls,send,pointer,tap,swipe,
    mode:value=>{currentMode=value;}, targets:value=>{currentTargets=value;}, band:value=>{currentBand=value;},
    revise:()=>{context++;}, enable:value=>{enabled=value;}, time:value=>{clock=value;}};
}

test('mouse drag and touch swipe use the same native direction, axis and inclusive threshold rules', t=>{
  for (const pointerType of ['mouse','touch','pen']) for (const scale of [.65,1,1.5]) {
    for (const [dx,dy,expected] of [[-40,0,1],[40,0,-1],[-39,0,0],[-60,40,1],[-60,41,0],
      [60,-40,-1],[60,-41,0],[0,-80,0],[0,80,0]]) {
      const f=fixture(t,{scale}); f.swipe(206,180,206+dx,180+dy,100,{pointerType});
      assert.deepEqual(f.calls.map(c=>c.delta),expected?[expected]:[],`${pointerType} scale ${scale} dx ${dx} dy ${dy}`);
    }
  }
});
test('gesture time boundaries are native 40 through 1500ms; navigation never repeats on duplicate Up', t=>{
  for (const [elapsed,expected] of [[39,0],[40,1],[1500,1],[1501,0]]) {
    const f=fixture(t); f.swipe(250,180,160,180,elapsed);
    f.pointer('pointerup',160,180,3000);
    assert.equal(f.calls.length,expected,`${elapsed}ms`);
  }
});
test('native edge tap rectangles are half-open and never turn centre artwork into an action', t=>{
  for (const [x,y,delta] of [[30,134,-1],[83,245,-1],[29,180,0],[84,180,0],
    [328,134,1],[381,245,1],[327,180,0],[382,180,0],[55,133,0],[55,246,0],[206,180,0]]) {
    const f=fixture(t); f.tap(x,y); assert.deepEqual(f.calls.map(c=>c.delta),delta?[delta]:[],`${x},${y}`);
  }
});
test('edge taps require 40ms, matching zones and no excursion over 24px even when returning',t=>{
  for (const [elapsed,expected] of [[39,0],[40,1],[1500,1],[1501,0]]) {
    const f=fixture(t); f.tap(55,180,elapsed); assert.equal(f.calls.length,expected);
  }
  const leftZone=fixture(t); leftZone.pointer('pointerdown',83,180);leftZone.pointer('pointerup',84,180,1100);assert.equal(leftZone.calls.length,0);
  const excursion=fixture(t);excursion.pointer('pointerdown',55,180);excursion.pointer('pointermove',80,180,1050);excursion.pointer('pointerup',55,180,1100);assert.equal(excursion.calls.length,0);
});
test('Home special browse band includes edge-button origins; ordinary button origins block swipes',t=>{
  const edge={id:'next',x:328,y:134,w:54,h:112,enabled:true};
  const home=fixture(t,{band:[112,282]});home.targets([edge]);home.tap(355,180);
  assert.deepEqual(home.calls.map(c=>[c.type,c.delta]),[['horizontal',1]]);
  for (const [y,expected] of [[111,0],[112,1],[281,1],[282,0]]) {
    const f=fixture(t,{band:[112,282]});f.swipe(250,y,160,y);assert.equal(f.calls.length,expected,`Home y${y}`);
  }
  for (const [y,expected] of [[99,0],[100,1],[279,1],[280,0]]) {
    const f=fixture(t);f.swipe(250,y,160,y);assert.equal(f.calls.length,expected,`browse y${y}`);
  }
  const button=fixture(t);button.targets([{id:'choose',x:116,y:252,w:180,h:52,enabled:true}]);button.swipe(206,260,130,180);assert.equal(button.calls.length,0);
});
test('battle icons select; only a separate clear upward gesture commits the selected move',t=>{
  const f=fixture(t,{mode:'battle'});f.targets([CATCH,BACK]);
  f.tap(120,252);f.tap(280,252);f.tap(206,180);f.swipe(206,220,206,180);
  assert.deepEqual(f.calls.map(c=>[c.type,c.delta]),[['horizontal',-1],['horizontal',1],['battle',undefined]]);
  for (const [dx,dy,expected] of [[0,-39,0],[0,-40,1],[40,-60,1],[41,-60,0],[0,40,0]]) {
    const g=fixture(t,{mode:'battle'});g.swipe(206,220,206+dx,220+dy);assert.equal(g.calls.filter(c=>c.type==='battle').length,expected);
  }
});
test('battle zone and side-icon bounds match native; disabled Catch origin cannot commit or browse',t=>{
  for (const [y,expected] of [[129,0],[130,1],[335,1],[336,0]]) {
    const f=fixture(t,{mode:'battle'});f.swipe(206,y,146,y);assert.equal(f.calls.length,expected,`battle y${y}`);
  }
  for (const [x,y,delta] of [[80,248,-1],[175,286,-1],[79,252,0],[176,252,0],[238,248,1],[333,286,1],[334,252,0],[280,287,0]]) {
    const f=fixture(t,{mode:'battle'});f.tap(x,y);assert.deepEqual(f.calls.map(c=>c.delta),delta?[delta]:[],`${x},${y}`);
  }
  for(const enabled of [true,false]) {
    const f=fixture(t,{mode:'battle'});f.targets([{...CATCH,enabled}]);f.swipe(300,310,300,210);assert.equal(f.calls.length,0);
  }
});
test('ordinary buttons use same target on Up, native 20–1800ms and 24px motion allowance',t=>{
  for(const [elapsed,expected] of [[19,0],[20,1],[1800,1],[1801,0]]) {
    const f=fixture(t,{mode:'buttons'});f.targets([BACK]);f.tap(206,365,elapsed);assert.equal(f.calls.length,expected);
  }
  for(const [distance,expected] of [[24,1],[25,0]]) {
    const f=fixture(t,{mode:'buttons'});f.targets([BACK]);f.swipe(206,365,206+distance,365);assert.equal(f.calls.length,expected);
  }
  const returning=fixture(t,{mode:'buttons'});returning.targets([BACK]);returning.pointer('pointerdown',206,365);returning.pointer('pointermove',231,365,1050);returning.pointer('pointerup',206,365,1100);assert.equal(returning.calls.length,0);
  const miss=fixture(t,{mode:'buttons'});miss.targets([BACK]);miss.pointer('pointerdown',145,365);miss.pointer('pointerup',143,365,1100);assert.equal(miss.calls.length,0);
});
test('targets disabled, removed, repositioned or changed while held cannot activate',t=>{
  for(const change of [f=>f.targets([{...BACK,enabled:false}]),f=>f.targets([]),f=>f.targets([{...BACK,x:143}]),f=>f.revise(),f=>f.mode('browse'),f=>f.enable(false)]) {
    const f=fixture(t,{mode:'buttons'});f.targets([BACK]);f.pointer('pointerdown',206,365);change(f);f.pointer('pointerup',206,365,1100);assert.equal(f.calls.length,0);
  }
  const disabled=fixture(t,{mode:'buttons'});disabled.targets([{...BACK,enabled:false}]);disabled.pointer('pointerdown',206,365);disabled.targets([BACK]);disabled.pointer('pointerup',206,365,1100);assert.equal(disabled.calls.length,0);
});
test('bordered CSS-scaled canvas maps its content pixels, including native 204px circular edge',t=>{
  for(const scale of [.65,1,1.5]) for(const border of [0,6,8]) {
    for(const [x,y,expected] of [[206,80,1],[206,330,1],[2,206,1],[410,206,1],[1,206,0],[411,206,0],[0,0,0],[206,79,0],[206,331,0]]) {
      const f=fixture(t,{mode:'capture',scale,border});f.tap(x,y);assert.equal(f.calls.length,expected,`${scale}/${border}: ${x},${y}`);
    }
  }
});
test('capture submits fresh DOWN immediately once; motion, release or cancellation cannot undo it',t=>{
  for(const interrupt of [f=>f.pointer('pointermove',0,0,1010),f=>f.pointer('pointercancel',206,180,1010),f=>f.input.cancel(),
    f=>f.send('blur',{target:f.window},f.window),f=>{f.document.hidden=true;f.send('visibilitychange');}]) {
    const f=fixture(t,{mode:'capture'});f.pointer('pointerdown',206,180,1000);assert.equal(f.calls.length,1);
    assert.equal(f.calls[0].time,1000);interrupt(f);f.pointer('pointerup',206,180,1200);assert.equal(f.calls.length,1);
  }
});
test('capture 450ms guard survives new screens/retries; held contact never becomes a second fresh DOWN',t=>{
  const f=fixture(t,{mode:'capture'});f.pointer('pointerdown',206,180,1000);f.revise();f.pointer('pointerdown',206,180,1600);assert.equal(f.calls.length,1);
  f.pointer('pointerup',206,180,1610);f.pointer('pointerdown',206,180,1700);f.pointer('pointerup',206,180,1710);assert.equal(f.calls.length,2);
  f.revise();f.pointer('pointerdown',206,180,2149);f.pointer('pointerup',206,180,2150);assert.equal(f.calls.length,2);
  f.pointer('pointerdown',206,180,2150);assert.equal(f.calls.length,3);
});
test('capture mode keeps Back/Skip release-based and disjoint from immediate capture',t=>{
  for(const target of [BACK,{id:'resume',x:104,y:348,w:204,h:38,enabled:true}]) {
    const f=fixture(t,{mode:'capture'});f.targets([target]);f.pointer('pointerdown',206,365);assert.equal(f.calls.length,0);
    f.pointer('pointerup',206,365,1100);assert.deepEqual(f.calls.map(c=>[c.type,c.id]),[['target',target.id]]);
  }
});
test('a held opening action cannot leak into capture on its Up or compatibility click',t=>{
  const f=fixture(t,{mode:'buttons'});f.targets([{id:'aim',x:116,y:252,w:180,h:52,enabled:true}]);
  f.pointer('pointerdown',206,280);f.mode('capture');f.revise();f.pointer('pointerup',206,280,1100);
  f.send('click',{target:f.surface,pointerId:1,detail:1});assert.equal(f.calls.length,0);
  f.pointer('pointerdown',206,180,1200);assert.equal(f.calls.length,1);
});
test('second contact anywhere cancels an unfinished gesture; mixed pointer and preheld outside contacts cannot act',t=>{
  for(const target of ['surface','outside']) {
    const f=fixture(t);f.pointer('pointerdown',250,180);
    f.pointer('pointerdown',206,180,1020,{pointerId:2,isPrimary:false,target:target==='surface'?f.surface:{}});
    f.pointer('pointerup',160,180,1100);assert.equal(f.calls.length,0);
  }
  const f=fixture(t,{mode:'capture'});const outside={};f.pointer('pointerdown',206,180,900,{pointerId:2,target:outside});
  f.pointer('pointerdown',206,180,1000);assert.equal(f.calls.length,0);
});
test('ordinary gestures cancel on pointercancel, lostcapture, blur, hidden, resize, pagehide, revision or explicit cancel',t=>{
  const interrupts=[f=>f.pointer('pointercancel',250,180,1020),f=>f.send('lostpointercapture',{pointerId:1}),
    f=>f.send('blur',{target:f.window},f.window),f=>{f.document.hidden=true;f.send('visibilitychange');},
    f=>f.send('resize',{},f.window),f=>f.send('pagehide',{},f.window),f=>f.revise(),f=>f.input.cancel()];
  for(const interrupt of interrupts) {const f=fixture(t);f.pointer('pointerdown',250,180);interrupt(f);f.pointer('pointerup',160,180,1100);assert.equal(f.calls.length,0);}
});
test('outside movement is terminal; return, reversed time and more than 10s cannot revive a gesture',t=>{
  const outside=fixture(t);outside.pointer('pointerdown',250,180);outside.pointer('pointermove',0,0,1050);outside.pointer('pointerup',160,180,1100);assert.equal(outside.calls.length,0);
  const reversal=fixture(t);reversal.pointer('pointerdown',250,180,1000);reversal.pointer('pointermove',220,180,1100);reversal.pointer('pointerup',160,180,1050);assert.equal(reversal.calls.length,0);
  const expired=fixture(t);expired.pointer('pointerdown',250,180);expired.pointer('pointermove',200,180,11001);expired.pointer('pointerup',160,180,11002);assert.equal(expired.calls.length,0);
});
test('clipped corners consume contact/click; canceled contacts cannot click through a new control; outside controls are unaffected',t=>{
  const f=fixture(t);const outside={};
  assert.equal(f.pointer('pointerdown',0,0).defaultPrevented,true);assert.equal(f.input.contactActive(),true);
  assert.equal(f.pointer('pointerup',0,0,1100).defaultPrevented,true);assert.equal(f.input.contactActive(),false);
  assert.equal(f.send('click',{target:outside,pointerId:1,detail:1}).defaultPrevented,true);
  assert.equal(f.pointer('pointerdown',206,180,1200,{target:outside}).defaultPrevented,false);
  assert.equal(f.pointer('pointerup',206,180,1300,{target:outside}).defaultPrevented,false);
  assert.equal(f.send('click',{target:outside,pointerId:1,detail:1}).defaultPrevented,false);
  assert.equal(f.calls.length,0);
});
test('new navigation after pointercancel with reused mouse ID is not swallowed',t=>{
  const f=fixture(t);f.pointer('pointerdown',250,180);f.pointer('pointercancel',250,180,1050);
  assert.equal(f.input.contactActive(),false);
  f.swipe(250,180,160,180,100,{pointerType:'mouse'});assert.equal(f.calls.length,1);
});
test('older browser compatibility clicks without pointerId cannot click through, but a fresh outside click works',t=>{
  const f=fixture(t); const outside={};
  f.pointer('pointerdown',250,180);f.pointer('pointercancel',250,180,1050);
  assert.equal(f.send('click',{target:outside,detail:1}).defaultPrevented,true);
  f.pointer('pointerdown',206,180,1200,{target:outside});f.pointer('pointerup',206,180,1300,{target:outside});
  assert.equal(f.send('click',{target:outside,detail:1}).defaultPrevented,false);
});
test('document-capture ownership prevents later capture listeners from seeing the same screen contact',t=>{
  const f=fixture(t,{mode:'capture'});let later=0;
  f.document.addEventListener('pointerdown',()=>later++);
  f.pointer('pointerdown',206,180);assert.equal(later,0);assert.equal(f.calls.length,1);
});
test('inactive, nonprimary, right button, invalid geometry or clock cannot act',t=>{
  for(const prepare of [f=>f.enable(false),f=>{f.document.hidden=true;},f=>{f.surface.isConnected=false;},
    f=>f.send('blur',{target:f.window},f.window)]) {
    const f=fixture(t,{mode:'capture'});prepare(f);f.pointer('pointerdown');assert.equal(f.calls.length,0);
  }
  for(const extra of [{isPrimary:false},{button:2},{clientX:NaN}]) {
    const f=fixture(t,{mode:'capture'});f.pointer('pointerdown',206,180,1000,extra);assert.equal(f.calls.length,0);
  }
  const clock=fixture(t,{mode:'capture'});clock.pointer('pointerdown',206,180,NaN);assert.equal(clock.calls.length,0);
});
test('surface alone prevents scrolling/selection; destroy restores styles and removes handlers',t=>{
  const f=fixture(t);assert.equal(f.surface.style.touchAction,'none');assert.equal(f.surface.style.userSelect,'none');
  f.input.destroy();assert.equal(f.surface.style.touchAction,'pan-y');assert.equal(f.surface.style.userSelect,'text');
  assert.equal(f.pointer('pointerdown').defaultPrevented,false);assert.equal(f.calls.length,0);
});

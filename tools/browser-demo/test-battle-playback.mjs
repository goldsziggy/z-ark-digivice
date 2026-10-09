#!/usr/bin/env node
import assert from 'node:assert/strict';
import test from 'node:test';
import createCore from '../../docs/play/runtime/demo-core.js';
import { buildBattleFrames, createBattlePlayback, BATTLE_ACTOR_MS } from '../../docs/play/battle-playback.js';

async function fixture(mode = 'auto') {
  const core = await createCore();
  const call = (name, args = []) => JSON.parse(core.ccall(name, 'string', args.map(v => typeof v === 'number' ? 'number' : 'string'), args));
  let state = call('demo_reset', [12345]).state;
  const command = (name, value = 0) => {
    const before = state, result = call('demo_command', [name, value]);
    assert.equal(result.ok, true, `${name}: ${result.error}`);
    state = result.state;
    return {before, result, frames: buildBattleFrames(before, result, name)};
  };
  command('hatch', 1);
  if (mode === 'auto') command('mode', 1);
  command('demo-encounter');
  return {command, state: () => state, snapshot: () => core.ccall('demo_snapshot', 'string', [], [])};
}

test('real native Auto trace expands into each actor, exact skill and HP change before capture', async () => {
  const h = await fixture(), {before, result, frames} = h.command('auto-fight');
  assert.equal(result.state.autoCapture, 1);
  assert.equal(result.trace.outcome, 'none');
  assert.equal(frames.length, 6);
  assert.deepEqual(frames.map(f => f.actor), ['player','enemy','player','enemy','player','enemy']);
  assert.deepEqual(frames.map(f => f.damage), [26,14,13,7,26,14]);
  assert.deepEqual(frames.map(f => f.move), ['magic','physical','physical','magic','magic','physical']);
  assert.equal(frames[0].skill, 'Night of Fire');
  assert.equal(frames[1].skill, 'Kuma Paw Hook');
  assert.equal(frames[0].playerHpAfter, before.hp);
  assert.equal(frames.at(-1).playerHpAfter, result.state.hp);
  assert.equal(frames.at(-1).enemyHpAfter, result.state.wildHp);
  for (let i=1;i<frames.length;i++) {
    assert.equal(frames[i].playerHpBefore, frames[i-1].playerHpAfter);
    assert.equal(frames[i].enemyHpBefore, frames[i-1].enemyHpAfter);
  }
});

test('Auto resume preserves identities after terminal save, no post-KO response and one XP award', async () => {
  const h = await fixture(); h.command('auto-fight');
  const {before,result,frames} = h.command('auto-resume'), saved = h.snapshot();
  assert.equal(result.state.phase, 'home'); assert.equal(result.state.wildName, null);
  assert.equal(result.state.xp - before.xp, 26);
  assert.equal(frames.length, 7); assert.equal(frames.at(-1).actor, 'player');
  assert.equal(frames.at(-1).enemyHpAfter, 0); assert.equal(frames.at(-1).damage, 7);
  assert.deepEqual(frames.map(f => f.turn), [4,4,5,5,6,6,7]);
  assert.equal(frames.at(-1).scene.wildName, 'Kumamon');
  const playback = createBattlePlayback(), metadata = {state: result.state};
  assert(playback.start(frames, metadata, 0));
  for (let i=0;i<frames.length;i++) {
    assert.equal(playback.sample(i*BATTLE_ACTOR_MS).index, i);
    assert.equal(playback.sample(i*BATTLE_ACTOR_MS+500).index, i);
    assert.equal(h.snapshot(), saved, 'Playing committed frames never rerolls or re-awards');
  }
  assert.equal(playback.sample(frames.length*BATTLE_ACTOR_MS).complete, true);
  assert.equal(playback.sample(999999).complete, true);
  assert.equal(playback.finish(), metadata); assert.equal(playback.finish(), null);
  assert.equal(playback.sample(999999), null); assert.equal(h.snapshot(), saved);
});

test('real tactical attacks use player first, alternate enemy moves and damage at each impact', async () => {
  const h = await fixture('tactical');
  for (const [name, enemyMove] of [['attack','physical'],['magic','magic']]) {
    const {before,result,frames} = h.command(name);
    assert.equal(result.trace, null); assert.equal(frames.length,2);
    assert.equal(frames[0].actor,'player'); assert.equal(frames[1].move,enemyMove);
    assert.equal(frames[0].damage,before.wildHp-result.state.wildHp);
    assert.equal(frames[1].damage,before.hp-result.state.hp);
    const p=createBattlePlayback();p.start(frames,{},0);
    assert.equal(p.sample(100).enemyHp,before.wildHp);
    assert.equal(p.sample(500).enemyHp,result.state.wildHp);
    assert.equal(p.sample(500).playerHp,before.hp);
    assert.equal(p.sample(1100).playerHp,before.hp);
    assert.equal(p.sample(1600).playerHp,result.state.hp);
  }
});

test('Heavy against Counter is reflection, not a miss or an extra ordinary response', async () => {
  const h = await fixture('tactical'); h.command('attack'); h.command('magic');
  const {before,result,frames}=h.command('heavy');
  assert.equal(before.wildGuard,'counter'); assert.equal(frames.length,2);
  assert.equal(frames[0].damage,0); assert.equal(frames[0].reflected,true);
  assert.equal(frames[1].move,'counter'); assert.equal(frames[1].skill,'Counter');
  assert.equal(frames[1].damage,11); assert.equal(result.state.wildHp,before.wildHp);
  assert.equal('miss' in frames[0],false); assert.equal('critical' in frames[0],false);
});

test('tactical victory clamps damage to remaining HP and omits a defeated enemy response', async () => {
  const h = await fixture('tactical'); let last;
  for(let i=0;i<20 && h.state().phase==='encounter';i++) last=h.command('magic');
  assert.equal(last.result.state.message,'A friendly battle won.');
  assert.equal(last.frames.length,1); assert.equal(last.frames[0].enemyHpAfter,0);
  assert.equal(last.frames[0].damage,last.before.wildHp);
  assert.equal(last.frames[0].playerHpAfter,last.before.hp);
  assert.equal(last.result.state.xp,26);
});

test('a real level-up reward cannot appear as healing during the last Auto attack', async () => {
  const h=await fixture(); h.command('auto-fight'); h.command('auto-resume');
  while(h.state().hp<h.state().combat.maxHp || h.state().energy<80) h.command('rest');
  for(let i=0;i<10;i++){h.command('feed');h.command('play');}
  h.command('demo-encounter');h.command('auto-fight');
  const {result,frames}=h.command('auto-resume');
  assert.equal(result.state.level,2);assert.equal(result.state.xp,52);
  assert.equal(result.state.hp,34);assert.equal(frames.at(-1).playerHpAfter,32);
  assert.equal(frames.at(-1).scene.level,1);assert.equal(frames.at(-1).scene.combat.maxHp,92);
});

test('tactical normal retreat shows partner KO without fabricating cleared foe HP or damage', async () => {
  const h = await fixture('tactical'); let last;
  for(let i=0;i<20 && h.state().phase==='encounter';i++) last=h.command('attack');
  assert.match(last.result.state.message,/gentle retreat/);
  assert.equal(last.result.state.hp,10,'Durable gentle recovery is not a combat heal');
  assert.equal(last.frames[0].enemyHpAfter,null); assert.equal(last.frames[0].damage,null);
  assert.equal(last.frames[1].playerHpAfter,0); assert.equal(last.frames[1].damage,last.before.hp);
  assert.equal(last.frames[1].enemyHpAfter,null); assert.equal(last.result.state.xp,0);
});

test('tactical reflected retreat keeps exact foe HP while applying capped self damage', async () => {
  const h = await fixture('tactical'); let last;
  for(let i=0;i<9;i++) last=h.command(['attack','magic','heavy'][i%3]);
  assert.match(last.result.state.message,/gentle retreat/);
  assert.equal(last.frames[0].enemyHpAfter,47);assert.equal(last.frames[0].damage,0);
  assert.equal(last.frames[1].move,'counter');assert.equal(last.frames[1].damage,7);
  assert.equal(last.frames[1].playerHpAfter,0);
});

test('capture interruption is a boundary; native throws never become fake attack frames', async () => {
  const h = await fixture(); const {frames}=h.command('auto-fight'), saved=h.snapshot();
  const p=createBattlePlayback(); p.start(frames,{openCapture:true},0);p.sample(500);
  assert.equal(h.snapshot(),saved); assert.equal(p.finish().openCapture,true);
  const attempted=h.command('ring-capture',0);
  assert.deepEqual(attempted.frames,[]);assert.equal(attempted.result.state.lastCapture.attempt,1);
  assert.notEqual(h.snapshot(),saved);
});

test('a slow frame cannot skip actors or their impact; timeline rebases without catch-up', async () => {
  const h=await fixture(), {frames}=h.command('auto-fight');
  const p=createBattlePlayback(); p.start(frames,{},100);
  assert.equal(p.sample(100).index,0);
  const late=p.sample(10000); assert.equal(late.index,0); assert.equal(late.phase,'impact');
  const next=p.sample(20000); assert.equal(next.index,1); assert.equal(next.phase,'windup');
  assert.equal(p.sample(20001).index,1);
  assert.equal(p.sample(30000).index,1,'Late second actor must still expose its impact');
  assert.equal(p.sample(40000).index,2);
});

test('cancel/finish is idempotent and a repeated start cannot overwrite in-flight metadata', async () => {
  const h=await fixture(), {frames}=h.command('auto-fight'), saved=h.snapshot();
  const p=createBattlePlayback(), metadata={message:'Capture available'};
  assert.equal(p.start(frames,metadata,0),true);assert.equal(p.active(),true);
  assert.equal(p.start(frames,{},0),false);p.sample(500);
  assert.equal(p.finish(),metadata);assert.equal(p.active(),false);assert.equal(p.finish(),null);
  assert.equal(p.sample(100000),null);assert.equal(h.snapshot(),saved);
  assert.equal(p.start([],{},0),false);assert.equal(p.start(frames,{},NaN),false);
  assert.equal(p.start(frames,{next:true},1000),true);
  assert.equal(p.sample(0).progress,0,'Clock rollback cannot move playback backwards');
});

test('stale, invalid, non-battle and malformed native results are not replayed', async () => {
  const h=await fixture(), {before,result}=h.command('auto-fight');
  assert.deepEqual(buildBattleFrames(before,{...result,ok:false},'auto-fight'),[]);
  assert.deepEqual(buildBattleFrames(result.state,result,'auto-fight'),[]);
  assert.deepEqual(buildBattleFrames(before,result,'feed'),[]);
  const bad=structuredClone(result);bad.trace.steps[1].enemyHpBefore++;
  assert.deepEqual(buildBattleFrames(before,bad,'auto-fight'),[]);
});

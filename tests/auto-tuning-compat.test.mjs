// Real baseline service records are immutable compatibility fixtures. These
// checks never synthesize old traces under the current combat implementation.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { validateAutoTrace, autoStepText } from '../web/auto-battle.js';
import { createBattleClient, BATTLE_PENDING_KEY } from '../web/battle-client.js';

const fixture = JSON.parse(readFileSync(new URL('./fixtures/auto-tuning-baseline16-service.json', import.meta.url)));
const response = body => new Response(JSON.stringify(body), { status: 200, headers: { 'Content-Type': 'application/json' } });
function storage(raw) {
  const values = new Map([[BATTLE_PENDING_KEY, raw]]);
  return { getItem: key => values.get(key) ?? null, setItem: (key, value) => values.set(key, value), removeItem: key => values.delete(key) };
}

test('genuine care8 and practice6 Auto records retain frozen actors, names and every saved step', () => {
  assert.equal(fixture.sourceCommit, '16feeddf58f7637b51564c83dd24e1e6e9a2ba23');
  const wild = fixture.care.checkpoints.autoResult.response.body.autoTrace;
  assert.deepEqual(fixture.care.checkpoints.afterCare.response.body.autoTrace, wild);
  const practice = fixture.practice.find(entry => entry.mode === 'auto').expectedCurrent;
  assert.equal(practice.battle.rulesVersion, 6); assert.equal(practice.battle.exchanges, 40); assert.equal(practice.battle.status, 'draw');
  for (const original of [wild, practice.autoTrace]) {
    const before = JSON.stringify(original), parsed = validateAutoTrace(original, original.kind);
    assert.deepEqual(parsed, original);
    for (const step of parsed.steps) {
      const text = autoStepText(step, parsed);
      if (['physical', 'heavy', 'magic'].includes(step.action)) assert.ok(text.includes(original.player.combat.skills[step.action]));
      if (step.phase === 'defend' && ['physical', 'heavy', 'magic'].includes(step.opponentAction)) assert.ok(text.includes(original.enemy.combat.skills[step.opponentAction]));
    }
    assert.equal(JSON.stringify(original), before);
    parsed.player.name = 'Untrusted view mutation'; assert.notEqual(original.player.name, parsed.player.name);
  }
});

test('genuine practice6 pending start receipt survives reload and exact retry without reinterpretation', async () => {
  const entry = fixture.practice.find(entry => entry.mode === 'auto');
  const body = JSON.stringify(entry.commands[0].body);
  const raw = JSON.stringify({ formatVersion: 1, rulesVersion: 6, deviceId: entry.deviceId, path: '/api/battle/start', body });
  const store = storage(raw), posts = [];
  const client = createBattleClient({ storage: store, fetcher: async (_path, options) => { if (options.body) posts.push(options.body); return response(entry.expectedCurrent); } });
  await client.load({ deviceId: entry.deviceId, token: Buffer.alloc(32, 88).toString('base64url') });
  assert.equal(client.getState().loaded, true); assert.deepEqual(posts, []);
  assert.equal(store.getItem(BATTLE_PENDING_KEY), raw); assert.deepEqual(client.getState().autoTrace, entry.expectedCurrent.autoTrace);
  await client.retry(); assert.deepEqual(posts, [body]); assert.equal(store.getItem(BATTLE_PENDING_KEY), null);
  assert.deepEqual(client.getState().state, entry.expectedCurrent.battle); assert.deepEqual(client.getState().autoTrace, entry.expectedCurrent.autoTrace);
});

test('retained practice6 active commands stay6; a rejected uncommitted start is never upgraded or silently discarded', async () => {
  const entry = fixture.practice.find(entry => entry.mode === 'tactical');
  let sent;
  const active = createBattleClient({ storage: storage(null), uuid: () => 'active_six_command_01', fetcher: async (_path, options) => {
    if (!options.body) return response(entry.expectedCurrent);
    sent = JSON.parse(options.body); return response(entry.continuation[0].response);
  } });
  const credential = { deviceId: entry.deviceId, token: Buffer.alloc(32, 88).toString('base64url') };
  await active.load(credential); await active.act(entry.continuation[0].body.action);
  assert.equal(sent.rulesVersion, 6); assert.equal(active.getState().pending, false);
  const body = JSON.stringify({ rulesVersion: 6, expectedRevision: 0, requestId: 'uncommitted_six_start', mode: 'auto' });
  const raw = JSON.stringify({ formatVersion: 1, rulesVersion: 6, deviceId: entry.deviceId, path: '/api/battle/start', body });
  const saved = storage(raw); let posts = 0;
  const retry = createBattleClient({ storage: saved, fetcher: async (_path, options) => {
    if (!options.body) return response({ revision: 0, battle: null, mode: 'tactical', autoTrace: null });
    posts++; assert.equal(options.body, body); return new Response('{"error":"battle_migration_required"}', { status: 409 });
  } });
  await retry.load(credential); await retry.archiveLegacyPending();
  assert.equal(posts, 0); assert.equal(saved.getItem(BATTLE_PENDING_KEY), raw);
  await retry.retry(); assert.equal(posts, 1); assert.equal(retry.getState().canArchiveLegacy, true);
  assert.equal(saved.getItem(BATTLE_PENDING_KEY), raw);
  await retry.archiveLegacyPending(); assert.equal(posts, 1); assert.equal(saved.getItem(BATTLE_PENDING_KEY), null);
});

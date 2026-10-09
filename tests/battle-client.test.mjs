import test from 'node:test';
import assert from 'node:assert/strict';
import { createBattleClient, BATTLE_PENDING_KEY, BATTLE_ARCHIVE_KEY } from '../web/battle-client.js';

const credential = { deviceId: `dv_${'a'.repeat(24)}`, token: 'b'.repeat(43) };
const other = { deviceId: `dv_${'c'.repeat(24)}`, token: 'd'.repeat(43) };
const combat = () => ({ maxHp: 100, attack: 18, defense: 14, magic: 16, resistance: 16, type: 'grove', skills: { physical: 'Twig Tap', heavy: 'Root Ram', magic: 'Seed Spark' } });
const battle = () => ({ schemaVersion: 7, rulesVersion: 7, maxExchanges: 40, playerSpecies: 'mote', enemySpecies: 'flicker', playerFormId: 1, enemyFormId: 4, playerFormName: 'Mote', enemyFormName: 'Flicker', playerCombat: combat(), enemyCombat: { ...combat(), type: 'neutral' }, sequence: 0, phase: 'attack', status: 'active',
  playerHp: 100, enemyHp: 100, playerLevel: 1, enemyLevel: 1, exchanges: 0, cardUsed: false, attackBoost: 0, shield: 0,
  enemyHint: ['brace', 'counter'], lastEnemyMoves: [], lastTurn: null,
  companion: { id: 1, species: 'mote', name: 'Mote', level: 1, formId: 1 }, enemy: { species: 'flicker', name: 'Flicker' } });
function legacyBattle() {
  const value = battle(); value.schemaVersion = value.rulesVersion = 2; delete value.maxExchanges;
  for (const key of ['playerFormId', 'enemyFormId', 'playerFormName', 'enemyFormName']) delete value[key];
  delete value.companion.formId; value.enemy.name = 'Practice rival'; return value;
}
const reply = (revision, state = battle()) => ({ revision, battle: state, mode: 'tactical', autoTrace: null });
const response = (value, status = 200) => new Response(JSON.stringify(value), { status, headers: { 'Content-Type': 'application/json' } });
function storage(initial = null) {
  const values = new Map(initial === null ? [] : [[BATTLE_PENDING_KEY, initial]]), writes = [];
  return { values, writes, getItem: key => values.get(key) ?? null,
    setItem(key, value) { writes.push(['set', key]); values.set(key, value); },
    removeItem(key) { writes.push(['remove', key]); values.delete(key); } };
}
function factory(store, fetcher, extras = {}) {
  let next = 0;
  return createBattleClient({ storage: store, fetcher, uuid: () => `battle_request_${String(++next).padStart(4, '0')}`, ...extras });
}

test('battle mode is explicitly saved before start and older Tactical pending keeps its exact body', async () => {
  for (const mode of ['tactical', 'auto']) {
    const store = storage(); let posted;
    const client = factory(store, async (_path, options) => {
      if (!options.body) return response(reply(0, null));
      posted = options.body;
      assert.equal(JSON.parse(JSON.parse(store.getItem(BATTLE_PENDING_KEY)).body).mode, mode);
      throw new Error('lost reply');
    });
    await client.load(credential); await client.start('unknown');
    assert.equal(posted, undefined);
    await client.start(mode); assert.equal(JSON.parse(posted).mode, mode);
    const stored = store.getItem(BATTLE_PENDING_KEY);
    await client.start(mode === 'auto' ? 'tactical' : 'auto');
    assert.equal(store.getItem(BATTLE_PENDING_KEY), stored, 'a pending mode cannot be replaced');
  }
  const originalBody = JSON.stringify({ rulesVersion: 2, expectedRevision: 0, requestId: 'old_tactical_start_01' });
  const store = storage(JSON.stringify({ formatVersion: 1, rulesVersion: 2, deviceId: credential.deviceId, path: '/api/battle/start', body: originalBody }));
  let posted;
  const client = factory(store, async (_path, options) => { if (options.body) posted = options.body; return response(options.body ? reply(1, legacyBattle()) : reply(0, null)); });
  await client.load(credential); await client.retry();
  assert.equal(posted, originalBody); assert.equal(client.getState().pending, false);
});

test('atomic Auto result and replay survive a lost reply without a second battle', async () => {
  const participant = { species: 'mote', name: 'Mote', level: 1, combat: combat() };
  const trace = { formatVersion: 1, mode: 'auto', kind: 'practice', startSequence: 0, endSequence: 1, outcome: 'won',
    player: participant, enemy: { ...participant, species: 'flicker', name: 'Flicker' },
    steps: [{ turn: 1, phase: 'attack', action: 'physical', opponentAction: 'brace', playerHpBefore: 100, playerHpAfter: 100,
      enemyHpBefore: 10, enemyHpAfter: 0, reflected: false, captured: false }] };
  const result = { revision: 1, battle: { ...battle(), phase: 'finished', status: 'won', sequence: 1, enemyHp: 0, enemyHint: [] }, mode: 'auto', autoTrace: trace };
  const store = storage(); let posts = 0; const bodies = [];
  const fetcher = async (_path, options) => {
    if (!options.body) return response(posts ? result : reply(0, null));
    bodies.push(options.body); posts++;
    if (posts === 1) throw new Error('reply lost after commit');
    return response(result);
  };
  const first = factory(store, fetcher); await first.load(credential); await first.start('auto');
  const saved = store.getItem(BATTLE_PENDING_KEY);
  const restored = factory(store, fetcher); await restored.load(credential);
  assert.equal(posts, 1, 'GET/reload never starts or resumes commands automatically');
  assert.equal(restored.getState().mode, 'auto'); assert.deepEqual(restored.getState().autoTrace, trace);
  assert.equal(store.getItem(BATTLE_PENDING_KEY), saved);
  await restored.retry(); assert.equal(bodies[0], bodies[1]); assert.equal(restored.getState().pending, false);
  const returned = restored.getState(); returned.autoTrace.player.name = 'Changed';
  assert.equal(restored.getState().autoTrace.player.name, 'Mote');
});

test('known Rookie participants are accepted while unknown species stay rejected', async () => {
  for (const value of ['impmon', 'agumon', 'gabumon', 'patamon', 'tentomon', 'palmon', 'gomamon', 'renamon', 'unknown-rookie']) {
    const state = battle(); state.schemaVersion = state.rulesVersion = 3; delete state.maxExchanges; state.playerSpecies = value;
    state.playerFormName = value;
    state.companion = { ...state.companion, species: value, name: value };
    const client = factory(storage(), async () => response(reply(1, state)));
    await client.load(credential);
    assert.equal(client.getState().loaded, value !== 'unknown-rookie');
    if (value !== 'unknown-rookie') assert.equal(client.getState().state.companion.species, value);
    else assert.equal(client.getState().state, null);
  }
  const invalid = battle(); invalid.companion.species = 'impmon';
  const client = factory(storage(), async () => response(reply(1, invalid)));
  await client.load(credential);
  assert.equal(client.getState().loaded, false, 'participant and authoritative player species must match');
});

test('new full-roster epoch accepts high form and permanent member IDs without loosening legacy epochs', async () => {
  const state = battle(); state.playerFormId = state.companion.formId = 276;
  state.playerSpecies = state.companion.species = 'calumon'; state.playerFormName = state.companion.name = 'Calumon'; state.companion.id = 4294967294;
  const client = factory(storage(), async () => response(reply(1, state))); await client.load(credential);
  assert.equal(client.getState().loaded, true); assert.equal(client.getState().state.playerFormId, 276);
  for (const change of [value => { value.schemaVersion = value.rulesVersion = 3; }, value => { value.playerFormId = value.companion.formId = 513; }]) {
    const invalid = structuredClone(state); change(invalid);
    const rejected = factory(storage(), async () => response(reply(1, invalid))); await rejected.load(credential); assert.equal(rejected.getState().loaded, false);
  }
});

test('lost acknowledgment survives reload and retries identical bytes without regressing a newer GET', async () => {
  const store = storage(), requests = []; let committed = false;
  const fetcher = async (path, options) => {
    requests.push({ path, body: options.body });
    assert.equal(options.headers.Authorization, `Bearer ${credential.token}`);
    if (!options.body) return response(committed ? reply(2, { ...battle(), sequence: 1, enemyHp: 88 }) : reply(0, null));
    assert.ok(store.getItem(BATTLE_PENDING_KEY), 'durable record exists before sending');
    if (!committed) { committed = true; throw new Error('lost acknowledgment'); }
    return response(reply(1));
  };
  const first = factory(store, fetcher);
  await first.load(credential); await first.start();
  assert.equal(first.getState().pending, true); assert.equal(first.getState().canSwitch, false);
  const saved = store.getItem(BATTLE_PENDING_KEY);
  assert.equal(saved.includes(credential.token), false);
  const reloaded = factory(store, fetcher); await reloaded.load(credential);
  assert.equal(reloaded.getState().revision, 2); assert.equal(store.getItem(BATTLE_PENDING_KEY), saved);
  await reloaded.retry();
  assert.equal(requests.filter(item => item.body).length, 2);
  assert.equal(requests.filter(item => item.body)[0].body, requests.filter(item => item.body)[1].body);
  assert.equal(JSON.parse(requests.at(-1).body).expectedRevision, 0);
  assert.equal(requests.at(-2).path, '/api/battle');
  assert.equal(reloaded.getState().revision, 2); assert.equal(reloaded.getState().state.enemyHp, 88);
  assert.equal(reloaded.getState().pending, false); assert.equal(store.getItem(BATTLE_PENDING_KEY), null);
  assert.ok(store.writes.every(([, key]) => key === BATTLE_PENDING_KEY));
});

test('double clicks coalesce and a pending command blocks new actions and identity switching', async () => {
  const store = storage(); let posts = 0, release;
  const waiting = new Promise(resolve => { release = resolve; });
  const client = factory(store, async (_path, options) => {
    if (!options.body) return response(reply(0, null));
    posts++; await waiting; throw new Error('disconnected');
  });
  await client.load(credential); const first = client.start();
  await client.start(); await client.act({ type: 'physical', value: 0 });
  assert.equal(posts, 1); assert.equal(client.getState().busy, true);
  await client.load(other); assert.equal(client.getState().ownerDeviceId, credential.deviceId);
  release(); await first;
  const raw = store.getItem(BATTLE_PENDING_KEY);
  await client.load(other); await client.start(); await client.act({ type: 'magic', value: 0 });
  assert.equal(posts, 1); assert.equal(store.getItem(BATTLE_PENDING_KEY), raw);
  assert.equal(client.getState().ownerDeviceId, credential.deviceId);
});

test('unknown or corrupt pending payloads remain byte-for-byte intact and never POST', async () => {
  const valid = { formatVersion: 1, rulesVersion: 1, deviceId: credential.deviceId, path: '/api/battle/start', body: JSON.stringify({ expectedRevision: 0, requestId: 'original_request_01' }) };
  for (const raw of ['{broken', 'x'.repeat(2049), JSON.stringify({ ...valid, rulesVersion: 99 }), JSON.stringify({ ...valid, path: 'https://example.com' }), JSON.stringify({ ...valid, token: 'unexpected' })]) {
    const store = storage(raw); let posts = 0;
    const client = factory(store, async (_path, options) => { if (options.body) posts++; return response(reply(0, null)); });
    await client.load(credential); await client.retry(); await client.start();
    assert.equal(client.getState().recoveryRequired, true); assert.equal(client.getState().canSwitch, false);
    assert.equal(store.getItem(BATTLE_PENDING_KEY), raw); assert.deepEqual(store.writes, []); assert.equal(posts, 0);
  }
});

test('storage failure prevents transmission and invalid action input cannot create pending data', async () => {
  const store = storage(); let posts = 0;
  const client = factory(store, async (_path, options) => { if (options.body) posts++; return response(reply(0, null)); });
  await client.load(credential);
  for (const action of [{ type: 'physical', value: 1 }, { type: 'card', value: 3 }, { type: 'unknown', value: 0 }, { type: 'magic', value: 0, seed: 1 }]) await client.act(action);
  assert.equal(posts, 0); assert.equal(store.getItem(BATTLE_PENDING_KEY), null);
  store.setItem = () => { throw new Error('quota exceeded'); };
  await client.start(); assert.equal(posts, 0); assert.equal(client.getState().pending, false);
  assert.match(client.getState().error, /could not be saved or sent/);
});

test('malformed successful acknowledgments keep pending commands and never expose hidden state', async () => {
  const invalid = [reply(1, null), reply(2), reply(1, { ...battle(), rulesVersion: 99 }), reply(1, { ...battle(), playerHp: -1 }),
    reply(1, { ...battle(), enemyHint: ['brace', 'brace'] }), reply(1, { ...battle(), rng: 'PRIVATE_SENTINEL' }),
    reply(1, { ...battle(), companion: { ...battle().companion, token: 'PRIVATE_SENTINEL' } })];
  for (const bad of invalid) {
    const store = storage(), client = factory(store, async (_path, options) => response(options.body ? bad : reply(0, null)));
    await client.load(credential); await client.start();
    assert.equal(client.getState().pending, true); assert.equal(client.getState().state, null);
    assert.equal(client.getState().revision, 0); assert.ok(store.getItem(BATTLE_PENDING_KEY));
    assert.equal(JSON.stringify(client.getState()).includes('PRIVATE_SENTINEL'), false);
  }
});

test('HTTP conflicts and secret-bearing failures are sanitized without dropping saved requests', async () => {
  for (const status of [401, 409, 422, 503]) {
    const store = storage(), client = factory(store, async (_path, options) => options.body ? response({ message: 'PRIVATE_SENTINEL', token: 'PRIVATE_SENTINEL' }, status) : response(reply(0, null)));
    await client.load(credential); await client.start();
    assert.equal(client.getState().pending, true); assert.ok(store.getItem(BATTLE_PENDING_KEY));
    assert.equal(client.getState().recoveryRequired, status === 409 || status === 422);
    assert.equal(JSON.stringify(client.getState()).includes('PRIVATE_SENTINEL'), false);
  }
});

test('stalled headers and oversized bodies terminate and retain exact pending commands', async () => {
  for (const mode of ['headers', 'body', 'length']) {
    const store = storage(); let cancelled = false;
    const client = factory(store, async (_path, options) => {
      if (!options.body) return response(reply(0, null));
      if (mode === 'headers') return new Promise(() => {});
      return new Response(new ReadableStream({ pull() { return new Promise(() => {}); }, cancel() { cancelled = true; } }), { headers: mode === 'length' ? { 'Content-Length': '32769' } : {} });
    }, { requestTimeoutMs: 20 });
    await client.load(credential); await client.start();
    assert.equal(client.getState().busy, false); assert.equal(client.getState().pending, true);
    if (mode !== 'headers') assert.equal(cancelled, true);
  }
});

test('successful action uses current revision and external callers cannot mutate public client state', async () => {
  const store = storage(); let observed;
  const client = factory(store, async (_path, options) => {
    if (!options.body) return response(reply(4));
    observed = JSON.parse(options.body); return response(reply(5, { ...battle(), sequence: 1, cardUsed: true, attackBoost: 5 }));
  });
  await client.load(credential); const publicState = client.getState(); publicState.state.enemyHp = 0;
  assert.equal(client.getState().state.enemyHp, 100);
  await client.act({ type: 'card', value: 1 });
  assert.equal(observed.expectedRevision, 4); assert.deepEqual(observed.action, { type: 'card', value: 1 });
  assert.equal(client.getState().revision, 5); assert.equal(client.getState().state.cardUsed, true);
  assert.equal(client.getState().pending, false);
});


test('old pending commands require explicit archive after fresh authoritative GET and are never replayed', async () => {
  const raw = JSON.stringify({ formatVersion: 1, rulesVersion: 1, deviceId: credential.deviceId, path: '/api/battle/start', body: JSON.stringify({ expectedRevision: 0, requestId: 'original_request_01' }) });
  const store = storage(raw); let gets = 0, posts = 0;
  const client = factory(store, async (_path, options) => { if(options.body) posts++; else gets++; return response(reply(4)); });
  await client.load(credential); await client.retry(); await client.start();
  assert.equal(client.getState().legacyPending, true); assert.equal(client.getState().canArchiveLegacy, true);
  assert.equal(store.getItem(BATTLE_PENDING_KEY), raw); assert.equal(posts, 0);
  await client.archiveLegacyPending();
  assert.equal(gets, 2); assert.equal(posts, 0); assert.equal(store.getItem(BATTLE_ARCHIVE_KEY), raw);
  assert.equal(store.getItem(BATTLE_PENDING_KEY), null); assert.equal(client.getState().recoveryRequired, false);
});

test('legacy archive never overwrites another archive or clears pending after quota failure', async () => {
  const raw = JSON.stringify({ formatVersion: 1, rulesVersion: 1, deviceId: credential.deviceId, path: '/api/battle/start', body: JSON.stringify({ expectedRevision: 0, requestId: 'original_request_01' }) });
  for (const mode of ['occupied', 'quota']) {
    const store = storage(raw); if(mode === 'occupied') store.values.set(BATTLE_ARCHIVE_KEY, 'previous archive');
    const client = factory(store, async () => response(reply(1)));
    await client.load(credential);
    if(mode === 'quota') store.setItem = () => { throw new Error('quota'); };
    await client.archiveLegacyPending();
    assert.equal(store.getItem(BATTLE_PENDING_KEY), raw); assert.equal(client.getState().recoveryRequired, true);
    if(mode === 'occupied') assert.equal(store.getItem(BATTLE_ARCHIVE_KEY), 'previous archive');
  }
});

test('existing v2 duel commands keep v2; rejected older starts require explicit reconciliation and archive', async () => {
  const store = storage(); let sent;
  const client = factory(store, async (_path, options) => {
    if (!options.body) return response(reply(4, legacyBattle()));
    sent = JSON.parse(options.body); return response(reply(5, { ...legacyBattle(), sequence: 1 }));
  });
  await client.load(credential); await client.act({ type: 'physical', value: 0 });
  assert.equal(sent.rulesVersion, 2); assert.equal(client.getState().pending, false);

  const body = JSON.stringify({ rulesVersion: 2, expectedRevision: 0, requestId: 'uncommitted_old_start', mode: 'tactical' });
  const raw = JSON.stringify({ formatVersion: 1, rulesVersion: 2, deviceId: credential.deviceId, path: '/api/battle/start', body });
  const preserved = storage(raw); let posts = 0;
  const restored = factory(preserved, async (_path, options) => {
    if (options.body) { posts++; assert.equal(options.body, body); return response({ error: 'battle_migration_required' }, 409); }
    return response(reply(0, null));
  });
  await restored.load(credential);
  assert.equal(restored.getState().canArchiveLegacy, false); assert.equal(posts, 0);
  await restored.retry(); assert.equal(restored.getState().canArchiveLegacy, true);
  assert.equal(preserved.getItem(BATTLE_PENDING_KEY), raw);
  await restored.archiveLegacyPending(); assert.equal(posts, 1);
  assert.equal(preserved.getItem(BATTLE_ARCHIVE_KEY), raw); assert.equal(preserved.getItem(BATTLE_PENDING_KEY), null);
});

test('new practice starts use v7 while existing v4 battle names and command bytes stay frozen', async () => {
  const old = battle(); old.schemaVersion = old.rulesVersion = 4; delete old.maxExchanges;
  old.playerCombat.skills.magic = 'Historical Spark';
  let sent;
  const client = factory(storage(), async (_path, options) => {
    if (!options.body) return response(reply(2, old));
    sent = JSON.parse(options.body); return response(reply(3, { ...old, sequence: 1 }));
  });
  await client.load(credential); await client.act({ type: 'magic', value: 0 });
  assert.equal(sent.rulesVersion, 4); assert.equal(client.getState().state.playerCombat.skills.magic, 'Historical Spark');
  const current = factory(storage(), async (_path, options) => {
    if (!options.body) return response(reply(0, null));
    assert.equal(JSON.parse(options.body).rulesVersion, 7); return response(reply(1));
  });
  await current.load(credential); await current.start('tactical'); assert.equal(current.getState().pending, false);
  const body = JSON.stringify({ rulesVersion: 4, expectedRevision: 0, requestId: 'old_uncommitted_start_04', mode: 'tactical' });
  const raw = JSON.stringify({ formatVersion: 1, rulesVersion: 4, deviceId: credential.deviceId, path: '/api/battle/start', body });
  const store = storage(raw); let posts = 0;
  const restored = factory(store, async (_path, options) => {
    if (!options.body) return response(reply(0, null));
    posts++; assert.equal(options.body, body); return response({ error: 'battle_migration_required' }, 409);
  });
  await restored.load(credential); assert.equal(posts, 0); await restored.retry();
  assert.equal(restored.getState().canArchiveLegacy, true); assert.equal(store.getItem(BATTLE_PENDING_KEY), raw);
  await restored.archiveLegacyPending(); assert.equal(posts, 1); assert.equal(store.getItem(BATTLE_ARCHIVE_KEY), raw);
});

test('practice v5 through v7 allow40 exchanges; v2–4 keep their30-exchange contract', async () => {
  for (const version of [2,3,4,5,6,7]) {
    for (const count of [30,31,40,41]) {
      const state=version===2?legacyBattle():battle();state.schemaVersion=state.rulesVersion=version;state.exchanges=count;state.sequence=count;
      if(version<=4)delete state.maxExchanges;
      const client=factory(storage(),async()=>response(reply(1,state)));await client.load(credential);
      assert.equal(client.getState().loaded,count<=(version>=5?40:30));
    }
  }
  const wrong=battle();wrong.maxExchanges=99;const client=factory(storage(),async()=>response(reply(1,wrong)));await client.load(credential);assert.equal(client.getState().loaded,false);
});

test('v5 Auto receipt retry preserves exact bytes, frozen names and terminal result after a v6 upgrade', async () => {
  const old = battle(); old.schemaVersion = old.rulesVersion = 5;
  old.phase = 'finished'; old.status = 'won'; old.sequence = old.exchanges = 1; old.enemyHp = 0; old.enemyHint = [];
  old.playerCombat.skills.magic = 'Frozen v5 Flame'; old.enemyCombat.skills.heavy = 'Frozen v5 Charge';
  const trace = { formatVersion: 1, mode: 'auto', kind: 'practice', startSequence: 0, endSequence: 1, outcome: 'won',
    player: { species: old.playerSpecies, name: old.playerFormName, level: 1, combat: old.playerCombat },
    enemy: { species: old.enemySpecies, name: old.enemyFormName, level: 1, combat: old.enemyCombat },
    steps: [{ turn: 1, phase: 'attack', action: 'magic', opponentAction: 'brace', playerHpBefore: 100, playerHpAfter: 100, enemyHpBefore: 10, enemyHpAfter: 0, reflected: false, captured: false }] };
  const result = { revision: 1, battle: old, mode: 'auto', autoTrace: trace };
  const body = JSON.stringify({ rulesVersion: 5, expectedRevision: 0, requestId: 'frozen_auto_start_v5', mode: 'auto' });
  const raw = JSON.stringify({ formatVersion: 1, rulesVersion: 5, deviceId: credential.deviceId, path: '/api/battle/start', body });
  const store = storage(raw); const posted = [];
  const client = factory(store, async (_path, options) => { if (options.body) posted.push(options.body); return response(result); });
  await client.load(credential);
  assert.deepEqual(posted, []); assert.equal(store.getItem(BATTLE_PENDING_KEY), raw); assert.equal(client.getState().canArchiveLegacy, false);
  assert.deepEqual(client.getState().autoTrace, trace);
  await client.retry(); assert.deepEqual(posted, [body]); assert.equal(store.getItem(BATTLE_PENDING_KEY), null);
  assert.deepEqual(client.getState().state, old); assert.deepEqual(client.getState().autoTrace, trace);
});

test('v5 active actions keep their epoch; an uncommitted v5 start is archived only after explicit rejection', async () => {
  const old = battle(); old.schemaVersion = old.rulesVersion = 5;
  let sent;
  const client = factory(storage(), async (_path, options) => {
    if (!options.body) return response(reply(4, old));
    sent = JSON.parse(options.body); return response(reply(5, { ...old, sequence: 1 }));
  });
  await client.load(credential); await client.act({ type: 'magic', value: 0 });
  assert.equal(sent.rulesVersion, 5); assert.equal(client.getState().pending, false);
  const body = JSON.stringify({ rulesVersion: 5, expectedRevision: 0, requestId: 'uncommitted_start_v5', mode: 'auto' });
  const raw = JSON.stringify({ formatVersion: 1, rulesVersion: 5, deviceId: credential.deviceId, path: '/api/battle/start', body });
  const store = storage(raw); let posts = 0;
  const restored = factory(store, async (_path, options) => {
    if (!options.body) return response(reply(0, null));
    posts++; assert.equal(options.body, body); return response({ error: 'battle_migration_required' }, 409);
  });
  await restored.load(credential); await restored.archiveLegacyPending();
  assert.equal(posts, 0); assert.equal(store.getItem(BATTLE_PENDING_KEY), raw); assert.equal(store.getItem(BATTLE_ARCHIVE_KEY), null);
  await restored.retry(); assert.equal(restored.getState().canArchiveLegacy, true); assert.equal(store.getItem(BATTLE_PENDING_KEY), raw);
  await restored.archiveLegacyPending(); assert.equal(posts, 1); assert.equal(store.getItem(BATTLE_ARCHIVE_KEY), raw); assert.equal(store.getItem(BATTLE_PENDING_KEY), null);
});

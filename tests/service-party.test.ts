import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createApp, startServer } from '../service/server.ts';
import { historicComparable } from './legacy-state-projection.ts';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
type Event = { type: string; value: number };
const event = (type: string, value = 0): Event => ({ type, value });
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 17, baseRevision, batchId, events });
const hash = (text: string | Buffer) => createHash('sha256').update(text).digest('hex');
const oldToken = Buffer.alloc(32, 65).toString('base64url'); // Temporary synthetic identity only.
const frozen = (events: Event[], version = 14) => JSON.parse(execFileSync(corePath, [`--replay-v${version}-onboarding-trace`, '12345'], {
  input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', maxBuffer: 128 * 1024,
}));

async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, original?: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-party-http-'));
  if (original) for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(original));
  const options = { rootDir, corePath, battleCorePath, dataDir, port: 0, seedSource: () => 12345 };
  let app = await startServer(options);
  const close = async () => { await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve())); app.close(); };
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (path: string, token?: string, body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, {
      method: body === undefined ? 'GET' : 'POST', headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    return { status: response.status, body: await response.json() as any };
  };
  const pair = async () => {
    const start = await request('/api/pairing/start', undefined, {}); assert.equal(start.status, 201);
    const claim = await request('/api/pairing/claim', undefined, { code: start.body.code }); assert.equal(claim.status, 201); return claim.body;
  };
  return { dataDir, request, pair, restart: async () => { await close(); app = await startServer(options); } };
}

async function roster(t: Parameters<typeof fixture>[0], minimum = 5) {
  const f = await fixture(t), identity = await f.pair();
  let save = identity, serial = 0;
  const command = (events: Event[]) => batch(save.revision, `party-http-${++serial}`, events);
  const submit = async (input: ReturnType<typeof batch>) => {
    const result = await f.request('/api/save-sync', identity.token, input);
    if (result.status === 200) save = result.body;
    return result;
  };
  assert.equal((await submit(command([event('hatch', 1), event('mode', 1)]))).status, 200);
  for (let i = 0; i < 64 && save.state.collection.length < minimum; ++i) {
    assert.equal((await submit(command([...Array.from({ length: save.state.recoveryRestCount }, () => event('rest')), event('walk', 100), event('auto')]))).status, 200);
  }
  assert.equal(save.state.collection.length, minimum);
  const get = () => f.request('/api/save', identity.token);
  return { ...f, identity, command, submit, get, current: () => save };
}

test('three owned non-partners assign durably, reject invalid changes atomically and reconcile release/promotion', async t => {
  const f = await roster(t), original = f.current();
  assert.deepEqual(original.state.partyMemberIds, []); assert.equal(original.state.partyCapacity, 3);
  const chosen = original.state.collection.filter((member: any) => member.id !== original.state.activeCreatureId).slice(0, 3).map((member: any) => member.id);
  for (const events of [[event('party-add', 0)], [event('party-add', 0xffffffff)], [event('party-add', original.state.activeCreatureId)], [event('party-add', 0xfffffffe)], [event('party-remove', chosen[0])]]) {
    assert.equal((await f.submit(f.command(events))).status, 422); assert.deepEqual((await f.get()).body.state, original.state);
  }
  const add = f.command(chosen.map((id: number) => event('party-add', id)));
  const added = await f.submit(add); assert.equal(added.status, 200); assert.deepEqual(added.body.state.partyMemberIds, chosen);
  assert.deepEqual(added.body.state.collection, original.state.collection, 'assignment grants no XP or care');
  assert.deepEqual(await f.request('/api/save-sync', f.identity.token, add), added);
  await f.restart(); assert.deepEqual((await f.get()).body.state, added.body.state);
  const unused = original.state.collection.find((member: any) => member.id !== original.state.activeCreatureId && !chosen.includes(member.id)).id;
  for (const events of [[event('party-add', unused)], [event('party-add', chosen[0])], [event('party-remove', chosen[0]), event('party-add', original.state.activeCreatureId)]]) {
    assert.equal((await f.submit(f.command(events))).status, 422); assert.deepEqual((await f.get()).body.state, added.body.state);
  }
  const promoted = await f.submit(f.command([event('select', chosen[0])])); assert.equal(promoted.status, 200);
  assert.equal(promoted.body.state.activeCreatureId, chosen[0]); assert.deepEqual(promoted.body.state.partyMemberIds, chosen.slice(1));
  const released = await f.submit(f.command([event('release', chosen[1])])); assert.equal(released.status, 200);
  assert.deepEqual(released.body.state.partyMemberIds, [chosen[2]]); assert.ok(!released.body.state.collection.some((member: any) => member.id === chosen[1]));
  assert.equal((await f.submit(f.command([event('party-remove', chosen[2])]))).status, 200);
  assert.deepEqual(f.current().state.partyMemberIds, []); await f.restart(); assert.deepEqual((await f.get()).body.state, f.current().state);
});

test('wild rewards pay each selected companion full native XP exactly once across concurrent retries and reboot', async t => {
  const f = await roster(t);
  const ids = f.current().state.collection.filter((member: any) => member.id !== f.current().state.activeCreatureId).slice(0, 3).map((member: any) => member.id);
  assert.equal((await f.submit(f.command(ids.map((id: number) => event('party-add', id))))).status, 200);
  let rewarded = false;
  for (let encounter = 0; encounter < 10 && !rewarded; ++encounter) {
    assert.equal((await f.submit(f.command([...Array.from({ length: f.current().state.recoveryRestCount }, () => event('rest')), event('walk', 100)]))).status, 200);
    const before = structuredClone(f.current().state), finish = f.command([event('auto')]);
    const forbidden = await f.submit(f.command([event('party-remove', ids[0])])); assert.equal(forbidden.status, 422);
    assert.deepEqual((await f.get()).body.state, before, 'party is fixed while wild battle is active');
    const responses = await Promise.all(Array.from({ length: 3 }, () => f.request('/api/save-sync', f.identity.token, finish)));
    for (const result of responses) { assert.equal(result.status, 200); assert.deepEqual(result, responses[0]); }
    const after = responses[0].body.state;
    rewarded = ['captured', 'won'].includes(after.lastAutoBattle.outcome);
    for (const member of before.collection) {
      const actual = after.collection.find((row: any) => row.id === member.id);
      const eligible = member.id === before.activeCreatureId || ids.includes(member.id);
      assert.equal(actual.xp, Math.min(49000, member.xp + (rewarded && eligible ? 20 + 6 * before.wildLevel : 0)), `XP for member ${member.id}`);
    }
    await f.restart(); assert.deepEqual((await f.get()).body.state, after);
    assert.deepEqual(await f.request('/api/save-sync', f.identity.token, finish), responses[0]);
    // Refresh the helper revision after concurrent direct requests.
    assert.equal((await f.submit(finish)).status, 200);
    const care = await f.submit(f.command([event('rest')])); assert.equal(care.status, 200);
    assert.deepEqual(await f.request('/api/save-sync', f.identity.token, finish), responses[0], 'later care never changes the original reward receipt');
  }
  assert.ok(rewarded, 'deterministic native fixture earns a wild terminal reward');
});

test('active practice blocks new party changes while old receipts remain exact; practice grants no XP', async t => {
  const f = await roster(t, 2), id = f.current().state.collection[1].id;
  const add = f.command([event('party-add', id)]), added = await f.submit(add); assert.equal(added.status, 200);
  const start = await f.request('/api/battle/start', f.identity.token, { rulesVersion: 7, expectedRevision: 0, requestId: 'party-practice-start' }); assert.equal(start.status, 200);
  for (const events of [[event('party-remove', id)], [event('party-add', id)], [event('feed'), event('party-remove', id)]]) {
    const rejected = await f.submit(f.command(events)); assert.equal(rejected.status, 409); assert.equal(rejected.body.error, 'partner_locked'); assert.deepEqual((await f.get()).body.state, added.body.state);
  }
  assert.deepEqual(await f.request('/api/save-sync', f.identity.token, add), added, 'prior acknowledged edit bypasses the new-practice guard');
  const retreat = await f.request('/api/battle/act', f.identity.token, { rulesVersion: 7, expectedRevision: start.body.revision, requestId: 'party-practice-retreat', action: event('retreat') }); assert.equal(retreat.status, 200);
  assert.deepEqual((await f.get()).body.state, added.body.state);
  const auto = await f.request('/api/battle/start', f.identity.token, { rulesVersion: 7, expectedRevision: retreat.body.revision, requestId: 'party-practice-auto', mode: 'auto' }); assert.equal(auto.status, 200);
  assert.deepEqual((await f.get()).body.state, added.body.state, 'practice Auto terminal outcome cannot award party XP');
});

for (const version of [13, 14]) test(`rules${version} migration leaves companions empty and reserves exact old receipts`, async t => {
  const events = [event('hatch', 1), event('mode', 1), event('walk', 100), event('auto')], previous = frozen(events, version);
  const pending = { rulesVersion: version, baseRevision: 0, batchId: `old-party-epoch-${version}`, events };
  const receipt = { batchId: pending.batchId, bodyHash: hash(JSON.stringify({ rulesVersion: version, baseRevision: 0, events })), revision: 1, eventEnd: events.length };
  const original = { formatVersion: version === 14 ? 16 : 15, gameSchemaVersion: version === 14 ? 21 : 20, rulesVersion: version, devices: [{
    deviceId: `dv_${'5'.repeat(24)}`, tokenHash: hash(oldToken), seed: 12345, initialMode: 'onboarding', revision: 1, legacy: null, events, receipts: [receipt],
  }] };
  const f = await fixture(t, original), save = await f.request('/api/save', oldToken); assert.equal(save.status, 200);
  assert.equal(save.body.state.maxLevel, 50);
  assert.deepEqual(historicComparable(save.body.state), historicComparable({ ...previous.state, schemaVersion: 24, rulesVersion: 17, collectionCapacity: 60, partyCapacity: 3, partyMemberIds: [] }));
  assert.deepEqual(save.body.autoTrace, previous.trace);
  assert.equal((await f.request('/api/save-sync', oldToken, pending)).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', oldToken, { ...pending, rulesVersion: 17 })).body.error, 'legacy_batch_requires_reconciliation');
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.gameSchemaVersion, stored.rulesVersion], [19, 24, 17]);
  assert.deepEqual(stored.devices[0].legacy.histories, [{ rulesVersion: version, events, receipts: [receipt] }]);
  assert.equal(Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64').length, 3216);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, `store.rules-v${version}.json`), 'utf8')), original);
  await f.restart(); assert.deepEqual(await f.request('/api/save', oldToken), save);
  await writeFile(join(f.dataDir, 'store.json'), '{interrupted'); await f.restart(); assert.deepEqual(await f.request('/api/save', oldToken), save);
});


test('rules14 snapshot baseline preserves inherited rules13 Auto trace and both receipt epochs', async t => {
  const fixtureData = JSON.parse(await readFile(join(rootDir, 'tests/fixtures/companion-migration-schema21.json'), 'utf8'));
  const snapshot = fixtureData.snapshotBase64;
  assert.equal(hash(Buffer.from(snapshot, 'base64')), fixtureData.snapshotSha256);
  const initial = [event('hatch', 1), event('mode', 1), event('walk', 100), event('auto')], suffix = [event('rest')];
  const previous = frozen(initial, 13);
  assert.ok(previous.trace);
  const expected = JSON.parse(execFileSync(corePath, ['--replay-v14-snapshot-trace', snapshot], { input: 'rest 0\n', encoding: 'utf8' }));
  const receipts = [initial, suffix].map((events, baseRevision) => ({ batchId: `party-chain-epoch-${13 + baseRevision}`,
    bodyHash: hash(JSON.stringify({ rulesVersion: 13 + baseRevision, baseRevision, events })), revision: baseRevision + 1, eventEnd: events.length }));
  const archived = { rulesVersion: 13, events: initial, receipts: [receipts[0]] };
  const original = { formatVersion: 16, gameSchemaVersion: 21, rulesVersion: 14, devices: [{
    deviceId: `dv_${'7'.repeat(24)}`, tokenHash: hash(oldToken), seed: 12345, initialMode: 'onboarding', revision: 2,
    legacy: { histories: [archived], snapshotBase64: snapshot, autoTrace: previous.trace }, events: suffix, receipts: [receipts[1]],
  }] };
  const f = await fixture(t, original), saved = await f.request('/api/save', oldToken); assert.equal(saved.status, 200);
  assert.equal(saved.body.state.maxLevel, 50);
  assert.deepEqual(historicComparable(saved.body.state), historicComparable({ ...expected.state, schemaVersion: 24, rulesVersion: 17, partyCapacity: 3, partyMemberIds: [] }));
  assert.deepEqual(saved.body.autoTrace, previous.trace, 'care suffix inherits exact earlier frames');
  const histories = [archived, { rulesVersion: 14, events: suffix, receipts: [receipts[1]] }];
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual(stored.devices[0].legacy.histories, histories);
  for (const [index, events] of [initial, suffix].entries()) {
    const pending = { rulesVersion: 13 + index, baseRevision: index, batchId: receipts[index].batchId, events };
    assert.equal((await f.request('/api/save-sync', oldToken, pending)).body.error, 'migration_required');
    assert.equal((await f.request('/api/save-sync', oldToken, { ...pending, rulesVersion: 17 })).body.error, 'legacy_batch_requires_reconciliation');
  }
  await f.restart(); assert.deepEqual(await f.request('/api/save', oldToken), saved);
  const current = await f.request('/api/save-sync', oldToken, batch(2, 'party-chain-current-care', [event('rest')])); assert.equal(current.status, 200);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8')).devices[0].legacy.histories, histories);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v14.json'), 'utf8')), original);
});

test('historical rules14 histories cannot conceal party events', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-party-old-rules-'));
  t.after(() => rm(dataDir, { recursive: true, force: true }));
  const events = [event('hatch', 1), event('party-add', 2)];
  const old = { formatVersion: 16, gameSchemaVersion: 21, rulesVersion: 14, devices: [{
    deviceId: `dv_${'8'.repeat(24)}`, tokenHash: hash(oldToken), seed: 12345, initialMode: 'onboarding', revision: 1, legacy: null, events,
    receipts: [{ batchId: 'party-forged-old-event', bodyHash: hash(JSON.stringify({ rulesVersion: 14, baseRevision: 0, events })), revision: 1, eventEnd: events.length }],
  }] };
  const originalText = JSON.stringify(old);
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), originalText);
  assert.throws(() => createApp({ rootDir, corePath, battleCorePath, dataDir }), /corrupt|Unsupported/);
  assert.equal(await readFile(join(dataDir, 'store.json'), 'utf8'), originalText);
  assert.equal(await readFile(join(dataDir, 'store.backup.json'), 'utf8'), originalText);
});

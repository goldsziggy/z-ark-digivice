import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { startServer } from '../service/server.ts';

type Event = { type: string; value: number };
const hash = (value: string) => createHash('sha256').update(value).digest('hex');
const species = ['impmon', 'agumon', 'gabumon', 'patamon', 'tentomon', 'palmon', 'gomamon', 'renamon'];
const hatch = (id: number, batchId = 'onboarding-hatch-001', baseRevision = 0) => ({ rulesVersion: 13, baseRevision, batchId, events: [{ type: 'hatch', value: id }] });
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, saved?: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-onboarding-'));
  if (saved) for (const file of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, file), JSON.stringify(saved));
  let app = await startServer({ corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (path: string, token?: string, body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, {
      method: body === undefined ? 'GET' : 'POST', headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    return { status: response.status, body: await response.json() as any };
  };
  const pair = async () => {
    const code = (await request('/api/pairing/start', undefined, {})).body.code;
    const result = await request('/api/pairing/claim', undefined, { code });
    assert.equal(result.status, 201);
    return result.body;
  };
  return { request, pair, dataDir, restart: async () => { await close(); app = await startServer({ corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 }); } };
}

test('new identities start as eggs; invalid and mixed hatch batches leave them unchanged', async t => {
  const f = await fixture(t), paired = await f.pair();
  assert.equal(paired.revision, 0); assert.equal(paired.state.schemaVersion, 17); assert.equal(paired.state.rulesVersion, 13);
  assert.equal(paired.state.phase, 'egg'); assert.equal(paired.state.sequence, 0);
  assert.deepEqual(paired.state.onboarding, { completed: false, starterId: null, offerSeed: 0, offers: [0, 0, 0] });
  assert.deepEqual(paired.state.collection, []); assert.equal(paired.state.activeCreatureId, 0);
  for (const key of ['creature', 'species', 'combat']) assert.equal(paired.state[key], null);
  const before = (await f.request('/api/save', paired.token)).body;
  assert.equal((await f.request('/api/health')).body.schemaVersion, 17);
  assert.equal((await f.request('/api/device/health')).body.gameSchemaVersion, 17);
  assert.equal((await f.request('/api/save-sync', undefined, hatch(1))).status, 401);
  for (const value of [0, 9, 1.5, -1]) assert.equal((await f.request('/api/save-sync', paired.token, hatch(value))).status, 422);
  for (const type of ['feed', 'walk', 'select']) {
    assert.equal((await f.request('/api/save-sync', paired.token, { rulesVersion: 13, baseRevision: 0, batchId: `unhatched-${type}`, events: [{ type, value: type === 'walk' || type === 'select' ? 1 : 0 }] })).status, 422);
  }
  const practice = await f.request('/api/battle/start', paired.token, { rulesVersion: 7, expectedRevision: 0, requestId: 'unhatched-practice' });
  assert.equal(practice.status, 409); assert.equal(practice.body.error, 'onboarding_required');
  const mixed = { ...hatch(1, 'atomic-hatch-failure'), events: [{ type: 'hatch', value: 1 }, { type: 'capture', value: 0 }] };
  assert.equal((await f.request('/api/save-sync', paired.token, mixed)).status, 422);
  assert.deepEqual((await f.request('/api/save', paired.token)).body, before);
  await f.restart();
  assert.deepEqual((await f.request('/api/save', paired.token)).body, before);
  const store = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(store.formatVersion, 15); assert.equal(store.devices[0].initialMode, 'onboarding');
});

test('all eight native starters hatch as member one; retries survive later care, restart and recovery', async t => {
  const f = await fixture(t), catalog = await f.request('/api/starters');
  assert.equal(catalog.status, 200); assert.equal(catalog.body.formatVersion, 1); assert.equal(catalog.body.rulesVersion, 13);
  assert.deepEqual(catalog.body.starters.map((entry: any) => entry.species), species);
  assert.deepEqual(catalog.body.starters.map((entry: any) => entry.id), [1, 2, 3, 4, 5, 6, 7, 8]);
  assert.ok(catalog.body.starters.every((entry: any) => entry.stage === 'Rookie' && entry.combat.maxHp > 0));
  let owner: any, original: any;
  for (let index = 0; index < 8; index++) {
    const paired = await f.pair(), body = hatch(index + 1);
    const accepted = await f.request('/api/save-sync', paired.token, body);
    assert.equal(accepted.status, 200); assert.equal(accepted.body.revision, 1); assert.equal(accepted.body.state.sequence, 1);
    assert.deepEqual(accepted.body.state.onboarding, { completed: true, starterId: index + 1, offerSeed: 0, offers: [0, 0, 0] });
    assert.equal(accepted.body.state.phase, 'home'); assert.equal(accepted.body.state.activeCreatureId, 1);
    assert.equal(accepted.body.state.collection.length, 1); assert.equal(accepted.body.state.collection[0].id, 1);
    assert.equal(accepted.body.state.species, species[index]);
    assert.deepEqual(accepted.body.state.combat, catalog.body.starters[index].combat);
    assert.deepEqual(await f.request('/api/save-sync', paired.token, body), accepted, 'lost response retries exactly');
    if (!index) { owner = paired; original = accepted; }
  }
  const feed = await f.request('/api/save-sync', owner.token, { rulesVersion: 13, baseRevision: 1, batchId: 'post-hatch-care-001', events: [{ type: 'feed', value: 0 }] });
  assert.equal(feed.status, 200); assert.equal(feed.body.revision, 2);
  assert.deepEqual(await f.request('/api/save-sync', owner.token, hatch(1)), original, 'old receipt must replay from the egg initializer');
  assert.equal((await f.request('/api/save-sync', owner.token, hatch(2))).body.error, 'batch_mismatch');
  assert.equal((await f.request('/api/save-sync', owner.token, hatch(2, 'stale-new-hatch', 0))).body.error, 'revision_conflict');
  assert.equal((await f.request('/api/save-sync', owner.token, hatch(2, 'second-hatch-now', 2))).status, 422);
  const practice = await f.request('/api/battle/start', owner.token, { rulesVersion: 7, expectedRevision: 0, requestId: 'impmon-practice-start' });
  assert.equal(practice.status, 200); assert.equal(practice.body.battle.companion.species, 'impmon');
  assert.deepEqual((await f.request('/api/save', owner.token)).body.state, feed.body.state, 'practice never changes the starter save');
  await f.restart();
  assert.deepEqual(await f.request('/api/save-sync', owner.token, hatch(1)), original);
  await writeFile(join(f.dataDir, 'store.json'), '{interrupted');
  await f.restart();
  assert.deepEqual(await f.request('/api/save-sync', owner.token, hatch(1)), original);
  assert.equal((await f.request('/api/save', owner.token)).body.revision, 2);
});

test('concurrent starter confirmations commit exactly one choice', async t => {
  const f = await fixture(t), paired = await f.pair();
  const replies = await Promise.all([f.request('/api/save-sync', paired.token, hatch(1, 'competing-hatch-a')), f.request('/api/save-sync', paired.token, hatch(8, 'competing-hatch-b'))]);
  assert.deepEqual(replies.map(reply => reply.status).sort(), [200, 409]);
  const current = (await f.request('/api/save', paired.token)).body;
  assert.equal(current.revision, 1); assert.equal(current.state.sequence, 1); assert.equal(current.state.collection.length, 1);
  assert.ok([1, 8].includes(current.state.onboarding.starterId));
});

// Captured from the exact pre-onboarding format-4 native migration output.
// Preserve these 404 archival bytes rather than regenerating them with new code.
const OLD_BASELINE = 'REdWUwQAiAEDAAAABgAAADkwAADqQY50ZAAAAAAAAABhAAAATAAAAFUAAABeAAAAEwAAAAEAAAABAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAKAAAAAQAAAAEAAAABAAAAAAAAAAEAAAABAAAAYQAAAEwAAABVAAAAXgAAABMAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAFnnKus=';
test('legacy zero-event pets, current receipts and archived baseline bytes survive metadata migration', async t => {
  const credentials = [1, 2, 3].map(value => ({ deviceId: `dv_${String(value).repeat(24)}`, token: Buffer.alloc(32, value).toString('base64url') }));
  const devices = credentials.map(({ deviceId, token }) => ({ deviceId, tokenHash: hash(token), seed: 12345, revision: 0, legacy: null as any, events: [] as Event[], receipts: [] as any[] }));
  const events = [{ type: 'feed', value: 0 }], oldPending = { rulesVersion: 3, baseRevision: 0, batchId: 'existing-current-receipt', events };
  devices[1].events = events; devices[1].revision = 1;
  devices[1].receipts = [{ batchId: oldPending.batchId, bodyHash: hash(JSON.stringify({ rulesVersion: 3, baseRevision: 0, events })), revision: 1, eventEnd: 1 }];
  const historical = ['feed', 'play', 'walk', 'card', 'attack', 'capture'].map(type => ({ type, value: type === 'walk' ? 100 : type === 'card' ? 1 : 0 }));
  devices[2].legacy = { histories: [{ rulesVersion: 1, events: historical, receipts: [{ batchId: 'archived-rules-one', bodyHash: hash(JSON.stringify({ baseRevision: 0, events: historical })), revision: 1, eventEnd: historical.length }] }], snapshotBase64: OLD_BASELINE };
  devices[2].revision = 1;
  const old = { formatVersion: 3, gameSchemaVersion: 4, rulesVersion: 3, devices };
  const f = await fixture(t, old);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(stored.formatVersion, 15); assert.equal(stored.gameSchemaVersion, 17);
  for (let i = 0; i < devices.length; i++) {
    const current = stored.devices[i], previous = devices[i];
    assert.equal(current.deviceId, previous.deviceId); assert.equal(current.tokenHash, previous.tokenHash);
    assert.equal(current.seed, previous.seed); assert.equal(current.revision, previous.revision); assert.equal(current.initialMode, 'legacy');
    assert.deepEqual(current.events, []); assert.deepEqual(current.receipts, []);
    const history = current.legacy.histories.find((entry: any) => entry.rulesVersion === 3);
    assert.deepEqual(history.events, previous.events); assert.deepEqual(history.receipts, previous.receipts);
    assert.equal(Buffer.from(current.legacy.snapshotBase64, 'base64').length, 652);
  }
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v3.json'), 'utf8')), old);
  for (const identity of credentials) {
    const save = (await f.request('/api/save', identity.token)).body;
    assert.equal(save.state.species, 'mote'); assert.deepEqual(save.state.onboarding, { completed: true, starterId: null, offerSeed: 0, offers: [0, 0, 0] });
    assert.equal(save.state.collection[0].id, 1);
  }
  const zero = (await f.request('/api/save', credentials[0].token)).body;
  assert.equal(zero.revision, 0); assert.equal(zero.state.sequence, 0);
  assert.equal((await f.request('/api/save-sync', credentials[0].token, hatch(1))).status, 422);
  const retry = await f.request('/api/save-sync', credentials[1].token, oldPending);
  assert.equal(retry.status, 409); assert.equal(retry.body.error, 'migration_required');
  assert.equal((await f.request('/api/save', credentials[1].token)).body.state.fullness, 85);
  const relabeled = await f.request('/api/save-sync', credentials[1].token, { ...oldPending, rulesVersion: 13 });
  assert.equal(relabeled.status, 409); assert.equal(relabeled.body.error, 'legacy_batch_requires_reconciliation');
  const archived = (await f.request('/api/save', credentials[2].token)).body;
  assert.equal(archived.baseSequence, 6); assert.equal(archived.state.legacyCaptures, 1); assert.equal(archived.state.hp, 97);
  await f.restart();
  assert.deepEqual(await f.request('/api/save-sync', credentials[1].token, oldPending), retry);
  assert.deepEqual((await f.request('/api/save', credentials[0].token)).body, zero);
  const unsupported = await f.request('/api/save-sync', credentials[0].token, { ...hatch(1), rulesVersion: 999 });
  assert.equal(unsupported.status, 409); assert.equal(unsupported.body.error, 'migration_required');
  assert.deepEqual((await f.request('/api/save', credentials[0].token)).body, zero);
});

test('unknown future store schema never falls back to an older backup', async t => {
  const f = await fixture(t); await f.pair();
  const file = join(f.dataDir, 'store.json'), original = JSON.parse(await readFile(file, 'utf8'));
  const future = { ...original, formatVersion: 999, gameSchemaVersion: 999 };
  await writeFile(file, JSON.stringify(future));
  await assert.rejects(f.restart(), /explicit migration/);
  assert.deepEqual(JSON.parse(await readFile(file, 'utf8')), future);
});

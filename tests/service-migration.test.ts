import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { startServer } from '../service/server.ts';
import { legacyFixture, namedFixture } from './legacy-fixture.ts';

type Event = { type: string; value: number };
const rootDir = resolve(fileURLToPath(new URL('..', import.meta.url)));
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const legacyToken = Buffer.alloc(32, 7).toString('base64url'); // Public test identity, temporary store only.
const legacyEvents: Event[] = [
  { type: 'feed', value: 0 }, { type: 'play', value: 0 }, { type: 'walk', value: 100 },
  { type: 'card', value: 1 }, { type: 'attack', value: 0 }, { type: 'capture', value: 0 },
];
function legacyStore(events = legacyEvents) {
  const bodyHash = createHash('sha256').update(JSON.stringify({ baseRevision: 0, events })).digest('hex');
  return { formatVersion: 1, gameSchemaVersion: 2, rulesVersion: 1, devices: [{
    deviceId: `dv_${'7'.repeat(24)}`, tokenHash: createHash('sha256').update(legacyToken).digest('hex'), seed: 12345,
    revision: 1, events, receipts: [{ batchId: 'committed-under-rules-one', bodyHash, revision: 1, eventEnd: events.length }],
  }] };
}
const OLD_V1_BASELINE = 'REdWUwMAiAECAAAABgAAADkwAADqQY50ZAAAAAAAAABhAAAATAAAAFUAAABeAAAAEwAAAAEAAAABAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAKAAAAAQAAAAEAAAABAAAAAAAAAAEAAAABAAAAYQAAAEwAAABVAAAAXgAAABMAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAOiifdw=';
function rulesTwoStore(withOldBaseline: boolean) {
  const old = legacyStore().devices[0];
  const events = withOldBaseline ? [{ type: 'feed', value: 0 }] : legacyEvents;
  const baseRevision = withOldBaseline ? 1 : 0;
  const receipt = { batchId: 'committed-under-rules-two', revision: baseRevision + 1, eventEnd: events.length,
    bodyHash: createHash('sha256').update(JSON.stringify({ rulesVersion: 2, baseRevision, events })).digest('hex') };
  return { formatVersion: 2, gameSchemaVersion: 3, rulesVersion: 2, devices: [{ ...old, revision: baseRevision + 1, events, receipts: [receipt],
    legacy: withOldBaseline ? { events: legacyEvents, receipts: old.receipts, snapshotBase64: OLD_V1_BASELINE } : null }] };
}
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, old?: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-migration-test-'));
  if (old) {
    await writeFile(join(dataDir, 'store.json'), JSON.stringify(old));
    await writeFile(join(dataDir, 'store.backup.json'), JSON.stringify(old));
  }
  let app = await startServer({ rootDir, dataDir, corePath, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  async function request(path: string, body?: unknown, token?: string) {
    const base = `http://127.0.0.1:${(app.server.address() as { port: number }).port}`;
    const response = await fetch(base + path, { method: body === undefined ? 'GET' : 'POST', headers: {
      ...(body === undefined ? {} : { 'Content-Type': 'application/json' }), ...(token ? { Authorization: `Bearer ${token}` } : {}),
    }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() };
  }
  return { request, dataDir, restart: async () => { await close(); app = await startServer({ rootDir, dataDir, corePath, port: 0 }); } };
}

test('rules-v1 history migrates through the frozen legacy core without inventing collection members', async (t) => {
  const original = legacyStore();
  const f = await fixture(t, original);
  const saved = await f.request('/api/save', undefined, legacyToken);
  assert.equal(saved.status, 200);
  assert.equal(saved.body.deviceId, original.devices[0].deviceId);
  assert.equal(saved.body.revision, 1);
  assert.equal(saved.body.baseSequence, 6);
  assert.deepEqual(saved.body.events, []);
  const state = saved.body.state;
  assert.equal(state.schemaVersion, 17); assert.equal(state.rulesVersion, 13);
  // These are frozen rules-v1 results captured before this migration was written.
  for (const [key, value] of Object.entries({ sequence: 6, rngState: 1955480042, steps: 100, hp: 97, energy: 76, fullness: 85, mood: 94, bond: 19, level: 1, captures: 1 })) assert.equal(state[key], value, key);
  assert.equal(state.legacyCaptures, 1);
  assert.equal(state.activeCreatureId, 1);
  assert.equal(state.collection.length, 1);
  assert.equal(state.collection[0].species, 'mote');
  assert.equal(state.collection[0].id, 1);
  assert.equal(state.wildSpecies, null);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v1.json'), 'utf8')), original);
  const migrated = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(migrated.formatVersion, 15);
  assert.equal(migrated.devices[0].tokenHash, original.devices[0].tokenHash);
  assert.deepEqual(migrated.devices[0].legacy.histories[0].events, legacyEvents);
  assert.deepEqual(migrated.devices[0].legacy.histories[0].receipts, original.devices[0].receipts);
  assert.equal(Buffer.from(migrated.devices[0].legacy.snapshotBase64, 'base64').length, 652);

  const oldPending = { baseRevision: 1, batchId: 'old-uncommitted-rest', events: [{ type: 'rest', value: 0 }] };
  for (const body of [oldPending, { ...oldPending, rulesVersion: 1 }, { ...oldPending, rulesVersion: 2 }]) {
    const rejected = await f.request('/api/save-sync', body, legacyToken);
    assert.equal(rejected.status, 409); assert.equal(rejected.body.error, 'migration_required');
  }
  const reserved = await f.request('/api/save-sync', { rulesVersion: 13, baseRevision: 0, batchId: original.devices[0].receipts[0].batchId, events: legacyEvents }, legacyToken);
  assert.equal(reserved.status, 409); assert.equal(reserved.body.error, 'legacy_batch_requires_reconciliation');
  assert.deepEqual((await f.request('/api/save', undefined, legacyToken)).body.state, state);

  const currentBatch = { rulesVersion: 13, baseRevision: 1, batchId: 'post-migration-care', events: [{ type: 'feed', value: 0 }] };
  const accepted = await f.request('/api/save-sync', currentBatch, legacyToken);
  assert.equal(accepted.status, 200); assert.equal(accepted.body.revision, 2);
  assert.equal(accepted.body.state.sequence, 7);
  assert.equal(accepted.body.state.collection.length, 1);
  assert.equal(accepted.body.state.legacyCaptures, 1);
  await f.restart();
  assert.deepEqual((await f.request('/api/save-sync', currentBatch, legacyToken)).body, accepted.body);
  // Mirrored recovery restores both the migration boundary and current receipts.
  await writeFile(join(f.dataDir, 'store.json'), '{corrupt');
  await f.restart();
  assert.deepEqual((await f.request('/api/save-sync', currentBatch, legacyToken)).body, accepted.body);
  assert.equal((await f.request('/api/save-sync', oldPending, legacyToken)).body.error, 'migration_required');
});

test('an existing rules-v1 encounter remains the original Flicker encounter after migration', async (t) => {
  const f = await fixture(t, legacyStore(legacyEvents.slice(0, 3)));
  const saved = await f.request('/api/save', undefined, legacyToken);
  assert.equal(saved.status, 200);
  assert.equal(saved.body.state.phase, 'encounter');
  assert.equal(saved.body.state.wildSpecies, 'flicker');
  assert.equal(saved.body.state.wildName, 'Flicker');
  assert.equal(saved.body.state.collection.length, 1);
  assert.equal(saved.body.state.legacyCaptures, 0);
});

test('rules-v2 collection migrates under frozen rules while new rules reject the old capture sequence', async (t) => {
  const old = rulesTwoStore(false), f = await fixture(t, old);
  const saved = await f.request('/api/save', undefined, legacyToken);
  assert.equal(saved.status, 200); assert.equal(saved.body.state.schemaVersion, 17); assert.equal(saved.body.state.rulesVersion, 13);
  assert.equal(saved.body.revision, 1); assert.equal(saved.body.baseSequence, 6); assert.deepEqual(saved.body.events, []);
  assert.equal(saved.body.state.collection.length, 2); assert.equal(saved.body.state.legacyCaptures, 0);
  assert.deepEqual(saved.body.state.collection.map((member: { species: string }) => member.species), ['mote', 'flicker']);
  assert.equal(saved.body.state.collection[1].hp, saved.body.state.collection[1].combat.maxHp);
  assert.equal(saved.body.state.rngState, 1955480042);
  const attemptedNewReplay = spawnSync(corePath, ['--replay', '12345'], { input: legacyEvents.map(event => `${event.type} ${event.value}`).join('\n') + '\n', encoding: 'utf8' });
  assert.notEqual(attemptedNewReplay.status, 0, 'old successful capture must not be reconstructed using new damage rules');
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v2.json'), 'utf8')), old);
  const oldPending = { rulesVersion: 2, baseRevision: 1, batchId: 'old-rules-two-pending', events: [{ type: 'rest', value: 0 }] };
  assert.equal((await f.request('/api/save-sync', oldPending, legacyToken)).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', { ...oldPending, rulesVersion: 13, batchId: old.devices[0].receipts[0].batchId }, legacyToken)).body.error, 'legacy_batch_requires_reconciliation');
  const current = { rulesVersion: 13, baseRevision: 1, batchId: 'stats-three-current-care', events: [{ type: 'rest', value: 0 }] };
  const accepted = await f.request('/api/save-sync', current, legacyToken); assert.equal(accepted.status, 200);
  await f.restart(); assert.deepEqual((await f.request('/api/save-sync', current, legacyToken)).body, accepted.body);
});

test('nested rules-v1 baseline plus rules-v2 events preserve both archived receipt sets and identity', async (t) => {
  const old = rulesTwoStore(true), f = await fixture(t, old);
  const saved = await f.request('/api/save', undefined, legacyToken);
  assert.equal(saved.body.deviceId, old.devices[0].deviceId); assert.equal(saved.body.revision, 2);
  assert.equal(saved.body.baseSequence, 7); assert.equal(saved.body.state.sequence, 7);
  assert.equal(saved.body.state.collection.length, 1); assert.equal(saved.body.state.legacyCaptures, 1);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(stored.formatVersion, 15);
  assert.deepEqual(stored.devices[0].legacy.histories.map((history: { rulesVersion: number }) => history.rulesVersion), [1, 2]);
  assert.equal(stored.devices[0].tokenHash, old.devices[0].tokenHash);
  for (const batchId of [old.devices[0].legacy!.receipts[0].batchId, old.devices[0].receipts[0].batchId]) {
    assert.equal((await f.request('/api/save-sync', { rulesVersion: 13, baseRevision: 2, batchId, events: [{ type: 'feed', value: 0 }] }, legacyToken)).body.error, 'legacy_batch_requires_reconciliation');
  }
  await f.restart(); assert.deepEqual((await f.request('/api/save', undefined, legacyToken)).body, saved.body);
});

test('collection captures and member selection survive HTTP retries; a full collection rejects capture without state changes', async (t) => {
  const historical = namedFixture();
  const f = await fixture(t, historical.store);
  const token = historical.identities[0].token;
  const identity = await f.request('/api/save', undefined, token);
  let state = identity.body.state;
  let revision = 1;
  let batchNumber = 0;
  async function submit(events: Event[]) {
    const body = { rulesVersion: 13, baseRevision: revision, batchId: `collection-http-${++batchNumber}`, events };
    const result = await f.request('/api/save-sync', body, token);
    if (result.status === 200) { state = result.body.state; revision = result.body.revision; }
    return { ...result, request: body };
  }
  assert.equal(state.collection.length, 1);
  assert.equal((await submit([{ type: 'select', value: 0 }])).status, 422);
  assert.equal((await submit([{ type: 'select', value: 9 }])).status, 422);
  assert.equal((await submit([{ type: 'select', value: 8 }])).status, 422);
  assert.equal(revision, 1);

  // Fill through real native Auto results. The collection contract does not
  // assume a particular rarity pool or that one manual weakening pattern works
  // equally well for every freshly caught form.
  for (let encounter = 0; encounter < 64 && state.collection.length < 8; encounter++) {
    assert.equal(state.phase, 'home');
    assert.equal((await submit([{ type: 'select', value: 1 }, { type: 'mode', value: 1 }])).status, 200);
    const recovery: Event[] = Array.from({ length: state.recoveryRestCount }, () => ({ type: 'rest', value: 0 }));
    assert.equal((await submit([...recovery, { type: 'walk', value: 100 }])).status, 200);
    const collectionBefore = structuredClone(state.collection);
    const encounterState = structuredClone(state);
    const badSelect = await submit([{ type: 'select', value: 1 }]);
    assert.equal(badSelect.status, 422);
    assert.deepEqual(state, encounterState);
    const resolved = await submit([{ type: 'auto', value: 0 }]);
    assert.equal(resolved.status, 200); assert.equal(state.phase, 'home');
    assert.deepEqual((await f.request('/api/save-sync', resolved.request, token)).body, resolved.body, 'retry must not duplicate a member or reward');
    if (state.collection.length > collectionBefore.length) {
      const captured = state.collection.at(-1);
      assert.equal(captured.species, encounterState.wildSpecies);
      assert.equal(captured.formId, encounterState.wildFormId);
      assert.ok(Number.isInteger(captured.capturedAtSequence));
    }
  }
  assert.equal(state.collection.length, 8, 'deterministic encounters should fill the bounded collection');
  assert.equal(state.captures, 7);
  assert.equal(state.legacyCaptures, 0);
  assert.equal(new Set(state.collection.map((member: { id: number }) => member.id)).size, 8);
  const finalMember = state.collection.at(-1).id;
  const selected = await submit([{ type: 'select', value: finalMember }, { type: 'mode', value: 0 }]);
  assert.equal(selected.status, 200); assert.equal(state.activeCreatureId, finalMember);
  await f.restart();
  assert.deepEqual((await f.request('/api/save', undefined, token)).body.state, state);
  assert.deepEqual((await f.request('/api/save-sync', selected.request, token)).body, selected.body);
  assert.equal((await submit([{ type: 'rest', value: 0 }, { type: 'walk', value: 100 }])).status, 200);
  // A full collection rejects capture immediately, before any battle resource is spent.
  assert.equal(state.phase, 'encounter');
  const fullState = structuredClone(state);
  const fullCapture = await submit([{ type: 'capture', value: 0 }]);
  assert.equal(fullCapture.status, 422);
  const saved = await f.request('/api/save', undefined, token);
  assert.equal(saved.body.revision, revision);
  assert.deepEqual(saved.body.state, fullState);
});

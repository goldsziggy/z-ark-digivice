import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createApp, startServer } from '../service/server.ts';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const token = Buffer.alloc(32, 63).toString('base64url'); // Synthetic test identity only.
type Event = { type: string; value: number };
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 16, baseRevision, batchId, events });
const oldStore = (events: Event[], seed = 12345) => ({ formatVersion: 15, gameSchemaVersion: 19, rulesVersion: 13, devices: [{
  deviceId: `dv_${'a'.repeat(24)}`, tokenHash: hash(token), seed, initialMode: 'onboarding', revision: events.length ? 1 : 0, legacy: null, events,
  receipts: events.length ? [{ batchId: 'historical-world-before', revision: 1, eventEnd: events.length, bodyHash: hash(JSON.stringify({ rulesVersion: 13, baseRevision: 0, events })) }] : [],
}] });

async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, seedSource?: () => number, saved?: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-world-seed-'));
  if (saved) for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(saved));
  const options = { rootDir, corePath, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0, seedSource };
  let app = await startServer(options);
  const close = async () => { await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve())); app.close(); };
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (path: string, bearer?: string, body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, {
      method: body === undefined ? 'GET' : 'POST', headers: { ...(bearer ? { authorization: `Bearer ${bearer}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    return { status: response.status, body: await response.json() as any };
  };
  const pair = async () => {
    const started = await request('/api/pairing/start', undefined, {}); assert.equal(started.status, 201);
    return request('/api/pairing/claim', undefined, { code: started.body.code });
  };
  return { dataDir, request, pair, restart: async () => { await close(); app = await startServer(options); } };
}

test('default enrollment draws independent crypto seeds and restart never reinitializes identities', async t => {
  const f = await fixture(t), profiles: any[] = [];
  for (let i = 0; i < 8; ++i) {
    const paired = await f.pair(); assert.equal(paired.status, 201);
    assert.ok(Number.isInteger(paired.body.seed) && paired.body.seed > 0 && paired.body.seed <= 0xffffffff);
    assert.equal(paired.body.state.worldSeed, 0);
    profiles.push(paired.body);
  }
  // A uniform 32-bit source can collide; eight equal defaults must never recur.
  assert.ok(new Set(profiles.map(profile => profile.seed)).size > 1);
  await f.restart();
  for (const profile of profiles) {
    const saved = await f.request('/api/save', profile.token); assert.equal(saved.status, 200);
    assert.equal(saved.body.seed, profile.seed); assert.deepEqual(saved.body.state, profile.state);
  }
});

test('world setup is trusted, concurrent-idempotent, durable, and preserves old batch responses', async t => {
  let calls = 0;
  const values = [0x87654321, 0xfedcba98];
  const f = await fixture(t, () => { ++calls; assert.ok(values.length); return values.shift()!; });
  const paired = (await f.pair()).body; assert.equal(paired.seed, 0x87654321); assert.equal(calls, 1);
  assert.equal((await f.request('/api/world/seed', undefined, {})).status, 401);
  assert.equal((await f.request('/api/world/seed', paired.token, {})).body.error, 'onboarding_required');
  assert.equal(calls, 1, 'egg and failed authentication consume no entropy');
  const hatch = batch(0, 'world-hatch-receipt', [{ type: 'hatch', value: 1 }]);
  const hatched = await f.request('/api/save-sync', paired.token, hatch); assert.equal(hatched.status, 200);
  for (const body of [{ seed: 3 }, { value: 4 }, []]) assert.equal((await f.request('/api/world/seed', paired.token, body)).status, 400);
  const forged = await f.request('/api/save-sync', paired.token, batch(1, 'forged-world-seed', [{ type: 'world-seed', value: 3 }]));
  assert.equal(forged.status, 422); assert.equal(forged.body.error, 'server_owned_event');
  const results = await Promise.all(Array.from({ length: 4 }, () => f.request('/api/world/seed', paired.token, {})));
  for (const result of results) assert.deepEqual(result, results[0]);
  const saved = results[0]; assert.equal(saved.status, 200); assert.equal(calls, 2);
  assert.equal(saved.body.revision, 2); assert.equal(saved.body.state.sequence, 2); assert.equal(saved.body.state.worldSeed, 0xfedcba98);
  assert.equal(saved.body.state.foregroundSequence, hatched.body.state.foregroundSequence);
  assert.equal(saved.body.events.filter((event: Event) => event.type === 'world-seed').length, 1);
  assert.deepEqual(await f.request('/api/save-sync', paired.token, hatch), hatched, 'old outbox receipt survives setup');
  const stale = await f.request('/api/save-sync', paired.token, batch(1, 'world-stale-outbox', [{ type: 'feed', value: 0 }]));
  assert.equal(stale.body.error, 'revision_conflict', 'never relabel an uncommitted old-revision batch');
  await f.restart(); assert.equal(calls, 2);
  assert.deepEqual(await f.request('/api/world/seed', paired.token, {}), saved);
  assert.deepEqual(await f.request('/api/save-sync', paired.token, hatch), hatched);
  await writeFile(join(f.dataDir, 'store.json'), '{interrupted');
  await f.restart();
  assert.deepEqual(await f.request('/api/world/seed', paired.token, {}), saved);
  assert.equal(calls, 2, 'backup recovery preserves the committed seed and receipt');
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(stored.gameSchemaVersion, 23); assert.equal(stored.devices[0].receipts.length, 2);
});

for (const kind of ['pending', 'capture'] as const) test(`schema19 ${kind} archives exact history and seeds only future encounters`, async t => {
  const events: Event[] = [{ type: 'hatch', value: 1 }, { type: 'encounter-seed', value: 54321 }];
  if (kind === 'pending') events.push({ type: 'accrue-steps', value: 1000 });
  else events.push({ type: 'mode', value: 1 }, { type: 'walk', value: 100 }, { type: 'auto-fight', value: 0 });
  const original = oldStore(events), f = await fixture(t, () => 0x1234abcd, original);
  const before = await f.request('/api/save', token); assert.equal(before.status, 200); assert.equal(before.body.state.worldSeed, 0);
  if (kind === 'pending') assert.ok(before.body.state.walking.pendingEncounter);
  else assert.equal(before.body.state.autoCapture, 1);
  const migrated = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([migrated.formatVersion, migrated.gameSchemaVersion, migrated.rulesVersion], [18, 23, 16]);
  assert.deepEqual(migrated.devices[0].legacy.histories, [{ rulesVersion: 13, events, receipts: original.devices[0].receipts }]);
  assert.deepEqual(migrated.devices[0].events, []); assert.deepEqual(migrated.devices[0].receipts, []);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v13.json'), 'utf8')), original);
  const seeded = await f.request('/api/world/seed', token, {}); assert.equal(seeded.status, 200);
  assert.deepEqual(seeded.body.state, { ...before.body.state, worldSeed: 0x1234abcd, sequence: before.body.state.sequence + 1 });
  assert.deepEqual(seeded.body.autoTrace, before.body.autoTrace);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual(stored.devices[0].legacy.histories[0].events, events);
  assert.deepEqual(stored.devices[0].legacy.histories[0].receipts[0], original.devices[0].receipts[0]);
  assert.equal(stored.devices[0].seed, original.devices[0].seed);
  await f.restart(); assert.deepEqual(await f.request('/api/world/seed', token, {}), seeded);
});

test('injected seed sequences reproduce encounter rosters across restart while distinct worlds diverge', async t => {
  const run = async (worldSeed: number, restartMidway = true) => {
    const f = await fixture(t, () => worldSeed, oldStore([{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }]));
    let saved = (await f.request('/api/world/seed', token, {})).body;
    const forms: number[] = [];
    for (let i = 0; i < 8; ++i) {
      const prefix: Event[] = Array.from({ length: 4 }, () => ({ type: 'rest', value: 0 }));
      prefix.push({ type: 'walk', value: 100 });
      const encounter = await f.request('/api/save-sync', token, batch(saved.revision, `world-roster-${i}`, prefix));
      assert.equal(encounter.status, 200); forms.push(encounter.body.state.wildFormId);
      const fight = await f.request('/api/save-sync', token, batch(encounter.body.revision, `world-fight-${i}`, [{ type: 'auto-fight', value: 0 }]));
      assert.equal(fight.status, 200); saved = fight.body;
      for (let guard = 0; saved.state.phase !== 'home' && guard < 12; guard++) {
        if (saved.state.autoCapture) {
          const end = await f.request('/api/save-sync', token, batch(saved.revision, `world-miss-${i}-${guard}`, [{ type: 'flick', value: 0 }]));
          assert.equal(end.status, 200); saved = end.body;
        } else {
          const resume = await f.request('/api/save-sync', token, batch(saved.revision, `world-resume-${i}-${guard}`, [{ type: 'auto-fight', value: 0 }]));
          assert.equal(resume.status, 200); saved = resume.body;
        }
      }
      assert.equal(saved.state.phase, 'home');
      if (i === 3 && restartMidway) { await f.restart(); assert.deepEqual((await f.request('/api/save', token)).body.state, saved.state); }
    }
    return forms;
  };
  const first = await run(0x11111111), other = await run(0x98765432), repeat = await run(0x11111111, false);
  assert.notDeepEqual(first, other); assert.deepEqual(first, repeat);
});

test('invalid seed sources and future or mislabeled storage fail closed without acknowledging an identity', async t => {
  for (const value of [0, -1, 0x100000000, 1.5, NaN]) {
    const f = await fixture(t, () => value);
    assert.equal((await f.pair()).status, 500);
    await assert.rejects(readFile(join(f.dataDir, 'store.json')), { code: 'ENOENT' });
  }
  for (const schema of [19, 21]) {
    const dataDir = await mkdtemp(join(tmpdir(), 'digivice-world-invalid-store-'));
    t.after(() => rm(dataDir, { recursive: true, force: true }));
    const saved = oldStore([{ type: 'hatch', value: 1 }, { type: 'world-seed', value: 99 }]);
    saved.gameSchemaVersion = schema;
    const original = JSON.stringify(saved);
    await writeFile(join(dataDir, 'store.json'), original);
    assert.throws(() => createApp({ rootDir, corePath, dataDir }), schema === 21 ? /explicit migration/ : /cannot be read/);
    assert.equal(await readFile(join(dataDir, 'store.json'), 'utf8'), original);
  }
});

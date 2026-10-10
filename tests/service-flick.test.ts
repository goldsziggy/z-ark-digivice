import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createApp, startServer } from '../service/server.ts';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
const hit = { type: 'flick', value: 41140 }; // dx 0, reach 180: target centre.
const miss = { type: 'flick', value: 0 }; // dx -160, reach 0.
const prepare = [{ type: 'hatch', value: 1 }, { type: 'walk', value: 100 },
  { type: 'magic', value: 0 }, { type: 'attack', value: 0 }, { type: 'magic', value: 0 }, { type: 'attack', value: 0 }];
const batch = (baseRevision: number, batchId: string, events: unknown[]) => ({ rulesVersion: 16, baseRevision, batchId, events });
const hash = (text: string) => createHash('sha256').update(text).digest('hex');

async function fixture(t: { after: (fn: () => Promise<void>) => unknown }) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-flick-http-'));
  const options = { seedSource: () => 12345, rootDir, dataDir, corePath, battleCorePath, port: 0 };
  let app = await startServer(options);
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (path: string, token?: string, body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, {
      method: body === undefined ? 'GET' : 'POST', headers: {
        ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }),
      }, body: body === undefined ? undefined : JSON.stringify(body),
    });
    return { status: response.status, body: await response.json() as any };
  };
  const pair = async () => {
    const start = await request('/api/pairing/start', undefined, {}); assert.equal(start.status, 201);
    const claim = await request('/api/pairing/claim', undefined, { code: start.body.code }); assert.equal(claim.status, 201);
    return claim.body;
  };
  return { dataDir, request, pair, restart: async () => { await close(); app = await startServer(options); } };
}

test('flick requires an exact bounded payload and legal Tactical capture context', async t => {
  const f = await fixture(t), identity = await f.pair();
  assert.deepEqual((await f.request('/api/health')).body.capabilities, { captureFlick: 1, captureTimingRing: 1, captureTimingQuality: 1, worldSeed: 1, deferredEncounters: 1, manualAutoCapture: 1 });
  assert.equal((await f.request('/api/device/health')).body.capabilities, undefined);
  const initial = await f.request('/api/save', identity.token);
  let serial = 0;
  for (const event of [{ type: 'flick' }, { ...hit, hit: true }, ...[-1, 82176, 0.5, Number.MAX_SAFE_INTEGER, '41140', null].map(value => ({ type: 'flick', value }))]) {
    const rejected = await f.request('/api/save-sync', identity.token, batch(0, `flick-invalid-payload-${++serial}`, [event]));
    assert.equal(rejected.status, 422); assert.equal(rejected.body.error, 'invalid_events');
  }
  assert.deepEqual(await f.request('/api/save', identity.token), initial);
  for (const value of [0, 82175, hit.value]) {
    const rejected = await f.request('/api/save-sync', identity.token, batch(0, `flick-egg-context-${++serial}`, [{ type: 'flick', value }]));
    assert.equal(rejected.status, 422); assert.equal(rejected.body.error, 'invalid_transition', 'valid bounds reach native context validation');
  }
  assert.equal((await f.request('/api/save-sync', identity.token, { ...batch(0, 'flick-old-rule-marker', [hit]), rulesVersion: 9 })).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', identity.token, batch(0, 'flick-context-hatch', [{ type: 'hatch', value: 1 }]))).status, 200);
  const home = await f.request('/api/save', identity.token);
  assert.equal((await f.request('/api/save-sync', identity.token, batch(1, 'flick-home-context', [hit]))).body.error, 'invalid_transition');
  assert.deepEqual(await f.request('/api/save', identity.token), home);
  assert.equal((await f.request('/api/save-sync', identity.token, batch(1, 'flick-context-walk', [{ type: 'walk', value: 100 }]))).status, 200);
  const healthy = await f.request('/api/save', identity.token);
  assert.equal((await f.request('/api/save-sync', identity.token, batch(2, 'flick-healthy-target', [hit]))).body.error, 'invalid_transition');
  assert.deepEqual(await f.request('/api/save', identity.token), healthy);

  const autoIdentity = await f.pair();
  assert.equal((await f.request('/api/save-sync', autoIdentity.token, batch(0, 'flick-auto-prepare', [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }, { type: 'walk', value: 100 }]))).status, 200);
  const auto = await f.request('/api/save', autoIdentity.token);
  assert.equal((await f.request('/api/save-sync', autoIdentity.token, batch(1, 'flick-auto-context', [hit]))).body.error, 'invalid_transition');
  assert.deepEqual(await f.request('/api/save', autoIdentity.token), auto);
});

test('aim misses debit exactly one attempt, roll back mixed batches and retain exact receipts across restart', async t => {
  const f = await fixture(t), identity = await f.pair();
  const prepared = await f.request('/api/save-sync', identity.token, batch(0, 'flick-miss-prepare', prepare));
  assert.equal(prepared.status, 200); assert.ok(prepared.body.state.wildCaptureChance > 0);
  const before = await f.request('/api/save', identity.token);
  const mixed = await f.request('/api/save-sync', identity.token, batch(1, 'flick-miss-invalid-batch', [miss, { type: 'hatch', value: 2 }]));
  assert.equal(mixed.status, 422); assert.equal(mixed.body.error, 'invalid_transition');
  assert.deepEqual(await f.request('/api/save', identity.token), before);
  const request = batch(1, 'flick-miss-exact-receipt', [miss]);
  const accepted = await f.request('/api/save-sync', identity.token, request);
  assert.equal(accepted.status, 200);
  assert.equal(accepted.body.state.sequence, before.body.state.sequence + 1);
  assert.equal(accepted.body.state.captureAttempts, 1);
  assert.equal(accepted.body.state.wildTurn, before.body.state.wildTurn);
  assert.equal(accepted.body.state.hp, before.body.state.hp, 'rules12+ aim misses cost one attempt without retaliation');
  for (const key of ['rngState', 'captures', 'wildHp', 'nextMemberId', 'xp']) assert.equal(accepted.body.state[key], before.body.state[key], key);
  assert.equal(accepted.body.state.collection.length, 1);
  assert.deepEqual(await f.request('/api/save-sync', identity.token, request), accepted);
  await f.restart();
  assert.deepEqual(await f.request('/api/save-sync', identity.token, request), accepted);
  assert.equal((await f.request('/api/save-sync', identity.token, { ...request, events: [hit] })).body.error, 'batch_mismatch');
  assert.equal((await f.request('/api/save-sync', identity.token, batch(1, 'flick-miss-stale-revision', [miss]))).status, 409);
  let opened = accepted.body;
  for (let attempt = 2; attempt <= 3; attempt++) {
    const resumed = await f.request('/api/save-sync', identity.token, batch(opened.revision, `flick-miss-resume-${attempt}`, [{ type: 'attack', value: 0 }]));
    assert.equal(resumed.status, 200); assert.equal(resumed.body.state.phase, 'encounter'); assert.equal(resumed.body.state.captureDeferred, 0); assert.ok(resumed.body.state.wildCaptureChance > 0);
    const next = await f.request('/api/save-sync', identity.token, batch(resumed.body.revision, `flick-miss-attempt-${attempt}`, [{ ...miss, value: 82175 }]));
    assert.equal(next.status, 200); assert.equal(next.body.state.captureAttempts, attempt);
    assert.equal(next.body.state.lastCapture.attempt, attempt); assert.equal(next.body.state.phase, 'encounter');
    assert.equal(next.body.state.hp, resumed.body.state.hp); assert.equal(next.body.state.rngState, resumed.body.state.rngState);
    opened = next.body;
  }
  const exhausted = await f.request('/api/save', identity.token);
  assert.equal(exhausted.body.state.phase, 'encounter'); assert.equal(exhausted.body.state.captureAttempts, 3);
  assert.equal((await f.request('/api/save-sync', identity.token, batch(exhausted.body.revision, 'flick-miss-fourth-rejected', [miss]))).body.error, 'invalid_transition');
  assert.deepEqual(await f.request('/api/save', identity.token), exhausted);
  await f.restart();
  assert.deepEqual(await f.request('/api/save-sync', identity.token, request), accepted, 'old receipt returns its original state after later throws');
});

test('a target hit has the exact existing capture outcome and cannot duplicate rewards', async t => {
  const f = await fixture(t), gesture = await f.pair(), button = await f.pair();
  for (const identity of [gesture, button]) assert.equal((await f.request('/api/save-sync', identity.token, batch(0, 'capture-equivalence-prepare', prepare))).status, 200);
  const request = batch(1, 'flick-hit-exact-receipt', [hit]);
  const captured = await f.request('/api/save-sync', gesture.token, request);
  const ordinary = await f.request('/api/save-sync', button.token, batch(1, 'button-capture-receipt', [{ type: 'capture', value: 0 }]));
  assert.equal(captured.status, 200); assert.equal(ordinary.status, 200);
  assert.deepEqual(captured.body.state, ordinary.body.state);
  assert.equal(captured.body.state.phase, 'home'); assert.equal(captured.body.state.captures, 1);
  assert.equal(captured.body.state.collection.length, 2); assert.ok(captured.body.state.xp > 0);
  await f.restart();
  assert.deepEqual(await f.request('/api/save-sync', gesture.token, request), captured);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  const device = stored.devices.find((entry: any) => entry.deviceId === gesture.deviceId);
  assert.equal(device.events.filter((event: any) => event.type === 'flick').length, 1);
  assert.equal((await f.request('/api/save-sync', gesture.token, batch(2, 'flick-after-capture-home', [hit]))).body.error, 'invalid_transition');
  assert.deepEqual((await f.request('/api/save', gesture.token)).body.state, captured.body.state);
});

test('frozen rules histories reject flick instead of relabelling it as an old action', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'digivice-flick-legacy-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  for (let rulesVersion = 1; rulesVersion <= 9; rulesVersion++) {
    const formatVersion = rulesVersion < 3 ? rulesVersion : rulesVersion + 2;
    const gameSchemaVersion = rulesVersion < 3 ? rulesVersion + 1 : rulesVersion + 3;
    const events = [hit], dataDir = join(directory, `rules-${rulesVersion}`);
    const body = { baseRevision: 0, events };
    const device = { deviceId: `dv_${'a'.repeat(24)}`, tokenHash: '0'.repeat(64), seed: 12345, revision: 1, events,
      receipts: [{ batchId: 'unsupported-historical-flick', bodyHash: hash(JSON.stringify(rulesVersion === 1 ? body : { rulesVersion, ...body })), revision: 1, eventEnd: 1 }],
      ...(formatVersion !== 1 ? { legacy: null } : {}), ...(formatVersion >= 4 ? { initialMode: 'legacy' } : {}) };
    await mkdir(dataDir);
    for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify({ formatVersion, gameSchemaVersion, rulesVersion, devices: [device] }));
    assert.throws(() => createApp({ rootDir, dataDir, corePath, battleCorePath }), /Unsupported or corrupt event history/, `rules ${rulesVersion}`);
  }
});

test('a stale native executable cannot advertise flick capability', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'digivice-flick-stale-core-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const staleCore = join(directory, 'old-core.sh');
  await writeFile(staleCore, '#!/bin/sh\nprintf \'{"inputVersion":0,"landingX":206,"landingY":120,"hit":true}\\n\'\n', { mode: 0o700 });
  assert.throws(() => createApp({ rootDir, dataDir: join(directory, '.data'), corePath: staleCore }), /does not support capture flick input version 1/);
});

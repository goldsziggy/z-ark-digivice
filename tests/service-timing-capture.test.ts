import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { request as httpRequest } from 'node:http';
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createApp, startServer } from '../service/server.ts';
import { captureReport, validLastCapture } from '../web/care-capture-state.js';
import { sampleCaptureRing, captureRingChance } from '../web/capture-ring.js';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
const prepare = [{ type: 'hatch', value: 1 }, { type: 'walk', value: 100 },
  { type: 'magic', value: 0 }, { type: 'attack', value: 0 }, { type: 'magic', value: 0 }];
const batch = (baseRevision: number, batchId: string, events: unknown[]) => ({ rulesVersion: 19, baseRevision, batchId, events });
const ring = (value: unknown) => ({ type: 'ring-capture', value });
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
const nextRoll = (input: number) => { let value = input; value ^= value << 13; value ^= value >>> 17; value ^= value << 5; return value >>> 0; };

async function fixture(t: { after: (fn: () => Promise<void>) => unknown }) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-ring-quality-http-'));
  const options = { seedSource: () => 12345, rootDir, dataDir, corePath, battleCorePath, port: 0 };
  let app = await startServer(options);
  const close = async () => {
    await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
    app.close();
  };
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const url = (path: string) => `http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`;
  const request = async (path: string, token?: string, body?: unknown) => {
    const response = await fetch(url(path), {
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
  const dropAcknowledgement = (token: string, body: unknown) => new Promise<void>((resolve, reject) => {
    const req = httpRequest(url('/api/save-sync'), { method: 'POST', headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' } }, response => {
      // Headers arrive only after persistence; discard the body as a lost ACK.
      assert.equal(response.statusCode, 200); response.destroy(); req.destroy(); resolve();
    });
    req.once('error', reject); req.end(JSON.stringify(body));
  });
  const ready = async () => {
    const identity = await pair();
    const prepared = await request('/api/save-sync', identity.token, batch(0, 'ring-quality-prepare', prepare));
    assert.equal(prepared.status, 200); assert.equal(prepared.body.state.wildFormId, 102);
    // Seed 12345 is one level above the partner, so the eligible chance is 55.
    assert.equal(prepared.body.state.wildLevel, 2);
    assert.equal(prepared.body.state.wildCaptureChance, 55);
    return { ...identity, prepared };
  };
  return { dataDir, request, pair, ready, dropAcknowledgement, restart: async () => { await close(); app = await startServer(options); } };
}

function checkRoll(before: any, after: any, chance: number) {
  const rng = nextRoll(before.rngState);
  assert.equal(after.rngState, rng, 'one timing attempt consumes exactly one existing capture roll');
  assert.equal(after.lastCapture.chance, chance);
  assert.equal(after.lastCapture.result, rng % 100 < chance ? 'captured' : 'escaped');
  assert.ok(validLastCapture(after.lastCapture, after.foregroundSequence));
  assert.equal(captureReport(after.lastCapture).odds, `${chance}% throw chance`);
  assert.equal(after.hp, before.hp, 'throw does not retaliate');
}

test('timing grades apply once to eligible mon odds and the displayed chance matches the actual RNG threshold', async t => {
  const f = await fixture(t); const states = new Map<string, any>();
  for (const [grade, phase, factor] of [['red', 0, 10], ['orange', 480, 50], ['green', 960, 100]] as const) {
    const identity = await f.ready(), before = identity.prepared.body.state;
    const shown = sampleCaptureRing(phase, before.wildFormId);
    assert.equal(shown.grade, grade);
    const chance = Math.max(1, Math.floor(before.wildCaptureChance * factor / 100));
    assert.equal(captureRingChance(before.wildCaptureChance, shown.grade), chance);
    const result = await f.request('/api/save-sync', identity.token, batch(1, `timing-${grade}-throw`, [ring(phase)]));
    assert.equal(result.status, 200); checkRoll(before, result.body.state, chance);
    assert.equal(result.body.state.lastCapture.attempt, 1);
    assert.ok(chance > 0 && chance <= before.wildCaptureChance);
    assert.equal(result.body.autoTrace, null);
    states.set(grade, result.body.state);
  }
  assert.equal(states.get('red').lastCapture.chance, 5);
  assert.equal(states.get('orange').lastCapture.chance, 27);
  assert.equal(states.get('orange').lastCapture.result, 'escaped', 'the capture roll remains above the orange threshold');
  assert.equal(states.get('green').lastCapture.chance, 55, 'green retains the full eligible chance, not guaranteed success');
  const legacy = await f.ready();
  const oldHit = await f.request('/api/save-sync', legacy.token, batch(1, 'existing-flick-hit', [{ type: 'flick', value: 41140 }]));
  assert.equal(oldHit.status, 200); assert.deepEqual(oldHit.body.state, states.get('green'), 'green matches the previous eligible aimed capture exactly');
});

test('inclusive quality boundaries and adjacent phases preserve matching browser and authoritative odds', async t => {
  const f = await fixture(t);
  // Kumamon form102 targets radius68. Green is +/-12, orange extends to +/-24.
  for (const [phase, grade, chance] of [[239, 'red', 5], [240, 'orange', 27], [599, 'orange', 27], [600, 'green', 55],
    [1320, 'green', 55], [1321, 'orange', 27], [1680, 'orange', 27], [1681, 'red', 5]] as const) {
    const identity = await f.ready(), before = identity.prepared.body.state;
    const shown = sampleCaptureRing(phase, before.wildFormId); assert.equal(shown.grade, grade);
    assert.equal(captureRingChance(before.wildCaptureChance, grade), chance);
    const result = await f.request('/api/save-sync', identity.token, batch(1, `timing-boundary-${phase}`, [ring(phase)]));
    assert.equal(result.status, 200); checkRoll(before, result.body.state, chance);
  }
});

test('invalid phases, fabricated quality and illegal contexts consume no attempts, RNG or receipts', async t => {
  const f = await fixture(t), identity = await f.pair();
  const initial = await f.request('/api/save', identity.token); let serial = 0;
  for (const event of [{ type: 'ring-capture' }, { ...ring(960), grade: 'green' }, { ...ring(960), chance: 90 },
    ...[-1, 2400, 960.5, Number.MAX_SAFE_INTEGER, '960', null].map(ring)]) {
    const rejected = await f.request('/api/save-sync', identity.token, batch(0, `ring-invalid-${++serial}`, [event]));
    assert.equal(rejected.status, 422); assert.equal(rejected.body.error, 'invalid_events');
  }
  for (const phase of [0, 2399]) {
    const rejected = await f.request('/api/save-sync', identity.token, batch(0, `ring-egg-${phase}`, [ring(phase)]));
    assert.equal(rejected.body.error, 'invalid_transition');
  }
  assert.deepEqual(await f.request('/api/save', identity.token), initial);
  assert.equal((await f.request('/api/save-sync', identity.token, { ...batch(0, 'ring-old-rules-marker', [ring(0)]), rulesVersion: 12 })).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', identity.token, batch(0, 'ring-context-hatch', [{ type: 'hatch', value: 1 }]))).status, 200);
  const home = await f.request('/api/save', identity.token);
  assert.equal((await f.request('/api/save-sync', identity.token, batch(1, 'ring-home-reject', [ring(960)]))).body.error, 'invalid_transition');
  assert.deepEqual(await f.request('/api/save', identity.token), home);
  assert.equal((await f.request('/api/save-sync', identity.token, batch(1, 'ring-healthy-target', [{ type: 'walk', value: 100 }]))).status, 200);
  const healthy = await f.request('/api/save', identity.token);
  assert.equal(healthy.body.state.wildCaptureChance, 0);
  assert.equal((await f.request('/api/save-sync', identity.token, batch(2, 'ring-ineligible-reject', [ring(960)]))).body.error, 'invalid_transition');
  assert.deepEqual(await f.request('/api/save', identity.token), healthy);
  const ready = await f.ready(), before = await f.request('/api/save', ready.token);
  const diskBefore = await readFile(join(f.dataDir, 'store.json'), 'utf8');
  assert.equal((await f.request('/api/save-sync', ready.token, batch(1, 'ring-mixed-invalid', [ring(0), { type: 'hatch', value: 2 }]))).body.error, 'invalid_transition');
  assert.deepEqual(await f.request('/api/save', ready.token), before);
  assert.equal(await readFile(join(f.dataDir, 'store.json'), 'utf8'), diskBefore, 'failed mixed batch commits no partial attempt or receipt');
});

test('lost ACK, simultaneous duplicate posts and service restart never spend another throw or reroll', async t => {
  const f = await fixture(t), identity = await f.ready(); const first = batch(1, 'ring-lost-ack-one', [ring(0)]);
  await f.dropAcknowledgement(identity.token, first);
  const committed = await f.request('/api/save', identity.token);
  assert.equal(committed.body.state.captureAttempts, 1); checkRoll(identity.prepared.body.state, committed.body.state, 5);
  const receipt = await f.request('/api/save-sync', identity.token, first);
  assert.deepEqual(receipt.body.state, committed.body.state);
  const duplicates = await Promise.all(Array.from({ length: 4 }, () => f.request('/api/save-sync', identity.token, first)));
  for (const duplicate of duplicates) assert.deepEqual(duplicate, receipt);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', identity.token, first), receipt);
  assert.equal((await f.request('/api/save-sync', identity.token, { ...first, events: [ring(960)] })).body.error, 'batch_mismatch');
  assert.equal((await f.request('/api/save-sync', identity.token, batch(1, 'ring-stale-fresh-id', [ring(0)]))).status, 409);
  let before = receipt.body.state;
  let revision = receipt.body.revision;
  for (let attempt = 2; attempt <= 3; attempt++) {
    const opened = await f.request('/api/save-sync', identity.token, batch(revision, `ring-open-${attempt}`, [{ type: 'attack', value: 0 }]));
    assert.equal(opened.status, 200); assert.equal(opened.body.state.phase, 'encounter'); assert.equal(opened.body.state.captureDeferred, 0);
    revision = opened.body.revision;
    const chance = Math.max(1, Math.floor(opened.body.state.wildCaptureChance * 10 / 100));
    const command = batch(revision, `ring-red-attempt-${attempt}`, [ring(0)]);
    const result = await f.request('/api/save-sync', identity.token, command);
    assert.equal(result.status, 200); checkRoll(opened.body.state, result.body.state, chance);
    assert.equal(result.body.state.lastCapture.attempt, attempt);
    assert.deepEqual(await f.request('/api/save-sync', identity.token, command), result);
    before = result.body.state; revision = result.body.revision;
  }
  assert.equal(before.phase, 'encounter'); assert.equal(before.captureAttempts, 3); assert.equal(before.lastCapture.result, 'escaped');
  assert.equal((await f.request('/api/save-sync', identity.token, batch(revision, 'ring-fourth-forbidden', [ring(0)]))).body.error, 'invalid_transition');
  const current = await f.request('/api/save', identity.token);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', identity.token, first), receipt);
  assert.deepEqual(await f.request('/api/save', identity.token), current);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.gameSchemaVersion, stored.rulesVersion], [21, 26, 19]);
  assert.equal(stored.devices[0].events.filter((event: any) => event.type === 'ring-capture').length, 3);
  assert.equal(stored.devices[0].receipts.length, 6, 'preparation, three throws, and two attacks that reopen capture');
});

test('a full roster rejects timing throws without writes or receipts and release retries preserve the waiting encounter', async t => {
  const f = await fixture(t), identity = await f.pair();
  let revision = 0, serial = 0, state: any;
  const submit = async (events: unknown[]) => {
    const command = batch(revision, `ring-full-roster-${++serial}`, events);
    const result = await f.request('/api/save-sync', identity.token, command);
    assert.equal(result.status, 200, JSON.stringify(result.body));
    state = result.body.state; revision = result.body.revision;
    return { command, result };
  };
  await submit([{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }]);
  // Build a valid collection through real deterministic events; no store or
  // snapshot patching can hide dropped members or broken replay validation.
  for (let encounter = 0; encounter < 512 && state.collection.length < 60; ++encounter) {
    // Rules 17: treat a knockout injury first so recovery reaches full health.
    const hurt = state.collection.find((member: any) => member.id === state.activeCreatureId)?.injury;
    const recovery = [...(hurt ? [{ type: 'treat', value: 0 }] : []), ...Array.from({ length: hurt ? 12 : state.recoveryRestCount }, () => ({ type: 'rest', value: 0 }))];
    const membersBefore = structuredClone(state.collection);
    const partnerBefore = state.activeCreatureId;
    await submit([...recovery, { type: 'walk', value: 100 }, { type: 'auto', value: 0 }]);
    if (membersBefore.length === 59 && state.collection.length === 60) {
      assert.equal(state.activeCreatureId, partnerBefore);
      assert.deepEqual(state.collection.slice(0, 59).map((member: any) => member.id), membersBefore.map((member: any) => member.id));
      assert.deepEqual(state.collection.filter((member: any) => member.id !== partnerBefore).slice(0, 58), membersBefore.filter((member: any) => member.id !== partnerBefore));
      assert.ok(state.collection.at(-1).id > membersBefore.at(-1).id);
    }
  }
  assert.equal(state.collection.length, 60); assert.ok(state.captures >= 59);
  assert.equal(new Set(state.collection.map((member: any) => member.id)).size, 60);
  await submit([{ type: 'mode', value: 0 }, { type: 'walk', value: 100 }]);
  assert.equal(state.phase, 'encounter'); assert.equal(state.wildCaptureChance, 0);
  const full = await f.request('/api/save', identity.token);
  const diskBefore = await readFile(join(f.dataDir, 'store.json'), 'utf8');
  for (const phase of [0, 960, 2399]) {
    const rejected = await f.request('/api/save-sync', identity.token, batch(revision, `ring-full-denied-${phase}`, [ring(phase)]));
    assert.equal(rejected.status, 422); assert.equal(rejected.body.error, 'invalid_transition');
    assert.deepEqual(await f.request('/api/save', identity.token), full, 'no attempts, RNG, member or revision changes');
    assert.equal(await readFile(join(f.dataDir, 'store.json'), 'utf8'), diskBefore, 'rejection persists no receipt or partial event');
  }
  await f.restart(); assert.deepEqual(await f.request('/api/save', identity.token), full);
  const releasedId = state.collection.find((member: any) => member.id !== state.activeCreatureId).id;
  const released = await submit([{ type: 'release', value: releasedId }]);
  assert.equal(state.collection.length, 59);
  assert.deepEqual(state.collection, full.body.state.collection.filter((member: any) => member.id !== releasedId));
  for (const key of ['phase', 'wildFormId', 'wildHp', 'wildTurn', 'rngState', 'captureAttempts', 'captures', 'activeCreatureId']) {
    assert.equal(state[key], full.body.state[key], `release preserves ${key}`);
  }
  assert.deepEqual(await f.request('/api/save-sync', identity.token, released.command), released.result, 'retry releases no second member');
  await f.restart(); assert.deepEqual((await f.request('/api/save', identity.token)).body.state, state);
});

test('Auto accepts timing only at its durable manual-capture pause', async t => {
  const f = await fixture(t), identity = await f.pair();
  const prepared = await f.request('/api/save-sync', identity.token, batch(0, 'ring-auto-prepare', [
    { type: 'hatch', value: 1 }, { type: 'mode', value: 1 }, { type: 'walk', value: 100 },
  ]));
  assert.equal(prepared.status, 200);
  assert.equal((await f.request('/api/save-sync', identity.token, batch(1, 'ring-auto-before-pause', [ring(960)]))).body.error, 'invalid_transition');
  assert.deepEqual((await f.request('/api/save', identity.token)).body.state, prepared.body.state);
  const paused = await f.request('/api/save-sync', identity.token, batch(1, 'ring-auto-fight-pause', [{ type: 'auto-fight', value: 0 }]));
  assert.equal(paused.status, 200); assert.equal(paused.body.state.autoCapture, 1);
  assert.ok(paused.body.autoTrace.steps.every((step: any) => step.action !== 'capture'));
  const thrown = await f.request('/api/save-sync', identity.token, batch(2, 'ring-auto-manual-red', [ring(0)]));
  assert.equal(thrown.status, 200); checkRoll(paused.body.state, thrown.body.state, Math.max(1, Math.floor(paused.body.state.wildCaptureChance / 10)));
  assert.equal(thrown.body.autoTrace, null, 'new direct capture records do not loosen historical Auto trace validation');
});

test('pre13 histories reject the new timing action instead of relabelling old saves', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'digivice-ring-history-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const events = [ring(0)], dataDir = join(directory, 'rules12');
  const body = { rulesVersion: 12, baseRevision: 0, events };
  const device = { deviceId: `dv_${'a'.repeat(24)}`, tokenHash: '0'.repeat(64), seed: 12345, revision: 1, events,
    receipts: [{ batchId: 'unsupported-old-timing', bodyHash: hash(JSON.stringify(body)), revision: 1, eventEnd: 1 }], legacy: null, initialMode: 'onboarding' };
  await mkdir(dataDir);
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify({ formatVersion: 14, gameSchemaVersion: 16, rulesVersion: 12, devices: [device] }));
  assert.throws(() => createApp({ rootDir, dataDir, corePath, battleCorePath }), /Unsupported or corrupt event history/);
});

test('a stale or mismatched native binary cannot advertise capture timing quality', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'digivice-ring-stale-core-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const contract = { inputVersion: 1, action: 'ring-capture', cycleMs: 2400, factors: { red: 10, orange: 50, green: 100 } };
  let serial = 0;
  for (const response of [{ schemaVersion: 22, rulesVersion: 15 }, { ...contract, inputVersion: 0 }, { ...contract, cycleMs: 2399 },
    { ...contract, factors: { red: 0, orange: 50, green: 100 } }, { ...contract, factors: { red: 10, orange: 50, green: 101 } }, { ...contract, extra: true }]) {
    const staleCore = join(directory, `old-core-${++serial}.sh`);
    const script = `#!/bin/sh\nif [ "$1" = '--flick-trajectory' ]; then\nprintf '%s\\n' '{"inputVersion":1,"landingX":206,"landingY":120,"hit":true}'\nelse\nprintf '%s\\n' '${JSON.stringify(response)}'\nfi\n`;
    await writeFile(staleCore, script, { mode: 0o700 });
    assert.throws(() => createApp({ rootDir, dataDir: join(directory, `data-${serial}`), corePath: staleCore }), /does not support capture timing quality input version 1/);
  }
});

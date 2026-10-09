import test from 'node:test';
import assert from 'node:assert/strict';
import { request as httpRequest } from 'node:http';
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';

const rootDir = resolve(import.meta.dirname, '..');
type Event = { type: string; value: number };
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, brokenArt = false) {
  const directory = await mkdtemp(join(tmpdir(), 'digivice-park-journey-'));
  let clock = 1_000_000;
  if (brokenArt) {
    await mkdir(join(directory, 'assets/packs'), { recursive: true });
    await writeFile(join(directory, 'assets/packs/catalog.json'), '{truncated');
  }
  const options = { seedSource: () => 12345, rootDir: brokenArt ? directory : rootDir, dataDir: join(directory, '.data'), port: 0,
    corePath: process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core'),
    battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle'), now: () => clock };
  let app = await startServer(options);
  const base = () => `http://127.0.0.1:${(app.server.address() as { port: number }).port}`;
  const close = async () => { await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve())); app.close(); };
  t.after(async () => { if (app.server.listening) await close(); await rm(directory, { recursive: true, force: true }); });
  const request = async (path: string, token?: string, body?: unknown) => {
    const response = await fetch(base() + path, { method: body === undefined ? 'GET' : 'POST', headers: {
      ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }),
    }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  const pair = async () => {
    const start = await request('/api/pairing/start', undefined, {}); assert.equal(start.status, 201);
    const claim = await request('/api/pairing/claim', undefined, { code: start.body.code }); assert.equal(claim.status, 201);
    return claim.body;
  };
  return { directory, request, pair, advance: (ms: number) => { clock += ms; },
    restart: async () => { await close(); app = await startServer(options); },
    loseAcknowledgement: (token: string, body: unknown) => new Promise<void>((resolve, reject) => {
      const request = httpRequest(base() + '/api/save-sync', { method: 'POST', headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' } }, response => {
        if (response.statusCode !== 200) { response.resume(); reject(new Error(`Unexpected pre-disconnect status ${response.statusCode}`)); return; }
        // The durable server write precedes headers. Drop the unread body so the
        // caller has no save result; its next step must be the identical retry.
        response.destroy(); resolve();
      });
      request.on('error', reject); request.end(JSON.stringify(body));
    }),
  };
}

test('park setup survives expired pairing, missing artwork, lost hatch/recovery ACKs and a day offline', async t => {
  const f = await fixture(t, true);
  assert.equal((await f.request('/api/health')).body.assetStatus, 'unavailable');
  assert.equal((await f.request('/api/assets/catalog')).status, 503);
  const expired = await f.request('/api/pairing/start', undefined, {});
  f.advance(300_001);
  assert.equal((await f.request('/api/pairing/claim', undefined, { code: expired.body.code })).status, 401);
  const pendingCode = await f.request('/api/pairing/start', undefined, {});
  await f.restart();
  assert.equal((await f.request('/api/pairing/claim', undefined, { code: pendingCode.body.code })).status, 401, 'restart does not revive unclaimed codes');
  const identity = await f.pair();
  assert.equal(identity.state.phase, 'egg');
  assert.equal((await readFile(join(f.directory, '.data/store.json'), 'utf8')).includes(identity.token), false);
  const hatch = { rulesVersion: identity.state.rulesVersion, baseRevision: 0, batchId: 'park-confirmed-hatch-lost-ack', events: [{ type: 'hatch', value: 1 }] };
  await f.loseAcknowledgement(identity.token, hatch);
  await f.restart();
  const saved = await f.request('/api/save', identity.token); assert.equal(saved.body.revision, 1);
  const acknowledgement = await f.request('/api/save-sync', identity.token, hatch);
  assert.equal(acknowledgement.status, 200); assert.deepEqual(acknowledgement.body.state, saved.body.state);
  assert.equal(saved.body.state.collection.length, 1);
  const play = { ...hatch, baseRevision: 1, batchId: 'park-play-before-rest', events: [{ type: 'play', value: 0 }] };
  const tired = await f.request('/api/save-sync', identity.token, play); assert.equal(tired.status, 200);
  const rests = Math.ceil(Math.max(tired.body.state.combat.maxHp - tired.body.state.hp, 100 - tired.body.state.energy) / 25);
  assert.ok(rests >= 1 && rests <= 16);
  const recovery = { ...hatch, baseRevision: 2, batchId: 'park-confirmed-recovery-lost-ack', events: Array.from({ length: rests }, () => ({ type: 'rest', value: 0 })) };
  await f.loseAcknowledgement(identity.token, recovery); await f.restart();
  const recovered = await f.request('/api/save-sync', identity.token, recovery); assert.equal(recovered.status, 200);
  assert.equal(recovered.body.state.hp, recovered.body.state.combat.maxHp); assert.equal(recovered.body.state.energy, 100);
  assert.equal(recovered.body.state.xp, tired.body.state.xp); assert.equal(recovered.body.state.captures, tired.body.state.captures);
  const beforeTime = await f.request('/api/save', identity.token);
  f.advance(24 * 60 * 60 * 1000); await f.restart();
  assert.deepEqual(await f.request('/api/save', identity.token), beforeTime, 'offline elapsed time adds no missed-care penalty');
  assert.deepEqual(await f.request('/api/save-sync', identity.token, recovery), recovered);
  assert.equal((await f.request('/api/assets/catalog')).status, 503, 'optional art failure does not prevent durable care');
});

test('park roster-full/release flow preserves member identity and exact ACKs across disconnect and restart', async t => {
  const f = await fixture(t), identity = await f.pair();
  let current = identity, serial = 0;
  const command = (events: Event[]) => ({ rulesVersion: identity.state.rulesVersion, baseRevision: current.revision, batchId: `park-roster-command-${++serial}`, events });
  const submit = async (body: ReturnType<typeof command>) => {
    const result = await f.request('/api/save-sync', identity.token, body); assert.equal(result.status, 200); current = result.body; return result;
  };
  await submit(command([{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }]));
  for (let attempt = 0; attempt < 512 && current.state.collection.length < 60; attempt++) {
    await submit(command([...Array.from({ length: 16 }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }, { type: 'auto', value: 0 }]));
    assert.equal(current.state.phase, 'home'); assert.ok(current.state.collection.length <= 60);
  }
  assert.equal(current.state.collection.length, 60, 'bounded real native encounters reach the capacity fixture');
  const ids = current.state.collection.map((member: any) => member.id);
  const totalCaptures = current.state.captures;
  await submit(command([{ type: 'mode', value: 0 }, { type: 'walk', value: 100 }]));
  const fullEncounter = await f.request('/api/save', identity.token);
  const blockedCapture = await f.request('/api/save-sync', identity.token, command([{ type: 'capture', value: 0 }]));
  assert.equal(blockedCapture.status, 422);
  assert.deepEqual(await f.request('/api/save', identity.token), fullEncounter, 'failed full capture consumes neither RNG nor another receipt');
  const invalidRecovery = await f.request('/api/save-sync', identity.token, command([{ type: 'rest', value: 0 }, { type: 'rest', value: 0 }]));
  assert.equal(invalidRecovery.status, 422);
  assert.deepEqual(await f.request('/api/save', identity.token), fullEncounter, 'a recovery batch cannot bypass encounter restrictions');
  const releasedId = ids.find((id: number) => id !== current.state.activeCreatureId)!;
  const survivors = current.state.collection.filter((member: any) => member.id !== releasedId);
  const journal = structuredClone(current.state.journal), nextMemberId = current.state.nextMemberId;
  assert.equal((await f.request('/api/save-sync', identity.token, command([{ type: 'release', value: releasedId }, { type: 'rest', value: 0 }]))).status, 422);
  assert.deepEqual(await f.request('/api/save', identity.token), fullEncounter, 'invalid recovery after a release rolls back the entire batch');
  assert.equal((await f.request('/api/save-sync', identity.token, command([{ type: 'release', value: current.state.activeCreatureId }]))).status, 422);
  const release = command([{ type: 'release', value: releasedId }]);
  await f.loseAcknowledgement(identity.token, release); await f.restart();
  const accepted = await f.request('/api/save-sync', identity.token, release); assert.equal(accepted.status, 200); current = accepted.body;
  assert.deepEqual(current.state.collection, survivors); assert.deepEqual(current.state.journal, journal);
  assert.equal(current.state.nextMemberId, nextMemberId); assert.equal(current.state.captures, totalCaptures);
  for (const key of ['phase', 'activeCreatureId', 'hp', 'energy', 'rngState', 'wildFormId', 'wildSpecies', 'wildLevel', 'wildHp', 'wildTurn', 'wildGuard', 'wildRules', 'wildRarity', 'captureAttempts', 'cardUsed', 'attackBoost', 'shield', 'battleMode']) {
    assert.deepEqual(current.state[key], fullEncounter.body.state[key], key);
  }
  assert.equal(current.state.sequence, fullEncounter.body.state.sequence + 1);
  for (let turn = 0; turn < 48 && current.state.phase === 'encounter' && current.state.wildHp > current.state.wildMaxHp / 2; turn++) await submit(command([{ type: 'attack', value: 0 }]));
  assert.equal(current.state.phase, 'encounter', 'the retained target can still be weakened after making room');
  const capture = command([{ type: 'capture', value: 0 }]);
  const attempted = await submit(capture); assert.equal(attempted.status, 200, 'capture is legal after the explicit release');
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', identity.token, capture), attempted);
  for (let turn = 0; turn < 48 && current.state.phase === 'encounter'; turn++) await submit(command([{ type: 'attack', value: 0 }]));
  assert.equal(current.state.phase, 'home'); assert.ok([totalCaptures, totalCaptures + 1].includes(current.state.captures));
  if (current.state.captures > totalCaptures) assert.equal(current.state.collection.at(-1).id, nextMemberId);
  await submit(command([{ type: 'rest', value: 0 }]));
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', identity.token, release), accepted);
  assert.equal((await f.request('/api/save-sync', identity.token, command([{ type: 'release', value: current.state.activeCreatureId }]))).status, 422);
});

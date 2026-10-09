import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { startServer } from '../service/server.ts';
import { namedFixture } from './legacy-fixture.ts';

test('practice HTTP uses paired identity and authoritative companion without changing pet progress', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-battle-http-'));
  const historical = namedFixture(2);
  await writeFile(join(dataDir, 'store.json'), JSON.stringify(historical.store));
  const app = await startServer({ corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 });
  t.after(async () => {
    await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
    await rm(dataDir, { recursive: true, force: true });
  });
  const base = `http://127.0.0.1:${(app.server.address() as {port:number}).port}`;
  const request = async (path: string, token?: string, body?: unknown) => {
    const r = await fetch(base + path, {
      method: body === undefined ? 'GET' : 'POST',
      headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    return { status: r.status, body: await r.json() as any };
  };
  let nextIdentity = 0;
  const pair = async () => {
    const identity = historical.identities[nextIdentity++];
    return { ...(await request('/api/save', identity.token)).body, ...identity };
  };
  for (const path of ['/api/battle', '/api/battle/start', '/api/battle/act']) {
    assert.equal((await request(path, undefined, path.endsWith('battle') ? undefined : {})).status, 401);
  }
  const first = await pair(), other = await pair();
  const before = (await request('/api/save', first.token)).body;
  assert.deepEqual((await request('/api/battle', first.token)).body, { revision: 0, battle: null, mode: 'tactical', autoTrace: null });
  const start = { rulesVersion: 7, expectedRevision: 0, requestId: 'battle-http-start-001' };
  const forged = await request('/api/battle/start', first.token, { ...start, profile: { level: 3 } });
  assert.equal(forged.status, 400);
  const accepted = await request('/api/battle/start', first.token, start);
  assert.equal(accepted.status, 200);
  assert.equal(accepted.body.revision, 1);
  assert.deepEqual(accepted.body.battle.companion, { id: 1, species: 'impmon', name: 'Impmon', level: 1, formId: 11 });
  assert.equal(accepted.body.battle.phase, 'attack');
  assert.deepEqual(await request('/api/battle/start', first.token, start), accepted);
  assert.deepEqual((await request('/api/battle', other.token)).body, { revision: 0, battle: null, mode: 'tactical', autoTrace: null });
  for (const key of ['snapshot', 'snapshotBase64', 'rngState', 'committedChoice', 'seed']) {
    assert.equal(JSON.stringify(accepted.body).includes(`"${key}"`), false);
  }
  const retreat = await request('/api/battle/act', first.token, {
    rulesVersion: 7, expectedRevision: 1, requestId: 'battle-http-retreat-001', action: { type: 'retreat', value: 0 },
  });
  assert.equal(retreat.status, 200);
  assert.equal(retreat.body.battle.status, 'retreated');
  assert.deepEqual((await request('/api/save', first.token)).body, before);
  assert.deepEqual(await request('/api/battle/start', first.token, start), accepted);
});

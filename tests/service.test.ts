import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { request as httpRequest } from 'node:http';
import { createApp, startServer } from '../service/server.ts';
import { legacyFixture } from './legacy-fixture.ts';

const rootDir = resolve(fileURLToPath(new URL('..', import.meta.url)));
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');

async function fixture(now?: () => number, legacyCount = 0) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-service-test-'));
  const historical = legacyFixture(legacyCount);
  if (legacyCount) await writeFile(join(dataDir, 'store.json'), JSON.stringify(historical.store));
  let app = await startServer({ seedSource: () => 12345, dataDir, rootDir, corePath, port: 0, now });
  const url = () => `http://127.0.0.1:${(app.server.address() as { port: number }).port}`;
  const request = async (path: string, options: { method?: string; body?: unknown; token?: string; headers?: Record<string, string> } = {}) => {
    const response = await fetch(`${url()}${path}`, {
      method: options.method ?? (options.body === undefined ? 'GET' : 'POST'),
      headers: { ...(options.body === undefined ? {} : { 'content-type': 'application/json' }), ...(options.token ? { authorization: `Bearer ${options.token}` } : {}), ...options.headers },
      body: options.body === undefined ? undefined : JSON.stringify(options.body),
    });
    const text = await response.text();
    let body;
    try { body = JSON.parse(text); } catch { body = text; }
    return { status: response.status, body, headers: response.headers, text };
  };
  const close = () => new Promise<void>((resolve, reject) => app.server.close((error) => error ? reject(error) : resolve()));
  const pair = async () => {
    const start = await request('/api/pairing/start', { body: {} });
    assert.equal(start.status, 201);
    const claim = await request('/api/pairing/claim', { body: { code: start.body.code } });
    assert.equal(claim.status, 201);
    return claim.body;
  };
  return {
    request, pair, dataDir, url,
    legacy: async (index = 0) => {
      const identity = historical.identities[index];
      const saved = await request('/api/save', { token: identity.token });
      assert.equal(saved.status, 200);
      return { ...saved.body, ...identity };
    },
    rawStatus: (headers: Record<string, string>) => new Promise<number>((resolve, reject) => {
      const request = httpRequest(`${url()}/api/health`, { headers }, (response) => {
        response.resume();
        response.on('end', () => resolve(response.statusCode!));
      });
      request.on('error', reject); request.end();
    }),
    stop: close,
    restart: async () => { await close(); app = await startServer({ seedSource: () => 12345, dataDir, rootDir, corePath, port: 0, now }); },
    dispose: async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); },
  };
}

test('HTTP service validates replay, identity, retries, concurrency, and production catalog boundaries', async (t) => {
  const f = await fixture(undefined, 1);
  t.after(f.dispose);
  const paired = await f.legacy();
  const token = paired.token;
  assert.equal(paired.state.schemaVersion, 27);
  assert.equal(paired.state.rulesVersion, 19);
  assert.equal(paired.revision, 0);
  assert.deepEqual(paired.events, []);
  assert.equal(paired.seed, 12345);

  await t.test('legacy credentials stay hashed on disk and remain required for saves', async () => {
    assert.match(paired.deviceId, /^dv_[a-f0-9]{24}$/);
    assert.match(token, /^[A-Za-z0-9_-]{43}$/);
    assert.equal((await f.request('/api/save')).status, 401);
    assert.equal((await f.request('/api/save', { token: 'a'.repeat(43) })).status, 401);
    assert.equal((await f.request('/api/save', { token })).status, 200);
    const raw = await readFile(join(f.dataDir, 'store.json'), 'utf8');
    assert.equal(raw.includes(token), false);
    assert.equal(JSON.parse(raw).devices[0].tokenHash, createHash('sha256').update(token).digest('hex'));
    assert.equal((await stat(join(f.dataDir, 'store.json'))).mode & 0o777, 0o600);
  });

  await t.test('same batch and semantic body retries return original response even after later updates', async () => {
    const first = { rulesVersion: 19, baseRevision: 0, batchId: 'first-batch-001', events: [{ type: 'feed', value: 0 }] };
    const accepted = await f.request('/api/save-sync', { token, body: first });
    assert.equal(accepted.status, 200);
    assert.equal(accepted.body.revision, 1);
    assert.equal(accepted.body.state.sequence, 1);
    const second = await f.request('/api/save-sync', { token, body: { rulesVersion: 19, baseRevision: 1, batchId: 'second-batch-001', events: [{ type: 'play', value: 0 }] } });
    assert.equal(second.status, 200);
    const retry = await f.request('/api/save-sync', { token, body: { rulesVersion: 19, events: [{ value: 0, type: 'feed' }], batchId: first.batchId, baseRevision: 0 } });
    assert.deepEqual(retry.body, accepted.body);
    const mismatch = await f.request('/api/save-sync', { token, body: { ...first, events: [{ type: 'rest', value: 0 }] } });
    assert.equal(mismatch.status, 409);
    assert.equal(mismatch.body.error, 'batch_mismatch');
    const stale = await f.request('/api/save-sync', { token, body: { ...first, batchId: 'stale-batch-001' } });
    assert.equal(stale.status, 409);
    assert.equal(stale.body.error, 'revision_conflict');
    await f.restart();
    assert.deepEqual((await f.request('/api/save-sync', { token, body: first })).body, accepted.body);
    assert.equal((await f.request('/api/save', { token })).body.revision, 2);
  });

  await t.test('invalid actions, transitions, metadata and raw GPS cannot alter saves', async () => {
    for (const events of [[{ type: 'teleport', value: 0 }], [{ type: 'walk', value: 1001 }], [{ type: 'walk', value: -1 }], [{ type: 'walk', value: 1.5 }], [{ type: 'card', value: 7 }], [{ type: 'feed', value: 1 }], [{ type: 'feed', value: 0, latitude: 1 }], [], Array.from({ length: 101 }, () => ({ type: 'feed', value: 0 }))]) {
      assert.equal((await f.request('/api/save-sync', { token, body: { rulesVersion: 19, baseRevision: 2, batchId: 'invalid-batch-001', events } })).status, 422);
    }
    const invalidTransition = await f.request('/api/save-sync', { token, body: { rulesVersion: 19, baseRevision: 2, batchId: 'invalid-attack-001', events: [{ type: 'attack', value: 0 }] } });
    assert.equal(invalidTransition.status, 422);
    assert.equal(invalidTransition.body.error, 'invalid_transition');
    assert.equal((await f.request('/api/save-sync', { token, body: { rulesVersion: 19, baseRevision: 2, batchId: 'extra-field-001', events: [{ type: 'rest', value: 0 }], gps: [1, 2] } })).status, 400);
    assert.equal((await f.request('/api/save', { token })).body.revision, 2);
  });

  await t.test('concurrent writes from the same revision accept one batch', async () => {
    const results = await Promise.all(['concurrent-001', 'concurrent-002'].map((batchId) => f.request('/api/save-sync', { token, body: { rulesVersion: 19, baseRevision: 2, batchId, events: [{ type: 'rest', value: 0 }] } })));
    assert.deepEqual(results.map((response) => response.status).sort(), [200, 409]);
    assert.equal((await f.request('/api/save', { token })).body.revision, 3);
  });

  await t.test('loopback host and origin checks stop cross-site access', async () => {
    assert.equal((await f.request('/api/health', { headers: { origin: 'https://malicious.example' } })).status, 403);
    assert.equal(await f.rawStatus({ host: 'malicious.example' }), 403);
    assert.equal(await f.rawStatus({ 'sec-fetch-site': 'cross-site' }), 403);
    assert.equal((await f.request('/api/health', { headers: { origin: f.url() } })).status, 200);
    const response = await f.request('/api/health');
    assert.equal(response.headers.get('access-control-allow-origin'), null);
    assert.equal(response.headers.get('x-content-type-options'), 'nosniff');
  });

  await t.test('only allowlisted assets are served and catalog verifies pack bytes', async () => {
    const combat = await f.request('/api/combat/catalog');
    assert.equal(combat.status, 200); assert.equal(combat.body.rulesVersion, 19);
    assert.equal(combat.body.profiles.length, 8);
    for (const lineage of ['mote','flicker','rill','cinder']) assert.equal((await f.request(`/api/evolution/catalog?species=${lineage}`)).body.error,'invalid_lineage');
    assert.ok(combat.body.profiles.every((profile: { species: string }) => !['mote', 'bramble', 'rill', 'flicker'].includes(profile.species)));
    const catalog = await f.request('/api/catalog');
    assert.equal(catalog.status, 200);
    assert.deepEqual(catalog.body.packs, []);
    assert.equal((await f.request('/api/packs/starter-v1')).status, 404);
    assert.equal((await f.request('/')).status, 200);
    assert.equal((await f.request('/styles.css')).status, 200);
    assert.equal((await f.request('/app.js')).status, 200);
    for (const path of ['/service/server.ts', '/.data/store.json', '/assets/../service/server.ts', '/%2e%2e%2fservice%2fserver.ts']) assert.equal((await f.request(path)).status, 404);
  });

  await t.test('payload size is bounded before replay', async () => {
    assert.equal((await f.request('/api/save-sync', { token, body: { padding: 'x'.repeat(33 * 1024) } })).status, 413);
  });
});

test('corrupt primary recovers the latest acknowledged save and durable idempotency receipt', async (t) => {
  const f = await fixture(undefined, 1);
  t.after(f.dispose);
  const paired = await f.legacy();
  const batch = { rulesVersion: 19, baseRevision: 0, batchId: 'recover-batch-001', events: [{ type: 'walk', value: 100 }] };
  const accepted = await f.request('/api/save-sync', { token: paired.token, body: batch });
  assert.equal(accepted.status, 200);
  await writeFile(join(f.dataDir, 'store.json'), '{interrupted/corrupt');
  await f.restart();
  assert.equal((await f.request('/api/health')).body.recoveredFromBackup, true);
  const saved = await f.request('/api/save', { token: paired.token });
  assert.equal(saved.body.revision, 1);
  assert.deepEqual(saved.body.state, accepted.body.state);
  assert.deepEqual((await f.request('/api/save-sync', { token: paired.token, body: batch })).body, accepted.body);
});

test('unknown on-disk schema/rules version fails closed instead of rolling back or resetting identity', async (t) => {
  const f = await fixture();
  t.after(f.dispose);
  await f.pair();
  await f.stop();
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  stored.rulesVersion = 999;
  await writeFile(join(f.dataDir, 'store.json'), JSON.stringify(stored));
  assert.throws(() => createApp({ dataDir: f.dataDir, rootDir, corePath }), /explicit migration/);
});

test('pairing code expiry, single use and device bounds', async (t) => {
  let time = 1_000_000;
  const f = await fixture(() => time);
  t.after(f.dispose);
  const expired = await f.request('/api/pairing/start', { body: {} });
  time += 300_001;
  assert.equal((await f.request('/api/pairing/claim', { body: { code: expired.body.code } })).status, 401);
  const start = await f.request('/api/pairing/start', { body: {} });
  const claimed = await f.request('/api/pairing/claim', { body: { code: start.body.code } });
  assert.equal(claimed.status, 201);
  assert.match(claimed.body.token, /^[A-Za-z0-9_-]{43}$/);
  assert.match(claimed.body.deviceId, /^dv_[a-f0-9]{24}$/);
  assert.equal(claimed.body.state.phase, 'egg');
  assert.equal((await readFile(join(f.dataDir, 'store.json'), 'utf8')).includes(claimed.body.token), false);
  assert.equal((await f.request('/api/pairing/claim', { body: { code: start.body.code } })).status, 401);
  for (let i = 0; i < 7; i++) await f.pair();
  assert.equal((await f.request('/api/pairing/start', { body: {} })).status, 409);
});

test('exclusive writer lock rejects a second process and clean shutdown releases it', async (t) => {
  const f = await fixture();
  t.after(f.dispose);
  assert.throws(() => createApp({ dataDir: f.dataDir, rootDir, corePath }), /locked by another service/);
  await f.stop();
  const app = createApp({ dataDir: f.dataDir, rootDir, corePath });
  app.close();
});

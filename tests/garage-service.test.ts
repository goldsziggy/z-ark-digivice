import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { startServer, type GarageReadAdapter } from '../service/server.ts';

const rootDir = resolve(fileURLToPath(new URL('..', import.meta.url)));
const SECRET = 'UPSTREAM_SECRET_MUST_NOT_REACH_BROWSER';
const originalBytes = await readFile(join(rootDir, 'assets/packs/starter-v2.json'));
const originalHash = createHash('sha256').update(originalBytes).digest('hex');
const originalEntry = { id: 'starter-v2', version: 1, kind: 'original', bytes: originalBytes.length, sha256: originalHash };

function adapterFixture() {
  const calls: string[] = [];
  const adapter: GarageReadAdapter = {
    async list() {
      calls.push('list');
      return { status: 'available', endpoint: SECRET, bucket: SECRET, diagnostics: { secret: SECRET },
        packs: [{ ...originalEntry, objectKey: SECRET, credential: SECRET },
          { id: 'personal-example', version: 1, kind: 'personal', bytes: 64, sha256: 'a'.repeat(64) }] };
    },
    async fetchPack(id, version) { calls.push(`pack:${id}:${version}`); return { bytes: originalBytes, sha256: originalHash, contentType: 'application/json' }; },
    async fetchPersonal(id, version) {
      calls.push(`personal:${id}:${version}`);
      if (id !== 'personal-example') throw Object.assign(new Error(SECRET), { status: 404, code: SECRET });
      return { packText: '{"fixture":"personal"}', provenanceText: '{"fixture":"provenance"}', credential: SECRET } as { packText: string; provenanceText: string };
    },
    close() { calls.push('close'); },
  };
  return { calls, adapter };
}
async function fixture(garageAssets?: GarageReadAdapter, includeTestFixtures = true) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-garage-http-'));
  let app: Awaited<ReturnType<typeof startServer>>;
  try { app = await startServer({ includeTestFixtures, dataDir, rootDir, corePath: process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core'), port: 0, garageAssets }); }
  catch (error) { await rm(dataDir, { recursive: true, force: true }); throw error; }
  const address = app.server.address(); assert.ok(address && typeof address !== 'string');
  const base = `http://127.0.0.1:${address.port}`;
  const request = async (path: string, options: { token?: string; method?: string; body?: unknown; headers?: Record<string, string> } = {}) => {
    const response = await fetch(base + path, { method: options.method ?? (options.body === undefined ? 'GET' : 'POST'),
      headers: { ...(options.token ? { Authorization: `Bearer ${options.token}` } : {}),
        ...(options.body === undefined ? {} : { 'Content-Type': 'application/json' }), ...options.headers },
      body: options.body === undefined ? undefined : JSON.stringify(options.body) });
    const text = await response.text(); let body;
    try { body = JSON.parse(text); } catch { body = text; }
    return { status: response.status, body, text, headers: response.headers };
  };
  const pair = async () => {
    const start = await request('/api/pairing/start', { body: {} }); assert.equal(start.status, 201);
    const claim = await request('/api/pairing/claim', { body: { code: start.body.code } }); assert.equal(claim.status, 201);
    return claim.body.token as string;
  };
  const dispose = async () => {
    if (app.server.listening) await new Promise<void>(resolve => app.server.close(() => resolve()));
    app.close(); await rm(dataDir, { recursive: true, force: true });
  };
  return { request, pair, dispose };
}

test('Garage bearer checks run before route parsing, methods, or any adapter operation', async t => {
  const backend = adapterFixture(), app = await fixture(backend.adapter); t.after(app.dispose);
  for (const path of ['/api/garage/catalog', '/api/garage/packs/starter-v2/1', '/api/garage/personal/personal-example/1',
    '/api/garage/unknown', '/api/garage/catalog?objectKey=secret', '/api/garage']) {
    assert.equal((await app.request(path)).status, 401);
    assert.equal((await app.request(path, { token: 'a'.repeat(43) })).status, 401);
    assert.equal((await app.request(path, { method: 'POST', body: {} })).status, 401);
  }
  assert.deepEqual(backend.calls, []);
  const token = await app.pair();
  assert.equal((await app.request('/api/garage/catalog', { token, headers: { Origin: 'https://untrusted.example' } })).status, 403);
  assert.deepEqual(backend.calls, []);
});

test('authenticated Garage routes return bounded verified bytes and project safe metadata only', async t => {
  const backend = adapterFixture(), app = await fixture(backend.adapter); t.after(app.dispose);
  const token = await app.pair();
  const catalog = await app.request('/api/garage/catalog', { token });
  assert.equal(catalog.status, 200); assert.equal(catalog.text.includes(SECRET), false);
  assert.deepEqual(catalog.body.packs[0], originalEntry);
  assert.deepEqual(Object.keys(catalog.body).sort(), ['packs', 'status']);
  const pack = await app.request('/api/garage/packs/starter-v2/1', { token });
  assert.equal(pack.status, 200); assert.equal(pack.text, originalBytes.toString('utf8'));
  assert.equal(pack.headers.get('etag'), `"${originalHash}"`);
  assert.equal(pack.headers.get('cache-control'), 'no-store');
  const personal = await app.request('/api/garage/personal/personal-example/1', { token });
  assert.equal(personal.status, 200); assert.equal(personal.text.includes(SECRET), false);
  assert.deepEqual(personal.body, { packText: '{"fixture":"personal"}', provenanceText: '{"fixture":"provenance"}' });
  assert.deepEqual(backend.calls, ['list', 'pack:starter-v2:1', 'personal:personal-example:1']);
});

test('unknown paths, IDs, versions, queries and upload methods cannot select remote object keys', async t => {
  const backend = adapterFixture(), app = await fixture(backend.adapter); t.after(app.dispose);
  const token = await app.pair();
  for (const path of ['/api/garage/diagnose', '/api/garage/upload', '/api/garage/packs/arbitrary-key/1',
    '/api/garage/packs/starter-v2/2', '/api/garage/personal/other/1', '/api/garage/personal/personal-example/0',
    '/api/garage/personal/personal-example/01', '/api/garage/personal/personal-example/2147483648',
    '/api/garage/personal/personal-example%2Fsecret/1', '/api/garage/packs/https%3A%2F%2Fexample.com/1']) {
    assert.equal((await app.request(path, { token })).status, 404, path);
  }
  assert.equal((await app.request('/api/garage/catalog?bucket=other', { token })).status, 400);
  assert.equal((await app.request('/api/garage/catalog', { token, method: 'POST', body: { key: 'anything' } })).status, 405);
  assert.deepEqual(backend.calls, []);
  const unknown = await app.request('/api/garage/personal/personal-unknown/1', { token });
  assert.equal(unknown.status, 404); assert.equal(unknown.text.includes(SECRET), false);
  assert.deepEqual(backend.calls, ['personal:personal-unknown:1']);
});

test('Garage failures are sanitized and local health, signed assets, pairing and save replay remain healthy', async t => {
  const backend = adapterFixture();
  backend.adapter.list = async () => { throw Object.assign(new Error(`Endpoint credential ${SECRET}`), { status: 503, code: SECRET, diagnostic: SECRET }); };
  const app = await fixture(backend.adapter); t.after(app.dispose); const token = await app.pair();
  const unavailable = await app.request('/api/garage/catalog', { token });
  assert.equal(unavailable.status, 503); assert.equal(unavailable.body.error, 'garage_unavailable');
  assert.match(unavailable.body.message, /private Garage bucket/); assert.equal(unavailable.text.includes(SECRET), false);
  assert.ok(unavailable.text.length < 512);
  assert.equal((await app.request('/api/health')).status, 200);
  assert.equal((await app.request('/api/assets/catalog')).status, 200);
  const saved = await app.request('/api/save-sync', { token, body: { rulesVersion: 18, baseRevision: 0, batchId: 'garage-offline-feed', events: [{ type: 'hatch', value: 1 }, { type: 'feed', value: 0 }] } });
  assert.equal(saved.status, 200); assert.equal(saved.body.revision, 1);
  const restored = await app.request('/api/save', { token });
  assert.equal(restored.status, 200); assert.equal(restored.body.state.sequence, 2);
});

test('default unconfigured Garage fails safely while local game and assets stay available', async t => {
  const previousBucket = process.env.DIGIVICE_GARAGE_BUCKET;
  const previousEnvFile = process.env.DIGIVICE_GARAGE_ENV_FILE;
  delete process.env.DIGIVICE_GARAGE_BUCKET;
  // Isolate from the developer's real server configuration; this test cannot
  // initialize a remote SDK even if default configuration changes later.
  process.env.DIGIVICE_GARAGE_ENV_FILE = join(tmpdir(), `digivice-absent-garage-env-${process.pid}`);
  t.after(() => {
    if (previousBucket === undefined) delete process.env.DIGIVICE_GARAGE_BUCKET;
    else process.env.DIGIVICE_GARAGE_BUCKET = previousBucket;
    if (previousEnvFile === undefined) delete process.env.DIGIVICE_GARAGE_ENV_FILE;
    else process.env.DIGIVICE_GARAGE_ENV_FILE = previousEnvFile;
  });
  const app = await fixture(); t.after(app.dispose); const token = await app.pair();
  const response = await app.request('/api/garage/catalog', { token });
  assert.equal(response.status, 503); assert.equal(response.body.error, 'garage_unavailable');
  assert.match(response.body.message, /private Garage bucket/); assert.ok(response.text.length < 512);
  assert.equal((await app.request('/api/health')).status, 200);
  assert.equal((await app.request('/api/assets/catalog')).status, 200);
  assert.equal((await app.request('/api/save', { token })).status, 200);
});

test('bad Garage metadata, hashes and sizes fail closed without forwarding upstream errors', async t => {
  const backend = adapterFixture(), app = await fixture(backend.adapter); t.after(app.dispose);
  const token = await app.pair();
  for (const result of [
    { status: 'available', packs: Array.from({ length: 9 }, (_, index) => ({ ...originalEntry, id: `personal-fixture-${index}`, kind: 'personal' })) },
    { status: 'available', packs: [{ ...originalEntry, id: SECRET }] },
    { status: 'available', packs: [{ ...originalEntry, bytes: 262145 }] },
    { status: 'available', packs: [{ ...originalEntry, sha256: SECRET }] },
  ]) {
    backend.adapter.list = async () => result;
    const response = await app.request('/api/garage/catalog', { token });
    assert.equal(response.status, 502); assert.equal(response.text.includes(SECRET), false);
  }
  backend.adapter.fetchPack = async () => ({ bytes: originalBytes, sha256: '0'.repeat(64), contentType: 'application/json' });
  assert.equal((await app.request('/api/garage/packs/starter-v2/1', { token })).status, 502);
  backend.adapter.fetchPack = async () => ({ bytes: new Uint8Array(262145), sha256: originalHash, contentType: 'application/json' });
  assert.equal((await app.request('/api/garage/packs/starter-v2/1', { token })).status, 502);
  backend.adapter.fetchPersonal = async () => ({ packText: '{}', provenanceText: 'x'.repeat(65537) });
  assert.equal((await app.request('/api/garage/personal/personal-example/1', { token })).status, 502);
  backend.adapter.fetchPersonal = async () => { throw Object.assign(new Error(SECRET), { status: 502, code: SECRET }); };
  const damaged = await app.request('/api/garage/personal/personal-example/1', { token });
  assert.equal(damaged.status, 502); assert.equal(damaged.text.includes(SECRET), false);
});

test('audio and Garage browser modules are explicit static routes and adapter closes once', async () => {
  const backend = adapterFixture(), app = await fixture(backend.adapter);
  try {
    for (const path of ['/audio-engine.js', '/garage-library.js']) {
      const response = await app.request(path); assert.equal(response.status, 200, path);
      assert.match(response.headers.get('content-type') || '', /text\/javascript/);
    }
    assert.equal((await app.request('/service/garage-assets.ts')).status, 404);
    assert.equal((await app.request('/.data/garage-assets.json')).status, 404);
    assert.deepEqual(backend.calls, []);
  } finally { await app.dispose(); }
  assert.deepEqual(backend.calls, ['close']);
});


test('normal gameplay hides Garage toy catalogs and rejects fixture downloads before adapter fetch', async t => {
  const backend = adapterFixture(), app = await fixture(backend.adapter, false); t.after(app.dispose);
  const token = await app.pair();
  const catalog = await app.request('/api/garage/catalog', { token });
  assert.equal(catalog.status, 200);
  assert.deepEqual(catalog.body.packs.map((entry: {id: string}) => entry.id), ['personal-example']);
  assert.equal((await app.request('/api/garage/packs/starter-v2/1', { token })).status, 404);
  assert.deepEqual(backend.calls, ['list']);
  assert.deepEqual((await app.request('/api/catalog')).body, { packs: [] });
  assert.equal((await app.request('/api/packs/starter-v1')).status, 404);
  const health = await app.request('/api/health');
  assert.equal(health.body.capabilities.testFixtures, undefined);
});

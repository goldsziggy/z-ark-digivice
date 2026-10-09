import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash, webcrypto } from 'node:crypto';
import { createServer } from 'node:http';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { mkdtemp, mkdir, readFile, writeFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { createAssetService } from '../service/asset-service.ts';
import { MAX_PACK_BYTES, PACK_IDS, BACKGROUND_PACK_IDS, ALL_PACK_IDS, PUBLIC_KEY_SPKI, inspectPack, signManifest, verifyCatalog, type AssetManifest } from '../service/asset-signing.ts';
import { buildAssetCatalog } from '../scripts/sign-assets.ts';

function tinyPack(packId: string, version = 1) {
  return { formatVersion: 1, packId, version, license: 'CC0-1.0', paletteEncoding: 'rgb565', sprites: { mote: {
    name: 'Mote', family: 'forest', stage: 1, width: 16, height: 16,
    palette: Array.from({ length: 16 }, (_, i) => i * 1000), transparentIndex: 0,
    animations: { idle: { frameMs: 180, frames: [Buffer.alloc(128, 0x12).toString('base64'), Buffer.alloc(128, 0x21).toString('base64')] }, ...Object.fromEntries(['attack', 'hurt', 'sleep', 'care', 'celebrate'].map(name => [name, { frameMs: 180, frames: [Buffer.alloc(128, 0x12).toString('base64')] }])) },
  } }, effects: {}, icons: {} };
}
async function addBackgrounds(packDir: string) {
  for (const id of BACKGROUND_PACK_IDS) await writeFile(join(packDir, `${id}.json`), await readFile(new URL(`../assets/packs/${id}.json`, import.meta.url)));
}
async function fixture(t: { after: (cleanup: () => Promise<void>) => unknown }, backgrounds = false, includeTestFixtures = true) {
  const rootDir = await mkdtemp(join(tmpdir(), 'digivice-assets-test-'));
  const packDir = join(rootDir, 'assets/packs');
  await mkdir(packDir, { recursive: true });
  for (const id of PACK_IDS) await writeFile(join(packDir, `${id}.json`), JSON.stringify(tinyPack(id)));
  if (backgrounds) await addBackgrounds(packDir);
  const signed = buildAssetCatalog(rootDir, undefined, { includeTestFixtures: true });
  const app = createAssetService({ rootDir, includeTestFixtures });
  const server = createServer((request, response) => {
    if (!app.handleAssetRequest(request, response, (request.url ?? '').split('?')[0])) { response.writeHead(404); response.end(); }
  });
  await new Promise<void>((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  t.after(async () => {
    await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    await rm(rootDir, { recursive: true, force: true });
  });
  const base = `http://127.0.0.1:${(server.address() as { port: number }).port}`;
  return { rootDir, packDir, signed, request: (path: string, options: RequestInit = {}) => fetch(`${base}${path}`, options) };
}

test('signed catalog binds exact UTF-8 manifest bytes to bounded, versioned packs', async (t) => {
  const f = await fixture(t);
  const response = await f.request('/api/assets/catalog');
  assert.equal(response.status, 200);
  const envelope = await response.json();
  const manifest = verifyCatalog(envelope);
  assert.equal(manifest.release, 1);
  assert.equal(manifest.rulesVersion, 1);
  assert.deepEqual(manifest.packs.map((pack) => pack.id), [...PACK_IDS]);
  assert.deepEqual(manifest.packs.map((pack) => pack.required), [true, false, false]);
  assert.equal(manifest.packs[0].decodedBytes, 7 * 16 * 16 * 4);
  // Browser Web Crypto consumes IEEE-P1363 directly; DER signatures would fail this.
  const key = await webcrypto.subtle.importKey('spki', Buffer.from(PUBLIC_KEY_SPKI, 'base64'), { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
  assert.equal(await webcrypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, Buffer.from(envelope.signature, 'base64'), Buffer.from(envelope.payloadBase64, 'base64')), true);
  for (const entry of manifest.packs) {
    const response = await f.request(entry.url);
    const bytes = Buffer.from(await response.arrayBuffer());
    assert.equal(response.status, 200);
    assert.equal(response.headers.get('etag'), `"${entry.sha256}"`);
    assert.equal(response.headers.get('accept-ranges'), 'bytes');
    assert.equal(bytes.length, entry.bytes);
    assert.equal(createHash('sha256').update(bytes).digest('hex'), entry.sha256);
    assert.match(response.headers.get('cache-control')!, /immutable/);
  }
  assert.equal(buildAssetCatalog(f.rootDir, undefined, { includeTestFixtures: true }).changed, false, 'identical rebuild retains its signature and catalog ETag');
});

test('resumable downloads honor byte ranges, validators and HEAD without altering bytes', async (t) => {
  const f = await fixture(t);
  const entry = f.signed.manifest.packs[0];
  const etag = `"${entry.sha256}"`;
  const whole = await readFile(join(f.packDir, `${entry.id}.json`));
  const first = await f.request(entry.url, { headers: { range: 'bytes=0-31', 'if-range': etag } });
  assert.equal(first.status, 206);
  assert.equal(first.headers.get('content-range'), `bytes 0-31/${entry.bytes}`);
  assert.equal(first.headers.get('content-length'), '32');
  const remaining = await f.request(entry.url, { headers: { range: 'bytes=32-', 'if-range': etag } });
  assert.equal(remaining.status, 206);
  assert.deepEqual(Buffer.concat([Buffer.from(await first.arrayBuffer()), Buffer.from(await remaining.arrayBuffer())]), whole);
  const suffix = await f.request(entry.url, { headers: { range: 'bytes=-10' } });
  assert.equal(suffix.status, 206);
  assert.deepEqual(Buffer.from(await suffix.arrayBuffer()), whole.subarray(-10));
  const clamp = await f.request(entry.url, { headers: { range: `bytes=${entry.bytes - 5}-${entry.bytes + 100}` } });
  assert.equal(clamp.status, 206);
  assert.equal((await clamp.arrayBuffer()).byteLength, 5);
  for (const value of ['"old-hash"', `W/${etag}`, 'Wed, 21 Oct 2015 07:28:00 GMT']) {
    const stale = await f.request(entry.url, { headers: { range: 'bytes=32-', 'if-range': value } });
    assert.equal(stale.status, 200);
    assert.deepEqual(Buffer.from(await stale.arrayBuffer()), whole);
  }
  const head = await f.request(entry.url, { method: 'HEAD', headers: { range: 'bytes=0-31' } });
  assert.equal(head.status, 200);
  assert.equal(head.headers.get('content-length'), String(entry.bytes));
  assert.equal(head.headers.get('etag'), etag);
  assert.equal((await head.arrayBuffer()).byteLength, 0);
  for (const value of [etag, `W/${etag}`, `"old", ${etag}`, '*']) {
    const unchanged = await f.request(entry.url, { headers: { 'if-none-match': value, range: 'bytes=0-31' } });
    assert.equal(unchanged.status, 304);
    assert.equal((await unchanged.arrayBuffer()).byteLength, 0);
  }
});

test('invalid and multiple ranges, unlisted names, methods and versions are rejected', async (t) => {
  const f = await fixture(t);
  const entry = f.signed.manifest.packs[0];
  for (const range of ['bytes=', 'bytes=-', 'bytes=-0', 'bytes=3-2', `bytes=${entry.bytes}-`, 'bytes=0-1,3-4', 'items=0-1', 'bytes=-1-2', 'bytes=999999999999999999999999999-', 'bytes=0-1;ignored']) {
    const response = await f.request(entry.url, { headers: { range } });
    assert.equal(response.status, 416, range);
    assert.equal(response.headers.get('content-range'), `bytes */${entry.bytes}`);
  }
  for (const path of ['/api/assets/packs/unknown/1', '/api/assets/packs/starter-v2/2', '/api/assets/packs/starter-v2/01', '/api/assets/packs/starter-v2/../catalog.json', '/api/assets/packs/%2e%2e%2fservice/1', `/api/assets/packs/${'x'.repeat(129)}/1`, '/api/assets/catalog.json']) assert.equal((await f.request(path)).status, 404, path);
  const wrongMethod = await f.request(entry.url, { method: 'POST' });
  assert.equal(wrongMethod.status, 405);
  assert.equal(wrongMethod.headers.get('allow'), 'GET, HEAD');
});

test('startup rejects invalid signatures, modified packs and oversized assets; active instance serves its immutable snapshot', async (t) => {
  const f = await fixture(t);
  const entry = f.signed.manifest.packs[0];
  const path = join(f.packDir, `${entry.id}.json`);
  const original = await readFile(path);
  await writeFile(path, `${original.toString()} `);
  assert.throws(() => createAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /integrity failure/);
  const inFlight = await f.request(entry.url, { headers: { range: 'bytes=0-31', 'if-range': `"${entry.sha256}"` } });
  assert.equal(inFlight.status, 206);
  assert.deepEqual(Buffer.from(await inFlight.arrayBuffer()), original.subarray(0, 32));
  await writeFile(path, Buffer.alloc(MAX_PACK_BYTES + 1));
  assert.throws(() => createAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /byte limit/);
  await writeFile(path, original);
  const catalogPath = join(f.packDir, 'catalog.json');
  const invalidSignature = { ...f.signed.catalog, signature: Buffer.alloc(64).toString('base64') };
  await writeFile(catalogPath, JSON.stringify(invalidSignature));
  assert.throws(() => createAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /signature verification failed/);
  const changedPayload = { ...f.signed.catalog, payloadBase64: Buffer.from(JSON.stringify({ ...f.signed.manifest, release: 2 })).toString('base64') };
  assert.throws(() => verifyCatalog(changedPayload), /signature verification failed/);
  assert.throws(() => verifyCatalog({ ...f.signed.catalog, keyId: 'unknown' }), /Unknown asset signing key/);
  await writeFile(catalogPath, 'x'.repeat(16 * 1024 + 1));
  assert.throws(() => createAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /byte limit/);
  await rm(catalogPath);
  assert.throws(() => createAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /ENOENT/);
});

test('release builder refuses mutable version URLs and produces valid bumped releases', async (t) => {
  const f = await fixture(t);
  const path = join(f.packDir, 'tide-v1.json');
  const changed = tinyPack('tide-v1');
  changed.sprites.mote.name = 'Tide Mote';
  await writeFile(path, JSON.stringify(changed));
  assert.throws(() => buildAssetCatalog(f.rootDir, undefined, { includeTestFixtures: true }), /higher manifest release/);
  assert.throws(() => buildAssetCatalog(f.rootDir, 2, { includeTestFixtures: true }), /higher pack version/);
  await writeFile(path, JSON.stringify({ ...changed, version: 2 }));
  const next = buildAssetCatalog(f.rootDir, 2, { includeTestFixtures: true });
  assert.equal(next.manifest.release, 2);
  assert.equal(next.manifest.packs[1].url, '/api/assets/packs/tide-v1/2');
  assert.equal(verifyCatalog(next.catalog).packs[1].version, 2);
  assert.equal(buildAssetCatalog(f.rootDir, undefined, { includeTestFixtures: true }).changed, false, 'default rebuild keeps the current release after an explicit bump');
  assert.doesNotThrow(() => createAssetService({ rootDir: f.rootDir, includeTestFixtures: true }));
  assert.throws(() => signManifest({ ...next.manifest, release: 0 }), /Unsupported asset manifest/);
  const oversized: AssetManifest = { ...next.manifest, packs: next.manifest.packs.map((pack) => ({ ...pack, bytes: MAX_PACK_BYTES + 1 })) };
  assert.throws(() => signManifest(oversized), /out-of-bounds/);
  const unsupported = tinyPack('starter-v2');
  unsupported.sprites.mote.width = 64; unsupported.sprites.mote.height = 64;
  assert.throws(() => inspectPack(unsupported, 'starter-v2'), /dimensions/);
  const tooManyFrames = tinyPack('starter-v2');
  tooManyFrames.sprites.mote.animations.idle.frames = Array(9).fill(Buffer.alloc(128).toString('base64'));
  assert.throws(() => inspectPack(tooManyFrames, 'starter-v2'), /animation resource bounds/);
});

test('public test fixture signing CLI refuses production mode before writing files', () => {
  const result = spawnSync(process.execPath, [fileURLToPath(new URL('../scripts/sign-assets.ts', import.meta.url))], { env: { ...process.env, NODE_ENV: 'production' }, encoding: 'utf8' });
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /disabled in production/);
});

test('backgrounds use the same signed catalog, immutable route and bounded Range transport', async (t) => {
  const f = await fixture(t, true);
  assert.deepEqual(f.signed.manifest.packs.map(entry => entry.id), [...ALL_PACK_IDS]);
  for (const id of BACKGROUND_PACK_IDS) {
    const entry = f.signed.manifest.packs.find(item => item.id === id)!;
    assert.equal(entry.decodedBytes, 921600); assert.equal(entry.required, false);
    const source = await readFile(join(f.packDir, `${id}.json`));
    const response = await f.request(entry.url, { headers: { range: 'bytes=16384-32767', 'if-range': `"${entry.sha256}"` } });
    assert.equal(response.status, 206);
    assert.deepEqual(Buffer.from(await response.arrayBuffer()), source.subarray(16384, 32768));
    const pack = JSON.parse(source.toString('utf8'));
    assert.deepEqual(inspectPack(pack, id), { version: 1, decodedBytes: 921600 });
    pack.background.width = 4096;
    assert.throws(() => inspectPack(pack, id), /dimensions/);
  }
  assert.equal((await f.request('/api/assets/packs/scene-unknown-v1/1')).status, 404);
});

test('three-pack releases upgrade to eleven without changing sprite versions; partial scene sets fail closed', async (t) => {
  const f = await fixture(t);
  const first = BACKGROUND_PACK_IDS[0];
  await writeFile(join(f.packDir, `${first}.json`), await readFile(new URL(`../assets/packs/${first}.json`, import.meta.url)));
  assert.throws(() => buildAssetCatalog(f.rootDir, 2, { includeTestFixtures: true }), /All eight/);
  await addBackgrounds(f.packDir);
  assert.throws(() => buildAssetCatalog(f.rootDir, undefined, { includeTestFixtures: true }), /higher manifest release/);
  const upgraded = buildAssetCatalog(f.rootDir, 2, { includeTestFixtures: true });
  assert.equal(upgraded.manifest.packs.length, 11);
  assert.deepEqual(upgraded.manifest.packs.slice(0, 3), f.signed.manifest.packs);
  assert.equal(verifyCatalog(f.signed.catalog).packs.length, 3, 'old signed cache envelopes remain valid');
  assert.equal(buildAssetCatalog(f.rootDir, undefined, { includeTestFixtures: true }).changed, false);
  assert.doesNotThrow(() => createAssetService({ rootDir: f.rootDir, includeTestFixtures: true }));
});


test('normal asset service exposes scenery only and never loads or serves toy creature fixtures', async t => {
  const f = await fixture(t, true, false);
  const envelope = await (await f.request('/api/assets/catalog')).json();
  const manifest = verifyCatalog(envelope);
  assert.deepEqual(manifest.packs.map(entry => entry.id), [...BACKGROUND_PACK_IDS]);
  for (const id of PACK_IDS) {
    assert.equal((await f.request(`/api/assets/packs/${id}/1`)).status, 404);
    await writeFile(join(f.packDir, `${id}.json`), '{bad fixture');
  }
  assert.doesNotThrow(() => createAssetService({ rootDir: f.rootDir }), 'excluded blobs cannot break normal play');
  const rebuilt = buildAssetCatalog(f.rootDir, 2);
  assert.deepEqual(rebuilt.manifest.packs.map(entry => entry.id), [...BACKGROUND_PACK_IDS]);
  assert.equal(verifyCatalog(rebuilt.catalog).packs.some(entry => entry.required), false);
});

test('a fixture-only legacy catalog becomes an empty signed catalog without synthetic fallback', async t => {
  const f = await fixture(t, false, false);
  const manifest = verifyCatalog(await (await f.request('/api/assets/catalog')).json());
  assert.deepEqual(manifest.packs, []);
  assert.equal((await f.request('/api/assets/packs/starter-v2/1')).status, 404);
});

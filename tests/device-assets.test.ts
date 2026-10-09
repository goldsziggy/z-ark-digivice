import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { cpSync, mkdtempSync, readFileSync, rmSync, symlinkSync, unlinkSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { createDeviceAssetService, DEVICE_LIMITS, validateDeviceManifest, verifyDeviceCatalog, type DeviceManifest } from '../service/device-assets.ts';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const digest = (data: Uint8Array) => createHash('sha256').update(data).digest('hex');
const SPRITE_IDS = ['mote', 'glint', 'lumen', 'flicker', 'rill', 'brine', 'pelagia', 'cinder', 'scoria', 'pyrel'].map(id => `sprite-${id}-v1`);
const SCENE_IDS = ['meadow', 'forest', 'beach', 'ruins', 'cavern', 'snow', 'volcanic', 'digital'].map(id => `scene-${id}-412-v1`);
function syntheticManifest(ids = [...SPRITE_IDS, ...SCENE_IDS]): DeviceManifest {
  return { formatVersion: 1, release: 1, rulesVersion: 1, profile: 's3-146-v1', packs: ids.map(id => {
    const sprite = id.startsWith('sprite-');
    return { id, version: 1, bytes: sprite ? 8816 : 512, sha256: 'a'.repeat(64), kind: sprite ? 'sprite' : 'background',
      width: sprite ? 32 : 412, height: sprite ? 32 : 412, url: `/api/device/assets/packs/${id}/1` };
  }) };
}
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, includeTestFixtures = true) {
  const rootDir = mkdtempSync(join(tmpdir(), 'digivice-device-art-'));
  cpSync(join(ROOT, 'assets/device'), join(rootDir, 'assets/device'), { recursive: true });
  const app = createDeviceAssetService({ rootDir, includeTestFixtures });
  const server = createServer((request, response) => {
    if (!app.handleDeviceAssetRequest(request, response, (request.url ?? '').split('?')[0])) { response.writeHead(404); response.end(); }
  });
  await new Promise<void>((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  t.after(async () => { await new Promise<void>((resolve, reject) => server.close(error => error ? reject(error) : resolve())); rmSync(rootDir, { recursive: true, force: true }); });
  const base = `http://127.0.0.1:${(server.address() as { port: number }).port}`;
  return { rootDir, app, get: (path: string, init?: RequestInit) => fetch(base + path, init) };
}

test('sprite-only catalog signs bounded metadata, serves every advertised asset and excludes scenes', async t => {
  const f = await fixture(t);
  const response = await f.get('/api/device/assets/catalog');
  assert.equal(response.status, 200);
  const text = await response.text(); assert.ok(Buffer.byteLength(text) <= DEVICE_LIMITS.envelopeBytes);
  const envelope = JSON.parse(text), manifest = verifyDeviceCatalog(envelope);
  assert.ok(Buffer.from(envelope.payloadBase64, 'base64').length <= DEVICE_LIMITS.payloadBytes);
  assert.equal(manifest.profile, 's3-146-v1'); assert.equal(manifest.packs.length, 10);
  assert.deepEqual(manifest.packs.map(entry => entry.id).sort(), [...SPRITE_IDS].sort());
  for (const entry of manifest.packs) {
    const fetched = await f.get(entry.url), data = Buffer.from(await fetched.arrayBuffer());
    assert.equal(fetched.status, 200); assert.equal(data.length, entry.bytes); assert.equal(digest(data), entry.sha256);
    assert.equal(fetched.headers.get('etag'), `"${entry.sha256}"`);
    assert.equal(entry.kind, 'sprite'); assert.match(fetched.headers.get('content-type')!, /application\/octet-stream/);
    assert.equal(entry.width, 32); assert.equal(entry.height, entry.width);
    assert.ok(data.length <= DEVICE_LIMITS.blobBytes);
  }
  for (const id of SCENE_IDS) {
    const excluded = await f.get(`/api/device/assets/packs/${id}/1`);
    assert.equal(excluded.status, 404); assert.equal(excluded.headers.get('location'), null);
    assert.match(excluded.headers.get('content-type')!, /json/);
  }
  assert.throws(() => verifyDeviceCatalog({ ...envelope, signature: Buffer.alloc(64).toString('base64') }), /signature/);
  assert.throws(() => verifyDeviceCatalog({ ...envelope, payloadBase64: 'A'.repeat(12000) }), /budget/);
  t.diagnostic(`Device catalog ${Buffer.byteLength(text)} envelope bytes, ${Buffer.from(envelope.payloadBase64, 'base64').length} payload bytes; 10 verified native assets.`);
});

test('manifest metadata permits bounded allowlisted subsets including empty catalogs', () => {
  assert.doesNotThrow(() => validateDeviceManifest(syntheticManifest(SPRITE_IDS)));
  assert.doesNotThrow(() => validateDeviceManifest(syntheticManifest()));
  const allIds = [...SPRITE_IDS, ...SCENE_IDS];
  for (const count of [9, 11, 17]) {
    assert.doesNotThrow(() => validateDeviceManifest(syntheticManifest(allIds.slice(0, count))));
  }
  for (const [label, ids] of [
    ['duplicate sprite', [...SPRITE_IDS.slice(1), SPRITE_IDS[1]]],
    ['unknown sprite', [...SPRITE_IDS.slice(1), 'sprite-unknown-v1']],
  ] as const) {
    assert.throws(() => validateDeviceManifest(syntheticManifest([...ids])), /Device assets:/, label);
  }
});

test('both manifest sets preserve strict shape, profile, hash, byte and entry bounds', () => {
  const invalid: Array<[string, (value: any) => void]> = [
    ['unknown profile', value => { value.profile = 'other'; }],
    ['unknown format', value => { value.formatVersion = 2; }],
    ['unknown rules', value => { value.rulesVersion = 2; }],
    ['invalid release', value => { value.release = 0; }],
    ['extra manifest field', value => { value.extra = true; }],
    ['missing manifest field', value => { delete value.rulesVersion; }],
    ['extra entry field', value => { value.packs[0].file = 'sprite-mote-v1.dva'; }],
    ['missing entry field', value => { delete value.packs[0].sha256; }],
    ['short hash', value => { value.packs[0].sha256 = 'a'.repeat(63); }],
    ['nonhex hash', value => { value.packs[0].sha256 = 'g'.repeat(64); }],
    ['empty bytes', value => { value.packs[0].bytes = 0; }],
    ['fractional bytes', value => { value.packs[0].bytes = 1.5; }],
    ['oversized bytes', value => { value.packs[0].bytes = DEVICE_LIMITS.blobBytes + 1; }],
    ['invalid version', value => { value.packs[0].version = 0; }],
    ['wrong kind', value => { value.packs[0].kind = 'background'; }],
    ['wrong width', value => { value.packs[0].width = 412; }],
    ['wrong height', value => { value.packs[0].height = 31; }],
    ['wrong URL', value => { value.packs[0].url = '/api/device/assets/packs/sprite-mote-v1/2'; }],
  ];
  for (const ids of [SPRITE_IDS, [...SPRITE_IDS, ...SCENE_IDS]]) {
    const boundary = syntheticManifest(ids); boundary.packs[0].bytes = DEVICE_LIMITS.blobBytes;
    assert.doesNotThrow(() => validateDeviceManifest(boundary));
    for (const [label, edit] of invalid) {
      const manifest = syntheticManifest(ids); edit(manifest);
      assert.throws(() => validateDeviceManifest(manifest), /Device assets:/, `${ids.length} entries: ${label}`);
    }
  }
  const wrongSceneShape = syntheticManifest(); wrongSceneShape.packs[10].width = 32; wrongSceneShape.packs[10].height = 32;
  assert.throws(() => validateDeviceManifest(wrongSceneShape), /Device assets:/, 'scenes still require 412-pixel dimensions');
});

test('device transport resumes 4 KiB chunks and rejects malformed paths/ranges without HTML or redirects', async t => {
  const f = await fixture(t), entry = f.app.manifest.packs[0], etag = `"${entry.sha256}"`;
  const parts: Buffer[] = [];
  for (let start = 0; start < entry.bytes; start += 4096) {
    const end = Math.min(start + 4095, entry.bytes - 1);
    const response = await f.get(entry.url, { headers: { Range: `bytes=${start}-${end}`, 'If-Range': etag } });
    assert.equal(response.status, 206); assert.equal(response.headers.get('content-range'), `bytes ${start}-${end}/${entry.bytes}`);
    parts.push(Buffer.from(await response.arrayBuffer()));
  }
  assert.equal(digest(Buffer.concat(parts)), entry.sha256);
  const full = await f.get(entry.url, { headers: { Range: 'bytes=4096-', 'If-Range': '"previous-version"' } });
  assert.equal(full.status, 200); assert.equal((await full.arrayBuffer()).byteLength, entry.bytes);
  const head = await f.get(entry.url, { method: 'HEAD', headers: { Range: 'bytes=0-4095' } });
  assert.equal(head.status, 200); assert.equal(head.headers.get('content-length'), String(entry.bytes)); assert.equal((await head.arrayBuffer()).byteLength, 0);
  assert.equal((await f.get(entry.url, { headers: { 'If-None-Match': etag } })).status, 304);
  for (const range of ['bytes=0-1,3-4', 'bytes=-0', 'bytes=3-2', `bytes=${entry.bytes}-`, 'bytes=999999999999999999999-']) {
    const bad = await f.get(entry.url, { headers: { Range: range } }); assert.equal(bad.status, 416);
    assert.equal(bad.headers.get('content-range'), `bytes */${entry.bytes}`); assert.match(bad.headers.get('content-type')!, /json/);
  }
  for (const path of [entry.url + '?raw=1', '/api/device/assets/packs/unknown/1', '/api/device/assets/packs/sprite-mote-v1/01', '/api/device/assets/packs/sprite-mote-v1/2', '/api/device/assets/packs/%2e%2e%2fsecret/1']) {
    const bad = await f.get(path); assert.equal(bad.status, 404); assert.equal(bad.headers.get('location'), null); assert.match(bad.headers.get('content-type')!, /json/);
  }
  assert.equal((await f.get(entry.url, { method: 'POST' })).status, 405);
});

test('device startup rejects index escapes, unknown formats, changed hashes and corrupt native CRC', async t => {
  const f = await fixture(t), indexPath = join(f.rootDir, 'assets/device/index.json');
  const originalIndex = readFileSync(indexPath), index = JSON.parse(originalIndex.toString('utf8'));
  const entry = index.packs.find((item: { kind: string }) => item.kind === 'sprite'), path = join(f.rootDir, 'assets/device', entry.file), original = readFileSync(path);
  for (const edit of [(value: any) => { value.formatVersion = 2; }, (value: any) => { value.packs[0].file = '../../secret'; },
    (value: any) => { value.packs[1] = value.packs[0]; }, (value: any) => { value.packs[0].bytes = DEVICE_LIMITS.blobBytes + 1; }]) {
    const changed = structuredClone(index); edit(changed); writeFileSync(indexPath, JSON.stringify(changed));
    assert.throws(() => createDeviceAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /Device assets:/);
  }
  writeFileSync(indexPath, originalIndex);
  const corrupt = Buffer.from(original); corrupt[corrupt.length - 1] ^= 1; writeFileSync(path, corrupt);
  assert.throws(() => createDeviceAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /integrity/);
  entry.sha256 = digest(corrupt); writeFileSync(indexPath, JSON.stringify(index));
  assert.throws(() => createDeviceAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /CRC/);
  writeFileSync(path, original); writeFileSync(indexPath, originalIndex);
  unlinkSync(path); symlinkSync(join(ROOT, 'assets/device', entry.file), path);
  assert.throws(() => createDeviceAssetService({ rootDir: f.rootDir, includeTestFixtures: true }), /regular file/);
  const activeEntry = f.app.manifest.packs.find(item => item.id === entry.id)!;
  const immutable = await f.get(activeEntry.url);
  assert.equal(digest(Buffer.from(await immutable.arrayBuffer())), activeEntry.sha256, 'running instance serves startup-verified immutable bytes');
});


test('normal device catalog excludes all toy sprites and remains usable when their files are absent', async t => {
  const f = await fixture(t, false);
  const manifest = verifyDeviceCatalog(await (await f.get('/api/device/assets/catalog')).json());
  assert.equal(manifest.packs.length, 0);
  assert.ok(manifest.packs.every(entry => entry.kind === 'background' && entry.id.startsWith('scene-')));
  const index = JSON.parse(readFileSync(join(f.rootDir, 'assets/device/index.json'), 'utf8'));
  for (const entry of index.packs.filter((item: {kind: string}) => item.kind === 'sprite')) {
    assert.equal((await f.get(`/api/device/assets/packs/${entry.id}/${entry.version}`)).status, 404);
    unlinkSync(join(f.rootDir, 'assets/device', entry.file));
  }
  const restarted = createDeviceAssetService({ rootDir: f.rootDir });
  assert.equal(restarted.manifest.packs.length, 0);
  assert.doesNotThrow(() => validateDeviceManifest({ ...restarted.manifest, packs: [] }));
});

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, unlinkSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { buildGaragePlan, createGarageAssets, GarageError, GARAGE_PREFIX, type GarageTransport } from '../service/garage-assets.ts';
import { BACKGROUND_PACK_IDS, PACK_IDS, signManifest, verifyCatalog } from '../service/asset-signing.ts';

const REPO = resolve(fileURLToPath(new URL('..', import.meta.url)));
function personalFixture(id = 'personal-test') {
  const names = ['idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate'];
  const frame = Buffer.alloc(512, 0x12).toString('base64');
  const pack = { formatVersion: 1, packId: id, version: 1, license: 'LicenseRef-Personal-Use-Restrictions', paletteEncoding: 'rgb565', sprites: { mote: {
    name: 'Test creature', family: 'fixture', stage: 1, width: 32, height: 32, palette: Array.from({ length: 16 }, (_, i) => i * 1000), transparentIndex: 0,
    animations: Object.fromEntries(names.map(name => [name, { frameMs: 180, frames: [frame] }])),
  } }, effects: {}, icons: {} };
  const packText = JSON.stringify(pack);
  const sidecar = { formatVersion: 1, packId: id, version: 1, packBytes: Buffer.byteLength(packText), packSha256: createHash('sha256').update(packText).digest('hex'), decodedBytes: names.length * 32 * 32 * 4,
    provenance: { creator: 'Original automated-test fixture', sourceUrl: 'https://example.invalid/fixture', game: 'Test', rights: 'Synthetic test pixels', reuseScope: 'Local automated tests' },
    coverage: { mote: Object.fromEntries(names.map(name => [name, { kind: 'genuine', originalFrameCount: 1, outputFrameCount: 1, sourceRects: [[0, 0, 32, 32]] }])) },
    sources: [{ path: 'fixture.png', sha256: '0'.repeat(64), bytes: 1, width: 32, height: 32 }],
  };
  return { packText, provenanceText: JSON.stringify(sidecar) };
}
function fixture(t: { after: (fn: () => void) => unknown }) {
  const rootDir = mkdtempSync(join(tmpdir(), 'digivice-garage-test-'));
  mkdirSync(join(rootDir, 'assets/packs'), { recursive: true });
  for (const id of PACK_IDS) cpSync(join(REPO, 'assets/packs', `${id}.json`), join(rootDir, 'assets/packs', `${id}.json`));
  // This mock tests the separately approved original-three publication scope.
  // A future local catalog must not implicitly enlarge that scope.
  const manifest = verifyCatalog(JSON.parse(readFileSync(join(REPO, 'assets/packs/catalog.json'), 'utf8')));
  const catalog = signManifest({ ...manifest, packs: manifest.packs.filter(entry => PACK_IDS.includes(entry.id as typeof PACK_IDS[number])) });
  writeFileSync(join(rootDir, 'assets/packs/catalog.json'), JSON.stringify(catalog));
  const personal = personalFixture();
  mkdirSync(join(rootDir, '.personal-assets/personal-test'), { recursive: true });
  writeFileSync(join(rootDir, '.personal-assets/personal-test/pack.json'), personal.packText);
  writeFileSync(join(rootDir, '.personal-assets/personal-test/provenance.json'), personal.provenanceText);
  writeFileSync(join(rootDir, '.personal-assets/personal-test/raw-secret-image.png'), 'excluded-source');
  t.after(() => rmSync(rootDir, { recursive: true, force: true }));
  return { rootDir, personal, receiptPath: join(rootDir, '.data/garage-assets.json') };
}
class FakeGarage implements GarageTransport {
  visibility: 'enabled' | 'disabled' = 'disabled';
  objects = new Map<string, Buffer>();
  calls: Array<{ method: string; key?: string }> = [];
  putCount = 0;
  async website() { this.calls.push({ method: 'website' }); return this.visibility; }
  async head(_bucket: string, key: string) { this.calls.push({ method: 'head', key }); const data = this.objects.get(key); return data ? { bytes: data.length } : null; }
  async get(_bucket: string, key: string, maximum: number) { this.calls.push({ method: 'get', key }); const data = this.objects.get(key); if (!data) throw new Error('missing'); assert.ok(data.length <= maximum); return Buffer.from(data); }
  async put(_bucket: string, key: string, data: Buffer) { this.calls.push({ method: 'put', key }); assert.equal(this.visibility, 'disabled'); assert.equal(this.objects.has(key), false, 'conditional insertion must not replace an existing object'); this.objects.set(key, Buffer.from(data)); ++this.putCount; }
}
function errorCode(code: string) { return (error: unknown) => error instanceof GarageError && error.code === code; }

test('dry plan includes only signed originals and validated personal pairs, never source images or credentials', async (t) => {
  const f = fixture(t);
  const plan = await buildGaragePlan(f.rootDir);
  assert.equal(plan.objects.length, 6);
  assert.deepEqual(plan.objects.map(item => item.kind), ['catalog', 'original', 'original', 'original', 'personal', 'provenance']);
  assert.ok(plan.objects.every(item => item.key.startsWith(`${GARAGE_PREFIX}/`) && item.key.endsWith(`${item.sha256}.json`)));
  assert.equal(plan.totalBytes, plan.objects.reduce((sum, item) => sum + item.data.length, 0));
  assert.equal(JSON.stringify(plan.objects.map(({ data: _data, ...entry }) => entry)).includes('raw-secret-image'), false);
  assert.equal(existsSync(f.receiptPath), false);
});

test('larger local catalogs cannot silently expand Garage publication or write any objects', async (t) => {
  const f = fixture(t), transport = new FakeGarage();
  // Synthetic metadata is sufficient: scope rejection must precede reading any
  // excluded scenery payload or touching remote object storage.
  const manifest = verifyCatalog(JSON.parse(readFileSync(join(f.rootDir, 'assets/packs/catalog.json'), 'utf8')));
  const expanded = signManifest({ ...manifest, packs: [...manifest.packs, ...BACKGROUND_PACK_IDS.map(id => ({
    id, version: 1, bytes: 1, sha256: '0'.repeat(64), url: `/api/assets/packs/${id}/1`, required: false, decodedBytes: 1,
  }))] });
  writeFileSync(join(f.rootDir, 'assets/packs/catalog.json'), JSON.stringify(expanded));
  await assert.rejects(buildGaragePlan(f.rootDir), errorCode('garage_catalog_scope'));
  const garage = createGarageAssets({ rootDir: f.rootDir, bucket: 'private-fixture', transport });
  await assert.rejects(garage.upload(), errorCode('garage_catalog_scope'));
  assert.equal(transport.putCount, 0);
  assert.equal(transport.calls.filter(call => ['get', 'head', 'put'].includes(call.method)).length, 0);
  assert.equal(existsSync(f.receiptPath), false);
});

test('missing explicit bucket, website-enabled destination or unverified privacy prevents every remote write', async (t) => {
  const f = fixture(t), transport = new FakeGarage();
  const noBucket = createGarageAssets({ rootDir: f.rootDir, transport });
  await assert.rejects(noBucket.upload(), errorCode('garage_bucket_required'));
  assert.equal(transport.calls.length, 0, 'missing explicit bucket must not access configuration or Garage');
  transport.visibility = 'enabled';
  const publicBucket = createGarageAssets({ rootDir: f.rootDir, bucket: 'public-fixture', transport });
  const diagnosis = await publicBucket.diagnose();
  assert.equal(diagnosis.status, 'unavailable'); assert.equal(diagnosis.reason, 'garage_bucket_public');
  await assert.rejects(publicBucket.upload(), errorCode('garage_bucket_public'));
  await assert.rejects(publicBucket.list(), errorCode('garage_bucket_public'));
  transport.website = async () => { throw new Error('AWS_SECRET_ACCESS_KEY=never-display-this-fixture'); };
  await assert.rejects(publicBucket.upload(), error => error instanceof GarageError && error.code === 'garage_privacy_unverified' && !error.message.includes('never-display'));
  assert.equal(transport.putCount, 0);
  assert.equal(existsSync(f.receiptPath), false);
});

test('mock private upload verifies every object, persists an allowlist, and reruns without overwriting', async (t) => {
  const f = fixture(t), transport = new FakeGarage();
  const garage = createGarageAssets({ rootDir: f.rootDir, bucket: 'private-fixture', transport });
  const receipt = await garage.upload();
  assert.equal(receipt.objects.length, 6); assert.equal(transport.putCount, 6);
  assert.deepEqual(JSON.parse(readFileSync(f.receiptPath, 'utf8')), receipt);
  await garage.upload(); assert.equal(transport.putCount, 6);
  const catalog = await garage.list();
  assert.equal(catalog.status, 'available'); assert.equal(catalog.packs.length, 4);
  assert.ok(catalog.packs.every(item => Object.keys(item).sort().join(',') === 'bytes,id,kind,sha256,version'));
  assert.equal(JSON.stringify(catalog).includes('private-fixture'), false);
  const original = await garage.fetchPack('starter-v2', 1);
  assert.equal(original.sha256, createHash('sha256').update(original.bytes).digest('hex'));
  assert.deepEqual(await garage.fetchPersonal('personal-test', 1), f.personal);
  assert.equal((await garage.verify()).objects, 6);
  await assert.rejects(garage.fetchPack('../../models/private', 1), errorCode('garage_not_found'));
  await assert.rejects(garage.fetchPersonal('personal-not-installed', 1), errorCode('garage_not_found'));
  await assert.rejects(garage.fetchPack('starter-v2', 999), errorCode('garage_not_found'));
});

test('hash mismatch, conflicting objects and privacy changes retain the existing receipt and stop transfer', async (t) => {
  const f = fixture(t), transport = new FakeGarage();
  const garage = createGarageAssets({ rootDir: f.rootDir, bucket: 'private-fixture', transport });
  const receipt = await garage.upload();
  const originalReceipt = readFileSync(f.receiptPath, 'utf8');
  const entry = receipt.objects.find(item => item.kind === 'original')!;
  const corrupt = Buffer.from(transport.objects.get(entry.key)!); corrupt[0] ^= 1; transport.objects.set(entry.key, corrupt);
  await assert.rejects(garage.fetchPack(entry.id, entry.version), errorCode('garage_hash_mismatch'));
  await assert.rejects(garage.upload(), errorCode('garage_hash_mismatch'));
  assert.equal(transport.putCount, 6);
  assert.equal(readFileSync(f.receiptPath, 'utf8'), originalReceipt);
  transport.visibility = 'enabled';
  await assert.rejects(garage.fetchPersonal('personal-test', 1), errorCode('garage_bucket_public'));
  await assert.rejects(garage.upload(), errorCode('garage_bucket_public'));
  assert.equal(transport.putCount, 6);
});

test('local receipt cannot redirect downloads outside the Digivice namespace', async (t) => {
  const f = fixture(t), transport = new FakeGarage();
  const garage = createGarageAssets({ rootDir: f.rootDir, bucket: 'private-fixture', transport });
  const receipt = await garage.upload();
  receipt.objects[1].key = 'models/unrelated.glb';
  writeFileSync(f.receiptPath, JSON.stringify(receipt));
  const before = transport.calls.filter(call => call.method === 'get').length;
  await assert.rejects(garage.fetchPack('starter-v2', 1), errorCode('garage_receipt_invalid'));
  assert.equal(transport.calls.filter(call => call.method === 'get').length, before);
});

test('upload planning rejects a symlinked private root and caps total library packs at eight', async (t) => {
  const f = fixture(t);
  const external = mkdtempSync(join(tmpdir(), 'digivice-outside-test-'));
  t.after(() => rmSync(external, { recursive: true, force: true }));
  const personalRoot = join(f.rootDir, '.personal-assets');
  rmSync(personalRoot, { recursive: true }); symlinkSync(external, personalRoot, 'dir');
  await assert.rejects(buildGaragePlan(f.rootDir), errorCode('garage_local_path'));
  unlinkSync(personalRoot); mkdirSync(personalRoot);
  for (let i = 0; i < 6; i++) mkdirSync(join(personalRoot, `personal-fixture-${i}`));
  await assert.rejects(buildGaragePlan(f.rootDir), errorCode('garage_plan_limit'));
});

test('personal JSON rejects invalid UTF-8 locally and after remote checksum verification', async (t) => {
  const f = fixture(t), transport = new FakeGarage();
  const garage = createGarageAssets({ rootDir: f.rootDir, bucket: 'private-fixture', transport });
  const receipt = await garage.upload();
  const provenancePath = join(f.rootDir, '.personal-assets/personal-test/provenance.json');
  const corrupt = Buffer.from(f.personal.provenanceText);
  const offset = corrupt.indexOf('Original automated-test fixture');
  assert.ok(offset > 0); corrupt[offset] = 0xff;
  writeFileSync(provenancePath, corrupt);
  await assert.rejects(buildGaragePlan(f.rootDir), errorCode('garage_invalid_json'));
  const entry = receipt.objects.find(item => item.kind === 'provenance')!;
  const oldHash = entry.sha256;
  entry.sha256 = createHash('sha256').update(corrupt).digest('hex');
  entry.key = entry.key.replace(oldHash, entry.sha256);
  transport.objects.set(entry.key, corrupt);
  writeFileSync(f.receiptPath, JSON.stringify(receipt));
  await assert.rejects(garage.fetchPersonal('personal-test', 1), errorCode('garage_invalid_json'));
});

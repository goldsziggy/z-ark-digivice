import test from 'node:test';
import { readFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
import { generateKeyPairSync, sign, createHash } from 'node:crypto';
import { AssetCache, ASSET_LIMITS, FrameDecoder, decodeFrame, verifyManifest, assetCacheBytes } from '../web/asset-cache.js';
import { MemoryAssetStore } from '../web/asset-store.js';
import { backgroundJpegBytes, BACKGROUND_DECODED_BYTES } from '../web/background-pack.js';

const keys = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const publicKey = keys.publicKey.export({ type: 'spki', format: 'der' }).toString('base64');
const names = ['idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate'];
function fixture(id = 'starter-v2', version = 1, change = 0) {
  const palette = [0, 0xffff, 0xf800, 0x07e0, 0x001f, ...Array(11).fill(0)];
  const sprite = { name: 'Mote', family: 'light', stage: 1, width: 32, height: 32, palette, transparentIndex: 0,
    animations: Object.fromEntries(names.map(name => [name, { frameMs: 100,
      frames: Array.from({ length: 4 }, (_, index) => Buffer.alloc(512, (index + change) % 2 ? 0x12 : 0x01).toString('base64')) }])) };
  const pack = { formatVersion: 1, packId: id, version, license: 'CC0-1.0', paletteEncoding: 'rgb565',
    sprites: { mote: sprite, flicker: structuredClone(sprite) }, effects: {}, icons: {} };
  return fromPack(pack, id === 'starter-v2');
}
function fromPack(pack, required = false) {
  const data = Buffer.from(JSON.stringify(pack));
  const decodedBytes = pack.formatVersion === 3 ? BACKGROUND_DECODED_BYTES : ['sprites', 'effects', 'icons'].reduce((sum, category) => sum + Object.values(pack[category]).reduce((subtotal, sprite) =>
    subtotal + Object.values(sprite.animations).reduce((cost, animation) => cost + animation.frames.length * sprite.width * sprite.height * 4, 0), 0), 0);
  return { pack, data, entry: { id: pack.packId, version: pack.version, bytes: data.length,
    sha256: createHash('sha256').update(data).digest('hex'), url: `/api/assets/packs/${pack.packId}/${pack.version}`, required, decodedBytes } };
}
function catalog(fixtures, release = 1) {
  const payload = Buffer.from(JSON.stringify({ formatVersion: 1, release, rulesVersion: 1, packs: fixtures.map(item => item.entry) }));
  return { keyId: 'digivice-dev-v1', payloadBase64: payload.toString('base64'),
    signature: sign('sha256', payload, { key: keys.privateKey, dsaEncoding: 'ieee-p1363' }).toString('base64') };
}
function server(fixtures, { failAt = -1, transform = data => data, forceFull = false, callback = () => {} } = {}) {
  const calls = [];
  const fetcher = async (url, options = {}) => {
    calls.push({ url, ...options, headers: { ...options.headers } }); callback(calls.length, options);
    if (calls.length === failAt) throw new Error('network interrupted');
    const found = fixtures.find(item => item.entry.url === url); assert.ok(found, url);
    const data = transform(found.data), etag = `"${found.entry.sha256}"`;
    const match = /^bytes=(\d+)-(\d+)$/.exec(options.headers.Range);
    assert.ok(match); const start = Number(match[1]), end = Number(match[2]);
    if (forceFull || (options.headers['If-Range'] && options.headers['If-Range'] !== etag)) {
      return new Response(data, { status: 200, headers: { ETag: etag, 'Content-Length': String(data.length) } });
    }
    const chunk = data.subarray(start, end + 1);
    return new Response(chunk, { status: 206, headers: { ETag: etag, 'Content-Range': `bytes ${start}-${end}/${data.length}`, 'Content-Length': String(chunk.length) } });
  };
  return { calls, fetcher };
}
function cache(storage, fetcher, extra = {}) { return new AssetCache({ storage, fetcher, publicKey, ...extra }); }
function cost(state) {
  return assetCacheBytes(state);
}

test('signed catalog, bounded frame decoding and offline cached startup', async () => {
  const pack = fixture(), envelope = catalog([pack]), storage = new MemoryAssetStore(), http = server([pack]);
  const manifest = await verifyManifest(envelope, publicKey); assert.equal(manifest.packs[0].sha256, pack.entry.sha256);
  const first = cache(storage, http.fetcher); assert.equal(await first.init(), null);
  const active = await first.installPack(envelope, pack.entry.id);
  assert.equal(active.packId, pack.entry.id); assert.ok(http.calls.length >= 2);
  const pixel = decodeFrame(active, 'sprites', 'mote').data;
  assert.deepEqual(Array.from(pixel.slice(0, 8)), [0, 0, 0, 0, 255, 255, 255, 255]);
  const decoder = new FrameDecoder({ maxBytes: 4096 });
  decoder.decode(active, 'sprites', 'mote', 'idle', 0);
  decoder.decode(active, 'sprites', 'mote', 'attack', 1);
  assert.equal(decoder.usedBytes, 4096); assert.equal(decoder.frames.size, 1);
  let requests = 0;
  const offline = cache(storage, async () => { ++requests; throw new Error('offline'); });
  assert.equal((await offline.init()).packId, pack.entry.id); assert.equal(offline.getPack(pack.entry.id).packId, pack.entry.id);
  assert.equal(requests, 0);
});

test('interrupted download resumes committed 16 KiB offset in a new instance', async () => {
  const pack = fixture(), envelope = catalog([pack]), storage = new MemoryAssetStore();
  const interrupted = server([pack], { failAt: 2 });
  await assert.rejects(cache(storage, interrupted.fetcher).installPack(envelope, pack.entry.id), /interrupted/);
  assert.equal((await storage.read()).stage.data.byteLength, ASSET_LIMITS.chunkBytes);
  const resumed = server([pack]);
  const next = cache(storage, resumed.fetcher); await next.init();
  assert.equal((await next.installPack(envelope, pack.entry.id)).packId, pack.entry.id);
  assert.match(resumed.calls[0].headers.Range, /^bytes=16384-/);
  assert.equal(resumed.calls[0].headers['If-Range'], `"${pack.entry.sha256}"`);
  assert.equal((await storage.read()).stage, null);
});

test('stale If-Range full200 replaces partial bytes instead of appending', async () => {
  const pack = fixture(), envelope = catalog([pack]), storage = new MemoryAssetStore();
  await assert.rejects(cache(storage, server([pack], { failAt: 2 }).fetcher).installPack(envelope, pack.entry.id));
  await storage.transaction(state => { state.stage.etag = '"old-etag"'; state.stage.data.fill(0x58); });
  const http = server([pack]);
  const resumed = cache(storage, http.fetcher);
  await resumed.installPack(envelope, pack.entry.id);
  assert.equal(http.calls.length, 1); assert.equal(http.calls[0].headers['If-Range'], '"old-etag"');
  assert.equal(resumed.getActive().packId, pack.entry.id);
});

test('bad signature, hash and signed malformed schema retain active fallback', async () => {
  const starter = fixture(), optional = fixture('tide-v1'), storage = new MemoryAssetStore();
  const envelope = catalog([starter, optional]);
  const first = cache(storage, server([starter, optional]).fetcher);
  await first.installPack(envelope, starter.entry.id);
  const tampered = structuredClone(envelope); const signature = Buffer.from(tampered.signature, 'base64'); signature[0] ^= 1; tampered.signature = signature.toString('base64');
  let requests = 0;
  await assert.rejects(cache(storage, async () => { ++requests; }).installPack(tampered, optional.entry.id), error => error.code === 'signature');
  assert.equal(requests, 0);
  const wrong = server([optional], { transform: data => { const copy = Buffer.from(data); copy[100] ^= 1; return copy; } });
  await assert.rejects(cache(storage, wrong.fetcher).installPack(envelope, optional.entry.id), error => error.code === 'hash');
  assert.equal((await storage.read()).stage, null);
  const malformed = structuredClone(optional.pack); malformed.sprites.mote.palette[0] = '#00ff00';
  const invalid = fromPack(malformed);
  await assert.rejects(cache(storage, server([invalid]).fetcher).installPack(catalog([starter, invalid]), invalid.entry.id), error => error.code === 'schema');
  const recovered = cache(storage, async () => { throw new Error('offline'); });
  assert.equal((await recovered.init()).packId, starter.entry.id);
  assert.deepEqual(recovered.list().map(item => item.id), [starter.entry.id]);
});

test('atomic activation failure retains old active pointer and resumable completed staging', async () => {
  class FaultStore extends MemoryAssetStore {
    fail = false;
    async transaction(update) {
      const draft = structuredClone(this.state), prior = draft.active;
      const result = update(draft);
      if (this.fail && draft.active !== prior) throw new Error('activation I/O failure');
      this.state = draft; return result;
    }
  }
  const starter = fixture(), tide = fixture('tide-v1'), envelope = catalog([starter, tide]), storage = new FaultStore();
  const first = cache(storage, server([starter, tide]).fetcher);
  await first.installPack(envelope, starter.entry.id); storage.fail = true;
  await assert.rejects(first.installPack(envelope, tide.entry.id), /activation I\/O/);
  assert.equal(first.getActive().packId, starter.entry.id);
  const state = await storage.read(); assert.equal(state.active, 'starter-v2@1'); assert.equal(state.stage.data.byteLength, tide.entry.bytes);
  storage.fail = false;
  const next = cache(storage, async () => { throw new Error('no network should be needed'); });
  await next.init(); await next.installPack(envelope, tide.entry.id);
  assert.equal(next.getActive().packId, tide.entry.id); assert.ok(next.getPack(starter.entry.id));
});

test('LRU eviction protects required starter, active pack and staged download', async () => {
  const packs = ['starter-v2', 'ember-v1', 'tide-v1', 'dawn-v1', 'moon-v1'].map(id => fixture(id));
  const envelope = catalog(packs), storage = new MemoryAssetStore(); let clock = 1;
  const first = cache(storage, server(packs).fetcher, { now: () => ++clock });
  for (const pack of packs.slice(0, 4)) await first.installPack(envelope, pack.entry.id);
  const bounded = cache(storage, server(packs).fetcher, { maxBytes: cost(await storage.read()) + 500, now: () => ++clock });
  await bounded.init(); bounded.getPack('ember-v1');
  await bounded.installPack(envelope, 'moon-v1');
  const ids = bounded.list().map(item => item.id).sort();
  assert.deepEqual(ids, ['dawn-v1', 'ember-v1', 'moon-v1', 'starter-v2']);
  assert.equal(bounded.getActive().packId, 'moon-v1');
  const before = await storage.read();
  const noRoom = cache(storage, server(packs).fetcher, { maxBytes: 4096 });
  await assert.rejects(noRoom.installPack(envelope, 'tide-v1'), error => error.code === 'quota');
  assert.deepEqual((await storage.read()).packs.map(item => item.key), before.packs.map(item => item.key));
});

test('signed catalog rollback and same-version replacement are rejected across restarts', async () => {
  const v1 = fixture(), v2 = fixture('starter-v2', 2), replacement = fixture('starter-v2', 2, 1);
  const storage = new MemoryAssetStore();
  await cache(storage, server([v1]).fetcher).installPack(catalog([v1], 1), v1.entry.id);
  await cache(storage, server([v2]).fetcher).installPack(catalog([v2], 2), v2.entry.id);
  const restored = cache(storage, server([v1, replacement]).fetcher); await restored.init();
  await assert.rejects(restored.installPack(catalog([v1], 1), v1.entry.id), error => error.code === 'rollback');
  await assert.rejects(restored.installPack(catalog([v1], 3), v1.entry.id), error => error.code === 'rollback');
  await assert.rejects(restored.installPack(catalog([replacement], 3), replacement.entry.id), error => error.code === 'rollback');
  assert.equal(restored.getActive().version, 2);
});

test('cancellation preserves last durable chunk and required cached art', async () => {
  const starter = fixture(), tide = fixture('tide-v1'), envelope = catalog([starter, tide]), storage = new MemoryAssetStore();
  const first = cache(storage, server([starter, tide]).fetcher); await first.installPack(envelope, starter.entry.id);
  const controller = new AbortController();
  await assert.rejects(first.installPack(envelope, tide.entry.id, { signal: controller.signal, onProgress: progress => {
    if (progress.received === ASSET_LIMITS.chunkBytes) controller.abort();
  } }), error => error.name === 'AbortError');
  assert.equal((await storage.read()).stage.data.length, ASSET_LIMITS.chunkBytes);
  assert.equal(first.getActive().packId, starter.entry.id);
});

test('corrupt active optional cache falls back to verified required art and can be repaired', async () => {
  const starter = fixture(), tide = fixture('tide-v1'), envelope = catalog([starter, tide]), storage = new MemoryAssetStore();
  const first = cache(storage, server([starter, tide]).fetcher);
  await first.installPack(envelope, starter.entry.id); await first.installPack(envelope, tide.entry.id);
  await storage.transaction(state => { state.packs.find(record => record.entry.id === tide.entry.id).data[20] ^= 1; });
  const next = cache(storage, server([tide]).fetcher);
  assert.equal((await next.init()).packId, starter.entry.id); assert.ok(next.warnings.length);
  await next.installPack(envelope, tide.entry.id); assert.equal(next.getActive().packId, tide.entry.id);
});

test('range mismatch, overlarge streamed data and decoded-cost mismatch never activate', async () => {
  const fixturePack = fixture(), envelope = catalog([fixturePack]);
  const mismatch = async () => new Response(fixturePack.data, { status: 206, headers: { ETag: '"x"', 'Content-Range': 'bytes 1-4/9' } });
  await assert.rejects(cache(new MemoryAssetStore(), mismatch).installPack(envelope, fixturePack.entry.id), error => error.code === 'download');
  const oversize = async () => new Response(new Uint8Array(fixturePack.entry.bytes + 1), { status: 200, headers: { ETag: '"x"' } });
  await assert.rejects(cache(new MemoryAssetStore(), oversize).installPack(envelope, fixturePack.entry.id), error => error.code === 'download');
  const wrongCost = { ...fixturePack, entry: { ...fixturePack.entry, decodedBytes: 4 } };
  await assert.rejects(cache(new MemoryAssetStore(), server([fixturePack]).fetcher).installPack(catalog([wrongCost]), fixturePack.entry.id), error => error.code === 'schema');
});


test('actual signed sprite and eight-scene catalog installs within full cache accounting', async (t) => {
  const root = new URL('../assets/packs/', import.meta.url);
  const envelope = JSON.parse(await readFile(new URL('catalog.json', root), 'utf8'));
  const manifest = await verifyManifest(envelope);
  const fixtures = await Promise.all(manifest.packs.map(async entry => ({ entry, data: await readFile(new URL(`${entry.id}.json`, root)) })));
  const storage = new MemoryAssetStore(), http = server(fixtures);
  const actual = new AssetCache({ storage, fetcher: http.fetcher });
  for (const entry of manifest.packs) await actual.installPack(envelope, entry.id);
  assert.equal(actual.list().length, 11); assert.ok(actual.getPack('starter-v2'));
  assert.equal(actual.getActive().packId, 'ember-v1');
  assert.equal(actual.getActiveBackground().packId, 'scene-digital-v1');
  const decoder = new FrameDecoder();
  for (const entry of manifest.packs) {
    const pack = actual.getPack(entry.id);
    if (pack.formatVersion === 3) { assert.ok(backgroundJpegBytes(pack).length > 0); continue; }
    for (const [id, sprite] of Object.entries(pack.sprites)) {
      for (const [animation, spec] of Object.entries(sprite.animations)) {
        for (let index = 0; index < spec.frames.length; ++index) {
          const frame = decoder.decode(pack, 'sprites', id, animation, index);
          assert.equal(frame.data.length, sprite.width * sprite.height * 4);
          assert.ok(decoder.usedBytes <= ASSET_LIMITS.decodedCacheBytes);
        }
      }
    }
  }
  const complete = cost(await storage.read());
  const largest = fixtures.reduce((a, b) => a.entry.bytes > b.entry.bytes ? a : b);
  const withLargestStage = complete + largest.entry.bytes + Buffer.byteLength(JSON.stringify(envelope)) + 512;
  assert.ok(withLargestStage < ASSET_LIMITS.cacheBytes, 'complete set plus largest replacement stage includes signature and metadata overhead');
  await actual.installPack(envelope, largest.entry.id); // Protect the old scene during its replacement.
  const replacementPack = structuredClone(actual.getPack(largest.entry.id)); ++replacementPack.version;
  const replacement = fromPack(replacementPack);
  const { signManifest } = await import('../service/asset-signing.ts');
  const upgradeEnvelope = signManifest({ ...manifest, release: manifest.release + 1,
    packs: manifest.packs.map(entry => entry.id === replacement.entry.id ? replacement.entry : entry) });
  const upgrade = new AssetCache({ storage, fetcher: server([replacement]).fetcher }); await upgrade.init();
  let peak = complete;
  await upgrade.installPack(upgradeEnvelope, replacement.entry.id, { onProgress: () => {
    peak = Math.max(peak, cost(storage.state));
    assert.ok(storage.state.packs.some(record => record.entry.id === 'starter-v2'));
    assert.ok(storage.state.packs.some(record => record.key === `${largest.entry.id}@1`), 'previous active scene survives staging');
  } });
  assert.equal(upgrade.list().length, 11, 'largest replacement needs no optional eviction');
  assert.equal(upgrade.getActive().packId, 'ember-v1');
  assert.equal(upgrade.getActiveBackground().version, 2);
  assert.ok(peak <= ASSET_LIMITS.cacheBytes);
  t.diagnostic(`BACKGROUND_CACHE_BUDGET ${JSON.stringify({ release: manifest.release, packs: 11, payloadBytes: fixtures.reduce((sum, item) => sum + item.entry.bytes, 0),
    completeAccountedBytes: complete, largestStageAccountedBytes: withLargestStage, actualReplacementPeakBytes: peak,
    limitBytes: ASSET_LIMITS.cacheBytes, headroomBytes: ASSET_LIMITS.cacheBytes - peak, optionalEvictions: 0 })}`);
  const invalid = structuredClone(actual.getPack('starter-v2'));
  invalid.sprites.mote.width = 1000000000;
  assert.throws(() => decodeFrame(invalid, 'sprites', 'mote'), error => error.code === 'frame');
});


test('finite header and stream deadlines preserve committed chunks and allow retry', async () => {
  const starter = fixture(), tide = fixture('tide-v1'), envelope = catalog([starter, tide]);
  const storage = new MemoryAssetStore(), normal = server([starter, tide]);
  const first = cache(storage, normal.fetcher); await first.installPack(envelope, starter.entry.id);
  const stalledHeaders = cache(storage, async (_url, { signal }) => new Promise((_resolve, reject) => {
    signal.addEventListener('abort', () => reject(new DOMException('Timed out', 'AbortError')), { once: true });
  }), { requestTimeoutMs: 10 });
  await assert.rejects(stalledHeaders.installPack(envelope, tide.entry.id), error => error.name === 'AbortError');
  assert.equal(stalledHeaders.installing, false);
  let calls = 0, stall = true;
  const stalledStream = cache(storage, async (url, options) => {
    ++calls;
    if (calls === 2 && stall) {
      const range = options.headers.Range.slice('bytes='.length);
      return new Response(new ReadableStream({ start() {} }), { status: 206, headers: {
        ETag: `"${tide.entry.sha256}"`, 'Content-Range': `bytes ${range}/${tide.entry.bytes}`,
      } });
    }
    return normal.fetcher(url, options);
  }, { requestTimeoutMs: 10 });
  await stalledStream.init();
  await assert.rejects(stalledStream.installPack(envelope, tide.entry.id), error => error.name === 'AbortError');
  assert.equal(stalledStream.getActive().packId, starter.entry.id);
  assert.equal((await storage.read()).stage.data.byteLength, ASSET_LIMITS.chunkBytes);
  assert.equal(stalledStream.installing, false);
  stall = false;
  await stalledStream.installPack(envelope, tide.entry.id);
  assert.equal(stalledStream.getActive().packId, tide.entry.id);
});


test('native-style fetch receives its global receiver, not the cache instance', async () => {
  const pack = fixture(), http = server([pack]);
  const fetcher = function (...args) { assert.equal(this, globalThis); return http.fetcher(...args); };
  await cache(new MemoryAssetStore(), fetcher).installPack(catalog([pack]), pack.entry.id);
});

async function backgroundFixture(scene = 'meadow') {
  return fromPack(JSON.parse(await readFile(new URL(`../assets/packs/scene-${scene}-v1.json`, import.meta.url), 'utf8')));
}

test('background JPEG validation rejects foreign types, unsupported headers, dimensions and provenance before decode', async () => {
  const { pack } = await backgroundFixture();
  const jpeg = backgroundJpegBytes(pack);
  assert.equal(jpeg.length, Buffer.from(pack.background.data, 'base64').length);
  const replace = data => { const value = structuredClone(pack); value.background.data = Buffer.from(data).toString('base64'); return value; };
  for (const bad of [Buffer.from('<svg width="480" height="480"></svg>'), Buffer.from('GIF89a'), jpeg.slice(0, -1),
    Buffer.concat([jpeg, Buffer.from([0])]), new Uint8Array(150 * 1024 + 1)]) {
    assert.throws(() => backgroundJpegBytes(replace(bad)), /Invalid background/);
  }
  const frameOffset = Buffer.from(jpeg).indexOf(Buffer.from([0xff, 0xc0]));
  assert.ok(frameOffset > 0);
  const wrongWidth = jpeg.slice(); wrongWidth[frameOffset + 8] = 0xff;
  assert.throws(() => backgroundJpegBytes(replace(wrongWidth)), /480/);
  const progressive = jpeg.slice(); progressive[frameOffset + 1] = 0xc2;
  assert.throws(() => backgroundJpegBytes(replace(progressive)), /progressive/);
  const truncatedSegment = jpeg.slice(); truncatedSegment[4] = 0xff; truncatedSegment[5] = 0xff;
  assert.throws(() => backgroundJpegBytes(replace(truncatedSegment.subarray(0, 64))), /segment length/);
  const incorrect = structuredClone(pack); incorrect.provenance.sourceSha256 = 'unknown';
  assert.throws(() => backgroundJpegBytes(incorrect), /provenance/);
  const unknown = structuredClone(pack); unknown.background.sceneId = 'remote';
  assert.throws(() => backgroundJpegBytes(unknown), /identity/);
});

test('old cache upgrades additively; interrupted background resumes without changing sprite or prior scene', async () => {
  const starter = fixture(), meadow = await backgroundFixture(), forest = await backgroundFixture('forest');
  const storage = new MemoryAssetStore(), oldCatalog = catalog([starter], 1);
  const first = cache(storage, server([starter]).fetcher); await first.installPack(oldCatalog, starter.entry.id);
  await storage.transaction(state => { delete state.backgroundActive; }); // Existing schema-1 IndexedDB record.
  const envelope = catalog([starter, meadow, forest], 2);
  const upgraded = cache(storage, server([meadow]).fetcher); await upgraded.init();
  assert.equal(upgraded.getActiveBackground(), null);
  await upgraded.installPack(envelope, meadow.entry.id);
  assert.equal(upgraded.getActive().packId, starter.entry.id);
  assert.equal(upgraded.getActiveBackground().packId, meadow.entry.id);
  const interrupted = cache(storage, server([forest], { failAt: 2 }).fetcher); await interrupted.init();
  await assert.rejects(interrupted.installPack(envelope, forest.entry.id), /interrupted/);
  assert.equal(interrupted.getActiveBackground().packId, meadow.entry.id);
  const http = server([forest]);
  const restarted = cache(storage, http.fetcher); await restarted.init();
  assert.equal(restarted.getActiveBackground().packId, meadow.entry.id);
  await restarted.installPack(envelope, forest.entry.id);
  assert.match(http.calls[0].headers.Range, /^bytes=16384-/);
  assert.equal(http.calls[0].headers['If-Range'], `"${forest.entry.sha256}"`);
  assert.equal(restarted.getActiveBackground().packId, forest.entry.id);
  assert.equal(restarted.getActive().packId, starter.entry.id);
  const offline = cache(storage, async () => { throw new Error('offline'); }); await offline.init();
  assert.equal(offline.getActiveBackground().packId, forest.entry.id);
  assert.equal(offline.getPack(meadow.entry.id).formatVersion, 3);
  assert.equal(Object.isFrozen(offline.getActiveBackground().background), true);
  await assert.rejects(offline.installPack(oldCatalog, starter.entry.id), error => error.code === 'rollback');
});

test('bad signed background and tampered download retain prior scene; corrupt cached scene never decodes', async () => {
  const starter = fixture(), meadow = await backgroundFixture(), forest = await backgroundFixture('forest');
  const envelope = catalog([starter, meadow, forest], 2), storage = new MemoryAssetStore();
  const first = cache(storage, server([starter, meadow]).fetcher);
  await first.installPack(envelope, starter.entry.id); await first.installPack(envelope, meadow.entry.id);
  const malformed = structuredClone(forest.pack); malformed.background.width = 999999;
  const badPack = fromPack(malformed), signedBad = catalog([starter, meadow, badPack], 3);
  const parserFailure = cache(storage, server([badPack]).fetcher); await parserFailure.init();
  await assert.rejects(parserFailure.installPack(signedBad, badPack.entry.id), error => error.code === 'schema');
  assert.equal(parserFailure.getActiveBackground().packId, meadow.entry.id);
  assert.equal((await storage.read()).stage, null);
  const hashFailure = cache(storage, server([forest], { transform: data => { const copy = Buffer.from(data); copy[500] ^= 1; return copy; } }).fetcher);
  await assert.rejects(hashFailure.installPack(envelope, forest.entry.id), error => error.code === 'hash');
  assert.equal(hashFailure.getActiveBackground().packId, meadow.entry.id);
  await storage.transaction(state => { state.packs.find(record => record.entry.id === meadow.entry.id).data[500] ^= 1; });
  const reload = cache(storage, async () => { throw new Error('offline'); }); await reload.init();
  assert.equal(reload.getActiveBackground(), null);
  assert.equal(reload.getActive().packId, starter.entry.id);
  assert.ok(reload.warnings.length);
  await storage.transaction(state => { state.backgroundActive = 'x'.repeat(2000); });
  await assert.rejects(reload.init(), error => error.code === 'storage');
});


test('release art registry omits toy builtins and ignores toy packs retained in an old browser cache', async () => {
  const { default: builtin } = await import('../web/builtin-pack.js');
  const { visibleAssetPacks } = await import('../web/asset-library.js');
  assert.deepEqual(Object.keys(builtin.sprites), []);
  const stored = { 'starter-v1': {}, 'starter-v2': {}, 'tide-v1': {}, 'ember-v1': {}, 'scene-meadow-v1': { scene: true } };
  assert.deepEqual(Object.keys(visibleAssetPacks(stored)), ['scene-meadow-v1']);
  assert.deepEqual(visibleAssetPacks(stored, { includeTestFixtures: true }), stored);
  assert.equal(Object.keys(stored).length, 5, 'filter does not erase old cache data');
});

import { ASSET_PUBLIC_KEY } from './asset-public-key.js';
import { backgroundJpegBytes, BACKGROUND_DECODED_BYTES } from './background-pack.js';

export const ASSET_LIMITS = Object.freeze({ packBytes: 256 * 1024, cacheBytes: 1024 * 1024,
  chunkBytes: 16 * 1024, manifestBytes: 16 * 1024, decodedPackBytes: 2 * 1024 * 1024,
  decodedFrameBytes: 32 * 32 * 4, decodedCacheBytes: 64 * 1024 });
const ENCODER = new TextEncoder();
const DECODER = new TextDecoder('utf-8', { fatal: true });
const ID = /^(?!constructor$|prototype$)[a-z][a-z0-9-]{0,47}$/;
const ANIMATIONS = ['idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate'];

export class AssetError extends Error {
  constructor(code, message) { super(message); this.name = 'AssetError'; this.code = code; }
}
function assert(value, code, message) { if (!value) throw new AssetError(code, message); }
function integer(value, min, max) { return Number.isSafeInteger(value) && value >= min && value <= max; }
function object(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function exactKeys(value, keys) { return object(value) && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key)); }
function freeze(value) { if (value && typeof value === 'object') { Object.freeze(value); for (const child of Object.values(value)) freeze(child); } return value; }
function aborted(signal) { if (signal?.aborted) throw new DOMException('Asset download cancelled', 'AbortError'); }
function keyFor(entry) { return `${entry.id}@${entry.version}`; }
function sameEntry(a, b) { return a?.id === b.id && a.version === b.version && a.sha256 === b.sha256 && a.bytes === b.bytes; }
function bytes(value) { return value instanceof Uint8Array; }
function base64(value, maximum) {
  assert(typeof value === 'string' && value.length <= Math.ceil(maximum / 3) * 4 &&
    /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value), 'encoding', 'Invalid bounded base64');
  let binary;
  try { binary = atob(value); } catch { throw new AssetError('encoding', 'Invalid base64'); }
  assert(binary.length <= maximum && btoa(binary) === value, 'encoding', 'Noncanonical base64');
  return Uint8Array.from(binary, character => character.charCodeAt(0));
}
export async function sha256(data) {
  const hash = new Uint8Array(await crypto.subtle.digest('SHA-256', data));
  return Array.from(hash, byte => byte.toString(16).padStart(2, '0')).join('');
}
export async function verifyManifest(envelope, publicKey = ASSET_PUBLIC_KEY) {
  assert(exactKeys(envelope, ['keyId', 'payloadBase64', 'signature']) && envelope.keyId === 'digivice-dev-v1', 'signature', 'Unknown asset signing key');
  const payload = base64(envelope.payloadBase64, ASSET_LIMITS.manifestBytes);
  const signature = base64(envelope.signature, 64);
  assert(signature.length === 64, 'signature', 'Expected a 64-byte P-256 signature');
  let key;
  try {
    key = await crypto.subtle.importKey('spki', base64(publicKey, 256), { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
    assert(await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, signature, payload), 'signature', 'Asset manifest signature rejected');
  } catch (error) { if (error instanceof AssetError) throw error; throw new AssetError('signature', 'Asset manifest verification failed'); }
  let manifest;
  try { manifest = JSON.parse(DECODER.decode(payload)); } catch { throw new AssetError('manifest', 'Invalid UTF-8 manifest JSON'); }
  assert(exactKeys(manifest, ['formatVersion', 'release', 'rulesVersion', 'packs']) && manifest.formatVersion === 1 &&
    manifest.rulesVersion === 1 && integer(manifest.release, 1, Number.MAX_SAFE_INTEGER) &&
    Array.isArray(manifest.packs) && manifest.packs.length <= 16, 'manifest', 'Unsupported or oversized asset manifest');
  const seen = new Set();
  for (const entry of manifest.packs) {
    assert(exactKeys(entry, ['id', 'version', 'bytes', 'sha256', 'url', 'required', 'decodedBytes']) && ID.test(entry.id) &&
      integer(entry.version, 1, Number.MAX_SAFE_INTEGER) && integer(entry.bytes, 1, ASSET_LIMITS.packBytes) &&
      typeof entry.sha256 === 'string' && /^[a-f0-9]{64}$/.test(entry.sha256) && typeof entry.required === 'boolean' &&
      integer(entry.decodedBytes, 1, ASSET_LIMITS.decodedPackBytes) &&
      entry.url === `/api/assets/packs/${entry.id}/${entry.version}` && !seen.has(entry.id), 'manifest', 'Invalid or duplicate asset entry');
    seen.add(entry.id);
  }
  return freeze(manifest);
}

// Hash verification happens BEFORE JSON parsing in AssetCache. This validator
// inspects packed pixels, never eagerly expands all animation frames to RGBA.
function validatePackSchema(pack, entry, dimensions) {
  assert(exactKeys(pack, ['formatVersion', 'packId', 'version', 'license', 'paletteEncoding', 'sprites', 'effects', 'icons']) &&
    typeof pack.license === 'string' && pack.license.length <= 128 && pack.formatVersion === 1 && pack.packId === entry.id && pack.version === entry.version &&
    pack.paletteEncoding === 'rgb565', 'schema', 'Unsupported asset pack identity or format');
  let decodedBytes = 0, assetCount = 0;
  for (const category of ['sprites', 'effects', 'icons']) {
    const items = pack[category];
    assert(object(items) && Object.keys(items).length <= 32, 'schema', 'Invalid asset category');
    for (const [id, sprite] of Object.entries(items)) {
      ++assetCount;
      assert(ID.test(id) && exactKeys(sprite, ['name', 'family', 'stage', 'width', 'height', 'palette', 'transparentIndex', 'animations']) &&
        typeof sprite.name === 'string' && sprite.name.length > 0 && sprite.name.length <= 64 &&
        typeof sprite.family === 'string' && sprite.family.length > 0 && sprite.family.length <= 48 && integer(sprite.stage, 0, 16) &&
        dimensions.includes(sprite.width) && sprite.height === sprite.width &&
        Array.isArray(sprite.palette) && sprite.palette.length === 16 && sprite.palette.every(color => integer(color, 0, 65535)) &&
        sprite.transparentIndex === 0 && object(sprite.animations), 'schema', 'Invalid dimensions or RGB565 palette');
      const animations = Object.entries(sprite.animations);
      assert(animations.length >= 1 && animations.length <= 6 && Object.hasOwn(sprite.animations, 'idle') &&
        (category !== 'sprites' || ANIMATIONS.every(name => Object.hasOwn(sprite.animations, name))), 'schema', 'Missing required animation');
      for (const [name, animation] of animations) {
        assert(ANIMATIONS.includes(name) && exactKeys(animation, ['frameMs', 'frames']) && integer(animation.frameMs, 40, 2000) &&
          Array.isArray(animation.frames) && animation.frames.length >= 1 && animation.frames.length <= 8, 'schema', 'Invalid animation bounds');
        const frameBytes = sprite.width * sprite.height / 2;
        for (const frame of animation.frames) assert(base64(frame, frameBytes).length === frameBytes, 'schema', 'Packed frame dimensions disagree');
        decodedBytes += animation.frames.length * sprite.width * sprite.height * 4;
        assert(decodedBytes <= ASSET_LIMITS.decodedPackBytes, 'schema', 'Decoded pack exceeds memory budget');
      }
    }
  }
  assert(assetCount > 0 && decodedBytes === entry.decodedBytes, 'schema', 'Decoded memory cost does not match signed manifest');
  return freeze(pack);
}

export function validatePackedPack(pack, entry) {
  if (pack?.formatVersion === 3) {
    assert(pack.packId === entry.id && pack.version === entry.version && !entry.required &&
      entry.decodedBytes === BACKGROUND_DECODED_BYTES, 'schema', 'Invalid signed background identity or decoded budget');
    try { backgroundJpegBytes(pack); } catch (error) { throw new AssetError('schema', error.message); }
    return freeze(pack);
  }
  assert(!entry.id.startsWith('scene-'), 'schema', 'Scene identity requires background format');
  return validatePackSchema(pack, entry, [16, 32]);
}
// Explicit opt-in schema for user-selected local art; never used by signed downloads.
export function validatePersonalPackSchema(pack, entry) {
  return validatePackSchema(pack, entry, [32, 64]);
}

function decodeBoundedFrame(pack, category, id, animation, index, personal) {
  assert(['sprites', 'effects', 'icons'].includes(category) && Object.hasOwn(pack[category] || {}, id), 'frame', 'Unknown asset');
  const sprite = pack[category][id];
  assert(object(sprite) && (personal ? [32, 64] : [16, 32]).includes(sprite.width) && sprite.height === sprite.width &&
    Array.isArray(sprite.palette) && sprite.palette.length === 16 && sprite.palette.every(color => integer(color, 0, 65535)) &&
    sprite.transparentIndex === 0 && object(sprite.animations), 'frame', 'Unvalidated sprite cannot be decoded');
  const sequence = sprite.animations[animation];
  assert(sequence && Array.isArray(sequence.frames) && sequence.frames.length <= 8 && integer(index, 0, sequence.frames.length - 1), 'frame', 'Unknown animation frame');
  const packed = base64(sequence.frames[index], sprite.width * sprite.height / 2);
  const decodedBytes = sprite.width * sprite.height * 4;
  assert(decodedBytes <= (personal ? 16384 : ASSET_LIMITS.decodedFrameBytes), 'frame', 'Frame exceeds memory budget');
  assert(packed.length === sprite.width * sprite.height / 2, 'frame', 'Packed frame is incomplete');
  const rgba = new Uint8ClampedArray(decodedBytes);
  for (let pixel = 0; pixel < sprite.width * sprite.height; ++pixel) {
    const colorIndex = pixel % 2 === 0 ? packed[pixel >> 1] >> 4 : packed[pixel >> 1] & 15;
    const color = sprite.palette[colorIndex];
    const offset = pixel * 4;
    rgba[offset] = Math.round(((color >> 11) & 31) * 255 / 31);
    rgba[offset + 1] = Math.round(((color >> 5) & 63) * 255 / 63);
    rgba[offset + 2] = Math.round((color & 31) * 255 / 31);
    rgba[offset + 3] = colorIndex === sprite.transparentIndex ? 0 : 255;
  }
  return { width: sprite.width, height: sprite.height, data: rgba };
}
export function decodeFrame(pack, category, id, animation = 'idle', index = 0) {
  return decodeBoundedFrame(pack, category, id, animation, index, false);
}
export function decodePersonalFrame(pack, category, id, animation = 'idle', index = 0) {
  return decodeBoundedFrame(pack, category, id, animation, index, true);
}
export class FrameDecoder {
  constructor({ maxBytes = ASSET_LIMITS.decodedCacheBytes } = {}) {
    assert(integer(maxBytes, ASSET_LIMITS.decodedFrameBytes, ASSET_LIMITS.decodedCacheBytes), 'quota', 'Invalid decoded frame budget');
    this.maxBytes = maxBytes; this.usedBytes = 0; this.frames = new Map(); this.packIds = new WeakMap(); this.nextId = 0;
  }
  decode(pack, category, id, animation = 'idle', index = 0) {
    if (!this.packIds.has(pack)) this.packIds.set(pack, ++this.nextId);
    const key = `${this.packIds.get(pack)}:${category}:${id}:${animation}:${index}`;
    let frame = this.frames.get(key);
    if (frame) { this.frames.delete(key); this.frames.set(key, frame); return frame; }
    frame = decodeFrame(pack, category, id, animation, index);
    while (this.usedBytes + frame.data.byteLength > this.maxBytes) {
      const oldest = this.frames.keys().next().value;
      this.usedBytes -= this.frames.get(oldest).data.byteLength; this.frames.delete(oldest);
    }
    this.frames.set(key, frame); this.usedBytes += frame.data.byteLength; return frame;
  }
  clear() { this.frames.clear(); this.usedBytes = 0; }
}

function checkState(state) {
  const pointer = value => value === null || (typeof value === 'string' && /^[a-z][a-z0-9-]{0,47}@[1-9]\d{0,15}$/.test(value));
  assert(object(state) && state.schema === 1 && integer(state.revision, 0, Number.MAX_SAFE_INTEGER) &&
    pointer(state.active) && (state.backgroundActive === undefined || pointer(state.backgroundActive)) && Array.isArray(state.packs) && state.packs.length <= 64 &&
    object(state.highWater) && integer(state.highWater.release, 0, Number.MAX_SAFE_INTEGER) &&
    Array.isArray(state.highWater.versions) && state.highWater.versions.length <= 64, 'storage', 'Unsupported asset storage; preserved for recovery');
  for (const item of state.highWater.versions) assert(object(item) && ID.test(item.id) && integer(item.version, 1, Number.MAX_SAFE_INTEGER) &&
    /^[a-f0-9]{64}$/.test(item.sha256), 'storage', 'Invalid stored asset version boundary');
}
function recordCost(record) { return record.data.byteLength + ENCODER.encode(JSON.stringify(record.envelope)).byteLength + 512; }
function usedBytes(state) {
  return 2048 + state.highWater.versions.length * 160 + state.packs.reduce((sum, record) => sum + recordCost(record), 0) +
    (state.stage ? recordCost(state.stage) : 0);
}
// Deterministic storage accounting used by eviction and the device simulator.
// It includes compact signed envelopes and conservative record/index overhead;
// it does not claim to measure the browser's engine-specific disk allocation.
export function assetCacheBytes(state) { checkState(state); return usedBytes(state); }
function makeRoom(state, maximum, accesses = new Map()) {
  for (const record of state.packs) record.lastUsed = Math.max(record.lastUsed, accesses.get(record.entry.id) || 0);
  while (usedBytes(state) > maximum) {
    const candidates = state.packs.filter(record => !record.entry.required && record.entry.id !== 'starter-v2' && record.key !== state.active && record.key !== state.backgroundActive && record.key !== state.stage?.key);
    candidates.sort((a, b) => a.lastUsed - b.lastUsed);
    assert(candidates.length, 'quota', 'Asset cache full; required and active packs were retained');
    state.packs.splice(state.packs.indexOf(candidates[0]), 1);
  }
}
function rollbackCheck(state, manifest, entry) {
  assert(manifest.release >= state.highWater.release, 'rollback', 'Older signed catalog rejected');
  const highest = state.highWater.versions.find(item => item.id === entry.id);
  assert(!highest || entry.version > highest.version || (entry.version === highest.version && entry.sha256 === highest.sha256),
    'rollback', 'Asset rollback or same-version replacement rejected');
}
function touchVersion(state, manifest, entry) {
  state.highWater.release = Math.max(state.highWater.release, manifest.release);
  const prior = state.highWater.versions.find(item => item.id === entry.id);
  if (prior) { prior.version = entry.version; prior.sha256 = entry.sha256; }
  else { assert(state.highWater.versions.length < 64, 'quota', 'Asset version history is full'); state.highWater.versions.push({ id: entry.id, version: entry.version, sha256: entry.sha256 }); }
}
async function readBounded(response, maximum, signal) {
  const announced = response.headers.get('Content-Length');
  assert(announced === null || (/^\d+$/.test(announced) && Number(announced) <= maximum), 'download', 'Response exceeds signed byte budget');
  assert(response.body?.getReader, 'download', 'Streaming asset response required');
  const reader = response.body.getReader(), chunks = [];
  let length = 0;
  const onAbort = () => { reader.cancel().catch(() => {}); };
  signal?.addEventListener('abort', onAbort, { once: true });
  try {
    for (;;) {
      aborted(signal);
      const next = await reader.read();
      if (next.done) break;
      length += next.value.byteLength;
      assert(length <= maximum, 'download', 'Response exceeds signed byte budget');
      chunks.push(next.value);
    }
    aborted(signal);
  } catch (error) { await reader.cancel().catch(() => {}); throw error; }
  finally { signal?.removeEventListener('abort', onAbort); reader.releaseLock(); }
  const result = new Uint8Array(length); let offset = 0;
  for (const chunk of chunks) { result.set(chunk, offset); offset += chunk.byteLength; }
  return result;
}

export class AssetCache {
  constructor({ storage, fetcher = globalThis.fetch, publicKey = ASSET_PUBLIC_KEY, maxBytes = ASSET_LIMITS.cacheBytes,
    now = () => Date.now(), requestTimeoutMs = 8000 } = {}) {
    assert(storage?.read && storage?.transaction && typeof fetcher === 'function', 'config', 'Asset storage and fetcher required');
    assert(integer(maxBytes, 4096, ASSET_LIMITS.cacheBytes), 'quota', 'Invalid asset cache budget');
    assert(integer(requestTimeoutMs, 10, 30000), 'config', 'Request timeout must be between 10 and 30000 ms');
    this.storage = storage; this.fetcher = (...args) => fetcher.call(globalThis, ...args); this.publicKey = publicKey; this.maxBytes = maxBytes; this.now = now;
    this.requestTimeoutMs = requestTimeoutMs;
    this.active = null; this.background = null; this.packs = new Map(); this.accesses = new Map(); this.records = []; this.warnings = []; this.installing = false;
  }
  async verifyRecord(record) {
    assert(object(record) && bytes(record.data) && record.data.byteLength <= ASSET_LIMITS.packBytes, 'storage', 'Invalid cached asset bytes');
    const manifest = await verifyManifest(record.envelope, this.publicKey);
    const entry = manifest.packs.find(item => sameEntry(item, record.entry));
    assert(entry && record.key === keyFor(entry) && record.data.byteLength === entry.bytes, 'storage', 'Cached asset identity changed');
    assert(await sha256(record.data) === entry.sha256, 'hash', 'Asset checksum rejected');
    let pack;
    try { pack = JSON.parse(DECODER.decode(record.data)); } catch { throw new AssetError('schema', 'Asset JSON is malformed'); }
    return { pack: validatePackedPack(pack, entry), entry, manifest };
  }
  async init() {
    const state = await this.storage.read(); checkState(state);
    this.packs.clear(); this.records = []; this.active = null; this.background = null; this.warnings = [];
    let total = 0;
    for (const record of state.packs) {
      try {
        total += bytes(record?.data) ? record.data.byteLength : ASSET_LIMITS.cacheBytes + 1;
        assert(total <= this.maxBytes, 'quota', 'Stored asset cache exceeds budget');
        const verified = await this.verifyRecord(record);
        const highest = state.highWater.versions.find(item => item.id === verified.entry.id);
        assert(!highest || (verified.entry.version === highest.version && verified.entry.sha256 === highest.sha256), 'rollback', 'Cached older asset version rejected');
        this.packs.set(verified.entry.id, verified.pack);
        this.records.push({ ...record, entry: verified.entry });
        if (record.key === state.active && verified.pack.formatVersion === 1) this.active = verified.pack;
        if (record.key === state.backgroundActive && verified.pack.formatVersion === 3) this.background = verified.pack;
      } catch (error) { this.warnings.push(error.message); }
    }
    if (!this.active) this.active = this.records.find(record => record.entry.required) ?
      this.packs.get(this.records.find(record => record.entry.required).entry.id) : null;
    return this.active;
  }
  getActive() { if (this.active) this.accesses.set(this.active.packId, this.now()); return this.active; }
  getActiveBackground() { if (this.background) this.accesses.set(this.background.packId, this.now()); return this.background; }
  getPack(id) { const pack = this.packs.get(id) || null; if (pack) this.accesses.set(id, this.now()); return pack; }
  list() { return this.records.map(record => ({ id: record.entry.id, version: record.entry.version, bytes: record.entry.bytes,
    required: record.entry.required, active: this.active?.packId === record.entry.id, backgroundActive: this.background?.packId === record.entry.id })); }
  async installPack(envelope, id, { signal, onProgress = () => {} } = {}) {
    assert(!this.installing, 'busy', 'An asset download is already running');
    this.installing = true;
    try { return await this.install(envelope, id, { signal, onProgress }); }
    catch (error) {
      // Partial staging may have evicted optional LRU records. Refresh visible
      // cache state so a failed download never labels evicted RAM art durable.
      try { await this.init(); } catch { /* Keep the previous in-memory fallback if storage itself is unavailable. */ }
      throw error;
    } finally { this.installing = false; }
  }
  async install(envelope, id, { signal, onProgress }) {
    aborted(signal);
    const manifest = await verifyManifest(envelope, this.publicKey);
    const entry = manifest.packs.find(item => item.id === id);
    assert(entry, 'manifest', 'Pack is absent from signed catalog');
    const key = keyFor(entry);
    let state = await this.storage.read(); checkState(state); rollbackCheck(state, manifest, entry);
    const existing = state.packs.find(record => record.key === key && sameEntry(record.entry, entry));
    let cached = null;
    if (existing) {
      try { cached = await this.verifyRecord(existing); } catch { /* Download a verified replacement; keep old bytes until activation. */ }
    }
    if (cached) {
      const { pack } = cached;
      aborted(signal);
      await this.storage.transaction(draft => {
        checkState(draft); rollbackCheck(draft, manifest, entry);
        assert(draft.packs.some(record => record.key === key && sameEntry(record.entry, entry)), 'concurrent', 'Cached asset changed in another tab');
        draft[pack.formatVersion === 3 ? 'backgroundActive' : 'active'] = key; draft.packs.find(record => record.key === key).lastUsed = this.now();
        touchVersion(draft, manifest, entry); ++draft.revision;
      });
      await this.init(); return pack;
    }
    await this.storage.transaction(draft => {
      checkState(draft); rollbackCheck(draft, manifest, entry);
      const stage = draft.stage;
      if (!(stage && sameEntry(stage.entry, entry) && bytes(stage.data) && stage.data.byteLength <= entry.bytes &&
        stage.envelope?.signature === envelope.signature && typeof stage.etag === 'string')) {
        draft.stage = { key, entry, envelope: structuredClone(envelope), data: new Uint8Array(), etag: '', lastUsed: this.now() };
      }
      makeRoom(draft, this.maxBytes, this.accesses); ++draft.revision;
    });
    state = await this.storage.read();
    let stage = state.stage;
    assert(stage?.key === key && sameEntry(stage.entry, entry), 'concurrent', 'Staged asset changed in another tab');
    const resumed = stage.data.byteLength > 0;
    onProgress({ received: stage.data.byteLength, total: entry.bytes, resumed });
    while (stage.data.byteLength < entry.bytes) {
      aborted(signal);
      const offset = stage.data.byteLength, end = Math.min(offset + ASSET_LIMITS.chunkBytes, entry.bytes) - 1;
      const headers = { Range: `bytes=${offset}-${end}`, Accept: 'application/json' };
      if (offset && stage.etag) headers['If-Range'] = stage.etag;
      const deadline = new AbortController();
      const cancel = () => deadline.abort();
      const timeout = setTimeout(cancel, this.requestTimeoutMs);
      signal?.addEventListener('abort', cancel, { once: true });
      let response, data, etag;
      try {
        response = await this.fetcher(entry.url, { headers, cache: 'no-store', signal: deadline.signal, credentials: 'omit', redirect: 'error' });
        assert(response.status === 200 || response.status === 206, 'download', `Asset request failed (${response.status})`);
        etag = response.headers.get('ETag') || '';
        assert(/^"[\x21\x23-\x7e]{1,126}"$/.test(etag), 'download', 'A strong asset ETag is required');
        if (response.status === 206) {
          assert(response.headers.get('Content-Range') === `bytes ${offset}-${end}/${entry.bytes}` &&
            (!offset || !stage.etag || stage.etag === etag), 'download', 'Range or ETag changed during asset download');
          const chunk = await readBounded(response, end - offset + 1, deadline.signal);
          assert(chunk.length === end - offset + 1, 'download', 'Asset chunk ended early');
          data = new Uint8Array(offset + chunk.length); data.set(stage.data); data.set(chunk, offset);
        } else {
          // If-Range mismatch returns a fresh full200; never append it to old bytes.
          data = await readBounded(response, entry.bytes, deadline.signal);
          assert(data.length === entry.bytes, 'download', 'Full asset response ended early');
        }
      } catch (error) {
        await response?.body?.cancel().catch(() => {});
        throw error;
      } finally { clearTimeout(timeout); signal?.removeEventListener('abort', cancel); }
      aborted(signal);
      await this.storage.transaction(draft => {
        checkState(draft); rollbackCheck(draft, manifest, entry);
        assert(draft.stage?.key === key && sameEntry(draft.stage.entry, entry) && draft.stage.data.byteLength === offset &&
          draft.stage.envelope.signature === envelope.signature, 'concurrent', 'Staged asset changed in another tab');
        draft.stage.data = data; draft.stage.etag = etag; draft.stage.lastUsed = this.now();
        makeRoom(draft, this.maxBytes, this.accesses); ++draft.revision;
      });
      stage = { ...stage, data, etag };
      onProgress({ received: data.byteLength, total: entry.bytes, resumed });
    }
    aborted(signal);
    let pack;
    try { ({ pack } = await this.verifyRecord(stage)); }
    catch (error) {
      // Invalid staging is disposable. Active and required complete packs never
      // move during this cleanup, and a subsequent install can retry from zero.
      await this.storage.transaction(draft => {
        checkState(draft);
        if (draft.stage?.key === key && draft.stage.envelope.signature === envelope.signature) { draft.stage = null; ++draft.revision; }
      });
      throw error;
    }
    aborted(signal);
    await this.storage.transaction(draft => {
      checkState(draft); rollbackCheck(draft, manifest, entry);
      assert(draft.stage?.key === key && draft.stage.data.byteLength === entry.bytes &&
        draft.stage.envelope.signature === envelope.signature, 'concurrent', 'Staged asset changed before activation');
      // Complete bytes + active pointer + version boundary move together. Any
      // quota/I/O/transaction failure retains the previously active pack.
      draft.packs = draft.packs.filter(record => record.entry.id !== entry.id);
      draft.packs.push({ key, entry, envelope: structuredClone(envelope), data: stage.data, lastUsed: this.now() });
      draft.stage = null; draft[pack.formatVersion === 3 ? 'backgroundActive' : 'active'] = key; touchVersion(draft, manifest, entry);
      makeRoom(draft, this.maxBytes, this.accesses); ++draft.revision;
    });
    await this.init(); return pack;
  }
}

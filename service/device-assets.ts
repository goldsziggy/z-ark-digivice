import { createHash } from 'node:crypto';
import { lstatSync, readFileSync, realpathSync } from 'node:fs';
import { createRequire } from 'node:module';
import { join, relative, resolve } from 'node:path';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { requireDevelopment, signDevelopmentPayload, verifyDevelopmentEnvelope, type SignedCatalog } from './asset-signing.ts';

const { validateBackgroundJpeg } = createRequire(import.meta.url)('../web/background-pack.js') as {
  validateBackgroundJpeg: (bytes: Uint8Array, dimension: number) => Uint8Array;
};
export const DEVICE_PROFILE = 's3-146-v1';
export const DEVICE_LIMITS = Object.freeze({ entries: 24, blobBytes: 128 * 1024, payloadBytes: 6 * 1024, envelopeBytes: 8 * 1024, indexBytes: 8 * 1024 });
const TEST_SPRITES = ['mote', 'glint', 'lumen', 'flicker', 'rill', 'brine', 'pelagia', 'cinder', 'scoria', 'pyrel'];
const SCENES = ['meadow', 'forest', 'beach', 'ruins', 'cavern', 'snow', 'volcanic', 'digital'];
export const DEVICE_TEST_ASSET_IDS = Object.freeze(TEST_SPRITES.map(id => `sprite-${id}-v1`));
export const DEVICE_ASSET_IDS = Object.freeze([...DEVICE_TEST_ASSET_IDS, ...SCENES.map(id => `scene-${id}-412-v1`)]);
type Entry = { id: string; version: number; bytes: number; sha256: string; kind: 'sprite' | 'background'; width: number; height: number; url: string };
export type DeviceManifest = { formatVersion: 1; release: number; rulesVersion: 1; profile: typeof DEVICE_PROFILE; packs: Entry[] };
const decoder = new TextDecoder('utf-8', { fatal: true });
function object(value: unknown): value is Record<string, unknown> { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function exact(value: unknown, keys: string[]): value is Record<string, unknown> { return object(value) && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key)); }
function integer(value: unknown, maximum = 0x7fffffff): value is number { return Number.isSafeInteger(value) && Number(value) > 0 && Number(value) <= maximum; }
function check(value: unknown, message: string): asserts value { if (!value) throw new Error(`Device assets: ${message}`); }
function digest(bytes: Uint8Array): string { return createHash('sha256').update(bytes).digest('hex'); }
function readBounded(path: string, limit: number): Buffer {
  const info = lstatSync(path);
  check(info.isFile() && info.size > 0 && info.size <= limit, 'expected a bounded regular file');
  const bytes = readFileSync(path); check(bytes.length === info.size && bytes.length <= limit, 'file changed during read'); return bytes;
}
export function validateDeviceManifest(value: unknown): asserts value is DeviceManifest {
  check(exact(value, ['formatVersion', 'release', 'rulesVersion', 'profile', 'packs']) && value.formatVersion === 1 && value.rulesVersion === 1 &&
    value.profile === DEVICE_PROFILE && integer(value.release) && Array.isArray(value.packs) && value.packs.length <= DEVICE_ASSET_IDS.length && value.packs.length <= DEVICE_LIMITS.entries, 'unsupported manifest');
  const ids = new Set<string>();
  for (const entry of value.packs) {
    check(exact(entry, ['id', 'version', 'bytes', 'sha256', 'kind', 'width', 'height', 'url']) && typeof entry.id === 'string' &&
      DEVICE_ASSET_IDS.includes(entry.id) && !ids.has(entry.id) && integer(entry.version) && integer(entry.bytes, DEVICE_LIMITS.blobBytes) &&
      typeof entry.sha256 === 'string' && /^[a-f0-9]{64}$/.test(entry.sha256) &&
      entry.kind === (entry.id.startsWith('sprite-') ? 'sprite' : 'background') && entry.width === (entry.kind === 'sprite' ? 32 : 412) &&
      entry.height === entry.width && entry.url === `/api/device/assets/packs/${entry.id}/${entry.version}`, 'invalid manifest entry');
    ids.add(entry.id);
  }
}
export function verifyDeviceCatalog(envelope: unknown): DeviceManifest {
  check(Buffer.byteLength(JSON.stringify(envelope)) <= DEVICE_LIMITS.envelopeBytes, 'catalog exceeds envelope budget');
  const manifest: unknown = JSON.parse(decoder.decode(verifyDevelopmentEnvelope(envelope, DEVICE_LIMITS.payloadBytes)));
  validateDeviceManifest(manifest); return manifest;
}
function crc32(bytes: Uint8Array): number {
  let value = 0xffffffff;
  for (const byte of bytes) { value ^= byte; for (let bit = 0; bit < 8; ++bit) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0); }
  return (value ^ 0xffffffff) >>> 0;
}
export function validateDeviceBlob(bytes: Buffer, entry: Pick<Entry, 'kind' | 'version' | 'width' | 'height'>): void {
  check(bytes.length > 0 && bytes.length <= DEVICE_LIMITS.blobBytes, 'blob size');
  if (entry.kind === 'background') { check(entry.width === 412 && entry.height === 412, 'background dimensions'); validateBackgroundJpeg(bytes, 412); return; }
  check(entry.kind === 'sprite' && entry.width === 32 && entry.height === 32 && bytes.length === 8816, 'sprite dimensions or byte length');
  check(bytes.toString('ascii', 0, 4) === 'DVA1' && bytes.readUInt16LE(4) === 1 && bytes.readUInt16LE(6) === 32 &&
    bytes.readUInt32LE(8) === entry.version && bytes.readUInt16LE(12) === 32 && bytes.readUInt16LE(14) === 32 &&
    bytes[16] === 16 && bytes[17] === 6 && bytes[18] === 0 && bytes[19] === 0 && bytes.readUInt16LE(20) === 17 &&
    bytes.readUInt16LE(22) === 512 && bytes.readUInt32LE(24) === bytes.length - 32 && bytes.readUInt32LE(28) === crc32(bytes.subarray(32)), 'invalid DVA1 header or CRC');
  let first = 0;
  const counts = [4, 3, 2, 2, 2, 4];
  for (let animation = 0; animation < 6; ++animation) {
    const offset = 64 + animation * 8;
    check(bytes[offset] === animation && bytes[offset + 1] === counts[animation] && bytes.readUInt16LE(offset + 2) >= 40 &&
      bytes.readUInt16LE(offset + 2) <= 2000 && bytes.readUInt16LE(offset + 4) === first && bytes.readUInt16LE(offset + 6) === 0, 'invalid DVA1 animation table');
    first += counts[animation];
  }
}

/** Eager fixed-file verification; caller enforces its explicit network policy. */
export function createDeviceAssetService({ rootDir, includeTestFixtures = false }: { rootDir: string; includeTestFixtures?: boolean }) {
  requireDevelopment();
  const root = realpathSync(rootDir), directory = join(root, 'assets/device');
  const actual = realpathSync(directory), within = relative(root, actual);
  check(actual === resolve(directory) && within && !within.startsWith('..'), 'asset directory must be local and nonsymlinked');
  const index: unknown = JSON.parse(decoder.decode(readBounded(join(directory, 'index.json'), DEVICE_LIMITS.indexBytes)));
  check(exact(index, ['formatVersion', 'release', 'profile', 'packs']) && index.formatVersion === 1 && index.profile === DEVICE_PROFILE && integer(index.release) &&
    Array.isArray(index.packs) && index.packs.length > 0 && index.packs.length <= DEVICE_ASSET_IDS.length, 'invalid machine index');
  const blobs = new Map<string, { bytes: Buffer; etag: string; type: string }>();
  const sourceEntries: Entry[] = index.packs.map(item => {
    check(exact(item, ['id', 'version', 'bytes', 'sha256', 'kind', 'width', 'height', 'file']) && typeof item.id === 'string' && DEVICE_ASSET_IDS.includes(item.id) &&
      item.file === `${item.id}.${item.kind === 'sprite' ? 'dva' : 'jpg'}`, 'invalid index file or identity');
    return { id: item.id, version: item.version, bytes: item.bytes, sha256: item.sha256, kind: item.kind, width: item.width, height: item.height,
      url: `/api/device/assets/packs/${item.id}/${item.version}` } as Entry;
  });
  const sourceManifest: DeviceManifest = { formatVersion: 1, release: index.release, rulesVersion: 1, profile: DEVICE_PROFILE, packs: sourceEntries };
  validateDeviceManifest(sourceManifest);
  const entries = sourceEntries.filter(entry => includeTestFixtures || !DEVICE_TEST_ASSET_IDS.includes(entry.id));
  const manifest: DeviceManifest = { ...sourceManifest, packs: entries };
  validateDeviceManifest(manifest);
  for (const entry of entries) {
    const bytes = readBounded(join(directory, `${entry.id}.${entry.kind === 'sprite' ? 'dva' : 'jpg'}`), DEVICE_LIMITS.blobBytes);
    check(bytes.length === entry.bytes && digest(bytes) === entry.sha256, 'blob integrity failure');
    validateDeviceBlob(bytes, entry);
    blobs.set(entry.url, { bytes, etag: `"${entry.sha256}"`, type: entry.kind === 'sprite' ? 'application/octet-stream' : 'image/jpeg' });
  }
  const payload = Buffer.from(JSON.stringify(manifest)); check(payload.length <= DEVICE_LIMITS.payloadBytes, 'manifest payload budget');
  const envelope: SignedCatalog = signDevelopmentPayload(payload); verifyDeviceCatalog(envelope);
  const catalog = Buffer.from(JSON.stringify(envelope));
  const catalogPath = '/api/device/assets/catalog';
  blobs.set(catalogPath, { bytes: catalog, etag: `"${digest(catalog)}"`, type: 'application/json; charset=utf-8' });
  function error(response: ServerResponse, status: number, code: string, headers: Record<string, string> = {}) {
    response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', ...headers });
    response.end(JSON.stringify({ error: code }));
  }
  function handleDeviceAssetRequest(request: IncomingMessage, response: ServerResponse, path: string): boolean {
    if (path !== '/api/device/assets' && !path.startsWith('/api/device/assets/')) return false;
    const asset = blobs.get(path);
    if (!asset || path.length > 128 || request.url !== path) { error(response, 404, 'device_asset_not_found'); return true; }
    if (request.method !== 'GET' && request.method !== 'HEAD') { error(response, 405, 'device_asset_method_not_allowed', { Allow: 'GET, HEAD' }); return true; }
    const size = asset.bytes.length;
    const headers = { 'Content-Type': asset.type, ETag: asset.etag, 'Accept-Ranges': 'bytes', 'Cache-Control': path === catalogPath ? 'no-cache' : 'public, max-age=31536000, immutable', 'X-Content-Type-Options': 'nosniff' };
    const ifNoneMatch = request.headers['if-none-match'];
    if (typeof ifNoneMatch === 'string' && ifNoneMatch.length <= 1024 && ifNoneMatch.split(',').some(value => value.trim() === '*' || value.trim().replace(/^W\//, '') === asset.etag)) {
      response.writeHead(304, headers); response.end(); return true;
    }
    const range = request.headers.range, ifRange = request.headers['if-range'];
    if (request.method === 'GET' && range && (ifRange === undefined || ifRange === asset.etag)) {
      const match = range.length <= 80 ? /^bytes=(\d*)-(\d*)$/.exec(range) : null;
      let start = 0, end = size - 1;
      if (match && (match[1] || match[2])) {
        if (match[1]) { start = Number(match[1]); if (match[2]) end = Math.min(Number(match[2]), size - 1); }
        else { const count = Number(match[2]); start = Number.isSafeInteger(count) && count > 0 ? Math.max(0, size - count) : size; }
      }
      if (!match || !(match[1] || match[2]) || !Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start < 0 || start >= size || end < start) {
        error(response, 416, 'device_asset_range_unsatisfiable', { 'Content-Range': `bytes */${size}`, ETag: asset.etag }); return true;
      }
      response.writeHead(206, { ...headers, 'Content-Range': `bytes ${start}-${end}/${size}`, 'Content-Length': end - start + 1 });
      response.end(asset.bytes.subarray(start, end + 1)); return true;
    }
    response.writeHead(200, { ...headers, 'Content-Length': size }); response.end(request.method === 'HEAD' ? undefined : asset.bytes); return true;
  }
  return { handleDeviceAssetRequest, manifest, envelope };
}

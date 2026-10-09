import { createHash } from 'node:crypto';
import { lstatSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { MAX_CATALOG_BYTES, MAX_PACK_BYTES, inspectPack, PACK_IDS, requireDevelopment, signManifest, verifyCatalog } from './asset-signing.ts';

type Asset = { bytes: Buffer; etag: string; immutable: boolean };
function digest(bytes: Buffer): string { return createHash('sha256').update(bytes).digest('hex'); }
function boundedFile(path: string, maximum: number): Buffer {
  const info = lstatSync(path);
  if (!info.isFile() || info.size < 1 || info.size > maximum) throw new Error('Asset file is missing, not regular, or exceeds its byte limit.');
  const bytes = readFileSync(path);
  if (bytes.length !== info.size || bytes.length > maximum) throw new Error('Asset file changed while loading.');
  return bytes;
}

/** Eagerly verify once, then serve immutable cached bytes. Caller applies local-origin policy. */
export function createAssetService({ rootDir, includeTestFixtures = false }: { rootDir: string; includeTestFixtures?: boolean }) {
  requireDevelopment();
  const packDir = join(rootDir, 'assets/packs');
  const sourceBytes = boundedFile(join(packDir, 'catalog.json'), MAX_CATALOG_BYTES);
  const source = verifyCatalog(JSON.parse(sourceBytes.toString('utf8')));
  // Stored catalogs from earlier prototypes may still contain toy creatures.
  // Exclude them before reading blobs, building URLs or exposing a signed catalog.
  const manifest = { ...source, packs: source.packs.filter(entry => includeTestFixtures || !PACK_IDS.includes(entry.id as typeof PACK_IDS[number])) };
  const catalogBytes = manifest.packs.length === source.packs.length ? sourceBytes : Buffer.from(JSON.stringify(signManifest(manifest)));
  const assets = new Map<string, Asset>();
  assets.set('/api/assets/catalog', { bytes: catalogBytes, etag: `"${digest(catalogBytes)}"`, immutable: false });
  for (const entry of manifest.packs) {
    const bytes = boundedFile(join(packDir, `${entry.id}.json`), MAX_PACK_BYTES);
    if (bytes.length !== entry.bytes || digest(bytes) !== entry.sha256) throw new Error(`Asset pack integrity failure: ${entry.id}.`);
    const actual = inspectPack(JSON.parse(bytes.toString('utf8')), entry.id);
    if (actual.version !== entry.version || actual.decodedBytes !== entry.decodedBytes) throw new Error(`Asset pack metadata mismatch: ${entry.id}.`);
    assets.set(entry.url, { bytes, etag: `"${entry.sha256}"`, immutable: true });
  }
  function error(response: ServerResponse, status: number, code: string, headers: Record<string, string> = {}) {
    response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...headers });
    response.end(JSON.stringify({ error: code }));
  }
  function handleAssetRequest(request: IncomingMessage, response: ServerResponse, path: string): boolean {
    if (path !== '/api/assets' && !path.startsWith('/api/assets/')) return false;
    if (path.length > 128 || !assets.has(path)) { error(response, 404, 'asset_not_found'); return true; }
    if (request.method !== 'GET' && request.method !== 'HEAD') { error(response, 405, 'asset_method_not_allowed', { Allow: 'GET, HEAD' }); return true; }
    const asset = assets.get(path)!;
    const size = asset.bytes.length;
    const headers: Record<string, string | number> = {
      'Content-Type': 'application/json; charset=utf-8',
      'ETag': asset.etag,
      'Accept-Ranges': 'bytes',
      'Cache-Control': asset.immutable ? 'public, max-age=31536000, immutable' : 'no-cache',
      'X-Content-Type-Options': 'nosniff',
    };
    const ifNoneMatch = request.headers['if-none-match'];
    if (typeof ifNoneMatch === 'string' && ifNoneMatch.length <= 1024 && ifNoneMatch.split(',').some((entry) => entry.trim() === '*' || entry.trim().replace(/^W\//, '') === asset.etag)) {
      response.writeHead(304, headers); response.end(); return true;
    }
    const range = request.headers.range;
    const ifRange = request.headers['if-range'];
    // Range is defined for GET. A stale/weak/date If-Range returns complete current bytes.
    if (request.method === 'GET' && range && (ifRange === undefined || ifRange === asset.etag)) {
      const match = range.length <= 80 ? /^bytes=(\d*)-(\d*)$/.exec(range) : null;
      let start = 0;
      let end = size - 1;
      if (match && (match[1] || match[2])) {
        if (match[1]) { start = Number(match[1]); if (match[2]) end = Math.min(Number(match[2]), size - 1); }
        else { const count = Number(match[2]); start = Math.max(0, size - count); if (!Number.isSafeInteger(count) || count <= 0) start = size; }
      }
      if (!match || !(match[1] || match[2]) || !Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start < 0 || start >= size || end < start) {
        error(response, 416, 'asset_range_unsatisfiable', { 'Content-Range': `bytes */${size}`, ETag: asset.etag, 'Accept-Ranges': 'bytes' }); return true;
      }
      response.writeHead(206, { ...headers, 'Content-Range': `bytes ${start}-${end}/${size}`, 'Content-Length': end - start + 1 });
      response.end(asset.bytes.subarray(start, end + 1)); return true;
    }
    response.writeHead(200, { ...headers, 'Content-Length': size });
    response.end(request.method === 'HEAD' ? undefined : asset.bytes);
    return true;
  }
  return { handleAssetRequest };
}

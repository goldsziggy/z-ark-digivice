import { createHash } from 'node:crypto';
import { closeSync, existsSync, fsyncSync, lstatSync, mkdirSync, openSync, readFileSync, readdirSync, realpathSync, renameSync, unlinkSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, isAbsolute, join, relative, resolve, sep } from 'node:path';
import { parseEnv } from 'node:util';
import { MAX_CATALOG_BYTES, MAX_PACK_BYTES, PACK_IDS, inspectPack, verifyCatalog } from './asset-signing.ts';

export const GARAGE_PREFIX = 'digivice/v1';
const MAX_PROVENANCE = 64 * 1024;
const MAX_RECEIPT = 64 * 1024;
const MAX_TOTAL = 4 * 1024 * 1024;
const PERSONAL_ID = /^personal-[a-z0-9][a-z0-9-]{0,38}$/;
type Kind = 'catalog' | 'original' | 'personal' | 'provenance';
type Entry = { id: string; version: number; kind: Kind; bytes: number; sha256: string; key: string; contentType: 'application/json' };
export type GarageReceipt = { formatVersion: 1; bucket: string; prefix: typeof GARAGE_PREFIX; objects: Entry[] };
export type GaragePlan = { objects: Array<Entry & { data: Buffer }>; totalBytes: number };
export type GarageStatus = { status: 'private' | 'unavailable'; reason?: string; hostname?: string; bucket?: string; prefix: string; explicitBucket: boolean };
export type GarageCatalog = { status: 'available'; packs: Array<{ id: string; version: number; kind: 'original' | 'personal'; bytes: number; sha256: string }> };
export type GarageTransport = {
  website(bucket: string): Promise<'enabled' | 'disabled'>;
  head(bucket: string, key: string): Promise<{ bytes: number } | null>;
  get(bucket: string, key: string, maximum: number): Promise<Buffer>;
  put(bucket: string, key: string, bytes: Buffer): Promise<void>;
  close?(): void;
};
export type GarageOptions = { rootDir: string; envFile?: string; bucket?: string; receiptPath?: string; sdkPackagePath?: string; transport?: GarageTransport };
export class GarageError extends Error {
  code: string;
  status: number;
  constructor(code: string, message: string, status = 503) { super(message); this.name = 'GarageError'; this.code = code; this.status = status; }
}
function failure(code: string, message: string, status = 503): never { throw new GarageError(code, message, status); }
function object(value: unknown): value is Record<string, unknown> { return !!value && typeof value === 'object' && !Array.isArray(value); }
function digest(data: Buffer): string { return createHash('sha256').update(data).digest('hex'); }
function exact(value: Record<string, unknown>, names: string[]): boolean { return Object.keys(value).length === names.length && names.every(name => Object.hasOwn(value, name)); }
function integer(value: unknown, max: number): value is number { return Number.isSafeInteger(value) && Number(value) > 0 && Number(value) <= max; }
function boundedFile(path: string, max: number): Buffer {
  try {
    const stat = lstatSync(path);
    if (!stat.isFile() || stat.size < 1 || stat.size > max) failure('garage_local_file', 'An asset file is not a bounded regular file.');
    const bytes = readFileSync(path);
    if (bytes.length !== stat.size || bytes.length > max) failure('garage_local_file', 'An asset file changed while reading.');
    return bytes;
  } catch (error) { if (error instanceof GarageError) throw error; return failure('garage_local_file', 'A required local asset file is unavailable.'); }
}
function confinedFile(root: string, path: string, max: number): Buffer {
  try {
    const resolvedRoot = resolve(root), resolvedPath = resolve(path);
    const tail = relative(resolvedRoot, resolvedPath);
    if (!tail || tail.startsWith(`..${sep}`) || tail === '..' || isAbsolute(tail)) failure('garage_local_path', 'Asset source is outside the selected repository.');
    let ancestor = resolvedRoot;
    for (const part of tail.split(sep)) {
      ancestor = join(ancestor, part);
      if (lstatSync(ancestor).isSymbolicLink()) failure('garage_local_path', 'Symlinked asset sources are not eligible for upload.');
    }
    if (!realpathSync(resolvedPath).startsWith(`${realpathSync(resolvedRoot)}${sep}`)) failure('garage_local_path', 'Asset source is outside the selected repository.');
    return boundedFile(resolvedPath, max);
  } catch (error) { if (error instanceof GarageError) throw error; return failure('garage_local_file', 'A required local asset file is unavailable.'); }
}
function utf8(data: Buffer): string {
  try { return new TextDecoder('utf-8', { fatal: true }).decode(data); }
  catch { return failure('garage_invalid_json', 'An asset or receipt is not valid bounded UTF-8 JSON.'); }
}
function parseJson(data: Buffer): unknown {
  try { return JSON.parse(utf8(data)); }
  catch { return failure('garage_invalid_json', 'An asset or receipt is not valid bounded UTF-8 JSON.'); }
}
function keyFor(entry: Pick<Entry, 'id' | 'version' | 'kind' | 'sha256'>): string {
  const area = entry.kind === 'personal' || entry.kind === 'provenance' ? 'personal' : 'original';
  return `${GARAGE_PREFIX}/${area}/${entry.id}/${entry.version}/${entry.kind}-${entry.sha256}.json`;
}
function entryFor(id: string, version: number, kind: Kind, data: Buffer): Entry & { data: Buffer } {
  const entry = { id, version, kind, bytes: data.length, sha256: digest(data), contentType: 'application/json' as const };
  return { ...entry, key: keyFor(entry), data };
}
async function validatePersonal(packText: string, provenanceText: string): Promise<void> {
  // Reuse the same bounded personal-art validator as the browser; never execute an imported pack.
  const moduleUrl = new URL('../web/personal-pack.js', import.meta.url).href;
  const { validatePersonalImport } = await import(moduleUrl);
  try { await validatePersonalImport(packText, provenanceText); }
  catch { failure('garage_personal_invalid', 'Personal art and its provenance did not pass local validation.'); }
}

/** Explicit allowlist: signed catalog + three original packs; direct personal-* pack/provenance pairs only. */
export async function buildGaragePlan(rootDir: string): Promise<GaragePlan> {
  const root = resolve(rootDir);
  const catalogData = confinedFile(root, join(root, 'assets/packs/catalog.json'), MAX_CATALOG_BYTES);
  let manifest;
  try { manifest = verifyCatalog(parseJson(catalogData)); }
  catch { return failure('garage_catalog_invalid', 'The original signed catalog is invalid.'); }
  // Local catalogs may grow independently. Never inherit new upload scope from
  // their entries or silently publish a filtered, differently signed catalog.
  if (manifest.packs.length !== PACK_IDS.length || manifest.packs.some(entry => !PACK_IDS.includes(entry.id as typeof PACK_IDS[number]))) {
    return failure('garage_catalog_scope', 'This local catalog exceeds the approved original-three Garage publication scope.');
  }
  const objects = [entryFor('original-catalog', manifest.release, 'catalog', catalogData)];
  for (const entry of manifest.packs) {
    const data = confinedFile(root, join(root, `assets/packs/${entry.id}.json`), MAX_PACK_BYTES);
    if (data.length !== entry.bytes || digest(data) !== entry.sha256) failure('garage_hash_mismatch', 'Original asset integrity check failed.', 502);
    try { const result = inspectPack(parseJson(data), entry.id); if (result.version !== entry.version || result.decodedBytes !== entry.decodedBytes) throw new Error(); }
    catch { failure('garage_original_invalid', 'An original pack does not match its signed schema.'); }
    objects.push(entryFor(entry.id, entry.version, 'original', data));
  }
  const personalRoot = join(root, '.personal-assets');
  if (existsSync(personalRoot)) {
    if (lstatSync(personalRoot).isSymbolicLink() || !lstatSync(personalRoot).isDirectory()) failure('garage_local_path', 'The personal-art source root must be a real repository directory.');
    const directories = readdirSync(personalRoot, { withFileTypes: true }).filter(item => item.isDirectory() && PERSONAL_ID.test(item.name)).sort((a, b) => a.name.localeCompare(b.name));
    if (directories.length > 5) failure('garage_plan_limit', 'At most five curated personal packs may be published in one plan.');
    for (const directory of directories) {
      const pack = confinedFile(root, join(personalRoot, directory.name, 'pack.json'), MAX_PACK_BYTES);
      const provenance = confinedFile(root, join(personalRoot, directory.name, 'provenance.json'), MAX_PROVENANCE);
      await validatePersonal(utf8(pack), utf8(provenance));
      const info = parseJson(pack) as { packId: string; version: number };
      if (info.packId !== directory.name) failure('garage_personal_invalid', 'Personal directory and pack identity disagree.');
      objects.push(entryFor(info.packId, info.version, 'personal', pack), entryFor(info.packId, info.version, 'provenance', provenance));
    }
  }
  const totalBytes = objects.reduce((sum, entry) => sum + entry.bytes, 0);
  if (objects.length > 14 || totalBytes > MAX_TOTAL) failure('garage_plan_limit', 'The allowlisted asset plan exceeds its 4 MiB or object limit.');
  return { objects, totalBytes };
}
function validateReceipt(value: unknown, bucket: string): asserts value is GarageReceipt {
  if (!object(value) || !exact(value, ['formatVersion', 'bucket', 'prefix', 'objects']) || value.formatVersion !== 1 || value.bucket !== bucket || value.prefix !== GARAGE_PREFIX || !Array.isArray(value.objects) || value.objects.length < 4 || value.objects.length > 14) failure('garage_receipt_invalid', 'The local Garage receipt is unsupported or belongs to another destination.');
  const entries = value.objects;
  const seen = new Set<string>();
  let total = 0;
  for (const entry of entries) {
    if (!object(entry) || !exact(entry, ['id', 'version', 'kind', 'bytes', 'sha256', 'key', 'contentType']) || typeof entry.id !== 'string' || !integer(entry.version, 0x7fffffff) || !['catalog', 'original', 'personal', 'provenance'].includes(String(entry.kind)) || !integer(entry.bytes, entry.kind === 'catalog' ? MAX_CATALOG_BYTES : entry.kind === 'provenance' ? MAX_PROVENANCE : MAX_PACK_BYTES) || typeof entry.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(entry.sha256) || entry.contentType !== 'application/json') failure('garage_receipt_invalid', 'The local Garage receipt contains an invalid object.');
    const typed = entry as Entry;
    if ((entry.kind === 'catalog' && entry.id !== 'original-catalog') || (entry.kind === 'original' && !PACK_IDS.includes(entry.id as typeof PACK_IDS[number])) || (['personal', 'provenance'].includes(typed.kind) && !PERSONAL_ID.test(entry.id)) || entry.key !== keyFor(typed) || seen.has(`${entry.kind}/${entry.id}/${entry.version}`)) failure('garage_receipt_invalid', 'The local Garage receipt has an unsafe key or duplicate object.');
    seen.add(`${entry.kind}/${entry.id}/${entry.version}`); total += entry.bytes;
  }
  if (total > MAX_TOTAL || entries.filter(entry => entry.kind === 'catalog').length !== 1 || PACK_IDS.some(id => entries.filter(entry => entry.kind === 'original' && entry.id === id).length !== 1)) failure('garage_receipt_invalid', 'The local Garage receipt is incomplete or too large.');
  for (const entry of entries.filter(entry => entry.kind === 'personal' || entry.kind === 'provenance')) {
    const pairedKind = entry.kind === 'personal' ? 'provenance' : 'personal';
    if (!entries.some(pair => pair.kind === pairedKind && pair.id === entry.id && pair.version === entry.version)) failure('garage_receipt_invalid', 'A personal pack is missing its provenance pair.');
  }
}
function writeReceipt(path: string, receipt: GarageReceipt): void {
  mkdirSync(dirname(path), { recursive: true, mode: 0o700 });
  const temporary = `${path}.${process.pid}.tmp`;
  let fd: number | undefined;
  try {
    fd = openSync(temporary, 'wx', 0o600); writeFileSync(fd, JSON.stringify(receipt)); fsyncSync(fd); closeSync(fd); fd = undefined;
    renameSync(temporary, path);
    const parent = openSync(dirname(path), 'r'); try { fsyncSync(parent); } finally { closeSync(parent); }
  } finally { if (fd !== undefined) closeSync(fd); if (existsSync(temporary)) unlinkSync(temporary); }
}
function sdkTransport(env: Record<string, string | undefined>, envFile: string, sdkPackagePath?: string): GarageTransport {
  let sdk: any;
  try { sdk = createRequire(sdkPackagePath ?? join(dirname(envFile), 'package.json'))('@aws-sdk/client-s3'); }
  catch { return failure('garage_sdk_unavailable', 'The existing server S3 SDK is unavailable; no package was installed.'); }
  const region = env.AWS_REGION || env.AWS_DEFAULT_REGION;
  if (!region || !env.S3_ENDPOINT || !env.AWS_ACCESS_KEY_ID || !env.AWS_SECRET_ACCESS_KEY) failure('garage_not_configured', 'Existing server Garage configuration is incomplete.');
  const client = new sdk.S3Client({ endpoint: env.S3_ENDPOINT, region, forcePathStyle: true,
    credentials: { accessKeyId: env.AWS_ACCESS_KEY_ID, secretAccessKey: env.AWS_SECRET_ACCESS_KEY, ...(env.AWS_SESSION_TOKEN ? { sessionToken: env.AWS_SESSION_TOKEN } : {}) },
    maxAttempts: 1, requestChecksumCalculation: 'WHEN_REQUIRED', responseChecksumValidation: 'WHEN_REQUIRED',
    requestHandler: { connectionTimeout: 1000, requestTimeout: 2500 } });
  const send = (command: unknown) => client.send(command, { abortSignal: AbortSignal.timeout(2500) });
  return {
    async website(bucket) {
      try { await send(new sdk.GetBucketWebsiteCommand({ Bucket: bucket })); return 'enabled'; }
      catch (error) { if ((error as { name?: string }).name === 'NoSuchWebsiteConfiguration') return 'disabled'; throw error; }
    },
    async head(bucket, key) {
      try { const response = await send(new sdk.HeadObjectCommand({ Bucket: bucket, Key: key })); return { bytes: response.ContentLength }; }
      catch (error) { if (['NotFound', 'NoSuchKey'].includes((error as { name?: string }).name ?? '')) return null; throw error; }
    },
    async get(bucket, key, maximum) {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), 2500);
      let body: any;
      const abortBody = () => body?.destroy?.(new Error('Garage read deadline exceeded'));
      controller.signal.addEventListener('abort', abortBody, { once: true });
      try {
        const response = await client.send(new sdk.GetObjectCommand({ Bucket: bucket, Key: key }), { abortSignal: controller.signal });
        body = response.Body;
        if (controller.signal.aborted) abortBody();
        if (!body || (response.ContentLength !== undefined && response.ContentLength > maximum)) return failure('garage_response_limit', 'Garage returned an oversized or missing body.', 502);
        const chunks: Buffer[] = []; let length = 0;
        for await (const part of body) { const chunk = Buffer.from(part); length += chunk.length; if (length > maximum) failure('garage_response_limit', 'Garage returned an oversized body.', 502); chunks.push(chunk); }
        return Buffer.concat(chunks);
      } finally { clearTimeout(timer); controller.signal.removeEventListener('abort', abortBody); body?.destroy?.(); }
    },
    async put(bucket, key, bytes) {
      await send(new sdk.PutObjectCommand({ Bucket: bucket, Key: key, Body: bytes, ContentLength: bytes.length, ContentType: 'application/json', CacheControl: 'private, no-store', IfNoneMatch: '*' }));
    },
    close() { client.destroy(); },
  };
}

/** Server-only credentials stay inside this closure. No method changes bucket policy, grants, or keys. */
export function createGarageAssets(options: GarageOptions) {
  const rootDir = resolve(options.rootDir);
  const receiptPath = options.receiptPath ?? join(rootDir, '.data/garage-assets.json');
  let transport = options.transport;
  let bucket = options.bucket ?? process.env.DIGIVICE_GARAGE_BUCKET ?? '';
  const explicitBucket = !!bucket;
  let hostname = transport ? 'injected-transport' : '';
  let initializationError: GarageError | null = null;
  function initialize() {
    if (initializationError) throw initializationError;
    if (transport) return;
    try {
      const envFile = options.envFile ?? process.env.DIGIVICE_GARAGE_ENV_FILE;
      if (!envFile) failure('garage_not_configured', 'Set an explicit Garage environment file before using remote assets.');
      const data = boundedFile(envFile, 256 * 1024);
      const env = parseEnv(data.toString('utf8'));
      bucket ||= env.S3_BUCKET ?? '';
      const endpoint = new URL(env.S3_ENDPOINT ?? '');
      if (!['http:', 'https:'].includes(endpoint.protocol) || endpoint.username || endpoint.password || endpoint.search || endpoint.hash) failure('garage_not_configured', 'Garage endpoint configuration is invalid.');
      hostname = endpoint.hostname;
      transport = sdkTransport(env, envFile, options.sdkPackagePath ?? process.env.DIGIVICE_GARAGE_SDK_PACKAGE);
    } catch { initializationError = new GarageError('garage_not_configured', 'Existing server Garage access is unavailable or incomplete.'); throw initializationError; }
  }
  async function checkPrivate(requireExplicit = true): Promise<void> {
    if (requireExplicit && !explicitBucket) failure('garage_bucket_required', 'Configure an explicitly selected private Garage bucket before using remote assets.');
    initialize();
    if (!bucket || !/^[a-z0-9][a-z0-9.-]{1,61}[a-z0-9]$/.test(bucket)) failure('garage_not_configured', 'An existing Garage bucket must be configured.');
    try { if (await transport!.website(bucket) !== 'disabled') failure('garage_bucket_public', 'The Garage bucket has website publication enabled; asset transfer is refused.'); }
    catch (error) { if (error instanceof GarageError) throw error; failure('garage_privacy_unverified', 'Garage bucket privacy or existing access could not be verified.'); }
  }
  function receipt(): GarageReceipt {
    if (!existsSync(receiptPath)) return failure('garage_not_uploaded', 'No verified Garage upload receipt is installed.');
    const stored = parseJson(boundedFile(receiptPath, MAX_RECEIPT)); validateReceipt(stored, bucket); return stored;
  }
  async function verified(entry: Entry): Promise<Buffer> {
    let data: Buffer;
    try { data = await transport!.get(bucket, entry.key, entry.bytes); }
    catch (error) { if (error instanceof GarageError) throw error; return failure('garage_unavailable', 'The private Garage asset could not be fetched.'); }
    if (data.length !== entry.bytes || digest(data) !== entry.sha256) return failure('garage_hash_mismatch', 'The private Garage asset failed its pinned checksum.', 502);
    return data;
  }
  return {
    async diagnose(): Promise<GarageStatus> {
      try { await checkPrivate(false); return { status: 'private', hostname, bucket, prefix: GARAGE_PREFIX, explicitBucket }; }
      catch (error) { return { status: 'unavailable', reason: error instanceof GarageError ? error.code : 'garage_unavailable', ...(hostname ? { hostname } : {}), ...(bucket ? { bucket } : {}), prefix: GARAGE_PREFIX, explicitBucket }; }
    },
    async list(): Promise<GarageCatalog> {
      await checkPrivate();
      return { status: 'available', packs: receipt().objects.filter((entry): entry is Entry & { kind: 'original' | 'personal' } => entry.kind === 'original' || entry.kind === 'personal').map(({ id, version, kind, bytes, sha256 }) => ({ id, version, kind, bytes, sha256 })) };
    },
    async fetchPack(id: string, version: number): Promise<{ bytes: Buffer; sha256: string; contentType: 'application/json' }> {
      if (!PACK_IDS.includes(id as typeof PACK_IDS[number]) || !integer(version, 0x7fffffff)) return failure('garage_not_found', 'No allowlisted Garage pack exists.', 404);
      await checkPrivate();
      const entry = receipt().objects.find(item => item.kind === 'original' && item.id === id && item.version === version);
      if (!entry) return failure('garage_not_found', 'No allowlisted Garage pack exists.', 404);
      return { bytes: await verified(entry), sha256: entry.sha256, contentType: 'application/json' };
    },
    async fetchPersonal(id: string, version: number): Promise<{ packText: string; provenanceText: string }> {
      if (!PERSONAL_ID.test(id) || !integer(version, 0x7fffffff)) return failure('garage_not_found', 'No allowlisted personal pack exists.', 404);
      await checkPrivate(); const stored = receipt();
      const pack = stored.objects.find(item => item.kind === 'personal' && item.id === id && item.version === version);
      const provenance = stored.objects.find(item => item.kind === 'provenance' && item.id === id && item.version === version);
      if (!pack || !provenance) return failure('garage_not_found', 'No allowlisted personal pack exists.', 404);
      const packText = utf8(await verified(pack));
      const provenanceText = utf8(await verified(provenance));
      await validatePersonal(packText, provenanceText);
      return { packText, provenanceText };
    },
    async verify(): Promise<{ objects: number; bytes: number }> {
      await checkPrivate(); const stored = receipt();
      for (const entry of stored.objects) await verified(entry);
      return { objects: stored.objects.length, bytes: stored.objects.reduce((sum, item) => sum + item.bytes, 0) };
    },
    async upload(): Promise<GarageReceipt> {
      // Guard BEFORE creating a plan or making any write; recheck before each PUT.
      await checkPrivate(); const plan = await buildGaragePlan(rootDir);
      for (const entry of plan.objects) {
        let existing;
        try { existing = await transport!.head(bucket, entry.key); }
        catch { return failure('garage_unavailable', 'Garage object existence could not be checked; nothing was overwritten.'); }
        if (existing) {
          if (existing.bytes !== entry.bytes) failure('garage_remote_conflict', 'An existing Garage object has conflicting bytes; nothing was overwritten.', 409);
          await verified(entry); continue;
        }
        await checkPrivate();
        try { await transport!.put(bucket, entry.key, entry.data); }
        catch { return failure('garage_upload_failed', 'Garage upload failed or conditional creation was refused; existing objects were not intentionally overwritten.'); }
        await verified(entry);
      }
      const stored: GarageReceipt = { formatVersion: 1, bucket, prefix: GARAGE_PREFIX, objects: plan.objects.map(({ data: _data, ...entry }) => entry) };
      validateReceipt(stored, bucket); writeReceipt(receiptPath, stored); return stored;
    },
    close() { transport?.close?.(); },
  };
}
export type GarageAssets = ReturnType<typeof createGarageAssets>;

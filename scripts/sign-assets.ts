import { createHash } from 'node:crypto';
import { closeSync, existsSync, fsyncSync, lstatSync, openSync, readFileSync, renameSync, unlinkSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { MAX_CATALOG_BYTES, MAX_PACK_BYTES, PACK_IDS, BACKGROUND_PACK_IDS, inspectPack, requireDevelopment, signManifest, verifyCatalog, type AssetManifest, type SignedCatalog } from '../service/asset-signing.ts';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
/** Local fixture release build. This is not production signing or a credential generator. */
export function buildAssetCatalog(rootDir = ROOT, release?: number, { includeTestFixtures = false }: { includeTestFixtures?: boolean } = {}): { manifest: AssetManifest; catalog: SignedCatalog; changed: boolean } {
  requireDevelopment();
  const directory = join(rootDir, 'assets/packs');
  const catalogPath = join(directory, 'catalog.json');
  let existing: SignedCatalog | undefined;
  let previous: AssetManifest | undefined;
  if (existsSync(catalogPath)) {
    const info = lstatSync(catalogPath);
    if (!info.isFile() || info.size > MAX_CATALOG_BYTES) throw new Error('Existing catalog is not a regular bounded file.');
    existing = JSON.parse(readFileSync(catalogPath, 'utf8'));
    previous = verifyCatalog(existing);
  }
  const chosenRelease = release ?? previous?.release ?? 1;
  const backgrounds = BACKGROUND_PACK_IDS.filter(id => existsSync(join(directory, `${id}.json`)));
  if (backgrounds.length && backgrounds.length !== BACKGROUND_PACK_IDS.length) throw new Error('All eight background packs are required for a scene release.');
  const packIds = [...(includeTestFixtures ? PACK_IDS : []), ...backgrounds];
  const manifest: AssetManifest = { formatVersion: 1, release: chosenRelease, rulesVersion: 1, packs: packIds.map((id) => {
    const path = join(directory, `${id}.json`);
    const info = lstatSync(path);
    if (!info.isFile() || info.size < 1 || info.size > MAX_PACK_BYTES) throw new Error(`Pack must be a regular file of 1–${MAX_PACK_BYTES} bytes: ${id}.`);
    const bytes = readFileSync(path);
    if (bytes.length !== info.size || bytes.length > MAX_PACK_BYTES) throw new Error('Pack changed while loading.');
    const { version, decodedBytes } = inspectPack(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)), id);
    return { id, version, bytes: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex'), url: `/api/assets/packs/${id}/${version}`, required: id === 'starter-v2', decodedBytes };
  }) };
  if (existing && previous) {
    if (existing.payloadBase64 === Buffer.from(JSON.stringify(manifest)).toString('base64')) return { manifest, catalog: existing, changed: false };
    if (chosenRelease <= previous.release) throw new Error('Changed assets require a higher manifest release; pass --release N.');
    for (const entry of manifest.packs) {
      const old = previous.packs.find((candidate) => candidate.id === entry.id);
      if (old && (entry.version < old.version || (entry.sha256 !== old.sha256 && entry.version === old.version))) throw new Error(`Changed pack ${entry.id} requires a higher pack version to preserve immutable URLs.`);
    }
  }
  const catalog = signManifest(manifest);
  const temporary = join(directory, `.catalog-${process.pid}.tmp`);
  let fd: number | undefined;
  try {
    fd = openSync(temporary, 'wx', 0o644);
    writeFileSync(fd, `${JSON.stringify(catalog, null, 2)}\n`);
    fsyncSync(fd); closeSync(fd); fd = undefined;
    renameSync(temporary, catalogPath);
    const directoryFd = openSync(directory, 'r');
    try { fsyncSync(directoryFd); } finally { closeSync(directoryFd); }
  } finally {
    if (fd !== undefined) closeSync(fd);
    if (existsSync(temporary)) unlinkSync(temporary);
  }
  return { manifest, catalog, changed: true };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const includeTestFixtures = process.argv.includes('--include-test-fixtures');
  const args = process.argv.slice(2).filter(arg => arg !== '--include-test-fixtures');
  if (args.length && (args.length !== 2 || args[0] !== '--release' || !/^[1-9]\d{0,9}$/.test(args[1]))) throw new Error('Usage: ./scripts/node.sh scripts/sign-assets.ts [--release N] [--include-test-fixtures]');
  const result = buildAssetCatalog(ROOT, args.length ? Number(args[1]) : undefined, { includeTestFixtures });
  console.log(`Public TEST key asset catalog ${result.changed ? 'signed' : 'unchanged'}: release ${result.manifest.release}; ${result.manifest.packs.length} packs, ${result.manifest.packs.reduce((sum, pack) => sum + pack.bytes, 0)} bytes. Not production trust.`);
}

import { createHash } from 'node:crypto';
import { existsSync, lstatSync, readFileSync, realpathSync } from 'node:fs';
import { join, resolve } from 'node:path';

// Private artwork is optional. This module has no network or upload capability.
// The HTTP caller must enforce the normal local-network policy and paired auth.
const LIMITS = Object.freeze({ index: 1024 * 1024, entries: 512, browser: 256 * 1024, provenance: 64 * 1024 });
type Descriptor = { file: string; bytes: number; sha256: string };
type Entry = { formId: number; artId: string; version: number; browser: Descriptor; provenance: Descriptor };
export type WorldDsAppearance = { status: 'unavailable' | 'private-local-available'; artId: string; version?: number; url?: string };
function object(value: unknown): value is Record<string, unknown> { return !!value && typeof value === 'object' && !Array.isArray(value); }
function integer(value: unknown, max: number): value is number { return Number.isSafeInteger(value) && Number(value) >= 1 && Number(value) <= max; }
function check(value: unknown, message: string): asserts value { if (!value) throw new Error(`Private World DS art: ${message}`); }
function hash(bytes: Buffer) { return createHash('sha256').update(bytes).digest('hex'); }
function bounded(path: string, maximum: number): Buffer {
  const info = lstatSync(path);
  check(info.isFile() && !info.isSymbolicLink() && info.size > 0 && info.size <= maximum, 'expected a bounded regular file');
  const data = readFileSync(path);
  check(data.length === info.size && data.length <= maximum, 'file changed during read');
  return data;
}

/** Index metadata stays small; exactly one requested form's bytes are read. */
export function createWorldDsAssetService({ rootDir }: { rootDir: string }) {
  const root = realpathSync(rootDir);
  const directory = join(root, '.personal-assets', 'world-ds');
  const indexPath = join(directory, 'index.json');
  const entries = new Map<number, Entry>();
  if (existsSync(indexPath)) {
    check(realpathSync(directory) === resolve(directory), 'private directory cannot contain symlinks');
    const index: unknown = JSON.parse(bounded(indexPath, LIMITS.index).toString('utf8'));
    check(object(index) && index.formatVersion === 1 && index.collection === 'digimon-world-ds' &&
      index.privateOnly === true && index.license === 'LicenseRef-Personal-Use-Restrictions' &&
      integer(index.version, 0x7fffffff) && Array.isArray(index.entries) && index.entries.length <= LIMITS.entries,
    'unsupported private index');
    for (const row of index.entries) {
      check(object(row) && typeof row.artId === 'string' && /^ds-form-[1-9][0-9]{0,2}$/.test(row.artId) &&
        integer(row.version, 0x7fffffff), 'invalid form identity');
      const formId = Number(row.artId.slice(8));
      check(integer(formId, 512) && !entries.has(formId), 'duplicate or unsupported form');
      function descriptor(value: unknown, filename: string, maximum: number): Descriptor {
        check(object(value) && typeof value.file === 'string' && integer(value.bytes, maximum) &&
          typeof value.sha256 === 'string' && /^[a-f0-9]{64}$/.test(value.sha256), 'invalid blob descriptor');
        const expected = new RegExp(`^objects/${row.artId}-v${row.version}-[a-f0-9]{16}/${filename.replace('.', '\\.')}$`);
        check(expected.test(value.file), 'blob path does not match its immutable form identity');
        return { file: value.file, bytes: value.bytes, sha256: value.sha256 };
      }
      entries.set(formId, { formId, artId: row.artId, version: row.version,
        browser: descriptor(row.browser, 'pack.json', LIMITS.browser),
        provenance: descriptor(row.provenance, 'provenance.json', LIMITS.provenance) });
    }
  }
  function describe(formId: number): WorldDsAppearance {
    const entry = entries.get(formId);
    return entry ? { status: 'private-local-available', artId: entry.artId, version: entry.version, url: `/api/roster/art/${formId}` }
      : { status: 'unavailable', artId: `ds-form-${formId}` };
  }
  function loadBrowser(formId: number) {
    const entry = entries.get(formId);
    if (!entry) return null;
    function read(descriptor: Descriptor, maximum: number): Buffer {
      const path = join(directory, descriptor.file);
      check(realpathSync(path) === resolve(path), 'blob path cannot contain symlinks');
      const bytes = bounded(path, maximum);
      check(bytes.length === descriptor.bytes && hash(bytes) === descriptor.sha256, 'blob hash or length mismatch');
      return bytes;
    }
    const pack = read(entry.browser, LIMITS.browser);
    const provenance = read(entry.provenance, LIMITS.provenance);
    const parsed: unknown = JSON.parse(pack.toString('utf8'));
    check(object(parsed) && object(parsed.sprites) && Object.keys(parsed.sprites).length === 1 &&
      Object.hasOwn(parsed.sprites, entry.artId), 'pack must contain exactly the requested form');
    return { formId, artId: entry.artId, version: entry.version, sha256: entry.browser.sha256,
      packText: pack.toString('utf8'), provenanceText: provenance.toString('utf8') };
  }
  return { describe, loadBrowser };
}

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createWorldDsAssetService } from '../service/world-ds-assets.ts';

function fixture() {
  const root = mkdtempSync(join(tmpdir(), 'digivice-private-art-'));
  const folder = join(root, '.personal-assets/world-ds');
  const objectPath = 'objects/ds-form-276-v1-1234567890abcdef';
  mkdirSync(join(folder, objectPath), { recursive: true });
  function blob(name: string, text: string) {
    writeFileSync(join(folder, objectPath, name), text);
    return { file: `${objectPath}/${name}`, bytes: Buffer.byteLength(text), sha256: createHash('sha256').update(text).digest('hex') };
  }
  const entry = { artId: 'ds-form-276', version: 1,
    browser: blob('pack.json', JSON.stringify({ sprites: { 'ds-form-276': { name: 'Synthetic fixture' } } })),
    provenance: blob('provenance.json', JSON.stringify({ fixture: 'original synthetic metadata; no franchise artwork' })) };
  const index = { formatVersion: 1, collection: 'digimon-world-ds', privateOnly: true,
    license: 'LicenseRef-Personal-Use-Restrictions', version: 1, entries: [entry] };
  const save = () => writeFileSync(join(folder, 'index.json'), JSON.stringify(index));
  save();
  return { root, folder, entry, index, save, close: () => rmSync(root, { recursive: true, force: true }) };
}

test('private art is optional and high form IDs load only their exact audited object', () => {
  const f = fixture();
  try {
    const service = createWorldDsAssetService({ rootDir: f.root });
    assert.equal(service.describe(276).status, 'private-local-available');
    assert.equal(service.describe(275).status, 'unavailable');
    assert.equal(service.loadBrowser(275), null);
    assert.equal(service.loadBrowser(276)?.artId, 'ds-form-276');
    assert.equal(service.loadBrowser(276)?.sha256, f.entry.browser.sha256);
    rmSync(join(f.folder, f.entry.browser.file));
    assert.equal(service.describe(275).status, 'unavailable');
    assert.throws(() => service.loadBrowser(276));
  } finally { f.close(); }
});

test('changed bytes fail integrity without activating replacement art', () => {
  const f = fixture();
  try {
    const service = createWorldDsAssetService({ rootDir: f.root });
    writeFileSync(join(f.folder, f.entry.browser.file), '{}');
    assert.throws(() => service.loadBrowser(276), /hash or length/);
  } finally { f.close(); }
});

test('index rejects duplicate IDs, path traversal and over-budget objects before loading', () => {
  const f = fixture();
  try {
    f.index.entries.push(f.entry); f.save();
    assert.throws(() => createWorldDsAssetService({ rootDir: f.root }), /duplicate/);
    f.index.entries.pop();
    const original = f.entry.browser.file;
    f.entry.browser.file = '../outside.json'; f.save();
    assert.throws(() => createWorldDsAssetService({ rootDir: f.root }), /path/);
    f.entry.browser.file = original;
    f.entry.browser.bytes = 256 * 1024 + 1; f.save();
    assert.throws(() => createWorldDsAssetService({ rootDir: f.root }), /descriptor/);
  } finally { f.close(); }
});

test('symlinked blob cannot disclose another local file, even with matching indexed hash', () => {
  const f = fixture();
  try {
    const original = join(f.folder, f.entry.browser.file), outside = join(f.root, 'private.json');
    writeFileSync(outside, readFileSync(original));
    rmSync(original); symlinkSync(outside, original);
    assert.throws(() => createWorldDsAssetService({ rootDir: f.root }).loadBrowser(276), /symlink/);
  } finally { f.close(); }
});

test('a form cannot masquerade as a different sprite identity inside a valid-hash pack', () => {
  const f = fixture();
  try {
    const bytes = JSON.stringify({ sprites: { 'ds-form-18': {} } });
    writeFileSync(join(f.folder, f.entry.browser.file), bytes);
    f.entry.browser.bytes = Buffer.byteLength(bytes);
    f.entry.browser.sha256 = createHash('sha256').update(bytes).digest('hex'); f.save();
    assert.throws(() => createWorldDsAssetService({ rootDir: f.root }).loadBrowser(276), /exactly the requested/);
  } finally { f.close(); }
});

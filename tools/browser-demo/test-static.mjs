#!/usr/bin/env node
// Verify the package that will actually ship under a GitHub Pages project path.
import assert from 'node:assert/strict';
import { readFile, readdir, stat, writeFile } from 'node:fs/promises';
import { dirname, resolve, relative, extname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const site = resolve(root, 'docs');
const play = resolve(site, 'play');
const base = 'https://example.invalid/z-ark-digivice/play/';
const source = await readFile(resolve(play, 'index.html'), 'utf8');
const ids = [...source.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
assert.equal(new Set(ids).size, ids.length, 'HTML IDs must be unique');
assert.match(source, /<html\s+lang="en"/);
assert.match(source, /name="viewport"/);
const checked = new Set();
async function reference(value, document = resolve(play, 'index.html'), requireLocal = false) {
  if (/^(?:data:|mailto:|tel:)/.test(value)) return;
  const owner = new URL(relative(site, document), 'https://example.invalid/z-ark-digivice/');
  const url = new URL(value, owner);
  if (url.origin !== owner.origin) {
    assert(!requireLocal, `Executable/style assets must be self-contained: ${value}`);
    assert.equal(url.protocol, 'https:');
    return;
  }
  assert(url.pathname.startsWith('/z-ark-digivice/'), `Reference must preserve the Pages project prefix: ${value}`);
  let path = resolve(site, decodeURIComponent(url.pathname.slice('/z-ark-digivice/'.length)));
  if ((await stat(path)).isDirectory()) path = resolve(path, 'index.html');
  checked.add(relative(site, path));
  if (url.hash && extname(path) === '.html') {
    const html = await readFile(path, 'utf8');
    const target = decodeURIComponent(url.hash.slice(1));
    assert(html.includes(`id="${target}"`) || html.includes(`id='${target}'`), `Missing anchor ${value}`);
  }
}
for (const match of source.matchAll(/<(a|link|script)\b[^>]*\b(?:href|src)="([^"]+)"[^>]*>/g))
  await reference(match[2], undefined, match[1] !== 'a');
for (const match of source.matchAll(/\b(?:aria-controls|aria-labelledby)="([^"]+)"/g))
  for (const id of match[1].split(/\s+/)) assert(ids.includes(id), `ARIA target ${id} must exist`);

async function inventory(directory) {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = resolve(directory, entry.name);
    assert(!entry.isSymbolicLink(), `Release package must not rely on symlinks: ${path}`);
    if (entry.isDirectory()) files.push(...await inventory(path));
    else if (entry.isFile()) files.push(path);
  }
  return files;
}
const files = await inventory(play);
const sizes = [];
for (const path of files) {
  const data = await readFile(path);
  sizes.push({ path: relative(site, path), bytes: data.byteLength, gzipBytes: gzipSync(data).byteLength });
  assert(!/\.(?:mov|mp4|dva|zip|sqlite|pem|key)$/i.test(path), `The playable package must not bundle private media, packs or credentials: ${path}`);
  if (extname(path) === '.js' && !path.includes('/runtime/')) {
    const javascript = data.toString('utf8');
    for (const match of javascript.matchAll(/(?:\bfrom\s*|\bimport\s*\(?\s*)['"]([^'"\n]+)['"]/g))
      await reference(match[1], path, true);
    assert(!/['"]\/api\//.test(javascript), `Static runtime must not depend on local server API: ${path}`);
    assert(!/localStorage\.clear\s*\(/.test(javascript), `Reset must preserve other site storage: ${path}`);
  }
  if (extname(path) === '.css') {
    for (const match of data.toString('utf8').matchAll(/url\(\s*['"]?([^'"\s)]+)['"]?\s*\)/g))
      await reference(match[1], path, true);
  }
}
const wasm = await readFile(resolve(play, 'runtime/demo-core.wasm'));
assert.deepEqual([...wasm.subarray(0, 8)], [0, 97, 115, 109, 1, 0, 0, 0], 'Real WebAssembly v1 binary must be present');
assert(WebAssembly.validate(wasm), 'Shipped WebAssembly must validate');
const totalBytes = sizes.reduce((sum, file) => sum + file.bytes, 0);
assert(totalBytes < 5 * 1024 * 1024, 'Self-contained demo must stay within a 5 MiB uncompressed package budget');
const report = { valid: true, simulatedPagesUrl: base, ids: ids.length, resolvedReferences: checked.size, totalBytes, gzipBytes: sizes.reduce((sum, file) => sum + file.gzipBytes, 0), files: sizes };
if (process.argv[2]) await writeFile(resolve(process.argv[2]), `${JSON.stringify(report, null, 2)}\n`);
console.log(`PASS static demo package: ${files.length} files, ${checked.size} references, ${ids.length} unique IDs, ${totalBytes} bytes`);

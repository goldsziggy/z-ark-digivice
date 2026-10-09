#!/usr/bin/env node
// Run existing product tests against the actual files shipped in docs/play.
// Only ephemeral copies of test import statements change; assertions stay intact.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const cases = [
  ['capture-ring-input.test.mjs', 'capture-ring-input.js'],
  ['capture-ring-model.test.mjs', 'capture-ring.js'],
  ['party.test.mjs', 'party.js'],
  ['starter-onboarding.test.mjs', 'starter-onboarding.js'],
];
const directory = await mkdtemp(join(tmpdir(), 'digivice-demo-shared-'));
try {
  const files = [];
  for (const [test, helper] of cases) {
    const shippedPath = resolve(root, 'docs/play/shared', helper);
    const original = await readFile(resolve(root, 'web', helper));
    const shipped = await readFile(shippedPath);
    assert(original.equals(shipped), `${helper} must remain byte-identical to the shared product helper`);
    const contents = await readFile(resolve(root, 'tests', test), 'utf8');
    const imported = `'../web/${helper}'`;
    assert(contents.includes(imported), `${test} must import the expected product helper`);
    const redirected = contents.replace(imported, `'${pathToFileURL(shippedPath).href}'`);
    const target = join(directory, test);
    await writeFile(target, redirected);
    files.push(target);
  }
  const result = spawnSync(process.execPath, ['--test', ...files], { cwd: root, stdio: 'inherit' });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exitCode = result.status || 1;
} finally {
  await rm(directory, { recursive: true, force: true });
}

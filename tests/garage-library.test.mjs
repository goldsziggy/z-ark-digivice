import test from 'node:test';
import assert from 'node:assert/strict';
import { readBoundedJSON, validateGarageCatalog } from '../web/garage-library.js';

const entry = { id: 'personal-example', version: 1, kind: 'personal', bytes: 4096, sha256: 'a'.repeat(64) };

test('private catalog constrains route identities, count, bytes and hash pins', () => {
  assert.equal(validateGarageCatalog({ status: 'available', packs: [entry] }).packs[0], entry);
  for (const patch of [{ id: '../secret' }, { id: 'https://elsewhere.invalid' }, { version: 0 }, { version: 1.5 }, { bytes: 262145 }, { bytes: -1 }, { sha256: 'not-a-hash' }, { kind: 'script' }]) {
    assert.throws(() => validateGarageCatalog({ status: 'available', packs: [{ ...entry, ...patch }] }));
  }
  assert.throws(() => validateGarageCatalog({ status: 'available', packs: [entry, entry] }));
  assert.throws(() => validateGarageCatalog({ status: 'available', packs: Array.from({ length: 9 }, (_, index) => ({ ...entry, id: `personal-${index}` })) }));
});

test('bounded response decoding cancels oversize streams and rejects malformed UTF-8/JSON', async () => {
  assert.deepEqual(await readBoundedJSON(new Response('{"ok":true}'), 20), { ok: true });
  let cancelled = false;
  const body = new ReadableStream({ start(controller) { controller.enqueue(new Uint8Array(21)); }, cancel() { cancelled = true; } });
  await assert.rejects(readBoundedJSON(new Response(body), 20), /exceeds its limit/);
  assert.equal(cancelled, true);
  await assert.rejects(readBoundedJSON(new Response(new Uint8Array([0xff]))), /invalid JSON/);
  await assert.rejects(readBoundedJSON(new Response('{broken')), /invalid JSON/);
});

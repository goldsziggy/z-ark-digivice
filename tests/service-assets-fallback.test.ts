import test from 'node:test';
import assert from 'node:assert/strict';
import { cpSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { startServer } from '../service/server.ts';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));

test('corrupt optional art fails asset routes closed while game pairing, saves and replay remain usable', async () => {
  const fixture = mkdtempSync(join(tmpdir(), 'digivice-bad-optional-'));
  cpSync(join(root, 'assets/packs'), join(fixture, 'assets/packs'), { recursive: true });
  writeFileSync(join(fixture, 'assets/packs/ember-v1.json'), '{truncated');
  const app = await startServer({ seedSource: () => 12345, includeTestFixtures: true, rootDir: fixture, dataDir: join(fixture, '.data'), corePath: process.env.DIGIVICE_TEST_CORE_PATH ?? join(root, 'build/digivice-core'), port: 0 });
  const address = app.server.address(); assert.ok(address && typeof address !== 'string');
  const base = `http://127.0.0.1:${address.port}`;
  const post = (path: string, value: unknown, token?: string) => fetch(base + path, {
    method: 'POST', headers: { 'Content-Type': 'application/json', ...(token ? { Authorization: `Bearer ${token}` } : {}) }, body: JSON.stringify(value),
  });
  try {
    const health = await fetch(base + '/api/health');
    assert.equal(health.status, 200); assert.equal((await health.json()).assetStatus, 'unavailable');
    assert.equal((await fetch(base + '/api/assets/catalog')).status, 503);
    assert.equal((await fetch(base + '/api/assets/packs/starter-v2/1')).status, 503);
    const pairing = await (await post('/api/pairing/start', {})).json();
    const identity = await (await post('/api/pairing/claim', { code: pairing.code })).json();
    const saved = await post('/api/save-sync', { rulesVersion: 15, baseRevision: 0, batchId: 'asset-fallback-care-1', events: [{ type: 'hatch', value: 1 }, { type: 'feed', value: 0 }] }, identity.token);
    assert.equal(saved.status, 200); assert.equal((await saved.json()).revision, 1);
    const restored = await fetch(base + '/api/save', { headers: { Authorization: `Bearer ${identity.token}` } });
    assert.equal(restored.status, 200); assert.equal((await restored.json()).state.sequence, 2);
  } finally { await new Promise<void>(resolve => app.server.close(() => resolve())); app.close(); rmSync(fixture, { recursive: true, force: true }); }
});

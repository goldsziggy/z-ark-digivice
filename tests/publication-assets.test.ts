import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { createAssetService } from '../service/asset-service.ts';
import { createDeviceAssetService, verifyDeviceCatalog } from '../service/device-assets.ts';
import { verifyCatalog, PACK_IDS, BACKGROUND_PACK_IDS } from '../service/asset-signing.ts';
const rootDir = fileURLToPath(new URL('..', import.meta.url));
for (const includeTestFixtures of [false, true]) test(`publication catalogs: fixtures ${includeTestFixtures ? 'explicitly enabled' : 'disabled by default'}`, async t => {
  const browser = createAssetService({ rootDir, includeTestFixtures });
  const device = createDeviceAssetService({ rootDir, includeTestFixtures });
  const server = createServer((req, res) => {
    const path = (req.url ?? '').split('?')[0];
    if (!browser.handleAssetRequest(req, res, path) && !device.handleDeviceAssetRequest(req, res, path)) { res.writeHead(404); res.end(); }
  });
  await new Promise<void>((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  t.after(() => new Promise<void>((resolve, reject) => server.close(error => error ? reject(error) : resolve())));
  const base = `http://127.0.0.1:${(server.address() as {port: number}).port}`;
  const browserCatalog = verifyCatalog(await (await fetch(base + '/api/assets/catalog')).json());
  const deviceCatalog = verifyDeviceCatalog(await (await fetch(base + '/api/device/assets/catalog')).json());
  assert.equal(browserCatalog.packs.length, includeTestFixtures ? 3 : 0);
  assert.equal(deviceCatalog.packs.length, includeTestFixtures ? 10 : 0);
  for (const entry of [...browserCatalog.packs, ...deviceCatalog.packs]) {
    const response = await fetch(base + entry.url); assert.equal(response.status, 200);
    const bytes = Buffer.from(await response.arrayBuffer()); assert.equal(bytes.length, entry.bytes);
    assert.equal(createHash('sha256').update(bytes).digest('hex'), entry.sha256);
  }
  for (const id of BACKGROUND_PACK_IDS) {
    assert.equal((await fetch(`${base}/api/assets/packs/${id}/1`)).status, 404);
    assert.equal((await fetch(`${base}/api/device/assets/packs/${id.replace('-v1','-412-v1')}/1`)).status, 404);
  }
  if (!includeTestFixtures) for (const id of PACK_IDS) assert.equal((await fetch(`${base}/api/assets/packs/${id}/${id === 'starter-v2' ? 2 : 1}`)).status, 404);
});

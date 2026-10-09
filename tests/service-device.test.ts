import test from 'node:test';
import assert from 'node:assert/strict';
import { Readable } from 'node:stream';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { createApp, lanOptionsFromEnv, type LanOptions } from '../service/server.ts';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
function fixture(t: { after: (fn: () => void) => unknown }, lan?: LanOptions, missingAssets = false) {
  const dataDir = mkdtempSync(join(tmpdir(), 'digivice-lan-policy-'));
  const app = createApp({ dataDir, rootDir: missingAssets ? dataDir : ROOT, corePath: process.env.DIGIVICE_TEST_CORE_PATH ?? join(ROOT, 'build/digivice-core'), lan });
  t.after(() => { app.close(); rmSync(dataDir, { recursive: true, force: true }); });
  async function request(path: string, { peer = '127.0.0.1', host = '127.0.0.1:8787', origin, input, token, headers = {} }: {
    peer?: string; host?: string; origin?: string; input?: unknown; token?: string; headers?: Record<string, string>;
  } = {}) {
    const encoded = input === undefined ? '' : JSON.stringify(input);
    const incoming = Object.assign(Readable.from(encoded ? [Buffer.from(encoded)] : []), {
      url: path, method: input === undefined ? 'GET' : 'POST', socket: { remoteAddress: peer },
      headers: { host, ...(origin ? { origin } : {}), ...(encoded ? { 'content-type': 'application/json' } : {}), ...(token ? { authorization: `Bearer ${token}` } : {}), ...headers },
    }) as unknown as IncomingMessage;
    let status = 200, raw = ''; const responseHeaders: Record<string, unknown> = {};
    const response = { setHeader(key: string, value: unknown) { responseHeaders[key.toLowerCase()] = value; },
      writeHead(code: number, values: Record<string, unknown>) { status = code; for (const [key, value] of Object.entries(values)) responseHeaders[key.toLowerCase()] = value; },
      end(value?: string | Buffer) { raw = value?.toString() ?? ''; },
    } as unknown as ServerResponse;
    await app.handler(incoming, response);
    return { status, body: raw ? JSON.parse(raw) : null, headers: responseHeaders };
  }
  return { request };
}

test('LAN configuration is explicit, private-interface-only and refuses broad or malformed allowlists', () => {
  assert.equal(lanOptionsFromEnv({}, 8787), undefined);
  for (const bind of ['0.0.0.0', '127.0.0.1', '8.8.8.8', '192.168.01.2', '::', 'digivice.local']) assert.throws(() => lanOptionsFromEnv({ DIGIVICE_LAN_BIND: bind }, 8787));
  assert.throws(() => lanOptionsFromEnv({ DIGIVICE_LAN_HOSTS: 'localhost:8787' }, 8787));
  for (const hosts of ['*', 'attacker.example:8787', '192.168.1.8:8787/path', '192.168.1.8:8787,']) {
    assert.throws(() => lanOptionsFromEnv({ DIGIVICE_LAN_BIND: '192.168.1.8', DIGIVICE_LAN_HOSTS: hosts }, 8787));
  }
  for (const origin of ['*', 'http://attacker.example', 'http://192.168.1.8:8787/path', 'https://192.168.1.8:8787']) {
    assert.throws(() => lanOptionsFromEnv({ DIGIVICE_LAN_BIND: '192.168.1.8', DIGIVICE_LAN_ORIGINS: origin }, 8787));
  }
  const valid = lanOptionsFromEnv({ DIGIVICE_LAN_BIND: '192.168.1.8', DIGIVICE_LAN_HOSTS: '192.168.1.8:8787,digivice.local:8787' }, 8787)!;
  assert.deepEqual(valid.allowedOrigins, ['http://192.168.1.8:8787', 'http://digivice.local:8787']);
});

test('default loopback and device health distinguish service readiness from asset availability', async t => {
  const f = fixture(t, undefined, true);
  const ready = await f.request('/api/device/health');
  assert.equal(ready.status, 200);
  assert.deepEqual(ready.body, { status: 'ok', protocolVersion: 1, gameRulesVersion: 13, gameSchemaVersion: 17, assetProfile: 's3-146-v1' });
  assert.equal((await f.request('/api/device/assets/catalog')).status, 503);
  assert.equal((await f.request('/api/device/health', { peer: '192.168.1.21', headers: { 'x-forwarded-for': '127.0.0.1' } })).status, 403);
  assert.equal((await f.request('/api/device/health', { host: '192.168.1.8:8787' })).status, 403);
  assert.equal((await f.request('/api/device/health?latitude=1')).status, 400);
});

test('LAN policy preserves bearer save isolation and confines enrollment to actual loopback peers', async t => {
  const lan = lanOptionsFromEnv({ DIGIVICE_LAN_BIND: '192.168.1.8' }, 8787)!;
  const f = fixture(t, lan);
  const network = { peer: '192.168.1.21', host: '192.168.1.8:8787' };
  assert.equal((await f.request('/api/device/health', network)).status, 200);
  assert.equal((await f.request('/api/device/assets/catalog', network)).status, 200);
  for (const override of [{ host: 'attacker.example:8787' }, { origin: 'http://attacker.example' }, { peer: '8.8.8.8' }, { headers: { 'sec-fetch-site': 'cross-site' } }]) {
    assert.equal((await f.request('/api/device/health', { ...network, ...override })).status, 403);
  }
  assert.equal((await f.request('/api/save', network)).status, 401);
  for (const path of ['/api/pairing/start', '/api/pairing/claim']) {
    const rejected = await f.request(path, { ...network, input: {}, headers: { 'x-forwarded-for': '127.0.0.1' } });
    assert.equal(rejected.status, 403); assert.equal(rejected.body.error, 'local_enrollment_only');
  }
  const start = await f.request('/api/pairing/start', { input: {} }); assert.equal(start.status, 201);
  const paired = await f.request('/api/pairing/claim', { input: { code: start.body.code } }); assert.equal(paired.status, 201);
  const saved = await f.request('/api/save-sync', { ...network, token: paired.body.token,
    input: { rulesVersion: 13, baseRevision: 0, batchId: 'lan-existing-token-1', events: [{ type: 'hatch', value: 1 }] } });
  assert.equal(saved.status, 200); assert.equal(saved.body.deviceId, paired.body.deviceId); assert.equal(saved.body.revision, 1);
  assert.equal(saved.headers['access-control-allow-origin'], undefined);
  assert.equal((await f.request('/api/save', { ...network, token: 'a'.repeat(43) })).status, 401);
});

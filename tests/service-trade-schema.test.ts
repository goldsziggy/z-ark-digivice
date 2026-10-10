import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { historicComparable } from './legacy-state-projection.ts';

const rootDir = resolve(import.meta.dirname, '..');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/schema17-trade-migration.json'), 'utf8'));
const token = Buffer.alloc(32, 61).toString('base64url'); // Synthetic test identity only.
const hash = (value: string) => createHash('sha256').update(value).digest('hex');
const initial = [{ type: 'hatch', value: 1 }, { type: 'feed', value: 0 }];
const suffix = [{ type: 'encounter-seed', value: 12345 }, { type: 'accrue-steps', value: 1000 }];
const receipt = (rulesVersion: number, baseRevision: number, batchId: string, events: typeof initial) => ({
  batchId, bodyHash: hash(JSON.stringify({ rulesVersion, baseRevision, events })), revision: baseRevision + 1, eventEnd: events.length,
});

for (const archivedBaseline of [false, true]) test(`schema17 upgrades metadata without changing ${archivedBaseline ? 'archived baseline or' : 'current'} rules13 receipts`, async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-schema18-'));
  const events = archivedBaseline ? suffix : [...initial, ...suffix];
  const baseRevision = archivedBaseline ? 1 : 0;
  const savedReceipt = receipt(13, baseRevision, 'frozen-thirteen-steps', events);
  const original = { formatVersion: 15, gameSchemaVersion: 17, rulesVersion: 13, devices: [{
    deviceId: `dv_${'e'.repeat(24)}`, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding', revision: baseRevision + 1,
    legacy: archivedBaseline ? { histories: [{ rulesVersion: 12, events: initial, receipts: [receipt(12, 0, 'older-twelve-care', initial)] }], snapshotBase64: frozen.cases.care.snapshotBase64 } : null,
    events, receipts: [savedReceipt],
  }] };
  const originalText = JSON.stringify(original, null, 2) + '\n';
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), originalText);
  const options = { seedSource: () => 12345, rootDir, dataDir, port: 0, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH };
  let app = await startServer(options);
  const close = async () => { await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve())); app.close(); };
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${body === undefined ? '/api/save' : '/api/save-sync'}`, {
      method: body === undefined ? 'GET' : 'POST', headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    return { status: response.status, body: await response.json() as any };
  };
  const saved = await request(); assert.equal(saved.status, 200);
  assert.equal(saved.body.state.maxLevel, 50); assert.equal(saved.body.state.schemaVersion, 26); assert.equal(saved.body.state.rulesVersion, 19);
  assert.deepEqual(historicComparable(saved.body.state), historicComparable({ ...frozen.cases.pending.state, schemaVersion: 26, rulesVersion: 19, collectionCapacity: 60, partyCapacity: 3, partyMemberIds: [], receivedTrades: 0, autoCapture: 0, worldSeed: 0 }));
  assert.equal(saved.body.revision, original.devices[0].revision);
  const upgraded = JSON.parse(await readFile(join(dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([upgraded.formatVersion, upgraded.gameSchemaVersion, upgraded.rulesVersion], [21, 26, 19]);
  assert.deepEqual(upgraded.devices[0].legacy.histories, [...(original.devices[0].legacy?.histories ?? []), { rulesVersion: 13, events, receipts: [savedReceipt] }]);
  assert.deepEqual(upgraded.devices[0].events, []); assert.deepEqual(upgraded.devices[0].receipts, []);
  assert.equal(await readFile(join(dataDir, 'store.rules-v13.json'), 'utf8'), originalText);
  assert.deepEqual(await readFile(join(dataDir, 'store.json')), await readFile(join(dataDir, 'store.backup.json')));
  const retry = { rulesVersion: 13, baseRevision, batchId: savedReceipt.batchId, events };
  const accepted = await request(retry); assert.equal(accepted.status, 409); assert.equal(accepted.body.error, 'migration_required');
  assert.equal((await request({ ...retry, rulesVersion: 19 })).body.error, 'legacy_batch_requires_reconciliation');
  await close(); app = await startServer(options);
  assert.deepEqual(await request(), saved); assert.deepEqual(await request(retry), accepted);
  // Native trading is not an HTTP event or an unvalidated snapshot import.
  for (const forbidden of [{ type: 'trade', value: 1 }, { type: 'received-trades', value: 1 }]) {
    const rejected = await request({ rulesVersion: 19, baseRevision: saved.body.revision, batchId: 'no-network-trade-import', events: [forbidden] });
    assert.equal(rejected.status, 422); assert.deepEqual(await request(), saved);
  }
});

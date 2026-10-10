import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createApp, startServer } from '../service/server.ts';
import { historicComparable } from './legacy-state-projection.ts';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const token = Buffer.alloc(32, 64).toString('base64url'); // Synthetic identity; temporary files only.
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
type Event = { type: string; value: number };
const frozen = (events: Event[]) => JSON.parse(execFileSync(corePath, ['--replay-v13-onboarding-trace', '12345'], {
  input: events.map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', maxBuffer: 128 * 1024,
}));

async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, events: Event[]) {
  const receipts = [];
  for (let offset = 0; offset < events.length; offset += 100) {
    const chunk = events.slice(offset, offset + 100), revision = receipts.length;
    receipts.push({ batchId: `archived-thirteen-${revision}`, revision: revision + 1, eventEnd: offset + chunk.length,
      bodyHash: hash(JSON.stringify({ rulesVersion: 13, baseRevision: revision, events: chunk })) });
  }
  const original = { formatVersion: 15, gameSchemaVersion: 20, rulesVersion: 13, devices: [{
    deviceId: `dv_${'6'.repeat(24)}`, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding', revision: receipts.length,
    legacy: null, events, receipts,
  }] };
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-roster60-service-'));
  const originalText = JSON.stringify(original, null, 2) + '\n';
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), originalText);
  const options = { rootDir, corePath, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 };
  let app = await startServer(options);
  const close = async () => { await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve())); app.close(); };
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown, path = body === undefined ? '/api/save' : '/api/save-sync') => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, {
      method: body === undefined ? 'GET' : 'POST', headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    return { status: response.status, body: await response.json() as any };
  };
  return { request, dataDir, original, originalText, restart: async () => { await close(); app = await startServer(options); } };
}

test('rules13 full-eight Auto history stays exact before expansion; archived retries never run under sixty', async t => {
  const events: Event[] = [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }];
  let old = frozen(events);
  for (let attempt = 0; attempt < 64 && old.state.collection.length < 8; ++attempt) {
    events.push(...Array.from({ length: old.state.recoveryRestCount }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }, { type: 'auto', value: 0 });
    old = frozen(events);
  }
  assert.equal(old.state.collection.length, 8);
  // This action must not gain a ninth member while reconstructing old history.
  events.push(...Array.from({ length: old.state.recoveryRestCount }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }, { type: 'auto', value: 0 });
  old = frozen(events); assert.equal(old.state.collection.length, 8); assert.equal(old.state.captures, 7);
  const f = await fixture(t, events), saved = await f.request(); assert.equal(saved.status, 200);
  assert.equal(saved.body.state.maxLevel, 50); assert.equal(saved.body.state.schemaVersion, 24); assert.equal(saved.body.state.rulesVersion, 17);
  assert.deepEqual(historicComparable(saved.body.state), historicComparable({ ...old.state, schemaVersion: 24, rulesVersion: 17, collectionCapacity: 60, partyCapacity: 3, partyMemberIds: [] }));
  assert.deepEqual(saved.body.autoTrace, old.trace);
  assert.deepEqual(saved.body.events, []); assert.equal(saved.body.baseSequence, events.length);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual(stored.devices[0].legacy.histories, [{ rulesVersion: 13, events, receipts: f.original.devices[0].receipts }]);
  assert.equal(Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64').length, 3216);
  assert.equal(await readFile(join(f.dataDir, 'store.rules-v13.json'), 'utf8'), f.originalText);
  const first = f.original.devices[0].receipts[0];
  const oldPending = { rulesVersion: 13, baseRevision: 0, batchId: first.batchId, events: events.slice(0, first.eventEnd) };
  assert.equal((await f.request(oldPending)).body.error, 'migration_required');
  assert.equal((await f.request({ ...oldPending, rulesVersion: 17 })).body.error, 'legacy_batch_requires_reconciliation');
  assert.deepEqual(await f.request(), saved);
  await f.restart(); assert.deepEqual(await f.request(), saved);
  await writeFile(join(f.dataDir, 'store.json'), '{interrupted'); await f.restart(); assert.deepEqual(await f.request(), saved);
  const health = await f.request(undefined, '/api/health'); assert.equal(health.body.collectionCapacity, 60);
  let current = saved.body;
  for (let attempt = 0; attempt < 32 && current.state.collection.length === 8; ++attempt) {
    const result = await f.request({ rulesVersion: 17, baseRevision: current.revision, batchId: `expanded-roster-capture-${attempt}`, events: [
      ...Array.from({ length: current.state.recoveryRestCount }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }, { type: 'auto', value: 0 },
    ] });
    assert.equal(result.status, 200); current = result.body;
  }
  assert.equal(current.state.collection.length, 9);
  assert.deepEqual(current.state.collection.slice(0, 8).map((member: any) => member.id), old.state.collection.map((member: any) => member.id));
  assert.equal(current.state.activeCreatureId, old.state.activeCreatureId);
  await f.restart(); assert.deepEqual((await f.request()).body.state, current.state);
});

test('rules13 ring event and paused Auto trace migrate without another timing draw', async t => {
  const events = [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }, { type: 'walk', value: 100 }, { type: 'auto-fight', value: 0 }, { type: 'ring-capture', value: 0 }];
  const old = frozen(events), f = await fixture(t, events), saved = await f.request();
  assert.equal(saved.status, 200); assert.equal(saved.body.state.lastCapture.chance, old.state.lastCapture.chance);
  assert.equal(saved.body.state.maxLevel, 50); assert.equal(saved.body.state.schemaVersion, 24); assert.equal(saved.body.state.rulesVersion, 17);
  assert.deepEqual(historicComparable(saved.body.state), historicComparable({ ...old.state, schemaVersion: 24, rulesVersion: 17, collectionCapacity: 60, partyCapacity: 3, partyMemberIds: [] }));
  assert.deepEqual(saved.body.autoTrace, old.trace);
  await f.restart(); assert.deepEqual(await f.request(), saved);
});

// A stale binary must not advertise capacity60 just because an empty service
// store has no save yet to replay through stateSupported().
test('empty service refuses an eight-member native executable before advertising current health', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-roster60-stale-'));
  t.after(() => rm(dataDir, { recursive: true, force: true }));
  const stalePath = join(dataDir, 'synthetic-core');
  const script = `#!/bin/sh
case "$1" in
  --flick-trajectory) printf '%s\\n' '{"inputVersion":1,"landingX":206,"landingY":120,"hit":true}' ;;
  --capture-ring-contract) printf '%s\\n' '{"inputVersion":1,"action":"ring-capture","cycleMs":2400,"factors":{"red":10,"orange":50,"green":100}}' ;;
  --budget) printf '%s\\n' '{"schemaVersion":20,"rulesVersion":13,"collectionCapacity":8,"snapshotBytes":664,"jsonBufferBytes":12288}' ;;
esac
`;
  await writeFile(stalePath, script, { mode: 0o700 });
  assert.throws(() => createApp({ rootDir, dataDir, corePath: stalePath }), /XP companion contract/);
});

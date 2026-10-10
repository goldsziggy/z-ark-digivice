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
  assert.equal(saved.body.state.maxLevel, 50); assert.equal(saved.body.state.schemaVersion, 27); assert.equal(saved.body.state.rulesVersion, 19);
  assert.deepEqual(historicComparable(saved.body.state), historicComparable({ ...old.state, schemaVersion: 27, rulesVersion: 19, collectionCapacity: 250, partyCapacity: 3, partyMemberIds: [] }));
  assert.deepEqual(saved.body.autoTrace, old.trace);
  assert.deepEqual(saved.body.events, []); assert.equal(saved.body.baseSequence, events.length);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual(stored.devices[0].legacy.histories, [{ rulesVersion: 13, events, receipts: f.original.devices[0].receipts }]);
  assert.equal(Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64').length, 6860);
  assert.equal(await readFile(join(f.dataDir, 'store.rules-v13.json'), 'utf8'), f.originalText);
  const first = f.original.devices[0].receipts[0];
  const oldPending = { rulesVersion: 13, baseRevision: 0, batchId: first.batchId, events: events.slice(0, first.eventEnd) };
  assert.equal((await f.request(oldPending)).body.error, 'migration_required');
  assert.equal((await f.request({ ...oldPending, rulesVersion: 19 })).body.error, 'legacy_batch_requires_reconciliation');
  assert.deepEqual(await f.request(), saved);
  await f.restart(); assert.deepEqual(await f.request(), saved);
  await writeFile(join(f.dataDir, 'store.json'), '{interrupted'); await f.restart(); assert.deepEqual(await f.request(), saved);
  const health = await f.request(undefined, '/api/health'); assert.equal(health.body.collectionCapacity, 250);
  let current = saved.body;
  for (let attempt = 0; attempt < 32 && current.state.collection.length === 8; ++attempt) {
    const result = await f.request({ rulesVersion: 19, baseRevision: current.revision, batchId: `expanded-roster-capture-${attempt}`, events: [
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
  assert.equal(saved.body.state.maxLevel, 50); assert.equal(saved.body.state.schemaVersion, 27); assert.equal(saved.body.state.rulesVersion, 19);
  assert.deepEqual(historicComparable(saved.body.state), historicComparable({ ...old.state, schemaVersion: 27, rulesVersion: 19, collectionCapacity: 250, partyCapacity: 3, partyMemberIds: [] }));
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

function crc32(bytes: Buffer): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) crc = (crc >>> 1) ^ (0xedb88320 & (0 - (crc & 1)));
  }
  return (~crc) >>> 0;
}
function schema26Snapshot(current: Buffer): string {
  const out = Buffer.alloc(3216);
  current.copy(out, 0, 0, 112);
  for (let i = 0; i < 60; i += 1) {
    const packed = 112 + i * 26, words = 112 + i * 48;
    const formSpecies = current.readUInt32LE(packed + 8), xpHp = current.readUInt32LE(packed + 12);
    const values = [current.readUInt32LE(packed), formSpecies >>> 16, xpHp >>> 16, current[packed + 17], current[packed + 18],
      current[packed + 19], current[packed + 20], current[packed + 16], current.readUInt32LE(packed + 4), xpHp & 0xffff,
      formSpecies & 0xffff, current.readUInt32LE(packed + 22)];
    for (let word = 0; word < 12; word += 1) out.writeUInt32LE(values[word] >>> 0, words + word * 4);
  }
  current.copy(out, 2992, 112 + 250 * 26, 112 + 250 * 26 + 220);
  out.writeUInt16LE(26, 4);
  out.writeUInt16LE(3204, 6);
  out.writeUInt32LE(crc32(out.subarray(0, 3212)), 3212);
  return out.toString('base64');
}

test('a schema 26 save is rewritten before the schema 27 label and keeps its three dungeon keys', async t => {
  const folded = JSON.parse(execFileSync(corePath, ['--fold-v19-onboarding', '12345'], {
    input: 'hatch 1\n', encoding: 'utf8', maxBuffer: 1024 * 1024,
  }));
  assert.equal(folded.state.sequence, 1);
  assert.equal(folded.state.expeditions.dungeonKeys, 0);
  const events = [{ type: 'hatch', value: 1 }];
  const snapshotBase64 = schema26Snapshot(Buffer.from(folded.snapshotBase64, 'base64'));
  assert.equal(Buffer.from(snapshotBase64, 'base64').readUInt16LE(4), 26);
  const storeFor = (gameSchemaVersion: 26 | 27) => ({
    formatVersion: 21, gameSchemaVersion, rulesVersion: 19, devices: [{
      deviceId: `dv_${'ab'.repeat(12)}`, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding', revision: 1,
      legacy: { histories: [{ rulesVersion: 18, events, receipts: [{ batchId: 'schema26-hatch-001', revision: 1, eventEnd: 1, bodyHash: hash(JSON.stringify({ rulesVersion: 18, baseRevision: 0, events })) }] }], snapshotBase64 },
      events: [], receipts: [],
    }],
  });
  const open = async (gameSchemaVersion: 26 | 27) => {
    const original = storeFor(gameSchemaVersion);
    const dataDir = await mkdtemp(join(tmpdir(), 'digivice-schema26-'));
    const originalText = JSON.stringify(original);
    for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), originalText);
    const options = { rootDir, corePath, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 };
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
    return { dataDir, originalText, request, restart: async () => { await close(); app = await startServer(options); } };
  };
  for (const gameSchemaVersion of [26, 27] as const) {
    const opened = await open(gameSchemaVersion);
    const saved = await opened.request();
    assert.equal(saved.status, 200);
    assert.equal(saved.body.state.expeditions.dungeonKeys, 3);
    assert.equal(saved.body.state.collection.length, 1);
    assert.deepEqual(saved.body.events, []);
    assert.equal(saved.body.baseSequence, 1);
    for (const name of ['store.json', 'store.backup.json']) {
      const stored = JSON.parse(await readFile(join(opened.dataDir, name), 'utf8'));
      const bytes = Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64');
      assert.equal(stored.gameSchemaVersion, 27);
      assert.equal(bytes.length, 6860);
      assert.equal(bytes.readUInt16LE(4), 27);
    }
    assert.equal(await readFile(join(opened.dataDir, 'store.schema-26.json'), 'utf8'), opened.originalText);
    const fed = await opened.request({ rulesVersion: 19, baseRevision: 1, batchId: `schema26-feed-${gameSchemaVersion}`, events: [{ type: 'feed', value: 0 }] });
    assert.equal(fed.status, 200);
    assert.equal(fed.body.state.expeditions.dungeonKeys, 3);
    const afterFeed = JSON.parse(await readFile(join(opened.dataDir, 'store.json'), 'utf8'));
    assert.equal(Buffer.from(afterFeed.devices[0].legacy.snapshotBase64, 'base64').readUInt16LE(4), 27);
    await opened.restart();
    const again = await opened.request();
    assert.equal(again.status, 200);
    assert.equal(again.body.state.expeditions.dungeonKeys, 3);
    assert.equal(again.body.revision, 2);
  }
});

import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const token = Buffer.alloc(32, 87).toString('base64url'); // Public temporary fixture identity.
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/rules19-save.json'), 'utf8'));
type Event = { type: string; value: number };
const receipt = (events: Event[], revision: number, batchId: string) => ({ batchId, revision: revision + 1, eventEnd: events.length,
  bodyHash: hash(JSON.stringify({ rulesVersion: 19, baseRevision: revision, events })) });
const native = (args: string[], events: Event[]) => JSON.parse(execFileSync(corePath, args, {
  input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', maxBuffer: 1024 * 1024,
}));
const comparable = ({schemaVersion, rulesVersion, evolution, expeditions, ...state}: any) => ({...state, expeditions: Object.fromEntries(Object.entries(expeditions).filter(([key]) => !['active','scenarioIndex','tier','tiers','awaitingNext'].includes(key)))});

async function fixture(t: { after(fn: () => Promise<void>): unknown }, device: any) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-rules20-'));
  const original = { formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices: [device] };
  const originalText = JSON.stringify(original, null, 2) + '\n';
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), originalText);
  const options = { rootDir, corePath, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 };
  let app = await startServer(options);
  const close = async () => { await new Promise<void>((done, reject) => app.server.close(e => e ? reject(e) : done())); app.close(); };
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, {recursive: true, force: true}); });
  const request = async (body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as {port: number}).port}${body === undefined ? '/api/save' : '/api/save-sync'}`, {
      method: body === undefined ? 'GET' : 'POST', headers: {authorization: `Bearer ${token}`, ...(body === undefined ? {} : {'content-type': 'application/json'})},
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    return {status: response.status, body: await response.json() as any};
  };
  return {request, dataDir, originalText, restart: async () => { await close(); app = await startServer(options); }};
}
const identity = {deviceId: `dv_${'b7'.repeat(12)}`, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding'};

test('rules19 history migrates once without changing care or replaying pending requests as20', async t => {
  const events = frozen.provenance.events as Event[];
  const committed = receipt(events, 0, 'rules19-original-001');
  const f = await fixture(t, {...identity, legacy: null, revision: 1, events, receipts: [committed]});
  const saved = await f.request(); assert.equal(saved.status, 200);
  assert.deepEqual(comparable(saved.body.state), comparable(frozen.state));
  assert.deepEqual(saved.body.events, []);
  assert.equal(saved.body.baseSequence, events.length);
  assert.equal(saved.body.state.rulesVersion, 22); assert.equal(saved.body.state.schemaVersion, 30);
  assert.ok(saved.body.state.evolution.options.every((o: any) => o.requiredCare === 0 && o.maxCareMistakes === null && o.careRouteOpen));
  const store = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([store.formatVersion, store.gameSchemaVersion, store.rulesVersion], [24, 30, 22]);
  assert.deepEqual(store.devices[0].legacy.histories, [{rulesVersion: 19, events, receipts: [committed]}]);
  assert.equal(await readFile(join(f.dataDir, 'store.rules-v19.json'), 'utf8'), f.originalText);
  const oldPending = {rulesVersion: 19, baseRevision: 0, batchId: committed.batchId, events};
  const rawPending = JSON.stringify(oldPending);
  assert.equal((await f.request(oldPending)).body.error, 'migration_required');
  assert.equal(JSON.stringify(oldPending), rawPending);
  assert.equal((await f.request({...oldPending, rulesVersion: 22})).body.error, 'legacy_batch_requires_reconciliation');
  assert.equal((await f.request({...oldPending, batchId: 'rules19-unsent-001'})).body.error, 'migration_required');
  assert.deepEqual(await f.request(), saved);
  await f.restart(); assert.deepEqual(await f.request(), saved);
  const current = {rulesVersion: 22, baseRevision: 1, batchId: 'rules20-new-care-001', events: [{type: 'play', value: 0}]};
  const played = await f.request(current); assert.equal(played.status, 200);
  await f.restart(); assert.deepEqual(await f.request(current), played);
});

test('an already archived rules19 baseline merges its rules19 suffix while preserving every receipt', async t => {
  const prefix = frozen.provenance.events as Event[];
  const oldReceipt = receipt(prefix, 0, 'rules19-before-roster-growth');
  const suffix = [{type: 'play', value: 0}, {type: 'rest', value: 0}];
  const suffixReceipt = receipt(suffix, 1, 'rules19-after-roster-growth');
  const snapshotBase64 = Buffer.from(frozen.envelope.snapshot, 'hex').toString('base64');
  const original = native(['--replay-v19-snapshot-trace', snapshotBase64], suffix);
  const f = await fixture(t, {...identity, revision: 2,
    legacy: {histories: [{rulesVersion: 19, events: prefix, receipts: [oldReceipt]}], snapshotBase64}, events: suffix, receipts: [suffixReceipt]});
  const saved = await f.request(); assert.equal(saved.status, 200);
  assert.deepEqual(comparable(saved.body.state), comparable(original.state));
  const store = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual(store.devices[0].legacy.histories, [{rulesVersion: 19, events: [...prefix, ...suffix],
    receipts: [oldReceipt, {...suffixReceipt, eventEnd: prefix.length + suffix.length}]}]);
  for (const [r, events, baseRevision] of [[oldReceipt, prefix, 0], [suffixReceipt, suffix, 1]] as const) {
    assert.equal((await f.request({rulesVersion: 22, baseRevision, batchId: r.batchId, events})).body.error, 'legacy_batch_requires_reconciliation');
  }
  await f.restart(); assert.deepEqual(await f.request(), saved);
});

test('an empty rules19 egg remains an egg with its original identity and seed', async t => {
  const f = await fixture(t, {...identity, revision: 0, legacy: null, events: [], receipts: []});
  const saved = await f.request(); assert.equal(saved.status, 200);
  assert.equal(saved.body.revision, 0); assert.equal(saved.body.state.phase, 'egg');
  assert.equal(saved.body.state.seed, identity.seed); assert.deepEqual(saved.body.state.collection, []);
  await f.restart(); assert.deepEqual(await f.request(), saved);
});

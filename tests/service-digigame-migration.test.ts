import { legacyFields } from './legacy-state-projection.ts';
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
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/digigame-frozen-rules6.json'), 'utf8'));
const corePath = process.env.DIGIVICE_TEST_CORE_PATH;
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
const TOKEN = Buffer.alloc(32, 17).toString('base64url'); // Public test fixture only.
type Event = { type: string; value: number };
function receipt(rulesVersion: number, baseRevision: number, events: Event[], batchId: string) {
  return { batchId, revision: baseRevision + 1, eventEnd: events.length, bodyHash: hash(JSON.stringify({ rulesVersion, baseRevision, events })) };
}
function oldStore(events: Event[], legacy: unknown = null, revision = events.length ? 1 : 0, initialMode = 'onboarding') {
  return { formatVersion: 8, gameSchemaVersion: 9, rulesVersion: 6, devices: [{ deviceId: `dv_${'1'.repeat(24)}`, tokenHash: hash(TOKEN), seed: 12345, initialMode, revision, legacy, events,
    receipts: events.length ? [receipt(6, revision - 1, events, 'frozen-six-committed-batch')] : [] }] };
}
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 13, baseRevision, batchId, events });
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, saved: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-digigame-migration-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(saved));
  let app = await startServer({ rootDir, dataDir, corePath, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  async function request(path: string, body?: unknown) {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST', headers: { authorization: `Bearer ${TOKEN}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  }
  return { dataDir, request, restart: async () => { await close(); app.close(); app = await startServer({ rootDir, dataDir, corePath, port: 0 }); } };
}

test('frozen6 snapshot plus suffix migrates once, preserving released journal and reserving every old receipt', async t => {
  const history5 = { rulesVersion: 5, events: frozen.prefixEvents, receipts: [receipt(5, 0, frozen.prefixEvents, 'frozen-five-before-six')] };
  const old = oldStore(frozen.suffixEvents, { histories: [history5], snapshotBase64: frozen.rules6Baseline.snapshotBase64 }, 2);
  const f = await fixture(t, old), saved = (await f.request('/api/save')).body;
  assert.equal(saved.state.schemaVersion, 17); assert.equal(saved.state.rulesVersion, 13); assert.equal(saved.revision, 2);
  assert.equal(saved.baseSequence, 7); assert.deepEqual(saved.events, []); assert.equal(saved.autoTrace, null);
  for (const key of ['collection', 'activeCreatureId', 'nextMemberId', 'journal', 'rngState', 'xp', 'formId', 'hp', 'bond', 'captures', 'onboarding', 'battleMode', 'lastAutoBattle']) assert.deepEqual(legacyFields(saved.state[key]), frozen.suffixResult.state[key], key);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.gameSchemaVersion, stored.rulesVersion], [15, 17, 13]);
  const snapshot = Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64');
  assert.equal(snapshot.length, 652); assert.equal(snapshot.readUInt16LE(4), 17); assert.equal(snapshot.readUInt32LE(8), 13);
  assert.deepEqual(stored.devices[0].legacy.histories, [history5, { rulesVersion: 6, events: old.devices[0].events, receipts: old.devices[0].receipts }]);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v6.json'), 'utf8')), old);
  const pending = { rulesVersion: 6, baseRevision: 1, batchId: old.devices[0].receipts[0].batchId, events: old.devices[0].events };
  assert.equal((await f.request('/api/save-sync', pending)).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', { ...pending, rulesVersion: 13 })).body.error, 'legacy_batch_requires_reconciliation');
  assert.equal((await f.request('/api/save-sync', { ...pending, rulesVersion: 13, events: [{ type: 'feed', value: 0 }] })).body.error, 'legacy_batch_requires_reconciliation');
  await f.restart(); assert.deepEqual((await f.request('/api/save')).body, saved);
  const input = batch(2, 'current-seven-care-retry', [{ type: 'feed', value: 0 }]);
  const accepted = await f.request('/api/save-sync', input); assert.equal(accepted.status, 200); assert.equal(accepted.body.state.xp, saved.state.xp);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', input), accepted); assert.equal((await f.request('/api/save')).body.revision, 3);
});

test('rules6 frozen outcomes remain replayable while a saved test encounter clears without rewards', async t => {
  const f = await fixture(t, oldStore(frozen.encounterEvents));
  const saved = (await f.request('/api/save')).body;
  assert.equal(saved.state.wildRules, 6);
  for (const key of ['wildSpecies', 'wildFormId', 'wildLevel', 'wildHp', 'wildGuard', 'wildTurn', 'rngState']) assert.deepEqual(legacyFields(saved.state[key]), frozen.encounterResult.state[key], key);
  const historical = JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core'), ['--migrate-v6-onboarding', '12345'], { input: [...frozen.encounterEvents, { type: 'auto', value: 0 }].map((e: Event) => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8' })).state;
  for (const key of ['hp', 'energy', 'xp', 'collection', 'journal', 'nextMemberId', 'captures', 'rngState']) assert.deepEqual(legacyFields(historical[key]), frozen.autoResult.state[key], `frozen outcome ${key}`);
  assert.ok(saved.state.wildFormId < 11);
  assert.equal((await f.request('/api/save-sync', batch(1, 'reject-old-6-test-auto', [{ type: 'auto', value: 0 }]))).status, 422);
  assert.deepEqual((await f.request('/api/save')).body, saved);
  const input = batch(1, 'resolve-frozen-six-auto', [{ type: 'resolve-test-encounter', value: 0 }]);
  const result = await f.request('/api/save-sync', input); assert.equal(result.status, 200);
  for (const key of ['hp', 'energy', 'xp', 'journal', 'nextMemberId', 'captures', 'rngState', 'activeCreatureId']) assert.deepEqual(result.body.state[key], saved.state[key], `clearing test encounter preserves ${key}`);
  // Stored care values are unchanged; leaving a pre12 fight re-enables
  // the existing Home display bonus for this mood80 Impmon.
  const expectedMembers = structuredClone(saved.state.collection);
  const active = expectedMembers.find((member: any) => member.id === saved.state.activeCreatureId);
  assert.equal(active.mood, 80); assert.equal(active.care.offenseBonus, 0);
  active.care.offenseBonus = 1; active.care.effective.attack = active.combat.attack + 1; active.care.effective.magic = active.combat.magic + 1;
  assert.deepEqual(result.body.state.collection, expectedMembers);
  assert.equal(result.body.state.phase, 'home'); assert.equal(result.body.state.sequence, saved.state.sequence + 1);
  assert.deepEqual(result.body.autoTrace, saved.autoTrace);
  assert.equal((await f.request('/api/save-sync', batch(2, 'later-seven-care', [{ type: 'rest', value: 0 }]))).status, 200);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', input), result); assert.equal((await f.request('/api/save')).body.revision, 3);
});

test('rules6 zero-event legacy and explicit onboarding identities keep their distinct initializers', async t => {
  for (const initialMode of ['legacy', 'onboarding']) {
    const f = await fixture(t, oldStore([], null, 0, initialMode));
    const save = (await f.request('/api/save')).body, expected = initialMode === 'legacy' ? frozen.legacyZeroEvent.state : frozen.egg.state;
    for (const key of ['collection', 'activeCreatureId', 'onboarding', 'phase', 'rngState']) assert.deepEqual(legacyFields(save.state[key]), expected[key], `${initialMode} ${key}`);
    assert.equal(save.revision, 0); assert.equal(save.baseSequence, 0);
    const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8')); assert.equal(stored.devices[0].initialMode, initialMode);
    await f.restart(); assert.deepEqual((await f.request('/api/save')).body, save);
  }
});

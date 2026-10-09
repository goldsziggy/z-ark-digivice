import { legacyFields } from './legacy-state-projection.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createApp, startServer } from '../service/server.ts';
import { validWalkingState } from '../web/walking-state.js';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/walking-rules10-baseline.json'), 'utf8'));
type Event = { type: string; value: number };
const hash = (value: string) => createHash('sha256').update(value).digest('hex');
const token = Buffer.alloc(32, 44).toString('base64url'); // Public test identity only.
const id = `dv_${'c'.repeat(24)}`;
const command = (revision: number, batchId: string, events: Event[], rulesVersion = 13) => ({ rulesVersion, baseRevision: revision, batchId, events });
const core = (args: string[], events: Event[] = []) => JSON.parse(execFileSync(corePath, args, { input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', maxBuffer: 64 * 1024 }));
const originalStore = (events: Event[]) => ({ formatVersion: 12, gameSchemaVersion: 13, rulesVersion: 10, devices: [{
  deviceId: id, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding', revision: events.length ? 1 : 0,
  legacy: null, events, receipts: events.length ? [{ batchId: 'frozen-ten-original-batch', bodyHash: hash(JSON.stringify({ rulesVersion: 10, baseRevision: 0, events })), revision: 1, eventEnd: events.length }] : [],
}] });
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, original: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-walking-service-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(original));
  const options = { rootDir, dataDir, corePath, battleCorePath, port: 0 };
  let app = await startServer(options);
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown, path = body === undefined ? '/api/save' : '/api/save-sync') => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST',
      headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  return { dataDir, request, restart: async () => { await close(); app.close(); app = await startServer(options); } };
}
function oldFields(state: any) {
  const { walking, schemaVersion, rulesVersion, ...rest } = legacyFields(state);
  assert.equal(schemaVersion, 17); assert.equal(rulesVersion, 13); assert.ok(validWalkingState(state.walking, state.phase));
  return { ...rest, schemaVersion: 13, rulesVersion: 10 };
}
for (const [name, value] of Object.entries(frozen.cases) as Array<[string, any]>) test(`schema13 ${name} preserves exact frozen10 state, trace, events and receipts`, async t => {
  assert.deepEqual(core(['--replay-v10-onboarding-trace', '12345'], value.events), value.result, 'golden output captured before the rules change');
  const original = originalStore(value.events), f = await fixture(t, original), save = await f.request();
  assert.equal(save.status, 200); assert.deepEqual(oldFields(save.body.state), value.result.state); assert.deepEqual(save.body.autoTrace, value.result.trace);
  assert.equal(save.body.revision, original.devices[0].revision); assert.equal(save.body.baseSequence, value.events.length); assert.deepEqual(save.body.events, []);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.gameSchemaVersion, stored.rulesVersion], [15, 17, 13]);
  assert.deepEqual(stored.devices[0].legacy.histories, [{ rulesVersion: 10, events: value.events, receipts: original.devices[0].receipts }]);
  assert.equal(Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64').length, 652);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v10.json'), 'utf8')), original);
  if (value.events.length) {
    const prior = command(0, 'frozen-ten-original-batch', value.events, 10);
    assert.equal((await f.request(prior)).body.error, 'migration_required');
    assert.equal((await f.request({ ...prior, rulesVersion: 13 })).body.error, 'legacy_batch_requires_reconciliation');
  }
  await f.restart(); assert.deepEqual(await f.request(), save);
  if (name === 'encounter') {
    assert.ok(save.body.state.wildFormId < 11);
    assert.equal((await f.request(command(1, 'reject-old-ten-test-auto', [{ type: 'auto', value: 0 }]))).status, 422);
    assert.deepEqual(await f.request(), save);
    const resolution = command(1, 'clear-frozen-ten-test-encounter', [{ type: 'resolve-test-encounter', value: 0 }]);
    const cleared = await f.request(resolution); assert.equal(cleared.status, 200);
    for (const key of ['hp', 'energy', 'xp', 'journal', 'captures', 'rngState', 'activeCreatureId', 'nextMemberId']) assert.deepEqual(cleared.body.state[key], save.body.state[key], key);
    // Stored care values are unchanged; leaving a pre12 fight re-enables
    // the existing Home display bonus for this mood80 Impmon.
    const expectedMembers = structuredClone(save.body.state.collection);
    const active = expectedMembers.find((member: any) => member.id === save.body.state.activeCreatureId);
    assert.equal(active.mood, 80); assert.equal(active.care.offenseBonus, 0);
    active.care.offenseBonus = 1; active.care.effective.attack = active.combat.attack + 1; active.care.effective.magic = active.combat.magic + 1;
    assert.deepEqual(cleared.body.state.collection, expectedMembers);
    assert.equal(cleared.body.state.phase, 'home'); assert.equal(cleared.body.state.sequence, save.body.state.sequence + 1);
    assert.deepEqual(cleared.body.autoTrace, save.body.autoTrace);
    const persisted = await f.request(); assert.deepEqual(persisted.body.state, cleared.body.state); assert.equal(persisted.body.revision, cleared.body.revision);
    await f.restart(); assert.deepEqual(await f.request(resolution), cleared); assert.deepEqual(await f.request(), persisted);
  }
});

test('walking seed, threshold and Auto receipt survive retries, later care and restart without reroll', async t => {
  const f = await fixture(t, originalStore(frozen.cases.home.events));
  const health = await f.request(undefined, '/api/health'); assert.equal(health.body.schemaVersion, 17); assert.equal(health.body.rulesVersion, 13);
  const seed = command(1, 'walking-seed-once', [{ type: 'encounter-seed', value: 123456789 }, { type: 'mode', value: 1 }]);
  const seeded = await f.request(seed); assert.equal(seeded.status, 200); assert.ok(seeded.body.state.walking.target > 0);
  const pacing = seeded.body.state.walking;
  assert.equal(pacing.eligibleSteps, 0); assert.deepEqual(await f.request(seed), seeded);
  assert.equal((await f.request(command(2, 'walking-seed-twice', [{ type: 'encounter-seed', value: 54321 }]))).status, 422);
  const walked = await f.request(command(2, 'walking-threshold-crossing', [{ type: 'explore', value: 1000 }]));
  assert.equal(walked.status, 200); assert.equal(walked.body.state.wildRules, 13); assert.equal(walked.body.state.walking.encounters, 1);
  assert.equal(walked.body.state.walking.progress, 0); assert.equal(walked.body.state.queuedEncounters, 0);
  assert.equal((await f.request(command(3, 'walking-menu-no-backlog', [{ type: 'explore', value: 1000 }]))).status, 422);
  const auto = command(3, 'walking-auto-exact-result', [{ type: 'auto', value: 0 }]), result = await f.request(auto);
  assert.equal(result.status, 200); assert.ok(result.body.autoTrace.steps.length);
  assert.ok(result.body.autoTrace.steps.every((step: any) => ['physical', 'magic', 'capture'].includes(step.action)));
  assert.deepEqual(await f.request(auto), result);
  await f.restart(); assert.deepEqual(await f.request(auto), result);
  assert.equal((await f.request(command(4, 'walking-later-care', [{ type: 'rest', value: 0 }]))).status, 200);
  assert.deepEqual(await f.request(auto), result, 'later progress must not replace original battle response');
  assert.deepEqual(await f.request(seed), seeded, 'original seed outcome stays fixed after later draws');
  const off = await f.request(command(5, 'walking-pause-setting', [{ type: 'encounter-rate', value: 0 }])); assert.equal(off.status, 200);
  const suspended = off.body.state.walking;
  assert.equal((await f.request(command(6, 'walking-off-no-progress', [{ type: 'explore', value: 1 }]))).status, 422);
  const frequent = await f.request(command(6, 'walking-frequency-setting', [{ type: 'encounter-rate', value: 3 }])); assert.equal(frequent.status, 200);
  assert.equal(frequent.body.state.walking.target, suspended.target); assert.equal(frequent.body.state.walking.rngState, suspended.rngState);
});

test('walking input bounds and mixed-batch failures never change the save', async t => {
  const f = await fixture(t, originalStore(frozen.cases.home.events)), before = await f.request(); let serial = 0;
  for (const [type, values] of [['explore', [0, -1, 1001, 1.5, '1']], ['encounter-rate', [-1, 4, 0.5, null]], ['encounter-seed', [0, -1, 0x100000000, 1.5]]] as const) {
    for (const value of values) assert.equal((await f.request(command(1, `walking-invalid-${++serial}`, [{ type, value: value as number }]))).body.error, 'invalid_events');
  }
  assert.equal((await f.request(command(1, 'walking-bad-extra-data', [{ type: 'explore', value: 1, gps: [1, 2] } as Event]))).body.error, 'invalid_events');
  assert.equal((await f.request(command(1, 'walking-rollback-seed', [{ type: 'encounter-seed', value: 22 }, { type: 'hatch', value: 1 }]))).status, 422);
  assert.deepEqual(await f.request(), before);
});

test('a rules10 archive cannot smuggle a walking event into migration', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-invalid-walking-')); t.after(() => rm(dataDir, { recursive: true, force: true }));
  for (const type of ['explore', 'encounter-rate', 'encounter-seed']) {
    const original = originalStore([{ type, value: 1 }]);
    for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(original));
    assert.throws(() => createApp({ rootDir, dataDir, corePath, battleCorePath }), /Unsupported or corrupt event history/);
  }
});

test('browser pacing validator rejects impossible thresholds and never interprets eligible steps as pedometer totals', () => {
  const state = core(['--replay-onboarding', '12345'], [{ type: 'hatch', value: 1 }, { type: 'encounter-seed', value: 42 }, { type: 'explore', value: 1 }]);
  assert.ok(validWalkingState(state.walking, state.phase));
  for (const change of [{ rate: 4 }, { name: 'Daily' }, { target: 81 }, { progress: 1000 }, { remainingSteps: 999 }, { rngState: 0 }, { eligibleSteps: -1 }, { lifetimeSteps: 1 }]) {
    assert.equal(validWalkingState({ ...state.walking, ...change }, state.phase), false);
  }
});

for (const suffix of [false, true]) test(`rules10 snapshot baseline retains an inherited rules9 Auto trace (${suffix ? 'care suffix' : 'empty suffix'})`, async t => {
  const { inherited } = frozen, events = suffix ? [{ type: 'rest', value: 0 }] : [];
  const oldHistory = { rulesVersion: 9, events: inherited.events, receipts: [{ batchId: 'inherited-nine-auto', bodyHash: hash(JSON.stringify({ rulesVersion: 9, baseRevision: 0, events: inherited.events })), revision: 1, eventEnd: inherited.events.length }] };
  const original = originalStore(events), device = original.devices[0];
  Object.assign(device, { revision: suffix ? 2 : 1, legacy: { histories: [oldHistory], snapshotBase64: inherited.baseline.snapshotBase64, autoTrace: inherited.trace },
    receipts: suffix ? [{ batchId: 'inherited-ten-rest', bodyHash: hash(JSON.stringify({ rulesVersion: 10, baseRevision: 1, events })), revision: 2, eventEnd: 1 }] : [] });
  const expected = suffix ? inherited.afterRest : { state: inherited.baseline.state, trace: null };
  assert.deepEqual(core(['--replay-v10-snapshot-trace', inherited.baseline.snapshotBase64], events), expected);
  const f = await fixture(t, original), save = await f.request(); assert.equal(save.status, 200);
  assert.deepEqual(oldFields(save.body.state), expected.state); assert.deepEqual(save.body.autoTrace, inherited.trace);
  assert.equal(save.body.revision, device.revision); assert.equal(save.body.baseSequence, inherited.events.length + events.length);
  await f.restart(); assert.deepEqual(await f.request(), save);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual(stored.devices[0].legacy.histories[0], oldHistory);
  assert.deepEqual(stored.devices[0].legacy.histories[1], { rulesVersion: 10, events, receipts: device.receipts });
});

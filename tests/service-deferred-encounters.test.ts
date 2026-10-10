import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createApp, startServer } from '../service/server.ts';
import { historicComparable } from './legacy-state-projection.ts';
import { validWalkingState } from '../web/walking-state.js';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/deferred-schema15-baseline.json'), 'utf8'));
type Event = { type: string; value: number };
const hash = (value: string) => createHash('sha256').update(value).digest('hex');
const token = Buffer.alloc(32, 57).toString('base64url'); // Synthetic test identity only.
const deviceId = `dv_${'d'.repeat(24)}`;
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 19, baseRevision, batchId, events });
const receipt = (rulesVersion: number, baseRevision: number, events: Event[], batchId: string) => ({ batchId, revision: baseRevision + 1, eventEnd: events.length, bodyHash: hash(JSON.stringify({ rulesVersion, baseRevision, events })) });
const oldStore = () => ({ formatVersion: 14, gameSchemaVersion: 15, rulesVersion: 12, devices: [{
  deviceId, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding', revision: 2,
  legacy: { histories: [{ rulesVersion: 11, events: frozen.baseEvents, receipts: [receipt(11, 0, frozen.baseEvents, 'old-eleven-hatch')] }], snapshotBase64: frozen.base.snapshotBase64 },
  events: frozen.events, receipts: [receipt(12, 1, frozen.events, 'old-twelve-walk')],
}] });
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, original = oldStore()) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-deferred-service-'));
  const originalText = JSON.stringify(original, null, 2) + '\n';
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), originalText);
  const options = { seedSource: () => 12345, rootDir, dataDir, corePath, battleCorePath, port: 0 };
  let app = await startServer(options);
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown, path = body === undefined ? '/api/save' : '/api/save-sync') => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST',
      headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  return { dataDir, originalText, request, restart: async () => { await close(); app.close(); app = await startServer(options); } };
}

function schema15Projection(state: any) {
  const copy = structuredClone(state); delete copy.partyCapacity; delete copy.partyMemberIds; assert.equal(copy.worldSeed, 0); delete copy.worldSeed; copy.collectionCapacity = 8; copy.schemaVersion = 15; copy.rulesVersion = 12; delete copy.foregroundSequence; delete copy.receivedTrades; delete copy.autoCapture; delete copy.walking.pendingEncounter;
  return historicComparable(copy);
}

test('rules12 migration archives exact original bytes and frozen results; old receipts stay reserved', async t => {
  const original = oldStore(), f = await fixture(t, original), save = await f.request();
  assert.equal(save.status, 200); assert.equal(save.body.state.schemaVersion, 27); assert.equal(save.body.state.rulesVersion, 19);
  assert.equal(save.body.state.maxLevel, 50); assert.deepEqual(schema15Projection(save.body.state), historicComparable(frozen.result.state));
  assert.deepEqual(save.body.autoTrace, frozen.result.trace);
  assert.equal(await readFile(join(f.dataDir, 'store.rules-v12.json'), 'utf8'), f.originalText);
  const current = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([current.formatVersion, current.gameSchemaVersion, current.rulesVersion], [21, 27, 19]);
  const migrated = current.devices[0];
  assert.equal(migrated.deviceId, original.devices[0].deviceId); assert.equal(migrated.tokenHash, original.devices[0].tokenHash);
  assert.equal(migrated.revision, 2); assert.deepEqual(migrated.events, []); assert.deepEqual(migrated.receipts, []);
  assert.deepEqual(migrated.legacy.histories, [...original.devices[0].legacy.histories, { rulesVersion: 12, events: frozen.events, receipts: original.devices[0].receipts }]);
  assert.equal(Buffer.from(migrated.legacy.snapshotBase64,'base64').readUInt16LE(4), 27);
  assert.deepEqual(await readFile(join(f.dataDir, 'store.json')), await readFile(join(f.dataDir, 'store.backup.json')));
  const retry = { ...batch(1, 'old-twelve-walk', frozen.events), rulesVersion: 12 };
  assert.equal((await f.request(retry)).body.error, 'migration_required');
  assert.equal((await f.request({ ...retry, rulesVersion: 19 })).body.error, 'legacy_batch_requires_reconciliation');
  await f.restart(); assert.deepEqual(await f.request(), save);
  assert.equal(await readFile(join(f.dataDir, 'store.rules-v12.json'), 'utf8'), f.originalText);
});

test('one waiting encounter survives care, restart and receipt retries; active wild fight can earn exactly one later result', async t => {
  const f = await fixture(t);
  const earnedBatch = batch(2, 'earn-one-in-menu', [{ type: 'accrue-steps', value: 1000 }]);
  const earned = await f.request(earnedBatch); assert.equal(earned.status, 200);
  const waiting = earned.body.state.walking.pendingEncounter; assert.ok(waiting); assert.ok(waiting.formId >= 11); assert.equal(waiting.rules, 19); assert.ok(validWalkingState(earned.body.state.walking, 'home'));
  assert.equal(earned.body.state.phase, 'home'); assert.equal(earned.body.state.walking.remainingSteps, 0);
  const filled = await f.request(batch(3, 'walk-with-full-slot', [{ type: 'accrue-steps', value: 1000 }, { type: 'rest', value: 0 }]));
  assert.equal(filled.status, 200); assert.deepEqual(filled.body.state.walking.pendingEncounter, waiting);
  assert.equal(filled.body.state.walking.rngState, earned.body.state.walking.rngState);
  assert.equal(filled.body.state.walking.encounters, earned.body.state.walking.encounters);
  await f.restart(); assert.deepEqual((await f.request()).body.state, filled.body.state); assert.deepEqual(await f.request(earnedBatch), earned);
  const presentedBatch = batch(4, 'present-only-once', [{ type: 'present-encounter', value: 0 }]);
  const shown = await f.request(presentedBatch); assert.equal(shown.status, 200); assert.equal(shown.body.state.phase, 'encounter');
  assert.equal(shown.body.state.wildFormId, waiting.formId); assert.equal(shown.body.state.wildLevel, waiting.level); assert.equal(shown.body.state.walking.pendingEncounter, null);
  assert.equal((await f.request(batch(5, 'cannot-present-twice', [{ type: 'present-encounter', value: 0 }]))).status, 422);
  const later = await f.request(batch(5, 'earn-during-current-fight', [{ type: 'accrue-steps', value: 1000 }]));
  assert.equal(later.status, 200); assert.ok(later.body.state.walking.pendingEncounter); assert.equal(later.body.state.wildFormId, shown.body.state.wildFormId);
  assert.equal(later.body.state.wildHp, shown.body.state.wildHp); assert.equal(later.body.state.wildTurn, shown.body.state.wildTurn);
  assert.equal((await f.request(batch(6, 'never-replace-current-fight', [{ type: 'present-encounter', value: 0 }]))).status, 422);
  const finished = await f.request(batch(6, 'finish-existing-fight', [{ type: 'auto', value: 0 }])); assert.equal(finished.status, 200);
  assert.equal(finished.body.state.phase, 'home'); assert.deepEqual(finished.body.state.walking.pendingEncounter, later.body.state.walking.pendingEncounter);
  await f.restart(); assert.deepEqual(await f.request(presentedBatch), shown);
  const next = await f.request(batch(7, 'present-after-fight-return', [{ type: 'present-encounter', value: 0 }]));
  assert.equal(next.status, 200); assert.equal(next.body.state.wildFormId, later.body.state.walking.pendingEncounter.formId); assert.equal(next.body.state.walking.pendingEncounter, null);
});

test('deferred events are bounded and event-authoritative; invalid mixed batches never change saves', async t => {
  const f = await fixture(t), before = await f.request(); let serial = 0;
  for (const [type, values] of [['accrue-steps', [0, -1, 1001, 1.5, '1']], ['present-encounter', [-1, 1, 0.5, null]]] as const) {
    for (const value of values) assert.equal((await f.request(batch(2, `invalid-deferred-${++serial}`, [{ type, value: value as number }]))).body.error, 'invalid_events');
  }
  assert.equal((await f.request({ ...batch(2, 'forged-snapshot-body', [{ type: 'accrue-steps', value: 1 }]), snapshotBase64: frozen.base.snapshotBase64 })).body.error, 'invalid_request');
  assert.equal((await f.request(batch(2, 'forged-result-in-event', [{ type: 'accrue-steps', value: 1, pendingEncounter: { formId: 18, level: 20, rules: 12 } } as Event]))).body.error, 'invalid_events');
  assert.equal((await f.request(batch(2, 'atomic-invalid-menu-batch', [{ type: 'accrue-steps', value: 1000 }, { type: 'hatch', value: 1 }]))).status, 422);
  assert.deepEqual(await f.request(), before);
  assert.throws(() => execFileSync(corePath, ['--replay-onboarding', '12345'], { input: 'accrue-steps 1\n', encoding: 'utf8', stdio: ['pipe', 'pipe', 'ignore'] }));
});

test('schema15 cannot conceal new deferred events and browser rejects impossible pending payloads', async t => {
  const original = oldStore(), events = [{ type: 'accrue-steps', value: 1 }];
  original.devices[0].events = events; original.devices[0].receipts = [receipt(12, 1, events, 'smuggled-deferred-event')];
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-deferred-invalid-')); t.after(() => rm(dataDir, { recursive: true, force: true }));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(original));
  assert.throws(() => createApp({ rootDir, dataDir, corePath, battleCorePath }), /Unsupported deferred encounter/);
  for (const gameSchemaVersion of ['15', '16', 17]) {
    for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify({ ...oldStore(), gameSchemaVersion }));
    assert.throws(() => createApp({ rootDir, dataDir, corePath, battleCorePath }), /explicit migration/);
  }
  const state = JSON.parse(execFileSync(corePath, ['--replay-onboarding', '12345'], { input: 'hatch 1\naccrue-steps 1000\n', encoding: 'utf8' }));
  assert.ok(validWalkingState(state.walking, state.phase));
  for (const pendingEncounter of [{ formId: 0, level: 1, rules: 12 }, { formId: 18, level: 51, rules: 12 }, { formId: 18, level: 1, rules: 11 }, { formId: 18, level: 1, rules: 12, gps: [1, 2] }, [], undefined]) {
    assert.equal(validWalkingState({ ...state.walking, pendingEncounter }, state.phase), false);
  }
  assert.equal(validWalkingState(state.walking, 'egg'), false);
});


test('practice match accepts walking but defers pending presentation until the current match ends', async t => {
  const f = await fixture(t);
  const started = await f.request({ rulesVersion: 7, expectedRevision: 0, requestId: 'deferred-practice-start' }, '/api/battle/start');
  assert.equal(started.status, 200); assert.equal(started.body.battle.status, 'active');
  const earned = await f.request(batch(2, 'walking-during-practice', [{ type: 'accrue-steps', value: 1000 }]));
  assert.equal(earned.status, 200); assert.ok(earned.body.state.walking.pendingEncounter);
  const show = batch(3, 'show-after-practice-match', [{ type: 'present-encounter', value: 0 }]);
  assert.equal((await f.request(show)).body.error, 'encounter_deferred');
  assert.deepEqual((await f.request()).body.state, earned.body.state);
  await f.restart(); assert.equal((await f.request(show)).body.error, 'encounter_deferred');
  const ended = await f.request({ rulesVersion: 7, expectedRevision: 1, requestId: 'deferred-practice-retreat', action: { type: 'retreat', value: 0 } }, '/api/battle/act');
  assert.equal(ended.status, 200); assert.equal(ended.body.battle.status, 'retreated');
  const shown = await f.request(show); assert.equal(shown.status, 200);
  assert.equal(shown.body.state.wildFormId, earned.body.state.walking.pendingEncounter.formId);
  assert.deepEqual(await f.request(show), shown);
});

for (const kind of ['active', 'pending']) test(`rules12 ${kind} test encounter is cleared once by an explicit durable event without rewards`, async t => {
  const events = [{ type:'hatch',value:1 }, ...(kind === 'active' ? [{type:'mode',value:1},{type:'walk',value:100}] : [{type:'accrue-steps',value:1000}])];
  const original = { formatVersion:14, gameSchemaVersion:16, rulesVersion:12, devices:[{deviceId,tokenHash:hash(token),seed:12345,initialMode:'onboarding',revision:1,legacy:null,events,receipts:[receipt(12,0,events,'old-test-encounter')]}] };
  const f = await fixture(t, original as any), before = await f.request();
  assert.equal(before.body.revision,1); assert.equal(before.body.state.sequence,events.length);
  assert.equal(kind === 'active' ? before.body.state.wildFormId : before.body.state.walking.pendingEncounter.formId,4);
  assert.equal(await readFile(join(f.dataDir,'store.rules-v12.json'),'utf8'), f.originalText);
  if (kind === 'active') assert.equal((await f.request(batch(1,'no-new-test-fight',[{type:'auto',value:0}]))).status,422);
  for (const value of [-1,1,0.5,null]) assert.equal((await f.request(batch(1,`bad-repair-${String(value).replace(/[^a-z0-9]/g,'x')}`,[{type:'resolve-test-encounter',value:value as number}]))).body.error,'invalid_events');
  const repair = batch(1,`repair-${kind}-once`,[{type:'resolve-test-encounter',value:0}]), after = await f.request(repair);
  assert.equal(after.status,200); assert.equal(after.body.revision,2); assert.equal(after.body.state.sequence,before.body.state.sequence+1);
  assert.equal(after.body.state.phase,'home'); assert.equal(after.body.state.wildFormId,0); assert.equal(after.body.state.walking.pendingEncounter,null);
  for (const field of ['hp','energy','fullness','mood','bond','xp','captures','encounters','rngState','journal','nextMemberId','steps','stepCredit','lastCapture']) assert.deepEqual(after.body.state[field], before.body.state[field], field);
  assert.deepEqual(after.body.state.collection.map(({care,...member}:any)=>member),before.body.state.collection.map(({care,...member}:any)=>member));
  assert.equal(after.body.state.care.offenseBonus,1);
  for (const field of ['eligibleSteps','encounters','rngState','target','progress']) assert.equal(after.body.state.walking[field],before.body.state.walking[field],field);
  if (kind === 'pending') { assert.equal(after.body.state.foregroundSequence,before.body.state.foregroundSequence); assert.equal(after.body.state.message,before.body.state.message); }
  assert.deepEqual(await f.request(repair),after);
  assert.equal((await f.request(batch(2,'no-second-repair',[{type:'resolve-test-encounter',value:0}]))).status,422);
  const saved = await f.request(); assert.deepEqual(saved.body.state,after.body.state);
  await f.restart(); assert.deepEqual(await f.request(repair),after); assert.deepEqual(await f.request(),saved);
  assert.equal(await readFile(join(f.dataDir,'store.rules-v12.json'),'utf8'),f.originalText);
});

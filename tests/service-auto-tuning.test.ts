import { historicComparable, legacyFields } from './legacy-state-projection.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { BattleError, createBattleService } from '../service/battle-service.ts';

const rootDir = resolve(import.meta.dirname, '..');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/auto-tuning-baseline16-service.json'), 'utf8'));
const corePath = process.env.DIGIVICE_TEST_BATTLE_PATH;
const errorCode = (code: string) => (error: unknown) => error instanceof BattleError && error.code === code;

for (const mode of ['tactical', 'auto']) test(`practice6 ${mode} keeps exact 16feedd receipts and outcomes while only new starts use7`, async t => {
  const history = frozen.practice.find((entry: any) => entry.mode === mode);
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-auto-tuning-'));
  for (const name of ['battle-store.json', 'battle-store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(history.store));
  let seedCalls = 0;
  const options = { rootDir, dataDir, corePath, seed: () => { seedCalls++; return history.seed; } };
  let service = createBattleService(options);
  t.after(async () => { service.close(); await rm(dataDir, { recursive: true, force: true }); });
  const readStore = async () => JSON.parse(await readFile(join(dataDir, 'battle-store.json'), 'utf8'));
  const replay = (command: any) => command.operation === 'start'
    ? service.start(history.deviceId, command.body, history.companion)
    : service.act(history.deviceId, command.body);

  assert.deepEqual(service.get(history.deviceId), history.expectedCurrent);
  const migrated = await readStore();
  assert.deepEqual([migrated.formatVersion, migrated.practiceSchemaVersion, migrated.practiceRulesVersion], [8, 7, 7]);
  assert.deepEqual(migrated.devices, history.store.devices, 'wrapper upgrade must not alter private snapshots, hashes, profiles, or revisions');
  assert.deepEqual(JSON.parse(await readFile(join(dataDir, 'battle-store.format-v7.json'), 'utf8')), history.store);
  for (const command of history.commands) {
    assert.deepEqual(replay(command), command.response);
    assert.throws(() => replay({ ...command, body: { ...command.body, rulesVersion: 7 } }), errorCode('request_mismatch'));
  }
  const first = history.commands[0];
  assert.throws(() => replay({ ...first, body: { ...first.body, mode: 'tactical' } }), errorCode('request_mismatch'));
  assert.equal(seedCalls, 0, 'migration, reads and retries must not pick a rival or seed');

  service.close();
  await writeFile(join(dataDir, 'battle-store.json'), '{interrupted');
  service = createBattleService(options);
  assert.equal(service.recoveredFromBackup, true);
  assert.deepEqual(service.get(history.deviceId), history.expectedCurrent);
  assert.equal(seedCalls, 0);

  if (mode === 'tactical') {
    assert.throws(() => service.act(history.deviceId, { ...history.continuation[0].body, rulesVersion: 7, requestId: 'wrong-current-rule-on-old-five' }), errorCode('battle_migration_required'));
    for (const command of history.continuation) {
      assert.deepEqual(replay(command), command.response, 'old6 damage, hints, labels and terminal status stay exact');
      service.close(); service = createBattleService(options);
      assert.deepEqual(replay(command), command.response);
    }
  } else {
    assert.equal(history.expectedCurrent.battle.exchanges, 40);
    assert.equal(history.expectedCurrent.autoTrace.steps.length, 40);
    assert.equal(history.expectedCurrent.battle.status, 'draw');
    assert.throws(() => service.act(history.deviceId, { rulesVersion: 6, expectedRevision: 1, requestId: 'no-manual-actions-old-auto', action: { type: 'physical', value: 0 } }), errorCode('battle_auto_complete'));
  }
  const beforeNew = service.get(history.deviceId);
  assert.throws(() => service.start(history.deviceId, { rulesVersion: 6, expectedRevision: beforeNew.revision, requestId: 'uncommitted-five-start-preserved' }, history.companion), errorCode('battle_migration_required'));
  const command = { rulesVersion: 7, expectedRevision: beforeNew.revision, requestId: 'confirmed-new-six-practice', mode: 'auto' as const };
  const accepted = service.start(history.deviceId, command, history.companion);
  assert.deepEqual([accepted.battle?.schemaVersion, accepted.battle?.rulesVersion, accepted.battle?.maxExchanges], [7, 7, 40]);
  assert.equal(accepted.battle?.phase, 'finished'); assert.equal(accepted.battle?.playerFormId, 223); assert.equal(accepted.battle?.enemyFormId, 233); // Seed maps into the named-only production rival pool.
  assert.deepEqual(accepted.battle?.playerCombat, history.expectedCurrent.battle.playerCombat, 'the repair must not tune profile stats or move labels');
  assert.ok(accepted.autoTrace && accepted.autoTrace.steps.length >= 1 && accepted.autoTrace.steps.length <= 40);
  assert.equal(seedCalls, 1);
  service.close(); service = createBattleService(options);
  assert.deepEqual(service.get(history.deviceId), accepted);
  assert.deepEqual(service.start(history.deviceId, command, history.companion), accepted);
  for (const old of history.commands) assert.deepEqual(replay(old), old.response, 'a new7 duel cannot replace an old retained ACK');
  assert.equal(seedCalls, 1);
});

const corePathCare = process.env.DIGIVICE_TEST_CORE_PATH;
const token = Buffer.alloc(32, 88).toString('base64url'); // Public fixture credential, not an enrolled device.
async function careFixture(t: { after: (fn: () => Promise<void>) => unknown }, checkpoint: string | { store: unknown; response: any }) {
  const historical = typeof checkpoint === 'string' ? frozen.care.checkpoints[checkpoint] : checkpoint;
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-care-nine-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(historical.store));
  let app = await startServer({ seedSource: () => 12345, rootDir, dataDir, corePath: corePathCare, battleCorePath: corePath, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${body === undefined ? '/api/save' : '/api/save-sync'}`, { method: body === undefined ? 'GET' : 'POST', headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  return { dataDir, historical, request, restart: async () => { await close(); app.close(); app = await startServer({ seedSource: () => 12345, rootDir, dataDir, corePath: corePathCare, battleCorePath: corePath, port: 0 }); } };
}
const currentCare = (state: any) => ({ ...state, schemaVersion: 23, rulesVersion: 16, walking: { rate: 2, name: 'Normal', eligibleSteps: 0, encounters: 0, rngState: 0, target: 0, progress: 0, remainingSteps: 0 }, wildRarity: null, recoveryRestCount: state.phase === 'home' ? Math.ceil(Math.max(state.combat.maxHp - state.hp, 100 - state.energy) / 25) : 0, queuedEncounters: Math.floor(state.stepCredit / 100), stepsToNextEncounter: state.stepCredit >= 100 ? 0 : 100 - state.stepCredit });
test('care8 committed Auto history migrates once, reserves pending IDs, and cannot duplicate its capture reward', async t => {
  const f = await careFixture(t, 'afterCare'), saved = await f.request();
  assert.equal(saved.status, 200);
  assert.equal(saved.body.state.maxLevel, 50); assert.deepEqual(historicComparable(legacyFields(saved.body.state)), historicComparable(currentCare(f.historical.response.body.state)));
  assert.deepEqual(saved.body.autoTrace, f.historical.response.body.autoTrace); assert.deepEqual(saved.body.events, []);
  assert.equal(saved.body.revision, 3); assert.equal(saved.body.baseSequence, f.historical.response.body.state.sequence);
  const store = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([store.formatVersion, store.gameSchemaVersion, store.rulesVersion], [18, 23, 16]);
  assert.deepEqual(store.devices[0].legacy.histories, [{ rulesVersion: 8, events: f.historical.store.devices[0].events, receipts: f.historical.store.devices[0].receipts }]);
  assert.deepEqual(store.devices[0].legacy.autoTrace, f.historical.response.body.autoTrace);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v8.json'), 'utf8')), f.historical.store);
  const snapshot = Buffer.from(store.devices[0].legacy.snapshotBase64, 'base64');
  assert.deepEqual([snapshot.length, snapshot.readUInt16LE(4), snapshot.readUInt32LE(8)], [3216, 23, 16]);
  for (const command of frozen.care.commands) {
    assert.equal((await f.request(command.body)).body.error, 'migration_required');
    assert.equal((await f.request({ ...command.body, rulesVersion: 16 })).body.error, 'legacy_batch_requires_reconciliation');
  }
  await f.restart(); assert.deepEqual(await f.request(), saved);
  const body = { rulesVersion: 16, baseRevision: saved.body.revision, batchId: 'new-nine-after-old-auto', events: [{ type: 'rest', value: 0 }] };
  const accepted = await f.request(body); assert.equal(accepted.status, 200);
  assert.equal(accepted.body.state.xp, saved.body.state.xp + (saved.body.state.hp < saved.body.state.combat.maxHp || saved.body.state.energy < 100 ? 2 : 0)); assert.equal(accepted.body.state.captures, saved.body.state.captures);
  assert.deepEqual(accepted.body.autoTrace, saved.body.autoTrace);
  await f.restart(); assert.deepEqual(await f.request(body), accepted);
  const next = { rulesVersion: 16, baseRevision: accepted.body.revision, batchId: 'next-nine-auto-after-migration', events: [...Array.from({ length: 8 }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }, { type: 'auto', value: 0 }] };
  const newer = await f.request(next); assert.equal(newer.status, 200); assert.ok(newer.body.autoTrace.endSequence > saved.body.autoTrace.endSequence);
  await f.restart(); assert.deepEqual(await f.request(next), newer);
  assert.deepEqual(await f.request(body), accepted, 'older current9 receipt retains the original8 trace after another Auto result');
});

test('an active wild8 test encounter retains its archive and clears without a new capture', async t => {
  const f = await careFixture(t, 'encounter'), before = await f.request();
  assert.equal(before.body.state.maxLevel, 50); assert.deepEqual(historicComparable(legacyFields(before.body.state)), historicComparable(currentCare(f.historical.response.body.state)));
  assert.equal(before.body.state.wildRules, 8);
  assert.equal((await f.request({ rulesVersion: 16, baseRevision: 1, batchId: 'no-manual-input-old-auto', events: [{ type: 'attack', value: 0 }] })).status, 422);
  assert.deepEqual(await f.request(), before);
  const oldAuto = JSON.parse(execFileSync(corePathCare ?? join(rootDir, 'build/digivice-core'), ['--replay-v8-onboarding-trace', String(f.historical.store.devices[0].seed)], { input: [...f.historical.store.devices[0].events, ...frozen.care.commands[1].body.events].map((e: any) => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8' }));
  assert.deepEqual(oldAuto.state, frozen.care.commands[1].response.body.state); assert.deepEqual(oldAuto.trace, frozen.care.commands[1].response.body.autoTrace);
  assert.equal((await f.request({ ...frozen.care.commands[1].body, rulesVersion: 16, batchId: 'reject-preserved-eight-auto' })).status, 422);
  const body = { rulesVersion: 16, baseRevision: 1, batchId: 'clear-preserved-eight-test', events: [{ type: 'resolve-test-encounter', value: 0 }] };
  const accepted = await f.request(body); assert.equal(accepted.status, 200);
  for (const key of ['hp', 'energy', 'xp', 'journal', 'captures', 'rngState', 'activeCreatureId', 'nextMemberId']) assert.deepEqual(accepted.body.state[key], before.body.state[key], key);
  // Stored care values are unchanged; leaving a pre12 fight re-enables
  // the existing Home display bonus for this mood80 Impmon.
  const expectedMembers = structuredClone(before.body.state.collection);
  const active = expectedMembers.find((member: any) => member.id === before.body.state.activeCreatureId);
  assert.equal(active.mood, 80); assert.equal(active.care.offenseBonus, 0);
  active.care.offenseBonus = 1; active.care.effective.attack = active.combat.attack + 1; active.care.effective.magic = active.combat.magic + 1;
  assert.deepEqual(accepted.body.state.collection, expectedMembers);
  assert.equal(accepted.body.state.phase, 'home'); assert.equal(accepted.body.state.sequence, before.body.state.sequence + 1);
  assert.deepEqual(accepted.body.autoTrace, before.body.autoTrace);
  await f.restart(); assert.deepEqual(await f.request(body), accepted);
  assert.equal((await f.request()).body.state.captures, accepted.body.state.captures);
});

test('explicit unhatched care8 identity remains an egg after migration and fresh hatch9 commits exactly once', async t => {
  const f = await careFixture(t, 'egg'), before = await f.request();
  assert.equal(before.body.state.maxLevel, 50); assert.deepEqual(historicComparable(legacyFields(before.body.state)), historicComparable(currentCare(f.historical.response.body.state)));
  assert.equal(before.body.revision, 0); assert.equal(before.body.state.phase, 'egg');
  const body = { rulesVersion: 16, baseRevision: 0, batchId: 'new-nine-hatch-on-old-egg', events: [{ type: 'hatch', value: 1 }] };
  const accepted = await f.request(body); assert.equal(accepted.status, 200); assert.equal(accepted.body.state.collection.length, 1);
  await f.restart(); assert.deepEqual(await f.request(body), accepted);
});

test('an archived Auto trace with a mismatched baseline summary fails closed on restart', async t => {
  const f = await careFixture(t, 'afterCare');
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  stored.devices[0].legacy.autoTrace.startSequence++;
  stored.devices[0].legacy.autoTrace.endSequence++;
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(f.dataDir, name), JSON.stringify(stored));
  await assert.rejects(f.restart(), /Archived Auto trace does not match/);
});

test('legacy Lumen/Pelagia checkpoints retain historical HP scaling and clear test encounters through exact ACK retries', async t => {
  const fixtures = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/care-v8-profile-migration.json'), 'utf8')).fixtures;
  for (const checkpoint of fixtures) await t.test(checkpoint.name, async sub => {
    // Native-authored partial-HP checkpoints, not a claim that these two bookkeeping
    // events earned this form. The baseline is already validated by frozen8.
    const old = structuredClone(frozen.care.checkpoints.egg.store);
    const events = Array.from({ length: checkpoint.state.sequence }, () => ({ type: 'rest', value: 0 }));
    const receipt = { batchId: 'native-profile-checkpoint', bodyHash: createHash('sha256').update(JSON.stringify({ rulesVersion: 7, baseRevision: 0, events })).digest('hex'), revision: 1, eventEnd: events.length };
    Object.assign(old.devices[0], { revision: 1, initialMode: 'legacy', legacy: { histories: [{ rulesVersion: 7, events, receipts: [receipt] }], snapshotBase64: checkpoint.snapshotBase64 } });
    const f = await careFixture(sub, { store: old, response: { body: { state: checkpoint.state } } });
    const migrated = await f.request(); assert.equal(migrated.status, 200);
    assert.equal(migrated.body.state.maxLevel, 50); assert.deepEqual(historicComparable(legacyFields(migrated.body.state)), historicComparable(currentCare(checkpoint.migrated.state)));
    const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
    const currentBytes = Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64'), previousBytes = Buffer.from(checkpoint.migrated.snapshotBase64, 'base64');
    assert.equal(currentBytes.length, 3216); assert.equal(previousBytes.length, 576);
    assert.deepEqual(currentBytes.subarray(12, 112), previousBytes.subarray(12, 112), 'header gameplay stays exact');
    for (let slot = 0; slot < 8; slot++) {
      assert.deepEqual(currentBytes.subarray(112 + slot * 48, 112 + slot * 48 + 44), previousBytes.subarray(112 + slot * 44, 112 + (slot + 1) * 44), `member ${slot} gameplay`);
      assert.ok(currentBytes.subarray(112 + slot * 48 + 44, 112 + (slot + 1) * 48).every(byte => byte === 0), `member ${slot} care word starts empty`);
    }
    assert.ok(currentBytes.subarray(112 + 8 * 48, 2992).every(byte => byte === 0), 'new slots start empty');
    assert.deepEqual(currentBytes.subarray(2992, 3100), previousBytes.subarray(464, 572), 'post-collection gameplay stays exact ahead of the care clock');
    await f.restart(); assert.deepEqual(await f.request(), migrated, 'restart must not scale HP twice');
    if (checkpoint.continuation) {
      const events = checkpoint.continuation.trim().split('\n').map((line: string) => { const [type, value] = line.split(' '); return { type, value: Number(value) }; });
      const historical = JSON.parse(execFileSync(corePathCare ?? join(rootDir, 'build/digivice-core'), ['--replay-v12-snapshot-trace', checkpoint.migrated.snapshotBase64], { input: checkpoint.continuation, encoding: 'utf8' }));
      for (const key of ['hp', 'energy', 'xp', 'collection', 'journal', 'captures', 'rngState']) assert.deepEqual(legacyFields(historical.state[key]), checkpoint.terminal.state[key], `frozen continuation ${key}`);
      assert.equal((await f.request({ rulesVersion: 16, baseRevision: 1, batchId: `profile-${checkpoint.name}-reject`, events })).status, 422);
      assert.deepEqual(await f.request(), migrated);
      const command = { rulesVersion: 16, baseRevision: 1, batchId: `profile-${checkpoint.name}-clear-test`, events: [{ type: 'resolve-test-encounter', value: 0 }] };
      const result = await f.request(command); assert.equal(result.status, 200);
      for (const key of ['energy', 'xp', 'journal', 'captures', 'rngState', 'formId', 'activeCreatureId', 'nextMemberId']) assert.deepEqual(result.body.state[key], migrated.body.state[key], key);
      assert.equal(result.body.state.hp, Math.ceil(migrated.body.state.hp * result.body.state.combat.maxHp / migrated.body.state.combat.maxHp), 'leaving the old HP scale preserves the health fraction');
      assert.deepEqual(result.body.state.collection.map((m: any) => [m.id, m.formId, m.xp, m.level]), migrated.body.state.collection.map((m: any) => [m.id, m.formId, m.xp, m.level]));
      assert.equal(result.body.state.phase, 'home'); assert.deepEqual(result.body.autoTrace, migrated.body.autoTrace);
      await f.restart(); assert.deepEqual(await f.request(command), result);
    }
  });
});

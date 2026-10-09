import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { BattleError, createBattleService } from '../service/battle-service.ts';

const rootDir = resolve(import.meta.dirname, '..');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/practice-service-v5.json'), 'utf8'));
const corePath = process.env.DIGIVICE_TEST_BATTLE_PATH;
const errorCode = (code: string) => (error: unknown) => error instanceof BattleError && error.code === code;

for (const mode of ['tactical', 'auto']) test(`practice5 ${mode} keeps exact c830072 receipts and outcomes while only new starts use7`, async t => {
  const history = frozen.cases.find((entry: any) => entry.mode === mode);
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-practice-six-'));
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
  assert.deepEqual(JSON.parse(await readFile(join(dataDir, 'battle-store.format-v6.json'), 'utf8')), history.store);
  for (const command of history.commands) {
    assert.deepEqual(replay(command), command.response);
    assert.throws(() => replay({ ...command, body: { ...command.body, rulesVersion: 6 } }), errorCode('request_mismatch'));
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
      assert.deepEqual(replay(command), command.response, 'old5 damage, hints, labels and terminal status stay exact');
      service.close(); service = createBattleService(options);
      assert.deepEqual(replay(command), command.response);
    }
  } else {
    assert.equal(history.expectedCurrent.battle.exchanges, 40);
    assert.equal(history.expectedCurrent.autoTrace.steps.length, 40);
    assert.equal(history.expectedCurrent.battle.status, 'draw');
    assert.throws(() => service.act(history.deviceId, { rulesVersion: 5, expectedRevision: 1, requestId: 'no-manual-actions-old-auto', action: { type: 'physical', value: 0 } }), errorCode('battle_auto_complete'));
  }
  const beforeNew = service.get(history.deviceId);
  assert.throws(() => service.start(history.deviceId, { rulesVersion: 5, expectedRevision: beforeNew.revision, requestId: 'uncommitted-five-start-preserved' }, history.companion), errorCode('battle_migration_required'));
  const command = { rulesVersion: 7, expectedRevision: beforeNew.revision, requestId: 'confirmed-new-six-practice', mode: 'auto' as const };
  const accepted = service.start(history.deviceId, command, history.companion);
  assert.deepEqual([accepted.battle?.schemaVersion, accepted.battle?.rulesVersion, accepted.battle?.maxExchanges], [7, 7, 40]);
  assert.equal(accepted.battle?.phase, 'finished'); assert.equal(accepted.battle?.playerFormId, 223); assert.equal(accepted.battle?.enemyFormId, 233);
  assert.deepEqual(accepted.battle?.playerCombat, history.expectedCurrent.battle.playerCombat, 'the repair must not tune profile stats or move labels');
  assert.ok(accepted.autoTrace && accepted.autoTrace.steps.length >= 1 && accepted.autoTrace.steps.length <= 40);
  assert.equal(seedCalls, 1);
  service.close(); service = createBattleService(options);
  assert.deepEqual(service.get(history.deviceId), accepted);
  assert.deepEqual(service.start(history.deviceId, command, history.companion), accepted);
  for (const old of history.commands) assert.deepEqual(replay(old), old.response, 'a new6 duel cannot replace an old retained ACK');
  assert.equal(seedCalls, 1);
});

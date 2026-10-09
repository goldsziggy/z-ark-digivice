import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { BattleError, createBattleService, type BattleProfile } from '../service/battle-service.ts';

const rootDir = resolve(fileURLToPath(new URL('..', import.meta.url)));
const corePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
const DEVICE = `dv_${'1'.repeat(24)}`;
const PROFILE: BattleProfile = { id: 1, species: 'impmon', name: 'Impmon', level: 1, formId: 11 };
const command = (revision: number, suffix: string) => ({ rulesVersion: 7, expectedRevision: revision, requestId: `practice-request-${suffix}` });
function code(expected: string) { return (error: unknown) => error instanceof BattleError && error.code === expected; }
function fixture(t: { after: (fn: () => void) => unknown }, options: { seed?: () => number; corePath?: string } = {}) {
  const dataDir = mkdtempSync(join(tmpdir(), 'digivice-battle-service-'));
  const config = { dataDir, rootDir, corePath, seed: () => 12345, ...options };
  let service = createBattleService(config);
  t.after(() => { service.close(); rmSync(dataDir, { recursive: true, force: true }); });
  return {
    dataDir, primary: join(dataDir, 'battle-store.json'), mirror: join(dataDir, 'battle-store.backup.json'),
    get service() { return service; },
    restart() { service.close(); service = createBattleService(config); return service; },
  };
}

test('practice uses the real C++ core, projects public state, and never changes the care save', (t) => {
  const f = fixture(t);
  const carePath = join(f.dataDir, 'store.json'); writeFileSync(carePath, 'unrelated-care-save-sentinel');
  assert.deepEqual(f.service.get(DEVICE), { revision: 0, battle: null, mode: 'tactical', autoTrace: null });
  const companion = { ...PROFILE };
  const start = f.service.start(DEVICE, command(0, 'initial'), companion);
  assert.equal(start.revision, 1); assert.equal(start.battle?.phase, 'attack'); assert.equal(start.battle?.status, 'active');
  assert.deepEqual(start.battle?.companion, PROFILE);
  assert.equal(start.battle?.enemy.species, start.battle?.enemySpecies);
  assert.equal(start.battle?.enemy.name, start.battle?.enemyFormName);
  const direct = spawnSync(corePath, ['--practice-start-forms', '12345', 'impmon', '1', '11', start.battle!.enemySpecies, '1', String(start.battle!.enemyFormId)], { encoding: 'utf8' });
  assert.equal(direct.status, 0, direct.stderr);
  const { companion: _companion, enemy: _enemy, ...publicCore } = start.battle!;
  assert.deepEqual(publicCore, JSON.parse(direct.stdout).state);
  assert.equal(/snapshotBase64|rngState|committed|seed/.test(JSON.stringify(start)), false);
  companion.name = 'Client mutation'; start.battle!.companion.name = 'Response mutation';
  assert.equal(f.service.get(DEVICE).battle?.companion.name, 'Impmon');
  assert.equal(readFileSync(carePath, 'utf8'), 'unrelated-care-save-sentinel');
  assert.equal(statSync(f.primary).mode & 0o777, 0o600);
  assert.equal(readFileSync(f.primary, 'utf8'), readFileSync(f.mirror, 'utf8'));
});

test('exact semantic retries survive later actions and restart; stale and mismatched requests never advance', (t) => {
  const f = fixture(t);
  const first = command(0, 'initial');
  const initial = f.service.start(DEVICE, first, PROFILE);
  const action = { ...command(1, 'first-turn'), action: { type: 'physical', value: 0 } };
  const accepted = f.service.act(DEVICE, action);
  assert.equal(accepted.revision, 2); assert.equal(accepted.battle?.phase, 'defend');
  assert.deepEqual(f.service.start(DEVICE, { rulesVersion: 7, requestId: first.requestId, expectedRevision: 0 }, { ...PROFILE, name: 'Changed active companion' }), initial);
  assert.deepEqual(f.service.act(DEVICE, { rulesVersion: 7, action: { value: 0, type: 'physical' }, requestId: action.requestId, expectedRevision: 1 }), accepted);
  assert.throws(() => f.service.act(DEVICE, { ...action, action: { type: 'heavy', value: 0 } }), code('request_mismatch'));
  assert.throws(() => f.service.act(DEVICE, { ...action, requestId: 'practice-request-stale' }), code('revision_conflict'));
  f.restart();
  assert.deepEqual(f.service.start(DEVICE, first, PROFILE), initial);
  assert.deepEqual(f.service.act(DEVICE, action), accepted);
  assert.deepEqual(f.service.get(DEVICE), accepted);
});

test('core phase/card rules and strict API inputs reject without durable mutation', (t) => {
  const f = fixture(t);
  assert.throws(() => f.service.act(DEVICE, { ...command(0, 'no-battle'), action: { type: 'physical', value: 0 } }), code('battle_not_started'));
  f.service.start(DEVICE, command(0, 'initial'), PROFILE);
  assert.throws(() => f.service.start(DEVICE, command(1, 'duplicate-start'), PROFILE), code('battle_active'));
  assert.throws(() => f.service.start(DEVICE, command(1, 'legacy-player'), { id:1,species:'mote',name:'Mote',level:1,formId:1 }), code('legacy_partner'));
  const before = readFileSync(f.primary, 'utf8');
  for (const action of [{ type: 'brace', value: 0 }, { type: 'physical', value: 1 }, { type: 'card', value: 3 }, { type: 'teleport', value: 0 }, { type: 'physical', value: 0, hp: 100 }]) {
    assert.throws(() => f.service.act(DEVICE, { ...command(1, 'invalid'), action }), code('invalid_battle_action'));
  }
  assert.throws(() => f.service.start(DEVICE, { ...command(1, 'profile-injection'), profile: { level: 99 } }, PROFILE), code('invalid_battle_request'));
  assert.throws(() => f.service.get('../../other-device'), code('invalid_battle_identity'));
  assert.equal(readFileSync(f.primary, 'utf8'), before);
  const hint = f.service.get(DEVICE).battle!.enemyHint;
  const card = f.service.act(DEVICE, { ...command(1, 'card'), action: { type: 'card', value: 1 } });
  assert.equal(card.battle?.phase, 'attack'); assert.equal(card.battle?.cardUsed, true); assert.deepEqual(card.battle?.enemyHint, hint);
  assert.throws(() => f.service.act(DEVICE, { ...command(2, 'card-again'), action: { type: 'card', value: 2 } }), code('invalid_battle_action'));
  const ended = f.service.act(DEVICE, { ...command(2, 'retreat'), action: { type: 'retreat', value: 0 } });
  assert.equal(ended.battle?.status, 'retreated');
  assert.throws(() => f.service.act(DEVICE, { ...command(3, 'after-end'), action: { type: 'physical', value: 0 } }), code('invalid_battle_action'));
  const nextProfile: BattleProfile = { id: 2, species: 'gabumon', name: 'Gabumon', level: 2, formId: 25 };
  assert.deepEqual(f.service.start(DEVICE, command(3, 'next-companion'), nextProfile).battle?.companion, nextProfile);
});

test('bounded last-32 receipts reject expired retries and isolate eight authenticated device histories', (t) => {
  const f = fixture(t);
  for (let duel = 0; duel < 18; duel++) {
    const revision = duel * 2;
    f.service.start(DEVICE, command(revision, `start-${duel}`), PROFILE);
    f.service.act(DEVICE, { ...command(revision + 1, `retreat-${duel}`), action: { type: 'retreat', value: 0 } });
  }
  const stored = JSON.parse(readFileSync(f.primary, 'utf8'));
  assert.equal(stored.devices[0].receipts.length, 32); assert.equal(stored.devices[0].revision, 36);
  assert.throws(() => f.service.start(DEVICE, command(0, 'start-0'), PROFILE), code('revision_conflict'));
  for (let i = 2; i <= 8; i++) {
    const device = `dv_${String(i).repeat(24)}`;
    const started = f.service.start(device, command(0, 'same-id-is-device-scoped'), PROFILE);
    assert.equal(started.revision, 1);
  }
  assert.throws(() => f.service.start(`dv_${'9'.repeat(24)}`, command(0, 'ninth-device'), PROFILE), code('battle_device_limit'));
  f.restart(); assert.equal(f.service.get(DEVICE).revision, 36);
  assert.ok(statSync(f.primary).size < 1024 * 1024);
});

test('restart restores latest acknowledged snapshot and receipt from a corrupt primary', (t) => {
  const f = fixture(t);
  f.service.start(DEVICE, command(0, 'initial'), PROFILE);
  const action = { ...command(1, 'turn'), action: { type: 'heavy', value: 0 } };
  const accepted = f.service.act(DEVICE, action);
  writeFileSync(f.primary, '{interrupted-json');
  assert.equal(f.restart().recoveredFromBackup, true);
  assert.deepEqual(f.service.get(DEVICE), accepted); assert.deepEqual(f.service.act(DEVICE, action), accepted);
  assert.equal(readFileSync(f.primary, 'utf8'), readFileSync(f.mirror, 'utf8'));
});

test('newest valid generation wins across an interrupted replace and unknown versions never roll back', (t) => {
  const f = fixture(t);
  f.service.start(DEVICE, command(0, 'initial'), PROFILE);
  const old = readFileSync(f.primary, 'utf8');
  const action = { ...command(1, 'newer'), action: { type: 'magic', value: 0 } };
  const newer = f.service.act(DEVICE, action);
  writeFileSync(f.primary, old);
  assert.equal(f.restart().recoveredFromBackup, true); assert.deepEqual(f.service.get(DEVICE), newer);
  const future = JSON.parse(readFileSync(f.primary, 'utf8')); future.practiceRulesVersion = 999;
  writeFileSync(f.primary, JSON.stringify(future));
  assert.throws(() => f.restart(), code('battle_migration_required'));
  assert.equal(JSON.parse(readFileSync(f.primary, 'utf8')).practiceRulesVersion, 999);
});

test('CRC validation rejects corrupt snapshots without resetting the practice history', (t) => {
  const f = fixture(t);
  f.service.start(DEVICE, command(0, 'initial'), PROFILE);
  const stored = JSON.parse(readFileSync(f.primary, 'utf8'));
  const bytes = Buffer.from(stored.devices[0].snapshotBase64, 'base64'); bytes[40] ^= 1;
  stored.devices[0].snapshotBase64 = bytes.toString('base64');
  stored.devices[0].receipts[0].snapshotBase64 = stored.devices[0].snapshotBase64;
  const corrupt = JSON.stringify(stored); writeFileSync(f.primary, corrupt); writeFileSync(f.mirror, corrupt);
  assert.throws(() => f.restart(), code('battle_store_invalid'));
  assert.equal(readFileSync(f.primary, 'utf8'), corrupt);
});

test('an interrupted two-copy write retries the retained receipt without selecting another seed', (t) => {
  let seeds = 0;
  const f = fixture(t, { seed: () => { ++seeds; return 12345; } });
  mkdirSync(f.primary); // The mirror commits, but replacing the primary directory must fail.
  const start = command(0, 'uncertain-commit');
  assert.throws(() => f.service.start(DEVICE, start, PROFILE), code('battle_storage_unavailable'));
  assert.equal(seeds, 1);
  rmSync(f.primary, { recursive: true });
  const recovered = f.service.start(DEVICE, start, PROFILE);
  assert.equal(recovered.revision, 1); assert.equal(seeds, 1);
  assert.deepEqual(f.service.get(DEVICE), recovered);
  assert.equal(readFileSync(f.primary, 'utf8'), readFileSync(f.mirror, 'utf8'));
});

test('missing practice binary fails locally without creating or altering a care save', (t) => {
  const f = fixture(t, { corePath: join(tmpdir(), 'no-such-digivice-practice-core') });
  assert.deepEqual(f.service.get(DEVICE), { revision: 0, battle: null, mode: 'tactical', autoTrace: null });
  assert.throws(() => f.service.start(DEVICE, command(0, 'no-core'), PROFILE), code('battle_core_unavailable'));
  assert.deepEqual(f.service.get(DEVICE), { revision: 0, battle: null, mode: 'tactical', autoTrace: null });
});

test('explicit v1 practice migration preserves intent/history, scales HP, and reserves old request IDs', (t) => {
  const dataDir = mkdtempSync(join(tmpdir(), 'digivice-practice-migration-'));
  t.after(() => rmSync(dataDir, { recursive: true, force: true }));
  const companion: BattleProfile = { id: 1, species: 'mote', name: 'Glint', level: 2 };
  const firstSnapshot = 'REdCUAEAWAABAAAAAAAAAK8JKmUCAAAAZAAAAGQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAGAAAABQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAm8e5Bw==';
  const lastSnapshot = 'REdCUAEAWAABAAAAAQAAAOpBjnQCAAAAZAAAAE4AAAABAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAACAAAAAwAAAAAAAAAGAAAAAgAAAAYAAAAAAAAAFgAAAAAAAAAAAAAAdrFPqg==';
  const oldStart = { expectedRevision: 0, requestId: 'old-practice-start' };
  const oldAct = { expectedRevision: 1, requestId: 'old-practice-heavy', action: { type: 'heavy', value: 0 } };
  const receipts = [
    { requestId: oldStart.requestId, revision: 1, snapshotBase64: firstSnapshot, companion,
      bodyHash: createHash('sha256').update(JSON.stringify({ operation: 'start', expectedRevision: 0 })).digest('hex') },
    { requestId: oldAct.requestId, revision: 2, snapshotBase64: lastSnapshot, companion,
      bodyHash: createHash('sha256').update(JSON.stringify({ operation: 'act', expectedRevision: 1, action: oldAct.action })).digest('hex') },
  ];
  const old = { formatVersion: 1, practiceSchemaVersion: 1, practiceRulesVersion: 1, generation: 2,
    devices: [{ deviceId: DEVICE, revision: 2, snapshotBase64: lastSnapshot, companion, receipts }] };
  for (const file of ['battle-store.json', 'battle-store.backup.json']) writeFileSync(join(dataDir, file), JSON.stringify(old));
  let service = createBattleService({ rootDir, dataDir, corePath });
  t.after(() => service.close());
  const migrated = service.get(DEVICE);
  assert.equal(migrated.revision, 2); assert.equal(migrated.battle?.schemaVersion, 2); assert.equal(migrated.battle?.rulesVersion, 2);
  assert.equal(migrated.battle?.phase, 'defend'); assert.equal(migrated.battle?.sequence, 1);
  assert.equal(migrated.battle?.playerHp, 116); assert.equal(migrated.battle?.enemyHp, 69);
  assert.equal(migrated.battle?.enemyLevel, 1); assert.equal(migrated.battle?.playerLevel, 2);
  assert.equal(migrated.battle?.lastTurn?.enemyDamage, 22, 'past outcomes must remain historical');
  assert.deepEqual(migrated.battle?.enemyHint, ['physical', 'heavy']);
  assert.deepEqual(JSON.parse(readFileSync(join(dataDir, 'battle-store.rules-v1.json'), 'utf8')), old);
  assert.throws(() => service.act(DEVICE, oldAct), code('battle_migration_required'));
  assert.throws(() => service.act(DEVICE, { ...oldAct, rulesVersion: 2 }), code('legacy_request_requires_reconciliation'));
  const current = { ...command(2, 'migrated-brace'), rulesVersion: 2, action: { type: 'brace', value: 0 } };
  const accepted = service.act(DEVICE, current); assert.equal(accepted.revision, 3);
  service.close(); service = createBattleService({ rootDir, dataDir, corePath });
  assert.deepEqual(service.act(DEVICE, current), accepted);
  assert.throws(() => service.start(DEVICE, { ...oldStart, rulesVersion: 2 }, companion), code('legacy_request_requires_reconciliation'));
});

test('new rivals vary by species and scale near the explicit evolved companion', (t) => {
  let seed = 0;
  const f = fixture(t, { seed: () => (seed++ * 2654435761) >>> 0 });
  const species = new Set<string>(), forms = new Set<number>();
  for (let duel = 0; duel < 6; duel++) {
    const result = f.service.start(DEVICE, command(duel * 2, `roster-start-${duel}`), { ...PROFILE, name: 'Baalmon', level: 10, formId: 13 });
    assert.ok(result.battle!.enemyLevel >= 8 && result.battle!.enemyLevel <= 12); assert.equal(result.battle?.playerLevel, 10);
    assert.ok(result.battle!.playerCombat.maxHp > 100); assert.ok(result.battle!.playerCombat.attack > 1);
    assert.equal(result.battle?.playerHp, result.battle?.playerCombat.maxHp);
    assert.equal(result.battle?.enemyHp, result.battle?.enemyCombat.maxHp);
    species.add(result.battle!.enemySpecies); assert.ok(Number.isInteger(result.battle!.enemyFormId)); assert.ok(result.battle!.enemyFormId! >= 11); forms.add(result.battle!.enemyFormId!);
    f.service.act(DEVICE, { ...command(duel * 2 + 1, `roster-retreat-${duel}`), action: { type: 'retreat', value: 0 } });
  }
  assert.equal(forms.size, 6); assert.ok(species.size >= 3);
});

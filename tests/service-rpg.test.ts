import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer, createApp } from '../service/server.ts';
import { BattleError, createBattleService } from '../service/battle-service.ts';
const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
const DEVICE = `dv_${'6'.repeat(24)}`;
const TOKEN = Buffer.alloc(32, 6).toString('base64url'); // Public test fixture only.
type Event = { type: string; value: number };
function oldCare(events: Event[], initialMode = 'onboarding') {
  const receipts = events.length ? [{ batchId: 'frozen-rules-three-batch', revision: 1, eventEnd: events.length,
    bodyHash: hash(JSON.stringify({ rulesVersion: 3, baseRevision: 0, events })) }] : [];
  return { formatVersion: 5, gameSchemaVersion: 6, rulesVersion: 3, devices: [{ deviceId: DEVICE, tokenHash: hash(TOKEN), seed: 12345,
    initialMode, revision: receipts.length, legacy: null, events, receipts }] };
}
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, saved?: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-rpg-http-'));
  if (saved) for (const file of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, file), JSON.stringify(saved));
  let app = await startServer({ seedSource: () => 12345, dataDir, corePath, battleCorePath, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (path: string, token?: string, body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST',
      headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  const pair = async () => { const code = (await request('/api/pairing/start', undefined, {})).body.code; return (await request('/api/pairing/claim', undefined, { code })).body; };
  return { request, pair, dataDir, restart: async () => { await close(); app = await startServer({ seedSource: () => 12345, dataDir, corePath, battleCorePath, port: 0 }); } };
}
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 19, baseRevision, batchId, events });

test('rules-three Auto capture migrates once without retroactive XP or relabelled receipts', async t => {
  const events = [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }, { type: 'walk', value: 100 }, { type: 'auto', value: 0 }];
  const old = oldCare(events), f = await fixture(t, old);
  const migrated = (await f.request('/api/save', TOKEN)).body;
  assert.equal(migrated.state.schemaVersion, 27); assert.equal(migrated.state.rulesVersion, 19);
  assert.equal(migrated.revision, 1); assert.equal(migrated.baseSequence, 4); assert.deepEqual(migrated.events, []);
  assert.equal(migrated.state.rngState, 3336926330); assert.equal(migrated.state.captures, 1);
  assert.deepEqual(migrated.state.collection.map((member: any) => [member.id, member.species, member.xp, member.level, member.capturedAtSequence]), [[1, 'impmon', 0, 1, 0], [2, 'flicker', 0, 1, 4]]);
  assert.deepEqual(migrated.state.lastAutoBattle, { sequence: 4, turns: 4, outcome: 'captured' }); assert.equal(migrated.autoTrace, null);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(stored.formatVersion, 21); assert.equal(Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64').length, 6860);
  assert.deepEqual(stored.devices[0].legacy.histories, [{ rulesVersion: 3, events, receipts: old.devices[0].receipts }]);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v3.json'), 'utf8')), old);
  const oldPending = { rulesVersion: 3, baseRevision: 0, batchId: old.devices[0].receipts[0].batchId, events };
  assert.equal((await f.request('/api/save-sync', TOKEN, oldPending)).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', TOKEN, { ...oldPending, rulesVersion: 19 })).body.error, 'legacy_batch_requires_reconciliation');
  await f.restart(); assert.deepEqual((await f.request('/api/save', TOKEN)).body, migrated);
  const care = await f.request('/api/save-sync', TOKEN, batch(1, 'care-awards-xp-once', [{ type: 'feed', value: 0 }]));
  assert.equal(care.status, 200); assert.equal(care.body.state.xp, migrated.state.fullness < 100 ? 2 : 0); assert.equal(care.body.state.level, 1);
});

test('zero-event legacy identities stay partners and unhatched onboarding remains an egg after RPG migration', async t => {
  const legacy = await fixture(t, oldCare([], 'legacy'));
  const state = (await legacy.request('/api/save', TOKEN)).body.state;
  assert.equal(state.species, 'mote'); assert.equal(state.formId, 1); assert.equal(state.xp, 0); assert.equal(state.phase, 'home');
  const egg = await fixture(t, oldCare([]));
  const pending = (await egg.request('/api/save', TOKEN)).body;
  assert.equal(pending.state.phase, 'egg'); assert.equal(pending.state.collection.length, 0);
  assert.equal((await egg.request('/api/save-sync', TOKEN, batch(0, 'new-hatch-after-upgrade', [{ type: 'hatch', value: 1 }]))).status, 200);
  await egg.restart(); assert.equal((await egg.request('/api/save', TOKEN)).body.state.formId, 11);
});

test('new wild XP and captures are atomic and exact retries survive restart without duplicating rewards', async t => {
  const f = await fixture(t), identity = await f.pair();
  const prepare = await f.request('/api/save-sync', identity.token, batch(0, 'prepare-current-wild', [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }, { type: 'walk', value: 100 }]));
  assert.equal(prepare.status, 200); assert.equal(prepare.body.state.xp, 0);
  const bad = batch(1, 'mixed-auto-then-invalid', [{ type: 'auto', value: 0 }, { type: 'hatch', value: 2 }]);
  const unchanged = (await f.request('/api/save', identity.token)).body;
  assert.equal((await f.request('/api/save-sync', identity.token, bad)).status, 422);
  assert.deepEqual((await f.request('/api/save', identity.token)).body, unchanged);
  const confirmed = batch(1, 'current-auto-reward', [{ type: 'auto', value: 0 }]);
  const accepted = await f.request('/api/save-sync', identity.token, confirmed);
  assert.equal(accepted.status, 200); assert.equal(accepted.body.state.phase, 'home'); assert.ok(accepted.body.state.xp > 0);
  assert.equal(accepted.body.state.sequence, prepare.body.state.sequence + 1);
  assert.deepEqual(await f.request('/api/save-sync', identity.token, confirmed), accepted);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', identity.token, confirmed), accepted);
  const later = await f.request('/api/save-sync', identity.token, batch(2, 'after-reward-care', [{ type: 'feed', value: 0 }, { type: 'play', value: 0 }, { type: 'rest', value: 0 }]));
  assert.equal(later.status, 200);
  {
    const start = accepted.body.state; let xp = start.xp, fullness = start.fullness, mood = start.mood, energy = start.energy, hp = start.hp;
    if (fullness < 100) { xp += 2; fullness = Math.min(100, fullness + 15); energy = Math.min(100, energy + 3); mood = Math.min(100, mood + 2); }
    if (mood < 100 && energy >= 5) { xp += 2; energy -= 5; mood = Math.min(100, mood + 12); }
    if (hp < start.combat.maxHp || energy < 100) xp += 2;
    assert.equal(later.body.state.xp, xp);
  }
  assert.deepEqual(await f.request('/api/save-sync', identity.token, confirmed), accepted);
});

test('native branch catalogs are bounded, evolution needs eligibility and is locked by active practice', async t => {
  const events: Event[] = [{ type: 'hatch', value: 1 }, ...Array.from({ length: 20 }, () => [{ type: 'feed', value: 0 }, { type: 'play', value: 0 }, { type: 'rest', value: 0 }]).flat()];
  const f = await fixture(t, oldCare(events)), before = (await f.request('/api/save', TOKEN)).body;
  assert.equal(before.state.level, 10); assert.equal(before.state.formId, 11); assert.equal(before.state.evolution.options.length, 2);
  const catalog = await f.request('/api/evolution/catalog?species=impmon');
  assert.equal(catalog.status, 200); assert.equal(catalog.body.rulesVersion, 19); assert.equal(catalog.body.forms.length, 7); assert.ok(Buffer.byteLength(JSON.stringify(catalog.body)) <= 16384);
  for (const path of ['/api/evolution/catalog', '/api/evolution/catalog?species=impmon&species=agumon', '/api/evolution/catalog?species=../../secret', '/api/evolution/catalog?species=impmon&level=20']) assert.equal((await f.request(path)).status, 400);
  assert.equal(before.state.evolution.options.some((option: any) => option.eligible), false, 'level 10 does not meet the earned champion gate');
  let revision = before.revision, serial = 0, saved = before;
  const send = async (events: Event[]) => {
    const result = await f.request('/api/save-sync', TOKEN, batch(revision, `earn-evolution-${++serial}`, events));
    assert.equal(result.status, 200, JSON.stringify(result.body?.error ?? result.body?.state?.message));
    revision = result.body.revision; return result.body;
  };
  for (let guard = 0; guard < 100 && !saved.state.evolution.options.some((option: any) => option.eligible); ++guard) {
    const option = saved.state.evolution.options[0];
    const member = saved.state.collection.find((row: any) => row.id === saved.state.activeCreatureId);
    if (member.carePoints < option.requiredCare) {
      const minutes = [1, 2, 3, 4].map(step => ({ type: 'care-minute', value: saved.state.careMinute + step }));
      saved = await send([minutes[0], { type: 'feed', value: 0 }, ...minutes.slice(1)]);
      const carried = saved.state.collection.find((row: any) => row.id === saved.state.activeCreatureId);
      if (carried.toilet >= 25 || carried.careMissed) saved = await send([{ type: 'toilet', value: 0 }]);
      continue;
    }
    const prep: Event[] = [];
    // Rules 17: a knockout injures the partner; treat before resting and digivolving.
    if (member.injury) prep.push({ type: 'treat', value: 0 });
    if (saved.state.battleMode !== 'auto') prep.push({ type: 'mode', value: 1 });
    prep.push(...Array.from({ length: member.injury ? 12 : saved.state.recoveryRestCount }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }, { type: 'auto', value: 0 });
    saved = await send(prep);
  }
  const target = saved.state.evolution.options.find((option: any) => option.eligible);
  assert.ok(target, 'an earned route becomes eligible without changing partners');
  const ready = (await f.request('/api/save', TOKEN)).body;
  assert.equal(ready.revision, revision);
  const evolve = batch(revision, 'confirmed-native-evolution', [{ type: 'evolve', value: target.formId }]);
  assert.equal((await f.request('/api/save-sync', TOKEN, batch(revision, 'invalid-cross-lineage', [{ type: 'evolve', value: 19 }]))).status, 422);
  const started = await f.request('/api/battle/start', TOKEN, { rulesVersion: 7, expectedRevision: 0, requestId: 'rpg-profile-practice-start' });
  assert.equal(started.status, 200); assert.equal(started.body.battle.playerFormId, 11); assert.equal(started.body.battle.playerLevel, ready.state.level); assert.equal(started.body.battle.enemyLevel, ready.state.level);
  assert.equal((await f.request('/api/save-sync', TOKEN, evolve)).body.error, 'partner_locked');
  const retired = await f.request('/api/battle/act', TOKEN, { rulesVersion: 7, expectedRevision: 1, requestId: 'rpg-profile-practice-retreat', action: { type: 'retreat', value: 0 } });
  assert.equal(retired.status, 200); assert.deepEqual((await f.request('/api/save', TOKEN)).body, ready, 'practice grants no XP or care changes');
  const evolved = await f.request('/api/save-sync', TOKEN, evolve);
  assert.equal(evolved.status, 200); assert.equal(evolved.body.state.formId, target.formId); assert.equal(evolved.body.state.activeCreatureId, 1); assert.equal(evolved.body.state.xp, ready.state.xp);
  assert.equal((await f.request('/api/battle/start', TOKEN, { rulesVersion: 7, expectedRevision: 2, requestId: 'rpg-evolved-practice-start' })).body.battle.playerFormId, target.formId);
  assert.deepEqual(await f.request('/api/save-sync', TOKEN, evolve), evolved, 'committed evolution receipt precedes later practice lock');
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', TOKEN, evolve), evolved);
});

// Frozen rules-2 fixtures captured from d4ada21 before changing native binaries.
const OLD_INITIAL = 'REdCUAIAZAACAAAAAAAAAK8JKmUBAAAAXAAAAFgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAGAAAABQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABQAAAAIAAAABAAAAU6jP0A==';
const OLD_TERMINAL = 'REdCUAIAZAACAAAACgAAAILAC80BAAAAAAAAAAoAAAAKAAAAAgAAAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAACAAAABgAAAAIAAAAUAAAAAAAAAAAAAAABAAAABQAAAAIAAAABAAAA5qunrg==';
const OLD_PHYSICAL = 'REdCUAIAZAACAAAAAQAAAOpBjnQBAAAAXAAAAEwAAAABAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAACAAAAAwAAAAAAAAAGAAAAAQAAAAYAAAAAAAAADAAAAAAAAAAAAAAABQAAAAIAAAABAAAAF0aSig==';
const oldProfile = { id: 1, species: 'impmon' as const, name: 'Impmon', level: 1 };
const failure = (code: string) => (error: unknown) => error instanceof BattleError && error.code === code;

test('legacy practice Auto snapshots and receipts replay byte-identically; new starts bind actual forms', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-rpg-practice-'));
  const requestId = 'frozen-practice-auto-start', oldBody = { rulesVersion: 2, expectedRevision: 0, requestId, mode: 'auto' as const };
  const receipt = { requestId, bodyHash: hash(JSON.stringify({ rulesVersion: 2, operation: 'start', expectedRevision: 0, mode: 'auto' })), revision: 1,
    snapshotBase64: OLD_TERMINAL, companion: oldProfile, mode: 'auto', initialSnapshotBase64: OLD_INITIAL };
  const old = { formatVersion: 3, practiceSchemaVersion: 2, practiceRulesVersion: 2, generation: 1,
    devices: [{ deviceId: DEVICE, revision: 1, snapshotBase64: OLD_TERMINAL, companion: oldProfile, mode: 'auto', initialSnapshotBase64: OLD_INITIAL, receipts: [receipt], legacyRequestIds: [] }] };
  for (const file of ['battle-store.json', 'battle-store.backup.json']) await writeFile(join(dataDir, file), JSON.stringify(old));
  let service = createBattleService({ rootDir, dataDir, corePath: battleCorePath, seed: () => 12345 });
  t.after(async () => { service.close(); await rm(dataDir, { recursive: true, force: true }); });
  const preserved = service.get(DEVICE), { companion, enemy, ...core } = preserved.battle!;
  assert.deepEqual(companion, oldProfile); assert.equal(enemy.name, 'Practice rival');
  const replay = spawnSync(battleCorePath, ['--practice-auto', OLD_INITIAL], { encoding: 'utf8' });
  assert.equal(replay.status, 0, replay.stderr);
  const native = JSON.parse(replay.stdout);
  assert.equal(hash(JSON.stringify(native.state)), '0ad3ebc25b3fe99cea9d9e162ce07afd314e73bf3e17c170ac56ce1c4f5c425f');
  assert.equal(hash(JSON.stringify(native.trace)), '65869f49bbdceb9a9c5708586962f9bc233af8f562c502ffc02e66ac4a5409ce');
  assert.deepEqual(core, native.state); assert.deepEqual(preserved.autoTrace, native.trace);
  assert.deepEqual(service.start(DEVICE, oldBody, { ...oldProfile, formId: 11 }), preserved);
  assert.throws(() => service.start(DEVICE, { ...oldBody, rulesVersion: 3 }, { ...oldProfile, formId: 11 }), failure('request_mismatch'));
  assert.throws(() => service.start(DEVICE, { ...oldBody, expectedRevision: 1, requestId: 'old-uncommitted-new-start' }, oldProfile), failure('battle_migration_required'));
  const stored = JSON.parse(await readFile(join(dataDir, 'battle-store.json'), 'utf8'));
  assert.equal(stored.formatVersion, 8); assert.deepEqual(stored.devices, old.devices);
  assert.deepEqual(JSON.parse(await readFile(join(dataDir, 'battle-store.format-v3.json'), 'utf8')), old);
  const current = service.start(DEVICE, { rulesVersion: 7, expectedRevision: 1, requestId: 'new-rpg-native-form-start', mode: 'auto' }, { ...oldProfile, level: 5, formId: 11 });
  assert.equal(current.battle?.rulesVersion, 7); assert.equal(current.battle?.playerFormId, 11); assert.equal(current.battle?.companion.formId, 11);
  service.close(); service = createBattleService({ rootDir, dataDir, corePath: battleCorePath, seed: () => { throw new Error('A retry must not reroll'); } });
  assert.deepEqual(service.get(DEVICE), current); assert.deepEqual(service.start(DEVICE, oldBody, oldProfile), preserved);
});

test('an active legacy practice duel continues only under its frozen rules', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-rpg-active-'));
  const receipt = { requestId: 'frozen-tactical-start', bodyHash: hash(JSON.stringify({ rulesVersion: 2, operation: 'start', expectedRevision: 0 })), revision: 1,
    snapshotBase64: OLD_INITIAL, companion: oldProfile, mode: 'tactical', initialSnapshotBase64: null };
  const old = { formatVersion: 3, practiceSchemaVersion: 2, practiceRulesVersion: 2, generation: 1,
    devices: [{ deviceId: DEVICE, revision: 1, snapshotBase64: OLD_INITIAL, companion: oldProfile, mode: 'tactical', initialSnapshotBase64: null, receipts: [receipt], legacyRequestIds: [] }] };
  await writeFile(join(dataDir, 'battle-store.json'), JSON.stringify(old));
  const service = createBattleService({ rootDir, dataDir, corePath: battleCorePath });
  t.after(async () => { service.close(); await rm(dataDir, { recursive: true, force: true }); });
  const action = { rulesVersion: 2, expectedRevision: 1, requestId: 'frozen-active-physical', action: { type: 'physical', value: 0 } };
  assert.throws(() => service.act(DEVICE, { ...action, rulesVersion: 3 }), failure('battle_migration_required'));
  const next = service.act(DEVICE, action);
  assert.equal(next.battle?.rulesVersion, 2); assert.deepEqual(service.act(DEVICE, action), next);
  const stored = JSON.parse(await readFile(join(dataDir, 'battle-store.json'), 'utf8'));
  assert.equal(stored.devices[0].snapshotBase64, OLD_PHYSICAL);
});

test('future care versions cannot silently fall back to an older mirror', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-rpg-future-'));
  t.after(async () => rm(dataDir, { recursive: true, force: true }));
  const future = { ...oldCare([]), formatVersion: 999 };
  await writeFile(join(dataDir, 'store.json'), JSON.stringify(future));
  await writeFile(join(dataDir, 'store.backup.json'), JSON.stringify(oldCare([])));
  assert.throws(() => createApp({ rootDir, dataDir, corePath, battleCorePath }), /explicit migration/);
  assert.deepEqual(JSON.parse(await readFile(join(dataDir, 'store.json'), 'utf8')), future);
});

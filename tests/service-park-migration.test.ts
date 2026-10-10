import { historicComparable, legacyFields } from './legacy-state-projection.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { startServer } from '../service/server.ts';
import { createBattleService } from '../service/battle-service.ts';

const rootDir = resolve(import.meta.dirname, '..');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/park-baseline-service.json'), 'utf8'));
const inherited = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/park-baseline-inherited-trace.json'), 'utf8'));
const token = Buffer.alloc(32, 88).toString('base64url'); // Explicit public test identity.
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, checkpoint: any) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-park-migration-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(checkpoint.store));
  const options = { seedSource: () => 12345, rootDir, dataDir, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, port: 0 };
  let app = await startServer(options);
  const close = async () => { await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve())); app.close(); };
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown, path = body === undefined ? '/api/save' : '/api/save-sync') => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST',
      headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  return { dataDir, request, restart: async () => { await close(); app = await startServer(options); } };
}
function oldFields(current: any) {
  assert.equal(current.maxLevel, 50);
  const { schemaVersion, rulesVersion, wildRarity, recoveryRestCount, queuedEncounters, stepsToNextEncounter, walking, ...state } = legacyFields(current);
  assert.deepEqual(walking, { rate: 2, name: 'Normal', eligibleSteps: 0, encounters: 0, rngState: 0, target: 0, progress: 0, remainingSteps: 0 });
  assert.equal(schemaVersion, 26); assert.equal(rulesVersion, 19);
  assert.equal(wildRarity, null, 'historical encounter/result cannot acquire a rarity label retroactively');
  assert.ok(Number.isInteger(recoveryRestCount) && recoveryRestCount >= 0 && recoveryRestCount <= 40);
  assert.equal(queuedEncounters, Math.floor(state.stepCredit / 100));
  assert.equal(stepsToNextEncounter, state.stepCredit >= 100 ? 0 : 100 - state.stepCredit);
  return { ...state, schemaVersion: 12, rulesVersion: 9 };
}

for (const name of ['egg', 'encounter', 'autoResult', 'afterCare']) test(`genuine9 ${name} migrates once without rerolls, rewards or relabeled pending commands`, async t => {
  const checkpoint = frozen.care.checkpoints[name], f = await fixture(t, checkpoint);
  const current = await f.request(); assert.equal(current.status, 200);
  assert.deepEqual(historicComparable(oldFields(current.body.state)), historicComparable(checkpoint.response.body.state));
  assert.deepEqual(current.body.autoTrace, checkpoint.response.body.autoTrace);
  assert.equal(current.body.revision, checkpoint.response.body.revision);
  const store = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([store.formatVersion, store.gameSchemaVersion, store.rulesVersion], [21, 26, 19]);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v9.json'), 'utf8')), checkpoint.store);
  for (const command of frozen.care.commands.slice(0, current.body.revision)) {
    assert.equal((await f.request(command.body)).body.error, 'migration_required');
    assert.equal((await f.request({ ...command.body, rulesVersion: 19 })).body.error, 'legacy_batch_requires_reconciliation');
  }
  await f.restart(); assert.deepEqual(await f.request(), current);
  if (name === 'encounter') {
    const rejected = await f.request({ rulesVersion: 19, baseRevision: 1, batchId: 'park-mixed-auto-rollback', events: [{ type: 'auto', value: 0 }, { type: 'attack', value: 0 }] });
    assert.equal(rejected.status, 422); assert.deepEqual(await f.request(), current);
    const oldAuto = JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core'), ['--replay-v9-onboarding-trace', String(checkpoint.store.devices[0].seed)], { input: [...checkpoint.store.devices[0].events, ...frozen.care.commands[1].body.events].map((e: any) => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8' }));
    assert.deepEqual(oldAuto.state, frozen.care.commands[1].response.body.state); assert.deepEqual(oldAuto.trace, frozen.care.commands[1].response.body.autoTrace);
    assert.equal((await f.request({ ...frozen.care.commands[1].body, rulesVersion: 19, batchId: 'park-reject-frozen-nine-auto' })).status, 422);
    const command = { rulesVersion: 19, baseRevision: 1, batchId: 'park-clear-frozen-nine-test', events: [{ type: 'resolve-test-encounter', value: 0 }] };
    const accepted = await f.request(command); assert.equal(accepted.status, 200);
    for (const key of ['hp', 'energy', 'xp', 'journal', 'captures', 'rngState', 'activeCreatureId', 'nextMemberId']) assert.deepEqual(accepted.body.state[key], current.body.state[key], key);
    // Stored care values are unchanged; leaving a pre12 fight re-enables
    // the existing Home display bonus for this mood80 Impmon.
    const expectedMembers = structuredClone(current.body.state.collection);
    const active = expectedMembers.find((member: any) => member.id === current.body.state.activeCreatureId);
    assert.equal(active.mood, 80); assert.equal(active.care.offenseBonus, 0);
    active.care.offenseBonus = 1; active.care.effective.attack = active.combat.attack + 1; active.care.effective.magic = active.combat.magic + 1;
    assert.deepEqual(accepted.body.state.collection, expectedMembers);
    assert.equal(accepted.body.state.phase, 'home'); assert.equal(accepted.body.state.sequence, current.body.state.sequence + 1);
    assert.deepEqual(accepted.body.autoTrace, current.body.autoTrace);
    await f.restart(); assert.deepEqual(await f.request(command), accepted);
  }
});

for (const name of ['inherited8', 'afterRest', 'newAuto']) test(`second migration preserves the correct recorded trace for ${name}`, async t => {
  const checkpoint = inherited.checkpoints[name], f = await fixture(t, checkpoint), saved = await f.request();
  assert.equal(saved.status, 200); assert.deepEqual(historicComparable(oldFields(saved.body.state)), historicComparable(checkpoint.response.body.state));
  assert.deepEqual(saved.body.autoTrace, checkpoint.response.body.autoTrace);
  assert.equal(saved.body.autoTrace.endSequence, saved.body.state.lastAutoBattle.sequence);
  const body = { rulesVersion: 19, baseRevision: saved.body.revision, batchId: `park-keep-trace-${name}`, events: [{ type: 'rest', value: 0 }] };
  const accepted = await f.request(body); assert.equal(accepted.status, 200); assert.deepEqual(accepted.body.autoTrace, saved.body.autoTrace);
  await f.restart(); assert.deepEqual(await f.request(body), accepted);
});

test('native recovery/rarity metadata is bounded and only existing Rest batches mutate recovery', async t => {
  const f = await fixture(t, frozen.care.checkpoints.afterCare);
  let saved = (await f.request()).body;
  const recovery = { rulesVersion: 19, baseRevision: saved.revision, batchId: 'park-native-count-recovery', events: Array.from({ length: saved.state.recoveryRestCount }, () => ({ type: 'rest', value: 0 })) };
  assert.ok(recovery.events.length > 0 && recovery.events.length <= 40);
  for (const events of [Array.from({ length: 101 }, () => ({ type: 'rest', value: 0 })), [{ type: 'rest', value: 0 }, { type: 'attack', value: 0 }]]) {
    assert.equal((await f.request({ ...recovery, events })).status, 422);
    assert.deepEqual((await f.request()).body, saved);
  }
  const recovered = await f.request(recovery); assert.equal(recovered.status, 200);
  assert.equal(recovered.body.state.recoveryRestCount, 0); assert.equal(recovered.body.state.energy, 100);
  assert.equal(recovered.body.state.hp, recovered.body.state.combat.maxHp); assert.equal(recovered.body.state.xp, saved.state.xp + 2);
  assert.equal((await f.request({ ...recovery, batchId: 'park-stale-recovery-command' })).body.error, 'revision_conflict');
  await f.restart(); assert.deepEqual(await f.request(recovery), recovered);
  saved = recovered.body;
  const encounter = await f.request({ rulesVersion: 19, baseRevision: saved.revision, batchId: 'park-current-rarity-encounter', events: [{ type: 'walk', value: 100 }] });
  assert.equal(encounter.status, 200); assert.equal(encounter.body.state.wildRules, 19); assert.equal(encounter.body.state.recoveryRestCount, 0);
  assert.ok(['common', 'uncommon', 'rare'].includes(encounter.body.state.wildRarity));
  const detail = await f.request(undefined, `/api/roster/${encounter.body.state.wildFormId}`);
  assert.equal(detail.status, 200); assert.equal(detail.body.form.encounterRarity, encounter.body.state.wildRarity);
  const page = await f.request(undefined, `/api/roster?ids=${encounter.body.state.wildFormId}`);
  assert.equal(page.body.entries[0].encounterRarity, encounter.body.state.wildRarity);
});

test('carried-over wild9 still refuses release while new Auto encounters allow an explicit nonactive release before resolution', async t => {
  const checkpoint = structuredClone(frozen.care.checkpoints.afterCare);
  const device = checkpoint.store.devices[0], events = [{ type: 'walk', value: 100 }];
  // A valid rules9 suffix on a genuinely captured save. The frozen native9
  // executor applies it during migration, so this is not a fabricated snapshot.
  device.receipts.push({ batchId: 'park-old-nine-next-encounter', revision: device.revision + 1, eventEnd: device.events.length + 1,
    bodyHash: createHash('sha256').update(JSON.stringify({ rulesVersion: 9, baseRevision: device.revision, events })).digest('hex') });
  device.events.push(...events); device.revision++;
  const old = await fixture(t, checkpoint), before = await old.request();
  assert.equal(before.body.state.wildRules, 9); assert.equal(before.body.state.collection.length, 2);
  const release = { rulesVersion: 19, baseRevision: before.body.revision, batchId: 'park-old-encounter-release-blocked', events: [{ type: 'release', value: 2 }] };
  assert.equal((await old.request(release)).status, 422); assert.deepEqual(await old.request(), before);

  const current = await fixture(t, frozen.care.checkpoints.afterCare), home = await current.request();
  const encountered = await current.request({ rulesVersion: 19, baseRevision: home.body.revision, batchId: 'park-new-auto-make-room', events });
  assert.equal(encountered.status, 200); assert.equal(encountered.body.state.wildRules, 19); assert.equal(encountered.body.state.battleMode, 'auto');
  const currentRelease = { ...release, baseRevision: encountered.body.revision, batchId: 'park-confirm-nonactive-release' };
  const released = await current.request(currentRelease); assert.equal(released.status, 200);
  assert.equal(released.body.state.collection.length, 1); assert.equal(released.body.state.phase, 'encounter');
  for (const key of ['rngState', 'hp', 'activeCreatureId', 'wildFormId', 'wildHp', 'wildTurn', 'captureAttempts', 'cardUsed', 'battleMode']) assert.deepEqual(released.body.state[key], encountered.body.state[key], key);
  await current.restart(); assert.deepEqual(await current.request(currentRelease), released);
  const resolved = await current.request({ rulesVersion: 19, baseRevision: released.body.revision, batchId: 'park-confirm-auto-after-room', events: [{ type: 'auto', value: 0 }] });
  assert.equal(resolved.status, 200); assert.equal(resolved.body.state.phase, 'home');
  assert.ok(resolved.body.autoTrace); assert.equal(resolved.body.state.sequence, released.body.state.sequence + 1);
  assert.deepEqual(await current.request(currentRelease), released, 'replaying the release ACK after battle completion cannot remove a new capture');
});

test('practice7 wrapper, exact Auto outcome and active Tactical continuation are unchanged by care10', async t => {
  for (const previous of frozen.practice) await t.test(previous.mode, async sub => {
    const dataDir = await mkdtemp(join(tmpdir(), 'digivice-park-practice-'));
    for (const name of ['battle-store.json', 'battle-store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(previous.store));
    let service = createBattleService({ rootDir, dataDir, corePath: process.env.DIGIVICE_TEST_BATTLE_PATH, seed: () => { throw new Error('Historical reads/retries cannot choose a new seed'); } });
    sub.after(async () => { service.close(); await rm(dataDir, { recursive: true, force: true }); });
    assert.deepEqual(service.get(previous.deviceId), previous.expectedCurrent);
    assert.deepEqual(JSON.parse(await readFile(join(dataDir, 'battle-store.json'), 'utf8')), previous.store);
    for (const command of [...previous.commands, ...previous.continuation]) {
      const accepted = command.operation === 'start' ? service.start(previous.deviceId, command.body, previous.companion) : service.act(previous.deviceId, command.body);
      assert.deepEqual(accepted, command.response);
    }
    service.close(); service = createBattleService({ rootDir, dataDir, corePath: process.env.DIGIVICE_TEST_BATTLE_PATH });
    for (const command of previous.commands) assert.deepEqual(command.operation === 'start' ? service.start(previous.deviceId, command.body, previous.companion) : service.act(previous.deviceId, command.body), command.response);
  });
});

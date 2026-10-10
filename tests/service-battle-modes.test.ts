import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { BattleError, createBattleService, type BattleProfile } from '../service/battle-service.ts';
import { parseAutoTrace } from '../service/battle-trace.ts';
const rootDir = resolve(import.meta.dirname, '..');
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
const DEVICE = `dv_${'9'.repeat(24)}`;
const PROFILE: BattleProfile = { id: 1, species: 'impmon', name: 'Impmon', level: 1, formId: 11 };
const command = (revision: number, requestId: string, mode?: 'auto' | 'tactical') => ({ rulesVersion: 7, expectedRevision: revision, requestId, ...(mode ? { mode } : {}) });
const failure = (code: string) => (error: unknown) => error instanceof BattleError && error.code === code;
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, saved?: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-battle-modes-'));
  if (saved) for (const file of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, file), JSON.stringify(saved));
  let app = await startServer({ seedSource: () => 12345, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (path: string, token?: string, body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST',
      headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  const pair = async () => { const code = (await request('/api/pairing/start', undefined, {})).body.code; return (await request('/api/pairing/claim', undefined, { code })).body; };
  return { request, pair, dataDir, restart: async () => { await close(); app = await startServer({ seedSource: () => 12345, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 }); } };
}

test('wild Auto requires explicit confirmation, resolves once, and preserves captured result and trace across reload', async t => {
  const f = await fixture(t), identity = await f.pair();
  const prepare = { rulesVersion: 17, baseRevision: 0, batchId: 'prepare-wild-auto', events: [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }, { type: 'walk', value: 100 }] };
  const encounter = await f.request('/api/save-sync', identity.token, prepare);
  assert.equal(encounter.status, 200); assert.equal(encounter.body.state.phase, 'encounter'); assert.equal(encounter.body.state.battleMode, 'auto'); assert.equal(encounter.body.autoTrace, null);
  const before = (await f.request('/api/save', identity.token)).body;
  await f.restart(); assert.deepEqual((await f.request('/api/save', identity.token)).body, before, 'GET/reload must not confirm or resolve Auto');
  for (const event of [{ type: 'mode', value: 0 }, { type: 'attack', value: 0 }, { type: 'card', value: 1 }, { type: 'capture', value: 0 }]) {
    assert.equal((await f.request('/api/save-sync', identity.token, { rulesVersion: 17, baseRevision: 1, batchId: `blocked-${event.type}`, events: [event] })).status, 422);
  }
  assert.deepEqual((await f.request('/api/save', identity.token)).body, before);
  const body = { rulesVersion: 17, baseRevision: 1, batchId: 'confirmed-wild-auto', events: [{ type: 'auto', value: 0 }] };
  const accepted = await f.request('/api/save-sync', identity.token, body);
  assert.equal(accepted.status, 200); assert.equal(accepted.body.revision, 2); assert.equal(accepted.body.state.sequence, 4);
  assert.equal(accepted.body.state.phase, 'home'); assert.equal(accepted.body.state.captures, 1); assert.equal(accepted.body.state.collection.length, 2);
  assert.equal(accepted.body.state.collection[1].capturedAtSequence, 4);
  const trace = parseAutoTrace(accepted.body.autoTrace, 'wild')!;
  assert.equal(trace.outcome, 'captured'); assert.equal(trace.startSequence, 3); assert.equal(trace.endSequence, 4);
  assert.equal(trace.player.species, 'impmon'); assert.equal(trace.enemy.species, encounter.body.state.wildSpecies); assert.ok(trace.enemy.formId !== undefined && trace.enemy.formId >= 11); assert.ok(trace.steps.some(step => step.action === 'capture' && step.captured));
  assert.deepEqual(await f.request('/api/save-sync', identity.token, body), accepted, 'lost acknowledgement retry');
  const later = await f.request('/api/save-sync', identity.token, { rulesVersion: 17, baseRevision: 2, batchId: 'care-after-auto', events: [{ type: 'feed', value: 0 }, { type: 'select', value: 2 }, { type: 'mode', value: 0 }] });
  assert.equal(later.status, 200); assert.equal(later.body.state.species, encounter.body.state.wildSpecies); assert.deepEqual(later.body.autoTrace, trace, 'past participants must remain the original named pair after changing partners');
  assert.deepEqual(await f.request('/api/save-sync', identity.token, body), accepted);
  const mismatch = await f.request('/api/save-sync', identity.token, { ...body, events: [{ type: 'auto', value: 0 }, { type: 'feed', value: 0 }] });
  assert.equal(mismatch.status, 409); assert.equal(mismatch.body.error, 'batch_mismatch');
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', identity.token, body), accepted);
  await writeFile(join(f.dataDir, 'store.json'), '{torn'); await f.restart();
  assert.deepEqual(await f.request('/api/save-sync', identity.token, body), accepted);
  const recovered = (await f.request('/api/save', identity.token)).body;
  assert.equal(recovered.revision, 3); assert.equal(recovered.state.captures, 1); assert.deepEqual(recovered.autoTrace, trace);
});

test('Tactical wild encounters reject Auto or mode changes and keep existing manual behavior', async t => {
  const f = await fixture(t), identity = await f.pair();
  const entered = await f.request('/api/save-sync', identity.token, { rulesVersion: 17, baseRevision: 0, batchId: 'tactical-encounter', events: [{ type: 'hatch', value: 1 }, { type: 'walk', value: 100 }] });
  assert.equal(entered.status, 200); assert.equal(entered.body.state.battleMode, 'tactical'); assert.equal(entered.body.autoTrace, null);
  for (const event of [{ type: 'auto', value: 0 }, { type: 'mode', value: 1 }]) assert.equal((await f.request('/api/save-sync', identity.token, { rulesVersion: 17, baseRevision: 1, batchId: `tactical-block-${event.type}`, events: [event] })).status, 422);
  const attack = await f.request('/api/save-sync', identity.token, { rulesVersion: 17, baseRevision: 1, batchId: 'tactical-manual-attack', events: [{ type: 'attack', value: 0 }] });
  assert.equal(attack.status, 200); assert.ok(attack.body.state.wildHp < entered.body.state.wildHp); assert.equal(attack.body.autoTrace, null);
});

test('practice Auto stores compact native replay boundaries and exact retry cannot reroll or accept manual actions', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-practice-auto-')); let seedCalls = 0;
  const options = { dataDir, rootDir, corePath: process.env.DIGIVICE_TEST_BATTLE_PATH, seed: () => { seedCalls++; return 12345; } };
  let service = createBattleService(options);
  t.after(async () => { service.close(); await rm(dataDir, { recursive: true, force: true }); });
  assert.deepEqual(service.get(DEVICE), { revision: 0, battle: null, mode: 'tactical', autoTrace: null }); assert.equal(seedCalls, 0);
  const body = command(0, 'confirmed-practice-auto', 'auto');
  const accepted = service.start(DEVICE, body, PROFILE);
  assert.equal(seedCalls, 1); assert.equal(accepted.revision, 1); assert.equal(accepted.mode, 'auto'); assert.equal(accepted.battle?.phase, 'finished'); assert.notEqual(accepted.battle?.status, 'active');
  assert.ok(accepted.autoTrace && accepted.autoTrace.steps.length >= 1 && accepted.autoTrace.steps.length <= 40);
  assert.equal(accepted.autoTrace?.player.species, 'impmon'); assert.equal(accepted.autoTrace?.outcome, accepted.battle?.status);
  assert.equal(/snapshotBase64|initialSnapshot|rngState|committed|"seed"/.test(JSON.stringify(accepted)), false);
  for (const type of ['physical', 'card', 'retreat']) assert.throws(() => service.act(DEVICE, { ...command(1, `manual-after-auto-${type}`), action: { type, value: type === 'card' ? 1 : 0 } }), failure('battle_auto_complete'));
  assert.throws(() => service.start(DEVICE, { ...body, mode: 'tactical' }, PROFILE), failure('request_mismatch'));
  assert.deepEqual(service.start(DEVICE, body, PROFILE), accepted); assert.equal(seedCalls, 1);
  const store = JSON.parse(await readFile(join(dataDir, 'battle-store.json'), 'utf8'));
  assert.equal(store.formatVersion, 8); assert.equal(store.devices[0].mode, 'auto');
  assert.equal(Buffer.from(store.devices[0].initialSnapshotBase64, 'base64').length, 120);
  assert.equal(Buffer.from(store.devices[0].snapshotBase64, 'base64').length, 120);
  assert.equal(JSON.stringify(store).includes('"steps"'), false, 'trace should be regenerated, never copied into receipt storage');
  service.close(); service = createBattleService(options); assert.deepEqual(service.get(DEVICE), accepted);
  assert.deepEqual(service.start(DEVICE, body, PROFILE), accepted); assert.equal(seedCalls, 1);
  const tactical = service.start(DEVICE, command(1, 'later-tactical-start', 'tactical'), PROFILE);
  assert.equal(tactical.mode, 'tactical'); assert.equal(tactical.battle?.status, 'active'); assert.equal(tactical.autoTrace, null);
  assert.deepEqual(service.start(DEVICE, body, PROFILE), accepted); assert.equal(seedCalls, 2, 'historical Auto retry cannot select another seed after later Tactical start');
  assert.throws(() => service.start(DEVICE, command(2, 'midfight-mode-change', 'auto'), PROFILE), failure('battle_active'));
});

test('pre-mode practice receipts migrate to Tactical without changing old hashes or snapshots', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-old-practice-mode-'));
  t.after(async () => rm(dataDir, { recursive: true, force: true }));
  const frozen = JSON.parse(await readFile(join(rootDir, 'tests/fixtures/practice-v2.json'), 'utf8'));
  const snapshotBase64 = frozen.find((entry: any) => entry.start.state.playerSpecies === 'impmon').start.snapshotBase64;
  const body = { ...command(0, 'old-start-without-mode'), rulesVersion: 2 };
  const PROFILE: BattleProfile = { id: 1, species: 'impmon', name: 'Impmon', level: 3 };
  const receipt = { requestId: body.requestId, bodyHash: hash(JSON.stringify({ rulesVersion: 2, operation: 'start', expectedRevision: 0 })), revision: 1, snapshotBase64, companion: PROFILE };
  const old = { formatVersion: 2, practiceSchemaVersion: 2, practiceRulesVersion: 2, generation: 1, devices: [{ deviceId: DEVICE, revision: 1, snapshotBase64, companion: PROFILE, receipts: [receipt], legacyRequestIds: [] }] };
  for (const file of ['battle-store.json', 'battle-store.backup.json']) await writeFile(join(dataDir, file), JSON.stringify(old));
  const service = createBattleService({ dataDir, rootDir, corePath: process.env.DIGIVICE_TEST_BATTLE_PATH, seed: () => { throw new Error('Retry must not choose a seed'); } }); t.after(() => service.close());
  const accepted = service.start(DEVICE, body, PROFILE); assert.equal(accepted.revision, 1); assert.equal(accepted.mode, 'tactical'); assert.equal(accepted.autoTrace, null);
  assert.equal(accepted.battle?.status, 'active'); assert.throws(() => service.start(DEVICE, { ...body, mode: 'auto' }, PROFILE), failure('request_mismatch'));
  const stored = JSON.parse(await readFile(join(dataDir, 'battle-store.json'), 'utf8'));
  assert.deepEqual(stored.devices[0].receipts[0], { ...receipt, mode: 'tactical', initialSnapshotBase64: null });
  assert.equal(stored.devices[0].snapshotBase64, snapshotBase64);
  assert.deepEqual(JSON.parse(await readFile(join(dataDir, 'battle-store.format-v2.json'), 'utf8')), old);
});

test('schema-five onboarding histories migrate intact and default to Tactical, including an unhatched egg', async t => {
  const tokens = [7, 8].map(value => Buffer.alloc(32, value).toString('base64url'));
  const events = [{ type: 'hatch', value: 8 }, { type: 'feed', value: 0 }], batchId = 'old-schema-five-hatch';
  const devices = tokens.map((token, index) => ({ deviceId: `dv_${String(index + 7).repeat(24)}`, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding', legacy: null,
    revision: index ? 0 : 1, events: index ? [] : events, receipts: index ? [] : [{ batchId, bodyHash: hash(JSON.stringify({ rulesVersion: 3, baseRevision: 0, events })), revision: 1, eventEnd: 2 }] }));
  const old = { formatVersion: 4, gameSchemaVersion: 5, rulesVersion: 3, devices }, f = await fixture(t, old);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(stored.formatVersion, 19); assert.equal(stored.gameSchemaVersion, 24);
  for (let i = 0; i < devices.length; i++) {
    assert.equal(stored.devices[i].deviceId, devices[i].deviceId); assert.equal(stored.devices[i].revision, devices[i].revision);
    assert.deepEqual(stored.devices[i].events, []); assert.deepEqual(stored.devices[i].receipts, []);
    assert.deepEqual(stored.devices[i].legacy.histories[0].events, devices[i].events);
    assert.deepEqual(stored.devices[i].legacy.histories[0].receipts, devices[i].receipts);
  }
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v3.json'), 'utf8')), old);
  const current = (await f.request('/api/save', tokens[0])).body; assert.equal(current.state.species, 'renamon'); assert.equal(current.state.battleMode, 'tactical'); assert.equal(current.autoTrace, null);
  const egg = (await f.request('/api/save', tokens[1])).body; assert.equal(egg.state.phase, 'egg'); assert.equal(egg.state.battleMode, 'tactical');
  const retry = await f.request('/api/save-sync', tokens[0], { rulesVersion: 3, baseRevision: 0, batchId, events }); assert.equal(retry.status, 409); assert.equal(retry.body.error, 'migration_required');
  const relabeled = await f.request('/api/save-sync', tokens[0], { rulesVersion: 17, baseRevision: 0, batchId, events });
  assert.equal(relabeled.body.error, 'legacy_batch_requires_reconciliation');
  await f.restart(); assert.deepEqual((await f.request('/api/save', tokens[0])).body, current);
});

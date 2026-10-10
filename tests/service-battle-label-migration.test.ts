import { legacyFields } from './legacy-state-projection.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { BattleError, createBattleService } from '../service/battle-service.ts';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH;
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH;
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/battle-labels-frozen-rules7.json'), 'utf8'));
const DEVICE = `dv_${'7'.repeat(24)}`, TOKEN = Buffer.alloc(32, 71).toString('base64url'); // Public test fixture only.
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
type Event = { type: string; value: number };
function receipts(rulesVersion: number, revisionOffset: number, events: Event[], prefix: string) {
  return Array.from({ length: Math.ceil(events.length / 100) }, (_, index) => ({
    batchId: `${prefix}-${index}`, revision: revisionOffset + index + 1, eventEnd: Math.min(events.length, (index + 1) * 100),
    bodyHash: hash(JSON.stringify({ rulesVersion, baseRevision: revisionOffset + index, events: events.slice(index * 100, (index + 1) * 100) })),
  }));
}
function oldStore(events: Event[], initialMode = 'onboarding', legacy: any = null) {
  const revisionOffset = legacy?.histories.reduce((sum: number, history: any) => sum + history.receipts.length, 0) ?? 0;
  const batches = receipts(7, revisionOffset, events, 'frozen-seven-batch');
  return { formatVersion: 9, gameSchemaVersion: 10, rulesVersion: 7, devices: [{ deviceId: DEVICE, tokenHash: hash(TOKEN), seed: 12345,
    revision: revisionOffset + batches.length, initialMode, legacy, events, receipts: batches }] };
}
// Migration intentionally refreshes current Home label metadata. Everything
// numeric/owned remains comparable; historic encounter traces are tested exact.
function withoutLabels(value: any): any {
  return Array.isArray(value) ? value.map(withoutLabels) : value && typeof value === 'object'
    ? Object.fromEntries(Object.entries(value).filter(([key]) => key !== 'skills').map(([key, child]) => [key, withoutLabels(child)])) : value;
}
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 17, baseRevision, batchId, events });
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, saved: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-label-epoch-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(saved));
  let app = await startServer({ seedSource: () => 12345, rootDir, dataDir, corePath, battleCorePath, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  async function request(path: string, body?: unknown) {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST', headers: { authorization: `Bearer ${TOKEN}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  }
  return { dataDir, request, restart: async () => { await close(); app.close(); app = await startServer({ seedSource: () => 12345, rootDir, dataDir, corePath, battleCorePath, port: 0 }); } };
}

test('rules7 baseline and suffix preserve progress, archive exact receipts, and expose reviewed current Home labels', async t => {
  const history6 = { rulesVersion: 6, events: frozen.prefixEvents, receipts: receipts(6, 0, frozen.prefixEvents, 'before-seven-baseline') };
  const old = oldStore(frozen.suffixEvents, 'onboarding', { histories: [history6], snapshotBase64: frozen.baseline.snapshotBase64 });
  const f = await fixture(t, old), saved = (await f.request('/api/save')).body;
  assert.deepEqual([saved.state.schemaVersion, saved.state.rulesVersion, saved.revision], [24, 17, old.devices[0].revision]);
  assert.equal(saved.baseSequence, frozen.prefixEvents.length + frozen.suffixEvents.length); assert.deepEqual(saved.events, []); assert.equal(saved.autoTrace, null);
  for (const key of ['collection', 'activeCreatureId', 'nextMemberId', 'journal', 'rngState', 'xp', 'formId', 'hp', 'bond', 'captures', 'onboarding', 'battleMode', 'lastAutoBattle']) assert.deepEqual(withoutLabels(legacyFields(saved.state[key])), withoutLabels(frozen.suffixResult.state[key]), key);
  assert.equal(frozen.suffixResult.state.combat.skills.magic, 'Hex Spark'); assert.equal(saved.state.combat.skills.magic, 'Thunder Cloud');
  assert.equal(saved.state.wildCaptureChance, 0);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.gameSchemaVersion, stored.rulesVersion], [19, 24, 17]);
  const snapshot = Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64'); assert.equal(snapshot.length, 3216); assert.equal(snapshot.readUInt16LE(4), 24); assert.equal(snapshot.readUInt32LE(8), 17);
  assert.deepEqual(stored.devices[0].legacy.histories, [history6, { rulesVersion: 7, events: old.devices[0].events, receipts: old.devices[0].receipts }]);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v7.json'), 'utf8')), old);
  const pending = { rulesVersion: 7, baseRevision: history6.receipts.length, batchId: old.devices[0].receipts[0].batchId, events: old.devices[0].events };
  assert.equal((await f.request('/api/save-sync', pending)).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', { ...pending, rulesVersion: 17 })).body.error, 'legacy_batch_requires_reconciliation');
  assert.equal((await f.request('/api/save-sync', { ...pending, rulesVersion: 17, events: [{ type: 'feed', value: 0 }] })).body.error, 'legacy_batch_requires_reconciliation');
  await f.restart(); assert.deepEqual((await f.request('/api/save')).body, saved);
  const input = batch(saved.revision, 'eight-care-ack-retry', [{ type: 'feed', value: 0 }]);
  const accepted = await f.request('/api/save-sync', input); assert.equal(accepted.status, 200); assert.equal(accepted.body.state.xp, saved.state.xp + 2);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', input), accepted);
  const detail = await f.request('/api/roster/12'); assert.equal(detail.body.form.skillLabelOrigin, 'mixed-reviewed-official-and-authored'); assert.equal(detail.body.form.combat.skills.magic, 'Thunder Cloud');
});

test('active rules7 wild battle retains old damage, labels and Auto outcome, with one reward across retry', async t => {
  const old = oldStore(frozen.encounterEvents), f = await fixture(t, old);
  const saved = (await f.request('/api/save')).body; assert.equal(saved.state.wildRules, 7);
  assert.equal(saved.state.combat.skills.magic, 'Hex Spark');
  const input = batch(saved.revision, 'frozen-seven-fight-finish', [{ type: 'auto', value: 0 }]);
  const result = await f.request('/api/save-sync', input); assert.equal(result.status, 200);
  for (const key of ['hp', 'energy', 'xp', 'collection', 'journal', 'nextMemberId', 'captures', 'rngState']) assert.deepEqual(withoutLabels(legacyFields(result.body.state[key])), withoutLabels(frozen.autoResult.state[key]), key);
  assert.deepEqual(result.body.autoTrace, frozen.autoResult.trace); assert.equal(result.body.autoTrace.player.combat.skills.magic, 'Hex Spark');
  assert.equal(result.body.state.combat.skills.magic, 'Thunder Cloud');
  const later = await f.request('/api/save-sync', batch(result.body.revision, 'later-eight-care', [{ type: 'rest', value: 0 }])); assert.equal(later.status, 200);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', input), result); assert.equal((await f.request('/api/save')).body.revision, later.body.revision);
});

test('zero-event rules7 legacy and Egg initializers remain distinct without resetting identities', async t => {
  for (const initialMode of ['legacy', 'onboarding']) {
    const f = await fixture(t, oldStore([], initialMode)); const saved = (await f.request('/api/save')).body;
    const expected = initialMode === 'legacy' ? frozen.zeroLegacy.state : frozen.emptyEgg.state;
    for (const key of ['collection', 'activeCreatureId', 'onboarding', 'phase', 'rngState']) assert.deepEqual(withoutLabels(legacyFields(saved.state[key])), withoutLabels(expected[key]));
    assert.equal(saved.revision, 0); assert.equal(saved.state.wildCaptureChance, 0);
    await f.restart(); assert.deepEqual((await f.request('/api/save')).body, saved);
  }
});

function practiceHash(body: any, operation = 'start') {
  return hash(JSON.stringify({ rulesVersion: body.rulesVersion, operation, expectedRevision: body.expectedRevision,
    ...(operation === 'act' ? { action: body.action } : Object.hasOwn(body, 'mode') ? { mode: body.mode } : {}) }));
}
async function practiceFixture(t: { after: (fn: () => Promise<void>) => unknown }, auto: boolean) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-old-practice-labels-'));
  const body = { rulesVersion: 4, expectedRevision: 0, requestId: 'frozen-four-practice-start', ...(auto ? { mode: 'auto' as const } : {}) };
  const output = auto ? frozen.practice.auto : frozen.practice.start;
  const record = { snapshotBase64: output.snapshotBase64, companion: frozen.practice.profile, mode: auto ? 'auto' : 'tactical', initialSnapshotBase64: auto ? output.initialSnapshotBase64 : null };
  const receipt = { ...record, requestId: body.requestId, bodyHash: practiceHash(body), revision: 1 };
  const old = { formatVersion: 5, practiceSchemaVersion: 4, practiceRulesVersion: 4, generation: 1, devices: [{ ...record, deviceId: DEVICE, revision: 1, receipts: [receipt], legacyRequestIds: [] }] };
  for (const name of ['battle-store.json', 'battle-store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(old));
  let service = createBattleService({ rootDir, dataDir, corePath: battleCorePath, seed: () => 12345 });
  t.after(async () => { service.close(); await rm(dataDir, { recursive: true, force: true }); });
  return { dataDir, old, body, output, get service() { return service; }, restart: () => { service.close(); service = createBattleService({ rootDir, dataDir, corePath: battleCorePath, seed: () => 12345 }); } };
}
function coreState(response: any) { const { companion, enemy, ...state } = response.battle; return state; }
const errorCode = (code: string) => (error: unknown) => error instanceof BattleError && error.code === code;

test('legacy practice4 Tactical continues and retries with exact old labels while new starts require7', async t => {
  const f = await practiceFixture(t, false), current = f.service.get(DEVICE);
  assert.deepEqual(coreState(current), frozen.practice.start.state); assert.equal(current.battle?.playerCombat.skills.magic, 'Hex Spark');
  assert.deepEqual(f.service.start(DEVICE, f.body, frozen.practice.profile), current);
  assert.throws(() => f.service.start(DEVICE, { ...f.body, rulesVersion: 6 }, frozen.practice.profile), errorCode('request_mismatch'));
  assert.throws(() => f.service.act(DEVICE, { rulesVersion: 7, expectedRevision: 1, requestId: 'wrong-epoch-four-heavy', action: { type: 'heavy', value: 0 } }), errorCode('battle_migration_required'));
  const input = { rulesVersion: 4, expectedRevision: 1, requestId: 'old-four-heavy-continuation', action: { type: 'heavy', value: 0 } };
  const result = f.service.act(DEVICE, input); assert.deepEqual(coreState(result), frozen.practice.heavy.state);
  await f.restart(); assert.deepEqual(f.service.act(DEVICE, input), result);
  f.service.act(DEVICE, { rulesVersion: 4, expectedRevision: 2, requestId: 'old-four-retreat-before-five', action: { type: 'retreat', value: 0 } });
  assert.throws(() => f.service.start(DEVICE, { rulesVersion: 4, expectedRevision: 3, requestId: 'uncommitted-four-start-now' }, frozen.practice.profile), errorCode('battle_migration_required'));
  const started = f.service.start(DEVICE, { rulesVersion: 7, expectedRevision: 3, requestId: 'current-five-practice-start' }, frozen.practice.profile);
  assert.equal(started.battle?.rulesVersion, 7); assert.equal(started.battle?.maxExchanges, 40); assert.equal(started.battle?.playerCombat.skills.magic, 'Thunder Cloud');
  assert.deepEqual(f.service.start(DEVICE, f.body, frozen.practice.profile), current);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'battle-store.format-v5.json'), 'utf8')), f.old);
});

test('legacy practice4 Auto initial and terminal snapshots, trace and receipt survive wrapper upgrade exactly', async t => {
  const f = await practiceFixture(t, true), old = f.service.get(DEVICE);
  assert.deepEqual(coreState(old), frozen.practice.auto.state); assert.deepEqual(old.autoTrace, frozen.practice.auto.trace);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'battle-store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.practiceSchemaVersion, stored.practiceRulesVersion], [8, 7, 7]); assert.deepEqual(stored.devices, f.old.devices);
  const newer = f.service.start(DEVICE, { rulesVersion: 7, expectedRevision: 1, requestId: 'new-five-auto-after-frozen', mode: 'auto' }, frozen.practice.profile);
  assert.equal(newer.battle?.rulesVersion, 7); assert.equal(newer.autoTrace?.player.combat.skills.magic, 'Thunder Cloud');
  await f.restart(); assert.deepEqual(f.service.start(DEVICE, f.body, frozen.practice.profile), old); assert.deepEqual(f.service.get(DEVICE), newer);
});

test('a native forty-exchange practice5 draw and its complete trace restore and retry exactly', async t => {
  const snapshots = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/practice-v5.json'), 'utf8'));
  const output = snapshots.find((entry: any) => entry.formId === 38 && entry.automatic.state.exchanges === 40).automatic;
  assert.deepEqual([output.state.rulesVersion, output.state.maxExchanges, output.state.exchanges, output.state.status, output.trace.steps.length], [5, 40, 40, 'draw', 40]);
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-forty-exchange-'));
  const body = { rulesVersion: 5, expectedRevision: 0, requestId: 'forty-exchange-draw-retry', mode: 'auto' as const };
  const companion = { id: 1, species: 'patamon', name: 'Gryphonmon', level: 20, formId: 38 };
  const record = { snapshotBase64: output.snapshotBase64, initialSnapshotBase64: output.initialSnapshotBase64, companion, mode: 'auto' };
  const receipt = { ...record, requestId: body.requestId, bodyHash: practiceHash(body), revision: 1 };
  const saved = { formatVersion: 6, practiceSchemaVersion: 5, practiceRulesVersion: 5, generation: 1, devices: [{ ...record, deviceId: DEVICE, revision: 1, receipts: [receipt], legacyRequestIds: [] }] };
  for (const name of ['battle-store.json', 'battle-store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(saved));
  const options = { rootDir, dataDir, corePath: battleCorePath, seed: () => { throw new Error('Restoring a draw cannot reroll'); } };
  let service = createBattleService(options);
  t.after(async () => { service.close(); await rm(dataDir, { recursive: true, force: true }); });
  const current = service.get(DEVICE);
  assert.deepEqual(coreState(current), output.state); assert.deepEqual(current.autoTrace, output.trace);
  assert.deepEqual(service.start(DEVICE, body, companion), current);
  service.close(); service = createBattleService(options);
  assert.deepEqual(service.get(DEVICE), current); assert.deepEqual(service.start(DEVICE, body, companion), current);
});

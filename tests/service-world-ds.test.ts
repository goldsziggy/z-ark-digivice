import { legacyFields } from './legacy-state-projection.ts';
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { createBattleService, BattleError } from '../service/battle-service.ts';
const rootDir = resolve(import.meta.dirname, '..');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/world-ds-frozen-rules.json'), 'utf8'));
const runtime = JSON.parse(readFileSync(join(rootDir, 'data/world-ds-runtime.json'), 'utf8'));
const hash = (value: string) => createHash('sha256').update(value).digest('hex');
const DEVICE = `dv_${'7'.repeat(24)}`, TOKEN = Buffer.alloc(32, 7).toString('base64url'); // Public test identity.
type Event = { type: string; value: number };
function receipt(rulesVersion: number, baseRevision: number, events: Event[], batchId: string) { return { batchId, revision: baseRevision + 1, eventEnd: events.length, bodyHash: hash(JSON.stringify({ rulesVersion, baseRevision, events })) }; }
function oldCare(events: Event[], legacy: unknown = null, revision = 1) { return { formatVersion: 6, gameSchemaVersion: 7, rulesVersion: 4, devices: [{ deviceId: DEVICE, tokenHash: hash(TOKEN), seed: 12345, initialMode: 'onboarding', revision, legacy, events, receipts: events.length ? [receipt(4, revision - 1, events, 'frozen-four-game-batch')] : [] }] }; }
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 18, baseRevision, batchId, events });
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, saved?: unknown) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-world-ds-'));
  if (saved) for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(saved));
  let app = await startServer({ seedSource: () => 12345, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, rootDir, dataDir, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  async function request(path: string, body?: unknown, token = TOKEN) { const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST', headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) }); return { status: response.status, body: await response.json() as any }; }
  async function pair() { const code = (await request('/api/pairing/start', {}, '')).body.code; return (await request('/api/pairing/claim', { code }, '')).body; }
  return { dataDir, request, pair, restart: async () => { await close(); app.close(); app = await startServer({ seedSource: () => 12345, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, rootDir, dataDir, port: 0 }); } };
}

test('rules4 care migration preserves exact rewards, IDs and receipts and creates only held-form journal records', async t => {
  const old = oldCare(frozen.careAutoEvents), f = await fixture(t, old);
  const saved = (await f.request('/api/save')).body;
  assert.equal(saved.state.schemaVersion, 25); assert.equal(saved.state.rulesVersion, 18); assert.equal(saved.revision, 1); assert.equal(saved.baseSequence, 4); assert.deepEqual(saved.events, []);
  for (const key of ['rngState', 'hp', 'energy', 'xp', 'level', 'formId', 'captures', 'collection', 'lastAutoBattle']) assert.deepEqual(legacyFields(saved.state[key]), frozen.careAutoState[key], key);
  assert.equal(saved.state.nextMemberId, 3); assert.deepEqual(saved.state.journal, { capacity: 512, obtainedFormIds: [4, 11] }); assert.equal(saved.autoTrace, null);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(stored.formatVersion, 20); assert.equal(Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64').length, 3216);
  assert.deepEqual(stored.devices[0].legacy.histories, [{ rulesVersion: 4, events: old.devices[0].events, receipts: old.devices[0].receipts }]);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v4.json'), 'utf8')), old);
  const pending = { rulesVersion: 4, baseRevision: 0, batchId: old.devices[0].receipts[0].batchId, events: old.devices[0].events };
  assert.equal((await f.request('/api/save-sync', pending)).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', { ...pending, rulesVersion: 18 })).body.error, 'legacy_batch_requires_reconciliation');
  await f.restart(); assert.deepEqual((await f.request('/api/save')).body, saved);
});

test('rules4 suffix replays from its frozen500-byte baseline without inventing previously obtained forms', async t => {
  const events: Event[] = [{ type: 'hatch', value: 1 }, ...Array.from({ length: 20 }, () => ['feed', 'play', 'rest'].map(type => ({ type, value: 0 }))).flat()];
  const legacy = { histories: [{ rulesVersion: 3, events, receipts: [receipt(3, 0, events, 'old-three-training-batch')] }], snapshotBase64: frozen.legacyV3Baseline.snapshotBase64 };
  assert.equal(Buffer.from(legacy.snapshotBase64, 'base64').length, 500);
  const old = oldCare([{ type: 'evolve', value: 12 }], legacy, 2), f = await fixture(t, old);
  const saved = (await f.request('/api/save')).body;
  for (const key of ['rngState', 'xp', 'level', 'formId', 'bond']) assert.deepEqual(legacyFields(saved.state[key]), frozen.careAfterEvolve[key]);
  const expectedMembers = structuredClone(frozen.careAfterEvolve.collection);
  assert.equal(expectedMembers[0].combat.skills.magic, 'Hex Spark');
  expectedMembers[0].combat.skills.magic = 'Thunder Cloud'; // Reviewed display binding; every numeric/ownership field stays exact.
  assert.deepEqual(legacyFields(saved.state.collection), expectedMembers);
  assert.deepEqual(saved.state.journal.obtainedFormIds, [12]); assert.equal(saved.revision, 2);
  const archive = JSON.parse(await readFile(join(f.dataDir, 'store.rules-v4.json'), 'utf8')); assert.equal(archive.devices[0].legacy.snapshotBase64, legacy.snapshotBase64);
  const migrated = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8')); assert.deepEqual(migrated.devices[0].legacy.histories.map((h: any) => h.rulesVersion), [3, 4]);
  await f.restart(); assert.deepEqual((await f.request('/api/save')).body, saved);
});

test('rules4 frozen outcomes remain replayable while a saved test encounter clears without rewards', async t => {
  const f = await fixture(t, oldCare(frozen.careAutoEvents.slice(0, 3)));
  const before = (await f.request('/api/save')).body; assert.equal(before.state.wildRules, 4);
  const historical = JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core'), ['--migrate-v4-onboarding', '12345'], { input: [...frozen.careAutoEvents.slice(0, 3), { type: 'auto', value: 0 }].map((e: Event) => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8' })).state;
  for (const key of ['rngState', 'hp', 'energy', 'xp', 'level', 'captures', 'collection']) assert.deepEqual(legacyFields(historical[key]), frozen.careAutoState[key], `frozen outcome ${key}`);
  assert.ok(before.state.wildFormId < 11);
  assert.equal((await f.request('/api/save-sync', batch(1, 'reject-old-4-test-auto', [{ type: 'auto', value: 0 }]))).status, 422);
  assert.deepEqual((await f.request('/api/save')).body, before);
  const request = batch(1, 'finish-frozen-four-encounter', [{ type: 'resolve-test-encounter', value: 0 }]);
  const result = await f.request('/api/save-sync', request); assert.equal(result.status, 200);
  for (const key of ['hp', 'energy', 'xp', 'journal', 'nextMemberId', 'captures', 'rngState', 'activeCreatureId']) assert.deepEqual(result.body.state[key], before.state[key], `clearing test encounter preserves ${key}`);
  // Stored care values are unchanged; leaving a pre12 fight re-enables
  // the existing Home display bonus for this mood80 Impmon.
  const expectedMembers = structuredClone(before.state.collection);
  const active = expectedMembers.find((member: any) => member.id === before.state.activeCreatureId);
  assert.equal(active.mood, 80); assert.equal(active.care.offenseBonus, 0);
  active.care.offenseBonus = 1; active.care.effective.attack = active.combat.attack + 1; active.care.effective.magic = active.combat.magic + 1;
  assert.deepEqual(result.body.state.collection, expectedMembers);
  assert.equal(result.body.state.phase, 'home'); assert.equal(result.body.state.sequence, before.state.sequence + 1);
  assert.deepEqual(result.body.autoTrace, before.autoTrace);
  assert.equal(result.body.state.wildRules, 0);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', request), result);
});

test('capture/release retains journal history, stable IDs beyond eight and durable exact receipts', async t => {
  const f = await fixture(t), identity = await f.pair(), token = identity.token;
  let current = (await f.request('/api/save-sync', batch(0, 'world-ds-initial-hatch', [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }]), token)).body;
  const capturedIds = new Set<number>(); let lastRelease: any; let lastResponse: any;
  for (let attempt = 0; attempt < 60 && current.state.nextMemberId <= 10; attempt++) {
    const events = [...Array.from({ length: 8 }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }, { type: 'auto', value: 0 }];
    const result = await f.request('/api/save-sync', batch(current.revision, `capture-attempt-${String(attempt).padStart(3, '0')}`, events), token);
    assert.equal(result.status, 200); current = result.body; assert.equal(current.state.phase, 'home');
    if (current.state.collection.length < 2) continue;
    const member = current.state.collection.find((m: any) => m.id !== 1); assert.ok(member); assert.equal(capturedIds.has(member.id), false); capturedIds.add(member.id);
    if (member.id === 9) {
      const selected = await f.request('/api/save-sync', batch(current.revision, 'select-nine-stable-id', [{ type: 'select', value: member.id }]), token);
      assert.equal(selected.status, 200); current = selected.body; assert.equal(current.state.activeCreatureId, 9);
      const duel = await f.request('/api/battle/start', { rulesVersion: 7, expectedRevision: 0, requestId: 'practice-with-stable-id-nine' }, token);
      assert.equal(duel.status, 200); assert.equal(duel.body.battle.companion.id, 9);
      assert.equal((await f.request('/api/save-sync', batch(current.revision, 'release-locked-by-practice', [{ type: 'release', value: 1 }]), token)).body.error, 'partner_locked');
      assert.deepEqual(await f.request('/api/save-sync', lastRelease, token), lastResponse, 'old committed release still acknowledges during a later practice battle');
      assert.equal((await f.request('/api/battle/act', { rulesVersion: 7, expectedRevision: 1, requestId: 'retreat-stable-id-nine', action: { type: 'retreat', value: 0 } }, token)).status, 200);
      current = (await f.request('/api/save-sync', batch(current.revision, 'return-to-founder-one', [{ type: 'select', value: 1 }]), token)).body;
    }
    const journal = structuredClone(current.state.journal);
    const invalid = await f.request('/api/save-sync', batch(current.revision, `release-mixed-invalid-${attempt}`, [{ type: 'release', value: member.id }, { type: 'hatch', value: 1 }]), token);
    assert.equal(invalid.status, 422); assert.deepEqual((await f.request('/api/save', undefined, token)).body.state, current.state);
    lastRelease = batch(current.revision, `release-confirmed-${attempt}`, [{ type: 'release', value: member.id }]);
    lastResponse = await f.request('/api/save-sync', lastRelease, token); assert.equal(lastResponse.status, 200); current = lastResponse.body;
    assert.deepEqual(current.state.journal, journal); assert.equal(current.state.collection.length, 1); assert.ok(current.state.nextMemberId > member.id);
    assert.deepEqual(await f.request('/api/save-sync', lastRelease, token), lastResponse);
  }
  assert.ok([...capturedIds].some(id => id > 8), 'real native captures allocate IDs above eight after releases');
  assert.ok(current.state.captures >= 9); assert.ok(current.state.journal.obtainedFormIds.length >= 2);
  assert.equal((await f.request('/api/save-sync', batch(current.revision, 'cannot-release-current', [{ type: 'release', value: 1 }]), token)).status, 422);
  assert.equal((await f.request('/api/save-sync', batch(current.revision, 'cannot-use-sentinel-id', [{ type: 'select', value: 0xffffffff }]), token)).status, 422);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', lastRelease, token), lastResponse);
});

test('paged roster, journal-ID lookup and detail use bounded authoritative tables with private art behind auth', async t => {
  const f = await fixture(t), identity = await f.pair();
  const first = await f.request('/api/roster?limit=16', undefined, ''); assert.equal(first.status, 200); assert.equal(first.body.total, 444); assert.equal(first.body.entries.length, 16);
  assert.ok(Buffer.byteLength(JSON.stringify(first.body)) < 16384);
  const seen = new Set<number>(); for (let offset = 0; offset < 444; offset += 16) { const page = (await f.request(`/api/roster?offset=${offset}&limit=16`, undefined, '')).body; for (const entry of page.entries) { assert.equal(seen.has(entry.formId), false); seen.add(entry.formId); } } assert.equal(seen.size, 444);
  const filtered = (await f.request('/api/roster?prefix=a&stage=Rookie&q=agu', undefined, '')).body; assert.ok(filtered.entries.length > 0); assert.ok(filtered.entries.every((e: any) => e.stage === 'Rookie' && e.sourceName.toLowerCase().startsWith('a')));
  const ids = (await f.request('/api/roster?ids=11,67,18&limit=3', undefined, '')).body; assert.deepEqual(ids.entries.map((e: any) => e.formId), [11, 67, 18]); assert.equal(ids.entries[0].entryKey, 'impmon');
  for (const path of ['/api/roster?limit=17', '/api/roster?limit=0', '/api/roster?offset=-1', '/api/roster?offset=0&offset=1', '/api/roster?prefix=AA', '/api/roster?stage=Rookie%00', '/api/roster?ids=1,1', '/api/roster?ids=1&q=Mote', `/api/roster?q=${'x'.repeat(65)}`]) assert.equal((await f.request(path, undefined, '')).status, 400, path);
  const champion = runtime.forms.find((form: any) => form.formId > 66 && form.minLevel > 1);
  assert.equal((await f.request(`/api/roster/${champion.formId}?level=1`, undefined, '')).status, 400);
  const detail = (await f.request(`/api/roster/${champion.formId}`, undefined, '')).body.form;
  assert.equal(detail.previewLevel, champion.minLevel); const expected = champion.statsByLevel.find((row: any) => row.level === champion.minLevel);
  for (const key of ['maxHp', 'attack', 'defense', 'magic', 'resistance']) assert.equal(detail.combat[key], expected[key]);
  assert.deepEqual(detail.baseStats, champion.baseStats); assert.deepEqual(detail.growth, champion.growth); assert.equal(detail.canonicalEvolutionClaim, false);
  const alias = (await f.request('/api/roster?ids=104', undefined, '')).body; assert.equal(alias.entries.length, 1);
  assert.equal((await f.request('/api/roster/art/18', undefined, '')).status, 401);
  const privateResult = await f.request('/api/roster/art/18', undefined, identity.token);
  assert.ok([200, 404].includes(privateResult.status)); if (privateResult.status === 200) { assert.equal(privateResult.body.formId, 18); assert.equal(privateResult.body.artId, 'ds-form-18'); assert.equal(hash(privateResult.body.packText), privateResult.body.sha256); }
  assert.equal((await f.request('/api/roster/art/512', undefined, identity.token)).status, 404);
});

test('test forms remain decode-only and are excluded from production roster endpoints', async t => {
  const f = await fixture(t);
  for (let id = 1; id <= 10; ++id) {
    assert.equal((await f.request(`/api/roster/${id}`, undefined, '')).status, 404);
    assert.equal((await f.request(`/api/roster?ids=${id}`, undefined, '')).status, 400);
    assert.equal(runtime.forms.find((form: any) => form.formId === id).preserved, true, 'stable historical identity remains available to old save decoding');
  }
  const ids = new Set<number>();
  for (let offset = 0; offset < 455; offset += 16) {
    const page = JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core'), ['--catalog-page', String(offset), '16'], { encoding: 'utf8' }));
    assert.equal(page.total, 455); assert.equal(page.rulesVersion, 18);
    for (const form of page.forms) { assert.ok(form.formId >= 11 && !ids.has(form.formId)); ids.add(form.formId); }
  }
  assert.equal(ids.size, 455);
});

test('old practice3 Auto receipts retain exact snapshots and trace, while current starts use practice7', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-world-ds-practice-'));
  const companion = { id: 1, species: 'impmon', name: 'Impmon', level: 1, formId: 11 };
  const body = { rulesVersion: 3, expectedRevision: 0, requestId: 'frozen-three-auto-request', mode: 'auto' as const };
  const record = { mode: 'auto', initialSnapshotBase64: frozen.practiceAuto.initialSnapshotBase64, snapshotBase64: frozen.practiceAuto.snapshotBase64, companion };
  const receipt = { ...record, requestId: body.requestId, revision: 1, bodyHash: hash(JSON.stringify({ rulesVersion: 3, operation: 'start', expectedRevision: 0, mode: 'auto' })) };
  const old = { formatVersion: 4, practiceSchemaVersion: 3, practiceRulesVersion: 3, generation: 1, devices: [{ ...record, deviceId: DEVICE, revision: 1, receipts: [receipt], legacyRequestIds: [] }] };
  for (const name of ['battle-store.json', 'battle-store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(old));
  let service = createBattleService({ rootDir, dataDir, corePath: process.env.DIGIVICE_TEST_BATTLE_PATH, seed: () => 12345 });
  t.after(async () => { service.close(); await rm(dataDir, { recursive: true, force: true }); });
  const result = service.get(DEVICE), { companion: storedCompanion, enemy, ...state } = result.battle!;
  assert.deepEqual(state, frozen.practiceAuto.state); assert.deepEqual(result.autoTrace, frozen.practiceAuto.trace); assert.deepEqual(storedCompanion, companion);
  assert.deepEqual(service.start(DEVICE, body, companion), result);
  assert.throws(() => service.start(DEVICE, { ...body, rulesVersion: 4 }, companion), (e: unknown) => e instanceof BattleError && e.code === 'request_mismatch');
  const stored = JSON.parse(await readFile(join(dataDir, 'battle-store.json'), 'utf8')); assert.equal(stored.formatVersion, 8); assert.deepEqual(stored.devices, old.devices);
  const current = service.start(DEVICE, { rulesVersion: 7, expectedRevision: 1, requestId: 'current-catalog-auto-start', mode: 'auto' }, { ...companion, id: 12 }); assert.equal(current.battle?.rulesVersion, 7); assert.equal(current.battle?.companion.id, 12);
  service.close(); service = createBattleService({ rootDir, dataDir, corePath: process.env.DIGIVICE_TEST_BATTLE_PATH, seed: () => { throw new Error('Retry cannot reroll'); } });
  assert.deepEqual(service.start(DEVICE, body, companion), result); assert.deepEqual(service.get(DEVICE), current);
});

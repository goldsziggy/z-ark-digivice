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
import { createBattleService } from '../service/battle-service.ts';
const rootDir = resolve(import.meta.dirname, '..');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/evolution-graph-frozen-rules5.json'), 'utf8'));
const runtime = JSON.parse(readFileSync(join(rootDir, 'data/world-ds-runtime.json'), 'utf8'));
const hash = (value: string) => createHash('sha256').update(value).digest('hex');
const DEVICE = `dv_${'9'.repeat(24)}`, TOKEN = Buffer.alloc(32, 9).toString('base64url'); // Public test identity, not credentials.
type Event = { type: string; value: number };
function receipt(rulesVersion: number, baseRevision: number, events: Event[], batchId: string) { return { batchId, revision: baseRevision + 1, eventEnd: events.length, bodyHash: hash(JSON.stringify({ rulesVersion, baseRevision, events })) }; }
function oldStore(events: Event[], legacy: unknown = null, revision = 1) { return { formatVersion: 7, gameSchemaVersion: 8, rulesVersion: 5, devices: [{ deviceId: DEVICE, tokenHash: hash(TOKEN), seed: 12345, initialMode: 'onboarding', revision, legacy, events, receipts: [receipt(5, revision - 1, events, 'frozen-five-committed-batch')] }] }; }
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 18, baseRevision, batchId, events });
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, saved: unknown, prepare?: (dataDir: string) => void) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-evolution-graph-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(saved));
  prepare?.(dataDir);
  let app = await startServer({ seedSource: () => 12345, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, rootDir, dataDir, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  async function request(path: string, body?: unknown, token = TOKEN) { const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST', headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) }); return { status: response.status, body: await response.json() as any }; }
  return { dataDir, request, restart: async () => { await close(); app.close(); app = await startServer({ seedSource: () => 12345, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, rootDir, dataDir, port: 0 }); } };
}

test('rules5 history and released-form journal migrate once without changing XP, identity or receipt hashes', async t => {
  const oldHistory = { rulesVersion: 4, events: frozen.oldRule4Events, receipts: [receipt(4, 0, frozen.oldRule4Events, 'frozen-four-baseline-batch')] };
  const old = oldStore(frozen.rules5SuffixEvents, { histories: [oldHistory], snapshotBase64: frozen.rules5Baseline.snapshotBase64 }, 2);
  const f = await fixture(t, old), before = (await f.request('/api/save')).body;
  assert.equal(before.state.schemaVersion, 25); assert.equal(before.state.rulesVersion, 18); assert.equal(before.revision, 2); assert.equal(before.baseSequence, 6); assert.deepEqual(before.events, []);
  for (const key of ['collection', 'activeCreatureId', 'nextMemberId', 'journal', 'rngState', 'xp', 'formId', 'hp', 'bond', 'captures', 'onboarding', 'battleMode', 'lastAutoBattle']) assert.deepEqual(legacyFields(before.state[key]), frozen.rules5SuffixResult.state[key], key);
  assert.deepEqual(before.state.journal.obtainedFormIds, [4, 11]); assert.equal(before.state.collection.length, 1); assert.equal(before.autoTrace, null);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.equal(stored.formatVersion, 20); assert.equal(stored.gameSchemaVersion, 25); assert.equal(stored.rulesVersion, 18);
  const snapshot = Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64'); assert.equal(snapshot.length, 3216); assert.equal(snapshot.readUInt16LE(4), 25); assert.equal(snapshot.readUInt32LE(8), 18);
  assert.deepEqual(stored.devices[0].legacy.histories, [oldHistory, { rulesVersion: 5, events: old.devices[0].events, receipts: old.devices[0].receipts }]);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v5.json'), 'utf8')), old, 'archival baseline bytes, initial mode and body hashes remain exact');
  const pending = { rulesVersion: 5, baseRevision: 1, batchId: old.devices[0].receipts[0].batchId, events: old.devices[0].events };
  assert.equal((await f.request('/api/save-sync', pending)).body.error, 'migration_required');
  assert.equal((await f.request('/api/save-sync', { ...pending, rulesVersion: 18 })).body.error, 'legacy_batch_requires_reconciliation');
  assert.equal((await f.request('/api/save-sync', { ...pending, rulesVersion: 999 })).body.error, 'migration_required');
  await f.restart(); assert.deepEqual((await f.request('/api/save')).body, before);
  const feed = batch(2, 'current-six-care-receipt', [{ type: 'feed', value: 0 }]), accepted = await f.request('/api/save-sync', feed);
  assert.equal(accepted.status, 200); assert.equal(accepted.body.revision, 3); assert.equal(accepted.body.state.xp, before.state.xp + (before.state.fullness < 100 ? 2 : 0));
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', feed), accepted);
});

test('rules5 frozen outcomes remain replayable while a saved test encounter clears without rewards', async t => {
  const f = await fixture(t, oldStore(frozen.rules5EncounterEvents));
  const before = (await f.request('/api/save')).body; assert.equal(before.state.wildRules, 5);
  for (const key of ['wildSpecies', 'wildFormId', 'wildLevel', 'wildHp', 'wildGuard', 'wildTurn', 'rngState']) assert.deepEqual(legacyFields(before.state[key]), frozen.rules5EncounterResult.state[key]);
  const historical = JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core'), ['--migrate-v5-onboarding', '12345'], { input: [...frozen.rules5EncounterEvents, { type: 'auto', value: 0 }].map((e: Event) => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8' })).state;
  for (const key of ['hp', 'energy', 'xp', 'collection', 'journal', 'nextMemberId', 'captures', 'rngState']) assert.deepEqual(legacyFields(historical[key]), frozen.rules5AutoResult.state[key], `frozen outcome ${key}`);
  assert.ok(before.state.wildFormId < 11);
  assert.equal((await f.request('/api/save-sync', batch(1, 'reject-old-5-test-auto', [{ type: 'auto', value: 0 }]))).status, 422);
  assert.deepEqual((await f.request('/api/save')).body, before);
  const input = batch(1, 'finish-frozen-five-encounter', [{ type: 'resolve-test-encounter', value: 0 }]);
  const result = await f.request('/api/save-sync', input); assert.equal(result.status, 200);
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
  const subsequent = await f.request('/api/save-sync', batch(2, 'after-five-auto-care', [{ type: 'rest', value: 0 }])); assert.equal(subsequent.status, 200);
  await f.restart(); assert.deepEqual(await f.request('/api/save-sync', input), result); assert.equal((await f.request('/api/save')).body.revision, 3);
});

test('graph pages and per-form metadata expose authoritative routes without mutating a companion', async t => {
  const f = await fixture(t, oldStore([{ type: 'hatch', value: 1 }]));
  const before = (await f.request('/api/save')).body;
  const graph = (await f.request('/api/evolution-graph?formId=11&limit=8', undefined, '')).body;
  assert.equal(graph.formatVersion, 2); assert.equal(graph.catalogVersion, 6); assert.equal(graph.rulesVersion, 18); assert.equal(graph.focusFormId, 11);
  const forms = new Map<number, any>(runtime.forms.map((form: any) => [form.formId, form]));
  const expected = new Set<number>([11]);
  for (let changed = true; changed;) { changed = false; for (const id of [...expected]) for (const next of [...forms.get(id).evolution.parents, ...forms.get(id).evolution.children]) if (!expected.has(next)) { expected.add(next); changed = true; } }
  assert.ok(expected.size > 16, 'a connected cross-lineage graph spans at least three eight-row pages');
  for (const id of [11, 32, 84]) assert.ok(expected.has(id), 'Impmon, Patamon and DemiDevimon share the browsable graph');
  const seen = new Set<number>(); let offset = 0;
  for (;;) {
    const response = await f.request(`/api/evolution-graph?formId=11&offset=${offset}&limit=8`, undefined, ''); assert.equal(response.status, 200);
    const page = response.body; assert.equal(page.offset, offset); assert.equal(page.limit, 8); assert.equal(page.total, expected.size); assert.ok(page.forms.length <= 8); assert.ok(Buffer.byteLength(JSON.stringify(page)) <= 16384);
    for (const form of page.forms) {
      assert.equal(seen.has(form.formId), false); seen.add(form.formId);
      const native = forms.get(form.formId); assert.equal(form.name, native.name); assert.equal(form.previewLevel, native.minLevel);
      assert.deepEqual(form.parents, native.evolution.parents); assert.deepEqual(form.children, native.evolution.children);
      assert.deepEqual(form.edges.map((edge: any) => edge.toFormId), native.evolution.children);
      for (const edge of form.edges) {
        assert.ok(edge.requiredLevel >= 1 && edge.requiredLevel <= 50); assert.ok(edge.requiredBond >= 0 && edge.requiredBond <= 200);
        assert.ok(edge.requiredCare >= 12 && edge.requiredCare <= 100);
      }
      const row = native.statsByLevel.find((row: any) => row.level === form.previewLevel); for (const stat of ['maxHp', 'attack', 'defense', 'magic', 'resistance']) assert.equal(form.combat[stat], row[stat]);
    }
    if (page.nextOffset === null) break;
    assert.equal(page.nextOffset, offset + page.forms.length); assert.ok(page.nextOffset > offset); offset = page.nextOffset;
  }
  assert.deepEqual([...seen].sort((a, b) => a - b), [...expected].sort((a, b) => a - b));
  const detail = await f.request('/api/roster/11', undefined, ''); assert.equal(detail.status, 200); assert.equal(detail.body.catalogVersion, 6); assert.equal(detail.body.rulesVersion, 15); assert.deepEqual(detail.body.form.evolution, forms.get(11).evolution);
  assert.equal(detail.body.form.parentId, forms.get(11).parent); assert.deepEqual(detail.body.form.children, forms.get(11).children, 'historical family fields are kept separate from current graph');
  const terminal = await f.request(`/api/evolution-graph?formId=11&offset=${expected.size}&limit=16`, undefined, ''); assert.equal(terminal.status, 200); assert.deepEqual(terminal.body.forms, []); assert.equal(terminal.body.nextOffset, null);
  for (const path of ['/api/evolution-graph', '/api/evolution-graph?formId=0', '/api/evolution-graph?formId=513', '/api/evolution-graph?formId=011', '/api/evolution-graph?formId=11&formId=68', '/api/evolution-graph?formId=11&limit=17', '/api/evolution-graph?formId=11&limit=0', '/api/evolution-graph?formId=11&offset=-1', '/api/evolution-graph?formId=11&offset=513', '/api/evolution-graph?formId=11&source=/tmp/anything']) assert.equal((await f.request(path, undefined, '')).status, 400, path);
  assert.equal((await f.request('/api/evolution-graph?formId=512', undefined, '')).status, 404);
  assert.equal((await f.request(`/api/evolution-graph?formId=11&offset=${expected.size + 1}`, undefined, '')).status, 404);
  const junction = await f.request('/api/roster/15', undefined, ''); assert.equal(junction.status, 200);
  assert.deepEqual(junction.body.form.evolution.parents, [11, 84, 116], 'Devimon exposes all three authored incoming paths');
  assert.equal(junction.body.form.parentId, 11, 'the historic Impmon family anchor remains unchanged');
  assert.deepEqual((await f.request('/api/save')).body, before);
});

test('care graph migration leaves persisted current practice Auto and its exact receipt unchanged', async t => {
  let practice: any, disk: string;
  const request = { rulesVersion: 7, expectedRevision: 0, requestId: 'practice-four-survives-graph', mode: 'auto' as const };
  const f = await fixture(t, oldStore([{ type: 'hatch', value: 1 }]), dataDir => {
    const battle = createBattleService({ rootDir, dataDir, corePath: process.env.DIGIVICE_TEST_BATTLE_PATH, seed: () => 12345 });
    practice = battle.start(DEVICE, request, { id: 1, species: 'impmon', name: 'Impmon', level: 1, formId: 11 }); battle.close();
    disk = readFileSync(join(dataDir, 'battle-store.json'), 'utf8');
  });
  const current = await f.request('/api/battle'); assert.equal(current.status, 200); assert.deepEqual(current.body, practice); assert.equal(current.body.battle.rulesVersion, 7);
  assert.deepEqual((await f.request('/api/battle/start', request)).body, practice);
  assert.equal(await readFile(join(f.dataDir, 'battle-store.json'), 'utf8'), disk!);
  await f.restart(); assert.deepEqual((await f.request('/api/battle/start', request)).body, practice);
});

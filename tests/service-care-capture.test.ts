import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { validCare, validLastCapture, captureReport } from '../web/care-capture-state.js';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
const frozen = JSON.parse(readFileSync(join(rootDir, 'tests/fixtures/care-capture-rules11-baseline.json'), 'utf8'));
type Event = { type: string; value: number };
const token = Buffer.alloc(32, 62).toString('base64url'); // Public fixture identity.
const id = `dv_${'d'.repeat(24)}`;
const hash = (value: string) => createHash('sha256').update(value).digest('hex');
const command = (revision: number, batchId: string, events: Event[], rulesVersion = 13) => ({ rulesVersion, baseRevision: revision, batchId, events });
const core = (args: string[], events: Event[] = []) => JSON.parse(execFileSync(corePath, args, { input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', maxBuffer: 64 * 1024 }));
function originalStore(events: Event[]) {
  return { formatVersion: 13, gameSchemaVersion: 14, rulesVersion: 11, devices: [{ deviceId: id, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding', revision: events.length ? 1 : 0,
    legacy: null, events, receipts: events.length ? [{ batchId: 'frozen-eleven-original-batch', bodyHash: hash(JSON.stringify({ rulesVersion: 11, baseRevision: 0, events })), revision: 1, eventEnd: events.length }] : [] }] };
}
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }, original: unknown = originalStore([])) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-care-capture-service-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(original));
  const options = { rootDir, dataDir, corePath, battleCorePath, port: 0 }; let app = await startServer(options);
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown, path = body === undefined ? '/api/save' : '/api/save-sync', credential: string | null = token) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, { method: body === undefined ? 'GET' : 'POST',
      headers: { ...(credential ? { authorization: `Bearer ${credential}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  return { dataDir, request, restart: async () => { await close(); app.close(); app = await startServer(options); } };
}
function beforeExtension(state: any) {
  const { care, lastCapture, foregroundSequence, ...previous } = structuredClone(state);
  assert.equal(foregroundSequence, previous.sequence);
  assert.equal(previous.walking.pendingEncounter, null); delete previous.walking.pendingEncounter;
  if (previous.phase !== 'home') previous.walking.remainingSteps = 0; // Before schema16 the UI did not expose active-fight pacing.
  previous.schemaVersion = 14; previous.rulesVersion = 11;
  delete previous.onboarding.offerSeed; delete previous.onboarding.offers;
  previous.collection.forEach((member: any) => { delete member.care; });
  return previous;
}
for (const [name, value] of Object.entries(frozen.cases) as Array<[string, any]>) test(`rules11 ${name} migration preserves exact frozen fields, trace and receipts`, async t => {
  assert.deepEqual(core(['--replay-v11-onboarding-trace', '12345'], value.events), value.result, 'independent pre-change golden output');
  const original = originalStore(value.events), f = await fixture(t, original), saved = await f.request();
  assert.equal(saved.status, 200); assert.deepEqual(beforeExtension(saved.body.state), value.result.state); assert.deepEqual(saved.body.autoTrace, value.result.trace);
  assert.ok(validLastCapture(saved.body.state.lastCapture, saved.body.state.sequence));
  assert.ok(saved.body.state.collection.every(validCare));
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.gameSchemaVersion, stored.rulesVersion], [15, 17, 13]);
  assert.deepEqual(stored.devices[0].legacy.histories, [{ rulesVersion: 11, events: value.events, receipts: original.devices[0].receipts }]);
  assert.equal(Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64').length, 652);
  assert.deepEqual(JSON.parse(await readFile(join(f.dataDir, 'store.rules-v11.json'), 'utf8')), original);
  if (value.events.length) {
    const pending = command(0, 'frozen-eleven-original-batch', value.events, 11);
    assert.equal((await f.request(pending)).body.error, 'migration_required');
    assert.equal((await f.request({ ...pending, rulesVersion: 13 })).body.error, 'legacy_batch_requires_reconciliation');
  }
  await f.restart(); assert.deepEqual(await f.request(), saved);
  if (name === 'encounter') {
    assert.ok(saved.body.state.wildFormId < 11);
    assert.equal((await f.request(command(1, 'reject-old-eleven-test-auto', [{ type: 'auto', value: 0 }]))).status, 422);
    assert.deepEqual(await f.request(), saved);
    const resolution = command(1, 'clear-frozen-eleven-test-encounter', [{ type: 'resolve-test-encounter', value: 0 }]);
    const cleared = await f.request(resolution); assert.equal(cleared.status, 200);
    for (const key of ['hp', 'energy', 'xp', 'journal', 'captures', 'rngState', 'activeCreatureId', 'nextMemberId']) assert.deepEqual(cleared.body.state[key], saved.body.state[key], key);
    // Stored care values are unchanged; leaving a pre12 fight re-enables
    // the existing Home display bonus for this mood80 Impmon.
    const expectedMembers = structuredClone(saved.body.state.collection);
    const active = expectedMembers.find((member: any) => member.id === saved.body.state.activeCreatureId);
    assert.equal(active.mood, 80); assert.equal(active.care.offenseBonus, 0);
    active.care.offenseBonus = 1; active.care.effective.attack = active.combat.attack + 1; active.care.effective.magic = active.combat.magic + 1;
    assert.deepEqual(cleared.body.state.collection, expectedMembers);
    assert.equal(cleared.body.state.phase, 'home'); assert.equal(cleared.body.state.sequence, saved.body.state.sequence + 1);
    assert.deepEqual(cleared.body.autoTrace, saved.body.autoTrace);
    const persisted = await f.request(); assert.deepEqual(persisted.body.state, cleared.body.state); assert.equal(persisted.body.revision, cleared.body.revision);
    await f.restart(); assert.deepEqual(await f.request(resolution), cleared); assert.deepEqual(await f.request(), persisted);
  }
});

test('starter offers commit once across concurrent requests, reload and independent identities', async t => {
  const f = await fixture(t);
  assert.equal((await f.request({}, '/api/starter-offers', null)).status, 401);
  assert.equal((await f.request({ seed: 5 }, '/api/starter-offers')).status, 400);
  const results = await Promise.all(Array.from({ length: 4 }, () => f.request({}, '/api/starter-offers')));
  for (const result of results) { assert.equal(result.status, 200); assert.deepEqual(result, results[0]); }
  const first = results[0].body;
  assert.equal(first.revision, 1); assert.equal(first.state.sequence, 1); assert.ok(first.state.onboarding.offerSeed > 0);
  assert.equal(new Set(first.state.onboarding.offers).size, 3);
  for (const formId of first.state.onboarding.offers) {
    const form = (await f.request(undefined, `/api/roster/${formId}?level=1`)).body.form;
    assert.equal(form.stage, 'Rookie'); assert.equal(form.previewLevel, 1); assert.ok(form.evolution.children.length);
  }
  await f.restart(); assert.deepEqual(await f.request({}, '/api/starter-offers'), results[0]);
  const pairing = await f.request({}, '/api/pairing/start', null), other = await f.request({ code: pairing.body.code }, '/api/pairing/claim', null);
  const second = await f.request({}, '/api/starter-offers', other.body.token);
  assert.equal(second.status, 200); assert.notEqual(second.body.deviceId, first.deviceId);
  // Do not assert random seeds differ: collisions are improbable but legal.
  const store = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  for (const device of store.devices) assert.equal(device.events.filter((e: Event) => e.type === 'starter-offer-seed').length, 1);
  const hatched = await f.request(command(1, 'offered-hatch-slot-nine', [{ type: 'hatch', value: 9 }]));
  assert.equal(hatched.status, 200); assert.equal(hatched.body.state.formId, first.state.onboarding.offers[0]);
  assert.equal((await f.request({}, '/api/starter-offers')).body.error, 'onboarding_complete');
  await f.restart(); assert.deepEqual((await f.request()).body.state, hatched.body.state);
});

test('three committed misses show no odds, preserve HP, close capture and cannot reroll on retry', async t => {
  const f = await fixture(t); let saved = await f.request(); let revision = saved.body.revision;
  const send = async (name: string, events: Event[]) => { const result = await f.request(command(revision, name, events)); assert.equal(result.status, 200, JSON.stringify(result.body)); revision = result.body.revision; return result; };
  await send('capture-current-prepare', [{ type: 'hatch', value: 1 }, { type: 'explore', value: 1000 }]);
  for (let attack = 0; attack < 8; attack++) {
    saved = await f.request(); if (saved.body.state.wildCaptureChance) break;
    await send(`capture-weaken-${attack}`, [{ type: attack % 2 ? 'attack' : 'magic', value: 0 }]);
  }
  const before = (await f.request()).body.state; assert.ok(before.wildCaptureChance > 0); let first: any, firstRequest: any;
  for (let attempt = 1; attempt <= 3; attempt++) {
    const request = command(revision, `capture-miss-${attempt}`, [{ type: 'flick', value: 0 }]);
    const accepted = await f.request(request); assert.equal(accepted.status, 200); revision = accepted.body.revision;
    assert.equal(accepted.body.state.hp, before.hp); assert.equal(accepted.body.state.rngState, before.rngState);
    const report = captureReport(accepted.body.state.lastCapture); assert.ok(report); assert.equal(report.odds, 'Miss · no catch'); assert.equal(report.remaining, 3 - attempt);
    assert.equal(accepted.body.state.lastCapture.attempt, attempt); assert.equal(accepted.body.state.lastCapture.result, 'miss');
    assert.equal(accepted.body.state.phase, attempt === 3 ? 'home' : 'encounter');
    assert.deepEqual(await f.request(request), accepted);
    if (attempt === 1) { first = accepted; firstRequest = request; }
  }
  const exhausted = await f.request(); await f.restart(); assert.deepEqual(await f.request(), exhausted);
  assert.equal((await f.request(command(revision, 'capture-fourth-forbidden', [{ type: 'flick', value: 41140 }]))).status, 422);
  assert.deepEqual(await f.request(firstRequest), first); assert.deepEqual(await f.request(), exhausted);
});

test('care modifiers are bounded and repeated full-mood Play cannot spend energy or farm stats', async t => {
  const f = await fixture(t); const events = [{ type: 'hatch', value: 1 }, ...Array.from({ length: 20 }, () => ({ type: 'play', value: 0 })), ...Array.from({ length: 20 }, () => ({ type: 'feed', value: 0 }))];
  const saved = await f.request(command(0, 'care-bounded-inputs', events)); assert.equal(saved.status, 200);
  const member = saved.body.state.collection[0]; assert.ok(validCare(member)); assert.equal(member.mood, 100);
  const repeated = command(1, 'care-repeated-full-actions', Array.from({ length: 100 }, () => ({ type: 'play', value: 0 })));
  const result = await f.request(repeated); assert.equal(result.status, 200);
  assert.deepEqual(result.body.state.collection, saved.body.state.collection); assert.equal(result.body.state.xp, saved.body.state.xp);
  assert.ok(Buffer.byteLength(JSON.stringify(result.body.state)) < 12288);
  await f.restart(); assert.deepEqual(await f.request(repeated), result);
});

test('a current-rules hit records its actual chance and owns exactly one captured instance through retry and later care', async t => {
  const f = await fixture(t);
  const prepared = await f.request(command(0, 'current-catch-prepare', [{ type: 'hatch', value: 1 }, { type: 'explore', value: 1000 }, { type: 'magic', value: 0 }, { type: 'attack', value: 0 }, { type: 'magic', value: 0 }]));
  assert.equal(prepared.status, 200); assert.equal(prepared.body.state.wildRules, 13);
  const request = command(1, 'current-catch-exactly-once', [{ type: 'flick', value: 41140 }]);
  const saved = await f.request(request); assert.equal(saved.status, 200);
  assert.equal(saved.body.state.lastCapture.chance, prepared.body.state.wildCaptureChance);
  assert.equal(saved.body.state.lastCapture.result, 'captured');
  assert.equal(saved.body.state.collection.length, prepared.body.state.collection.length + 1);
  const obtained = saved.body.state.collection.at(-1); assert.equal(obtained.formId, saved.body.state.lastCapture.targetFormId);
  assert.equal(new Set(saved.body.state.collection.map((member: any) => member.id)).size, saved.body.state.collection.length);
  assert.deepEqual(await f.request(request), saved);
  const selected = await f.request(command(2, 'current-catch-select-owned', [{ type: 'select', value: obtained.id }, { type: 'feed', value: 0 }]));
  assert.equal(selected.status, 200); assert.equal(selected.body.state.activeCreatureId, obtained.id);
  assert.deepEqual(selected.body.state.collection[0], saved.body.state.collection[0], 'care for captured individual leaves earlier partner unchanged');
  await f.restart(); assert.deepEqual(await f.request(request), saved); assert.deepEqual((await f.request()).body.state, selected.body.state);
});

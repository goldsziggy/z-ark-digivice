import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { careQualitySummary } from '../web/care-capture-state.js';
import { evolutionRequirements } from '../web/progression.js';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
type Event = { type: string; value: number };
const token = Buffer.alloc(32, 71).toString('base64url'); // Public fixture identity.
const id = `dv_${'e'.repeat(24)}`;
const sha = (value: string) => createHash('sha256').update(value).digest('hex');
const command = (revision: number, batchId: string, events: Event[], rulesVersion = 17) => ({ rulesVersion, baseRevision: revision, batchId, events });

// A rules-16 (format 18) store with one hatched device and an acknowledged care batch.
function rules16Store() {
  const events: Event[] = [{ type: 'hatch', value: 1 }, { type: 'care-minute', value: 1 }, { type: 'feed', value: 0 }];
  const bodyHash = sha(JSON.stringify({ rulesVersion: 16, baseRevision: 0, events }));
  return { formatVersion: 18, gameSchemaVersion: 23, rulesVersion: 16, devices: [{ deviceId: id, tokenHash: sha(token), seed: 12345, revision: 1, events,
    receipts: [{ batchId: 'rules-sixteen-original', bodyHash, revision: 1, eventEnd: events.length }], legacy: null, initialMode: 'onboarding' }] };
}
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-care-injury-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(rules16Store()));
  const app = await startServer({ seedSource: () => 12345, rootDir, dataDir, corePath, battleCorePath, port: 0 });
  t.after(async () => { await new Promise<void>(done => app.server.close(() => done())); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${body === undefined ? '/api/save' : '/api/save-sync'}`, {
      method: body === undefined ? 'GET' : 'POST', headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  return { dataDir, request };
}
const partner = (state: any) => state.collection.find((member: any) => member.id === state.activeCreatureId);

test('rules16 store migrates once to rules17 with clean care-quality fields and an archived history', async t => {
  const f = await fixture(t), saved = await f.request();
  assert.equal(saved.status, 200);
  assert.equal(saved.body.state.schemaVersion, 24); assert.equal(saved.body.state.rulesVersion, 17);
  assert.ok(saved.body.state.collection.every((member: any) => member.careMistakes === 0 && member.injury === 0));
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.gameSchemaVersion, stored.rulesVersion], [19, 24, 17]);
  assert.deepEqual(stored.devices[0].legacy.histories.map((history: any) => history.rulesVersion), [16]);
  const snapshot = Buffer.from(stored.devices[0].legacy.snapshotBase64, 'base64');
  assert.deepEqual([snapshot.length, snapshot.readUInt16LE(4), snapshot.readUInt32LE(8)], [3216, 24, 17]);
  // Treat is a rules-17 event: an old-rules batch cannot carry it.
  assert.equal((await f.request(command(saved.body.revision, 'old-rules-treat', [{ type: 'treat', value: 0 }], 16))).status, 409);
  // A healthy partner has nothing to treat; the batch changes nothing.
  assert.equal((await f.request(command(saved.body.revision, 'healthy-treat', [{ type: 'treat', value: 0 }]))).status, 422);
  assert.deepEqual(await f.request(), saved);
});

test('a real knockout injures once, caps Rest at half, and Treat restores full recovery exactly once', async t => {
  const f = await fixture(t);
  let current = (await f.request()).body, serial = 0;
  const send = async (events: Event[]) => {
    const result = await f.request(command(current.revision, `injury-flow-${++serial}`, events));
    assert.equal(result.status, 200, JSON.stringify(result.body?.error));
    current = result.body; return result;
  };
  await send([{ type: 'mode', value: 1 }]);
  // Fight without resting until the partner is knocked out by a real native foe.
  for (let guard = 0; guard < 200 && !partner(current.state).injury; ++guard) await send([{ type: 'walk', value: 100 }, { type: 'auto', value: 0 }]);
  const hurt = partner(current.state);
  assert.equal(hurt.injury, 1); assert.ok(hurt.careMistakes >= 1);
  assert.match(current.state.message, /hurt/i);
  assert.match(careQualitySummary(hurt), /Treat/);
  const half = Math.ceil(current.state.combat.maxHp / 2);
  await send(Array.from({ length: 12 }, () => ({ type: 'rest', value: 0 })));
  assert.equal(current.state.hp, half); assert.equal(current.state.recoveryRestCount, 0);
  const treat = command(current.revision, 'treat-once', [{ type: 'treat', value: 0 }]);
  const treated = await f.request(treat); assert.equal(treated.status, 200); current = treated.body;
  assert.equal(partner(current.state).injury, 0); assert.match(current.state.message, /treated/i);
  // A retried acknowledgement returns the same receipt and never applies twice.
  assert.deepEqual(await f.request(treat), treated);
  assert.ok(current.state.recoveryRestCount > 0);
  await send(Array.from({ length: current.state.recoveryRestCount }, () => ({ type: 'rest', value: 0 })));
  assert.equal(current.state.hp, current.state.combat.maxHp);
});

test('browser presentation names care mistakes, locked routes and injuries', () => {
  const member = { level: 20, bond: 120, carePoints: 50, careMistakes: 4, injury: 0 };
  const facts = evolutionRequirements(member, { requiredLevel: 18, requiredBond: 64, requiredCare: 40, maxCareMistakes: 3, careRouteOpen: false });
  assert.deepEqual(facts.at(-1), ['Care mistakes', '4 / 3 max · route locked this stage']);
  assert.equal(evolutionRequirements(member, { requiredLevel: 18, requiredBond: 64, requiredCare: 40, maxCareMistakes: null, careRouteOpen: true }).length, 3);
  assert.deepEqual(evolutionRequirements({ ...member, injury: 2 }, { requiredLevel: 18, requiredBond: 64, requiredCare: 40, maxCareMistakes: null, careRouteOpen: true }).at(-1), ['Injury', 'Treat before Digivolving']);
  assert.equal(careQualitySummary({ careMistakes: 0, injury: 0 }), '');
  assert.equal(careQualitySummary({ careMistakes: 1, injury: 3 }), '1 care mistake this stage · Hurt and neglected · Treat now');
  assert.equal(careQualitySummary({ carePoints: 4 }), '');
});

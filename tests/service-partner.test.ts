import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { startServer } from '../service/server.ts';
import { namedFixture } from './legacy-fixture.ts';

async function fixture(t: { after: (fn: () => Promise<void>) => unknown }) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-partner-http-'));
  const historical = namedFixture(2);
  await writeFile(join(dataDir, 'store.json'), JSON.stringify(historical.store));
  let app = await startServer({ seedSource: () => 12345, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 });
  const close = () => new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (path: string, token?: string, body?: unknown) => {
    const url = `http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`;
    const response = await fetch(url, { method: body === undefined ? 'GET' : 'POST',
      headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  let nextIdentity = 0;
  const pair = async () => {
    const identity = historical.identities[nextIdentity++];
    const saved = await request('/api/save', identity.token);
    assert.equal(saved.status, 200);
    return { ...saved.body, ...identity };
  };
  return { request, pair, dataDir, restart: async () => { await close(); app = await startServer({ seedSource: () => 12345, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH, dataDir, port: 0 }); } };
}

test('SetPartner locks new selections during practice while preserving receipts, per-member care and other devices', async t => {
  const f = await fixture(t), owner = await f.pair(), other = await f.pair();
  let revision = 1, serial = 0;
  const batch = (events: Array<{ type: string; value: number }>) => ({ rulesVersion: 15, baseRevision: revision, batchId: `partner-http-${++serial}`, events });
  const submit = async (body: ReturnType<typeof batch>) => {
    const result = await f.request('/api/save-sync', owner.token, body);
    if (result.status === 200) revision = result.body.revision;
    return result;
  };
  const captured = await submit(batch([{ type: 'feed', value: 0 }, { type: 'play', value: 0 }, { type: 'mode', value: 1 }, { type: 'walk', value: 100 }, { type: 'auto', value: 0 }, { type: 'mode', value: 0 }]));
  assert.equal(captured.status, 200); assert.equal(captured.body.state.collection.length, 2);
  const founder = structuredClone(captured.body.state.collection[0]);
  const selectedSecond = await submit(batch([{ type: 'select', value: 2 }]));
  assert.equal(selectedSecond.status, 200); assert.equal(selectedSecond.body.state.activeCreatureId, 2);
  const caredForSecond = await submit(batch([{ type: 'feed', value: 0 }]));
  assert.deepEqual(caredForSecond.body.state.collection[0], founder, 'feeding one member must not change another');
  const restoreFounderBody = batch([{ type: 'select', value: 1 }]);
  const restoredFounder = await submit(restoreFounderBody);
  assert.equal(restoredFounder.body.state.hp, founder.hp);
  assert.deepEqual(restoredFounder.body.state.collection[1], caredForSecond.body.state.collection[1]);

  const start = await f.request('/api/battle/start', owner.token, { rulesVersion: 7, expectedRevision: 0, requestId: 'partner-practice-start' });
  assert.equal(start.status, 200); assert.equal(start.body.battle.companion.id, 1);
  const before = (await f.request('/api/save', owner.token)).body;
  for (const events of [[{ type: 'select', value: 2 }], [{ type: 'select', value: 1 }], [{ type: 'feed', value: 0 }, { type: 'select', value: 2 }]]) {
    const rejected = await submit(batch(events));
    assert.equal(rejected.status, 409); assert.equal(rejected.body.error, 'partner_locked');
    assert.deepEqual((await f.request('/api/save', owner.token)).body, before, 'the complete rejected batch must be unchanged');
  }
  assert.deepEqual(await f.request('/api/save-sync', owner.token, restoreFounderBody), restoredFounder, 'historical selection retry must bypass the new-batch guard');
  assert.deepEqual((await f.request('/api/battle', owner.token)).body, start.body);

  const otherSelection = await f.request('/api/save-sync', other.token, { rulesVersion: 15, baseRevision: 1, batchId: 'other-device-selection', events: [{ type: 'select', value: 1 }] });
  assert.equal(otherSelection.status, 200); assert.equal(otherSelection.body.revision, 2);
  assert.equal(otherSelection.body.state.sequence, 2, 'selecting the already-active member retains existing event semantics');
  assert.equal((await submit(batch([{ type: 'feed', value: 0 }]))).status, 200, 'unrelated care remains allowed');

  const ended = await f.request('/api/battle/act', owner.token, { rulesVersion: 7, expectedRevision: 1, requestId: 'partner-practice-retreat', action: { type: 'retreat', value: 0 } });
  assert.equal(ended.status, 200); assert.equal(ended.body.battle.status, 'retreated');
  const after = await submit(batch([{ type: 'select', value: 2 }]));
  assert.equal(after.status, 200); assert.equal(after.body.state.activeCreatureId, 2);
  assert.deepEqual(after.body.state.collection[1], caredForSecond.body.state.collection[1]);
  assert.equal((await submit(batch([{ type: 'walk', value: 100 }]))).status, 200);
  const wildBefore = (await f.request('/api/save', owner.token)).body;
  assert.equal(wildBefore.state.phase, 'encounter');
  assert.equal((await submit(batch([{ type: 'select', value: 1 }]))).status, 422, 'native wild-battle selection restriction remains unchanged');
  assert.deepEqual((await f.request('/api/save', owner.token)).body, wildBefore);
});

test('unavailable practice state fails new partner selection closed without blocking care or historical receipts', async t => {
  const f = await fixture(t), owner = await f.pair();
  const selected = { rulesVersion: 15, baseRevision: 1, batchId: 'selection-before-corruption', events: [{ type: 'select', value: 1 }] };
  const accepted = await f.request('/api/save-sync', owner.token, selected);
  assert.equal(accepted.status, 200);
  for (const name of ['battle-store.json', 'battle-store.backup.json']) await writeFile(join(f.dataDir, name), '{corrupt-practice');
  await f.restart();
  assert.deepEqual(await f.request('/api/save-sync', owner.token, selected), accepted);
  const fresh = await f.request('/api/save-sync', owner.token, { ...selected, baseRevision: 2, batchId: 'selection-after-corruption' });
  assert.equal(fresh.status, 503); assert.equal(fresh.body.error, 'battle_store_invalid');
  const care = await f.request('/api/save-sync', owner.token, { rulesVersion: 15, baseRevision: 2, batchId: 'care-after-corruption', events: [{ type: 'feed', value: 0 }] });
  assert.equal(care.status, 200); assert.equal(care.body.revision, 3);
});

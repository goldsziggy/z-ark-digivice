import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { awaitingAutoFocus, pausedAutoTraceMatchesState } from '../web/auto-battle.js';

const rootDir = resolve(import.meta.dirname, '..');
const corePath = process.env.DIGIVICE_TEST_CORE_PATH ?? join(rootDir, 'build/digivice-core');
const battleCorePath = process.env.DIGIVICE_TEST_BATTLE_PATH ?? join(rootDir, 'build/digivice-battle');
type Event = { type: string; value: number };
const token = Buffer.alloc(32, 83).toString('base64url'); // Public fixture identity.
const id = `dv_${'f'.repeat(24)}`;
const sha = (value: string) => createHash('sha256').update(value).digest('hex');
const command = (revision: number, batchId: string, events: Event[], rulesVersion = 19) => ({ rulesVersion, baseRevision: revision, batchId, events });

// A rules-17 (format 19) store: hatched, Auto mode, nothing else.
function rules17Store() {
  const events: Event[] = [{ type: 'hatch', value: 2 }, { type: 'mode', value: 1 }];
  const bodyHash = sha(JSON.stringify({ rulesVersion: 17, baseRevision: 0, events }));
  return { formatVersion: 19, gameSchemaVersion: 24, rulesVersion: 17, devices: [{ deviceId: id, tokenHash: sha(token), seed: 4242, revision: 1, events,
    receipts: [{ batchId: 'rules-seventeen-original', bodyHash, revision: 1, eventEnd: events.length }], legacy: null, initialMode: 'onboarding' }] };
}
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-auto-focus-'));
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(rules17Store()));
  const app = await startServer({ seedSource: () => 4242, rootDir, dataDir, corePath, battleCorePath, port: 0 });
  t.after(async () => { await new Promise<void>(done => app.server.close(() => done())); app.close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown, path = body === undefined ? '/api/save' : '/api/save-sync') => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${path}`, {
      method: body === undefined ? 'GET' : 'POST', headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: response.status, body: await response.json() as any };
  };
  return { dataDir, request };
}

test('a rules17 store migrates once; a real Auto fight pauses for one focus tap that saves exactly once', async t => {
  const f = await fixture(t);
  assert.equal((await f.request(undefined, '/api/health')).body.capabilities.autoFocus, 1);
  let current = (await f.request()).body, serial = 0;
  assert.equal(current.state.rulesVersion, 19); assert.equal(current.state.focus, null);
  const stored = JSON.parse(await readFile(join(f.dataDir, 'store.json'), 'utf8'));
  assert.deepEqual([stored.formatVersion, stored.gameSchemaVersion, stored.rulesVersion], [21, 27, 19]);
  assert.deepEqual(stored.devices[0].legacy.histories.map((history: any) => history.rulesVersion), [17]);
  const send = async (events: Event[]) => {
    const result = await f.request(command(current.revision, `focus-flow-${++serial}`, events));
    assert.equal(result.status, 200, JSON.stringify(result.body?.error));
    current = result.body; return result;
  };
  let paused: any = null;
  for (let attempt = 0; attempt < 30 && !paused; ++attempt) {
    const hurt = current.state.collection.find((member: any) => member.id === current.state.activeCreatureId)?.injury;
    await send([...(hurt ? [{ type: 'treat', value: 0 }] : []), ...Array.from({ length: 12 }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }]);
    for (let guard = 0; guard < 8 && current.state.phase === 'encounter'; ++guard) {
      if (current.state.autoCapture >= 2) { paused = current; break; }
      await send([{ type: current.state.autoCapture === 1 ? 'auto-resume' : 'auto-fight', value: 0 }]);
    }
  }
  assert.ok(paused, 'a real rules-18 Auto fight reaches a focus pause');
  assert.ok(awaitingAutoFocus(paused.state));
  assert.ok(['strike', 'block'].includes(paused.state.focus.kind) && [1, 2].includes(paused.state.focus.turn));
  if (paused.autoTrace) assert.ok(pausedAutoTraceMatchesState(paused.autoTrace, paused.state));
  // Old-rules batches and out-of-range taps never answer the prompt.
  assert.equal((await f.request(command(paused.revision, 'focus-old-rules', [{ type: 'focus', value: 100 }], 17))).status, 409);
  assert.notEqual((await f.request(command(paused.revision, 'focus-out-of-range', [{ type: 'focus', value: 2401 }]))).status, 200);
  assert.equal((await f.request(command(paused.revision, 'focus-blocked-fight', [{ type: 'auto-fight', value: 0 }]))).status, 422);
  assert.deepEqual((await f.request()).body.revision, paused.revision);
  const answer = command(paused.revision, 'focus-answer-once', [{ type: 'focus', value: 1200 }]);
  const answered = await f.request(answer);
  assert.equal(answered.status, 200); assert.equal(answered.body.revision, paused.revision + 1);
  assert.ok(!awaitingAutoFocus(answered.body.state)); assert.equal(answered.body.state.focus, null);
  assert.ok(answered.body.autoTrace && answered.body.autoTrace.steps.length >= 1);
  assert.deepEqual(await f.request(answer), answered); // Exact receipt on retry; no second exchange.
});

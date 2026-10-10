import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { startServer } from '../service/server.ts';
import { parseAutoTrace } from '../service/battle-trace.ts';

const rootDir = resolve(import.meta.dirname, '..');
const token = Buffer.alloc(32, 62).toString('base64url'); // Synthetic identity.
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
type Event = { type: string; value: number };
const batch = (baseRevision: number, batchId: string, events: Event[]) => ({ rulesVersion: 18, baseRevision, batchId, events });
async function fixture(t: { after: (fn: () => Promise<void>) => unknown }) {
  const dataDir = await mkdtemp(join(tmpdir(), 'digivice-auto-flick-'));
  const events = [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }, { type: 'walk', value: 100 }];
  const store = { formatVersion: 15, gameSchemaVersion: 19, rulesVersion: 13, devices: [{
    deviceId: `dv_${'f'.repeat(24)}`, tokenHash: hash(token), seed: 12345, initialMode: 'onboarding', revision: 1, legacy: null, events,
    receipts: [{ batchId: 'synthetic-auto-start', revision: 1, eventEnd: events.length, bodyHash: hash(JSON.stringify({ rulesVersion: 13, baseRevision: 0, events })) }],
  }] };
  for (const name of ['store.json', 'store.backup.json']) await writeFile(join(dataDir, name), JSON.stringify(store));
  const options = { seedSource: () => 12345, rootDir, dataDir, port: 0, corePath: process.env.DIGIVICE_TEST_CORE_PATH, battleCorePath: process.env.DIGIVICE_TEST_BATTLE_PATH };
  let app = await startServer(options);
  const close = async () => { await new Promise<void>((resolve, reject) => app.server.close(error => error ? reject(error) : resolve())); app.close(); };
  t.after(async () => { if (app.server.listening) await close(); await rm(dataDir, { recursive: true, force: true }); });
  const request = async (body?: unknown) => {
    const response = await fetch(`http://127.0.0.1:${(app.server.address() as { port: number }).port}${body === undefined ? '/api/save' : '/api/save-sync'}`, {
      method: body === undefined ? 'GET' : 'POST', headers: { authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'content-type': 'application/json' }) }, body: body === undefined ? undefined : JSON.stringify(body),
    });
    return { status: response.status, body: await response.json() as any };
  };
  return { request, restart: async () => { await close(); app = await startServer(options); } };
}

test('Auto commits a capture pause, restores it, and requires each of three real flick events', async t => {
  const f = await fixture(t), before = await f.request();
  const command = batch(1, 'pause-at-eligibility', [{ type: 'auto-fight', value: 0 }]);
  const paused = await f.request(command); assert.equal(paused.status, 200);
  const state = paused.body.state, trace = paused.body.autoTrace;
  assert.equal(state.autoCapture, 1); assert.equal(state.phase, 'encounter');
  assert.ok(state.wildCaptureChance > 0 && state.wildHp <= state.wildMaxHp / 2);
  assert.equal(state.captureAttempts, 0); assert.equal(state.captures, before.body.state.captures);
  assert.equal(state.rngState, before.body.state.rngState);
  assert.equal(trace.outcome, 'none'); assert.ok(trace.steps.every((s: any) => s.action !== 'capture' && !s.captured));
  assert.equal(trace.steps.at(-1).enemyHpAfter, state.wildHp);
  assert.equal(trace.steps.at(-1).playerHpAfter, state.hp);
  assert.deepEqual(parseAutoTrace(trace, 'wild'), trace);
  assert.deepEqual(await f.request(command), paused);
  await f.restart(); assert.deepEqual((await f.request()).body.state, state);
  const walking = await f.request(batch(2, 'walk-while-paused', [{ type: 'accrue-steps', value: 1 }]));
  assert.equal(walking.status, 200); assert.equal(walking.body.state.autoCapture, 1); assert.deepEqual(walking.body.autoTrace, trace);
  for (const type of ['auto', 'auto-fight', 'capture', 'attack', 'magic']) {
    const rejected = await f.request(batch(3, 'reject-automatic-throw', [{ type, value: 0 }]));
    assert.equal(rejected.status, 422); assert.deepEqual((await f.request()).body.state, walking.body.state);
  }
  for (let attempt = 1; attempt <= 3; ++attempt) {
    const flick = batch(attempt + 2, `manual-aim-miss-${attempt}`, [{ type: 'flick', value: 0 }]);
    const thrown = await f.request(flick); assert.equal(thrown.status, 200);
    assert.equal(thrown.body.state.lastCapture.attempt, attempt); assert.equal(thrown.body.state.lastCapture.result, 'miss');
    assert.equal(thrown.body.state.rngState, state.rngState); assert.equal(thrown.body.state.captures, state.captures);
    assert.equal(thrown.body.state.autoCapture, attempt === 3 ? 0 : 1);
    assert.equal(thrown.body.state.phase, attempt === 3 ? 'home' : 'encounter');
    assert.equal(thrown.body.autoTrace, null, 'stale partial animation cannot replay over a throw');
    assert.deepEqual(await f.request(flick), thrown);
    await f.restart(); assert.deepEqual((await f.request()).body.state, thrown.body.state);
  }
});

test('Resume Fighting skips captures for the rest of this encounter and retries exactly', async t => {
  const f = await fixture(t);
  const paused = await f.request(batch(1, 'pause-before-declining', [{ type: 'auto-fight', value: 0 }]));
  assert.equal(paused.status, 200);
  const resume = batch(2, 'decline-and-resume', [{ type: 'auto-resume', value: 0 }]);
  const result = await f.request(resume); assert.equal(result.status, 200);
  assert.equal(result.body.state.phase, 'home'); assert.equal(result.body.state.autoCapture, 0);
  assert.equal(result.body.state.captures, paused.body.state.captures);
  assert.ok(result.body.autoTrace.steps.every((s: any) => s.action !== 'capture' && !s.captured));
  assert.ok(['won', 'retreated'].includes(result.body.autoTrace.outcome));
  assert.deepEqual(await f.request(resume), result);
  await f.restart(); assert.deepEqual((await f.request()).body.state, result.body.state);
  const fake = structuredClone(paused.body.autoTrace); fake.steps[0].action = 'capture'; fake.steps[0].captured = true;
  assert.throws(() => parseAutoTrace(fake, 'wild'));
  const pvp = structuredClone(paused.body.autoTrace); pvp.kind = 'practice';
  assert.throws(() => parseAutoTrace(pvp, 'practice'));
});

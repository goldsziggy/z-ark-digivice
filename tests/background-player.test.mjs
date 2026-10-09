import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { BACKGROUND_FRAME_BYTES, BACKGROUND_SCENES, createBackgroundPlayer } from '../web/background-player.js';

const packs = Object.fromEntries(BACKGROUND_SCENES.map(id => {
  const pack = JSON.parse(readFileSync(new URL(`../assets/packs/scene-${id}-v1.json`, import.meta.url), 'utf8'));
  return [pack.packId, pack];
}));
const tick = () => new Promise(resolve => setImmediate(resolve));
function fixture() {
  const jobs = [], frames = [], changes = [];
  const player = createBackgroundPlayer({ onChange: state => changes.push(state), decode: pack => new Promise((resolve, reject) => jobs.push({ pack, resolve, reject })) });
  function resolve(index, dimensions = {}) {
    const frame = { width: 480, height: 480, closes: 0, close() { this.closes++; }, ...dimensions };
    frames.push(frame); jobs[index].resolve(frame); return frame;
  }
  return { player, jobs, frames, changes, resolve };
}

test('all eight verified scenes draw exact frames, close prior frames, and stay within two RGBA frames', async () => {
  const { player, jobs, frames, resolve } = fixture(); player.setPacks(packs);
  const drawn = [], context = { drawImage: (...args) => drawn.push(args) };
  for (let i = 0; i < BACKGROUND_SCENES.length; i++) {
    const id = BACKGROUND_SCENES[i]; assert.equal(player.select(id), true); await tick();
    assert.equal(jobs.length, i + 1); assert.equal(jobs[i].pack, packs[`scene-${id}-v1`]);
    assert.equal(player.getState().inFlight, 1); assert.equal(player.draw(context), false, 'wrong-scene retained image is never shown');
    const frame = resolve(i); await tick();
    assert.equal(player.getState().status, 'ready'); assert.equal(player.getState().visibleId, id);
    assert.equal(player.getState().decodedBytes, BACKGROUND_FRAME_BYTES);
    assert.equal(player.draw(context, 2, 3, 400, 401), true); assert.deepEqual(drawn.at(-1), [frame, 2, 3, 400, 401]);
    assert.ok(frames.slice(0, -1).every(frame => frame.closes === 1));
  }
  assert.equal(player.getState().peakDecodedBytes, 2 * BACKGROUND_FRAME_BYTES);
  player.destroy(); assert.ok(frames.every(frame => frame.closes === 1)); assert.equal(player.getState().decodedBytes, 0);
});

test('rapid navigation serializes decode and coalesces to the latest requested scene', async () => {
  const { player, jobs, resolve } = fixture(); player.setPacks(packs); await tick();
  for (let round = 0; round < 20; round++) for (const id of BACKGROUND_SCENES) player.select(id);
  await tick(); assert.equal(jobs.length, 1); assert.equal(player.getState().id, 'digital');
  const stale = resolve(0); await tick(); assert.equal(stale.closes, 1); assert.equal(jobs.length, 2);
  assert.equal(jobs[1].pack.packId, 'scene-digital-v1');
  const latest = resolve(1); await tick(); assert.equal(latest.closes, 0); assert.equal(player.getState().visibleId, 'digital');
  assert.equal(player.getState().peakDecodedBytes, BACKGROUND_FRAME_BYTES);
  player.destroy(); assert.equal(latest.closes, 1);
});

test('missing/unsupported packs fall back without decoding or drawing an obsolete image', async () => {
  const { player, jobs, resolve } = fixture(); player.setPacks(packs); await tick(); resolve(0); await tick();
  const drawn = [], context = { drawImage: (...args) => drawn.push(args) };
  player.setPacks({}); assert.equal(player.getState().status, 'fallback');
  assert.equal(player.draw(context), false); assert.equal(drawn.length, 0);
  assert.equal(player.select('../forest'), false); assert.equal(player.getState().id, 'meadow');
  player.setPacks({ 'scene-meadow-v1': { ...packs['scene-meadow-v1'], formatVersion: 4 } });
  await tick(); assert.equal(jobs.length, 1); assert.equal(player.getState().status, 'fallback');
  player.setPacks(packs); await tick(); assert.equal(player.getState().status, 'ready'); assert.equal(jobs.length, 1, 'same verified object can reuse retained frame');
  player.destroy();
});

test('replacement during decode closes stale output and only adopts the replacement pack', async () => {
  const { player, jobs, resolve } = fixture(); player.setPacks(packs); await tick();
  const next = { ...packs, 'scene-meadow-v1': structuredClone(packs['scene-meadow-v1']) };
  player.setPacks(next); const stale = resolve(0); await tick(); assert.equal(stale.closes, 1);
  assert.equal(jobs.length, 2); assert.equal(jobs[1].pack, next['scene-meadow-v1']);
  resolve(1); await tick(); assert.equal(player.getState().status, 'ready'); player.destroy();
});

test('decoder errors are finite, invalid dimensions close, and explicit retry recovers', async () => {
  const { player, jobs, resolve } = fixture(); player.setPacks(packs); await tick();
  jobs[0].reject(new Error('JPEG entropy failure')); await tick(); assert.equal(player.getState().status, 'error');
  for (let i = 0; i < 20; i++) { player.select('meadow'); player.setPacks(packs); }
  await tick(); assert.equal(jobs.length, 1, 'render/reconciliation must not repeatedly retry a broken decode');
  player.retry(); await tick(); const invalid = resolve(1, { width: 481 }); await tick();
  assert.equal(invalid.closes, 1); assert.equal(player.getState().status, 'error'); assert.equal(player.getState().decodedBytes, 0);
  player.retry(); await tick(); resolve(2); await tick(); assert.equal(player.getState().status, 'ready'); player.destroy();
});

test('destroy during decode closes the late frame and prevents further callbacks or allocations', async () => {
  const { player, jobs, changes, resolve } = fixture(); player.setPacks(packs); await tick();
  player.destroy(); const count = changes.length; const late = resolve(0); await tick();
  assert.equal(late.closes, 1); assert.equal(changes.length, count); assert.equal(player.getState().decodedBytes, 0);
  assert.equal(player.select('forest'), false); player.setPacks(packs); player.retry(); await tick(); assert.equal(jobs.length, 1);
  assert.equal(player.draw({ drawImage() { throw new Error('Destroyed player must not draw'); } }), false);
  player.destroy(); assert.equal(late.closes, 1);
});

test('onChange may reenter with the same selection without duplicate decode or recursive notification', async () => {
  const jobs = []; let calls = 0, player;
  player = createBackgroundPlayer({ onChange: state => { assert.ok(++calls < 20); player.select(state.id); },
    decode: pack => new Promise(resolve => jobs.push({ pack, resolve })) });
  player.setPacks(packs); await tick(); assert.equal(jobs.length, 1);
  jobs[0].resolve({ width: 480, height: 480, close() {} }); await tick();
  assert.equal(player.getState().status, 'ready'); assert.equal(jobs.length, 1); player.destroy();
});

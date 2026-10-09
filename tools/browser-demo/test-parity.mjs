#!/usr/bin/env node
// Exercise the unchanged native rules through the same bridge on both targets.
// No fixtures rewrite HP, XP, RNG, ownership, or capture results.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { readFile, writeFile } from 'node:fs/promises';
import { createInterface } from 'node:readline';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const options = new Map();
for (let i = 2; i < process.argv.length; i += 2) {
  if (!['--native', '--module', '--source', '--report'].includes(process.argv[i]) || !process.argv[i + 1])
    throw new Error('Usage: node test-parity.mjs [--native BINARY] [--module JS] [--source ROOT] [--report JSON]');
  options.set(process.argv[i], resolve(process.argv[i + 1]));
}
const nativePath = options.get('--native') || resolve(here, 'native-output/digivice-demo-native');
const modulePath = options.get('--module') || resolve(root, 'docs/play/runtime/demo-core.js');
const sourceRoot = options.get('--source') || root;
const { sampleCaptureRing, captureRingChance } = await import(pathToFileURL(resolve(sourceRoot, 'web/capture-ring.js')));
const { default: createDemoCore } = await import(pathToFileURL(modulePath));
const wasmBinary = await readFile(resolve(dirname(modulePath), 'demo-core.wasm'));
const wasm = await createDemoCore({ wasmBinary, printErr: message => process.stderr.write(`${message}\n`) });
const native = spawn(nativePath, [], { stdio: ['pipe', 'pipe', 'pipe'] });
const lines = createInterface({ input: native.stdout });
const pending = [];
let nativeError = '';
let nativeExited = false;
native.stderr.on('data', data => { nativeError += data; });
native.once('error', error => { while (pending.length) pending.shift().reject(error); });
native.once('exit', (code, signal) => {
  nativeExited = true;
  while (pending.length) pending.shift().reject(new Error(`Native bridge exited ${code ?? signal}: ${nativeError}`));
});
lines.on('line', line => {
  const request = pending.shift();
  if (!request) throw new Error(`Unsolicited native output: ${line.slice(0, 200)}`);
  request.resolve(line);
});
function nativeCall(line) {
  assert(!nativeExited, 'Native bridge must remain available');
  assert(!/[\r\n]/.test(line), 'Native request must be one line');
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`Native response timed out for ${line.slice(0, 80)}`)), 15000);
    pending.push({ resolve: value => { clearTimeout(timer); resolve(value); }, reject: error => { clearTimeout(timer); reject(error); } });
    native.stdin.write(`${line}\n`);
  });
}
const history = [];
const report = { sourceCommit: null, rulesVersion: null, schemaVersion: null, checks: [], comparisons: 0, snapshotsCompared: 0, traceEvents: 0, captureGrades: {}, finished: false };
let state;
function remember(line) {
  history.push(line.startsWith('load ') ? `load [${line.length - 5} hex characters]` : line);
  if (history.length > 12) history.shift();
}
async function both(name, args = [], { raw = false, snapshot = true } = {}) {
  const line = [name, ...args].join(' ');
  remember(line);
  const api = ['reset', 'state', 'snapshot', 'load', 'starters', 'form', 'catalog', 'evolutions'].includes(name)
    ? { name: `demo_${name}`, args }
    : { name: 'demo_command', args: [name, args[0] ?? 0] };
  const output = wasm.ccall(api.name, 'string', api.args.map(x => typeof x === 'number' ? 'number' : 'string'), api.args);
  const nativeOutput = await nativeCall(line);
  const expected = raw ? nativeOutput : JSON.parse(nativeOutput);
  const actual = raw ? output : JSON.parse(output);
  assert.deepEqual(actual, expected, `Native/WASM difference after ${history.join(' → ')}`);
  ++report.comparisons;
  if (actual?.state) {
    state = actual.state;
    report.sourceCommit = actual.sourceCommit;
    report.rulesVersion = actual.rulesVersion;
    report.schemaVersion = actual.schemaVersion;
    assert.equal(actual.rulesVersion, 15);
    assert.equal(actual.schemaVersion, 22);
    assert.equal(state.collectionCapacity, 60);
    assert.equal(state.partyCapacity, 3);
    assert(state.collection.length <= state.collectionCapacity);
    assert(state.partyMemberIds.length <= state.partyCapacity);
    assert.equal(new Set(state.collection.map(member => member.id)).size, state.collection.length);
    assert(!state.partyMemberIds.includes(state.activeCreatureId));
    assert(state.partyMemberIds.every(id => state.collection.some(member => member.id === id)));
    if (actual.trace) ++report.traceEvents;
    if (snapshot) await snapshots();
  }
  return actual;
}
async function snapshots() {
  const hex = await both('snapshot', [], { raw: true, snapshot: false });
  assert.match(hex, /^[0-9a-f]{5928}$/);
  ++report.snapshotsCompared;
  return hex;
}
async function ok(command, value) {
  const result = await both(command, value === undefined ? [] : [value]);
  assert.equal(result.ok, true, `${command}: ${result.error}`);
  return result;
}
async function rejected(command, value, pattern) {
  const before = await snapshots();
  const result = await both(command, value === undefined ? [] : [value]);
  assert.equal(result.ok, false, `${command} must be rejected`);
  if (pattern) assert.match(result.error, pattern);
  assert.equal(await snapshots(), before, `${command} error must preserve canonical snapshot`);
  return result;
}
async function check(name, body) {
  const began = performance.now();
  await body();
  const elapsedMs = Math.round(performance.now() - began);
  report.checks.push({ name, elapsedMs });
  console.log(`PASS ${name} (${elapsedMs} ms)`);
}
async function hatch(seed = 12345, starter = 8) {
  await ok('reset', seed);
  assert.equal(state.phase, 'egg');
  assert.equal(state.collection.length, 0);
  assert.equal(state.onboarding.completed, false);
  await ok('hatch', starter);
  assert.equal(state.phase, 'home');
  assert.equal(state.collection.length, 1);
  assert.equal(state.onboarding.starterId, starter);
  assert(state.formId >= 11, 'Production starter must not be a legacy original fixture');
}
function phaseFor(grade, formId) {
  for (let phase = 0; phase < 2400; ++phase)
    if (sampleCaptureRing(phase, formId).grade === grade) return phase;
  throw new Error(`No ${grade} phase for form ${formId}`);
}
async function restoreCare() {
  assert.equal(state.phase, 'home');
  for (let i = 0; state.recoveryRestCount && i < 40; ++i) await ok('rest');
  assert.equal(state.recoveryRestCount, 0);
  for (let i = 0; state.fullness < 100 && i < 10; ++i) await ok('feed');
  for (let i = 0; state.mood < 100 && i < 10; ++i) await ok('play');
  if (state.recoveryRestCount) await ok('rest');
}
async function encounter() {
  assert.equal(state.phase, 'home');
  const before = structuredClone(state);
  const result = await ok('demo-encounter');
  assert(result.demoSteps > 0 && result.demoSteps <= 1000, 'Demo encounter uses a bounded synthetic step count');
  assert.equal(state.phase, 'encounter');
  assert.equal(state.walking.eligibleSteps - before.walking.eligibleSteps, result.demoSteps);
  assert.equal(state.encounters, before.encounters + 1);
  assert.equal(state.wildRules, 15);
  assert.equal(state.captures, before.captures);
  assert.equal(state.rngState, before.rngState, 'Walking must not consume capture RNG');
  return result;
}
async function pauseForCapture(maxEncounters = 80) {
  for (let i = 0; i < maxEncounters; ++i) {
    await restoreCare();
    if (state.battleMode !== 'auto') await ok('mode', 1);
    await encounter();
    const captures = state.captures;
    const rng = state.rngState;
    const result = await ok('auto-fight');
    assert.equal(state.captures, captures, 'AutoFight must not catch without manual input');
    assert.equal(state.rngState, rng, 'AutoFight must not spend capture RNG');
    assert.equal(state.captureAttempts, 0);
    if (state.phase === 'encounter') {
      assert.equal(state.autoCapture, 1);
      assert(state.wildHp <= Math.floor(state.wildMaxHp / 2));
      assert(state.wildCaptureChance > 0);
      assert(result.trace, 'Pause must expose actual native battle animation trace');
      return;
    }
    assert.equal(state.phase, 'home');
  }
  throw new Error(`No manual capture opportunity in ${maxEncounters} encounters`);
}
async function throwRing(grade) {
  const before = structuredClone(state);
  assert.equal(before.phase, 'encounter');
  const phase = phaseFor(grade, before.wildFormId);
  await ok('ring-capture', phase);
  const recorded = state.lastCapture;
  assert.equal(recorded.targetFormId, before.wildFormId);
  assert.equal(recorded.attempt, before.captureAttempts + 1);
  assert.equal(recorded.chance, captureRingChance(before.wildCaptureChance, grade));
  assert.notEqual(state.rngState, before.rngState, 'Every legal ring grade consumes a draw');
  assert(['captured', 'escaped'].includes(recorded.result));
  if (recorded.result === 'captured') {
    assert.equal(state.collection.length, before.collection.length + 1);
    assert.equal(state.captures, before.captures + 1);
    assert.equal(state.phase, 'home');
    for (const id of before.partyMemberIds) {
      const oldMember = before.collection.find(member => member.id === id);
      const newMember = state.collection.find(member => member.id === id);
      assert.equal(newMember.xp, Math.min(7600, oldMember.xp + 20 + 6 * before.wildLevel));
    }
  } else if (recorded.attempt === 3) {
    assert.equal(state.hp, before.hp, 'An escaped manual throw must not trigger an enemy attack');
    assert.equal(state.phase, 'home', 'Three escaped throws end the encounter');
    assert.equal(state.captures, before.captures);
  } else {
    assert.equal(state.hp, before.hp, 'An escaped manual throw must not trigger an enemy attack');
    assert.equal(state.phase, 'encounter');
    assert.equal(state.autoCapture, 1);
    assert.equal(state.captureAttempts, recorded.attempt);
  }
  const outcomes = report.captureGrades[grade] ||= { captured: 0, escaped: 0 };
  ++outcomes[recorded.result];
  return recorded.result;
}
async function catchUntil(count, maxEncounters = 1200) {
  for (let i = 0; state.collection.length < count && i < maxEncounters; ++i) {
    await pauseForCapture();
    while (state.phase === 'encounter') await throwRing('green');
  }
  assert.equal(state.collection.length, count, `Legitimate captures must reach ${count} members within bound`);
}

try {
  await check('Production starters and native metadata agree', async () => {
    const starterInfo = await both('starters');
    assert(starterInfo && typeof starterInfo === 'object');
    await both('catalog', [0, 16]);
    await both('form', [11]);
    await both('evolutions', [11, 0, 16]);
    for (let starter = 1; starter <= 8; ++starter) {
      await hatch(12345, starter);
      await rejected('hatch', starter, /already chosen/);
    }
    await ok('reset', 12345);
    await rejected('feed', undefined, /phase/);
    await rejected('hatch', 0, /value/);
    await rejected('hatch', 9, /value/);
  });
  await check('Demo boundary, invalid inputs, and save corruption are transactional', async () => {
    await hatch();
    const original = await snapshots();
    await rejected('unknown-demo-command', 0, /not available/);
    await rejected('world-seed', 9, /not available/);
    await rejected('walk', 100, /not available/);
    await rejected('demo-encounter', 1, /does not take/);
    await rejected('ring-capture', 0, /phase/);
    for (const corrupt of ['0', `g${original.slice(1)}`, `${original.slice(0, 80)}${original[80] === '0' ? '1' : '0'}${original.slice(81)}`])
      await rejected('load', corrupt, /snapshot|checksum/);
    await ok('feed');
    assert.notEqual(await snapshots(), original);
    await ok('load', original);
    assert.equal(await snapshots(), original);
    await ok('mode', 1);
    await encounter();
    await rejected('demo-encounter', 0, /phase/);
    await rejected('auto-fight', 1, /do not take/);
  });
  await check('Auto battle pauses for a human and Skip never captures', async () => {
    await hatch();
    await pauseForCapture();
    await rejected('auto-fight', 0);
    await rejected('ring-capture', 2400, /value/);
    await rejected('party-add', state.activeCreatureId, /phase/);
    const before = structuredClone(state);
    await ok('auto-resume');
    assert.equal(state.phase, 'home');
    assert.equal(state.captures, before.captures);
    assert.equal(state.rngState, before.rngState);
    assert.deepEqual(state.lastCapture, before.lastCapture);
    await rejected('auto-resume', 0, /phase/);
  });
  await check('Manual attacks, care, cards and locked evolution use native actions', async () => {
    await hatch(12345, 5);
    const locked = state.evolution.options.find(option => !option.eligible);
    assert(locked, 'A fresh Rookie must expose an actual locked evolution route');
    await rejected('evolve', locked.formId, /evolution requires/);
    const bond = state.bond;
    await restoreCare();
    assert(state.bond > bond, 'Useful care must build real bond');
    await encounter();
    await rejected('ring-capture', 0, /weaken/);
    const initial = structuredClone(state);
    const checkpoint = await snapshots();
    for (const action of ['attack', 'heavy', 'magic']) {
      await ok('load', checkpoint);
      await ok(action);
      assert.equal(state.sequence, initial.sequence + 1);
      assert(state.energy <= initial.energy, 'A manual attack must consume native energy cost');
      assert.notEqual(await snapshots(), checkpoint);
    }
    for (const card of [1, 2]) {
      await ok('load', checkpoint);
      await ok('card', card);
      assert.equal(state.cardUsed, true);
      await rejected('card', card, /one card/);
    }
  });
  await check('Red/orange/green grades use real odds; green can escape', async () => {
    let greenCaptured = false;
    let greenEscaped = false;
    let threeEscapes = false;
    for (let seed = 1; seed <= 64 && !(greenCaptured && greenEscaped && threeEscapes); ++seed) {
      await hatch(seed);
      await pauseForCapture();
      const checkpoint = await snapshots();
      for (const grade of ['red', 'orange', 'green']) {
        await ok('load', checkpoint);
        const outcome = await throwRing(grade);
        if (grade === 'green') {
          greenCaptured ||= outcome === 'captured';
          greenEscaped ||= outcome === 'escaped';
        }
      }
      await ok('load', checkpoint);
      while (state.phase === 'encounter') await throwRing('red');
      threeEscapes ||= state.lastCapture.attempt === 3 && state.lastCapture.result === 'escaped';
    }
    assert(greenCaptured && greenEscaped, 'Green timing must demonstrate both probabilistic outcomes');
    assert(threeEscapes, 'Three actual failed draws must end a match');
    assert.deepEqual(Object.keys(report.captureGrades).sort(), ['green', 'orange', 'red']);
  });
  await check('Owned box selection, three XP companions, rewards and release', async () => {
    await hatch();
    await catchUntil(5);
    const activeId = state.activeCreatureId;
    const others = state.collection.filter(member => member.id !== activeId).map(member => member.id);
    await rejected('party-add', activeId, /active/);
    await rejected('party-add', 0xfffffff0, /collection/);
    for (const id of others.slice(0, 3)) await ok('party-add', id);
    assert.deepEqual(state.partyMemberIds, others.slice(0, 3));
    await rejected('party-add', others[0], /already/);
    await rejected('party-add', others[3], /3|three|full/i);
    await catchUntil(6); // throwRing verifies full base XP for each chosen companion.
    await ok('select', others[0]);
    assert.equal(state.activeCreatureId, others[0]);
    assert.deepEqual(state.partyMemberIds, others.slice(1, 3));
    await ok('select', activeId);
    await ok('party-add', others[0]);
    await ok('party-remove', others[1]);
    assert(!state.partyMemberIds.includes(others[1]));
    await rejected('party-remove', others[1], /companion|party/i);
    await ok('party-add', others[1]);
    const count = state.collection.length;
    await ok('release', others[0]);
    assert.equal(state.collection.length, count - 1);
    assert(!state.partyMemberIds.includes(others[0]));
    await rejected('release', activeId, /active/);
  });
  await check('60-member capacity preserves ownership and round-trips every member', async () => {
    await catchUntil(60);
    const full = structuredClone(state);
    const saved = await snapshots();
    await ok('reset', 98765);
    await ok('load', saved);
    assert.deepEqual(state, full);
    const evolution = state.evolution.options.find(option => option.eligible);
    assert(evolution, 'Earned XP and care bond must unlock an actual evolution');
    const xp = state.xp;
    const ids = state.collection.map(member => member.id);
    await ok('evolve', evolution.formId);
    assert.equal(state.formId, evolution.formId);
    assert.equal(state.xp, xp);
    assert.deepEqual(state.collection.map(member => member.id), ids);
    await ok('mode', 0);
    await encounter();
    await rejected('ring-capture', 0, /full.*60/);
    assert.equal(state.collection.length, 60);
    report.finalCollectionCount = state.collection.length;
    report.finalSequence = state.sequence;
    report.finalEncounters = state.encounters;
  });
  await check('A fresh WASM instance restores a save and remains isolated', async () => {
    const saved = await snapshots();
    const original = structuredClone(state);
    const fresh = await createDemoCore({ wasmBinary });
    const invoke = (name, args = []) => fresh.ccall(`demo_${name}`, 'string', args.map(value => typeof value === 'number' ? 'number' : 'string'), args);
    const initial = JSON.parse(invoke('state'));
    assert.equal(initial.state.phase, 'egg');
    const restored = JSON.parse(invoke('load', [saved]));
    assert.equal(restored.ok, true);
    assert.deepEqual(restored.state, original);
    assert.equal(invoke('snapshot'), saved);
    const reset = JSON.parse(invoke('reset', [24680]));
    assert.equal(reset.state.phase, 'egg');
    assert.equal(await snapshots(), saved, 'Resetting a separate WASM instance must not clear the first');
    assert.deepEqual((await both('state')).state, original);
  });
  report.finished = true;
  if (options.has('--report')) await writeFile(options.get('--report'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(`PASS native/WASM parity: ${report.comparisons} outputs, ${report.snapshotsCompared} canonical snapshots, ${report.traceEvents} battle traces`);
} catch (error) {
  report.failure = error.stack || String(error);
  report.lastCommands = history;
  if (options.has('--report')) await writeFile(options.get('--report'), `${JSON.stringify(report, null, 2)}\n`);
  throw error;
} finally {
  native.stdin.end();
  lines.close();
  if (!nativeExited) native.kill();
}

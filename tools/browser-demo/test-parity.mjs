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
    assert.equal(actual.sourceCommit, process.env.DEMO_SOURCE_COMMIT || '9e6a74538326b82abf7bda0a7764b47e8094de09');
    assert.equal(actual.rulesVersion, 22);
    assert.equal(actual.schemaVersion, 30);
    assert.equal(state.collectionCapacity, 250);
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
  assert.match(hex, /^[0-9a-f]{13752}$/);
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
await check('rules19 snapshot migration preserves native/WASM state and saved care', async () => {
  const old = JSON.parse(await readFile(resolve(sourceRoot, 'tests/fixtures/rules19-save.json'), 'utf8'));
  const loaded = await both('load', [old.envelope.snapshot]);
  assert.equal(loaded.ok, true);
  for (const key of ['collection', 'rngState', 'sequence', 'xp', 'careMinute', 'activeCreatureId', 'partyMemberIds'])
    assert.deepEqual(state[key], old.state[key], `rules19 migration preserves ${key}`);
  for (const [key,value] of Object.entries(old.state.expeditions)) assert.deepEqual(state.expeditions[key],value,`Migration preserves expedition ${key}`);
  assert.equal(state.expeditions.active,false);
  assert(state.evolution.options.every(option => option.requiredCare === 0 && option.maxCareMistakes === null && option.careRouteOpen));
});
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
function active() { return state.collection.find(member => member.id === state.activeCreatureId); }
function phaseFor(grade, formId) {
  for (let phase = 0; phase < 2400; ++phase)
    if (sampleCaptureRing(phase, formId).grade === grade) return phase;
  throw new Error(`No ${grade} phase for form ${formId}`);
}
async function restoreCare() {
  assert.equal(state.phase, 'home');
  if (active().injury) await ok('treat');
  for (let i = 0; state.recoveryRestCount && i < 40; ++i) await ok('rest');
  assert.equal(state.recoveryRestCount, 0);
  for (let i = 0; state.fullness < 100 && i < 10; ++i) await ok('feed');
  for (let i = 0; state.mood < 100 && i < 10; ++i) await ok('play');
  if (state.toilet >= 25 || state.careMissed) await ok('toilet');
}
async function encounter() {
  assert.equal(state.phase, 'home');
  const before = structuredClone(state);
  const result = await ok('demo-encounter');
  assert(result.demoSteps > 0 && result.demoSteps <= 1000);
  assert.equal(state.phase, 'encounter');
  assert.equal(state.walking.eligibleSteps - before.walking.eligibleSteps, result.demoSteps);
  assert.equal(state.encounters, before.encounters + 1);
  assert.equal(state.wildRules, 22);
  assert.equal(state.rngState, before.rngState, 'Synthetic walking never draws capture RNG');
}
async function throwRing(grade) {
  const before = structuredClone(state);
  const phase = phaseFor(grade, before.wildFormId);
  await ok('ring-capture', phase);
  const recorded = state.lastCapture;
  assert.equal(recorded.targetFormId, before.wildFormId);
  assert.equal(recorded.attempt, before.captureAttempts + 1);
  assert.equal(recorded.chance, captureRingChance(before.wildCaptureChance, grade));
  assert.notEqual(state.rngState, before.rngState);
  assert(['captured', 'escaped'].includes(recorded.result));
  if (recorded.result === 'captured') {
    const duplicate = before.collection.some(member => member.formId === before.wildFormId);
    assert.equal(state.collection.length, before.collection.length + (duplicate ? 0 : 1));
    assert.equal(state.captures, before.captures + 1);
    assert.equal(state.phase, 'home');
  } else {
    assert.equal(state.hp, before.hp, 'A missed timing throw itself does not attack');
    assert.equal(state.phase, 'encounter', 'Rules 16+ misses retain the fight');
    assert.equal(state.autoCapture, 0);
    assert.equal(state.captureDeferred, 1);
    await rejected('ring-capture', phase);
  }
  const outcomes = report.captureGrades[grade] ||= { captured: 0, escaped: 0 };
  ++outcomes[recorded.result];
}
async function finishAuto({ capture = false, focusPhase = 2400 } = {}) {
  const captures = state.captures;
  for (let guard = 0; state.phase === 'encounter' && guard < 16; ++guard) {
    if (state.focus) {
      ++report.focusInputs;
      const before = structuredClone(state);
      const result = await ok('focus', focusPhase);
      assert(result.trace, 'A real focus answer returns its authoritative battle trace');
      assert.deepEqual(state.lastCapture, before.lastCapture, 'Focus never rolls a capture');
    } else if (state.autoCapture === 1) {
      if (capture) await throwRing('green');
      else await ok('auto-resume');
    } else {
      const before = structuredClone(state);
      const result = await ok('auto-fight');
      assert(result.trace);
      assert.equal(state.captures, before.captures);
      assert.deepEqual(state.lastCapture, before.lastCapture, 'Attacks may roll criticals, never captures');
    }
  }
  assert.equal(state.phase, 'home', 'Bounded real Auto actions must resolve the fight');
  if (!capture) assert.equal(state.captures, captures, 'Skipping prompts never captures');
}

report.focusInputs = 0;
try {
  await check('Current starters, metadata, versions, XP and canonical snapshots agree', async () => {
    await both('starters'); await both('catalog', [0, 16]); await both('form', [11]); await both('evolutions', [11, 0, 16]);
    for (let starter = 1; starter <= 8; ++starter) {
      await hatch(12345, starter);
      assert.equal(state.xpToNext, 40);
      assert.equal(state.maxLevel, 99);
      await rejected('hatch', starter, /already chosen/);
    }
    await ok('reset', 12345);
    await rejected('feed', undefined, /phase/);
    await rejected('hatch', 0, /value/); await rejected('hatch', 9, /value/);
  });
  await check('Invalid actions, old-length saves and CRC failures preserve the current save', async () => {
    await hatch();
    const original = await snapshots();
    for (const command of ['unknown-demo-command', 'world-seed', 'walk', 'claim-expedition'])
      await rejected(command, 0, /not available/);
    await rejected('demo-encounter', 1, /does not take/);
    await rejected('ring-capture', 0, /phase/);
    await rejected('start-expedition', 6, /value/);
    await rejected('focus', 2401, /value/);
    for (const corrupt of ['0', '0'.repeat(5928), `g${original.slice(1)}`, `${original.slice(0, 80)}${original[80] === '0' ? '1' : '0'}${original.slice(81)}`])
      await rejected('load', corrupt, /snapshot|checksum/);
    await ok('feed'); assert.notEqual(await snapshots(), original);
    await ok('load', original); assert.equal(await snapshots(), original);
    await rejected('start-expedition', 0); await rejected('start-expedition', 3);
  });
  await check('Care, manual attacks, cards, retreat and injury treatment use native actions', async () => {
    await hatch(12345, 1);
    const locked = state.evolution.options.find(option => !option.eligible);
    await rejected('evolve', locked.formId, /evolution requires/);
    await rejected('treat');
    await restoreCare(); await rejected('toilet'); assert.equal(state.toilet, 0);
    await rejected('care-minute', state.careMinute + 2, /next visible/);
    for (let i = 0; i < 4; ++i) await ok('care-minute', state.careMinute + 1);
    assert.equal(state.toilet, 32); await ok('toilet'); assert.equal(state.toilet, 0);
    await encounter();
    const checkpoint = await snapshots(), before = structuredClone(state);
    for (const move of ['attack', 'heavy', 'magic']) {
      await ok('load', checkpoint); await ok(move);
      assert.equal(state.sequence, before.sequence + 1);
      assert(state.energy <= before.energy);
    }
    for (const card of [1, 2]) {
      await ok('load', checkpoint); await ok('card', card);
      assert.equal(state.cardUsed, true); await rejected('card', card, /one card/);
    }
    await ok('load', checkpoint); await ok('retreat');
    assert.equal(state.phase, 'home');
    assert(state.hp < before.hp);
    // An actual defeat earns the injury; no health or care bits are injected.
    await hatch(12345, 1); await encounter();
    for (let i = 0; state.phase === 'encounter' && i < 40; ++i) await ok('attack');
    assert.equal(state.phase, 'home'); assert(active().injury > 0);
    const hp = state.hp; await ok('treat');
    assert.equal(active().injury, 0); assert.equal(state.hp, hp, 'Treat clears injury without inventing a heal');
    await restoreCare(); assert.equal(state.hp, state.combat.maxHp);
  });
  await check('All timing grades use native odds; missed throws continue the same fight', async () => {
    await hatch(12345, 1); await ok('mode', 1); await encounter();
    await ok('auto-fight');
    assert.equal(state.autoCapture, 1);
    const checkpoint = await snapshots();
    for (const grade of ['red', 'orange', 'green']) {
      await ok('load', checkpoint); await throwRing(grade);
    }
    await ok('load', checkpoint); await rejected('ring-capture', 2400, /value/);
    await rejected('auto-fight'); await ok('auto-resume');
    if (state.phase === 'encounter') await finishAuto();
    assert.equal(state.captures, 0);
  });
  await check('Natural progression earns keys; all six scenarios charge the right currency', async () => {
    await hatch(12345, 1); await ok('mode', 1);
    for (let i = 0; !state.expeditions.dungeonKeys && i < 160; ++i) {
      await restoreCare(); await encounter(); await finishAuto();
    }
    assert(state.expeditions.dungeonKeys > 0, 'Real wins must earn a key');
    assert(state.xp > 0 && state.level > 1);
    assert.equal(state.xpToNext, 20 * state.level * (state.level + 1) - state.xp);
    const saved = await snapshots();
    for (let scenario = 0; scenario < 3; ++scenario) {
      await ok('load', saved); const held = state.expeditions.dungeonKeys;
      await ok('start-expedition', scenario);
      assert.equal(state.phase, 'encounter'); assert.equal(state.wildRules, 22);
      assert.equal(state.expeditions.dungeonKeys, held - 1);
      await ok('retreat'); assert.equal(state.expeditions.dungeonKeys, held - 1, 'Retreat does not refund entry');
      if (!state.expeditions.dungeonKeys) await rejected('start-expedition', scenario);
    }
    for (let i = 0; !state.expeditions.bossSigils && i < 160; ++i) {
      await encounter(); await ok('retreat');
    }
    assert.equal(state.expeditions.bossSigils, 1, 'Only simulated native steps earned this sigil');
    const bossSave = await snapshots();
    for (let scenario = 3; scenario < 6; ++scenario) {
      await ok('load', bossSave); await ok('start-expedition', scenario);
      assert.equal(state.phase, 'encounter'); assert.equal(state.expeditions.bossSigils, 0);
      await ok('retreat'); await rejected('start-expedition', scenario);
    }
  });
  await check('Focus prompts, collection, companions, selection and release persist', async () => {
    await hatch(12345, 1); await ok('mode', 1);
    for (let i = 0; state.collection.length < 5 && i < 160; ++i) {
      await restoreCare(); await encounter(); await finishAuto({ capture: true, focusPhase: 1200 });
    }
    assert(state.collection.length >= 5);
    assert(report.focusInputs > 0, 'A natural encounter must exercise current focus flow');
    const activeId = state.activeCreatureId;
    const others = state.collection.filter(member => member.id !== activeId).map(member => member.id);
    await rejected('party-add', activeId, /active/);
    for (const id of others.slice(0, 3)) await ok('party-add', id);
    assert.deepEqual(state.partyMemberIds, others.slice(0, 3));
    await rejected('party-add', others[3], /3|three|full/i);
    await ok('select', others[0]); assert(!state.partyMemberIds.includes(others[0]));
    await ok('select', activeId); await ok('party-add', others[0]);
    await ok('party-remove', others[1]); assert(!state.partyMemberIds.includes(others[1]));
    const count = state.collection.length;
    await ok('release', others[0]); assert.equal(state.collection.length, count - 1);
    assert(!state.partyMemberIds.includes(others[0])); await rejected('release', activeId, /active/);
    report.finalCollectionCount = state.collection.length;
    report.finalSequence = state.sequence; report.finalEncounters = state.encounters;
  });
  await check('Fresh WASM instances restore current saves without changing the first instance', async () => {
    const saved = await snapshots(), original = structuredClone(state);
    const fresh = await createDemoCore({ wasmBinary });
    const invoke = (name, args = []) => fresh.ccall(`demo_${name}`, 'string', args.map(value => typeof value === 'number' ? 'number' : 'string'), args);
    assert.equal(JSON.parse(invoke('state')).state.phase, 'egg');
    const restored = JSON.parse(invoke('load', [saved])); assert.equal(restored.ok, true);
    assert.deepEqual(restored.state, original); assert.equal(invoke('snapshot'), saved);
    assert.equal(JSON.parse(invoke('reset', [24680])).state.phase, 'egg');
    assert.equal(await snapshots(), saved); assert.deepEqual((await both('state')).state, original);
  });
  report.finished = true;
  if (options.has('--report')) await writeFile(options.get('--report'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(`PASS native/WASM parity: ${report.comparisons} outputs, ${report.snapshotsCompared} canonical snapshots, ${report.traceEvents} battle traces`);
} catch (error) {
  report.failure = error.stack || String(error); report.lastCommands = history;
  if (options.has('--report')) await writeFile(options.get('--report'), `${JSON.stringify(report, null, 2)}\n`);
  throw error;
} finally {
  native.stdin.end(); lines.close(); if (!nativeExited) native.kill();
}

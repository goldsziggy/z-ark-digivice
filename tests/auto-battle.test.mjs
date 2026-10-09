import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { validateAutoTrace, autoStepText, autoResultTitle, captureChoice, awaitingAutoCapture, pausedAutoTraceMatchesState } from '../web/auto-battle.js';
import { displayGameMessage } from '../web/game-message.js';

test('legacy game messages present Digimon without rewriting unrelated or stored text', () => {
  const receipt = { message: 'Your friend is free to roam; your journal remembers them.' };
  assert.equal(displayGameMessage(receipt.message), 'Your Digimon is free to roam; your journal remembers them.');
  assert.equal(receipt.message, 'Your friend is free to roam; your journal remembers them.');
  assert.equal(displayGameMessage('A new friend joined your collection!'), 'A new Digimon joined your collection!');
  assert.equal(displayGameMessage('A friendly battle won.'), 'A friendly battle won.');
  assert.equal(displayGameMessage('An unrelated friend quote'), 'An unrelated friend quote');
  assert.equal(displayGameMessage('saved_message'), 'saved message');
  assert.equal(displayGameMessage(null), 'A new little adventure awaits.');
});

const combat = { maxHp: 100, attack: 18, defense: 14, magic: 16, resistance: 16, type: 'grove', skills: { physical: 'Twig Tap', heavy: 'Root Ram', magic: 'Seed Spark' } };
export const traceFixture = (kind = 'wild') => ({ formatVersion: 1, mode: 'auto', kind, startSequence: 2, endSequence: 3,
  outcome: kind === 'wild' ? 'captured' : 'won', player: { species: 'mote', name: 'Mote', level: 1, combat }, enemy: { species: 'flicker', name: 'Flicker', level: 1, combat },
  steps: [{ turn: 1, phase: 'attack', action: kind === 'wild' ? 'capture' : 'physical', opponentAction: null,
    playerHpBefore: 100, playerHpAfter: 100, enemyHpBefore: 10, enemyHpAfter: kind === 'wild' ? 10 : 0, reflected: false, captured: kind === 'wild' }] });

test('Auto replay carries only bounded saved participants and outcomes, with no policy calculation', () => {
  for (const kind of ['wild', 'practice']) {
    const original = traceFixture(kind), trace = validateAutoTrace(original, kind);
    assert.deepEqual(trace, original); trace.player.name = 'Changed UI copy'; assert.equal(original.player.name, 'Mote');
    assert.ok(autoResultTitle(original)); assert.ok(autoStepText(original.steps[0]));
  }
  assert.equal(validateAutoTrace(null, 'wild'), null);
  assert.match(autoStepText({ ...traceFixture().steps[0], captured: false }), /missed/);
});

test('malformed, oversized, wrong-mode and secret-bearing replay records are rejected', () => {
  const original = traceFixture();
  const mutate = change => { const next = structuredClone(original); change(next); return next; };
  const bad = [undefined, {}, { ...original, rngState: 123 }, { ...original, mode: 'tactical' }, { ...original, kind: 'practice' },
    mutate(value => { value.player.species = 'unknown'; }), mutate(value => { value.player.token = 'SECRET'; }),
    mutate(value => { value.player.combat.maxHp = 1000000; }), mutate(value => { value.steps[0].turn = 0; }),
    mutate(value => { value.steps[0].playerHpAfter = -1; }), mutate(value => { value.steps[0].rng = 'SECRET'; }),
    mutate(value => { value.steps = Array.from({ length: 49 }, (_, index) => ({ ...value.steps[0], turn: index + 1 })); })];
  for (const value of bad) assert.throws(() => validateAutoTrace(value, 'wild'));
  const practice = traceFixture('practice'); practice.steps[0].captured = true;
  assert.throws(() => validateAutoTrace(practice, 'practice'));
});

test('actual native Wild and Practice records cross the browser boundary unchanged', () => {
  const options = { encoding: 'utf8', timeout: 2000, maxBuffer: 32768 };
  const wild = JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH || './build/digivice-core', ['--replay-trace', '12345'], {
    ...options, input: `${'play 0\n'.repeat(7)}mode 1\nwalk 100\nauto 0\nfeed 0\n`,
  }));
  const recorded = validateAutoTrace(wild.trace, 'wild');
  assert.equal(recorded.player.name, 'Mote'); assert.equal(wild.state.phase, 'home');
  assert.equal(wild.state.lastAutoBattle.sequence, recorded.endSequence);
  assert.ok(wild.state.sequence > recorded.endSequence, 'care after the battle keeps its earlier actor record');
  const practice = JSON.parse(execFileSync(process.env.DIGIVICE_TEST_BATTLE_PATH || './build/digivice-battle', ['--practice-start', '12345', 'mote', '1', 'flicker', '1', 'auto'], options));
  const duel = validateAutoTrace(practice.trace, 'practice');
  assert.equal(practice.state.phase, 'finished'); assert.equal(duel.outcome, practice.state.status);
  assert.equal(duel.endSequence, practice.state.sequence);
});

test('Auto attack narration uses each frozen named move and preserves its category', () => {
  const trace = traceFixture('practice');
  trace.player.combat = structuredClone(combat);
  trace.player.combat.skills = { physical: 'Frozen Claw', heavy: 'Frozen Charge', magic: 'Frozen Flame' };
  const original = structuredClone(trace);
  for (const [action, category] of [['physical', 'Physical'], ['heavy', 'Heavy'], ['magic', 'Magic']]) {
    const step = { ...trace.steps[0], action };
    assert.ok(autoStepText(step, trace).startsWith(`${trace.player.combat.skills[action]} (${category}) · Rival −10 HP`));
  }
  assert.deepEqual(trace, original, 'narration never rewrites a saved participant or action');
  assert.match(autoStepText(trace.steps[0]), /^Physical attack ·/, 'legacy helper calls retain their generic fallback');
});

test('Auto defense narration names the revealed rival move from its own frozen profile', () => {
  const trace = traceFixture('practice'); trace.enemy.combat = structuredClone(combat);
  trace.enemy.combat.skills.magic = 'Recorded Rival Flame';
  const step = { ...trace.steps[0], phase: 'defend', action: 'ward', opponentAction: 'magic', playerHpAfter: 96 };
  assert.equal(autoStepText(step, trace), 'Rune Ward vs Recorded Rival Flame (Magic) · Rival −10 HP · You −4 HP');
  assert.match(autoStepText({ ...step, opponentAction: null }, trace), /^Rune Ward ·/);
  const oldTrace = structuredClone(trace); oldTrace.enemy.combat.skills.magic = 'Legacy Seed Spark';
  assert.match(autoStepText(step, oldTrace), /Legacy Seed Spark/);
  assert.doesNotMatch(autoStepText(step, oldTrace), /Recorded Rival Flame/);
  assert.equal(autoStepText({ ...step, action: 'capture', captured: true }, trace), 'Capture succeeded. Your new companion is saved.');
});

test('Capture prepares throws without exposing odds until the committed result', () => {
  const encounter = { phase: 'encounter', wildHp: 1, wildMaxHp: 100, captureAttempts: 0, collection: [{}], collectionCapacity: 60 };
  for (const chance of [50, 70, 89, 75, 80, 85]) {
    const choice = captureChoice({ ...encounter, wildCaptureChance: chance });
    assert.equal(choice.available, true); assert.equal(choice.label, 'Capture · 3 left');
    assert.doesNotMatch(choice.detail, /\d+%/);
  }
  for (const chance of [undefined, null, -1, 101, 50.5, 0]) assert.equal(captureChoice({ ...encounter, wildCaptureChance: chance }).available, false);
  assert.equal(captureChoice({ ...encounter, wildCaptureChance: 0, collectionCapacity: 1 }).detail, 'Collection full');
  assert.equal(captureChoice({ ...encounter, wildCaptureChance: 0, captureAttempts: 3 }).detail, 'No attempts left');
  assert.equal(captureChoice({ ...encounter, wildCaptureChance: 0 }).detail, 'Weaken the wild Digimon first');
  assert.equal(captureChoice({ ...encounter, phase: 'home', wildCaptureChance: 80 }).available, false);
  for (const wildRules of [8, 9]) assert.match(captureChoice({ ...encounter, wildRules, wildCaptureChance: 50 }).detail, /Green gives full eligible odds/);
  assert.doesNotMatch(captureChoice({ ...encounter, wildRules: 7, wildCaptureChance: 75 }).detail, /Lower HP/);
});

test('a full carried team blocks capture and Auto arming even with stale positive odds', () => {
  const full = { phase: 'encounter', battleMode: 'auto', autoCapture: 1, captureAttempts: 0,
    collection: Array.from({ length: 60 }, (_, id) => ({ id: id + 1 })), collectionCapacity: 60 };
  for (const wildCaptureChance of [0, 1, 50, 100]) {
    const state = { ...full, wildCaptureChance };
    assert.deepEqual(captureChoice(state), { available: false, label: 'Capture', detail: 'Collection full' });
    assert.equal(awaitingAutoCapture(state), false);
  }
  const room = { ...full, collection: full.collection.slice(0, 59), wildCaptureChance: 50 };
  assert.equal(captureChoice(room).available, true);
  assert.equal(awaitingAutoCapture(room), true);
});

test('practice presentation bounds permit40 saved exchanges but reject41 without running a policy', () => {
  const trace = traceFixture('practice');trace.steps=Array.from({length:40},(_,i)=>({...trace.steps[0],turn:i+1}));trace.endSequence=42;
  assert.equal(validateAutoTrace(trace,'practice').steps.length,40);
  trace.steps.push({...trace.steps[0],turn:41});assert.throws(()=>validateAutoTrace(trace,'practice'));
});

const pausedNative = () => JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH || './build/digivice-core', ['--replay-onboarding-trace', '12345'], {
  encoding: 'utf8', input: 'hatch 1\nmode 1\nwalk 100\nauto-fight 0\n', timeout: 2000,
}));

test('manual Auto pause accepts its complete positive-HP attack trace, never a terminal/practice capture', () => {
  const { state, trace } = pausedNative();
  assert.equal(state.schemaVersion, 22); assert.equal(state.autoCapture, 1); assert.equal(trace.outcome, 'none');
  assert.deepEqual(validateAutoTrace(trace, 'wild'), trace);
  assert.equal(awaitingAutoCapture(state), true); assert.equal(pausedAutoTraceMatchesState(trace, state), true);
  for (const change of [
    t => { t.kind = 'practice'; }, t => { t.steps[0].action = 'capture'; }, t => { t.steps[0].captured = true; },
    t => { t.steps[0].phase = 'defend'; }, t => { t.steps[0].playerHpAfter = 0; },
    t => { t.steps[1].enemyHpBefore++; }, t => { t.steps[0].enemyHpAfter = t.steps[0].enemyHpBefore + 1; },
    t => { t.endSequence++; }, t => { t.steps = []; },
  ]) { const bad = structuredClone(trace); change(bad); assert.throws(() => validateAutoTrace(bad, 'wild')); }
  for (const bad of [{ ...state, autoCapture: 0 }, { ...state, battleMode: 'tactical' }, { ...state, phase: 'home' },
    { ...state, wildCaptureChance: 0 }, { ...state, foregroundSequence: state.foregroundSequence + 1 },
    { ...state, hp: state.hp - 1 }, { ...state, wildFormId: 99 }]) assert.equal(pausedAutoTraceMatchesState(trace, bad), false);
  assert.equal(pausedAutoTraceMatchesState(null, state), true, 'a later throw may clear the partial trace while remaining Awaiting');
  assert.equal(pausedAutoTraceMatchesState(trace, { ...state, sequence: state.sequence + 1 }), true, 'background bookkeeping preserves latest foreground pause');
  assert.equal(autoResultTitle(trace), 'Ready for your tap.');
});

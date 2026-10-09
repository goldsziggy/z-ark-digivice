import { validTraceCare, validTraceCapture } from './care-capture-state.js';
// Bounded presentation of authoritative, already committed battle records.
// No combat policy, reward calculation or network command lives here.
const object = value => value && typeof value === 'object' && !Array.isArray(value);
const exact = (value, keys) => object(value) && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
const integer = (value, min, max) => Number.isSafeInteger(value) && value >= min && value <= max;
const actions = new Set(['physical', 'heavy', 'magic', 'capture', 'brace', 'counter', 'ward']);
const TRACE_KEYS = ['formatVersion', 'mode', 'kind', 'startSequence', 'endSequence', 'outcome', 'player', 'enemy', 'steps'];
const STEP_KEYS = ['turn', 'phase', 'action', 'opponentAction', 'playerHpBefore', 'playerHpAfter', 'enemyHpBefore', 'enemyHpAfter', 'reflected', 'captured'];
function participant(value) {
  const expanded = object(value) && Object.hasOwn(value, 'formId');
  if (!exact(value, ['species', 'name', 'level', 'combat', ...(expanded ? ['formId'] : [])])
    || (expanded ? !integer(value.formId, 1, 512) || typeof value.species !== 'string' || !/^[a-z][a-z0-9-]{0,63}$/.test(value.species)
      : !['mote', 'flicker', 'rill', 'cinder', 'impmon', 'agumon', 'gabumon', 'patamon', 'tentomon', 'palmon', 'gomamon', 'renamon'].includes(value.species))
    || typeof value.name !== 'string' || !value.name.length || value.name.length > 64 || !integer(value.level, 1, 20)) return false;
  const combat = value.combat;
  return exact(combat, ['maxHp', 'attack', 'defense', 'magic', 'resistance', 'type', 'skills']) && integer(combat.maxHp, 1, 400)
    && ['attack', 'defense', 'magic', 'resistance'].every(key => integer(combat[key], 1, 128))
    && ['grove', 'tide', 'ember', 'neutral'].includes(combat.type) && exact(combat.skills, ['physical', 'heavy', 'magic'])
    && Object.values(combat.skills).every(label => typeof label === 'string' && label.length > 0 && label.length <= 64);
}

export function validateAutoTrace(value, kind) {
  if (value === null) return null;
  const current = object(value) && Object.hasOwn(value, 'combatRulesVersion');
  if (current && (kind !== 'wild' || value.combatRulesVersion !== 12 || !validTraceCare(value.playerCare) || !validTraceCare(value.enemyCare))) throw new Error('Unsupported Auto care record.');
  if (!['wild', 'practice'].includes(kind) || !exact(value, [...TRACE_KEYS, ...(current ? ['combatRulesVersion', 'playerCare', 'enemyCare'] : [])]) || value.formatVersion !== 1 || value.mode !== 'auto' || value.kind !== kind
    || !integer(value.startSequence, 0, 10000) || !integer(value.endSequence, value.startSequence + 1, 10000)
    || !participant(value.player) || !participant(value.enemy)
    || !(kind === 'wild' ? ['won', 'captured', 'retreated'] : ['won', 'lost', 'draw']).includes(value.outcome)
    || !Array.isArray(value.steps) || value.steps.length < 1 || value.steps.length > (kind === 'wild' ? 48 : 40)) throw new Error('Unsupported Auto battle record.');
  for (const [index, step] of value.steps.entries()) {
    const guarded = object(step) && Object.hasOwn(step, 'guard');
    if (!exact(step, [...STEP_KEYS, ...(guarded ? ['guard'] : []), ...(current && step.action === 'capture' ? ['capture'] : [])]) || guarded && (kind !== 'wild' || !['brace', 'ward', 'counter'].includes(step.guard)) || step.turn !== index + 1 || !['attack', 'defend'].includes(step.phase) || !actions.has(step.action)
      || !(step.opponentAction === null || actions.has(step.opponentAction))
      || !['playerHpBefore', 'playerHpAfter', 'enemyHpBefore', 'enemyHpAfter'].every(key => integer(step[key], 0, 400))
      || typeof step.reflected !== 'boolean' || typeof step.captured !== 'boolean'
      || current && step.action === 'capture' && !validTraceCapture(step.capture, step.captured)
      || kind === 'practice' && (step.captured || step.action === 'capture')) throw new Error('Unsupported Auto battle step.');
  }
  return structuredClone(value);
}

const ACTION_LABELS = Object.freeze({ physical: 'Physical attack', heavy: 'Heavy attack', magic: 'Magic attack', capture: 'Capture attempt', brace: 'Brace', counter: 'Reversal', ward: 'Rune Ward' });
const ATTACK_CATEGORIES = Object.freeze({ physical: 'Physical', heavy: 'Heavy', magic: 'Magic' });
function recordedMove(action, participant) {
  const label = participant?.combat?.skills?.[action];
  return ATTACK_CATEGORIES[action] && typeof label === 'string' && label.length > 0 && label.length <= 64
    ? `${label} (${ATTACK_CATEGORIES[action]})` : ACTION_LABELS[action];
}
export function autoStepText(step, trace = null) {
  // Names belong to this committed battle, including legacy traces. Never look
  // up a current roster profile or reinterpret the saved action category.
  const label = recordedMove(step.action, trace?.player);
  const opponent = step.phase === 'defend' && step.opponentAction !== null
    ? recordedMove(step.opponentAction, trace?.enemy) : null;
  const exchange = opponent ? `${label} vs ${opponent}` : label;
  if (step.capture) return `${step.capture.result === 'miss' ? 'Miss · no catch' : `${step.capture.chance}% throw chance`} · Throw ${step.capture.attempt}/3 · ${step.captured ? 'Captured and saved.' : step.capture.attempt === 3 ? 'Slipped free. No throws left.' : 'Slipped free.'}`;
  if (step.captured) return 'Capture succeeded. Your new companion is saved.';
  if (step.action === 'capture') return 'The capture beam missed. The battle continues.';
  const rival = Math.max(0, step.enemyHpBefore - step.enemyHpAfter);
  const player = Math.max(0, step.playerHpBefore - step.playerHpAfter);
  return `${exchange} · Rival −${rival} HP · You −${player} HP${step.reflected ? ' · Reversed!' : ''}`;
}

export function autoResultTitle(trace) {
  return { won: 'Auto victory!', captured: 'A new companion!', retreated: 'Safely back home.', lost: 'A lesson learned.', draw: 'An even match.' }[trace?.outcome] || 'Auto battle saved';
}

// Read the authoritative eligibility/chance, including migrated old encounters.
// This presentation helper deliberately does not derive odds from remaining HP.
export function captureChoice(state) {
  const chance = state?.wildCaptureChance;
  const available = state?.phase === 'encounter' && Number.isInteger(chance) && chance > 0 && chance <= 100;
  const detail = available ? `${Math.max(0, 3 - (state.captureAttempts || 0))} throws left · Odds after throwing` : state?.phase !== 'encounter' ? 'Find a wild companion first'
    : state.collection?.length >= state.collectionCapacity ? 'Collection full' : state.captureAttempts >= 3 ? 'No attempts left' : 'Weaken your new friend first';
  return { available, label: available ? `Capture · ${Math.max(0, 3 - (state.captureAttempts || 0))} left` : 'Capture', detail };
}

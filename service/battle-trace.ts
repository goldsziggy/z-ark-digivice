import { validTraceCare, validTraceCapture } from '../web/care-capture-state.js';
// Public native-derived animation data only. No RNG, snapshots or rule decisions.
type TraceCombat = { maxHp: number; attack: number; defense: number; magic: number; resistance: number; type: string; skills: { physical: string; heavy: string; magic: string } };
type Participant = { formId?: number; species: string; name: string; level: number; combat: TraceCombat };
export type AutoTrace = {
  formatVersion: 1; mode: 'auto'; kind: 'wild' | 'practice'; startSequence: number; endSequence: number;
  player: Participant; enemy: Participant;
  combatRulesVersion?: 12; playerCare?: { offenseBonus: number; protectionBonus: number }; enemyCare?: { offenseBonus: number; protectionBonus: number };
  outcome: 'won' | 'captured' | 'retreated' | 'lost' | 'draw';
  steps: Array<{ turn: number; phase: 'attack' | 'defend'; action: string; opponentAction: string | null;
    playerHpBefore: number; playerHpAfter: number; enemyHpBefore: number; enemyHpAfter: number; reflected: boolean; captured: boolean; capture?: { chance: number; attempt: number; result: 'miss' | 'escaped' | 'captured' }; guard?: null | 'brace' | 'ward' | 'counter' }>;
};
const object = (value: unknown): value is Record<string, unknown> => value !== null && typeof value === 'object' && !Array.isArray(value);
const exact = (value: Record<string, unknown>, names: string[]) => Object.keys(value).length === names.length && names.every(name => Object.hasOwn(value, name));
const integer = (value: unknown, maximum: number) => Number.isSafeInteger(value) && Number(value) >= 0 && Number(value) <= maximum;
const choices = new Set(['physical', 'heavy', 'magic', 'capture', 'brace', 'counter', 'ward']);
function participant(value: unknown): Participant {
  const text = (input: unknown) => typeof input === 'string' && input.length >= 1 && input.length <= 64 && !/[\u0000-\u001f\u007f]/.test(input);
  if (!object(value) || !exact(value, ['species', 'name', 'level', 'combat', ...(Object.hasOwn(value, 'formId') ? ['formId'] : [])]) || Object.hasOwn(value, 'formId') && (!integer(value.formId, 512) || Number(value.formId) < 1) || !(typeof value.species === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(value.species) && value.species.length <= 63) ||
    !text(value.name) || !integer(value.level, 20) || Number(value.level) < 1 || !object(value.combat)) throw new Error('Invalid native Auto participant.');
  const combat = value.combat;
  if (!exact(combat, ['maxHp', 'attack', 'defense', 'magic', 'resistance', 'type', 'skills']) || !['maxHp', 'attack', 'defense', 'magic', 'resistance'].every(key => integer(combat[key], 1000) && Number(combat[key]) >= 1) ||
    !['grove', 'tide', 'ember', 'neutral'].includes(String(combat.type)) || !object(combat.skills) || !exact(combat.skills, ['physical', 'heavy', 'magic']) || !Object.values(combat.skills).every(text)) throw new Error('Invalid native Auto combat profile.');
  return { ...(Object.hasOwn(value, 'formId') ? { formId: Number(value.formId) } : {}), species: String(value.species), name: String(value.name), level: Number(value.level), combat: {
    maxHp: Number(combat.maxHp), attack: Number(combat.attack), defense: Number(combat.defense), magic: Number(combat.magic), resistance: Number(combat.resistance), type: String(combat.type),
    skills: { physical: String(combat.skills.physical), heavy: String(combat.skills.heavy), magic: String(combat.skills.magic) } } };
}
export function parseAutoTrace(value: unknown, kind: AutoTrace['kind'], practiceLimit: 30 | 40 = 30): AutoTrace | null {
  if (value === null) return null;
  const invalid = () => { throw new Error('The native Auto trace is invalid or exceeds its bounds.'); };
  const current = object(value) && Object.hasOwn(value, 'combatRulesVersion');
  if (current && (kind !== 'wild' || value.combatRulesVersion !== 12 || !validTraceCare(value.playerCare) || !validTraceCare(value.enemyCare))) return invalid();
  if (!object(value) || !exact(value, ['formatVersion', 'mode', 'kind', 'startSequence', 'endSequence', 'outcome', 'steps', 'player', 'enemy', ...(current ? ['combatRulesVersion', 'playerCare', 'enemyCare'] : [])]) || value.formatVersion !== 1 || value.mode !== 'auto' || value.kind !== kind ||
    !integer(value.startSequence, 0xffffffff) || !integer(value.endSequence, 0xffffffff) || Number(value.endSequence) <= Number(value.startSequence) ||
    !['won', 'captured', 'retreated', 'lost', 'draw'].includes(String(value.outcome)) || !Array.isArray(value.steps) || value.steps.length < 1 || value.steps.length > (kind === 'wild' ? 48 : practiceLimit)) return invalid();
  if (kind === 'wild' ? value.endSequence !== Number(value.startSequence) + 1 : Number(value.endSequence) - Number(value.startSequence) !== value.steps.length) return invalid();
  const player = participant(value.player), enemy = participant(value.enemy);
  const steps = value.steps.map((step, index) => {
    if (!object(step) || !exact(step, ['turn', 'phase', 'action', 'opponentAction', 'playerHpBefore', 'playerHpAfter', 'enemyHpBefore', 'enemyHpAfter', 'reflected', 'captured', ...(Object.hasOwn(step, 'guard') ? ['guard'] : []), ...(current && step.action === 'capture' ? ['capture'] : [])]) || step.turn !== index + 1 ||
      !['attack', 'defend'].includes(String(step.phase)) || !choices.has(String(step.action)) || !(step.opponentAction === null || choices.has(String(step.opponentAction))) ||
      !['playerHpBefore', 'playerHpAfter', 'enemyHpBefore', 'enemyHpAfter'].every(key => integer(step[key], 1000)) || typeof step.reflected !== 'boolean' || typeof step.captured !== 'boolean' || Object.hasOwn(step, 'guard') && !(step.guard === null || ['brace', 'ward', 'counter'].includes(String(step.guard)))) return invalid();
    if (current && step.action === 'capture' && !validTraceCapture(step.capture, step.captured)) return invalid();
    return { turn: Number(step.turn), phase: step.phase as 'attack' | 'defend', action: String(step.action), opponentAction: step.opponentAction === null ? null : String(step.opponentAction),
      playerHpBefore: Number(step.playerHpBefore), playerHpAfter: Number(step.playerHpAfter), enemyHpBefore: Number(step.enemyHpBefore), enemyHpAfter: Number(step.enemyHpAfter), reflected: step.reflected, captured: step.captured, ...(Object.hasOwn(step, 'guard') ? { guard: step.guard as null | 'brace' | 'ward' | 'counter' } : {}), ...(current && step.action === 'capture' ? { capture: { ...(step.capture as NonNullable<AutoTrace['steps'][number]['capture']>) } } : {}) };
  });
  return { ...(current ? { combatRulesVersion: 12 as const, playerCare: value.playerCare as NonNullable<AutoTrace['playerCare']>, enemyCare: value.enemyCare as NonNullable<AutoTrace['enemyCare']> } : {}), formatVersion: 1, mode: 'auto', kind, player, enemy, startSequence: Number(value.startSequence), endSequence: Number(value.endSequence), outcome: value.outcome as AutoTrace['outcome'], steps };
}

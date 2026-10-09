// Native metadata supplies the recovery count and encounter rarity. This module
// never computes healing, XP, encounter probabilities or capture odds.
export const MAX_RECOVERY_RESTS = 40;
export function validRecoveryCount(value) {
  return Number.isInteger(value) && value >= 0 && value <= MAX_RECOVERY_RESTS;
}
export function recoveryReview(state, deviceId, revision) {
  if (!state || state.phase !== 'home' || !validRecoveryCount(state.recoveryRestCount) || state.recoveryRestCount === 0
    || typeof deviceId !== 'string' || !deviceId || !Number.isSafeInteger(revision) || revision < 0
    || !Number.isSafeInteger(state.activeCreatureId) || state.activeCreatureId < 1
    || !Number.isSafeInteger(state.sequence) || state.sequence < 0) return null;
  return { deviceId, revision, memberId: state.activeCreatureId, sequence: state.sequence, count: state.recoveryRestCount };
}
export function reviewedRecoveryEvents(review, state, deviceId, revision) {
  const current = recoveryReview(state, deviceId, revision);
  if (!review || !current || Object.keys(current).some(key => review[key] !== current[key])) return null;
  return Array.from({ length: current.count }, () => ({ type: 'rest', value: 0 }));
}
export function validEncounterRarity(value) { return value === null || ['common', 'uncommon', 'rare'].includes(value); }
export function rarityLabel(value) { return value === null ? 'Earlier encounter' : value === 'common' ? 'Common' : value === 'uncommon' ? 'Uncommon' : value === 'rare' ? 'Rare' : ''; }

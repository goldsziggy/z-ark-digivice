// Validate saved native pacing; lifetime physical steps belong to the device
// pedometer and are deliberately not inferred from these eligible game steps.
const keys = ['rate', 'name', 'eligibleSteps', 'encounters', 'rngState', 'target', 'progress', 'remainingSteps', 'pendingEncounter'];
const names = ['Off', 'Relaxed', 'Normal', 'Frequent'];
const uint = value => Number.isSafeInteger(value) && value >= 0 && value <= 0xffffffff;
export function validWalkingState(value, phase) {
  if (!['egg', 'home', 'encounter'].includes(phase)) return false;
  if (!value || typeof value !== 'object' || Array.isArray(value) || Object.keys(value).length !== keys.length || !keys.every(key => Object.hasOwn(value, key))
    || !Number.isInteger(value.rate) || value.rate < 0 || value.rate > 3 || value.name !== names[value.rate]
    || !keys.filter(key => key !== 'name' && key !== 'rate' && key !== 'pendingEncounter').every(key => uint(value[key])) || value.encounters > value.eligibleSteps) return false;
  const pending = value.pendingEncounter;
  if (pending !== null && (!pending || typeof pending !== 'object' || Array.isArray(pending) || Object.keys(pending).length !== 3
    || !['formId', 'level', 'rules'].every(key => Object.hasOwn(pending, key))
    || !Number.isInteger(pending.formId) || pending.formId < 1 || pending.formId > 512
    || !Number.isInteger(pending.level) || pending.level < 1 || pending.level > 50 || ![12, 13, 14, 15, 16, 17, 18, 19].includes(pending.rules) || phase === 'egg')) return false;
  if (value.target === 0) return value.eligibleSteps === 0 && value.encounters === 0 && value.rngState === 0 && value.progress === 0 && value.remainingSteps === 0 && pending === null;
  if (!value.rngState || value.target < 80 || value.target > 280 || value.target % 2 || value.progress >= value.target) return false;
  return value.remainingSteps === (phase !== 'egg' && value.rate && pending === null ? Math.ceil((value.target - value.progress) / value.rate) : 0);
}

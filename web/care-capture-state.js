// Validate and present values already calculated by the native game core.
// Neither battle damage nor capture outcomes are rolled in the browser.
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const integer = (value, min, max) => Number.isSafeInteger(value) && value >= min && value <= max;
const exact = (value, keys) => object(value) && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
export function validCare(member) {
  const care = member?.care, base = member?.combat;
  if (!base || !exact(care, ['bondRank', 'offenseBonus', 'protectionBonus', 'effective'])
    || !integer(care.bondRank, 0, 4) || !integer(care.offenseBonus, 0, Math.min(5, care.bondRank + 1))
    || !integer(care.protectionBonus, 0, Math.min(5, care.bondRank + 1))
    || !exact(care.effective, ['maxHp', 'attack', 'defense', 'magic', 'resistance'])) return false;
  return care.effective.maxHp === base.maxHp
    && ['attack', 'magic'].every(key => integer(base[key], 1, 1000) && care.effective[key] === base[key] + care.offenseBonus)
    && ['defense', 'resistance'].every(key => integer(base[key], 1, 1000) && care.effective[key] === base[key] + care.protectionBonus);
}
export function validLastCapture(value, sequence) {
  if (!exact(value, ['sequence', 'targetFormId', 'targetLevel', 'chance', 'attempt', 'result']) || !integer(sequence, 0, 0xffffffff)
    || !integer(value.sequence, 0, sequence) || !integer(value.targetLevel, 0, 20) || !integer(value.targetFormId, 0, 512) || !integer(value.chance, 0, 100)
    || !integer(value.attempt, 0, 3) || !['none', 'miss', 'escaped', 'captured'].includes(value.result)) return false;
  if (value.result === 'none') return value.sequence === 0 && value.targetFormId === 0 && value.targetLevel === 0 && value.chance === 0 && value.attempt === 0;
  return value.sequence > 0 && value.targetFormId > 0 && value.targetLevel > 0 && value.attempt > 0 && (value.result === 'miss' ? value.chance === 0 : integer(value.chance, 10, 90));
}
export function careSummary(member) {
  return validCare(member) ? `Care: ATK/MAG +${member.care.offenseBonus} · DEF/RES +${member.care.protectionBonus}` : '';
}
export function captureReport(record) {
  if (!record || !validLastCapture(record, record.sequence) || record.result === 'none') return null;
  const remaining = record.result === 'captured' ? 0 : 3 - record.attempt;
  return { missed: record.result === 'miss', captured: record.result === 'captured', remaining,
    odds: record.result === 'miss' ? 'Miss · no catch' : `${record.chance}% throw chance`,
    attempts: `Throw ${record.attempt}/3${record.result === 'captured' ? '' : ` · ${remaining} left`}` };
}

export function validTraceCare(value) {
  return exact(value, ['offenseBonus', 'protectionBonus']) && integer(value.offenseBonus, 0, 5) && integer(value.protectionBonus, 0, 5);
}
export function validTraceCapture(value, captured = value?.result === 'captured') {
  return exact(value, ['chance', 'attempt', 'result']) && integer(value.attempt, 1, 3)
    && ['miss', 'escaped', 'captured'].includes(value.result) && (value.result === 'captured') === captured
    && (value.result === 'miss' ? value.chance === 0 : integer(value.chance, 10, 90));
}

// Presentation and metadata validation only. Starter identity/stats/hatching are
// authoritative native-core state; these original drawings contain no game art.
export const STARTER_COUNT = 8;
const types = new Set(['grove', 'tide', 'ember', 'neutral']);
const number = (value, min, max) => Number.isSafeInteger(value) && value >= min && value <= max;
const label = value => typeof value === 'string' && value.trim().length > 0 && value.length <= 64;
export function validateStarterCatalog(value) {
  if (!value || value.formatVersion !== 1 || value.rulesVersion !== 19 || !Array.isArray(value.starters)
    || value.starters.length !== STARTER_COUNT) throw new Error('The starter list is unavailable or unsupported.');
  const species = new Set();
  for (const [index, starter] of value.starters.entries()) {
    const combat = starter?.combat;
    if (starter?.id !== index + 1 || !label(starter.name) || typeof starter.species !== 'string' || !/^[a-z][a-z0-9-]{0,31}$/.test(starter.species)
      || species.has(starter.species) || starter.stage !== 'Rookie' || !combat || !types.has(combat.type)
      || !['maxHp', 'attack', 'defense', 'magic', 'resistance'].every(key => number(combat[key], 1, 1000))
      || !combat.skills || !['physical', 'heavy', 'magic'].every(key => label(combat.skills[key])))
      throw new Error('The starter list is unavailable or unsupported.');
    species.add(starter.species);
  }
  return value.starters.map(starter => Object.freeze({ ...starter, combat: Object.freeze({ ...starter.combat, skills: Object.freeze({ ...starter.combat.skills }) }) }));
}
export const isEggState = state => state?.phase === 'egg' && state.onboarding?.completed === false;

const accents = ['#a77de0', '#eea34a', '#75abd9', '#edbe73', '#ca727c', '#85b870', '#61b8b1', '#d7b04d'];
export function paintStarterEgg(surface, id) {
  const ctx = surface.getContext('2d'), size = surface.width;
  ctx.clearRect(0, 0, size, size); ctx.save(); ctx.scale(size / 128, size / 128);
  const accent = accents[Math.max(0, id - 1) % accents.length];
  ctx.fillStyle = '#174d4220'; ctx.beginPath(); ctx.ellipse(64, 114, 33, 7, 0, 0, Math.PI * 2); ctx.fill();
  ctx.beginPath(); ctx.moveTo(64, 12); ctx.bezierCurveTo(91, 12, 109, 63, 103, 87);
  ctx.bezierCurveTo(98, 121, 30, 121, 25, 87); ctx.bezierCurveTo(19, 63, 37, 12, 64, 12); ctx.closePath();
  ctx.fillStyle = '#fff5d9'; ctx.fill(); ctx.lineWidth = 4; ctx.strokeStyle = '#234a4d'; ctx.stroke();
  ctx.save(); ctx.clip(); ctx.fillStyle = accent; ctx.strokeStyle = accent; ctx.lineWidth = 9;
  const style = (id - 1) % 4;
  if (style === 0) for (const [x, y] of [[47, 39], [79, 60], [44, 85], [83, 101]]) { ctx.beginPath(); ctx.arc(x, y, id > 4 ? 6 : 9, 0, Math.PI * 2); ctx.fill(); }
  if (style === 1) for (const y of [49, 82, 111]) { ctx.beginPath(); ctx.moveTo(20, y); ctx.lineTo(42, y - 11); ctx.lineTo(64, y + 2); ctx.lineTo(86, y - 11); ctx.lineTo(109, y); ctx.stroke(); }
  if (style === 2) for (const [x, y] of [[47, 47], [82, 72], [44, 99]]) { ctx.beginPath(); ctx.moveTo(x, y - 11); ctx.lineTo(x + 11, y); ctx.lineTo(x, y + 11); ctx.lineTo(x - 11, y); ctx.closePath(); ctx.fill(); }
  if (style === 3) for (const y of [45, 76, 106]) { ctx.beginPath(); ctx.ellipse(65, y, 44, 6, -.2, 0, Math.PI * 2); ctx.fill(); }
  ctx.restore(); ctx.strokeStyle = '#ffffffb0'; ctx.lineWidth = 6; ctx.lineCap = 'round';
  ctx.beginPath(); ctx.moveTo(42, 43); ctx.quadraticCurveTo(36, 52, 34, 65); ctx.stroke(); ctx.restore();
}

// A deliberately generic original buddy silhouette, never a purported drawing
// of the named Rookie. The caller labels it “Artwork pending”.
export function paintRookiePlaceholder(ctx, x, y, size = 96, type = 'neutral') {
  ctx.save(); ctx.translate(x, y); ctx.scale(size / 96, size / 96);
  ctx.fillStyle = ({ grove: '#91c896', tide: '#75cbd5', ember: '#efa878', neutral: '#bca0e1' })[type] || '#bca0e1';
  ctx.beginPath(); ctx.ellipse(0, 0, 30, 33, 0, 0, Math.PI * 2); ctx.fill();
  ctx.strokeStyle = '#234a4d'; ctx.lineWidth = 3; ctx.stroke();
  ctx.fillStyle = '#234a4d'; ctx.beginPath(); ctx.arc(-10, -3, 3, 0, Math.PI * 2); ctx.arc(10, -3, 3, 0, Math.PI * 2); ctx.fill();
  ctx.beginPath(); ctx.arc(0, 6, 7, .15, Math.PI - .15); ctx.stroke();
  ctx.fillStyle = '#fff5d9'; ctx.font = 'bold 11px Arial'; ctx.textAlign = 'center';
  ctx.fillRect(-49, 39, 98, 17); ctx.fillStyle = '#234a4d'; ctx.fillText('ARTWORK PENDING', 0, 51); ctx.restore();
}

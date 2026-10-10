// Presentation bounds only. The native core supplies XP, eligibility and previews.
const integer = (value, min, max) => Number.isSafeInteger(value) && value >= min && value <= max;
const label = value => typeof value === 'string' && value.length > 0 && value.length <= 64;
const object = value => value && typeof value === 'object' && !Array.isArray(value);
const exact = (value, keys) => object(value) && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
const stage = value => value === null || ['Original', 'Fresh', 'In-Training', 'Rookie', 'Champion', 'Ultimate', 'Mega', 'Armor', 'No Level'].includes(value);
const art = value => value === null || typeof value === 'string' && /^[a-z][a-z0-9-]{0,63}$/.test(value);
export function validProgressCombat(value) {
  return exact(value, ['maxHp', 'attack', 'defense', 'magic', 'resistance', 'type', 'skills'])
    && integer(value.maxHp, 1, 2048) && ['attack', 'defense', 'magic', 'resistance'].every(key => integer(value[key], 1, 256))
    && ['grove', 'tide', 'ember', 'neutral'].includes(value.type) && exact(value.skills, ['physical', 'heavy', 'magic']) && Object.values(value.skills).every(label);
}
function form(value) {
  return object(value) && integer(value.formId, 1, 512) && label(value.name) && stage(value.stage) && art(value.artId)
    && integer(value.requiredLevel, 1, 50) && integer(value.requiredBond, 0, 200) && integer(value.requiredCare, 12, 100) && integer(value.previewLevel, 1, 50) && validProgressCombat(value.combat);
}
export function validateEvolutionOptions(value) {
  if (!Array.isArray(value) || value.length > 2 || new Set(value.map(entry => entry?.formId)).size !== value.length
    || !value.every(entry => exact(entry, ['formId', 'name', 'stage', 'artId', 'requiredLevel', 'requiredBond', 'requiredCare', 'previewLevel', 'eligible', 'combat']) && form(entry) && typeof entry.eligible === 'boolean')) throw new Error('Unsupported evolution choices.');
  return structuredClone(value);
}
const ids = (value, max, self) => Array.isArray(value) && value.length <= max
  && new Set(value).size === value.length && value.every(id => integer(id, 1, 512) && id !== self);
export function validEvolutionLinks(value, self) {
  return object(value) && ids(value.parents, 512, self) && ids(value.children, 2, self)
    && Array.isArray(value.edges) && value.edges.length === value.children.length
    && new Set(value.edges.map(edge => edge?.toFormId)).size === value.edges.length
    && value.edges.every(edge => exact(edge, ['toFormId', 'requiredLevel', 'requiredBond', 'requiredCare']) && value.children.includes(edge.toFormId)
      && integer(edge.requiredLevel, 1, 50) && integer(edge.requiredBond, 0, 200) && integer(edge.requiredCare, 12, 100));
}
export function validateEvolutionGraph(value, focusFormId, offset = 0, limit = 8) {
  if (!exact(value, ['formatVersion', 'rulesVersion', 'catalogVersion', 'focusFormId', 'offset', 'limit', 'total', 'nextOffset', 'forms'])
    || value.formatVersion !== 2 || value.rulesVersion !== 16 || value.catalogVersion !== 6
    || !integer(focusFormId, 1, 512) || value.focusFormId !== focusFormId || value.offset !== offset || value.limit !== limit
    || !integer(offset, 0, 511) || !integer(limit, 1, 16) || !integer(value.total, 1, 512) || offset > value.total
    || !Array.isArray(value.forms) || value.forms.length !== Math.min(limit, value.total - offset)
    || new Set(value.forms.map(entry => entry?.formId)).size !== value.forms.length
    || value.nextOffset !== (offset + value.forms.length < value.total ? offset + value.forms.length : null)) throw new Error('Unsupported evolution graph page.');
  for (const entry of value.forms) {
    if (!exact(entry, ['formId', 'name', 'stage', 'artId', 'minLevel', 'minBond', 'previewLevel', 'combat', 'parents', 'children', 'edges'])
      || !integer(entry.formId, 1, 512) || !label(entry.name) || !stage(entry.stage) || !art(entry.artId)
      || !integer(entry.minLevel, 1, 50) || !integer(entry.minBond, 0, 200) || !integer(entry.previewLevel, 1, 50)
      || !validProgressCombat(entry.combat) || !validEvolutionLinks(entry, entry.formId)) throw new Error('Unsupported evolution graph node.');
  }
  return structuredClone(value);
}
export function evolutionRequirements(member, option) {
  const care = member.carePoints ?? 0;
  return [['Level', `${member.level} / ${option.requiredLevel}${member.level >= option.requiredLevel ? ' ✓' : ' needed'}`],
    ['Bond', `${member.bond} / ${option.requiredBond}${member.bond >= option.requiredBond ? ' ✓' : ' needed'}`],
    ['Care', `${care} / ${option.requiredCare}${care >= option.requiredCare ? ' ✓' : ' needed'}`]];
}

// No silhouette of a different form is substituted for missing character art.
export function paintFormPlaceholder(ctx, x, y, size = 96) {
  ctx.save(); ctx.translate(x, y); ctx.scale(size / 96, size / 96);
  ctx.fillStyle = '#fff5d9'; ctx.strokeStyle = '#234a4d'; ctx.lineWidth = 3;
  ctx.beginPath(); ctx.roundRect(-36, -38, 72, 72, 12); ctx.fill(); ctx.stroke();
  ctx.fillStyle = '#234a4d'; ctx.font = 'bold 42px Arial'; ctx.textAlign = 'center'; ctx.fillText('?', 0, 14);
  ctx.fillStyle = '#fff5d9'; ctx.fillRect(-49, 39, 98, 17); ctx.fillStyle = '#234a4d'; ctx.font = 'bold 11px Arial'; ctx.fillText('ARTWORK PENDING', 0, 51); ctx.restore();
}

// Read-only catalog metadata. Game eligibility, stats and journal membership are
// supplied by the service/native core; this module calculates no game values.
import { validProgressCombat, validEvolutionLinks, validateEvolutionGraph } from './progression.js';
import { validEncounterRarity } from './care-actions.js';
export const ROSTER_PAGE_SIZE = 8;
export const ROSTER_STAGES = Object.freeze(['Fresh', 'In-Training', 'Rookie', 'Champion', 'Ultimate', 'Mega', 'Armor', 'No Level']);
const integer = (value, low, high) => Number.isSafeInteger(value) && value >= low && value <= high;
const object = value => value && typeof value === 'object' && !Array.isArray(value);
const text = (value, max = 128) => typeof value === 'string' && value.length > 0 && value.length <= max;
const formId = value => integer(value, 1, 512);
function art(value, id) {
  return object(value) && ['private-local-available', 'unavailable'].includes(value.status)
    && value.artId === `ds-form-${id}` && (value.status === 'unavailable'
      || integer(value.version, 1, 0x7fffffff) && value.url === `/api/roster/art/${id}`);
}
function base(entry) {
  return object(entry) && formId(entry.formId) && text(entry.name, 64) && (entry.stage === null || text(entry.stage, 32))
    && text(entry.role, 64) && text(entry.combatTier, 32) && ['grove', 'tide', 'ember', 'neutral'].includes(entry.type)
    && validEncounterRarity(entry.encounterRarity) && entry.encounterRarity !== null
    && integer(entry.previewLevel, 1, 20) && validProgressCombat(entry.combat) && entry.combat.type === entry.type && art(entry.art, entry.formId);
}
function header(value) { return object(value) && value.formatVersion === 1 && value.catalogVersion === 6 && value.rulesVersion === 13; }
export function validateRosterPage(value) {
  if (!header(value) || !integer(value.total, 0, 512) || !integer(value.offset, 0, 512) || !integer(value.limit, 1, 16)
    || !Array.isArray(value.entries) || value.entries.length > value.limit || !value.entries.every(base)
    || new Set(value.entries.map(entry => entry.formId)).size !== value.entries.length) throw new Error('Unsupported roster page.');
  return structuredClone(value);
}
export function validateRosterDetail(value, id) {
  const row = value?.form;
  const stats = (value, max) => object(value) && ['maxHp', 'attack', 'defense', 'magic', 'resistance'].every(key => integer(value[key], 0, max));
  if (!header(value) || !base(row) || row.formId !== id || !integer(row.requiredLevel, 1, 20) || !integer(row.requiredBond, 0, 200)
    || !(row.parentId === 0 || row.parentId === null || formId(row.parentId)) || !Array.isArray(row.children) || row.children.length > 8 || !row.children.every(formId)
    || !validEvolutionLinks(row.evolution, id) || !['progression', 'terminal', 'independent'].includes(row.evolution.status)
    || !(row.evolution.reason === null || text(row.evolution.reason, 512))
    || (row.evolution.status === 'progression' ? !row.evolution.children.length || row.evolution.reason !== null
      : row.evolution.children.length > 0 || !row.evolution.reason)
    || typeof row.obtainable !== 'boolean' || !(row.leafReason === null || text(row.leafReason, 1024))
    || !stats(row.baseStats, 400) || !stats(row.growth, 128) || !text(row.statModel, 64)
    || row.canonicalEvolutionClaim !== false || !(row.source === null || object(row.source)) || !(row.official === null || object(row.official))) throw new Error('Unsupported roster detail.');
  return structuredClone(row);
}
async function request(path, fetcher) {
  const controller = new AbortController(); const timer = setTimeout(() => controller.abort(), 8000); let reader;
  try {
    const response = await fetcher(path, { credentials: 'omit', cache: 'no-store', signal: controller.signal });
    if (!response.ok || !response.body || Number(response.headers.get('content-length')) > 65536) { response.body?.cancel().catch(() => {}); throw new Error('Catalog unavailable'); }
    reader = response.body.getReader(); const chunks = []; let length = 0;
    while (true) { const { done, value } = await reader.read(); if (done) break; length += value.byteLength; if (length > 65536) throw new Error('Catalog response too large'); chunks.push(value); }
    const bytes = new Uint8Array(length); let offset = 0;
    for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
    return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
  } finally { clearTimeout(timer); controller.abort(); if (reader) { reader.cancel().catch(() => {}); reader.releaseLock(); } }
}
export async function fetchRosterPage({ offset = 0, q = '', prefix = '', stage = '' } = {}, fetcher = (...args) => fetch(...args)) {
  if (!integer(offset, 0, 512) || typeof q !== 'string' || q.length > 64 || !/^[a-z]?$/.test(prefix) || stage && !ROSTER_STAGES.includes(stage)) throw new Error('Unsupported catalog filter.');
  const query = new URLSearchParams({ offset: String(offset), limit: String(ROSTER_PAGE_SIZE) });
  if (q) query.set('q', q); if (prefix) query.set('prefix', prefix); if (stage) query.set('stage', stage);
  return validateRosterPage(await request(`/api/roster?${query}`, fetcher));
}
export async function fetchRosterDetail(id, fetcher = (...args) => fetch(...args)) {
  if (!formId(id)) throw new Error('Unsupported form identity.');
  return validateRosterDetail(await request(`/api/roster/${id}`, fetcher), id);
}
export async function fetchRosterIds(ids, fetcher = (...args) => fetch(...args)) {
  if (!Array.isArray(ids) || !ids.length || ids.length > ROSTER_PAGE_SIZE || !ids.every(formId) || new Set(ids).size !== ids.length) throw new Error('Unsupported journal page.');
  return validateRosterPage(await request(`/api/roster?offset=0&limit=${ROSTER_PAGE_SIZE}&ids=${ids.join(',')}`, fetcher));
}
export function rosterReferences(form) {
  const candidates = [['Official reference', form?.official?.url], ['Source game sheet', form?.source?.sheetUrl || form?.source?.url]];
  return candidates.flatMap(([label, value]) => {
    try { const url = new URL(value); return url.protocol === 'https:' && !url.username && !url.password ? [{ label, url: url.href }] : []; }
    catch { return []; }
  });
}

export function rosterEncyclopediaMoves(form) {
  const official = form?.official;
  const label = value => text(value, 64) && value.trim() === value
    && new TextEncoder().encode(value).byteLength <= 128 && !/[\u0000-\u001f\u007f<>]/.test(value);
  if (!object(official) || !label(official.name) || typeof official.sheetVariantReviewRequired !== 'boolean'
    || !Array.isArray(official.specialMoves) || official.specialMoves.length > 8
    || !official.specialMoves.every(label)) return [];
  let url;
  try { url = new URL(official.url); } catch { return []; }
  if (url.protocol !== 'https:' || url.hostname !== 'digimon.net' || url.port || url.username || url.password
    || url.pathname !== '/reference_en/detail.php' || !url.searchParams.get('directory_name')) return [];
  return [...new Set(official.specialMoves)].map(name => ({ name, sourceName: official.name,
    variantReviewRequired: official.sheetVariantReviewRequired, url: url.href,
    detail: `${name} · ${official.name}${official.sheetVariantReviewRequired ? ' · Sheet identity needs review' : ''}` }));
}

export async function fetchEvolutionGraph(formId, offset = 0, fetcher = (...args) => fetch(...args)) {
  if (!integer(formId, 1, 512) || !integer(offset, 0, 511)) throw new Error('Unsupported graph position.');
  return validateEvolutionGraph(await request(`/api/evolution-graph?formId=${formId}&offset=${offset}&limit=${ROSTER_PAGE_SIZE}`, fetcher), formId, offset, ROSTER_PAGE_SIZE);
}

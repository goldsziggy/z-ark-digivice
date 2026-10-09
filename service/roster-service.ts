import { lstatSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import type { WorldDsAppearance } from './world-ds-assets.ts';

const MAX_FILE = 8 * 1024 * 1024;
const STAGES = new Set(['Fresh', 'In-Training', 'Rookie', 'Champion', 'Ultimate', 'Mega', 'Armor', 'No Level']);
const TYPES = new Set(['grove', 'tide', 'ember', 'neutral']);
const ROLES = new Set(['Balanced', 'Striker', 'Mystic', 'Bulwark', 'Warden', 'Preserved']);
const STATS = ['maxHp', 'attack', 'defense', 'magic', 'resistance'] as const;
type Stats = Record<typeof STATS[number], number>;
type Evolution = { parents: number[]; children: number[]; edges: { toFormId: number; requiredLevel: number; requiredBond: number }[]; status: 'progression' | 'terminal' | 'independent'; reason: string | null };
type Skills = { physical: string; heavy: string; magic: string };
type Form = { encounterRarity: 'common' | 'uncommon' | 'rare'; id: number; name: string; lineageSlug: string; stage: string | null; combatTier: string; type: string; role: string; parent: number; children: number[]; minLevel: number; minBond: number; skills: Skills; baseStats: Stats; growth: Stats; statModel: string; statsByLevel: Map<number, Stats>; obtainable: boolean; leafReason: string | null; artId: string | null; evolution: Evolution };
type Source = Record<string, unknown> & { entryKey: string; formId: number; displayName: string; source: Record<string, unknown>; official: Record<string, unknown> };
export class RosterError extends Error { status: number; code: string; constructor(status: number, code: string, message: string) { super(message); this.status = status; this.code = code; } }
const object = (value: unknown): value is Record<string, unknown> => value !== null && typeof value === 'object' && !Array.isArray(value);
const integer = (value: unknown, minimum: number, maximum: number): value is number => Number.isSafeInteger(value) && Number(value) >= minimum && Number(value) <= maximum;
const text = (value: unknown, maximum = 64): value is string => typeof value === 'string' && value.length >= 1 && Buffer.byteLength(value) <= maximum && !/[\u0000-\u001f\u007f]/.test(value);
function check(value: unknown): asserts value { if (!value) throw new RosterError(503, 'roster_unavailable', 'The bounded roster metadata is unavailable or invalid. Saved companions remain available.'); }
function file(path: string): unknown { const stat = lstatSync(path); check(stat.isFile() && !stat.isSymbolicLink() && stat.size > 0 && stat.size <= MAX_FILE); const bytes = readFileSync(path); check(bytes.length === stat.size); return JSON.parse(bytes.toString('utf8')); }
function stats(value: unknown, minimum = 1): Stats { check(object(value) && STATS.every(key => integer(value[key], minimum, 1000))); return Object.fromEntries(STATS.map(key => [key, value[key]])) as Stats; }
function evolution(value: unknown): Evolution {
  check(object(value) && Object.keys(value).length === 5 && Array.isArray(value.parents) && value.parents.length <= 512 && value.parents.every(id => integer(id, 1, 512)) && new Set(value.parents).size === value.parents.length &&
    Array.isArray(value.children) && value.children.length <= 2 && value.children.every(id => integer(id, 1, 512)) && new Set(value.children).size === value.children.length &&
    Array.isArray(value.edges) && value.edges.length === value.children.length && value.edges.every((edge, index) => object(edge) && Object.keys(edge).length === 3 && edge.toFormId === (value.children as unknown[])[index] && integer(edge.requiredLevel, 1, 20) && integer(edge.requiredBond, 0, 200)) &&
    ['progression', 'terminal', 'independent'].includes(String(value.status)) && (value.reason === null || text(value.reason, 512)));
  check(value.children.length ? value.status === 'progression' && value.reason === null : ['terminal', 'independent'].includes(String(value.status)) && typeof value.reason === 'string');
  return { parents: [...value.parents] as number[], children: [...value.children] as number[], edges: value.edges.map(edge => ({ toFormId: Number(edge.toFormId), requiredLevel: Number(edge.requiredLevel), requiredBond: Number(edge.requiredBond) })), status: value.status as Evolution['status'], reason: value.reason as string | null };
}
function nullableText(value: unknown, maximum = 256): string | null { return text(value, maximum) ? value : null; }
function url(value: unknown): string | null { if (typeof value !== 'string' || value.length > 512) return null; try { const parsed = new URL(value); return parsed.protocol === 'https:' && !parsed.username && !parsed.password && ['digimon.net', 'www.spriters-resource.com'].includes(parsed.hostname) ? value : null; } catch { return null; } }
function parameters(query: URLSearchParams, allowed: string[]): void { const keys = [...query.keys()]; if (keys.some(key => !allowed.includes(key)) || new Set(keys).size !== keys.length) throw new RosterError(400, 'invalid_roster_query', 'Supply each supported roster parameter at most once.'); }
function numberParameter(query: URLSearchParams, key: string, fallback: number, minimum: number, maximum: number): number { const raw = query.get(key); if (raw === null) return fallback; if (!/^(0|[1-9][0-9]*)$/.test(raw) || !integer(Number(raw), minimum, maximum)) throw new RosterError(400, 'invalid_roster_query', `Invalid ${key} parameter.`); return Number(raw); }

/** One bounded host metadata load. Never downloads art or accepts arbitrary file paths. */
export function createRosterService({ rootDir, describeArt }: { rootDir: string; describeArt?: (formId: number) => WorldDsAppearance }) {
  const native = file(join(rootDir, 'data/world-ds-runtime.json'));
  const catalog = file(join(rootDir, 'data/world-ds-catalog.json'));
  check(object(native) && native.formatVersion === 1 && native.catalogVersion === 6 && native.rulesVersion === 10 && Array.isArray(native.forms) && native.forms.length >= 66 && native.forms.length <= 512);
  check(object(catalog) && catalog.formatVersion === 1 && catalog.catalogId === 'digimon-world-ds' && catalog.catalogRevision === 6 && Array.isArray(catalog.entries) && catalog.entries.length > 0 && catalog.entries.length <= 512 && catalog.sourceEntryCount === catalog.entries.length);
  const forms = new Map<number, Form>();
  for (const raw of native.forms) {
    check(object(raw) && integer(raw.formId, 1, 512) && !forms.has(raw.formId) && text(raw.name) && typeof raw.lineageSlug === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(raw.lineageSlug) && raw.lineageSlug.length <= 63 && (raw.stage === null || text(raw.stage)) && text(raw.combatTier) && TYPES.has(String(raw.type)) && integer(raw.parent, 0, 512) && Array.isArray(raw.children) && raw.children.length <= 2 && raw.children.every(id => integer(id, 1, 512)) && new Set(raw.children).size === raw.children.length && integer(raw.minLevel, 1, 20) && integer(raw.minBond, 0, 200) && object(raw.skills) && ['physical', 'heavy', 'magic'].every(key => text((raw.skills as Record<string, unknown>)[key])) && ['preserved-v4', 'linear-v1', 'original-ultimate-v9'].includes(String(raw.statModel)) && Array.isArray(raw.statsByLevel) && raw.statsByLevel.length <= 20 && typeof raw.obtainable === 'boolean' && ['common', 'uncommon', 'rare'].includes(String(raw.encounterRarity)));
    const levels = new Map<number, Stats>();
    for (const row of raw.statsByLevel) { check(object(row) && integer(row.level, raw.minLevel, 20) && !levels.has(row.level)); levels.set(row.level, stats(row)); }
    check(levels.size === 21 - raw.minLevel);
    forms.set(raw.formId, { encounterRarity: raw.encounterRarity as Form['encounterRarity'], id: raw.formId, name: raw.name, lineageSlug: raw.lineageSlug, stage: raw.stage as string | null, combatTier: raw.combatTier, type: String(raw.type), role: raw.statModel === 'original-ultimate-v9' ? 'Original' : raw.preserved ? 'Preserved' : String(raw.role ?? ''), parent: raw.parent, children: raw.children as number[], minLevel: raw.minLevel, minBond: raw.minBond, skills: { physical: String(raw.skills.physical), heavy: String(raw.skills.heavy), magic: String(raw.skills.magic) }, baseStats: stats(raw.baseStats), growth: stats(raw.growth, 0), statModel: String(raw.statModel), statsByLevel: levels, obtainable: raw.obtainable, leafReason: nullableText(raw.leafReason, 512), artId: nullableText(raw.artId), evolution: evolution(raw.evolution) });
  }
  for (const form of forms.values()) {
    check((form.parent === 0 || forms.has(form.parent)) && form.children.every(id => forms.has(id)));
    check(form.evolution.parents.every(id => id !== form.id && forms.get(id)?.evolution.children.includes(form.id)) && form.evolution.edges.every(edge => {
      const child = forms.get(edge.toFormId); return !!child && child.id !== form.id && child.evolution.parents.includes(form.id) && edge.requiredLevel >= child.minLevel && edge.requiredBond >= child.minBond;
    }));
  }
  const reviewedSkillForms = new Set<number>(), reviewedSkillSlots = new Set<string>();
  check(Array.isArray(catalog.battleSkillBindings) && catalog.battleSkillBindings.length <= 512 * 3);
  for (const binding of catalog.battleSkillBindings) {
    check(object(binding) && integer(binding.formId, 1, 512) && forms.has(binding.formId) && ['physical', 'heavy', 'magic'].includes(String(binding.category)) && text(binding.name, 32) && object(binding.source) && url(binding.source.url)?.startsWith('https://digimon.net/reference_en/') && binding.source.moveName === binding.name);
    const category = binding.category as keyof Skills, key = `${binding.formId}:${category}`;
    check(!reviewedSkillSlots.has(key) && forms.get(binding.formId)!.skills[category] === binding.name);
    reviewedSkillSlots.add(key); reviewedSkillForms.add(binding.formId);
  }
  const sources = new Map<number, Source>(); const keys = new Set<string>();
  for (const raw of catalog.entries) {
    check(object(raw) && integer(raw.formId, 1, 512) && forms.has(raw.formId) && !sources.has(raw.formId) && typeof raw.entryKey === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(raw.entryKey) && raw.entryKey.length <= 64 && !keys.has(raw.entryKey) && text(raw.displayName) && object(raw.source) && STAGES.has(String(raw.source.sourceStage)) && object(raw.official) && ROLES.has(String(raw.role)));
    sources.set(raw.formId, raw as Source); keys.add(raw.entryKey);
    const form = forms.get(raw.formId)!; if (form.role !== 'Preserved' && form.role !== 'Original') form.role = String(raw.role);
  }
  const all = [...sources.values()].filter(row => row.formId >= 11).sort((a, b) => a.displayName.toLowerCase().localeCompare(b.displayName.toLowerCase()) || a.formId - b.formId);
  const lineageNames = new Set([...forms.values()].filter(form => form.id >= 11).map(form => form.lineageSlug));
  function appearance(id: number): WorldDsAppearance { try { return describeArt?.(id) ?? { status: 'unavailable', artId: `ds-form-${id}` }; } catch { return { status: 'unavailable', artId: `ds-form-${id}` }; } }
  function combat(form: Form, level: number) { const value = form.statsByLevel.get(level); if (!value) throw new RosterError(400, 'invalid_roster_level', 'Choose a legal level for this form.'); return { ...value, type: form.type, skills: { ...form.skills } }; }
  function briefForm(form: Form, source?: Source) { return { entryKey: source?.entryKey ?? null, formId: form.id, name: form.name, sourceName: source?.displayName ?? form.name, stage: source?.source.sourceStage ?? form.stage, combatTier: form.combatTier, encounterRarity: form.encounterRarity, role: form.role, type: form.type, previewLevel: form.minLevel, combat: combat(form, form.minLevel), art: appearance(form.id) }; }
  return {
    hasLineage(species: string) { return lineageNames.has(species); },
    matchesProfile(profile: { species: string; formId: number; name: string; level: number }) { const form = forms.get(profile.formId); return !!form && form.lineageSlug === profile.species && form.name === profile.name && form.statsByLevel.has(profile.level); },
    list(query: URLSearchParams) {
      parameters(query, ['offset', 'limit', 'q', 'prefix', 'stage', 'ids']);
      const offset = numberParameter(query, 'offset', 0, 0, 512), limit = numberParameter(query, 'limit', 8, 1, 16);
      if (query.has('ids')) {
        const raw = query.get('ids')!;
        if (raw.length > 63 || !/^[1-9][0-9]{0,2}(?:,[1-9][0-9]{0,2}){0,15}$/.test(raw) || ['q', 'prefix', 'stage'].some(key => query.has(key))) throw new RosterError(400, 'invalid_roster_query', 'Supply at most sixteen form IDs without search filters.');
        const ids = raw.split(',').map(Number);
        if (new Set(ids).size !== ids.length || ids.some(id => id < 11 || !forms.has(id))) throw new RosterError(400, 'invalid_roster_query', 'Supply unique known native form IDs.');
        return { formatVersion: 1, catalogVersion: 6, rulesVersion: 15, total: ids.length, offset, limit, entries: ids.slice(offset, offset + limit).map(id => briefForm(forms.get(id)!, sources.get(id))) };
      }
      const q = query.get('q') ?? '', prefix = query.get('prefix') ?? '', stage = query.get('stage') ?? '';
      if (Buffer.byteLength(q) > 64 || /[\u0000-\u001f\u007f]/.test(q) || prefix && !/^[a-z]$/.test(prefix) || stage && !STAGES.has(stage)) throw new RosterError(400, 'invalid_roster_query', 'Use a bounded search, lowercase first letter, and supported source stage.');
      const search = q.toLowerCase();
      const selected = all.filter(row => (!stage || row.source.sourceStage === stage) && (!prefix || row.displayName.toLowerCase().startsWith(prefix)) && (!search || [row.displayName, forms.get(row.formId)!.name, row.official.canonicalName, row.entryKey].some(name => typeof name === 'string' && name.toLowerCase().includes(search))));
      return { formatVersion: 1, catalogVersion: 6, rulesVersion: 15, total: selected.length, offset, limit, entries: selected.slice(offset, offset + limit).map(row => briefForm(forms.get(row.formId)!, row)) };
    },
    detail(formId: number, query: URLSearchParams) {
      parameters(query, ['level']); const form = forms.get(formId); if (formId < 11 || !form) throw new RosterError(404, 'roster_form_not_found', 'No such catalog form.');
      const level = numberParameter(query, 'level', form.minLevel, 1, 20), row = sources.get(formId);
      const official = row?.official;
      const moves = official ? Array.isArray(official.officialSpecialMoves) ? official.officialSpecialMoves : typeof official.signatureSkillLabel === 'string' ? [official.signatureSkillLabel] : [] : [];
      return { formatVersion: 1, catalogVersion: 6, rulesVersion: 15, form: { formId, entryKey: row?.entryKey ?? null, name: form.name, lineage: form.lineageSlug, stage: row?.source.sourceStage ?? form.stage, combatTier: form.combatTier, encounterRarity: form.encounterRarity, role: form.role || 'Preserved', type: form.type, requiredLevel: form.minLevel, requiredBond: form.minBond, previewLevel: level, combat: combat(form, level), baseStats: { ...form.baseStats }, growth: { ...form.growth }, statModel: form.statModel, art: appearance(formId), parentId: form.parent, children: [...form.children], evolution: structuredClone(form.evolution), obtainable: form.obtainable, leafReason: form.leafReason, skillLabelOrigin: reviewedSkillForms.has(formId) ? 'mixed-reviewed-official-and-authored' : row?.skillLabelOrigin ?? 'preserved-prototype', canonicalEvolutionClaim: false,
        source: row ? { game: nullableText(row.source.sourceGame), listedName: nullableText(row.source.listedName), stage: nullableText(row.source.sourceStage), url: url(row.source.listSourceUrl), sheetUrl: url(row.source.sheetUrl), uploader: nullableText(row.source.siteUploader), creatorCredit: nullableText(row.source.sheetCreatorCredit) } : null,
        official: official ? { name: nullableText(official.canonicalName), stage: nullableText(official.canonicalStage), type: nullableText(official.officialType ?? official.officialSpeciesType), attribute: official.officialAttribute === '' ? '' : nullableText(official.officialAttribute), url: url(official.officialReferenceUrl), specialMoves: moves.filter(move => text(move)).slice(0, 8), mappingStatus: nullableText(official.mappingStatus ?? official.canonicalStatus), sheetVariantReviewRequired: official.sheetVariantReviewRequired === true || !['observed-name-match', 'official-romanized-name-match', undefined].includes(official.mappingStatus as string | undefined), notes: Array.isArray(official.notes) ? official.notes.filter(note => text(note, 512)).slice(0, 8) : [] } : null,
      } };
    },
  };
}

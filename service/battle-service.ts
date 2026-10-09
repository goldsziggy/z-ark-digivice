import { parseAutoTrace, type AutoTrace } from './battle-trace.ts';
import { createHash, randomBytes } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { closeSync, existsSync, fsyncSync, lstatSync, mkdirSync, openSync, readFileSync, renameSync, unlinkSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export type BattleProfile = { id: number; species: string; name: string; level: number; formId?: number };
export type BattleAction = { type: 'physical' | 'heavy' | 'magic' | 'brace' | 'counter' | 'ward' | 'card' | 'retreat'; value: number };
export type BattleMode = 'tactical' | 'auto';
export type BattleRequest = { rulesVersion: 2 | 3 | 4 | 5 | 6 | 7; expectedRevision: number; requestId: string; mode?: BattleMode };
export type BattleActRequest = BattleRequest & { action: BattleAction };
type Combat = { maxHp: number; attack: number; defense: number; magic: number; resistance: number; type: string; skills: { physical: string; heavy: string; magic: string } };
type PublicCoreState = {
  schemaVersion: 2 | 3 | 4 | 5 | 6 | 7; rulesVersion: 2 | 3 | 4 | 5 | 6 | 7; sequence: number;
  phase: 'attack' | 'defend' | 'finished'; status: 'active' | 'won' | 'lost' | 'draw' | 'retreated';
  playerHp: number; enemyHp: number; playerLevel: number; enemyLevel: number; exchanges: number; maxExchanges?: 40;
  playerSpecies: BattleProfile['species']; enemySpecies: BattleProfile['species']; playerCombat: Combat; enemyCombat: Combat;
  playerFormId?: number; enemyFormId?: number; playerFormName?: string; enemyFormName?: string;
  cardUsed: boolean; attackBoost: number; shield: number; enemyHint: string[]; lastEnemyMoves: string[];
  lastTurn: null | { phase: string; playerChoice: string; enemyChoice: string; playerDamage: number; enemyDamage: number; reflected: boolean };
};
export type BattleResponse = { revision: number; mode: BattleMode; autoTrace: AutoTrace | null; battle: null | (PublicCoreState & { companion: BattleProfile; enemy: { species: BattleProfile['species']; name: string } }) };
type OldReceipt = { requestId: string; bodyHash: string; revision: number; snapshotBase64: string; companion: BattleProfile };
type ModeRecord = { mode: BattleMode; initialSnapshotBase64: string | null };
type Receipt = OldReceipt & ModeRecord;
type V2Device = { deviceId: string; revision: number; snapshotBase64: string; companion: BattleProfile; receipts: OldReceipt[]; legacyRequestIds: string[] };
type DeviceBattle = Omit<V2Device, 'receipts'> & ModeRecord & { receipts: Receipt[] };
type OldStore = { formatVersion: 1; practiceSchemaVersion: 1; practiceRulesVersion: 1; generation: number; devices: Omit<V2Device, 'legacyRequestIds'>[] };
type V2Store = { formatVersion: 2; practiceSchemaVersion: 2; practiceRulesVersion: 2; generation: number; devices: V2Device[] };
type V3Store = { formatVersion: 3; practiceSchemaVersion: 2; practiceRulesVersion: 2; generation: number; devices: DeviceBattle[] };
type V4Store = { formatVersion: 4; practiceSchemaVersion: 3; practiceRulesVersion: 3; generation: number; devices: DeviceBattle[] };
type V5Store = { formatVersion: 5; practiceSchemaVersion: 4; practiceRulesVersion: 4; generation: number; devices: DeviceBattle[] };
type V6Store = { formatVersion: 6; practiceSchemaVersion: 5; practiceRulesVersion: 5; generation: number; devices: DeviceBattle[] };
type V7Store = { formatVersion: 7; practiceSchemaVersion: 6; practiceRulesVersion: 6; generation: number; devices: DeviceBattle[] };
type Store = { formatVersion: 8; practiceSchemaVersion: 7; practiceRulesVersion: 7; generation: number; devices: DeviceBattle[] };
type StoredData = OldStore | V2Store | V3Store | V4Store | V5Store | V6Store | V7Store | Store;
export type BattleOptions = { rootDir?: string; dataDir: string; corePath?: string; seed?: () => number; profileValidator?: (profile: BattleProfile) => boolean };
export class BattleError extends Error {
  status: number;
  code: string;
  constructor(status: number, code: string, message: string) { super(message); this.name = 'BattleError'; this.status = status; this.code = code; }
}

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const MAX_STORE = 1024 * 1024;
const MAX_DEVICES = 8;
const MAX_RECEIPTS = 32;
const MAX_REVISION = 0xffffffff;
const PROFILE_SPECIES = new Set(['mote', 'flicker', 'rill', 'cinder', 'impmon', 'agumon', 'gabumon', 'patamon', 'tentomon', 'palmon', 'gomamon', 'renamon']);
const CHOICES = new Set(['physical', 'heavy', 'magic', 'brace', 'counter', 'ward']);
const ACTIONS = new Set([...CHOICES, 'card', 'retreat']);
function fail(status: number, code: string, message: string): never { throw new BattleError(status, code, message); }
function object(value: unknown): value is Record<string, unknown> { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function exact(value: Record<string, unknown>, keys: string[]): boolean { return Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key)); }
function integer(value: unknown, min = 0, max = MAX_REVISION): value is number { return Number.isSafeInteger(value) && Number(value) >= min && Number(value) <= max; }
function hash(value: string): string { return createHash('sha256').update(value).digest('hex'); }
function validId(value: unknown): value is string { return typeof value === 'string' && /^dv_[a-f0-9]{24}$/.test(value); }
function validRequestId(value: unknown): value is string { return typeof value === 'string' && /^[A-Za-z0-9_-]{16,64}$/.test(value); }
function validProfile(value: unknown): value is BattleProfile {
  return object(value) && exact(value, ['id', 'species', 'name', 'level', ...(Object.hasOwn(value, 'formId') ? ['formId'] : [])]) && integer(value.id, 1, 0xfffffffe) &&
    typeof value.species === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(value.species) && value.species.length <= 63 && typeof value.name === 'string' &&
    value.name.length > 0 && value.name.length <= 64 && !/[\u0000-\u001f\u007f]/.test(value.name) && integer(value.level, 1, Object.hasOwn(value, 'formId') ? 20 : 3) && (!Object.hasOwn(value, 'formId') || integer(value.formId, 1, 512));
}
function profile(value: BattleProfile): BattleProfile { return { id: value.id, species: value.species, name: value.name, level: value.level, ...(value.formId === undefined ? {} : { formId: value.formId }) }; }
function snapshotVersion(value: string): number { return Buffer.from(value, 'base64').readUInt32LE(8); }
function validSnapshot(value: unknown, version?: number): value is string {
  if (typeof value !== 'string' || ![136, 152, 160].includes(value.length)) return false;
  const bytes = Buffer.from(value, 'base64');
  if (bytes.toString('base64') !== value || bytes.toString('ascii', 0, 4) !== 'DGBP') return false;
  const rules = bytes.readUInt32LE(8), format = bytes.readUInt16LE(4);
  return (version === undefined ? rules === 2 || rules === 3 || rules === 4 || rules === 5 || rules === 6 || rules === 7 : rules === version) && format === rules && bytes.length === (rules === 1 ? 100 : rules === 2 ? 112 : 120);
}
function supportedVersion(value: unknown): boolean { return object(value) && ((value.formatVersion === 1 && value.practiceSchemaVersion === 1 && value.practiceRulesVersion === 1) || ([2, 3].includes(Number(value.formatVersion)) && value.practiceSchemaVersion === 2 && value.practiceRulesVersion === 2) || (value.formatVersion === 4 && value.practiceSchemaVersion === 3 && value.practiceRulesVersion === 3) || (value.formatVersion === 5 && value.practiceSchemaVersion === 4 && value.practiceRulesVersion === 4) || (value.formatVersion === 6 && value.practiceSchemaVersion === 5 && value.practiceRulesVersion === 5) || (value.formatVersion === 7 && value.practiceSchemaVersion === 6 && value.practiceRulesVersion === 6) || (value.formatVersion === 8 && value.practiceSchemaVersion === 7 && value.practiceRulesVersion === 7)); }
function validModeRecord(value: Record<string, unknown>): boolean { return value.mode === 'tactical' ? value.initialSnapshotBase64 === null : value.mode === 'auto' && validSnapshot(value.initialSnapshotBase64) && typeof value.snapshotBase64 === 'string' && snapshotVersion(value.initialSnapshotBase64) === snapshotVersion(value.snapshotBase64); }
function validateRequest(value: unknown, action: boolean): asserts value is BattleRequest | BattleActRequest {
  if (object(value) && value.rulesVersion !== 2 && value.rulesVersion !== 3 && value.rulesVersion !== 4 && value.rulesVersion !== 5 && value.rulesVersion !== 6 && value.rulesVersion !== 7) fail(409, 'battle_migration_required', 'This pending command uses older or unsupported practice rules. Preserve it and reconcile; do not relabel it.');
  if (!object(value) || !exact(value, action ? ['rulesVersion', 'expectedRevision', 'requestId', 'action'] : ['rulesVersion', 'expectedRevision', 'requestId', ...(Object.hasOwn(value, 'mode') ? ['mode'] : [])]) || (!action && Object.hasOwn(value, 'mode') && value.mode !== 'tactical' && value.mode !== 'auto') || !integer(value.expectedRevision) || !validRequestId(value.requestId)) {
    fail(400, 'invalid_battle_request', 'Supply the current battle rulesVersion, expectedRevision and a 16–64 character requestId, with an action only for battle actions.');
  }
  if (action) {
    const choice = value.action;
    if (!object(choice) || !exact(choice, ['type', 'value']) || typeof choice.type !== 'string' || !ACTIONS.has(choice.type) ||
      !(choice.type === 'card' ? choice.value === 1 || choice.value === 2 : choice.value === 0)) {
      fail(422, 'invalid_battle_action', 'Supply a supported action with value 0, or card value 1 or 2.');
    }
  }
}
function combat(value: unknown): Combat {
  if (!object(value) || !exact(value, ['maxHp', 'attack', 'defense', 'magic', 'resistance', 'type', 'skills']) ||
    !['maxHp', 'attack', 'defense', 'magic', 'resistance'].every(key => integer(value[key], 1, 1000)) ||
    !['grove', 'tide', 'ember', 'neutral'].includes(String(value.type)) || !object(value.skills) || !exact(value.skills, ['physical', 'heavy', 'magic']) ||
    !Object.values(value.skills).every(name => typeof name === 'string' && name.length > 0 && name.length <= 64)) fail(503, 'battle_core_output_invalid', 'The native combat profile is invalid.');
  return { maxHp: Number(value.maxHp), attack: Number(value.attack), defense: Number(value.defense), magic: Number(value.magic), resistance: Number(value.resistance), type: String(value.type), skills: { physical: String(value.skills.physical), heavy: String(value.skills.heavy), magic: String(value.skills.magic) } };
}

/** Explicit projection is the only path from the core to clients; snapshots and RNG never enter responses. */
function publicState(value: unknown): PublicCoreState {
  const bad = () => fail(503, 'battle_core_output_invalid', 'The practice battle core returned unsupported public state.');
  if (!object(value) || (value.rulesVersion !== 2 && value.rulesVersion !== 3 && value.rulesVersion !== 4 && value.rulesVersion !== 5 && value.rulesVersion !== 6 && value.rulesVersion !== 7) || value.schemaVersion !== value.rulesVersion || !integer(value.sequence, 0, value.rulesVersion >= 5 ? 42 : 32) ||
    !['attack', 'defend', 'finished'].includes(String(value.phase)) || !['active', 'won', 'lost', 'draw', 'retreated'].includes(String(value.status)) ||
    !integer(value.playerHp, 0, 1000) || !integer(value.enemyHp, 0, 1000) || !integer(value.playerLevel, 1, value.rulesVersion === 2 ? 3 : 20) || !integer(value.enemyLevel, 1, value.rulesVersion === 2 ? 3 : 20) ||
    !(value.rulesVersion >= 4 ? typeof value.playerSpecies === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(value.playerSpecies) && value.playerSpecies.length <= 63 : PROFILE_SPECIES.has(String(value.playerSpecies))) || !(value.rulesVersion >= 4 ? typeof value.enemySpecies === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(value.enemySpecies) && value.enemySpecies.length <= 63 : PROFILE_SPECIES.has(String(value.enemySpecies))) ||
    !integer(value.exchanges, 0, value.rulesVersion >= 5 ? 40 : 30) || (value.rulesVersion >= 5 ? value.maxExchanges !== 40 : Object.hasOwn(value, 'maxExchanges')) || typeof value.cardUsed !== 'boolean' || !integer(value.attackBoost, 0, 5) || !integer(value.shield, 0, 12) ||
    !Array.isArray(value.enemyHint) || value.enemyHint.length > 2 || !value.enemyHint.every(item => typeof item === 'string' && CHOICES.has(item)) ||
    !Array.isArray(value.lastEnemyMoves) || value.lastEnemyMoves.length > 2 || !value.lastEnemyMoves.every(item => typeof item === 'string' && CHOICES.has(item))) return bad();
  if (value.rulesVersion >= 3 && (!integer(value.playerFormId, 1, value.rulesVersion === 3 ? 66 : 512) || !integer(value.enemyFormId, 1, value.rulesVersion === 3 ? 66 : 512) || ![value.playerFormName, value.enemyFormName].every(name => typeof name === 'string' && name.length > 0 && name.length <= 64 && !/[\u0000-\u001f\u007f]/.test(name)))) return bad();
  const playerCombat = combat(value.playerCombat), enemyCombat = combat(value.enemyCombat);
  if (value.playerHp > playerCombat.maxHp || value.enemyHp > enemyCombat.maxHp) return bad();
  let lastTurn: PublicCoreState['lastTurn'] = null;
  if (value.lastTurn !== null) {
    const turn = value.lastTurn;
    if (!object(turn) || !['attack', 'defend'].includes(String(turn.phase)) || typeof turn.playerChoice !== 'string' || !CHOICES.has(turn.playerChoice) ||
      typeof turn.enemyChoice !== 'string' || !CHOICES.has(turn.enemyChoice) || !integer(turn.playerDamage, 0, 1000) || !integer(turn.enemyDamage, 0, 1000) || typeof turn.reflected !== 'boolean') return bad();
    lastTurn = { phase: String(turn.phase), playerChoice: turn.playerChoice, enemyChoice: turn.enemyChoice, playerDamage: turn.playerDamage, enemyDamage: turn.enemyDamage, reflected: turn.reflected };
  }
  return {
    schemaVersion: value.rulesVersion, rulesVersion: value.rulesVersion, sequence: value.sequence,
    phase: value.phase as PublicCoreState['phase'], status: value.status as PublicCoreState['status'],
    playerHp: value.playerHp, enemyHp: value.enemyHp, playerLevel: value.playerLevel, enemyLevel: value.enemyLevel, exchanges: value.exchanges,
    ...(value.rulesVersion >= 5 ? { maxExchanges: 40 as const } : {}),
    playerSpecies: value.playerSpecies as BattleProfile['species'], enemySpecies: value.enemySpecies as BattleProfile['species'], playerCombat, enemyCombat,
    ...(value.rulesVersion >= 3 ? { playerFormId: Number(value.playerFormId), enemyFormId: Number(value.enemyFormId), playerFormName: String(value.playerFormName), enemyFormName: String(value.enemyFormName) } : {}),
    cardUsed: value.cardUsed, attackBoost: value.attackBoost, shield: value.shield,
    enemyHint: [...value.enemyHint] as string[], lastEnemyMoves: [...value.lastEnemyMoves] as string[], lastTurn,
  };
}
function atomicWrite(path: string, data: string, onReplace?: () => void): void {
  const temporary = `${path}.${randomBytes(8).toString('hex')}.tmp`;
  let fd: number | undefined;
  try {
    fd = openSync(temporary, 'wx', 0o600); writeFileSync(fd, data, 'utf8'); fsyncSync(fd); closeSync(fd); fd = undefined;
    renameSync(temporary, path);
    onReplace?.();
    const directory = openSync(dirname(path), 'r'); try { fsyncSync(directory); } finally { closeSync(directory); }
  } finally { if (fd !== undefined) closeSync(fd); if (existsSync(temporary)) unlinkSync(temporary); }
}
function validateStore(value: unknown): asserts value is StoredData {
  if (!supportedVersion(value)) fail(503, 'battle_migration_required', 'The practice battle store version needs explicit migration.');
  if (!object(value) || !exact(value, ['formatVersion', 'practiceSchemaVersion', 'practiceRulesVersion', 'generation', 'devices']) || !integer(value.generation) || !Array.isArray(value.devices) || value.devices.length > MAX_DEVICES) fail(503, 'battle_store_invalid', 'The practice battle store is corrupt or exceeds its limits.');
  const ids = new Set<string>();
  let revisions = 0;
  for (const device of value.devices) {
    if (!object(device) || !exact(device, ['deviceId', 'revision', 'snapshotBase64', 'companion', 'receipts', ...(Number(value.formatVersion) >= 2 ? ['legacyRequestIds'] : []), ...(Number(value.formatVersion) >= 3 ? ['mode', 'initialSnapshotBase64'] : [])]) || !validId(device.deviceId) || ids.has(device.deviceId) || !integer(device.revision, 1) || !validSnapshot(device.snapshotBase64, value.formatVersion === 1 ? 1 : Number(value.formatVersion) >= 4 ? undefined : 2) || !validProfile(device.companion) || snapshotVersion(device.snapshotBase64) > Number(value.practiceRulesVersion) || (snapshotVersion(device.snapshotBase64) < 4 && (device.companion.id > 8 || !PROFILE_SPECIES.has(device.companion.species))) || !Array.isArray(device.receipts) || (value.formatVersion === 1 && device.receipts.length < 1) || device.receipts.length > MAX_RECEIPTS) fail(503, 'battle_store_invalid', 'The practice battle device record is invalid.');
    if (Number(value.formatVersion) >= 3 && !validModeRecord(device)) fail(503, 'battle_store_invalid', 'The saved practice battle mode is invalid.');
    if (Number(value.formatVersion) >= 4 && (snapshotVersion(device.snapshotBase64) >= 3 ? device.companion.formId === undefined : device.companion.formId !== undefined)) fail(503, 'battle_store_invalid', 'The practice profile does not match its saved rules.');
    ids.add(device.deviceId);
    revisions += device.revision;
    const requests = new Set<string>();
    if (Number(value.formatVersion) >= 2) {
      if (!Array.isArray(device.legacyRequestIds) || device.legacyRequestIds.length > MAX_RECEIPTS || !device.legacyRequestIds.every(validRequestId) || new Set(device.legacyRequestIds).size !== device.legacyRequestIds.length || (!device.receipts.length && !device.legacyRequestIds.length)) fail(503, 'battle_store_invalid', 'The archived practice request IDs are invalid.');
      for (const id of device.legacyRequestIds) requests.add(id);
    }
    for (let index = 0; index < device.receipts.length; index++) {
      const receipt = device.receipts[index];
      if (!object(receipt) || !exact(receipt, ['requestId', 'bodyHash', 'revision', 'snapshotBase64', 'companion', ...(Number(value.formatVersion) >= 3 ? ['mode', 'initialSnapshotBase64'] : [])]) || !validRequestId(receipt.requestId) || requests.has(receipt.requestId) || typeof receipt.bodyHash !== 'string' || !/^[a-f0-9]{64}$/.test(receipt.bodyHash) || receipt.revision !== device.revision - device.receipts.length + index + 1 || !validSnapshot(receipt.snapshotBase64, value.formatVersion === 1 ? 1 : Number(value.formatVersion) >= 4 ? undefined : 2) || !validProfile(receipt.companion) || snapshotVersion(receipt.snapshotBase64) > Number(value.practiceRulesVersion) || (snapshotVersion(receipt.snapshotBase64) < 4 && (receipt.companion.id > 8 || !PROFILE_SPECIES.has(receipt.companion.species)))) fail(503, 'battle_store_invalid', 'The practice battle retry history is invalid.');
      if (Number(value.formatVersion) >= 3 && !validModeRecord(receipt)) fail(503, 'battle_store_invalid', 'The saved practice receipt mode is invalid.');
      if (Number(value.formatVersion) >= 4 && (snapshotVersion(receipt.snapshotBase64) >= 3 ? receipt.companion.formId === undefined : receipt.companion.formId !== undefined)) fail(503, 'battle_store_invalid', 'The practice receipt profile does not match its saved rules.');
      requests.add(receipt.requestId);
    }
    const last = device.receipts.at(-1)!;
    if (last && (last.snapshotBase64 !== device.snapshotBase64 || JSON.stringify(profile(last.companion)) !== JSON.stringify(profile(device.companion)) || (Number(value.formatVersion) >= 3 && (last.mode !== device.mode || last.initialSnapshotBase64 !== device.initialSnapshotBase64)))) fail(503, 'battle_store_invalid', 'The current practice battle disagrees with its latest receipt.');
  }
  if (value.generation !== revisions) fail(503, 'battle_store_invalid', 'The practice battle generation disagrees with device revisions.');
}

/** The authenticated caller owns the exclusive dataDir writer lock and passes server-trusted profiles only. */
export function createBattleService(options: BattleOptions) {
  const corePath = resolve(options.corePath ?? join(options.rootDir ?? ROOT, 'build/digivice-battle'));
  const dataDir = resolve(options.dataDir);
  const primary = join(dataDir, 'battle-store.json'), mirror = join(dataDir, 'battle-store.backup.json');
  const seed = options.seed ?? (() => randomBytes(4).readUInt32LE());
  let closed = false;
  let needsRepair = false;
  let recoveredFromBackup = false;
  function core(args: string[], mutation = false): { state: PublicCoreState; snapshotBase64: string; mode: BattleMode; initialSnapshotBase64: string | null; autoTrace: AutoTrace | null } {
    const result = spawnSync(corePath, args, { input: '', encoding: 'utf8', timeout: 3000, maxBuffer: 32 * 1024, windowsHide: true });
    if (result.error || result.signal) fail(503, 'battle_core_unavailable', 'The practice battle core is unavailable. Build the project before starting a duel.');
    if (result.status !== 0) fail(mutation ? 422 : 503, mutation ? 'invalid_battle_action' : 'battle_snapshot_invalid', mutation ? 'This action is not available in the current practice battle phase.' : 'The practice battle snapshot did not pass core validation.');
    let resultJson: unknown;
    try { resultJson = JSON.parse(result.stdout); } catch { fail(503, 'battle_core_output_invalid', 'The practice battle core returned invalid JSON.'); }
    if (!object(resultJson) || !validSnapshot(resultJson.snapshotBase64)) fail(503, 'battle_core_output_invalid', 'The practice battle core returned an unsupported snapshot.');
    const state = publicState(resultJson.state);
    if (snapshotVersion(resultJson.snapshotBase64) !== state.rulesVersion) fail(503, 'battle_core_output_invalid', 'The native state and snapshot rules disagree.');
    if (resultJson.mode === 'auto') {
      if (!exact(resultJson, ['state', 'snapshotBase64', 'mode', 'initialSnapshotBase64', 'trace']) || !validSnapshot(resultJson.initialSnapshotBase64)) fail(503, 'battle_core_output_invalid', 'The native Auto replay boundary is invalid.');
      let autoTrace: AutoTrace | null;
      try { autoTrace = parseAutoTrace(resultJson.trace, 'practice', state.rulesVersion >= 5 ? 40 : 30); } catch { fail(503, 'battle_core_output_invalid', 'The native Auto trace is invalid.'); }
      if (!autoTrace || state.phase !== 'finished' || state.status === 'active' || autoTrace.endSequence !== state.sequence || autoTrace.outcome !== state.status) fail(503, 'battle_core_output_invalid', 'The native Auto trace and terminal result disagree.');
      return { state, snapshotBase64: resultJson.snapshotBase64, mode: 'auto', initialSnapshotBase64: resultJson.initialSnapshotBase64, autoTrace };
    }
    if (!exact(resultJson, ['state', 'snapshotBase64'])) fail(503, 'battle_core_output_invalid', 'The native Tactical response has unsupported fields.');
    return { state, snapshotBase64: resultJson.snapshotBase64, mode: 'tactical', initialSnapshotBase64: null, autoTrace: null };
  }
  function rival(seed: number, playerFormId: number, level: number): { formId: number; species: string; level: number } {
    const result = spawnSync(corePath, ['--practice-rival', String(seed), String(playerFormId), String(level)], { input: '', encoding: 'utf8', timeout: 3000, maxBuffer: 4096, windowsHide: true });
    if (result.error || result.signal || result.status !== 0) fail(503, 'battle_core_unavailable', 'The native practice rival selector is unavailable.');
    let value: unknown; try { value = JSON.parse(result.stdout); } catch { fail(503, 'battle_core_output_invalid', 'The native practice rival is invalid.'); }
    if (!object(value) || !exact(value, ['formId', 'species', 'level']) || !integer(value.formId, 1, 512) || value.level !== level || typeof value.species !== 'string' || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(value.species) || value.species.length > 63) fail(503, 'battle_core_output_invalid', 'The native practice rival is invalid.');
    return { formId: value.formId, species: value.species, level };
  }
  function view(record: Pick<V2Device, 'revision' | 'snapshotBase64' | 'companion'> & Partial<ModeRecord>): BattleResponse {
    const mode = record.mode ?? 'tactical';
    const output = core(mode === 'auto' ? ['--practice-auto', record.initialSnapshotBase64!] : ['--practice-read', record.snapshotBase64]);
    if (output.snapshotBase64 !== record.snapshotBase64 || output.mode !== mode || (mode === 'auto' && output.initialSnapshotBase64 !== record.initialSnapshotBase64) || output.state.playerLevel !== record.companion.level || output.state.playerSpecies !== record.companion.species || (output.state.rulesVersion >= 3 && (output.state.playerFormId !== record.companion.formId || output.state.playerFormName !== record.companion.name))) fail(503, 'battle_snapshot_invalid', 'The practice battle snapshot and companion profile disagree.');
    return { revision: record.revision, mode, autoTrace: output.autoTrace, battle: { ...output.state, companion: profile(record.companion), enemy: { species: output.state.enemySpecies, name: output.state.enemyFormName ?? 'Practice rival' } } };
  }
  function migrateSnapshot(record: Pick<DeviceBattle, 'snapshotBase64' | 'companion'>) {
    const result = core(['--practice-migrate-v1', record.snapshotBase64, record.companion.species, String(record.companion.level)]);
    if (result.state.playerLevel !== record.companion.level || result.state.playerSpecies !== record.companion.species) fail(503, 'battle_snapshot_invalid', 'The migrated practice profile disagrees with its saved companion.');
    return result;
  }
  function readStore(path: string): StoredData {
    const stat = lstatSync(path);
    if (!stat.isFile() || stat.size > MAX_STORE) fail(503, 'battle_store_invalid', 'The practice battle store is not a bounded regular file.');
    const parsed: unknown = JSON.parse(readFileSync(path, 'utf8'));
    validateStore(parsed);
    for (const device of parsed.devices) {
      if (parsed.formatVersion === 1) migrateSnapshot(device);
      else view(device); // Core checks CRC and private state invariants.
    }
    return parsed;
  }
  mkdirSync(dataDir, { recursive: true, mode: 0o700 });
  let store: Store = { formatVersion: 8, practiceSchemaVersion: 7, practiceRulesVersion: 7, generation: 0, devices: [] };
  let loaded: StoredData = store;
  const candidates: Array<{ path: string; store: StoredData }> = [];
  for (const path of [primary, mirror]) {
    if (!existsSync(path)) continue;
    try { candidates.push({ path, store: readStore(path) }); }
    catch (error) { if (error instanceof BattleError && ['battle_migration_required', 'battle_core_unavailable'].includes(error.code)) throw error; needsRepair = true; }
  }
  if (existsSync(primary) || existsSync(mirror)) {
    if (!candidates.length) fail(503, 'battle_store_invalid', 'Neither practice battle store copy can be restored. Pet saves are separate.');
    candidates.sort((left, right) => right.store.generation - left.store.generation || right.store.formatVersion - left.store.formatVersion);
    if (candidates.length === 2 && candidates[0].store.generation === candidates[1].store.generation && candidates[0].store.formatVersion === candidates[1].store.formatVersion && JSON.stringify(candidates[0].store) !== JSON.stringify(candidates[1].store)) fail(503, 'battle_store_invalid', 'Practice battle copies disagree at the same generation.');
    loaded = candidates[0].store;
    recoveredFromBackup = candidates[0].path === mirror;
    needsRepair ||= candidates.length !== 2 || candidates.some(candidate => candidate.store.generation !== loaded.generation || candidate.store.formatVersion !== loaded.formatVersion);
  }
  function persist(next: Store): void {
    const data = JSON.stringify(next);
    if (Buffer.byteLength(data) > MAX_STORE) fail(507, 'battle_storage_limit', 'The bounded practice battle store is full.');
    try {
      // Highest valid generation wins on restart. A first-copy commit may be
      // unacknowledged; keep its receipt in memory so retry never reapplies it.
      atomicWrite(mirror, data, () => { store = next; needsRepair = true; });
      atomicWrite(primary, data); needsRepair = false;
    } catch { fail(503, 'battle_storage_unavailable', 'The practice battle could not be saved to both durable copies. Retry the same request.'); }
  }
  if (loaded.formatVersion === 1) {
    const devices = loaded.devices.map(device => ({ ...device, mode: 'tactical' as const, initialSnapshotBase64: null, snapshotBase64: migrateSnapshot(device).snapshotBase64, receipts: [], legacyRequestIds: device.receipts.map(receipt => receipt.requestId) }));
    const migrated: Store = { formatVersion: 8, practiceSchemaVersion: 7, practiceRulesVersion: 7, generation: loaded.generation, devices };
    validateStore(migrated);
    const archive = join(dataDir, 'battle-store.rules-v1.json');
    if (!existsSync(archive)) atomicWrite(archive, JSON.stringify(loaded));
    persist(migrated);
  } else if (loaded.formatVersion === 2) {
    const devices: DeviceBattle[] = loaded.devices.map(device => ({ ...device, mode: 'tactical', initialSnapshotBase64: null,
      receipts: device.receipts.map(receipt => ({ ...receipt, mode: 'tactical', initialSnapshotBase64: null })) }));
    const migrated: Store = { ...loaded, formatVersion: 8, practiceSchemaVersion: 7, practiceRulesVersion: 7, devices };
    validateStore(migrated);
    const archive = join(dataDir, 'battle-store.format-v2.json');
    if (!existsSync(archive)) atomicWrite(archive, JSON.stringify(loaded));
    persist(migrated);
  } else if (loaded.formatVersion === 3 || loaded.formatVersion === 4 || loaded.formatVersion === 5 || loaded.formatVersion === 6 || loaded.formatVersion === 7) {
    // Rules are carried by each native snapshot. Preserve old duels/receipts byte
    // for byte; only new starts use current rules and a trusted current form.
    const migrated: Store = { ...loaded, formatVersion: 8, practiceSchemaVersion: 7, practiceRulesVersion: 7 };
    validateStore(migrated);
    const archive = join(dataDir, `battle-store.format-v${loaded.formatVersion}.json`);
    if (!existsSync(archive)) atomicWrite(archive, JSON.stringify(loaded));
    persist(migrated);
  } else {
    store = loaded;
    if (needsRepair) persist(store);
  }
  function ready(deviceId: string): void {
    if (closed) fail(503, 'battle_service_closed', 'The practice battle service is closed.');
    if (!validId(deviceId)) fail(400, 'invalid_battle_identity', 'An authenticated device identity is required.');
    if (needsRepair) persist(store);
  }
  function request(deviceId: string, input: unknown, action: boolean): { body: BattleRequest | BattleActRequest; current?: DeviceBattle; bodyHash: string; retry?: BattleResponse } {
    ready(deviceId); validateRequest(input, action);
    const choice = action ? (input as BattleActRequest).action : undefined;
    const bodyHash = hash(JSON.stringify({ rulesVersion: input.rulesVersion, operation: action ? 'act' : 'start', expectedRevision: input.expectedRevision, ...(choice ? { action: { type: choice.type, value: choice.value } } : Object.hasOwn(input, 'mode') ? { mode: input.mode } : {}) }));
    const current = store.devices.find(device => device.deviceId === deviceId);
    if (current?.legacyRequestIds.includes(input.requestId)) fail(409, 'legacy_request_requires_reconciliation', 'This request was committed under older practice rules and cannot be relabelled or replayed.');
    const previous = current?.receipts.find(receipt => receipt.requestId === input.requestId);
    if (previous) {
      if (previous.bodyHash !== bodyHash) fail(409, 'request_mismatch', 'This requestId was already used for different practice battle content.');
      return { body: input, current, bodyHash, retry: view(previous) };
    }
    if (input.expectedRevision !== (current?.revision ?? 0)) fail(409, 'revision_conflict', 'Fetch the current practice battle before sending a new action. Older retries outside the retained window cannot be reapplied.');
    if ((current?.revision ?? 0) === MAX_REVISION || store.generation === MAX_REVISION) fail(409, 'battle_revision_limit', 'The prototype practice battle revision limit requires migration.');
    return { body: input, current, bodyHash };
  }
  function commit(deviceId: string, current: DeviceBattle | undefined, input: BattleRequest, bodyHash: string, companion: BattleProfile, snapshotBase64: string, mode: BattleMode = 'tactical', initialSnapshotBase64: string | null = null): BattleResponse {
    const revision = (current?.revision ?? 0) + 1;
    const receipt: Receipt = { mode, initialSnapshotBase64, requestId: input.requestId, bodyHash, revision, snapshotBase64, companion: profile(companion) };
    const next: DeviceBattle = { mode, initialSnapshotBase64, deviceId, revision, snapshotBase64, companion: profile(companion), receipts: [...(current?.receipts ?? []), receipt].slice(-MAX_RECEIPTS), legacyRequestIds: current?.legacyRequestIds ?? [] };
    const response = view(next);
    persist({ ...store, generation: store.generation + 1, devices: current ? store.devices.map(device => device.deviceId === deviceId ? next : device) : [...store.devices, next] });
    return response;
  }
  return {
    get recoveredFromBackup() { return recoveredFromBackup; },
    get(deviceId: string): BattleResponse {
      ready(deviceId); const current = store.devices.find(device => device.deviceId === deviceId);
      return current ? view(current) : { revision: 0, battle: null, mode: 'tactical', autoTrace: null };
    },
    start(deviceId: string, input: unknown, trustedProfile: BattleProfile): BattleResponse {
      const { body, current, bodyHash, retry } = request(deviceId, input, false);
      if (retry) return retry;
      if (body.rulesVersion !== 7) fail(409, 'battle_migration_required', 'New practice starts require rulesVersion:7. Preserve old pending commands; do not relabel them.');
      if (!validProfile(trustedProfile) || trustedProfile.formId === undefined) fail(503, 'battle_profile_invalid', 'The saved companion profile is unsupported for practice.');
      if (trustedProfile.formId < 11) fail(409, 'legacy_partner', 'Choose a named partner before starting a new practice match. Saved matches remain available.');
      if (options.profileValidator) {
        let supported = false; try { supported = options.profileValidator(trustedProfile); } catch { /* Fail closed without exposing metadata paths. */ }
        if (!supported) fail(503, 'battle_profile_invalid', 'The saved companion does not match the current native roster metadata.');
      }
      if (current && view(current).battle?.status === 'active') fail(409, 'battle_active', 'Finish or retreat from the current practice battle before starting another.');
      if (!current && store.devices.length >= MAX_DEVICES) fail(409, 'battle_device_limit', 'Practice battles support eight paired devices.');
      const nextSeed = seed();
      if (!integer(nextSeed)) fail(503, 'battle_seed_invalid', 'The practice battle seed source is unavailable.');
      const enemy = rival(nextSeed, trustedProfile.formId, trustedProfile.level);
      const output = core(['--practice-start-forms', String(nextSeed), trustedProfile.species, String(trustedProfile.level), String(trustedProfile.formId), enemy.species, String(enemy.level), String(enemy.formId), body.mode ?? 'tactical']);
      if (output.state.rulesVersion !== body.rulesVersion || output.mode !== (body.mode ?? 'tactical')) fail(503, 'battle_core_output_invalid', 'The native battle mode does not match the confirmed request.');
      return commit(deviceId, current, body, bodyHash, trustedProfile, output.snapshotBase64, output.mode, output.initialSnapshotBase64);
    },
    act(deviceId: string, input: unknown): BattleResponse {
      const { body, current, bodyHash, retry } = request(deviceId, input, true);
      if (retry) return retry;
      if (!current) fail(409, 'battle_not_started', 'Start a practice battle before choosing an action.');
      if (body.rulesVersion !== snapshotVersion(current.snapshotBase64)) fail(409, 'battle_migration_required', 'This command does not match the saved duel rules. Fetch the current battle without relabelling a pending command.');
      if (current.mode === 'auto') fail(409, 'battle_auto_complete', 'This confirmed Auto battle is already resolved; manual actions cannot change it.');
      const action = (body as BattleActRequest).action;
      const output = core(['--practice-act', current.snapshotBase64, action.type, String(action.value)], true);
      if (output.state.rulesVersion !== body.rulesVersion) fail(503, 'battle_core_output_invalid', 'The native battle action changed its saved rules.');
      return commit(deviceId, current, body, bodyHash, current.companion, output.snapshotBase64);
    },
    close() { closed = true; },
  };
}
export type BattleService = ReturnType<typeof createBattleService>;

import { validCare, validLastCapture } from '../web/care-capture-state.js';
import { validWalkingState } from '../web/walking-state.js';
import { createRosterService, RosterError } from './roster-service.ts';
import { createWorldDsAssetService } from './world-ds-assets.ts';
import { parseAutoTrace, type AutoTrace } from './battle-trace.ts';
import { createAssetService } from './asset-service.ts';
import { createDeviceAssetService, DEVICE_PROFILE } from './device-assets.ts';
import { BattleError, createBattleService, type BattleProfile } from './battle-service.ts';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import { isIPv4 } from 'node:net';
import { createHash, randomBytes, randomInt, timingSafeEqual } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { closeSync, existsSync, fsyncSync, mkdirSync, openSync, readFileSync, renameSync, statSync, unlinkSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

type Event = { type: 'feed' | 'play' | 'rest' | 'walk' | 'card' | 'attack' | 'heavy' | 'magic' | 'capture' | 'flick' | 'ring-capture' | 'select' | 'hatch' | 'mode' | 'auto' | 'auto-fight' | 'auto-resume' | 'evolve' | 'release' | 'explore' | 'encounter-rate' | 'encounter-seed' | 'starter-offer-seed' | 'accrue-steps' | 'present-encounter' | 'resolve-test-encounter' | 'world-seed' | 'party-add' | 'party-remove' | 'toilet' | 'retreat' | 'care-minute' | 'evolve-member' | 'treat' | 'focus'; value: number };
type State = Record<string, unknown> & { schemaVersion: number; rulesVersion: number; sequence: number };
type Receipt = { batchId: string; bodyHash: string; revision: number; eventEnd: number };
type LegacyHistory = { events: Event[]; receipts: Receipt[]; snapshotBase64: string };
type OldDevice = { deviceId: string; tokenHash: string; seed: number; revision: number; events: Event[]; receipts: Receipt[] };
type V2Device = OldDevice & { legacy: LegacyHistory | null };
type HistoricalEvents = { rulesVersion: 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19; events: Event[]; receipts: Receipt[] };
type Baseline = { histories: HistoricalEvents[]; snapshotBase64: string; autoTrace?: AutoTrace };
type InitialMode = 'legacy' | 'onboarding';
type V3Device = OldDevice & { legacy: Baseline | null };
type Device = V3Device & { initialMode: InitialMode };
type OldStore = { formatVersion: 1; gameSchemaVersion: 2; rulesVersion: 1; devices: OldDevice[] };
type V2Store = { formatVersion: 2; gameSchemaVersion: 3; rulesVersion: 2; devices: V2Device[] };
type V3Store = { formatVersion: 3; gameSchemaVersion: 4; rulesVersion: 3; devices: V3Device[] };
type V4Store = { formatVersion: 4; gameSchemaVersion: 5; rulesVersion: 3; devices: Device[] };
type V5Store = { formatVersion: 5; gameSchemaVersion: 6; rulesVersion: 3; devices: Device[] };
type V6Store = { formatVersion: 6; gameSchemaVersion: 7; rulesVersion: 4; devices: Device[] };
type V7Store = { formatVersion: 7; gameSchemaVersion: 8; rulesVersion: 5; devices: Device[] };
type V8Store = { formatVersion: 8; gameSchemaVersion: 9; rulesVersion: 6; devices: Device[] };
type V9Store = { formatVersion: 9; gameSchemaVersion: 10; rulesVersion: 7; devices: Device[] };
type V10Store = { formatVersion: 10; gameSchemaVersion: 11; rulesVersion: 8; devices: Device[] };
type V11Store = { formatVersion: 11; gameSchemaVersion: 12; rulesVersion: 9; devices: Device[] };
type V12Store = { formatVersion: 12; gameSchemaVersion: 13; rulesVersion: 10; devices: Device[] };
type V13Store = { formatVersion: 13; gameSchemaVersion: 14; rulesVersion: 11; devices: Device[] };
type Schema15Store = { formatVersion: 14; gameSchemaVersion: 15; rulesVersion: 12; devices: Device[] };
type Schema16Store = { formatVersion: 14; gameSchemaVersion: 16; rulesVersion: 12; devices: Device[] };
type Schema17Store = { formatVersion: 15; gameSchemaVersion: 17; rulesVersion: 13; devices: Device[] };
type Schema18Store = { formatVersion: 15; gameSchemaVersion: 18; rulesVersion: 13; devices: Device[] };
type Schema19Store = { formatVersion: 15; gameSchemaVersion: 19; rulesVersion: 13; devices: Device[] };
type Schema20Store = { formatVersion: 15; gameSchemaVersion: 20; rulesVersion: 13; devices: Device[] };
type Schema21Store = { formatVersion: 16; gameSchemaVersion: 21; rulesVersion: 14; devices: Device[] };
type Schema22Store = { formatVersion: 17; gameSchemaVersion: 22; rulesVersion: 15; devices: Device[] };
type Schema23Store = { formatVersion: 18; gameSchemaVersion: 23; rulesVersion: 16; devices: Device[] };
type Schema24Store = { formatVersion: 19; gameSchemaVersion: 24; rulesVersion: 17; devices: Device[] };
type Schema25Store = { formatVersion: 20; gameSchemaVersion: 25; rulesVersion: 18; devices: Device[] };
type Store = { formatVersion: 21; gameSchemaVersion: 26 | 27; rulesVersion: 19; devices: Device[] };
type StoredData = OldStore | V2Store | V3Store | V4Store | V5Store | V6Store | V7Store | V8Store | V9Store | V10Store | V11Store | V12Store | V13Store | Schema15Store | Schema16Store | Schema17Store | Schema18Store | Schema19Store | Schema20Store | Schema21Store | Schema22Store | Schema23Store | Schema24Store | Schema25Store | Store;
export type GarageReadAdapter = {
  list(): Promise<unknown>;
  fetchPack(id: string, version: number): Promise<{ bytes: Uint8Array; sha256: string; contentType: string }>;
  fetchPersonal(id: string, version: number): Promise<{ packText: string; provenanceText: string }>;
  close?(): void;
};
export type LanOptions = { bindAddress: string; allowedHosts: string[]; allowedOrigins: string[] };
type AppOptions = { /** Explicit in-process test injection only; credentials always use Node crypto. */ seedSource?: () => number; includeTestFixtures?: boolean; rootDir?: string; dataDir?: string; corePath?: string; battleCorePath?: string; now?: () => number; garageAssets?: GarageReadAdapter; lan?: LanOptions };

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const MAX_BODY = 32 * 1024;
const MAX_EVENTS_PER_BATCH = 100;
const MAX_EVENTS = 10_000;
const MAX_DEVICES = 8;
const MAX_STORE = 16 * 1024 * 1024;
const STARTER_SPECIES = ['impmon', 'agumon', 'gabumon', 'patamon', 'tentomon', 'palmon', 'gomamon', 'renamon'];
const LINEAGES = [...STARTER_SPECIES];
const CODE_LIFETIME_MS = 5 * 60 * 1000;
const GARAGE_ORIGINAL_IDS = new Set(['starter-v2', 'tide-v1', 'ember-v1']);
const GARAGE_PERSONAL_ID = /^personal-[a-z0-9][a-z0-9-]{0,38}$/;
const GARAGE_MAX_PACK = 256 * 1024;
const GARAGE_MAX_PROVENANCE = 64 * 1024;
const EVENT_TYPES = new Set(['feed', 'play', 'rest', 'walk', 'card', 'attack', 'heavy', 'magic', 'capture', 'flick', 'ring-capture', 'select', 'hatch', 'mode', 'auto', 'auto-fight', 'auto-resume', 'evolve', 'release', 'explore', 'encounter-rate', 'encounter-seed', 'starter-offer-seed', 'accrue-steps', 'present-encounter', 'resolve-test-encounter', 'world-seed', 'party-add', 'party-remove', 'toilet', 'retreat', 'care-minute', 'evolve-member', 'treat', 'focus']);
const ASSETS = new Map([
  ['/', ['web/index.html', 'text/html; charset=utf-8']],
  ['/index.html', ['web/index.html', 'text/html; charset=utf-8']],
  ['/power-demo.html', ['web/power-demo.html', 'text/html; charset=utf-8']],
  ['/power-demo.js', ['web/power-demo.js', 'text/javascript; charset=utf-8']],
  ['/power-demo.css', ['web/power-demo.css', 'text/css; charset=utf-8']],
  ['/styles.css', ['web/styles.css', 'text/css; charset=utf-8']],
  ['/app.js', ['web/app.js', 'text/javascript; charset=utf-8']],
  ['/walking-state.js', ['web/walking-state.js', 'text/javascript; charset=utf-8']],
  ['/care-actions.js', ['web/care-actions.js', 'text/javascript; charset=utf-8']],
  ['/care-capture-state.js', ['web/care-capture-state.js', 'text/javascript; charset=utf-8']],
  ['/party.js', ['web/party.js', 'text/javascript; charset=utf-8']],
  ['/game-message.js', ['web/game-message.js', 'text/javascript; charset=utf-8']],
  ['/capture-gesture.js', ['web/capture-gesture.js', 'text/javascript; charset=utf-8']],
  ['/capture-trajectory.js', ['web/capture-trajectory.js', 'text/javascript; charset=utf-8']],
  ['/capture-ring.js', ['web/capture-ring.js', 'text/javascript; charset=utf-8']],
  ['/capture-ring-input.js', ['web/capture-ring-input.js', 'text/javascript; charset=utf-8']],
  ['/auto-battle.js', ['web/auto-battle.js', 'text/javascript; charset=utf-8']],
  ['/starter-onboarding.js', ['web/starter-onboarding.js', 'text/javascript; charset=utf-8']],
  ['/roster-client.js', ['web/roster-client.js', 'text/javascript; charset=utf-8']],
  ['/form-art.js', ['web/form-art.js', 'text/javascript; charset=utf-8']],
  ['/progression.js', ['web/progression.js', 'text/javascript; charset=utf-8']],
  ['/audio-engine.js', ['web/audio-engine.js', 'text/javascript; charset=utf-8']],
  ['/garage-library.js', ['web/garage-library.js', 'text/javascript; charset=utf-8']],
  ['/device-navigation.js', ['web/device-navigation.js', 'text/javascript; charset=utf-8']],
  ['/device-screen.js', ['web/device-screen.js', 'text/javascript; charset=utf-8']],
  ['/battle-client.js', ['web/battle-client.js', 'text/javascript; charset=utf-8']],
  ['/two-button-input.js', ['web/two-button-input.js', 'text/javascript; charset=utf-8']],
  ['/asset-cache.js', ['web/asset-cache.js', 'text/javascript; charset=utf-8']],
  ['/background-pack.js', ['web/background-pack.js', 'text/javascript; charset=utf-8']],
  ['/background-player.js', ['web/background-player.js', 'text/javascript; charset=utf-8']],
  ['/asset-store.js', ['web/asset-store.js', 'text/javascript; charset=utf-8']],
  ['/asset-public-key.js', ['web/asset-public-key.js', 'text/javascript; charset=utf-8']],
  ['/asset-library.js', ['web/asset-library.js', 'text/javascript; charset=utf-8']],
  ['/builtin-pack.js', ['web/builtin-pack.js', 'text/javascript; charset=utf-8']],
  ['/personal-art.js', ['web/personal-art.js', 'text/javascript; charset=utf-8']],
  ['/personal-pack.js', ['web/personal-pack.js', 'text/javascript; charset=utf-8']],

]);

class HttpError extends Error {
  status: number;
  code: string;
  constructor(status: number, code: string, message: string) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

function loopback(address: string): boolean { return ['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(address); }
function privateIPv4(address: string): boolean {
  if (!isIPv4(address)) return false;
  const [first, second] = address.split('.').map(Number);
  return first === 10 || (first === 172 && second >= 16 && second <= 31) || (first === 192 && second === 168);
}
function lanPolicy(value: LanOptions | undefined): LanOptions | undefined {
  if (value === undefined) return undefined;
  if (!object(value) || !keysExactly(value, ['bindAddress', 'allowedHosts', 'allowedOrigins']) || !privateIPv4(value.bindAddress) ||
    !Array.isArray(value.allowedHosts) || !value.allowedHosts.length || value.allowedHosts.length > 8 ||
    !Array.isArray(value.allowedOrigins) || !value.allowedOrigins.length || value.allowedOrigins.length > 8) throw new Error('LAN mode requires one explicit private IPv4 interface and bounded Host/Origin allowlists.');
  const hosts = new Set<string>();
  for (const host of value.allowedHosts) {
    if (typeof host !== 'string' || host.length > 128) throw new Error('Invalid LAN allowed Host.');
    let parsed: URL;
    try { parsed = new URL(`http://${host}`); } catch { throw new Error('Invalid LAN allowed Host.'); }
    if (parsed.host !== host || parsed.username || parsed.password || parsed.pathname !== '/' || parsed.search || parsed.hash ||
      ![value.bindAddress, '127.0.0.1', 'localhost'].includes(parsed.hostname) && !/^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.local$/.test(parsed.hostname)) throw new Error('LAN Hosts must name the bound address, loopback, or an explicit .local hostname.');
    hosts.add(host);
  }
  if (![...hosts].some(host => new URL(`http://${host}`).hostname === value.bindAddress)) throw new Error('LAN allowlist must include the bound interface address.');
  const origins = new Set<string>();
  for (const origin of value.allowedOrigins) {
    if (typeof origin !== 'string' || origin.length > 160) throw new Error('Invalid LAN allowed Origin.');
    let parsed: URL;
    try { parsed = new URL(origin); } catch { throw new Error('Invalid LAN allowed Origin.'); }
    if (parsed.protocol !== 'http:' || parsed.origin !== origin || !hosts.has(parsed.host)) throw new Error('LAN Origins must be exact HTTP origins from the Host allowlist.');
    origins.add(origin);
  }
  return { bindAddress: value.bindAddress, allowedHosts: [...hosts], allowedOrigins: [...origins] };
}

/** Explicit CLI opt-in only. No interface discovery, wildcard bind or environment changes. */
export function lanOptionsFromEnv(env: NodeJS.ProcessEnv, port: number): LanOptions | undefined {
  const bindAddress = env.DIGIVICE_LAN_BIND;
  if (!bindAddress) {
    if (env.DIGIVICE_LAN_HOSTS || env.DIGIVICE_LAN_ORIGINS) throw new Error('LAN Host/Origin settings require DIGIVICE_LAN_BIND.');
    return undefined;
  }
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('LAN mode requires an explicit valid port.');
  const authority = (host: string) => new URL(`http://${host}:${port}`).host;
  const allowedHosts = env.DIGIVICE_LAN_HOSTS?.split(',').map(value => value.trim()) ?? [bindAddress, '127.0.0.1', 'localhost'].map(authority);
  const allowedOrigins = env.DIGIVICE_LAN_ORIGINS?.split(',').map(value => value.trim()) ?? allowedHosts.map(host => `http://${host}`);
  return lanPolicy({ bindAddress, allowedHosts, allowedOrigins });
}

function sha256(value: string | Buffer): string {
  return createHash('sha256').update(value).digest('hex');
}
function object(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}
function keysExactly(value: Record<string, unknown>, keys: string[]): boolean {
  const actual = Object.keys(value).sort();
  return actual.length === keys.length && actual.every((key, index) => key === [...keys].sort()[index]);
}
function validEvents(input: unknown, maximum = MAX_EVENTS, rulesVersion = 19): input is Event[] {
  return Array.isArray(input) && input.length <= maximum && input.every((event) => {
    if (!object(event) || !keysExactly(event, ['type', 'value']) || typeof event.type !== 'string' || !EVENT_TYPES.has(event.type) || !Number.isSafeInteger(event.value)) return false;
    if (event.type === 'evolve-member') {
      const memberId = Math.floor(Number(event.value) / 65536);
      const formId = Number(event.value) % 65536;
      return rulesVersion >= 16 && memberId >= 1 && memberId <= 0xfffe && formId >= 1 && formId <= 512 && memberId * 65536 + formId === Number(event.value);
    }
    if (event.type === 'care-minute') return rulesVersion >= 16 && Number(event.value) >= 1 && Number(event.value) <= 0xffffffff;
    if (event.type === 'toilet' || event.type === 'retreat') return rulesVersion >= 16 && event.value === 0;
    if (event.type === 'treat') return rulesVersion >= 17 && event.value === 0;
    if (event.type === 'focus') return rulesVersion >= 18 && Number.isSafeInteger(event.value) && Number(event.value) >= 0 && Number(event.value) <= 2400;
    if (event.type === 'party-add' || event.type === 'party-remove') return rulesVersion >= 15 && Number(event.value) >= 1 && Number(event.value) <= 0xfffffffe;
    if (event.type === 'auto-fight' || event.type === 'auto-resume') return rulesVersion >= 13 && event.value === 0;
    if (event.type === 'resolve-test-encounter') return rulesVersion >= 13 && event.value === 0;
    if (event.type === 'accrue-steps') return rulesVersion >= 12 && Number(event.value) >= 1 && Number(event.value) <= 1000;
    if (event.type === 'present-encounter') return rulesVersion >= 12 && event.value === 0;
    if (event.type === 'world-seed') return rulesVersion >= 13 && Number(event.value) >= 1 && Number(event.value) <= 0xffffffff;
    if (event.type === 'starter-offer-seed') return rulesVersion >= 12 && Number(event.value) >= 1 && Number(event.value) <= 0xffffffff;
    if (event.type === 'encounter-seed') return rulesVersion >= 11 && Number(event.value) >= 1 && Number(event.value) <= 0xffffffff;
    if (event.type === 'explore') return rulesVersion >= 11 && Number(event.value) >= 1 && Number(event.value) <= 1000;
    if (event.type === 'encounter-rate') return rulesVersion >= 11 && Number(event.value) >= 0 && Number(event.value) <= 3;
    if (event.type === 'walk') return Number(event.value) >= 1 && Number(event.value) <= 1000;
    if (event.type === 'card') return event.value === 1 || event.value === 2;
    if (event.type === 'flick') return rulesVersion >= 10 && Number(event.value) >= 0 && Number(event.value) <= 82175;
    if (event.type === 'ring-capture') return rulesVersion >= 13 && Number(event.value) >= 0 && Number(event.value) < 2400;
    if (event.type === 'mode') return rulesVersion >= 3 && (event.value === 0 || event.value === 1);
    if (event.type === 'auto') return rulesVersion >= 3 && event.value === 0;
    if (event.type === 'hatch') return rulesVersion >= 3 && Number(event.value) >= 1 && Number(event.value) <= (rulesVersion >= 12 ? 11 : 8);
    if (event.type === 'evolve') return rulesVersion >= 4 && Number(event.value) >= 1 && Number(event.value) <= (rulesVersion === 4 ? 66 : 512);
    if (event.type === 'release') return rulesVersion >= 5 && Number(event.value) >= 1 && Number(event.value) <= 0xfffffffe;
    if (event.type === 'select') return rulesVersion >= 2 && Number(event.value) >= 1 && Number(event.value) <= (rulesVersion >= 5 ? 0xfffffffe : 8);
    if (event.type === 'heavy' || event.type === 'magic') return rulesVersion >= 3 && event.value === 0;
    return event.value === 0;
  });
}
function expeditionsSupported(value: unknown): boolean {
  if (!object(value) || !keysExactly(value, ['dungeonKeys', 'bossSigils', 'dungeonWins', 'bossSteps', 'dungeonClears', 'bossClears', 'maxDungeonKeys', 'scenarios']) || !Array.isArray(value.scenarios) || value.scenarios.length !== 6) return false;
  const bounded = (input: unknown, minimum: number, maximum: number) => Number.isSafeInteger(input) && Number(input) >= minimum && Number(input) <= maximum;
  if (value.maxDungeonKeys !== 3 || !bounded(value.dungeonKeys, 0, 3) || !bounded(value.bossSigils, 0, 1) || !bounded(value.dungeonWins, 0, 7) || !bounded(value.bossSteps, 0, 4999) || !bounded(value.dungeonClears, 0, 0xffffffff) || !bounded(value.bossClears, 0, 0xffffffff)) return false;
  const names = ['Grove Dungeon', 'Tide Dungeon', 'Ember Dungeon', 'Grove Boss', 'Tide Boss', 'Ember Boss'];
  return value.scenarios.every((scenario, index) => object(scenario) && keysExactly(scenario, ['name', 'kind', 'theme', 'floors', 'minPlayers', 'maxPlayers']) && scenario.name === names[index] && ['grove', 'tide', 'ember'][index % 3] === scenario.theme && (scenario.kind === 'dungeon' ? scenario.floors === 3 && scenario.minPlayers === 2 && scenario.maxPlayers === 2 : scenario.kind === 'boss' && scenario.floors === 1 && scenario.minPlayers === 1 && scenario.maxPlayers === 2));
}
function stateSupported(value: unknown): value is State {
  if (!object(value) || Buffer.byteLength(JSON.stringify(value)) >= 256 * 1024 || value.schemaVersion !== 27 || value.rulesVersion !== 19 || value.collectionCapacity !== 250 || !expeditionsSupported(value.expeditions) || !Number.isSafeInteger(value.sequence) || Number(value.sequence) < 0 || !Array.isArray(value.collection) || !['tactical', 'auto'].includes(String(value.battleMode)) || !object(value.onboarding) || !keysExactly(value.onboarding, ['completed', 'starterId', 'offerSeed', 'offers'])) return false;
  const bounded = (input: unknown, minimum: number, maximum: number) => Number.isSafeInteger(input) && Number(input) >= minimum && Number(input) <= maximum;
  if (![0, 1, 2, 3].includes(Number(value.autoCapture)) || typeof value.autoCapture !== 'number' || (value.autoCapture === 1 && (value.phase !== 'encounter' || value.battleMode !== 'auto' || Number(value.wildCaptureChance) <= 0))) return false;
  // Rules 18 focus pause: 2 Strike, 3 Block, mirrored by a bounded focus object.
  if (Number(value.autoCapture) >= 2 ? value.phase !== 'encounter' || value.battleMode !== 'auto' || !object(value.focus) || !keysExactly(value.focus, ['kind', 'turn']) || value.focus.kind !== (value.autoCapture === 2 ? 'strike' : 'block') || !bounded(value.focus.turn, 1, 2) : value.focus !== null) return false;
  if (!bounded(value.worldSeed, 0, 0xffffffff)) return false;
  if (!bounded(value.receivedTrades, 0, Number(value.sequence))) return false;
  if (!bounded(value.foregroundSequence, 0, Number(value.sequence))) return false;
  if (!bounded(value.recoveryRestCount, 0, 40) || !bounded(value.queuedEncounters, 0, 42_949_672) || !bounded(value.stepsToNextEncounter, 0, 100) || value.phase !== 'home' && value.recoveryRestCount !== 0 ||
    (value.phase === 'encounter' && [10, 11, 12, 13, 14, 15, 16, 17, 18, 19].includes(Number(value.wildRules)) ? !['common', 'uncommon', 'rare'].includes(String(value.wildRarity)) : value.wildRarity !== null)) return false;
  if (!validLastCapture(value.lastCapture, value.foregroundSequence) || !validWalkingState(value.walking, value.phase) || value.maxLevel !== 50 || !bounded(value.wildCaptureChance, 0, 100) || !bounded(value.wildLevel, 0, 50) || !bounded(value.wildTurn, 0, 1000) ||
    !bounded(value.careMinute, 0, 0xffffffff) || typeof value.critical !== 'boolean' || ![0, 1].includes(Number(value.captureDeferred)) || typeof value.captureDeferred !== 'number' ||
    !object(value.evolution) || !Array.isArray(value.evolution.options) || value.evolution.options.length > 2 ||
    !value.collection.every(member => object(member) && validCare(member) && bounded(member.level, 1, 50) && bounded(member.formId, 1, 512) && bounded(member.id, 1, 0xfffffffe) && bounded(member.xp, 0, 49000) && bounded(member.xpToNext, 0, 49000) && bounded(member.carePoints, 0, 100) && bounded(member.toilet, 0, 100) && typeof member.careMissed === 'boolean' && bounded(member.careMistakes, 0, 7) && bounded(member.injury, 0, 3))) return false;
  if (!bounded(value.nextMemberId, 1, 0xffffffff) || !bounded(value.wildFormId, 0, 512) || ![0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19].includes(Number(value.wildRules)) || !(value.wildGuard === null || ['brace', 'ward', 'counter'].includes(String(value.wildGuard))) ||
    !object(value.journal) || !keysExactly(value.journal, ['capacity', 'obtainedFormIds']) || value.journal.capacity !== 512 || !Array.isArray(value.journal.obtainedFormIds) || value.journal.obtainedFormIds.length > 512 ||
    !value.journal.obtainedFormIds.every((id, index, ids) => bounded(id, 1, 512) && (index === 0 || Number(ids[index - 1]) < Number(id))) ||
    !value.collection.every((member, index, members) => object(member) && Number(member.id) < Number(value.nextMemberId) && (index === 0 || Number((members[index - 1] as Record<string, unknown>).id) < Number(member.id)))) return false;
  if (value.partyCapacity !== 3 || !Array.isArray(value.partyMemberIds) || value.partyMemberIds.length > 3 || new Set(value.partyMemberIds).size !== value.partyMemberIds.length ||
    !value.partyMemberIds.every(id => bounded(id, 1, 0xfffffffe) && id !== value.activeCreatureId && (value.collection as unknown[]).some(member => object(member) && member.id === id))) return false;
  const { completed, starterId, offerSeed, offers } = value.onboarding;
  if (!bounded(offerSeed, 0, 0xffffffff) || !Array.isArray(offers) || offers.length !== 3 || (offerSeed === 0 ? offers.some(id => id !== 0) : new Set(offers).size !== 3 || !offers.every(id => bounded(id, 1, 512)))) return false;
  if (typeof completed !== 'boolean' || !(starterId === null || Number.isInteger(starterId) && Number(starterId) >= 1 && Number(starterId) <= 11)) return false;
  return completed
    ? ['home', 'encounter'].includes(String(value.phase)) && value.collection.length >= 1 && value.collection.length <= 250
    : starterId === null && value.phase === 'egg' && value.collection.length === 0 && value.activeCreatureId === 0 && value.creature === null && value.species === null && value.combat === null;
}
function supportedStoredVersion(value: unknown): boolean {
  return object(value) && ((value.formatVersion === 1 && value.gameSchemaVersion === 2 && value.rulesVersion === 1) || (value.formatVersion === 2 && value.gameSchemaVersion === 3 && value.rulesVersion === 2) || (value.formatVersion === 3 && value.gameSchemaVersion === 4 && value.rulesVersion === 3) || (value.formatVersion === 4 && value.gameSchemaVersion === 5 && value.rulesVersion === 3) || (value.formatVersion === 5 && value.gameSchemaVersion === 6 && value.rulesVersion === 3) || (value.formatVersion === 6 && value.gameSchemaVersion === 7 && value.rulesVersion === 4) || (value.formatVersion === 7 && value.gameSchemaVersion === 8 && value.rulesVersion === 5) || (value.formatVersion === 8 && value.gameSchemaVersion === 9 && value.rulesVersion === 6) || (value.formatVersion === 9 && value.gameSchemaVersion === 10 && value.rulesVersion === 7) || (value.formatVersion === 10 && value.gameSchemaVersion === 11 && value.rulesVersion === 8) || (value.formatVersion === 11 && value.gameSchemaVersion === 12 && value.rulesVersion === 9) || (value.formatVersion === 12 && value.gameSchemaVersion === 13 && value.rulesVersion === 10) || (value.formatVersion === 13 && value.gameSchemaVersion === 14 && value.rulesVersion === 11) || (value.formatVersion === 14 && (value.gameSchemaVersion === 15 || value.gameSchemaVersion === 16) && value.rulesVersion === 12) || (value.formatVersion === 15 && (value.gameSchemaVersion === 17 || value.gameSchemaVersion === 18 || value.gameSchemaVersion === 19 || value.gameSchemaVersion === 20) && value.rulesVersion === 13) || (value.formatVersion === 16 && value.gameSchemaVersion === 21 && value.rulesVersion === 14) || (value.formatVersion === 17 && value.gameSchemaVersion === 22 && value.rulesVersion === 15) || (value.formatVersion === 18 && value.gameSchemaVersion === 23 && value.rulesVersion === 16) || (value.formatVersion === 19 && value.gameSchemaVersion === 24 && value.rulesVersion === 17) || (value.formatVersion === 20 && value.gameSchemaVersion === 25 && value.rulesVersion === 18) || (value.formatVersion === 21 && (value.gameSchemaVersion === 26 || value.gameSchemaVersion === 27) && value.rulesVersion === 19));
}
function validSnapshot(value: unknown, format?: 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 | 26 | 27, maximumFormat = 27): value is string {
  if (typeof value !== 'string' || ![540, 552, 572, 668, 768, 800, 848, 872, 876, 880, 888, 3936, 3952, 4288, 9148].includes(value.length)) return false;
  const bytes = Buffer.from(value, 'base64');
  if (bytes.toString('base64') !== value || bytes.toString('ascii', 0, 4) !== 'DGVS') return false;
  const actualFormat = bytes.readUInt16LE(4);
  return (format === undefined ? [4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27].includes(actualFormat) && actualFormat <= maximumFormat : actualFormat === format) &&
    bytes.length === (actualFormat === 27 ? 6860 : actualFormat >= 23 ? 3216 : actualFormat === 22 ? 2964 : actualFormat === 21 ? 2952 : actualFormat === 20 ? 664 : actualFormat === 19 ? 660 : actualFormat === 18 ? 656 : actualFormat >= 16 ? 652 : actualFormat === 15 ? 636 : actualFormat === 14 ? 600 : actualFormat >= 8 ? 576 : actualFormat === 7 ? 500 : actualFormat === 6 ? 428 : actualFormat === 5 ? 412 : 404) && bytes.readUInt32LE(8) === (actualFormat === 3 ? 2 : actualFormat === 27 || actualFormat === 26 ? 19 : actualFormat === 25 ? 18 : actualFormat === 24 ? 17 : actualFormat === 23 ? 16 : actualFormat === 22 ? 15 : actualFormat === 21 ? 14 : actualFormat >= 17 ? 13 : actualFormat >= 15 ? 12 : actualFormat === 14 ? 11 : actualFormat === 13 ? 10 : actualFormat === 12 ? 9 : actualFormat === 11 ? 8 : actualFormat === 10 ? 7 : actualFormat === 9 ? 6 : actualFormat === 8 ? 5 : actualFormat === 7 ? 4 : 3);
}
function acceptedFormat21Snapshot(snapshot: unknown, gameSchema: unknown): boolean {
  if (typeof snapshot !== 'string') return false;
  const actual = Buffer.from(snapshot, 'base64').readUInt16LE(4);
  if (gameSchema === 26) return actual === 26;
  return gameSchema === 27 && (actual === 26 || actual === 27);
}

function baseSequence(legacy: Baseline | null): number { return legacy?.histories.reduce((total, history) => total + history.events.length, 0) ?? 0; }
function traceMatches(state: State, trace: AutoTrace): boolean {
  if (trace.outcome === 'none') {
    const last = trace.steps.at(-1);
    return last !== undefined && state.phase === 'encounter' && state.battleMode === 'auto' && [1, 2, 3].includes(Number(state.autoCapture)) &&
      trace.endSequence === state.foregroundSequence && last.playerHpAfter === state.hp && last.enemyHpAfter === state.wildHp &&
      trace.player.formId === state.formId && trace.enemy.formId === state.wildFormId;
  }
  return trace.endSequence <= state.sequence && object(state.lastAutoBattle) && state.lastAutoBattle.sequence === trace.endSequence &&
    state.lastAutoBattle.turns === trace.steps.length && state.lastAutoBattle.outcome === trace.outcome;
}
function batchHash(baseRevision: number, events: Event[], rulesVersion: 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19): string {
  const body = { baseRevision, events: events.map(({ type, value }) => ({ type, value })) };
  return sha256(JSON.stringify(rulesVersion === 1 ? body : { rulesVersion, ...body }));
}
function validateHistory(events: unknown, receipts: unknown, revisionOffset: number, rulesVersion: 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19, batches: Set<string>): number {
  if (!validEvents(events, MAX_EVENTS, rulesVersion) || !Array.isArray(receipts) || receipts.length > MAX_EVENTS) throw new Error('Unsupported or corrupt event history.');
  let end = 0;
  for (let index = 0; index < receipts.length; index++) {
    const receipt = receipts[index];
    if (!object(receipt) || !keysExactly(receipt, ['batchId', 'bodyHash', 'revision', 'eventEnd']) || typeof receipt.batchId !== 'string' || !/^[a-zA-Z0-9_-]{8,80}$/.test(receipt.batchId) || batches.has(receipt.batchId) || typeof receipt.bodyHash !== 'string' || !/^[a-f0-9]{64}$/.test(receipt.bodyHash) || receipt.revision !== revisionOffset + index + 1 || !Number.isSafeInteger(receipt.eventEnd) || Number(receipt.eventEnd) <= end || Number(receipt.eventEnd) > end + MAX_EVENTS_PER_BATCH || Number(receipt.eventEnd) > events.length) throw new Error('Corrupt idempotency history.');
    if (receipt.bodyHash !== batchHash(revisionOffset + index, events.slice(end, Number(receipt.eventEnd)), rulesVersion)) throw new Error('Corrupt event batch checksum.');
    batches.add(receipt.batchId); end = Number(receipt.eventEnd);
  }
  if (end !== events.length) throw new Error('Incomplete event history.');
  return receipts.length;
}
function validateStore(value: unknown): asserts value is StoredData {
  if (!object(value) || !keysExactly(value, ['formatVersion', 'gameSchemaVersion', 'rulesVersion', 'devices']) || !supportedStoredVersion(value) || !Array.isArray(value.devices) || value.devices.length > MAX_DEVICES) throw new Error('Unsupported or corrupt store; explicit migration is required.');
  const ids = new Set();
  const hashes = new Set();
  for (const device of value.devices) {
    const keys = ['deviceId', 'tokenHash', 'seed', 'revision', 'events', 'receipts', ...(value.formatVersion !== 1 ? ['legacy'] : []), ...(Number(value.formatVersion) >= 4 ? ['initialMode'] : [])];
    if (!object(device) || !keysExactly(device, keys) || typeof device.deviceId !== 'string' || !/^dv_[a-f0-9]{24}$/.test(device.deviceId) || typeof device.tokenHash !== 'string' || !/^[a-f0-9]{64}$/.test(device.tokenHash) || !Number.isSafeInteger(device.seed) || Number(device.seed) < 1 || Number(device.seed) > 0xffffffff || !Number.isSafeInteger(device.revision) || Number(device.revision) < 0) throw new Error('Unsupported or corrupt device record.');
    if (Number(value.formatVersion) >= 4 && device.initialMode !== 'legacy' && device.initialMode !== 'onboarding') throw new Error('Invalid immutable game initializer.');
    if (Number(value.formatVersion) >= 4 && Number(value.formatVersion) < 6 && device.initialMode === 'onboarding' && device.legacy !== null) throw new Error('An onboarding identity cannot carry a legacy baseline.');
    if (Array.isArray(device.events) && device.events.some(event => object(event) && event.type === 'hatch') && (Number(value.formatVersion) < 4 || device.initialMode !== 'onboarding')) throw new Error('A legacy identity cannot contain hatch events.');
    if (Number(value.formatVersion) < 5 && Array.isArray(device.events) && device.events.some(event => object(event) && (event.type === 'mode' || event.type === 'auto'))) throw new Error('An older store cannot contain Auto-mode events.');
    if (ids.has(device.deviceId) || hashes.has(device.tokenHash)) throw new Error('Duplicate device record.');
    ids.add(device.deviceId); hashes.add(device.tokenHash);
    const batches = new Set<string>();
    let legacyRevisions = 0;
    let legacyEvents = 0;
    if (value.formatVersion === 2 && device.legacy !== null) {
      const legacy = device.legacy;
      if (!object(legacy) || !keysExactly(legacy, ['events', 'receipts', 'snapshotBase64']) || !validSnapshot(legacy.snapshotBase64, 3)) throw new Error('Corrupt migration baseline.');
      legacyRevisions = validateHistory(legacy.events, legacy.receipts, 0, 1, batches);
      legacyEvents = (legacy.events as Event[]).length;
    }
    if ((Number(value.formatVersion) >= 3) && device.legacy !== null) {
      const legacy = device.legacy;
      if (!object(legacy) || !keysExactly(legacy, ['histories', 'snapshotBase64', ...(Number(value.formatVersion) >= 11 && Object.hasOwn(legacy, 'autoTrace') ? ['autoTrace'] : [])]) || !validSnapshot(legacy.snapshotBase64, value.formatVersion === 21 ? undefined : value.formatVersion === 20 ? 25 : value.formatVersion === 19 ? 24 : value.formatVersion === 18 ? 23 : value.formatVersion === 17 ? 22 : value.formatVersion === 16 ? 21 : value.formatVersion === 15 ? undefined : value.formatVersion === 14 ? undefined : value.formatVersion === 13 ? 14 : value.formatVersion === 12 ? 13 : value.formatVersion === 11 ? 12 : value.formatVersion === 10 ? 11 : value.formatVersion === 9 ? 10 : value.formatVersion === 8 ? 9 : value.formatVersion === 7 ? 8 : value.formatVersion === 6 ? 7 : undefined, Number(value.gameSchemaVersion)) || (value.formatVersion === 15 && ![17, 18, 19, 20].includes(Buffer.from(String(legacy.snapshotBase64), 'base64').readUInt16LE(4))) || (value.formatVersion === 14 && ![15, 16].includes(Buffer.from(String(legacy.snapshotBase64), 'base64').readUInt16LE(4))) || !Array.isArray(legacy.histories) || legacy.histories.length < 1 || legacy.histories.length > (Number(value.formatVersion) >= 21 ? 19 : Number(value.formatVersion) >= 6 ? Number(value.formatVersion) - 3 : 2) || (value.formatVersion === 21 && !acceptedFormat21Snapshot(legacy.snapshotBase64, value.gameSchemaVersion))) throw new Error('Corrupt migration baseline.');
      if (Object.hasOwn(legacy, 'autoTrace') && (Buffer.byteLength(JSON.stringify(legacy.autoTrace)) > 16 * 1024 || !parseAutoTrace(legacy.autoTrace, 'wild'))) throw new Error('Corrupt archived Auto trace.');
      let lastRules = 0;
      for (const history of legacy.histories) {
        if (!object(history) || !keysExactly(history, ['rulesVersion', 'events', 'receipts']) || (![1, 2, ...(Number(value.formatVersion) >= 6 ? [3] : []), ...(Number(value.formatVersion) >= 7 ? [4] : []), ...(Number(value.formatVersion) >= 8 ? [5] : []), ...(Number(value.formatVersion) >= 9 ? [6] : []), ...(Number(value.formatVersion) >= 10 ? [7] : []), ...(Number(value.formatVersion) >= 11 ? [8] : []), ...(Number(value.formatVersion) >= 12 ? [9] : []), ...(Number(value.formatVersion) >= 13 ? [10] : []), ...(Number(value.formatVersion) >= 14 ? [11] : []), ...(Number(value.formatVersion) >= 15 ? [12] : []), ...(Number(value.formatVersion) >= 16 ? [13] : []), ...(Number(value.formatVersion) >= 17 ? [14] : []), ...(Number(value.formatVersion) >= 18 ? [15] : []), ...(Number(value.formatVersion) >= 19 ? [16] : []), ...(Number(value.formatVersion) >= 20 ? [17] : []), ...(Number(value.formatVersion) >= 21 ? [18, 19] : [])].includes(Number(history.rulesVersion))) || Number(history.rulesVersion) <= lastRules) throw new Error('Corrupt archived rule history.');
        if (device.initialMode === 'onboarding' && Number(history.rulesVersion) < 3) throw new Error('An onboarding identity cannot contain pre-onboarding history.');
        legacyRevisions += validateHistory(history.events, history.receipts, legacyRevisions, history.rulesVersion as 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19, batches);
        legacyEvents += (history.events as Event[]).length;
        lastRules = Number(history.rulesVersion);
      }
    }
    if (Number(value.gameSchemaVersion) < 20 && Array.isArray(device.events) && device.events.some(event => object(event) && event.type === 'world-seed')) throw new Error('Unsupported world seed event in a historical schema.');
    if (value.gameSchemaVersion === 15 && Array.isArray(device.events) && device.events.some(event => object(event) && ['accrue-steps', 'present-encounter'].includes(String(event.type)))) throw new Error('Unsupported deferred encounter events in a schema 15 history.');
    const revisions = validateHistory(device.events, device.receipts, legacyRevisions, value.rulesVersion as 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19, batches);
    if (device.revision !== legacyRevisions + revisions || legacyEvents + (device.events as Event[]).length > MAX_EVENTS) throw new Error('Invalid device revision or lifetime history limit.');
  }
}

/** Same-directory replace + fsync: a response is successful only after this completes. */
function atomicWrite(path: string, data: string): void {
  const temporary = `${path}.${randomBytes(8).toString('hex')}.tmp`;
  let descriptor: number | undefined;
  try {
    descriptor = openSync(temporary, 'wx', 0o600);
    writeFileSync(descriptor, data, 'utf8');
    fsyncSync(descriptor);
    closeSync(descriptor); descriptor = undefined;
    renameSync(temporary, path);
    const directory = openSync(dirname(path), 'r');
    try { fsyncSync(directory); } finally { closeSync(directory); }
  } finally {
    if (descriptor !== undefined) closeSync(descriptor);
    if (existsSync(temporary)) unlinkSync(temporary);
  }
}
function loadFile(path: string): StoredData {
  if (statSync(path).size > MAX_STORE) throw new Error('Store exceeds prototype storage limit.');
  const parsed: unknown = JSON.parse(readFileSync(path, 'utf8'));
  validateStore(parsed);
  return parsed;
}

function createUnlockedApp(options: AppOptions = {}) {
  if (process.env.NODE_ENV === 'production') throw new Error('Development-only service: production deployment is intentionally disabled.');
  const lan = lanPolicy(options.lan);
  const includeTestFixtures = options.includeTestFixtures === true;
  const rootDir = resolve(options.rootDir ?? ROOT);
  const dataDir = resolve(options.dataDir ?? join(rootDir, '.data'));
  const corePath = resolve(options.corePath ?? join(rootDir, 'build/digivice-core'));
  const now = options.now ?? Date.now;
  const seedSource = options.seedSource ?? (() => randomInt(1, 0x100000000));
  function nextSeed(): number {
    const seed = seedSource();
    if (!Number.isSafeInteger(seed) || seed < 1 || seed > 0xffffffff) throw new Error('Seed source must return a nonzero unsigned 32-bit integer.');
    return seed;
  }
  let worldArt: ReturnType<typeof createWorldDsAssetService> | undefined;
  let roster: ReturnType<typeof createRosterService> | undefined;
  const privateArt = () => worldArt ??= createWorldDsAssetService({ rootDir });
  const rosterService = () => roster ??= createRosterService({ rootDir, describeArt: id => privateArt().describe(id) });
  let battles: ReturnType<typeof createBattleService> | undefined;
  // Practice has an independent bounded store. A broken practice save must not
  // prevent care/capture recovery; initialize only after device authentication.
  const battleService = () => battles ??= createBattleService({ rootDir, dataDir, corePath: options.battleCorePath, profileValidator: profile => profile.formId !== undefined && rosterService().matchesProfile({ ...profile, formId: profile.formId }) });
  let garage: GarageReadAdapter | undefined = options.garageAssets;
  let garageLoading: Promise<GarageReadAdapter> | undefined;
  // Optional remote storage is loaded only after route and bearer validation.
  // Missing configuration/SDK can never prevent local save recovery or startup.
  async function garageAdapter(): Promise<GarageReadAdapter> {
    if (garage) return garage;
    garageLoading ??= import('./garage-assets.ts').then(({ createGarageAssets }) => {
      garage = createGarageAssets({ rootDir });
      return garage;
    }).catch(error => { garageLoading = undefined; throw error; });
    return garageLoading;
  }
  function garageError(error: unknown): HttpError {
    const status = object(error) ? error.status : undefined;
    if (status === 404) return new HttpError(404, 'garage_asset_not_found', 'This asset is not in the configured private Garage collection.');
    if (status === 502) return new HttpError(502, 'garage_integrity_failed', 'Garage asset verification failed. Previous local art remains available.');
    return new HttpError(503, 'garage_unavailable', 'Garage assets are unavailable. A configured private Garage bucket is required; local play and bundled art remain available.');
  }
  function garageCatalog(value: unknown) {
    if (!object(value) || value.status !== 'available' || !Array.isArray(value.packs) || value.packs.length > 8) throw new HttpError(502, 'garage_integrity_failed', 'Garage returned an invalid asset catalog.');
    const seen = new Set<string>();
    const packs = value.packs.map(entry => {
      if (!object(entry) || typeof entry.id !== 'string' || !Number.isSafeInteger(entry.version) || Number(entry.version) < 1 || Number(entry.version) > 0x7fffffff ||
        !Number.isSafeInteger(entry.bytes) || Number(entry.bytes) < 1 || Number(entry.bytes) > GARAGE_MAX_PACK ||
        typeof entry.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(entry.sha256) ||
        !((entry.kind === 'original' && GARAGE_ORIGINAL_IDS.has(entry.id) && entry.version === 1) || (entry.kind === 'personal' && GARAGE_PERSONAL_ID.test(entry.id))) || seen.has(entry.id)) {
        throw new HttpError(502, 'garage_integrity_failed', 'Garage returned an invalid asset catalog.');
      }
      seen.add(entry.id);
      // Explicit projection keeps adapter diagnostics, bucket names and secrets
      // out of browser responses even if the adapter grows extra fields later.
      return { id: entry.id, version: entry.version, kind: entry.kind, bytes: entry.bytes, sha256: entry.sha256 };
    });
    return { status: 'available', packs: includeTestFixtures ? packs : packs.filter(pack => pack.kind !== 'original') };
  }
  let assetService: ReturnType<typeof createAssetService> | null = null;
  try { assetService = createAssetService({ rootDir, includeTestFixtures }); }
  catch { /* Optional content must not prevent restoring or playing a pet. Asset routes fail closed below. */ }
  let deviceAssets: ReturnType<typeof createDeviceAssetService> | null = null;
  try { deviceAssets = createDeviceAssetService({ rootDir, includeTestFixtures }); }
  catch { /* Missing or invalid device assets cannot block existing saves or browser assets. */ }
  mkdirSync(dataDir, { recursive: true, mode: 0o700 });
  const storePath = join(dataDir, 'store.json');
  const backupPath = join(dataDir, 'store.backup.json');
  let loaded: StoredData = { formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices: [] };
  let loadedText: string | null = null;
  let store: Store;
  let migratedLegacyStore = false;
  let recoveredFromBackup = false;
  if (existsSync(storePath) || existsSync(backupPath)) {
    try {
      loaded = loadFile(storePath);
      loadedText = readFileSync(storePath, 'utf8');
    } catch (error) {
      // Unknown versions need a migration, never silent fallback to an older save.
      try {
        if (statSync(storePath).size > MAX_STORE) throw new Error('Store exceeds prototype storage limit.');
        const raw = JSON.parse(readFileSync(storePath, 'utf8'));
        if (object(raw) && !supportedStoredVersion(raw)) throw new Error('Unsupported store version; explicit migration is required.');
      } catch (versionError) {
        if (versionError instanceof Error && versionError.message.includes('explicit migration')) throw versionError;
      }
      if (!existsSync(backupPath)) throw new Error('Save store cannot be read and no valid recovery backup exists.', { cause: error });
      loaded = loadFile(backupPath);
      loadedText = readFileSync(backupPath, 'utf8');
      atomicWrite(storePath, JSON.stringify(loaded));
      recoveredFromBackup = true;
    }
  }

  function runCore(args: string[], events: Event[]): unknown {
    const input = events.map((event) => `${event.type} ${event.value}`).join('\n');
    const result = spawnSync(corePath, args, { input: input ? `${input}\n` : '', encoding: 'utf8', timeout: 3000, maxBuffer: 1024 * 1024, windowsHide: true });
    if (result.error) throw new HttpError(503, 'core_unavailable', 'The shared game core is unavailable. Run npm run build first.');
    if (result.status !== 0) throw new HttpError(422, 'invalid_transition', 'The game core rejected an action in this event history.');
    try { return JSON.parse(result.stdout); } catch { throw new HttpError(503, 'core_output_invalid', 'The game core returned invalid output.'); }
  }
  // Flick is additive within rules 10. Check the executable as well as its
  // schema before advertising support to a newer browser against a stale build.
  try {
    const trajectory = runCore(['--flick-trajectory', '41140'], []);
    if (!object(trajectory) || !keysExactly(trajectory, ['inputVersion', 'landingX', 'landingY', 'hit']) ||
      trajectory.inputVersion !== 1 || trajectory.landingX !== 206 || trajectory.landingY !== 120 || trajectory.hit !== true) throw new Error('Unsupported flick trajectory.');
  } catch (error) {
    throw new Error('The installed game core does not support capture flick input version 1. Run npm run build before starting this service.', { cause: error });
  }
  // Ring timing is additive within rules 13. A matching schema alone does not
  // establish that the native binary can interpret this newer event correctly.
  try {
    const contract = runCore(['--capture-ring-contract'], []);
    if (!object(contract) || !keysExactly(contract, ['inputVersion', 'action', 'cycleMs', 'factors']) ||
      contract.inputVersion !== 1 || contract.action !== 'ring-capture' || contract.cycleMs !== 2400 ||
      !object(contract.factors) || !keysExactly(contract.factors, ['red', 'orange', 'green']) ||
      contract.factors.red !== 10 || contract.factors.orange !== 50 || contract.factors.green !== 100) throw new Error('Unsupported capture timing quality contract.');
  } catch (error) {
    throw new Error('The installed game core does not support capture timing quality input version 1. Run npm run build before starting this service.', { cause: error });
  }
  // Party XP changes wild reward outcomes. Refuse a stale executable
  // even with an empty store before advertising XP companions.
  try {
    const budget = runCore(['--budget'], []);
    if (!object(budget) || budget.schemaVersion !== 27 || budget.rulesVersion !== 19 || budget.collectionCapacity !== 250 || budget.partyCapacity !== 3 ||
      budget.snapshotBytes !== 6860 || budget.jsonBufferBytes !== 262144) throw new Error('Unsupported XP companion contract.');
  } catch (error) {
    throw new Error('The installed game core does not support the XP companion contract. Run npm run build before starting this service.', { cause: error });
  }
  let starters: unknown;
  function starterCatalog(): unknown {
    if (starters) return starters;
    const value = runCore(['--starters'], []);
    const boundedText = (input: unknown) => typeof input === 'string' && input.length >= 1 && input.length <= 64 && !/[\u0000-\u001f\u007f]/.test(input);
    if (!object(value) || !keysExactly(value, ['formatVersion', 'rulesVersion', 'starters']) || value.formatVersion !== 1 || value.rulesVersion !== 19 ||
      !Array.isArray(value.starters) || value.starters.length !== 8 || Buffer.byteLength(JSON.stringify(value)) > 8192 ||
      !value.starters.every((entry, index) => {
        if (!object(entry) || !keysExactly(entry, ['id', 'species', 'name', 'stage', 'combat']) || entry.id !== index + 1 || entry.species !== STARTER_SPECIES[index] || !boundedText(entry.name) || entry.stage !== 'Rookie' || !object(entry.combat)) return false;
        const combat = entry.combat;
        return keysExactly(combat, ['maxHp', 'attack', 'defense', 'magic', 'resistance', 'type', 'skills']) &&
          ['maxHp', 'attack', 'defense', 'magic', 'resistance'].every(key => Number.isSafeInteger(combat[key]) && Number(combat[key]) >= 1 && Number(combat[key]) <= 1000) && boundedText(combat.type) &&
          object(combat.skills) && keysExactly(combat.skills, ['physical', 'heavy', 'magic']) && Object.values(combat.skills).every(boundedText);
      })) throw new HttpError(503, 'core_output_invalid', 'The game core returned an unsupported starter catalog.');
    starters = value;
    return starters;
  }
  const evolutionCatalogs = new Map<string, unknown>();
  function evolutionCatalog(species: string): unknown {
    if (!LINEAGES.includes(species) && (!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(species) || species.length > 63 || !rosterService().hasLineage(species))) throw new HttpError(400, 'invalid_lineage', 'Choose one supported species lineage.');
    if (evolutionCatalogs.has(species)) return evolutionCatalogs.get(species);
    const value = runCore(['--evolutions', species], []);
    const integer = (input: unknown, min: number, max: number) => Number.isSafeInteger(input) && Number(input) >= min && Number(input) <= max;
    const text = (input: unknown) => typeof input === 'string' && input.length >= 1 && input.length <= 64 && !/[\u0000-\u001f\u007f]/.test(input);
    if (!object(value) || !keysExactly(value, ['formatVersion', 'rulesVersion', 'species', 'forms']) || value.formatVersion !== 1 || value.rulesVersion !== 19 || value.species !== species ||
      !Array.isArray(value.forms) || value.forms.length < 1 || value.forms.length > 7 || Buffer.byteLength(JSON.stringify(value)) > 16 * 1024 || !value.forms.every(form => {
        if (!object(form) || !keysExactly(form, ['formId', 'parentId', 'children', 'name', 'stage', 'requiredLevel', 'requiredBond', 'previewLevel', 'artId', 'combat']) ||
          !integer(form.formId, 1, 512) || !integer(form.parentId, 0, 512) || !Array.isArray(form.children) || form.children.length > 2 || !form.children.every(id => integer(id, 1, 512)) ||
          !text(form.name) || !(form.stage === null || ['Fresh', 'In-Training', 'Rookie', 'Champion', 'Ultimate', 'Mega', 'Armor', 'No Level', 'Original'].includes(String(form.stage))) ||
          !integer(form.requiredLevel, 1, 50) || !integer(form.requiredBond, 0, 200) || !integer(form.previewLevel, 1, 50) || !(form.artId === null || text(form.artId)) || !object(form.combat)) return false;
        const combat = form.combat;
        return keysExactly(combat, ['maxHp', 'attack', 'defense', 'magic', 'resistance', 'type', 'skills']) &&
          integer(combat.maxHp, 1, 2048) && ['attack', 'defense', 'magic', 'resistance'].every(key => integer(combat[key], 1, 256)) && ['grove', 'tide', 'ember', 'neutral'].includes(String(combat.type)) &&
          object(combat.skills) && keysExactly(combat.skills, ['physical', 'heavy', 'magic']) && Object.values(combat.skills).every(text);
      })) throw new HttpError(503, 'core_output_invalid', 'The game core returned an unsupported bounded evolution catalog.');
    const ids = new Set(value.forms.map(form => (form as Record<string, unknown>).formId));
    if (ids.size !== value.forms.length || value.forms.some(form => {
      const entry = form as Record<string, unknown>;
      return entry.parentId !== 0 && !ids.has(entry.parentId) || (entry.children as unknown[]).some(id => !ids.has(id));
    })) throw new HttpError(503, 'core_output_invalid', 'The native evolution catalog has invalid form links.');
    evolutionCatalogs.set(species, value);
    return value;
  }
  function evolutionGraph(formId: number, offset: number, limit: number): unknown {
    let value: unknown;
    try { value = runCore(['--evolution-graph', String(formId), String(offset), String(limit)], []); }
    catch (error) { if (error instanceof HttpError && error.status === 422) throw new HttpError(404, 'graph_form_not_found', 'No evolution graph page exists for this form and offset.'); throw error; }
    const integer = (input: unknown, min: number, max: number) => Number.isSafeInteger(input) && Number(input) >= min && Number(input) <= max;
    const text = (input: unknown, max = 64) => typeof input === 'string' && Buffer.byteLength(input) >= 1 && Buffer.byteLength(input) <= max && !/[\u0000-\u001f\u007f]/.test(input);
    const ids = (input: unknown, max: number): input is number[] => Array.isArray(input) && input.length <= max && input.every(id => integer(id, 1, 512)) && new Set(input).size === input.length;
    const bad = () => { throw new HttpError(503, 'core_output_invalid', 'The native evolution graph is unsupported or exceeds its bounds.'); };
    if (!object(value) || !keysExactly(value, ['formatVersion', 'rulesVersion', 'catalogVersion', 'focusFormId', 'offset', 'limit', 'total', 'nextOffset', 'forms']) || value.formatVersion !== 2 || value.rulesVersion !== 19 || value.catalogVersion !== 6 || value.focusFormId !== formId || value.offset !== offset || value.limit !== limit || !integer(value.total, 1, 512) || !Array.isArray(value.forms) || value.forms.length !== Math.min(limit, Math.max(0, Number(value.total) - offset)) || Buffer.byteLength(JSON.stringify(value)) > 16384) return bad();
    const expectedNext = offset + value.forms.length < Number(value.total) ? offset + value.forms.length : null;
    if (value.nextOffset !== expectedNext) return bad();
    const seen = new Set<number>();
    for (const form of value.forms) {
      if (!object(form) || !keysExactly(form, ['formId', 'name', 'stage', 'artId', 'minLevel', 'minBond', 'previewLevel', 'combat', 'parents', 'children', 'edges']) || !integer(form.formId, 1, 512) || seen.has(Number(form.formId)) || !text(form.name) || !(form.stage === null || ['Fresh', 'In-Training', 'Rookie', 'Champion', 'Ultimate', 'Mega', 'Armor', 'No Level', 'Original'].includes(String(form.stage))) || !(form.artId === null || text(form.artId)) || !integer(form.minLevel, 1, 50) || !integer(form.minBond, 0, 200) || !integer(form.previewLevel, Number(form.minLevel), 50) || !ids(form.parents, 512) || !ids(form.children, 2) || form.parents.includes(Number(form.formId)) || form.children.includes(Number(form.formId)) || !Array.isArray(form.edges) || form.edges.length !== form.children.length || !object(form.combat)) return bad();
      seen.add(Number(form.formId));
      const children = form.children;
      if (!form.edges.every((edge, index) => object(edge) && keysExactly(edge, ['toFormId', 'requiredLevel', 'requiredBond', 'requiredCare']) && edge.toFormId === children[index] && integer(edge.requiredLevel, 1, 50) && integer(edge.requiredBond, 0, 200) && integer(edge.requiredCare, 12, 100))) return bad();
      const combat = form.combat;
      if (!keysExactly(combat, ['maxHp', 'attack', 'defense', 'magic', 'resistance', 'type', 'skills']) || !integer(combat.maxHp, 1, 2048) || !['attack', 'defense', 'magic', 'resistance'].every(key => integer(combat[key], 1, 256)) || !['grove', 'tide', 'ember', 'neutral'].includes(String(combat.type)) || !object(combat.skills) || !keysExactly(combat.skills, ['physical', 'heavy', 'magic']) || !Object.values(combat.skills).every(label => text(label, 32))) return bad();
    }
    return value;
  }
  function replay(events: Event[], seed: number, legacy: Baseline | null = null, initialMode: InitialMode = 'legacy'): State {
    const state = runCore(legacy ? ['--replay-snapshot', legacy.snapshotBase64] : [initialMode === 'onboarding' ? '--replay-onboarding' : '--replay', String(seed)], events);
    if (!stateSupported(state) || state.sequence !== baseSequence(legacy) + events.length) throw new HttpError(503, 'migration_required', 'The game core state or rules version is unsupported.');
    return state;
  }
  function replayResult(events: Event[], seed: number, legacy: Baseline | null, initialMode: InitialMode): { state: State; autoTrace: AutoTrace | null } {
    const result = runCore(legacy ? ['--replay-snapshot-trace', legacy.snapshotBase64] : [initialMode === 'onboarding' ? '--replay-onboarding-trace' : '--replay-trace', String(seed)], events);
    if (!object(result) || !keysExactly(result, ['state', 'trace']) || !stateSupported(result.state) || result.state.sequence !== baseSequence(legacy) + events.length) throw new HttpError(503, 'core_output_invalid', 'The game core returned an unsupported Auto result.');
    let autoTrace: AutoTrace | null;
    try { autoTrace = parseAutoTrace(result.trace, 'wild'); }
    catch { throw new HttpError(503, 'core_output_invalid', 'The game core returned an unsupported Auto trace.'); }
    if (autoTrace && !traceMatches(result.state, autoTrace)) throw new HttpError(503, 'core_output_invalid', 'The native Auto result and trace disagree.');
    // A migrated snapshot carries the summary, not its old animation frames.
    // Retain the frozen historical trace without replaying old actions under current rules.
    if (!autoTrace && legacy?.autoTrace && traceMatches(result.state, legacy.autoTrace)) autoTrace = legacy.autoTrace;
    return { state: result.state, autoTrace };
  }
  // Probe the executable without consuming device entropy or creating a profile.
  replay([], 1, null, 'onboarding');
  function persist(next: Store): void {
    for (const device of next.devices) {
      if (!device.legacy) continue;
      const bytes = Buffer.from(device.legacy.snapshotBase64, 'base64');
      if (bytes.length !== 6860 || bytes.readUInt16LE(4) !== 27) throw new Error('Refusing to label a store schema 27 while a snapshot still uses an older layout.');
    }
    const serialized = JSON.stringify(next);
    if (Buffer.byteLength(serialized) > MAX_STORE) throw new HttpError(507, 'storage_limit', 'The development store is full.');
    // Both copies contain each acknowledged save. An interrupted write may recover
    // an unacknowledged batch; its retained receipt makes the retry safe.
    atomicWrite(backupPath, serialized);
    atomicWrite(storePath, serialized);
    store = next;
  }
  if (loaded.formatVersion === 1 || loaded.formatVersion === 2) {
    const previous = loaded;
    const devices = previous.devices.map((device): Device => {
      // Replay old events only with their frozen executor, never new combat rules.
      const oldLegacy = previous.formatVersion === 2 ? (device as V2Device).legacy : null;
      const histories: HistoricalEvents[] = oldLegacy ? [{ rulesVersion: 1, events: oldLegacy.events, receipts: oldLegacy.receipts }] : [];
      histories.push({ rulesVersion: previous.rulesVersion, events: device.events, receipts: device.receipts });
      const args = previous.formatVersion === 1 ? ['--migrate-v1', String(device.seed)] : oldLegacy ? ['--migrate-v2-snapshot', oldLegacy.snapshotBase64] : ['--migrate-v2', String(device.seed)];
      const migrated = runCore(args, device.events);
      if (!object(migrated) || !validSnapshot(migrated.snapshotBase64) || !stateSupported(migrated.state)) throw new Error('Legacy migration returned an unsupported baseline.');
      const legacy: Baseline = { histories, snapshotBase64: migrated.snapshotBase64 };
      if (migrated.state.sequence !== baseSequence(legacy)) throw new Error('Legacy migration changed the event sequence.');
      const restored = replay([], device.seed, legacy);
      if (JSON.stringify(restored) !== JSON.stringify(migrated.state)) throw new Error('Legacy migration baseline did not restore identically.');
      return { ...device, legacy, initialMode: 'legacy', events: [], receipts: [] };
    });
    const next: Store = { formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices };
    validateStore(next);
    const archivePath = join(dataDir, `store.rules-v${previous.rulesVersion}.json`);
    if (!existsSync(archivePath)) atomicWrite(archivePath, loadedText ?? JSON.stringify(previous));
    persist(next);
    migratedLegacyStore = true;
  } else if (loaded.formatVersion === 3 || loaded.formatVersion === 4 || loaded.formatVersion === 5 || loaded.formatVersion === 6 || loaded.formatVersion === 7 || loaded.formatVersion === 8 || loaded.formatVersion === 9 || loaded.formatVersion === 10 || loaded.formatVersion === 11 || loaded.formatVersion === 12 || loaded.formatVersion === 13 || loaded.formatVersion === 14 || loaded.formatVersion === 15 || loaded.formatVersion === 16 || loaded.formatVersion === 17 || loaded.formatVersion === 18 || loaded.formatVersion === 19 || loaded.formatVersion === 20) {
    const previous = loaded;
    const version = previous.rulesVersion;
    const devices = previous.devices.map((device): Device => {
      const initialMode: InitialMode = previous.formatVersion === 3 ? 'legacy' : (device as Device).initialMode;
      const args = device.legacy ? [`--migrate-v${version}-snapshot`, device.legacy.snapshotBase64] :
        [`--migrate-v${version}${initialMode === 'onboarding' ? '-onboarding' : ''}`, String(device.seed)];
      const migrated = runCore(args, device.events);
      if (!object(migrated) || !validSnapshot(migrated.snapshotBase64, 27) || !stateSupported(migrated.state)) throw new Error('Frozen-rules migration returned an unsupported baseline.');
      const legacy: Baseline = { histories: [...(device.legacy?.histories ?? []), { rulesVersion: version, events: device.events, receipts: device.receipts }], snapshotBase64: migrated.snapshotBase64 };
      if (migrated.state.sequence !== baseSequence(legacy)) throw new Error('Frozen-rules migration changed the event sequence.');
      if (version === 8 || version === 9 || version === 10 || version === 11 || version === 12 || version === 13 || version === 14 || version === 15 || version === 16 || version === 17 || version === 18) {
        const traceArgs = device.legacy ? [`--replay-v${version}-snapshot-trace`, device.legacy.snapshotBase64] :
          [`--replay-v${version}${initialMode === 'onboarding' ? '-onboarding' : ''}-trace`, String(device.seed)];
        const original = runCore(traceArgs, device.events);
        if (!object(original) || !keysExactly(original, ['state', 'trace']) || !object(original.state) || original.state.schemaVersion !== (version === 18 ? 25 : version === 17 ? 24 : version === 16 ? 23 : version === 15 ? 22 : version === 14 ? 21 : version === 13 ? 20 : version === 12 ? 16 : version + 3) || original.state.rulesVersion !== version || original.state.sequence !== migrated.state.sequence || Buffer.byteLength(JSON.stringify(original.trace)) > 16 * 1024) throw new Error('Frozen-rules Auto trace recovery failed.');
        let autoTrace = parseAutoTrace(original.trace, 'wild');
        if (device.legacy?.autoTrace) {
          // The old suffix may contain no Auto action, while its service baseline
          // already retained an even earlier exact trace. Validate that association
          // before carrying it across another epoch, without relabeling its frames.
          const oldBaseline = runCore(traceArgs, []);
          if (!object(oldBaseline) || !object(oldBaseline.state) || !traceMatches(oldBaseline.state as State, device.legacy.autoTrace)) throw new Error('Archived Auto trace does not match its frozen baseline summary.');
          if (!autoTrace && traceMatches(original.state as State, device.legacy.autoTrace)) autoTrace = device.legacy.autoTrace;
        }
        if (autoTrace) {
          if (!traceMatches(original.state as State, autoTrace) || !traceMatches(migrated.state, autoTrace)) throw new Error('Frozen-rules Auto trace does not match its saved summary.');
          legacy.autoTrace = autoTrace;
        }
      }
      const restored = replay([], device.seed, legacy, initialMode);
      if (JSON.stringify(restored) !== JSON.stringify(migrated.state)) throw new Error('Frozen-rules migration baseline did not restore identically.');
      return { ...device, initialMode, legacy, events: [], receipts: [] };
    });
    const next: Store = { formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices };
    validateStore(next);
    const archivePath = join(dataDir, `store.rules-v${version}.json`);
    if (!existsSync(archivePath)) atomicWrite(archivePath, loadedText ?? JSON.stringify(previous));
    persist(next);
    migratedLegacyStore = true;
  } else if (loaded.formatVersion === 21 && loaded.devices.some(device => (device.legacy ? Buffer.from(device.legacy.snapshotBase64, 'base64').readUInt16LE(4) !== 27 : loaded.gameSchemaVersion === 26 && (device.events.length > 0 || device.receipts.length > 0)))) {
    const previous = loaded;
    const devices = previous.devices.map((device): Device => {
      const snapshotSchema = device.legacy ? Buffer.from(device.legacy.snapshotBase64, 'base64').readUInt16LE(4) : 0;
      const foldSnapshot = snapshotSchema !== 0 && snapshotSchema !== 27;
      const foldEvents = !device.legacy && previous.gameSchemaVersion === 26 && (device.events.length > 0 || device.receipts.length > 0);
      if (!foldSnapshot && !foldEvents) return device;
      // Replay the saved suffix at the 60-slot roster, then store a real schema-27 snapshot.
      const args = device.legacy ? ['--fold-v19-snapshot', device.legacy.snapshotBase64] : [`--fold-v19${device.initialMode === 'onboarding' ? '-onboarding' : ''}`, String(device.seed)];
      const migrated = runCore(args, device.events);
      if (!object(migrated) || !validSnapshot(migrated.snapshotBase64, 27) || !stateSupported(migrated.state)) throw new Error('Rules-19 roster fold returned an unsupported baseline.');
      const histories: HistoricalEvents[] = [...(device.legacy?.histories ?? [])];
      if (device.events.length > 0 || device.receipts.length > 0) histories.push({ rulesVersion: 19, events: device.events, receipts: device.receipts });
      if (histories.length < 1) throw new Error('Rules-19 roster fold lost the archived history.');
      const legacy: Baseline = { histories, snapshotBase64: migrated.snapshotBase64 };
      if (device.legacy?.autoTrace && traceMatches(migrated.state, device.legacy.autoTrace)) legacy.autoTrace = device.legacy.autoTrace;
      if (migrated.state.sequence !== baseSequence(legacy)) throw new Error('Rules-19 roster fold changed the event sequence.');
      const restored = replay([], device.seed, legacy, device.initialMode);
      if (JSON.stringify(restored) !== JSON.stringify(migrated.state)) throw new Error('Rules-19 roster fold baseline did not restore identically.');
      return { ...device, legacy, events: [], receipts: [] };
    });
    const next: Store = { formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices };
    validateStore(next);
    const archivePath = join(dataDir, 'store.schema-26.json');
    if (!existsSync(archivePath)) atomicWrite(archivePath, loadedText ?? JSON.stringify(previous));
    persist(next);
    migratedLegacyStore = true;
    for (const device of store.devices) {
      if (device.legacy?.autoTrace && !traceMatches(replay([], device.seed, device.legacy, device.initialMode), device.legacy.autoTrace)) throw new Error('Archived Auto trace does not match its baseline summary.');
      replay(device.events, device.seed, device.legacy, device.initialMode);
    }
  } else {
    store = loaded;
    // Unknown versions or invalid snapshots fail closed before the server listens.
    for (const device of store.devices) {
      if (device.legacy?.autoTrace && !traceMatches(replay([], device.seed, device.legacy, device.initialMode), device.legacy.autoTrace)) throw new Error('Archived Auto trace does not match its baseline summary.');
      replay(device.events, device.seed, device.legacy, device.initialMode);
    }
  }
  const pairings = new Map<string, number>();
  let pairingWindow = now();
  let pairingStarts = 0;
  function json(response: ServerResponse, status: number, body: unknown): void {
    response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' });
    response.end(JSON.stringify(body));
  }
  function checkLocal(request: IncomingMessage): void {
    const host = (request.headers.host ?? '').toLowerCase();
    const remote = request.socket.remoteAddress;
    if (lan) {
      const peer = remote?.startsWith('::ffff:') ? remote.slice(7) : remote;
      if (!lan.allowedHosts.includes(host) || !peer || (!loopback(peer) && !privateIPv4(peer))) throw new HttpError(403, 'lan_access_rejected', 'Use the explicitly configured local network service.');
    } else {
      if (!/^(localhost|127\.0\.0\.1)(:\d{1,5})?$/.test(host)) throw new HttpError(403, 'local_only', 'Use the loopback URL shown by the service.');
      if (remote && !loopback(remote)) throw new HttpError(403, 'local_only', 'Only loopback connections are accepted.');
    }
    const origin = request.headers.origin;
    if (origin && (lan ? !lan.allowedOrigins.includes(origin) : origin !== `http://${host}`)) throw new HttpError(403, 'origin_rejected', 'This request origin is not allowed.');
    if (request.headers['sec-fetch-site'] === 'cross-site') throw new HttpError(403, 'origin_rejected', 'Cross-site requests are disabled.');
  }
  async function body(request: IncomingMessage): Promise<unknown> {
    if (request.headers['content-type']?.split(';')[0].trim() !== 'application/json') throw new HttpError(415, 'json_required', 'Use Content-Type: application/json.');
    if (Number(request.headers['content-length'] ?? 0) > MAX_BODY) throw new HttpError(413, 'body_too_large', 'Request body exceeds 32 KiB.');
    const chunks: Buffer[] = [];
    let bytes = 0;
    for await (const chunk of request) {
      bytes += chunk.length;
      if (bytes > MAX_BODY) throw new HttpError(413, 'body_too_large', 'Request body exceeds 32 KiB.');
      chunks.push(chunk);
    }
    try { return JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch { throw new HttpError(400, 'invalid_json', 'The request body is not valid JSON.'); }
  }
  function authenticated(request: IncomingMessage): Device {
    const token = request.headers.authorization?.match(/^Bearer ([A-Za-z0-9_-]{43})$/)?.[1];
    if (!token) throw new HttpError(401, 'unauthorized', 'A paired device bearer token is required.');
    const candidate = Buffer.from(sha256(token), 'hex');
    const device = store.devices.find((entry) => timingSafeEqual(candidate, Buffer.from(entry.tokenHash, 'hex')));
    if (!device) throw new HttpError(401, 'unauthorized', 'A paired device bearer token is required.');
    return device;
  }
  function pack(): Buffer {
    const path = join(rootDir, 'assets/starter-v1.json');
    if (!existsSync(path)) throw new HttpError(503, 'pack_unavailable', 'The starter asset pack is not installed.');
    if (statSync(path).size > 512 * 1024) throw new HttpError(503, 'pack_invalid', 'The starter pack exceeds 512 KiB.');
    return readFileSync(path);
  }
  const handler = async (request: IncomingMessage, response: ServerResponse) => {
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('X-Content-Type-Options', 'nosniff');
    response.setHeader('X-Frame-Options', 'DENY');
    response.setHeader('Referrer-Policy', 'no-referrer');
    response.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
    try {
      checkLocal(request);
      const path = (request.url ?? '').split('?')[0];
      const method = request.method;
      if (path === '/api/device/health') {
        if (method !== 'GET' || request.url !== path) throw new HttpError(400, 'invalid_request', 'Device health expects GET without parameters.');
        return json(response, 200, { status: 'ok', protocolVersion: 1, gameRulesVersion: 19, gameSchemaVersion: 27, assetProfile: DEVICE_PROFILE });
      }
      if (path === '/api/device/assets' || path.startsWith('/api/device/assets/')) {
        if (!deviceAssets) return json(response, 503, { error: 'device_assets_unavailable', message: 'Verified device assets are not installed.' });
        if (deviceAssets.handleDeviceAssetRequest(request, response, path)) return;
      }
      if (path === '/api/garage' || path.startsWith('/api/garage/')) {
        authenticated(request); // First: never diagnose/read Garage for anonymous callers.
        if (method !== 'GET') throw new HttpError(405, 'garage_read_only', 'Garage browser routes support GET downloads only.');
        if ((request.url ?? '') !== path) throw new HttpError(400, 'invalid_request', 'Garage routes do not accept query parameters.');
        const original = /^\/api\/garage\/packs\/(starter-v2|tide-v1|ember-v1)\/1$/.exec(path);
        const personal = /^\/api\/garage\/personal\/(personal-[a-z0-9][a-z0-9-]{0,38})\/([1-9][0-9]{0,9})$/.exec(path);
        if (original && !includeTestFixtures) throw new HttpError(404, 'not_found', 'No such Garage asset route.');
        if (path !== '/api/garage/catalog' && !original && !(personal && Number(personal[2]) <= 0x7fffffff)) {
          throw new HttpError(404, 'not_found', 'No such Garage asset route.');
        }
        try {
          const adapter = await garageAdapter();
          if (path === '/api/garage/catalog') return json(response, 200, garageCatalog(await adapter.list()));
          if (original) {
            const asset = await adapter.fetchPack(original[1], 1);
            if (!(asset.bytes instanceof Uint8Array) || asset.bytes.byteLength < 1 || asset.bytes.byteLength > GARAGE_MAX_PACK ||
              asset.contentType !== 'application/json' || typeof asset.sha256 !== 'string' || asset.sha256 !== sha256(Buffer.from(asset.bytes))) {
              throw new HttpError(502, 'garage_integrity_failed', 'Garage returned an invalid asset pack.');
            }
            response.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': asset.bytes.byteLength, ETag: `"${asset.sha256}"` });
            response.end(asset.bytes); return;
          }
          // fetchPersonal enforces the local receipt's exact ID/version/key pair
          // before any remote object download; no request can supply an object key.
          const asset = await adapter.fetchPersonal(personal![1], Number(personal![2]));
          if (typeof asset.packText !== 'string' || typeof asset.provenanceText !== 'string' ||
            Buffer.byteLength(asset.packText) < 1 || Buffer.byteLength(asset.packText) > GARAGE_MAX_PACK ||
            Buffer.byteLength(asset.provenanceText) < 1 || Buffer.byteLength(asset.provenanceText) > GARAGE_MAX_PROVENANCE) {
            throw new HttpError(502, 'garage_integrity_failed', 'Garage returned an invalid personal-art bundle.');
          }
          return json(response, 200, { packText: asset.packText, provenanceText: asset.provenanceText });
        } catch (error) { throw garageError(error); }
      }
      if (path === '/api/assets' || path.startsWith('/api/assets/')) {
        if (!assetService) return json(response, 503, { error: 'assets_unavailable', message: 'Asset verification failed. The built-in art and game save remain available.' });
        if (assetService.handleAssetRequest(request, response, path)) return;
      }
      if (path === '/api/roster' || path.startsWith('/api/roster/')) {
        const art = /^\/api\/roster\/art\/([1-9][0-9]{0,2})$/.exec(path);
        if (path.startsWith('/api/roster/art/')) {
          authenticated(request);
          if (method !== 'GET' || request.url !== path || !art || Number(art[1]) > 512) throw new HttpError(404, 'roster_art_not_found', 'No such private catalog artwork.');
          let bundle;
          try { bundle = privateArt().loadBrowser(Number(art[1])); }
          catch { throw new HttpError(503, 'roster_art_unavailable', 'This private artwork could not be verified. Existing artwork remains available.'); }
          if (!bundle) throw new HttpError(404, 'roster_art_not_found', 'No audited private artwork is installed for this form.');
          return json(response, 200, bundle);
        }
        if (method !== 'GET') throw new HttpError(405, 'roster_read_only', 'Roster metadata supports GET only.');
        const query = new URL(request.url!, 'http://localhost').searchParams;
        const detail = /^\/api\/roster\/([1-9][0-9]{0,2})$/.exec(path);
        if (path !== '/api/roster' && !(detail && Number(detail[1]) <= 512)) throw new HttpError(404, 'roster_form_not_found', 'No such catalog form.');
        let result;
        try { result = detail ? rosterService().detail(Number(detail[1]), query) : rosterService().list(query); }
        catch (error) { if (error instanceof RosterError) throw new HttpError(error.status, error.code, error.message); throw new HttpError(503, 'roster_unavailable', 'The bounded roster metadata is unavailable. Saved companions remain available.'); }
        if (Buffer.byteLength(JSON.stringify(result)) > 64 * 1024) throw new HttpError(503, 'roster_unavailable', 'Roster response exceeds its bounded size.');
        return json(response, 200, result);
      }
      if (method === 'GET' && path === '/api/health') return json(response, 200, { status: 'ok', mode: 'local-development', assetStatus: assetService ? 'available' : 'unavailable', schemaVersion: 27, rulesVersion: 19, collectionCapacity: 250, partyCapacity: 3, capabilities: { careInjury: 1, autoFocus: 1, captureFlick: 1, captureTimingRing: 1, captureTimingQuality: 1, worldSeed: 1, deferredEncounters: 1, manualAutoCapture: 1, ...(includeTestFixtures ? { testFixtures: 1 } : {}) }, recoveredFromBackup, migratedLegacyStore });
      if (method === 'GET' && path === '/api/starters') return json(response, 200, starterCatalog());
      if (path === '/api/evolution-graph') {
        const query = new URL(request.url!, 'http://localhost').searchParams, keys = [...query.keys()];
        if (method !== 'GET' || !query.has('formId') || keys.some(key => !['formId', 'offset', 'limit'].includes(key)) || new Set(keys).size !== keys.length) throw new HttpError(400, 'invalid_graph_query', 'Supply one formId and optional bounded offset/limit.');
        const parameter = (name: string, fallback: number, min: number, max: number) => {
          const raw = query.get(name); if (raw === null) return fallback;
          if (!/^(0|[1-9][0-9]*)$/.test(raw) || !Number.isSafeInteger(Number(raw)) || Number(raw) < min || Number(raw) > max) throw new HttpError(400, 'invalid_graph_query', 'Graph parameters must be bounded unsigned integers.');
          return Number(raw);
        };
        return json(response, 200, evolutionGraph(parameter('formId', 0, 1, 512), parameter('offset', 0, 0, 512), parameter('limit', 16, 1, 16)));
      }
      if (method === 'GET' && path === '/api/evolution/catalog') {
        const parameters = new URL(request.url!, 'http://localhost').searchParams;
        if ([...parameters.keys()].length !== 1 || !parameters.has('species')) throw new HttpError(400, 'invalid_lineage', 'Supply exactly one species lineage.');
        return json(response, 200, evolutionCatalog(parameters.get('species')!));
      }
      if (method === 'GET' && path === '/api/combat/catalog') return json(response, 200, runCore(['--roster'], []));
      if (method === 'GET' && path === '/api/catalog') {
        if (!includeTestFixtures) return json(response, 200, { packs: [] });
        const bytes = pack();
        return json(response, 200, { packs: [{ packId: 'starter-v1', version: 1, url: '/api/packs/starter-v1', sha256: sha256(bytes), bytes: bytes.length }] });
      }
      if (method === 'GET' && path === '/api/packs/starter-v1') {
        if (!includeTestFixtures) throw new HttpError(404, 'not_found', 'No such asset pack.');
        const bytes = pack();
        response.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'ETag': `"${sha256(bytes)}"`, 'Content-Length': bytes.length });
        response.end(bytes); return;
      }
      if (method === 'POST' && path === '/api/pairing/start') {
        if (lan && !loopback(request.socket.remoteAddress ?? '')) throw new HttpError(403, 'local_enrollment_only', 'Development pairing is available only on the server loopback connection.');
        const input = await body(request);
        if (!object(input) || !keysExactly(input, [])) throw new HttpError(400, 'invalid_request', 'Pairing start expects an empty JSON object.');
        const time = now();
        for (const [code, expiresAt] of pairings) if (expiresAt <= time) pairings.delete(code);
        if (time - pairingWindow >= 60_000) { pairingWindow = time; pairingStarts = 0; }
        if (++pairingStarts > 30 || pairings.size >= 32) throw new HttpError(429, 'pairing_rate_limit', 'Too many pairing requests. Wait a minute.');
        if (store.devices.length >= MAX_DEVICES) throw new HttpError(409, 'device_limit', 'The prototype supports eight paired devices.');
        const code = randomBytes(6).toString('hex').toUpperCase();
        const expiresAt = time + CODE_LIFETIME_MS;
        pairings.set(code, expiresAt);
        return json(response, 201, { code, expiresAt });
      }
      if (method === 'POST' && path === '/api/pairing/claim') {
        if (lan && !loopback(request.socket.remoteAddress ?? '')) throw new HttpError(403, 'local_enrollment_only', 'Development pairing is available only on the server loopback connection.');
        const input = await body(request);
        if (!object(input) || !keysExactly(input, ['code']) || typeof input.code !== 'string' || !/^[A-F0-9]{12}$/.test(input.code)) throw new HttpError(400, 'invalid_request', 'A 12-character pairing code is required.');
        const expiresAt = pairings.get(input.code);
        if (!expiresAt || expiresAt <= now()) { pairings.delete(input.code); throw new HttpError(401, 'pairing_expired', 'The code is invalid, expired, or already used.'); }
        if (store.devices.length >= MAX_DEVICES) throw new HttpError(409, 'device_limit', 'The prototype supports eight paired devices.');
        const token = randomBytes(32).toString('base64url');
        const device: Device = { deviceId: `dv_${randomBytes(12).toString('hex')}`, tokenHash: sha256(token), seed: nextSeed(), revision: 0, legacy: null, initialMode: 'onboarding', events: [], receipts: [] };
        const initialState = replay([], device.seed, null, 'onboarding');
        persist({ formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices: [...store.devices, device] });
        pairings.delete(input.code);
        return json(response, 201, { deviceId: device.deviceId, token, revision: 0, seed: device.seed, state: initialState, autoTrace: null, events: [], baseSequence: 0 });
      }
      if (method === 'POST' && path === '/api/starter-offers') {
        authenticated(request);
        const input = await body(request);
        if (!object(input) || !keysExactly(input, [])) throw new HttpError(400, 'invalid_request', 'Starter offers expects an empty JSON object.');
        // Re-read after the body await. Replay and persistence are synchronous,
        // so concurrent first requests cannot draw or append a second offer set.
        let device = authenticated(request);
        let result = replayResult(device.events, device.seed, device.legacy, device.initialMode);
        if (result.state.phase !== 'egg') throw new HttpError(409, 'onboarding_complete', 'This companion has already hatched.');
        if ((result.state.onboarding as Record<string, unknown>).offerSeed === 0) {
          if (baseSequence(device.legacy) + device.events.length >= MAX_EVENTS) throw new HttpError(409, 'history_limit', 'The prototype lifetime event limit is reached.');
          const offerSeed = nextSeed();
          const events: Event[] = [{ type: 'starter-offer-seed', value: offerSeed }];
          const nextEvents = [...device.events, ...events];
          result = replayResult(nextEvents, device.seed, device.legacy, device.initialMode);
          const next: Device = { ...device, revision: device.revision + 1, events: nextEvents,
            receipts: [...device.receipts, { batchId: `starter-offers-${randomBytes(12).toString('hex')}`, bodyHash: batchHash(device.revision, events, 19), revision: device.revision + 1, eventEnd: nextEvents.length }] };
          persist({ formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices: store.devices.map(entry => entry.deviceId === device.deviceId ? next : entry) });
          device = next;
        }
        return json(response, 200, { deviceId: device.deviceId, revision: device.revision, seed: device.seed, ...result, events: device.events, baseSequence: baseSequence(device.legacy) });
      }
      if (method === 'POST' && path === '/api/world/seed') {
        authenticated(request);
        const input = await body(request);
        if (!object(input) || !keysExactly(input, [])) throw new HttpError(400, 'invalid_request', 'World initialization expects an empty JSON object.');
        // Replay/persist are synchronous after the body await: concurrent calls and
        // lost-response retries observe one durable seed, never a reroll. The browser
        // drains its outbox first; unchanged client batches keep their original receipts.
        let device = authenticated(request);
        let result = replayResult(device.events, device.seed, device.legacy, device.initialMode);
        if (result.state.phase === 'egg') throw new HttpError(409, 'onboarding_required', 'Hatch your starter before initializing its world.');
        if (result.state.worldSeed === 0) {
          if (baseSequence(device.legacy) + device.events.length >= MAX_EVENTS) throw new HttpError(409, 'history_limit', 'The prototype lifetime event limit is reached.');
          const events: Event[] = [{ type: 'world-seed', value: nextSeed() }];
          const nextEvents = [...device.events, ...events];
          result = replayResult(nextEvents, device.seed, device.legacy, device.initialMode);
          const next: Device = { ...device, revision: device.revision + 1, events: nextEvents,
            receipts: [...device.receipts, { batchId: `world-seed-${randomBytes(12).toString('hex')}`, bodyHash: batchHash(device.revision, events, 19), revision: device.revision + 1, eventEnd: nextEvents.length }] };
          persist({ formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices: store.devices.map(entry => entry.deviceId === device.deviceId ? next : entry) });
          device = next;
        }
        return json(response, 200, { deviceId: device.deviceId, revision: device.revision, seed: device.seed, ...result, events: device.events, baseSequence: baseSequence(device.legacy) });
      }
      if (method === 'GET' && path === '/api/save') {
        const device = authenticated(request);
        return json(response, 200, { deviceId: device.deviceId, revision: device.revision, seed: device.seed, ...replayResult(device.events, device.seed, device.legacy, device.initialMode), events: device.events, baseSequence: baseSequence(device.legacy) });
      }
      if (method === 'GET' && path === '/api/battle') {
        const device = authenticated(request);
        return json(response, 200, battleService().get(device.deviceId));
      }
      if (method === 'POST' && (path === '/api/battle/start' || path === '/api/battle/act')) {
        authenticated(request);
        const input = await body(request);
        const device = authenticated(request);
        if (path === '/api/battle/act') return json(response, 200, battleService().act(device.deviceId, input));
        const state = replay(device.events, device.seed, device.legacy, device.initialMode);
        if (state.phase === 'egg') throw new HttpError(409, 'onboarding_required', 'Hatch your starter before beginning a practice battle.');
        // The companion is always selected from the authoritative care save.
        // Client-provided names, levels, HP, rewards and RNG are never accepted.
        const member = (state.collection as Record<string, unknown>[]).find(entry => entry.id === state.activeCreatureId);
        if (!member) throw new HttpError(503, 'battle_profile_unavailable', 'The active companion could not be loaded.');
        const profile = { id: member.id, species: member.species, name: member.name, level: member.level, formId: member.formId } as BattleProfile;
        return json(response, 200, battleService().start(device.deviceId, input, profile));
      }
      if (method === 'POST' && path === '/api/save-sync') {
        authenticated(request);
        const input = await body(request);
        // Fetch again after body await: another request may have updated the store.
        const device = authenticated(request);
        if (object(input) && input.rulesVersion !== 19) throw new HttpError(409, 'migration_required', 'This batch uses older or unsupported rules. Preserve it and fetch the current save; do not relabel or replay it.');
        if (!object(input) || !keysExactly(input, ['rulesVersion', 'baseRevision', 'batchId', 'events']) || !Number.isSafeInteger(input.baseRevision) || Number(input.baseRevision) < 0 || typeof input.batchId !== 'string' || !/^[a-zA-Z0-9_-]{8,80}$/.test(input.batchId)) throw new HttpError(400, 'invalid_request', 'Supply rulesVersion:18, baseRevision, an 8–80 character batchId, and events.');
        if (!validEvents(input.events, MAX_EVENTS_PER_BATCH) || input.events.length === 0) throw new HttpError(422, 'invalid_events', 'Supply 1–100 supported, bounded events with exact type and value fields.');
        const events = input.events;
        if (events.some(event => event.type === 'world-seed')) throw new HttpError(422, 'server_owned_event', 'World initialization is generated by the trusted setup endpoint.');
        const bodyHash = batchHash(Number(input.baseRevision), events, 19);
        if (device.legacy?.histories.some(history => history.receipts.some(receipt => receipt.batchId === input.batchId))) throw new HttpError(409, 'legacy_batch_requires_reconciliation', 'This batch was committed under older rules. Fetch the current save; it will not be applied again.');
        const previous = device.receipts.find((entry) => entry.batchId === input.batchId);
        if (previous) {
          if (previous.bodyHash !== bodyHash) throw new HttpError(409, 'batch_mismatch', 'This batchId was already used for different content.');
          return json(response, 200, { deviceId: device.deviceId, revision: previous.revision, ...replayResult(device.events.slice(0, previous.eventEnd), device.seed, device.legacy, device.initialMode) });
        }
        if (input.baseRevision !== device.revision) throw new HttpError(409, 'revision_conflict', `Fetch the current save before retrying; current revision is ${device.revision}.`);
        // A receipt above acknowledges an already committed selection. Only new
        // selection batches consult this device's independent practice state.
        if (events.some(event => event.type === 'present-encounter') && battleService().get(device.deviceId).battle?.status === 'active') throw new HttpError(409, 'encounter_deferred', 'Finish the current practice battle before showing the waiting wild encounter.');
        if (events.some(event => event.type === 'select' || event.type === 'evolve' || event.type === 'evolve-member' || event.type === 'release' || event.type === 'party-add' || event.type === 'party-remove') && battleService().get(device.deviceId).battle?.status === 'active') {
          throw new HttpError(409, 'partner_locked', 'Finish or retreat from the current practice battle before changing the partner, XP companions, evolution or collection.');
        }
        if (baseSequence(device.legacy) + device.events.length + events.length > MAX_EVENTS) throw new HttpError(409, 'history_limit', 'The prototype lifetime event limit is reached; export and migrate the save.');
        const nextEvents = [...device.events, ...events];
        const result = replayResult(nextEvents, device.seed, device.legacy, device.initialMode);
        const next: Device = { ...device, revision: device.revision + 1, events: nextEvents, receipts: [...device.receipts, { batchId: input.batchId, bodyHash, revision: device.revision + 1, eventEnd: nextEvents.length }] };
        persist({ formatVersion: 21, gameSchemaVersion: 27, rulesVersion: 19, devices: store.devices.map((entry) => entry.deviceId === device.deviceId ? next : entry) });
        return json(response, 200, { deviceId: device.deviceId, revision: next.revision, ...result });
      }
      if (method === 'GET' && ASSETS.has(path)) {
        const [relativePath, mime] = ASSETS.get(path)!;
        const localPath = join(rootDir, relativePath);
        if (!existsSync(localPath)) throw new HttpError(404, 'not_found', 'This development asset is not installed.');
        const bytes = readFileSync(localPath);
        response.writeHead(200, { 'Content-Type': mime, 'Content-Length': bytes.length });
        response.end(bytes); return;
      }
      throw new HttpError(404, 'not_found', 'No such route.');
    } catch (error) {
      if (response.headersSent || response.destroyed) return;
      if (error instanceof HttpError || error instanceof BattleError) return json(response, error.status, { error: error.code, message: error.message });
      // Neither request bodies nor identity secrets are written to logs.
      json(response, 500, { error: 'internal_error', message: 'The local operation failed; no success was acknowledged. Check disk availability.' });
    }
  };
  return { handler, dataDir, recoveredFromBackup, migratedLegacyStore, closeGarage: () => { try { battles?.close(); } finally { garage?.close?.(); } } };
}

/** A data directory has one writer. A crashed process leaves a lock for deliberate recovery. */
export function createApp(options: AppOptions = {}) {
  const rootDir = resolve(options.rootDir ?? ROOT);
  const dataDir = resolve(options.dataDir ?? join(rootDir, '.data'));
  mkdirSync(dataDir, { recursive: true, mode: 0o700 });
  const lockPath = join(dataDir, '.writer.lock');
  let lock: number;
  try { lock = openSync(lockPath, 'wx', 0o600); }
  catch { throw new Error('Save directory is locked by another service or a crashed process. Stop all writers before manually removing .data/.writer.lock.'); }
  let released = false;
  const close = () => {
    if (released) return;
    released = true;
    if (existsSync(lockPath)) unlinkSync(lockPath);
  };
  try {
    writeFileSync(lock, JSON.stringify({ pid: process.pid, startedAt: new Date().toISOString() }));
    closeSync(lock);
    const app = createUnlockedApp({ ...options, rootDir, dataDir });
    return { ...app, close: () => { if (released) return; try { app.closeGarage(); } finally { close(); } } };
  } catch (error) {
    try { closeSync(lock); } catch { /* already closed */ }
    close();
    throw error;
  }
}

export async function startServer(options: AppOptions & { port?: number } = {}) {
  const lan = lanPolicy(options.lan);
  const includeTestFixtures = options.includeTestFixtures === true;
  const app = createApp({ ...options, lan });
  const server = createServer(app.handler);
  server.once('close', app.close);
  server.requestTimeout = 10_000;
  server.headersTimeout = 5_000;
  server.timeout = 10_000;
  server.maxHeadersCount = 32;
  server.maxConnections = 32;
  await new Promise<void>((resolve, reject) => {
    const failedToListen = (error: Error) => { app.close(); reject(error); };
    server.once('error', failedToListen);
    server.listen(options.port ?? 8787, lan?.bindAddress ?? '127.0.0.1', () => { server.off('error', failedToListen); resolve(); });
  });
  return { ...app, server };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const rawPort = process.env.PORT ?? '8787';
  const port = Number(rawPort);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('PORT must be 1–65535.');
  const lan = lanOptionsFromEnv(process.env, port);
  const { server } = await startServer({ port, lan, includeTestFixtures: process.env.DIGIVICE_TEST_FIXTURES === '1' });
  console.log(`Digivice ${lan ? 'LAN' : 'local'} development service: http://${lan?.bindAddress ?? '127.0.0.1'}:${port}`);
  if (lan) console.log('LAN HTTP development mode; no TLS or production trust. Pairing remains loopback-only; use existing device tokens.');
  const shutdown = () => server.close(() => process.exit(0));
  process.once('SIGINT', shutdown);
  process.once('SIGTERM', shutdown);
}

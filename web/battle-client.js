// Commands are durable before sending. Only the service/core decides outcomes.
import { validateAutoTrace } from './auto-battle.js';
export const BATTLE_PENDING_KEY = 'digivice.dev.battle.pending.v1';
export const BATTLE_ARCHIVE_KEY = 'digivice.dev.battle.archived.v1';
const MAX_PENDING = 2048, MAX_RESPONSE = 32768;
const DEVICE = /^dv_[a-f0-9]{24}$/, TOKEN = /^[A-Za-z0-9_-]{43}$/, REQUEST = /^[A-Za-z0-9_-]{16,64}$/;
const ACTIONS = new Set(['physical', 'heavy', 'magic', 'brace', 'counter', 'ward', 'card', 'retreat']);
const object = value => !!value && typeof value === 'object' && !Array.isArray(value);
const exact = (value, keys) => object(value) && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
const revision = value => Number.isSafeInteger(value) && value >= 0 && value < Number.MAX_SAFE_INTEGER;
const copy = value => value === null ? null : structuredClone(value);
const boundedInteger = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;
const MOVES = new Set(['physical', 'heavy', 'magic', 'brace', 'counter', 'ward']);
const moves = (value, max) => Array.isArray(value) && value.length <= max && value.every(move => MOVES.has(move));
const name = value => typeof value === 'string' && value.length > 0 && value.length <= 64;
function validAction(action) {
  return exact(action, ['type', 'value']) && ACTIONS.has(action.type) && (action.type === 'card' ? action.value === 1 || action.value === 2 : action.value === 0);
}
function validBody(path, body, rulesVersion = 7) {
  if (!object(body)) return false;
  const keys = path === '/api/battle/start' ? ['expectedRevision', 'requestId'] : ['expectedRevision', 'requestId', 'action'];
  if (rulesVersion >= 2) keys.push('rulesVersion');
  // Earlier v2 pending starts omitted mode; retain those exact bytes as Tactical.
  if (path === '/api/battle/start' && Object.hasOwn(body, 'mode')) {
    if (rulesVersion < 2 || !['tactical', 'auto'].includes(body.mode)) return false;
    keys.push('mode');
  }
  return exact(body, keys) && (rulesVersion === 1 || body.rulesVersion === rulesVersion) &&
    revision(body.expectedRevision) && typeof body.requestId === 'string' && REQUEST.test(body.requestId) && (path === '/api/battle/start' || validAction(body.action));
}
function parsePending(raw) {
  if (typeof raw !== 'string' || raw.length > MAX_PENDING) throw new Error('invalid pending');
  const value = JSON.parse(raw);
  if (!exact(value, ['formatVersion', 'rulesVersion', 'deviceId', 'path', 'body']) || value.formatVersion !== 1 || ![1, 2, 3, 4, 5, 6, 7].includes(value.rulesVersion) ||
    typeof value.deviceId !== 'string' || !DEVICE.test(value.deviceId) || !['/api/battle/start', '/api/battle/act'].includes(value.path) ||
    typeof value.body !== 'string' || !validBody(value.path, JSON.parse(value.body), value.rulesVersion)) throw new Error('invalid pending');
  return value;
}

const species = value => ['mote', 'flicker', 'rill', 'cinder', 'impmon', 'agumon', 'gabumon', 'patamon', 'tentomon', 'palmon', 'gomamon', 'renamon'].includes(value);
function validCombat(value) {
  return exact(value, ['maxHp', 'attack', 'defense', 'magic', 'resistance', 'type', 'skills']) &&
    boundedInteger(value.maxHp, 1, 400) && ['attack', 'defense', 'magic', 'resistance'].every(key => boundedInteger(value[key], 1, 128)) &&
    ['grove', 'tide', 'ember', 'neutral'].includes(value.type) && exact(value.skills, ['physical', 'heavy', 'magic']) &&
    Object.values(value.skills).every(name);
}

// Reject unknown fields so hidden core state can never become public UI state.
// These are schema/resource checks, never battle calculations.
function validateReply(value) {
  if (!exact(value, ['revision', 'battle', 'mode', 'autoTrace']) || !revision(value.revision) || !(value.battle === null || object(value.battle)) || !['tactical', 'auto'].includes(value.mode)) throw new Error('invalid reply');
  validateAutoTrace(value.autoTrace, 'practice');
  if (value.mode === 'auto' ? !value.autoTrace || value.battle?.phase !== 'finished' : value.autoTrace !== null) throw new Error('invalid mode result');
  const state = value.battle;
  if (state !== null) {
    const fortyExchanges = state.schemaVersion === state.rulesVersion && [5, 6, 7].includes(state.rulesVersion);
    const maximumExchanges = fortyExchanges ? 40 : 30;
    const expanded = state.schemaVersion === state.rulesVersion && [4, 5, 6, 7].includes(state.rulesVersion);
    const rpg = expanded || state.schemaVersion === 3 && state.rulesVersion === 3;
    const validSpecies = value => expanded ? typeof value === 'string' && /^[a-z][a-z0-9-]{0,63}$/.test(value) : species(value);
    const maximumLevel = rpg ? 20 : 3;
    const keys = ['schemaVersion', 'rulesVersion', 'sequence', 'phase', 'status', 'playerHp', 'enemyHp', 'playerLevel', 'enemyLevel', 'exchanges', 'cardUsed', 'attackBoost', 'shield', 'enemyHint', 'lastEnemyMoves', 'lastTurn', 'companion', 'enemy', 'playerSpecies', 'enemySpecies', 'playerCombat', 'enemyCombat'];
    if (fortyExchanges) keys.push('maxExchanges');
    if (rpg) keys.push('playerFormId', 'enemyFormId', 'playerFormName', 'enemyFormName');
    if (!exact(state, keys) || fortyExchanges && state.maxExchanges !== 40 || value.autoTrace?.steps.length > maximumExchanges || (!rpg && (state.schemaVersion !== 2 || state.rulesVersion !== 2)) ||
      rpg && (!boundedInteger(state.playerFormId, 1, expanded ? 512 : 66) || !boundedInteger(state.enemyFormId, 1, expanded ? 512 : 66) || !name(state.playerFormName) || !name(state.enemyFormName)) ||
      !validSpecies(state.playerSpecies) || !validSpecies(state.enemySpecies) || !validCombat(state.playerCombat) || !validCombat(state.enemyCombat) || !boundedInteger(state.sequence, 0, maximumExchanges + 2) ||
      !['attack', 'defend', 'finished'].includes(state.phase) || !['active', 'won', 'lost', 'draw', 'retreated'].includes(state.status) ||
      !boundedInteger(state.playerHp, 0, state.playerCombat.maxHp) || !boundedInteger(state.enemyHp, 0, state.enemyCombat.maxHp) || !boundedInteger(state.playerLevel, 1, maximumLevel) || !boundedInteger(state.enemyLevel, 1, maximumLevel) ||
      !boundedInteger(state.exchanges, 0, maximumExchanges) || typeof state.cardUsed !== 'boolean' || ![0, 5].includes(state.attackBoost) || !boundedInteger(state.shield, 0, 12) ||
      !moves(state.enemyHint, 2) || new Set(state.enemyHint).size !== state.enemyHint.length || !moves(state.lastEnemyMoves, 2) ||
      (state.phase === 'finished' ? state.status === 'active' || state.enemyHint.length !== 0 : state.status !== 'active' || state.enemyHint.length !== 2)) throw new Error('invalid battle state');
    if (state.lastTurn !== null && (!exact(state.lastTurn, ['phase', 'playerChoice', 'enemyChoice', 'playerDamage', 'enemyDamage', 'reflected']) ||
      !['attack', 'defend'].includes(state.lastTurn.phase) || !MOVES.has(state.lastTurn.playerChoice) || !MOVES.has(state.lastTurn.enemyChoice) ||
      !boundedInteger(state.lastTurn.playerDamage, 0, 400) || !boundedInteger(state.lastTurn.enemyDamage, 0, 400) || typeof state.lastTurn.reflected !== 'boolean')) throw new Error('invalid turn');
    if (!exact(state.companion, ['id', 'species', 'name', 'level', ...(rpg ? ['formId'] : [])]) || !boundedInteger(state.companion.id, 1, expanded ? 4294967294 : 8) ||
      rpg && (state.companion.formId !== state.playerFormId || state.companion.name !== state.playerFormName) ||
      !validSpecies(state.companion.species) || !name(state.companion.name) || !boundedInteger(state.companion.level, 1, maximumLevel) ||
      !exact(state.enemy, ['species', 'name']) || !validSpecies(state.enemy.species) || state.enemy.species !== state.enemySpecies || state.companion.species !== state.playerSpecies || state.companion.level !== state.playerLevel || !name(state.enemy.name)) throw new Error('invalid participants');
  }
  return value;
}

export function createBattleClient({ storage, fetcher = globalThis.fetch, onChange = () => {}, uuid = () => crypto.randomUUID(), requestTimeoutMs = 8000 } = {}) {
  if (!Number.isInteger(requestTimeoutMs) || requestTimeoutMs < 10 || requestTimeoutMs > 30000) throw new Error('Invalid battle request timeout.');
  const fetchRequest = (...args) => fetcher(...args);
  let backend, credential = null, battle = null, mode = 'tactical', autoTrace = null, currentRevision = null, loaded = false, busy = false;
  let rawPending = null, pending = null, recoveryRequired = false, commandRejected = false, reconciliationRejected = false, error = null, attempted = false;
  try { backend = storage ?? globalThis.localStorage; } catch { /* reported by readPending below */ }
  function readPending() {
    try {
      rawPending = backend.getItem(BATTLE_PENDING_KEY);
      pending = rawPending === null ? null : parsePending(rawPending);
      recoveryRequired = commandRejected || pending?.rulesVersion === 1;
      if (pending?.rulesVersion === 1) error = 'An older battle command is preserved. Review the current battle, then archive the old command to continue.';
      return true;
    } catch {
      pending = null; recoveryRequired = true;
      error = 'Saved battle data needs recovery. Existing data has been kept.';
      return false;
    }
  }
  function getState() {
    const hasPending = rawPending !== null || recoveryRequired;
    return { state: copy(battle), mode, autoTrace: copy(autoTrace), revision: currentRevision, loaded, busy, pending: hasPending, recoveryRequired,
      legacyPending: pending?.rulesVersion === 1 || [2, 3, 4, 5, 6].includes(pending?.rulesVersion) && reconciliationRejected,
      canArchiveLegacy: !busy && loaded && (pending?.rulesVersion === 1 || [2, 3, 4, 5, 6].includes(pending?.rulesVersion) && reconciliationRejected) && pending.deviceId === credential?.deviceId,
      error, ownerDeviceId: credential?.deviceId ?? pending?.deviceId ?? null, canSwitch: !busy && !hasPending };
  }
  function emit() { onChange(getState()); }
  function fail(message) { error = message; emit(); return getState(); }
  function applyReply(reply) {
    if (currentRevision === null || reply.revision >= currentRevision) { battle = copy(reply.battle); mode = reply.mode; autoTrace = copy(reply.autoTrace); currentRevision = reply.revision; }
  }
  async function request(path, body) {
    const controller = new AbortController(); let timer, reader;
    const timeout = new Promise((_, reject) => { timer = setTimeout(() => {
      controller.abort(); reader?.cancel().catch(() => {});
      reject(new Error('timeout'));
    }, requestTimeoutMs); });
    try {
      return await Promise.race([timeout, (async () => {
        const response = await fetchRequest(path, { method: body === undefined ? 'GET' : 'POST',
          headers: { Authorization: `Bearer ${credential.token}`, Accept: 'application/json', ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
          ...(body === undefined ? {} : { body }), cache: 'no-store', credentials: 'omit', signal: controller.signal });
        // Never relay upstream messages or diagnostic fields to the browser UI.
        if (!response.ok) {
          const failure = new Error('request failed'); failure.status = response.status;
          response.body?.cancel().catch(() => {}); throw failure;
        }
        if (Number(response.headers.get('content-length')) > MAX_RESPONSE || !response.body) { response.body?.cancel().catch(() => {}); throw new Error('invalid response'); }
        reader = response.body.getReader(); const chunks = []; let size = 0;
        while (true) {
          const result = await reader.read(); if (result.done) break;
          size += result.value.byteLength; if (size > MAX_RESPONSE) { reader.cancel().catch(() => {}); throw new Error('response limit'); }
          chunks.push(result.value);
        }
        const bytes = new Uint8Array(size); let offset = 0;
        for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
        return validateReply(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)));
      })()]);
    } finally {
      clearTimeout(timer); controller.abort();
      if (reader) { reader.cancel().catch(() => {}); try { reader.releaseLock(); } catch { /* already released */ } }
    }
  }
  async function load(nextCredential) {
    if (busy) return getState();
    if (!object(nextCredential) || typeof nextCredential.deviceId !== 'string' || !DEVICE.test(nextCredential.deviceId) || typeof nextCredential.token !== 'string' || !TOKEN.test(nextCredential.token)) return fail('Pair a virtual device before opening battle mode.');
    readPending();
    if ((pending && pending.deviceId !== nextCredential.deviceId) || (recoveryRequired && credential && credential.deviceId !== nextCredential.deviceId)) return fail('Resolve the saved battle command before changing playtests.');
    if (credential?.deviceId !== nextCredential.deviceId) { battle = null; mode = 'tactical'; autoTrace = null; currentRevision = null; }
    credential = { deviceId: nextCredential.deviceId, token: nextCredential.token };
    busy = true; loaded = false; if (!recoveryRequired) error = null; emit();
    try {
      const reply = await request('/api/battle');
      if (currentRevision !== null && reply.revision < currentRevision) throw new Error('stale reply');
      applyReply(reply); loaded = true;
    } catch { error = 'Battle service is unavailable or returned an invalid state. Existing data has been kept.'; }
    finally { busy = false; emit(); }
    return getState();
  }
  async function sendSaved({ refresh = false } = {}) {
    if (refresh) { const latest = await request('/api/battle'); applyReply(latest); loaded = true; }
    const savedRaw = rawPending, saved = pending;
    if (!saved || saved.deviceId !== credential.deviceId || backend.getItem(BATTLE_PENDING_KEY) !== savedRaw) throw new Error('pending changed');
    attempted = true;
    const reply = await request(saved.path, saved.body);
    if (reply.battle === null || reply.revision !== JSON.parse(saved.body).expectedRevision + 1 || reply.battle.rulesVersion !== saved.rulesVersion) throw new Error('invalid acknowledgment');
    applyReply(reply);
    if (backend.getItem(BATTLE_PENDING_KEY) !== savedRaw) throw new Error('pending changed');
    backend.removeItem(BATTLE_PENDING_KEY);
    if (backend.getItem(BATTLE_PENDING_KEY) !== null) throw new Error('pending not cleared');
    rawPending = null; pending = null; error = null;
  }
  function commandError(failure) {
    if ([409, 422].includes(failure?.status)) {
      commandRejected = true; recoveryRequired = true;
      reconciliationRejected = failure.status === 409;
      error = 'The service could not apply this saved battle command. It is kept for recovery; no replacement was sent.';
    } else error = attempted ? 'Battle response was not confirmed. The exact saved command is kept for retry.' : 'The battle command could not be saved or sent. Existing data has been kept.';
  }
  async function createCommand(path, action, mode) {
    if (busy) return getState();
    if (!readPending() || rawPending !== null) return fail('Resolve the saved battle command before starting another.');
    if (!credential || !loaded || currentRevision === null) return fail('Load the current battle state before sending a command.');
    if (path === '/api/battle/act' && !validAction(action)) return fail('Choose a supported battle action.');
    if (path === '/api/battle/act' && ![2, 3, 4, 5, 6, 7].includes(battle?.rulesVersion)) return fail('Load an existing battle before choosing a move.');
    if (path === '/api/battle/start' && !['tactical', 'auto'].includes(mode)) return fail('Choose Tactical or Auto before starting a battle.');
    busy = true; attempted = false; error = null; emit();
    try {
      const rulesVersion = path === '/api/battle/start' ? 7 : battle.rulesVersion;
      const body = { rulesVersion, expectedRevision: currentRevision, requestId: uuid(), ...(action ? { action: copy(action) } : { mode }) };
      if (!validBody(path, body, rulesVersion)) throw new Error('invalid request ID');
      const next = { formatVersion: 1, rulesVersion, deviceId: credential.deviceId, path, body: JSON.stringify(body) };
      const raw = JSON.stringify(next);
      if (raw.length > MAX_PENDING || backend.getItem(BATTLE_PENDING_KEY) !== null) throw new Error('pending changed');
      backend.setItem(BATTLE_PENDING_KEY, raw);
      if (backend.getItem(BATTLE_PENDING_KEY) !== raw) throw new Error('write not durable');
      rawPending = raw; pending = next; emit();
      await sendSaved();
    } catch (failure) { readPending(); commandError(failure); }
    finally { busy = false; emit(); }
    return getState();
  }
  async function retry() {
    if (busy) return getState();
    if (!readPending() || recoveryRequired) return fail('Saved battle data needs recovery before retry. Existing data has been kept.');
    if (!pending || !credential || pending.deviceId !== credential.deviceId) return fail('Load the playtest that owns the saved battle command before retrying.');
    busy = true; attempted = false; error = null; emit();
    try { await sendSaved({ refresh: true }); }
    catch (failure) { readPending(); commandError(failure); }
    finally { busy = false; emit(); }
    return getState();
  }
  async function archiveLegacyPending() {
    if (busy) return getState();
    if (!readPending() || !(pending?.rulesVersion === 1 || [2, 3, 4, 5, 6].includes(pending?.rulesVersion) && reconciliationRejected) || !loaded || pending.deviceId !== credential?.deviceId) return fail('Load and review the playtest that owns the older command first.');
    busy = true; error = null; emit();
    const saved = rawPending;
    try {
      // Reconcile against authoritative migrated state; never replay v1 under v2.
      const latest = await request('/api/battle');
      if (latest.revision < currentRevision) throw new Error('stale reply');
      applyReply(latest);
      if (backend.getItem(BATTLE_PENDING_KEY) !== saved) throw new Error('pending changed');
      const archived = backend.getItem(BATTLE_ARCHIVE_KEY);
      if (archived !== null && archived !== saved) throw new Error('archive occupied');
      backend.setItem(BATTLE_ARCHIVE_KEY, saved);
      if (backend.getItem(BATTLE_ARCHIVE_KEY) !== saved || backend.getItem(BATTLE_PENDING_KEY) !== saved) throw new Error('archive failed');
      backend.removeItem(BATTLE_PENDING_KEY);
      if (backend.getItem(BATTLE_PENDING_KEY) !== null) throw new Error('pending not cleared');
      rawPending = null; pending = null; recoveryRequired = false; commandRejected = false; reconciliationRejected = false;
    } catch { error = 'The old command could not be archived safely. Existing data has been kept for recovery.'; }
    finally { busy = false; emit(); }
    return getState();
  }
  readPending();
  return { getState, load, archiveLegacyPending, start: (mode = 'tactical') => createCommand('/api/battle/start', undefined, mode), act: action => createCommand('/api/battle/act', action), retry };
}

// Optional private artwork: two exact forms, fetched only when requested by the
// visible scene. No roster preload, persistence, source download or upload.
import { validatePersonalImport } from './personal-pack.js';
export const FORM_ART_LIMITS = Object.freeze({ packs: 2, responseBytes: 384 * 1024 });
const validId = id => Number.isInteger(id) && id >= 1 && id <= 512;
export function createFormArt({ getCredential, fetcher = (...args) => fetch(...args), onChange = () => {} } = {}) {
  const packs = new Map(), failed = new Set();
  let owner = null, wanted = [], enabled = true, running = false, controller = null;
  function current() {
    const credential = getCredential?.();
    const next = credential?.deviceId || null;
    if (next !== owner) { owner = next; packs.clear(); failed.clear(); controller?.abort(); }
    return credential;
  }
  function get(id) { return enabled && validId(id) ? packs.get(id) || null : null; }
  function state() { return { enabled, loaded: [...packs.keys()], pending: running, unavailable: [...failed], requested: [...wanted] }; }
  function emit() { onChange(state()); }
  async function read(response) {
    if (!response.ok || !response.body || Number(response.headers.get('content-length')) > FORM_ART_LIMITS.responseBytes) {
      response.body?.cancel().catch(() => {}); throw new Error('Artwork unavailable');
    }
    const reader = response.body.getReader(); const chunks = []; let length = 0;
    try {
      while (true) {
        const { done, value } = await reader.read(); if (done) break;
        length += value.byteLength; if (length > FORM_ART_LIMITS.responseBytes) throw new Error('Artwork too large'); chunks.push(value);
      }
    } finally { reader.cancel().catch(() => {}); reader.releaseLock(); }
    const bytes = new Uint8Array(length); let offset = 0;
    for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
    return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
  }
  async function pump() {
    if (running || !enabled) return;
    const credential = current(); if (!credential?.token) return;
    const id = wanted.find(candidate => !packs.has(candidate) && !failed.has(candidate));
    if (id === undefined) return;
    running = true; controller = new AbortController(); const activeController = controller;
    const timer = setTimeout(() => activeController.abort(), 8000);
    try {
      const response = await fetcher(`/api/roster/art/${id}`, { headers: { Authorization: `Bearer ${credential.token}`, Accept: 'application/json' },
        credentials: 'omit', cache: 'no-store', signal: activeController.signal });
      const body = await read(response);
      if (body.formId !== id || body.artId !== `ds-form-${id}` || !Number.isInteger(body.version) || body.version < 1 || !/^[a-f0-9]{64}$/.test(body.sha256)) throw new Error('Artwork identity mismatch');
      const result = await validatePersonalImport(body.packText, body.provenanceText, { expectedArtId: body.artId });
      if (result.pack.version !== body.version || result.provenance.packSha256 !== body.sha256) throw new Error('Artwork version mismatch');
      if (!enabled || current()?.deviceId !== credential.deviceId || !wanted.includes(id)) return;
      while (packs.size >= FORM_ART_LIMITS.packs) packs.delete(packs.keys().next().value);
      packs.set(id, result.pack);
    } catch { if (owner === credential.deviceId && wanted.includes(id)) failed.add(id); }
    finally { clearTimeout(timer); running = false; if (controller === activeController) controller = null; emit(); void pump(); }
  }
  function select(ids) {
    current(); wanted = [...new Set(ids.filter(validId))].slice(0, FORM_ART_LIMITS.packs);
    // Negative cache is bounded to the current two requested forms as well.
    for (const id of failed) if (!wanted.includes(id)) failed.delete(id);
    for (const id of packs.keys()) if (!wanted.includes(id)) packs.delete(id);
    void pump();
  }
  function setEnabled(value) { enabled = Boolean(value); if (!enabled) { controller?.abort(); packs.clear(); } else { failed.clear(); void pump(); } emit(); }
  function retry() { failed.clear(); void pump(); }
  return { select, get, state, setEnabled, retry };
}

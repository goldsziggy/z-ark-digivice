import { PERSONAL_LIMITS } from './personal-pack.js';

// The browser speaks only to its paired, same-origin service. S3 credentials,
// bucket names and object keys never cross this boundary.
// JSON string wrapping can double already-escaped source JSON bytes.
const MAX_RESPONSE = 2 * (PERSONAL_LIMITS.packBytes + PERSONAL_LIMITS.provenanceBytes) + 64;
const validId = value => typeof value === 'string' && /^[a-z0-9][a-z0-9-]{0,47}$/.test(value);
const validVersion = value => Number.isSafeInteger(value) && value >= 1 && value <= 0x7fffffff;

export function validateGarageCatalog(value) {
  if (!value || value.status !== 'available' || !Array.isArray(value.packs) || value.packs.length > 8) throw new Error('The private asset catalog is invalid.');
  const ids = new Set();
  for (const pack of value.packs) {
    const key = `${pack?.id}/${pack?.version}`;
    if (!pack || !validId(pack.id) || !validVersion(pack.version) || !['original', 'personal'].includes(pack.kind)
      || !Number.isSafeInteger(pack.bytes) || pack.bytes < 1 || pack.bytes > PERSONAL_LIMITS.packBytes
      || typeof pack.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(pack.sha256) || ids.has(key)) throw new Error('The private asset catalog is invalid.');
    ids.add(key);
  }
  return value;
}

export async function readBoundedJSON(response, limit = MAX_RESPONSE) {
  if (!response.body) throw new Error('The local asset service returned an empty response.');
  const reader = response.body.getReader();
  const chunks = []; let length = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      length += value.byteLength;
      if (length > limit) throw new Error('The local asset response exceeds its limit.');
      chunks.push(value);
    }
  } catch (error) { await reader.cancel().catch(() => {}); throw error; }
  finally { reader.releaseLock(); }
  const bytes = new Uint8Array(length); let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
  try { return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)); }
  catch { throw new Error('The local asset service returned invalid JSON.'); }
}

export function setupGarageLibrary({ getCredential, importPersonal }) {
  const anchor = document.getElementById('personal-status');
  if (!anchor || typeof getCredential !== 'function' || typeof importPersonal !== 'function') return;
  let busy = false;
  const section = document.createElement('div');
  section.className = 'garage-library';
  const heading = document.createElement('h3'); heading.textContent = 'Private Garage assets';
  const description = document.createElement('p');
  description.textContent = 'Check the configured private store, then save a personal appearance pack in this browser.';
  const check = document.createElement('button'); check.type = 'button'; check.id = 'garage-check'; check.className = 'button secondary'; check.textContent = 'Check private Garage';
  const choices = document.createElement('div'); choices.id = 'garage-packs';
  const status = document.createElement('p'); status.id = 'garage-status'; status.setAttribute('role', 'status');
  status.textContent = 'Optional. Local file imports and original packs work without Garage.';
  section.append(heading, description, check, choices, status);
  anchor.after(section);

  function updateBusy(value) {
    busy = value; check.disabled = value;
    for (const button of choices.querySelectorAll('button')) button.disabled = value;
  }
  async function request(path, limit) {
    const credential = getCredential();
    if (!credential?.token) throw new Error('Pair a virtual device before checking private assets.');
    const response = await fetch(path, { headers: { Accept: 'application/json', Authorization: `Bearer ${credential.token}` }, cache: 'no-store', signal: AbortSignal.timeout(10000) });
    const value = await readBoundedJSON(response, limit);
    if (!response.ok) throw new Error(typeof value?.message === 'string' ? value.message.slice(0, 300) : 'Private Garage assets are unavailable.');
    return value;
  }
  function failure(error) {
    status.textContent = `${error.name === 'TimeoutError' || error.name === 'AbortError' ? 'The private asset request timed out.' : error instanceof TypeError ? 'The local asset service is unavailable.' : error.message} Existing artwork is retained; you can retry.`;
  }
  async function install(pack) {
    if (busy) return;
    updateBusy(true); status.textContent = 'Fetching and verifying the personal pack…';
    try {
      const pair = await request(`/api/garage/personal/${pack.id}/${pack.version}`, MAX_RESPONSE);
      // Recheck the catalog pin as well as the detailed pack/provenance contract.
      const bytes = new TextEncoder().encode(pair?.packText);
      if (typeof pair?.packText !== 'string' || typeof pair?.provenanceText !== 'string' || bytes.byteLength !== pack.bytes) throw new Error('The personal pack does not match the catalog.');
      const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), value => value.toString(16).padStart(2, '0')).join('');
      if (hash !== pack.sha256) throw new Error('The personal pack does not match the catalog checksum.');
      await importPersonal(pair.packText, pair.provenanceText);
      status.textContent = 'Private artwork saved in this browser. It can be reused without another Garage download.';
    } catch (error) { failure(error); }
    finally { updateBusy(false); }
  }
  check.addEventListener('click', async () => {
    if (busy) return;
    updateBusy(true); status.textContent = 'Checking private asset storage…';
    try {
      const catalog = validateGarageCatalog(await request('/api/garage/catalog', 16384));
      const personal = catalog.packs.filter(pack => pack.kind === 'personal');
      choices.replaceChildren();
      for (const pack of personal) {
        const button = document.createElement('button'); button.type = 'button'; button.className = 'button secondary'; button.disabled = true;
        button.textContent = `Use ${pack.id} · version ${pack.version}`;
        button.addEventListener('click', () => install(pack)); choices.append(button);
      }
      status.textContent = personal.length ? 'Private storage verified. Choose a personal pack to load.' : 'Private storage verified. No personal appearance pack is configured. Original art is available in the Pack library.';
    } catch (error) { failure(error); }
    finally { updateBusy(false); }
  });
}

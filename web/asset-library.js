import { isPersonalPack } from './personal-pack.js';
import { AssetCache, decodeFrame, decodePersonalFrame, verifyManifest } from './asset-cache.js';
import { IndexedDBAssetStore } from './asset-store.js';
import { ASSET_PUBLIC_KEY } from './asset-public-key.js';

const $ = id => document.getElementById(id);
const scenes = ['meadow', 'forest', 'beach', 'ruins', 'cavern', 'snow', 'volcanic', 'digital'];
const names = { 'starter-v2': 'Little Forest', 'tide-v1': 'Tidal Pools', 'ember-v1': 'Ember Hollow',
  ...Object.fromEntries(scenes.map(id => [`scene-${id}-v1`, `${id[0].toUpperCase()}${id.slice(1)} scenery`])) };
const fixturePackIds = new Set(['starter-v1', 'starter-v2', 'tide-v1', 'ember-v1']);
export function visibleAssetPacks(packs, { includeTestFixtures = false } = {}) {
  return Object.fromEntries(Object.entries(packs).filter(([id]) => includeTestFixtures || !fixturePackIds.has(id)));
}
const sizes = bytes => `${(bytes / 1024).toFixed(1)} KiB`;

// Decode outside the animation loop; only current companion/wild/gallery clips
// are resident. Each 32px frame is 4 KiB in this browser, 2 KiB as device RGB565.
export function prepareFrames(pack, category, id, animation = 'idle') {
  const item = pack?.[category]?.[id];
  const clip = item?.animations?.[animation] || item?.animations?.idle;
  if (!clip) return null;
  const key = item.animations[animation] ? animation : 'idle';
  return { frameMs: clip.frameMs, frames: clip.frames.map((_, index) => {
    const decoded = (isPersonalPack(pack) ? decodePersonalFrame : decodeFrame)(pack, category, id, key, index);
    const surface = document.createElement('canvas');
    surface.width = decoded.width; surface.height = decoded.height;
    surface.getContext('2d').putImageData(new ImageData(decoded.data, decoded.width, decoded.height), 0, 0);
    return surface;
  }) };
}

async function fetchCatalog() {
  const response = await fetch('/api/assets/catalog', { cache: 'no-store', signal: AbortSignal.timeout(12000) });
  if (!response.ok || !response.body) throw new Error('Pack catalog unavailable. Saved art still works.');
  const reader = response.body.getReader();
  const chunks = []; let length = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    length += value.byteLength;
    if (length > 32768) { await reader.cancel(); throw new Error('Pack catalog exceeds its limit.'); }
    chunks.push(value);
  }
  const bytes = new Uint8Array(length); let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
  return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
}

export async function setupAssets(onStarter, onState = () => {}, { includeTestFixtures = false } = {}) {
  const visibleNames = visibleAssetPacks(names, { includeTestFixtures });
  let cache;
  let catalog = null;
  let manifest = null;
  let controller = null;
  let gallery = null;
  let galleryFrames = null;
  let lastDraw = 0;
  let reducedMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
  let transfer = { id: null, phase: 'idle', received: 0, total: 0, resumed: false };
  const screen = $('asset-display');
  const ctx = screen.getContext('2d');
  const getState = () => ({ ready: Boolean(cache), busy: Boolean(controller), selectedId: $('asset-pack-select').value,
    message: $('asset-status').textContent, ...transfer,
    packs: Object.entries(visibleNames).map(([id, name]) => ({ id, name, installed: Boolean(cache?.getPack(id)),
      kind: id.startsWith('scene-') ? 'background' : 'sprites', bytes: manifest?.packs.find(entry => entry.id === id)?.bytes || 0 })) });
  const emit = () => onState(getState());
  const status = message => { $('asset-status').textContent = message; emit(); };
  $('asset-pack-select').replaceChildren(...Object.entries(visibleNames).map(([id, name]) => new Option(name, id)));

  function notifyPacks() {
    const available = visibleAssetPacks(Object.fromEntries((cache?.list() || []).map(({ id }) => [id, cache.getPack(id)]).filter(([, pack]) => pack)), { includeTestFixtures });
    onStarter(available['starter-v2'] || null, available);
  }

  function chooseCreature() {
    if (!gallery?.sprites) return;
    const id = $('asset-creature-select').value;
    const spec = gallery.sprites[id];
    if (!spec) return;
    galleryFrames = prepareFrames(gallery, 'sprites', id, $('asset-animation-select').value);
    $('asset-creature-name').textContent = spec.name;
    $('asset-creature-detail').textContent = `${names[gallery.packId]} · ${spec.width} × ${spec.height} pixels · stage ${spec.stage}`;
    screen.setAttribute('aria-label', `${spec.name} performing ${$('asset-animation-select').value}, original ${spec.width}-pixel artwork`);
  }

  function choosePack() {
    const id = $('asset-pack-select').value;
    gallery = cache?.getPack(id) || null;
    const entry = manifest?.packs.find(pack => pack.id === id);
    $('asset-pack-detail').textContent = `${entry ? sizes(entry.bytes) + ' download · ' : ''}${gallery ? 'Saved locally' : 'Available to download'}${id === 'starter-v2' ? ' · test fixture' : ''}`;
    $('asset-download').disabled = Boolean(controller) || !cache;
    $('asset-download').textContent = gallery ? 'Check for pack update' : 'Download / resume pack';
    if (!controller && id === transfer.id && ['paused', 'error'].includes(transfer.phase)) { $('asset-progress').max = transfer.total || 1; $('asset-progress').value = transfer.received; }
    else if (!controller) { $('asset-progress').max = 1; $('asset-progress').value = gallery ? 1 : 0; }
    const hasSprites = Boolean(gallery?.sprites);
    $('asset-creature-select').disabled = !hasSprites;
    $('asset-animation-select').disabled = !hasSprites;
    $('asset-creature-select').replaceChildren();
    if (hasSprites) {
      for (const [id, sprite] of Object.entries(gallery.sprites)) {
        const option = new Option(sprite.name, id); $('asset-creature-select').append(option);
      }
      chooseCreature();
    } else {
      galleryFrames = null;
      $('asset-creature-name').textContent = names[id];
      $('asset-creature-detail').textContent = gallery?.formatVersion === 3 ? '480 × 480 scenery · shown on your device when this scene is selected' : id.startsWith('scene-') ? 'Download to add this cosmetic scenery' : 'Download to preview this family';
    }
    emit();
  }

  async function install(id) {
    if (controller || !cache || !Object.hasOwn(visibleNames, id)) return false;
    controller = new AbortController();
    transfer = { id, phase: 'loading', received: 0, total: 0, resumed: false };
    let complete = false;
    $('asset-download').disabled = true;
    $('asset-cancel').hidden = false;
    $('asset-pack-select').disabled = true;
    status('Checking the pack catalog…');
    try {
      // Refresh on a user request; pinned cached packs do not need this to draw.
      if (!catalog) { catalog = await fetchCatalog(); manifest = await verifyManifest(catalog, ASSET_PUBLIC_KEY); }
      await cache.installPack(catalog, id, {
        signal: controller.signal,
        onProgress: ({ received, total, resumed }) => {
          transfer = { id, phase: 'downloading', received, total, resumed };
          $('asset-progress').max = total; $('asset-progress').value = received;
          status(`${resumed ? 'Resuming' : 'Downloading'} ${names[id]} · ${sizes(received)} / ${sizes(total)}`);
        },
      });
      notifyPacks();
      transfer.phase = 'ready'; complete = true;
      status(`${names[id]} is verified and saved. It can be previewed without another download.`);
    } catch (error) {
      transfer.phase = controller.signal.aborted ? 'paused' : 'error';
      status(controller.signal.aborted ? 'Download paused. Choose Download / resume to continue. Your previous art is safe.' : `${error.message} Previous art remains available; you can retry.`);
      // A future retry should learn about a changed manifest, while the cache
      // enforces anti-downgrade and still resumes matching staged bytes.
      catalog = null;
    } finally {
      controller = null;
      $('asset-cancel').hidden = true;
      $('asset-pack-select').disabled = false;
      choosePack();
    }
    return complete;
  }

  function draw(time) {
    if (time - lastDraw >= 100) {
      lastDraw = time;
      ctx.fillStyle = '#182d3b'; ctx.fillRect(0, 0, 320, 256);
      ctx.fillStyle = '#254451'; ctx.beginPath(); ctx.ellipse(160, 204, 90, 17, 0, 0, Math.PI * 2); ctx.fill();
      ctx.fillStyle = '#a3dabe';
      for (const [x,y] of [[49,79],[271,102],[86,159],[239,61]]) { ctx.fillRect(x,y,2,2); }
      if (galleryFrames) {
        const frame = galleryFrames.frames[reducedMotion ? 0 : Math.floor(time / galleryFrames.frameMs) % galleryFrames.frames.length];
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(frame, (320 - frame.width * 5) / 2, 47, frame.width * 5, frame.height * 5);
      } else {
        ctx.font = '13px ui-monospace, Menlo, monospace'; ctx.textAlign = 'center';
        ctx.fillStyle = '#d9e8d5'; ctx.fillText('A NEW PLACE TO EXPLORE', 160, 133);
      }
    }
    requestAnimationFrame(draw);
  }
  requestAnimationFrame(draw);
  $('asset-pack-select').addEventListener('change', choosePack);
  $('asset-creature-select').addEventListener('change', chooseCreature);
  $('asset-animation-select').addEventListener('change', chooseCreature);
  $('asset-download').addEventListener('click', () => install($('asset-pack-select').value));
  const pause = () => {
    if (!controller) return;
    // A warmup download can differ from the pack the user last browsed. Keep
    // the interrupted pack selected so its next action is the correct Resume.
    if (transfer.id) $('asset-pack-select').value = transfer.id;
    controller.abort();
  };
  $('asset-cancel').addEventListener('click', pause);
  try {
    cache = new AssetCache({ storage: new IndexedDBAssetStore(), fetcher: fetch, publicKey: ASSET_PUBLIC_KEY });
    await cache.init();
    const starter = includeTestFixtures ? cache.getPack('starter-v2') : null;
    notifyPacks();
    if (Object.keys(visibleAssetPacks(Object.fromEntries(cache.list().map(({ id }) => [id, true])), { includeTestFixtures })).length) status('Saved packs restored and verified. Ready without an asset download.');
    choosePack();
    // Warm scenery through the very same bounded, verified, resumable cache.
    // Stop after a pause/error; only an explicit request starts that transfer again.
    void (async () => {
      if (includeTestFixtures) {
        if (!starter && !await install('starter-v2')) return;
        for (const id of ['tide-v1', 'ember-v1']) {
          if (!cache.getPack(id) && !await install(id)) return;
        }
      }
      for (const scene of ['meadow', 'digital', ...scenes.filter(id => !['meadow', 'digital'].includes(id))]) {
        const id = `scene-${scene}-v1`;
        if (!cache.getPack(id) && !await install(id)) break;
      }
    })();
  } catch (error) {
    cache = null;
    status(`Asset storage is unavailable: ${error.message} Your partner remains identified by name when art is unavailable.`);
    choosePack();
  }
  return { getState, install, pause, select(id) { if (Object.hasOwn(visibleNames, id)) { $('asset-pack-select').value = id; choosePack(); } } };
}

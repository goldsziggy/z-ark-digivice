import { backgroundJpegBytes, BACKGROUND_SCENES } from './background-pack.js';

// Cosmetic scene IDs only: these never modify game RNG, stats or encounters.
export { BACKGROUND_SCENES };
export const BACKGROUND_FRAME_BYTES = 480 * 480 * 4;
const sceneSet = new Set(BACKGROUND_SCENES);

async function decodeJpeg(pack) {
  // Re-check the bounded JPEG header before entering the browser decoder.
  const bytes = backgroundJpegBytes(pack);
  return createImageBitmap(new Blob([bytes], { type: 'image/jpeg' }));
}

/** One retained bitmap, one serialized decoder, latest requested scene wins.
 * Encoded packs belong to the existing signed AssetCache, not this renderer.
 * During atomic replacement the old and new RGBA bitmaps briefly coexist;
 * decoder scratch/browser overhead are additional and are not measured here.
 */
export function createBackgroundPlayer({ onChange = () => {}, decode = decodeJpeg } = {}) {
  let packs = new Map(), id = 'meadow', current = null, flight = null;
  let status = 'fallback', failed = null, destroyed = false, peakDecodedBytes = 0;
  const packFor = scene => packs.get(`scene-${scene}-v1`) || null;
  const close = bitmap => { try { bitmap?.close?.(); } catch { /* Release is best effort after disposal. */ } };
  function getState() {
    return { id, status, visibleId: current?.id === id && current.pack === packFor(id) ? id : null,
      decodedBytes: current ? BACKGROUND_FRAME_BYTES : 0, peakDecodedBytes,
      inFlight: flight ? 1 : 0, requestedPackId: `scene-${id}-v1` };
  }
  function emit() { if (!destroyed) onChange(getState()); }
  function reconcile() {
    if (destroyed) return;
    const pack = packFor(id);
    const ready = current?.id === id && current.pack === pack;
    const rejected = failed?.id === id && failed.pack === pack;
    const nextStatus = ready ? 'ready' : !pack ? 'fallback' : rejected ? 'error' : 'loading';
    if (status !== nextStatus) { status = nextStatus; emit(); }
    if (ready || !pack || rejected || flight) return;
    const job = { id, pack };
    flight = job; emit();
    Promise.resolve().then(() => decode(job.pack)).then(bitmap => {
      if (!bitmap || bitmap.width !== 480 || bitmap.height !== 480 || typeof bitmap.close !== 'function') {
        close(bitmap); throw new Error('Background decoder returned an invalid frame.');
      }
      peakDecodedBytes = Math.max(peakDecodedBytes, (current ? BACKGROUND_FRAME_BYTES : 0) + BACKGROUND_FRAME_BYTES);
      if (destroyed || id !== job.id || packFor(id) !== job.pack) { close(bitmap); return; }
      close(current?.bitmap);
      current = { ...job, bitmap };
      failed = null;
    }).catch(() => {
      if (!destroyed && id === job.id && packFor(id) === job.pack) failed = job;
    }).finally(() => {
      flight = null;
      reconcile(); emit();
    });
  }
  return {
    setPacks(available) {
      if (destroyed) return;
      const next = new Map();
      for (const scene of BACKGROUND_SCENES) {
        const key = `scene-${scene}-v1`, pack = available?.[key];
        if (pack?.formatVersion === 3 && pack.packId === key && pack.background?.sceneId === scene) next.set(key, pack);
      }
      packs = next;
      reconcile();
    },
    select(scene) {
      if (destroyed || !sceneSet.has(scene)) return false;
      if (scene !== id) { id = scene; failed = null; }
      reconcile(); return true;
    },
    retry() { if (!destroyed) { failed = null; reconcile(); } },
    draw(context, x = 0, y = 0, width = 480, height = 480) {
      if (destroyed || !current || current.id !== id || current.pack !== packFor(id)) return false;
      context.drawImage(current.bitmap, x, y, width, height); return true;
    },
    getState,
    destroy() {
      destroyed = true; close(current?.bitmap); current = null; packs.clear();
      // An in-flight browser decode cannot be cancelled; its result is closed
      // without adoption or notification when its promise settles above.
      status = 'fallback'; failed = null;
    },
  };
}

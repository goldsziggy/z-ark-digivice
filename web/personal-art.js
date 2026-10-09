import { IndexedDBAssetStore } from './asset-store.js';
import { prepareFrames } from './asset-library.js';
import { PERSONAL_LIMITS, validatePersonalImport } from './personal-pack.js';

const $ = id => document.getElementById(id);

export async function setupPersonalArt(onChange) {
  const storage = new IndexedDBAssetStore({ name: 'digivice-personal-art-v1' });
  let record = null;
  let loaded = null;
  let frames = null;
  let busy = false;
  let lastDraw = 0;
  const canvas = $('personal-display');
  if (!canvas) { onChange(null); return; }
  const ctx = canvas.getContext('2d');
  const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const status = message => { $('personal-status').textContent = message; };

  function renderPreview() {
    frames = null;
    if (!loaded) return;
    const id = $('personal-creature').value;
    const animation = $('personal-animation').value;
    const sprite = loaded.pack.sprites[id];
    if (!sprite) return;
    frames = prepareFrames(loaded.pack, 'sprites', id, animation);
    const clip = loaded.provenance.coverage[id][animation];
    $('personal-name').textContent = sprite.name;
    $('personal-coverage').textContent = clip.kind === 'genuine'
      ? `${animation}: ${clip.originalFrameCount} source frame${clip.originalFrameCount === 1 ? '' : 's'} · ${clip.sourceDescription || 'mapped source poses'} · ${sprite.width} × ${sprite.height}`
      : `${animation}: reuses ${clip.sourceAnimation}; no separate source clip imported`;
    canvas.setAttribute('aria-label', `${sprite.name} appearance preview. ${$('personal-coverage').textContent}`);
  }
  function render() {
    $('personal-use').disabled = !loaded || busy;
    $('personal-use').checked = Boolean(record?.enabled && loaded);
    $('personal-remove').disabled = !loaded || busy;
    $('personal-import').disabled = busy;
    $('personal-creature').disabled = !loaded;
    $('personal-animation').disabled = !loaded;
    $('personal-creature').replaceChildren();
    if (loaded) {
      for (const [id, sprite] of Object.entries(loaded.pack.sprites)) $('personal-creature').append(new Option(sprite.name, id));
      const provenance = loaded.provenance.provenance;
      $('personal-source').textContent = `${provenance.game} · ${provenance.creator}`;
      $('personal-source').href = provenance.sourceUrl;
      $('personal-source').hidden = false;
      $('personal-rights').textContent = `${provenance.rights} ${provenance.reuseScope}`;
      renderPreview();
    } else {
      frames = null;
      $('personal-source').hidden = true;
      $('personal-rights').textContent = 'Importing a sheet does not grant rights to its characters or artwork. Keep restricted personal packs out of public distribution.';
      $('personal-name').textContent = 'Your personal appearance pack';
      $('personal-coverage').textContent = 'Original art remains the default.';
    }
  }
  function notify() { onChange(record?.enabled && loaded ? loaded.pack : null); }
  async function save(next) {
    // Pack, provenance and enabled preference commit together. This store is
    // separate from signed packs, identities and pet state. No network calls.
    await storage.transaction(draft => { draft.personal = next; });
    record = next;
  }
  async function commitImport(packText, provenanceText) {
    const candidate = await validatePersonalImport(packText, provenanceText);
    await save({ packText, provenanceText, enabled: true });
    loaded = candidate;
  }
  async function importTexts(packText, provenanceText) {
    if (busy) throw new Error('An appearance change is already in progress.');
    busy = true; render();
    try {
      await commitImport(packText, provenanceText);
      status('Personal artwork verified and saved in this browser. Game rules and pet saves are unchanged.');
    } finally { busy = false; render(); notify(); }
  }
  async function importFiles() {
    if (busy) return;
    const files = Array.from($('personal-files').files || []);
    if (files.length !== 2) { status('Choose both generated files: pack.json and provenance.json.'); return; }
    if (files.some(file => file.size > PERSONAL_LIMITS.packBytes) || files.reduce((sum, file) => sum + file.size, 0) > PERSONAL_LIMITS.packBytes + PERSONAL_LIMITS.provenanceBytes) {
      status('The selected files exceed the bounded personal-pack limits.'); return;
    }
    busy = true; render();
    try {
      let packText, provenanceText;
      for (const file of files) {
        const text = await file.text();
        let json;
        try { json = JSON.parse(text); } catch { throw new Error('Both files must be generated JSON.'); }
        if (json?.sprites) { if (packText) throw new Error('Choose one pack and one provenance file.'); packText = text; }
        else if (json?.provenance) { if (provenanceText) throw new Error('Choose one pack and one provenance file.'); provenanceText = text; }
      }
      await commitImport(packText, provenanceText);
      status('Imported and enabled in this browser only. Game rules and pet saves are unchanged.');
    } catch (error) { status(`${error.message} Previous artwork is retained.`); }
    finally { busy = false; render(); notify(); }
  }
  async function changeEnabled() {
    if (!record || !loaded || busy) return;
    busy = true;
    const enabled = $('personal-use').checked;
    render();
    // Keep the user's immediate checkbox state while its preference commits.
    $('personal-use').checked = enabled;
    try { await save({ ...record, enabled }); status(enabled ? 'Personal appearances enabled. Original game rules still apply.' : 'Original appearances restored. Your personal pack is still saved.'); }
    catch (error) { status(`Could not save the appearance preference: ${error.message}`); }
    finally { busy = false; render(); notify(); }
  }
  async function remove() {
    if (busy || !loaded) return;
    busy = true; render();
    try { await save(null); loaded = null; status('Personal art removed from this browser. Original art and pet saves are unchanged.'); }
    catch (error) { status(`Could not remove personal art: ${error.message}`); }
    finally { busy = false; render(); notify(); }
  }
  function draw(time) {
    if (time - lastDraw >= 100) {
      lastDraw = time;
      ctx.fillStyle = '#162b3a'; ctx.fillRect(0, 0, 320, 256);
      ctx.fillStyle = '#284754'; ctx.beginPath(); ctx.ellipse(160, 220, 86, 12, 0, 0, Math.PI * 2); ctx.fill();
      if (frames) {
        const frame = frames.frames[reducedMotion ? 0 : Math.floor(time / frames.frameMs) % frames.frames.length];
        const scale = frame.width === 64 ? 3 : 5;
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(frame, Math.floor((320 - frame.width * scale) / 2), 220 - frame.height * scale, frame.width * scale, frame.height * scale);
      } else {
        ctx.fillStyle = '#bdcfc8'; ctx.font = '12px ui-monospace, Menlo, monospace'; ctx.textAlign = 'center'; ctx.fillText('LOCAL FILES · OPTIONAL ART', 160, 128);
      }
    }
    requestAnimationFrame(draw);
  }
  $('personal-import').addEventListener('click', importFiles);
  $('personal-use').addEventListener('change', changeEnabled);
  $('personal-remove').addEventListener('click', remove);
  $('personal-creature').addEventListener('change', renderPreview);
  $('personal-animation').addEventListener('change', renderPreview);
  render(); requestAnimationFrame(draw);
  try {
    const state = await storage.read();
    if (state.personal) {
      if (typeof state.personal.enabled !== 'boolean') throw new Error('Invalid saved appearance preference.');
      loaded = await validatePersonalImport(state.personal.packText, state.personal.provenanceText);
      record = state.personal; render(); notify();
      status(record.enabled ? 'Personal appearances restored from this browser.' : 'Personal pack saved; original appearances selected.');
    }
  } catch (error) { status(`Personal artwork could not be restored: ${error.message} Existing data was retained; original art is available.`); onChange(null); }
  return { importTexts };
}

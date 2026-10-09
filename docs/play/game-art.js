// Exact installed game pixels, loaded only for visible forms and scenery.
// See art/attribution.json for asset provenance and rights; no game rules here.
const ASSET_BASE = new URL('./art/', import.meta.url);
const MAX_FORMS = 64;
const DIRECTIONS = new Set(['left', 'right']);

function image(url, width, height) {
  return new Promise((resolve, reject) => {
    const bitmap = new Image();
    bitmap.decoding = 'async';
    bitmap.onload = () => bitmap.naturalWidth === width && bitmap.naturalHeight === height
      ? resolve(bitmap) : reject(new Error('Game artwork dimensions do not match its manifest.'));
    bitmap.onerror = () => reject(new Error('Game artwork could not load.'));
    bitmap.src = url;
  });
}

export async function loadGameArt({onChange = () => {}} = {}) {
  const response = await fetch(new URL('manifest.json', ASSET_BASE));
  if (!response.ok) throw new Error('The game artwork catalog could not load.');
  const manifest = await response.json();
  if (manifest.formatVersion !== 1 || manifest.sourceCommit !== '171cda7e698cf2915b50aa46bc766d8d1d0ee50d'
    || !Array.isArray(manifest.forms) || manifest.forms.length > 512 || !Array.isArray(manifest.scenes))
    throw new Error('The game artwork catalog is incompatible.');
  const entries = new Map(manifest.forms.map(form => [form.id, form]));
  const scenes = new Map(manifest.scenes.map(scene => [scene.id, scene]));
  const forms = new Map(), backgrounds = new Map();
  let wanted = [], wantedScene = 'meadow';
  const emit = () => onChange(status());
  function formStatus(id) {
    if (!entries.has(id)) return 'missing';
    const entry = forms.get(id);
    return entry?.image ? 'ready' : entry?.error ? 'error' : 'loading';
  }
  function status() {
    const selected = wanted.map(id => [id, forms.get(id)]);
    return {ready:selected.filter(([,entry]) => entry?.image).map(([id]) => id),
      loading:selected.filter(([,entry]) => entry?.promise && !entry.image && !entry.error).map(([id]) => id),
      missing:wanted.filter(id => !entries.has(id)),
      failed:selected.filter(([,entry]) => entry?.error).map(([id]) => id),
      background:{id:wantedScene, state:backgrounds.get(wantedScene)?.image ? 'ready'
        : backgrounds.get(wantedScene)?.error ? 'failed' : scenes.has(wantedScene) ? 'loading' : 'missing'}};
  }
  function trim(cache, maximum, keep) {
    for (const [key, entry] of cache) {
      if (cache.size <= maximum) break;
      if (!keep.has(key) && (entry.image || entry.error)) cache.delete(key);
    }
  }
  function load(cache, key, descriptor, width, height) {
    if (!descriptor) return Promise.resolve(false);
    let entry = cache.get(key);
    if (entry) {
      cache.delete(key); cache.set(key, entry);
      return entry.promise;
    }
    entry = {image:null,error:false,promise:null}; cache.set(key, entry);
    entry.promise = image(new URL(descriptor.file, ASSET_BASE), width, height)
      .then(bitmap => { entry.image = bitmap; return true; })
      .catch(() => { entry.error = true; return false; })
      .finally(() => {
        trim(forms,MAX_FORMS,new Set(wanted)); trim(backgrounds,2,new Set([wantedScene])); emit();
      });
    return entry.promise;
  }
  function prepare(ids = [], scene = 'meadow') {
    wanted = [...new Set(ids.filter(id => Number.isInteger(id) && id > 0))].slice(0, MAX_FORMS);
    wantedScene = scene;
    const jobs = wanted.map(id => {
      const form = entries.get(id);
      return load(forms,id,form,form?.frameWidth * form?.atlasFrames,form?.frameHeight);
    });
    const descriptor = scenes.get(scene);
    jobs.push(load(backgrounds,scene,descriptor,descriptor?.width,descriptor?.height));
    trim(forms,MAX_FORMS,new Set(wanted)); trim(backgrounds,2,new Set([wantedScene]));
    return Promise.all(jobs);
  }
  function drawBackground(context, scene, x = 0, y = 0, width = 412, height = 412) {
    const bitmap = backgrounds.get(scene)?.image;
    if (!bitmap) return false;
    context.save(); context.imageSmoothingEnabled = true;
    context.drawImage(bitmap,x,y,width,height); context.restore(); return true;
  }
  function drawForm(context, id, {x,y,maxSide,facing=null,time=0,animation='idle'} = {}) {
    const descriptor = entries.get(id), bitmap = forms.get(id)?.image;
    if (!descriptor || !bitmap || !Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(maxSide)) return false;
    // This demo uses native idle loops only. Other animation names do not
    // invent attacks or request unused clips from the source game packs.
    const clip = descriptor.animations.idle;
    if (!clip) return false;
    const [left,top,width,height] = clip.bounds;
    const scale = Math.min(16,Math.floor(maxSide / Math.max(width,height)));
    if (scale < 1) return false;
    const frame = clip.frames[Math.floor(Math.max(0,time) / clip.frameMs) % clip.frames.length];
    const mirror = DIRECTIONS.has(facing) && DIRECTIONS.has(descriptor.nativeFacing) && facing !== descriptor.nativeFacing;
    context.save(); context.imageSmoothingEnabled = false;
    // Integer placement matches the native center and stable clip-wide crop.
    context.translate(Math.trunc(x),Math.trunc(y));
    if (mirror) context.scale(-1,1);
    context.drawImage(bitmap,frame * descriptor.frameWidth + left,top,width,height,
      -(mirror ? Math.ceil(width * scale / 2) : Math.floor(width * scale / 2)),
      -Math.floor(height * scale / 2),width * scale,height * scale);
    context.restore(); return true;
  }
  return {prepare,drawBackground,drawForm,formStatus,status};
}

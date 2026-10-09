import assert from 'node:assert/strict';
import {readFileSync, readdirSync, statSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {resolve, relative} from 'node:path';

const root = fileURLToPath(new URL('../../docs/play/art/',import.meta.url));
const manifest = JSON.parse(readFileSync(resolve(root,'manifest.json')));
const attribution = JSON.parse(readFileSync(resolve(root,'attribution.json')));
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
assert.equal(manifest.forms.length,241);
assert.equal(manifest.missingForms.length,25);
assert.equal(manifest.scenes.length,8);
assert.equal(attribution.sprites.length,241);
assert.equal(new Set([...manifest.forms,...manifest.missingForms].map(form=>form.id)).size,266);
assert.ok([...manifest.forms,...manifest.missingForms].every(form=>form.id>=11 && form.id<=276));
const credits = new Map(attribution.sprites.map(form=>[form.formId,form]));
for(const form of manifest.forms) {
  assert.match(form.file,new RegExp(`^forms/${form.id}\\.png$`));
  const bytes=readFileSync(resolve(root,form.file));
  assert.equal(hash(bytes),form.sha256); assert.equal(bytes.length,form.bytes);
  assert.equal(bytes.subarray(1,4).toString(),'PNG');
  assert.equal(bytes.readUInt32BE(16),form.frameWidth*form.atlasFrames);
  assert.equal(bytes.readUInt32BE(20),form.frameHeight);
  assert.deepEqual(Object.keys(form.animations),['idle']);
  const clip=form.animations.idle, [x,y,w,h]=clip.bounds;
  assert.ok(x>=0 && y>=0 && w>0 && h>0 && x+w<=32 && y+h<=32);
  assert.ok(clip.frames.length>=1 && clip.frames.length<=8);
  assert.ok(clip.frames.every(frame=>frame>=0 && frame<form.atlasFrames));
  assert.ok(credits.has(form.id));
  assert.match(credits.get(form.id).sourceUrl,/^https:\/\//);
}
for(const scene of manifest.scenes) {
  const bytes=readFileSync(resolve(root,scene.file));
  assert.equal(hash(bytes),scene.sha256); assert.equal(bytes.length,scene.bytes);
  assert.equal(attribution.backgrounds.scenes.find(row=>row.sceneId===scene.id).runtimeJpegSha256,scene.sha256);
}
for(const id of [73,91,104,114,139]) assert.ok(credits.get(id).retainedSourceCreditNote);
function files(directory) { return readdirSync(directory).flatMap(name=>{
  const path=resolve(directory,name);return statSync(path).isDirectory()?files(path):[path];
}); }
for(const path of files(root)) {
  assert.match(relative(root,path),/^(forms\/\d+\.png|scenes\/[a-z]+\.jpg|manifest\.json|attribution\.json|ATTRIBUTION\.md)$/);
  if(/\.(json|md)$/.test(path)) assert.doesNotMatch(readFileSync(path,'utf8'),/\/Users\/|\.personal-assets|objects\/ds-form|pack\.json|sourceFile/);
}

// Module behavior with deterministic decoded image doubles. Actual browser
// decoding and visual review are separate acceptance checks.
const requests=[];
let failId=null;
globalThis.fetch=async url=>({ok:true,json:async()=>JSON.parse(readFileSync(fileURLToPath(url),'utf8'))});
globalThis.Image=class {
  set src(url) {
    const path=fileURLToPath(url);requests.push(path);
    queueMicrotask(()=>{
      if(failId!==null && path.endsWith(`/forms/${failId}.png`)) {this.onerror();return;}
      const bytes=readFileSync(path);
      if(path.endsWith('.png')) {this.naturalWidth=bytes.readUInt32BE(16);this.naturalHeight=bytes.readUInt32BE(20);}
      else {this.naturalWidth=412;this.naturalHeight=412;}
      this.onload();
    });
  }
};
const {loadGameArt}=await import('../../docs/play/game-art.js');
let changes=0;
const art=await loadGameArt({onChange:()=>changes++});
assert.equal(requests.length,0,'Catalog loading must not request artwork');
assert.equal(art.formStatus(11),'loading');
assert.equal(art.formStatus(13),'missing');
await art.prepare([11,18,0,11],'meadow');
assert.equal(requests.length,3);
assert.equal(art.formStatus(11),'ready');
assert.deepEqual(art.status().ready,[11,18]);
await art.prepare([11,18],'meadow');
assert.equal(requests.length,3,'Repeated preparation cannot rerequest the same images');
assert.equal(changes,3);
const calls=[];
const context={save(){},restore(){},translate(...args){calls.push(['translate',...args]);},scale(...args){calls.push(['scale',...args]);},drawImage(...args){calls.push(['draw',...args.slice(1)]);}};
assert.equal(art.drawForm(context,13,{x:206,y:196,maxSide:176}),false);
assert.equal(art.drawBackground(context,'forest'),false);
assert.equal(art.drawBackground(context,'meadow'),true);
let oddMirror=0;
for(const form of manifest.forms) {
  await art.prepare([form.id],'meadow');
  for(const maximum of [112,176]) for(const facing of ['left','right',null]) {
    calls.length=0;
    assert.equal(art.drawForm(context,form.id,{x:206,y:176,maxSide:maximum,facing,time:0}),true);
    const [x,y,w,h]=form.animations.idle.bounds;
    const scale=Math.min(16,Math.floor(maximum/Math.max(w,h)));
    const mirror=['left','right'].includes(form.nativeFacing)&&facing&&facing!==form.nativeFacing;
    const draw=calls.find(call=>call[0]==='draw');
    assert.deepEqual(draw.slice(1,5),[form.animations.idle.frames[0]*32+x,y,w,h]);
    const destinationLeft=mirror ? -draw[5]-draw[7] : draw[5];
    assert.equal(destinationLeft,-Math.floor(w*scale/2),'Native centered destination, including odd mirrored widths');
    assert.equal(draw[6],-Math.floor(h*scale/2));
    assert.equal(draw[7],w*scale);assert.equal(draw[8],h*scale);
    if(mirror && w*scale%2) oddMirror++;
  }
}
assert.ok(oddMirror>0,'Must exercise actual odd-width mirrored source art');
await art.prepare([13],'digital');
assert.deepEqual(art.status().missing,[13]);
failId=11;
const failed=await loadGameArt();
await failed.prepare([11],'meadow');
assert.equal(failed.formStatus(11),'error');
const count=requests.length;
await failed.prepare([11],'meadow');
assert.equal(requests.length,count,'A failed asset must not cause a render-loop network storm');
console.log(JSON.stringify({passed:true,forms:manifest.forms.length,missingForms:manifest.missingForms.length,scenes:manifest.scenes.length,nativeFramingChecks:manifest.forms.length*6,oddMirroredFramingChecks:oddMirror,visibleLoading:true,repeatedPreparation:true,explicitMissing:true,failedLoadBackoff:true,attributionCoverage:credits.size},null,2));

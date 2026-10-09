#!/usr/bin/env node
// Run the real controller, artwork loader and WASM together. Only DOM drawing,
// image decoding and browser services are mocked; no browser or device is used.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import { createDeviceView } from '../../docs/play/device-view.js';
import { buildBattleFrames, createBattlePlayback } from '../../docs/play/battle-playback.js';
import createDemoCore from '../../docs/play/runtime/demo-core.js';
import * as party from '../../docs/play/shared/party.js';
import * as starter from '../../docs/play/shared/starter-onboarding.js';
import * as ring from '../../docs/play/shared/capture-ring.js';
import { loadGameArt } from '../../docs/play/game-art.js';

const play = new URL('../../docs/play/', import.meta.url);
const manifest = JSON.parse(await readFile(new URL('art/manifest.json', play), 'utf8'));
const pendingImages = [];
globalThis.fetch = async source => {
  assert.equal(String(source), String(new URL('art/manifest.json', play)));
  return { ok: true, json: async () => manifest };
};
globalThis.Image = class {
  set src(source) {
    this.source = source;
    const base = new URL('art/', play);
    assert(String(source).startsWith(String(base)));
    const filename = String(source).slice(String(base).length);
    const entry = [...manifest.forms, ...manifest.scenes].find(value => value.file === filename);
    assert(entry, `Only a manifest asset may load: ${filename}`);
    this.naturalWidth = entry.width ?? entry.frameWidth * entry.atlasFrames;
    this.naturalHeight = entry.height ?? entry.frameHeight;
    pendingImages.push(this);
  }
};
async function finishImages(succeed = true) {
  for (const image of pendingImages.splice(0)) {
    if (succeed) image.onload();
    else image.onerror();
  }
  // Drain image resolution, cache update and the controller's onChange callback.
  await Promise.resolve(); await Promise.resolve(); await Promise.resolve();
}

const elements = new Map();
function element(tag = 'div') {
  const draws = [], texts = [];
  const context = new Proxy({ draws, texts }, {
    get(target, key) {
      if (key in target) return target[key];
      return (...args) => {
        if (key === 'drawImage') draws.push(args);
        if (key === 'fillText') texts.push(args[0]);
      };
    },
  });
  return {
    tag, width: 824, height: 824, draws, texts, textContent: '', innerHTML: '',
    hidden: false, dataset: {}, style: {}, disabled: false, listeners: new Map(),
    attributes: new Map(), children: [], open: false,
    getContext: () => context,
    addEventListener(name, callback) { this.listeners.set(name, callback); },
    replaceChildren(...children) { this.children = children; },
    append(...children) { this.children.push(...children); },
    setAttribute(name, value) { this.attributes.set(name, value); },
    focus() {}, showModal() { this.open = true; }, close() { this.open = false; },
  };
}
const document = {
  getElementById(id) {
    if (!elements.has(id)) elements.set(id, element());
    return elements.get(id);
  },
  createElement: tag => element(tag), querySelectorAll: () => [],
  querySelector: selector => document.getElementById(selector),
  addEventListener() {}, hidden: false,
};
let raw = null;
const sandbox = {
  document, window: { addEventListener() {} }, WebAssembly,
  matchMedia: () => ({ matches: true }), requestAnimationFrame() {},
  performance: { now: () => 1000 },
  navigator: { locks: { async request(_name, _options, callback) { return callback(); } } },
  localStorage: { getItem() { return raw; }, setItem(_key, value) { raw = value; } },
  ...party, ...starter, ...ring, loadGameArt,
  buildBattleFrames, createBattlePlayback, createDeviceView, createDeviceTouchInput:()=>({refresh(){},cancel(){},contactActive:()=>false}),
    createCaptureRingInput: () => ({ refresh() {}, cancel() {} }),
  runtimeFactory: createDemoCore,
};
const source = await readFile(new URL('app.js', play), 'utf8');
assert.match(source, /\ninit\(\);\s*$/);
assert.equal((source.match(/const \{default:createDemoCore\}=await import\('\.\/runtime\/demo-core\.js'\);/g) || []).length, 1);
const controller = source.replace(/^import .+;\n/gm, '')
  .replace("const {default:createDemoCore}=await import('./runtime/demo-core.js');", 'const createDemoCore=globalThis.runtimeFactory;')
  .replace(/\ninit\(\);\s*$/, `
    globalThis.ui = {init, command, setTab, paint, actor, openCapture, finishBattlePlayback,
      state: () => state, thumbs: () => memberThumbs, gameArt: () => gameArt,
      snapshot: () => core.ccall('demo_snapshot', 'string', [], [])};
  `);
vm.runInNewContext(controller, sandbox, { filename: 'app.js' });
const { ui } = sandbox;
await ui.init();
assert.equal(ui.state().phase, 'egg');
assert.equal(pendingImages.length, 1, 'A new egg loads only the meadow');
await finishImages();

await ui.command('hatch', 1);
assert.equal(ui.state().battleMode, 'auto');
assert.equal(elements.get('mode-auto').attributes.get('aria-pressed'), 'true');
assert.equal(elements.get('mode-manual').attributes.get('aria-pressed'), 'false');
assert.equal(pendingImages.length, 1, 'Hatch lazily loads only its exact partner');
const thumbnail = ui.thumbs()[0].canvas;
assert(thumbnail.texts.includes('Loading art…'));
await finishImages();
assert.equal(ui.gameArt().formStatus(ui.state().formId), 'ready');
assert.equal(thumbnail.draws.length, 0, 'Hidden Box still has its original loading canvas');
ui.setTab('box');
assert(ui.thumbs()[0].canvas.draws.length >= 1, 'Opening Box must display already cached art without a new load callback');

ui.setTab('play');
await ui.command('demo-encounter');
assert.equal(pendingImages.length, 1, 'The encounter lazily loads only its exact foe');
await finishImages(false);
assert.equal(ui.gameArt().formStatus(ui.state().wildFormId), 'error');
ui.paint(1000);
assert(elements.get('screen').texts.includes('ART COULD NOT LOAD'));
ui.actor(manifest.missingForms[0].id, 206, 196, 176, 1000);
assert(elements.get('screen').texts.includes('EXACT ART UNAVAILABLE'));

await ui.command('auto-fight');
assert.equal(ui.state().autoCapture, 1, 'Auto pauses for manual capture even when art fails');
ui.finishBattlePlayback();
const beforeCapture = ui.snapshot();
ui.openCapture();
assert.equal(ui.snapshot(), beforeCapture, 'Rendering the capture ring cannot spend capture RNG');
assert.equal(elements.get('capture-controls').hidden, false);
assert.equal(elements.get('manual-actions').hidden, true);

elements.get('reset-open').listeners.get('click')();
await elements.get('reset-confirm').listeners.get('click')();
assert.equal(ui.state().phase, 'egg');
await ui.command('hatch', 2);
await finishImages();
assert.equal(ui.state().battleMode, 'auto', 'The fully rendered reset adventure also defaults to Auto');
assert.equal(ui.gameArt().formStatus(ui.state().formId), 'ready');
assert.equal(elements.get('mode-auto').attributes.get('aria-pressed'), 'true');
console.log('PASS controller/WASM/art integration: initial and lazy loading, cached Box redraw, truthful missing/failed art, fresh/reset Auto, manual capture pause.');

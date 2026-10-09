// Observe the real app's cue calls and real Web Audio nodes during touch input.
// The observer forwards original methods; it never substitutes sound or state.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { touchDevice } from './touch-browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const sourcePaths = ['web/app.js', 'web/audio-engine.js'];
const sources = () => sourcePaths.map(path => ({ path, sha256: hash(path) }));
const initialSources = sources();
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-app-audio-'));
const checks = [], errors = [], posts = [], observations = [];
let app, browser, failure, trace, savedCapture;
try {
  app = await startServer({ dataDir, port: 0, corePath: resolve('build/digivice-core'), battleCorePath: resolve('build/digivice-battle') });
  const base = `http://127.0.0.1:${app.server.address().port}`;
  let identity;
  async function api(path, body) {
    const response = await fetch(base + path, { method: body ? 'POST' : 'GET', headers: {
      ...(identity ? { Authorization: `Bearer ${identity.token}` } : {}), ...(body ? { 'Content-Type': 'application/json' } : {}),
    }, ...(body ? { body: JSON.stringify(body) } : {}) });
    assert.ok(response.ok, `${path}: ${response.status}`); return response.json();
  }
  const pairing = await api('/api/pairing/start', {});
  identity = await api('/api/pairing/claim', { code: pairing.code });
  const initialSave = await api('/api/save');
  const fixtureEvents = [{ type: 'hatch', value: 1 }, { type: 'walk', value: 100 }, { type: 'magic', value: 0 }, { type: 'heavy', value: 0 }, { type: 'magic', value: 0 }];
  const fixture = await api('/api/save-sync', { rulesVersion: initialSave.state.rulesVersion,
    baseRevision: initialSave.revision, batchId: crypto.randomUUID(), events: fixtureEvents });
  assert.equal(fixture.state.phase, 'encounter');
  checks.push('Isolated capture-eligible fixture created through genuine service/native-core hatch, walk and attack commands.');
  browser = await chromium.launch({ headless: true, args: ['--mute-audio'],
    ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
  const context = await browser.newContext({ viewport: { width: 1000, height: 1000 }, hasTouch: true, reducedMotion: 'no-preference' });
  await context.addInitScript(identity => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity)), identity);
  const page = await context.newPage(); page.setDefaultTimeout(10000);
  const ui = touchDevice(page);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => {
    if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) {
      posts.push({ events: request.postDataJSON().events });
    }
  });
  await page.route('**/api/roster/art/*', route => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"original-placeholder-evidence"}' }));
  await page.goto(`${base}/?controls=touch`); await ui.onScreen('battle');
  await page.evaluate(async () => {
    const { AudioEngine } = await import('/audio-engine.js');
    window.__appAudio = { cues: [], unlocks: [], peak: 0, keyboard: 0, trustedTouches: 0 };
    document.addEventListener('keydown', () => __appAudio.keyboard++);
    document.addEventListener('pointerdown', event => { if (event.isTrusted && event.pointerType === 'touch') __appAudio.trustedTouches++; });
    const play = AudioEngine.prototype.playCue, unlock = AudioEngine.prototype.unlock;
    AudioEngine.prototype.playCue = function (name) {
      window.__observedAudio = this;
      const scheduled = play.call(this, name);
      if (__appAudio.cues.length < 300) __appAudio.cues.push({ name, scheduled, at: performance.now(),
        screen: document.querySelector('#device-ui')?.dataset.screen, state: this._context?.state || 'not-created',
        voices: this._voices.size, muted: this.getState().muted });
      return scheduled;
    };
    AudioEngine.prototype.unlock = async function (event) {
      window.__observedAudio = this;
      const accepted = await unlock.call(this, event);
      __appAudio.unlocks.push({ accepted, trusted: event?.isTrusted === true });
      if (accepted && !window.__appAnalyser) {
        const analyser = this._context.createAnalyser(); analyser.fftSize = 512;
        this._master.connect(analyser); window.__appAnalyser = analyser;
        const samples = new Float32Array(512);
        window.__appAudioTimer = setInterval(() => {
          analyser.getFloatTimeDomainData(samples);
          for (const sample of samples) __appAudio.peak = Math.max(__appAudio.peak, Math.abs(sample));
        }, 20);
      }
      return accepted;
    };
  });
  await ui.menu('settings'); await ui.tap('sound', 'sound');
  assert.equal(await page.evaluate(() => __observedAudio.getState().unlocked), false);
  await ui.tap('toggle-sound');
  await page.waitForFunction(() => __observedAudio._context?.state === 'running' && !__observedAudio.getState().muted);
  await ui.tap('volume-up'); assert.ok(Math.abs(await page.evaluate(() => __observedAudio.getState().volume) - 0.45) < 1e-9);
  await ui.tap('volume-down'); assert.ok(Math.abs(await page.evaluate(() => __observedAudio.getState().volume) - 0.35) < 1e-9);
  await ui.tap('toggle-music'); assert.equal(await page.evaluate(() => __observedAudio.getState().playingMusic), true);
  await ui.tap('toggle-music'); assert.equal(await page.evaluate(() => __observedAudio.getState().playingMusic), false);
  await ui.tap('toggle-sound');
  await page.waitForFunction(() => __observedAudio._context.state === 'suspended');
  assert.equal(await page.evaluate(() => __observedAudio._voices.size), 0);
  await ui.back('settings'); await ui.tap('sound', 'sound');
  await ui.tap('toggle-sound');
  await page.waitForFunction(() => __observedAudio._context.state === 'running' && !__observedAudio.getState().muted);
  await ui.home(); await ui.tap('battle', 'battle');
  checks.push('Actual round-screen Settings/Sound touches enable, change volume, toggle music, mute/suspend with zero voices, and re-enable real Web Audio.');
  assert.equal(posts.length, 0, 'sound settings and navigation do not change the save');
  const traceStart = await page.evaluate(() => __appAudio.cues.length);
  await ui.tap('capture', 'capture-aim'); await page.waitForTimeout(260);
  const box = await page.locator('#screen-surface').boundingBox(); assert.ok(box);
  const cdp = await context.newCDPSession(page);
  async function touch(type, x, y) {
    await cdp.send('Input.dispatchTouchEvent', { type, touchPoints: x === undefined ? [] : [{
      x: box.x + x * box.width / 412, y: box.y + y * box.height / 412,
      id: 1, radiusX: 3, radiusY: 3, force: 1,
    }] });
  }
  const response = page.waitForResponse(response => response.url().endsWith('/api/save-sync') && response.status() === 200);
  await touch('touchStart', 206, 300);
  const began = performance.now();
  for (let step = 0; step < 3; step++) {
    await page.waitForTimeout(20);
    const elapsed = performance.now() - began;
    assert.ok(elapsed < 200, 'bounded headless touch dispatch');
    await touch('touchMove', 206, 300 - 1.2 * elapsed);
  }
  await touch('touchEnd'); await cdp.detach();
  const saved = await (await response).json();
  await page.waitForFunction(start => __appAudio.cues.slice(start).some(cue => ['capture-success', 'capture-fail'].includes(cue.name)), traceStart);
  await page.waitForTimeout(900);
  const captureTrace = await page.evaluate(start => __appAudio.cues.slice(start), traceStart);
  for (const name of ['capture-arm', 'capture-throw', 'hit', 'capture-wiggle']) {
    assert.ok(captureTrace.some(cue => cue.name === name && cue.scheduled), `${name} must reach real oscillator scheduling`);
  }
  const captured = saved.state.collection.length > fixture.state.collection.length;
  const outcomeName = captured ? 'capture-success' : 'capture-fail';
  assert.ok(captureTrace.some(cue => cue.name === outcomeName && cue.scheduled), 'cue must match accepted native outcome');
  assert.equal(posts.length, 1); assert.equal(posts[0].events.length, 1); assert.equal(posts[0].events[0].type, 'flick');
  assert.equal(saved.state.sequence, fixture.state.sequence + 1);
  savedCapture = { fixtureSequence: fixture.state.sequence, savedSequence: saved.state.sequence,
    captured, cue: outcomeName, command: posts[0].events[0], trace: captureTrace };
  checks.push('Trusted touchscreen flick invokes capture-arm → capture-throw → hit → capture-wiggle → saved capture outcome; each named cue schedules real audio nodes, with exactly one native flick command.');
  trace = await page.evaluate(() => {
    clearInterval(__appAudioTimer); __appAnalyser.disconnect();
    return __appAudio;
  });
  assert.ok(trace.cues.some(cue => cue.name === 'menu-confirm' && cue.scheduled));
  assert.ok(trace.cues.some(cue => cue.name === 'menu-back' && cue.scheduled));
  assert.ok(trace.cues.some(cue => cue.name === 'menu-back' && !cue.scheduled && cue.muted));
  assert.ok(trace.peak > 0 && trace.peak < 1); assert.equal(trace.keyboard, 0);
  assert.ok(trace.trustedTouches > 0); assert.ok(trace.unlocks.every(event => event.accepted && event.trusted));
  assert.deepEqual(errors, []); assert.deepEqual(sources(), initialSources, 'product source is unchanged during verification');
  observations.push({ liveAnalyserPeak: trace.peak, trustedTouches: trace.trustedTouches, keyboardEvents: trace.keyboard });
  checks.push('Menu confirm/Back schedule real cues only when enabled; live analyser sees nonzero unclipped samples. No keyboard input, JavaScript errors or product-source changes.');
} catch (error) { failure = error; }
finally {
  if (browser) await browser.close();
  if (app) await new Promise(resolve => app.server.close(resolve));
  rmSync(dataDir, { recursive: true, force: true });
  const result = { result: failure ? 'FAIL' : 'PASS', scope: 'Actual app touch integration with real Web Audio; observer forwards original methods; OS output muted; no physical speaker/human-listening claim',
    sources: initialSources, checks, observations, savedCapture, trace, ...(failure ? { error: failure.stack } : {}) };
  mkdirSync('docs/evidence', { recursive: true });
  writeFileSync('docs/evidence/audio-app-integration.json', JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify({ result: result.result, checks, observations, ...(failure ? { error: failure.message } : {}) }, null, 2));
}
if (failure) throw failure;

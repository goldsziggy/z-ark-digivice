// Real browser synthesis and context lifecycle, with OS output explicitly muted.
// This is not a physical speaker test or a human listening assessment.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { AUDIO_CUES } from '../web/audio-engine.js';
import { startServer } from '../service/server.ts';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const aliases = new Set(['select', 'attack', 'hurt', 'capture-start', 'evolve']);
const canonicalCues = AUDIO_CUES.filter(name => !aliases.has(name));
const sampleRate = 44100, durationSeconds = 1.5, gapSeconds = 0.15;
const samplesPerCue = sampleRate * durationSeconds, gapSamples = sampleRate * gapSeconds;
const outputDir = resolve(process.env.AUDIO_DEMO_DIR || '../deliverables/audio-demo');
const evidencePath = resolve('docs/evidence/audio-render.json');
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-audio-browser-'));
const sha256 = value => createHash('sha256').update(value).digest('hex');
const diskSnapshot = (root = dataDir) => Object.fromEntries(readdirSync(root, { withFileTypes: true })
  .sort((a, b) => a.name.localeCompare(b.name)).map(entry => [entry.name,
    entry.isDirectory() ? diskSnapshot(join(root, entry.name)) : sha256(readFileSync(join(root, entry.name)))]));
const checks = [], cues = [], silenceChecks = [], errors = [], requests = [];
let app, browser, failure, combined, lifecycle;

function wav(pcm) {
  const header = Buffer.alloc(44);
  header.write('RIFF', 0); header.writeUInt32LE(36 + pcm.length, 4); header.write('WAVEfmt ', 8);
  header.writeUInt32LE(16, 16); header.writeUInt16LE(1, 20); header.writeUInt16LE(1, 22);
  header.writeUInt32LE(sampleRate, 24); header.writeUInt32LE(sampleRate * 2, 28);
  header.writeUInt16LE(2, 32); header.writeUInt16LE(16, 34); header.write('data', 36);
  header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}

function analyze(rendered) {
  const source = Buffer.from(rendered, 'base64');
  assert.equal(source.length, samplesPerCue * 4);
  const pcm = Buffer.alloc(samplesPerCue * 2);
  let peak = 0, energy = 0, nonzeroSamples = 0;
  for (let index = 0; index < samplesPerCue; index++) {
    const sample = source.readFloatLE(index * 4);
    assert.ok(Number.isFinite(sample), 'rendered PCM must be finite');
    assert.ok(Math.abs(sample) < 1, 'rendered PCM must not clip');
    peak = Math.max(peak, Math.abs(sample)); energy += sample * sample;
    if (sample !== 0) nonzeroSamples++;
    pcm.writeInt16LE(Math.round(sample * (sample < 0 ? 32768 : 32767)), index * 2);
  }
  return { pcm, peak, rms: Math.sqrt(energy / samplesPerCue), nonzeroSamples, pcmSha256: sha256(pcm) };
}

try {
  mkdirSync(outputDir, { recursive: true });
  app = await startServer({ dataDir, port: 0 });
  const initialDisk = diskSnapshot();
  const base = `http://127.0.0.1:${app.server.address().port}`;
  browser = await chromium.launch({ headless: true, args: ['--mute-audio'],
    ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
  const context = await browser.newContext({ hasTouch: true, viewport: { width: 412, height: 412 } });
  const page = await context.newPage(); page.setDefaultTimeout(10000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => requests.push({ url: request.url(), method: request.method() }));
  // Test-only page, importing the real module through the existing static route.
  await page.route(`${base}/audio-audit.html`, route => route.fulfill({ contentType: 'text/html', body: `
    <!doctype html><html><head><title>Isolated audio audit</title></head><body>
    <button id="enable" style="min-width:80px;min-height:48px">Enable sound</button>
    <button id="mute" style="min-width:80px;min-height:48px">Mute</button>
    <script type="module">
      window.audioModule = await import('/audio-engine.js');
      window.audio = new audioModule.AudioEngine({ storage: null });
      window.unlockEvents = [];
      document.querySelector('#enable').addEventListener('click', async event => {
        const unlocked = await audio.unlock(event);
        unlockEvents.push({ trusted: event.isTrusted, unlocked });
        if (unlocked) { audio.setMuted(false); audio.setMusicEnabled(true); audio.playCue('evolution'); }
      });
      document.querySelector('#mute').addEventListener('click', () => audio.setMuted(true));
    </script></body></html>` }));
  await page.goto(`${base}/audio-audit.html`);
  await page.waitForFunction(() => Boolean(window.audioModule && window.audio));

  async function render(name, { muted = false, volume = 0.35 } = {}) {
    return page.evaluate(async ({ name, sampleRate, samplesPerCue, muted, volume }) => {
      const offline = new OfflineAudioContext(1, samplesPerCue, sampleRate);
      const facade = {
        get currentTime() { return offline.currentTime; }, state: 'running', destination: offline.destination,
        createGain: () => offline.createGain(), createOscillator: () => offline.createOscillator(),
        resume: async () => {}, suspend: async () => {}, close: async () => {},
      };
      const engine = new audioModule.AudioEngine({ contextFactory: () => facade, userGesture: () => true,
        storage: null, document: { hidden: false, addEventListener() {}, removeEventListener() {} } });
      await engine.unlock(); engine.setMuted(muted); engine.setVolume(volume);
      const scheduled = engine.playCue(name);
      const buffer = await offline.startRendering();
      const bytes = new Uint8Array(buffer.getChannelData(0).buffer);
      let binary = '';
      for (let index = 0; index < bytes.length; index += 8192) {
        binary += String.fromCharCode(...bytes.subarray(index, index + 8192));
      }
      engine.destroy();
      return { scheduled, pcm: btoa(binary) };
    }, { name, sampleRate, samplesPerCue, muted, volume });
  }

  const chunks = [];
  for (const name of canonicalCues) {
    const rendered = await render(name);
    assert.equal(rendered.scheduled, true, name);
    const { pcm, ...metrics } = analyze(rendered.pcm);
    assert.ok(metrics.nonzeroSamples > 100 && metrics.peak > 0.001, `${name} must render sound`);
    const file = `${name}.wav`, bytes = wav(pcm);
    writeFileSync(join(outputDir, file), bytes);
    cues.push({ name, file, offsetSeconds: chunks.length / 2 * (durationSeconds + gapSeconds),
      durationSeconds, ...metrics, wavSha256: sha256(bytes), bytes: bytes.length });
    chunks.push(pcm, Buffer.alloc(gapSamples * 2));
  }
  assert.equal(new Set(cues.map(cue => cue.pcmSha256)).size, canonicalCues.length, 'every canonical cue has distinct rendered PCM');
  checks.push(`${canonicalCues.length} canonical cues rendered by Chromium OfflineAudioContext: finite, nonzero, below clipping, distinct PCM fingerprints.`);
  for (const settings of [{ muted: true }, { volume: 0 }]) {
    const rendered = await render('evolution', settings), measured = analyze(rendered.pcm);
    assert.equal(rendered.scheduled, false); assert.equal(measured.peak, 0); assert.equal(measured.nonzeroSamples, 0);
    silenceChecks.push({ settings, peak: measured.peak, nonzeroSamples: measured.nonzeroSamples });
  }
  const quiet = analyze((await render('hit', { volume: 0.2 })).pcm);
  const louder = analyze((await render('hit', { volume: 0.4 })).pcm);
  assert.ok(Math.abs(louder.rms / quiet.rms - 2) < 0.001,
    `master volume scales rendered output (RMS ratio ${louder.rms / quiet.rms})`);
  checks.push('Mute and zero master volume render exact silence; doubling volume doubles rendered RMS.');
  const combinedPcm = Buffer.concat(chunks.slice(0, -1));
  const demoWav = wav(combinedPcm);
  const demoFile = 'digivice-sound-cues.wav';
  writeFileSync(join(outputDir, demoFile), demoWav);
  combined = { file: demoFile, bytes: demoWav.length, durationSeconds: combinedPcm.length / 2 / sampleRate,
    sha256: sha256(demoWav), gapSeconds };

  assert.equal(await page.evaluate(() => audio._context === null), true, 'offline harness must not activate the live engine');
  await page.locator('#enable').evaluate(button => button.click());
  await page.waitForFunction(() => unlockEvents.length === 1);
  assert.deepEqual(await page.evaluate(() => unlockEvents[0]), { trusted: false, unlocked: false });
  assert.equal(await page.evaluate(() => audio._context === null), true, 'synthetic click cannot allocate a live context');
  await page.locator('#enable').tap();
  await page.waitForFunction(() => audio.getState().unlocked && audio._context?.state === 'running');
  assert.deepEqual(await page.evaluate(() => unlockEvents.at(-1)), { trusted: true, unlocked: true });
  const playback = await page.evaluate(async () => {
    const started = audio._context.currentTime;
    const analyser = audio._context.createAnalyser(); analyser.fftSize = 512;
    audio._master.connect(analyser);
    const samples = new Float32Array(analyser.fftSize);
    let peak = 0;
    for (let index = 0; index < 6; index++) {
      await new Promise(resolve => setTimeout(resolve, 25));
      analyser.getFloatTimeDomainData(samples);
      for (const sample of samples) peak = Math.max(peak, Math.abs(sample));
    }
    audio._master.disconnect(analyser); analyser.disconnect();
    return { state: audio._context.state, secondsAdvanced: audio._context.currentTime - started,
      analyserPeak: peak, voices: audio._voices.size, music: audio.getState().playingMusic };
  });
  assert.equal(playback.state, 'running'); assert.ok(playback.secondsAdvanced > 0.05);
  assert.ok(playback.analyserPeak > 0 && playback.analyserPeak < 1, 'live audio graph produces bounded samples');
  assert.ok(playback.voices > 0 && playback.voices <= 12); assert.equal(playback.music, true);
  await page.locator('#mute').tap();
  await page.waitForFunction(() => audio._context.state === 'suspended');
  const muted = await page.evaluate(() => ({ state: audio._context.state, voices: audio._voices.size,
    music: audio.getState().playingMusic, scheduled: audio.playCue('hit') }));
  assert.deepEqual(muted, { state: 'suspended', voices: 0, music: false, scheduled: false });
  checks.push('Real AudioContext rejects a synthetic click, unlocks on a trusted touchscreen tap, advances time and emits nonzero analyser samples; touch mute suspends and clears voices/music. OS output stays muted.');

  await page.locator('#enable').tap();
  await page.waitForFunction(() => audio._context.state === 'running');
  // Headless Chromium has no foreground tab to hide. Simulate only the browser
  // visibility signal; the AudioContext, suspension and voice cleanup are real.
  await page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await page.waitForFunction(() => audio._context.state === 'suspended');
  const hidden = await page.evaluate(() => ({ state: audio._context.state, hidden: audio.getState().hidden,
    voices: audio._voices.size, music: audio.getState().playingMusic }));
  assert.deepEqual(hidden, { state: 'suspended', hidden: true, voices: 0, music: false });
  await page.evaluate(() => { delete document.hidden; document.dispatchEvent(new Event('visibilitychange')); });
  await page.waitForFunction(() => audio._context.state === 'running' && audio.getState().playingMusic);
  await page.locator('#mute').tap();
  await page.waitForFunction(() => audio._context.state === 'suspended');
  await page.evaluate(() => audio.destroy());
  await page.waitForFunction(() => audio._context.state === 'closed');
  lifecycle = { browser: browser.version(), syntheticUnlockRejected: true, trustedTouchUnlock: true,
    playback, muted, hidden, visibilityInput: 'Explicit injected document.hidden + visibilitychange signal; native tab hiding not claimed',
    restoredPreviouslyEnabledMusic: true, destroyedContextState: 'closed', osOutputMuted: true };
  checks.push('Injected hidden/visible signals suspend/clean up and resume the real context; destroy closes it. Native background-tab delivery is not claimed.');
  assert.deepEqual(errors, []);
  assert.ok(requests.every(request => request.method === 'GET' && request.url.startsWith(`${base}/`)), 'only local read-only module/page requests');
  assert.equal(await page.evaluate(() => localStorage.length + sessionStorage.length), 0);
  assert.deepEqual(diskSnapshot(), initialDisk);
  checks.push('No JavaScript errors, external requests, game API requests, browser persistence or service-data changes.');
} catch (error) {
  failure = error;
} finally {
  if (browser) await browser.close();
  if (app) await new Promise(resolve => app.server.close(resolve));
  rmSync(dataDir, { recursive: true, force: true });
  const result = { result: failure ? 'FAIL' : 'PASS', scope: 'Actual browser-generated PCM and Web Audio lifecycle; no physical speaker, human listening, device audio or native tab-background claim',
    createdAt: new Date().toISOString(), sourceSha256: sha256(readFileSync('web/audio-engine.js')),
    format: { sampleRate, channels: 1, bitsPerSample: 16, renderedFramesPerCue: samplesPerCue },
    outputDir, checks, cues, silenceChecks, combined, lifecycle, ...(failure ? { error: failure.stack } : {}) };
  mkdirSync(resolve('docs/evidence'), { recursive: true });
  writeFileSync(evidencePath, JSON.stringify(result, null, 2) + '\n');
  writeFileSync(join(outputDir, 'cue-manifest.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify({ result: result.result, checks, cueCount: cues.length, combined,
    evidencePath, outputDir, ...(failure ? { error: failure.message } : {}) }, null, 2));
}
if (failure) throw failure;

// Native game + real IndexedDB + real two-button pointer input. The only direct
// asset manipulation below is transport fault injection; no game state, outcome,
// scene selector or damage calculation is injected into the application.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';
import { legacyFixture } from './legacy-fixture.ts';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Set PLAYWRIGHT_MODULE to an existing playwright/index.mjs.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const scenes = ['meadow', 'forest', 'beach', 'ruins', 'cavern', 'snow', 'volcanic', 'digital'];
const evidence = process.env.RECORD_EVIDENCE === '1';
const sourcePaths = ['web/app.js', 'web/styles.css', 'web/index.html', 'web/device-screen.js', 'web/device-navigation.js', 'web/two-button-input.js',
  'web/asset-library.js', 'web/asset-cache.js', 'web/asset-store.js', 'web/background-pack.js', 'web/background-player.js', 'assets/packs/catalog.json'];
const sourceHashes = () => Object.fromEntries(sourcePaths.map(path => [path, createHash('sha256').update(readFileSync(path)).digest('hex')]));
const testedSource = sourceHashes();
const dataDir = mkdtempSync(join(tmpdir(), 'digivice-background-browser-'));
const { store, identities } = legacyFixture();
for (const name of ['store.json', 'store.backup.json']) writeFileSync(join(dataDir, name), JSON.stringify(store));
const videoDir = mkdtempSync(join(tmpdir(), 'digivice-background-video-'));
const app = await startServer({ dataDir, port: 0, includeTestFixtures: true });
const base = `http://127.0.0.1:${app.server.address().port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
const context = await browser.newContext({ viewport: { width: 480, height: 960 }, reducedMotion: evidence ? 'no-preference' : 'reduce',
  ...(evidence ? { recordVideo: { dir: videoDir, size: { width: 480, height: 960 } } } : {}) });
const page = await context.newPage();
page.setDefaultTimeout(10000);
const screen = page.locator('#device-ui'), surface = page.locator('#screen-surface');
const left = page.locator('#device-back-button'), right = page.locator('#device-confirm-button');
const errors = [], posts = [], screenshots = [], scaleMeasurements = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (request.method() === 'POST' && /\/api\/(save-sync|battle\/act|battle\/start)$/.test(request.url())) posts.push({ url: request.url(), body: request.postDataJSON() });
});
const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
const selected = () => screen.locator('[data-selected="true"]').getAttribute('data-device-action');
const identity = identities[0];
let saved, battle, releaseForest, completed = false;

async function choose(id, execute = true) {
  for (let count = 0; count < 24; count++) {
    if (await selected() === id) { if (execute) await right.click(); return; }
    await left.click();
  }
  throw new Error(`Two-button input cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function back(expected) { await holdDeviceBack(page); if (expected) await onScreen(expected); }
async function home() {
  for (let count = 0; count < 12 && await screen.getAttribute('data-screen') !== 'home'; count++) await back();
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await choose(id); }
async function read(path = '/api/save') {
  const response = await fetch(`${base}${path}`, { headers: { Authorization: `Bearer ${identity.token}` } });
  assert.equal(response.status, 200); return response.json();
}
async function command(id, path = '/api/save-sync', afterReply, repeat = false) {
  await choose(id, false);
  const response = page.waitForResponse(response => response.url().endsWith(path) && response.request().method() === 'POST' && response.status() === 200);
  if (repeat) await right.dblclick({ delay: 0 }); else await right.click();
  const result = await (await response).json();
  if (afterReply) await afterReply(result);
  if (path === '/api/save-sync') {
    saved = result;
    await page.waitForFunction(revision => document.querySelector('#revision').textContent === String(revision), result.revision);
    await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') === null && !document.querySelector('#device-confirm-button').disabled);
  } else battle = result;
  return result;
}
async function assetState() {
  return page.evaluate(async () => {
    const { IndexedDBAssetStore } = await import('/asset-store.js');
    const storage = new IndexedDBAssetStore(); try { return await storage.read(); } finally { storage.close(); }
  });
}
async function installed(id) {
  await page.waitForFunction(async id => {
    const { IndexedDBAssetStore } = await import('/asset-store.js');
    const storage = new IndexedDBAssetStore();
    try { return (await storage.read()).packs.some(pack => pack.key === `${id}@1`); } finally { storage.close(); }
  }, id, { timeout: 25000 });
}
async function background(id) {
  await page.waitForFunction(id => {
    const element = document.querySelector('#screen-surface');
    return element?.dataset.backgroundId === id && element.dataset.backgroundStatus === 'ready';
  }, id);
}
async function photo(name) {
  if (!evidence) return;
  const path = `docs/evidence/background-${name}.png`;
  await surface.screenshot({ path }); screenshots.push(path);
}
async function geometry(label = '') {
  const failures = await screen.evaluate(root => {
    const circle = root.closest('.screen-surface').getBoundingClientRect();
    const cx = circle.x + circle.width / 2, cy = circle.y + circle.height / 2, radius = circle.width / 2;
    const entries = [...root.querySelectorAll('button')].filter(button => button.getClientRects().length && getComputedStyle(button).visibility !== 'hidden')
      .map(button => ({ text: button.textContent.trim(), box: button.getBoundingClientRect() }));
    const failures = [];
    for (const { text, box } of entries) if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]]
      .some(([x, y]) => Math.hypot(x - cx, y - cy) > radius + 2)) failures.push(`clipped: ${text}`);
    for (let i = 0; i < entries.length; i++) for (let j = i + 1; j < entries.length; j++) {
      const a = entries[i].box, b = entries[j].box;
      if (Math.min(a.right, b.right) - Math.max(a.left, b.left) > 1 && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 1) failures.push(`overlap: ${entries[i].text}/${entries[j].text}`);
    }
    // Main live text is checked against its opaque backing plate; the eight
    // illustrations therefore cannot change this measured contrast. This is
    // intentionally not a claim about every decorative pixel or hardware panel.
    const rgba = value => { const match = /^rgba?\(([^)]+)\)$/.exec(value); return match ? match[1].split(',').map(Number) : null; };
    const luminance = color => color.slice(0, 3).map(value => value / 255).map(value => value <= .04045 ? value / 12.92 : ((value + .055) / 1.055) ** 2.4)
      .reduce((sum, value, index) => sum + value * [.2126, .7152, .0722][index], 0);
    for (const label of root.querySelectorAll('.screen-heading h2,button:not(:disabled),.screen-choice:not(:disabled) small')) {
      if (!label.getClientRects().length || getComputedStyle(label).visibility === 'hidden') continue;
      const ink = rgba(getComputedStyle(label).color); let plate = null;
      for (let node = label; node && node !== root.parentElement; node = node.parentElement) {
        const candidate = rgba(getComputedStyle(node).backgroundColor);
        if (candidate && (candidate.length === 3 || candidate[3] === 1)) { plate = candidate; break; }
      }
      if (!ink || !plate) { failures.push(`no measurable opaque plate: ${label.textContent.trim()}`); continue; }
      const values = [luminance(ink), luminance(plate)].sort((a, b) => b - a);
      const ratio = (values[0] + .05) / (values[1] + .05);
      if (ratio < 4.5) failures.push(`contrast ${ratio.toFixed(2)}: ${label.textContent.trim()}`);
    }
    return failures;
  });
  if (failures.length) await surface.screenshot({ path: '/tmp/digivice-background-geometry-failure.png' });
  assert.deepEqual(failures, [], `${label}: circular ${await screen.getAttribute('data-screen')} layout`);
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'no horizontal document overflow');
}
async function cycleChoices() {
  const initial = await selected();
  for (let i = 0; i < 24; i++) { await geometry(); await left.click(); if (await selected() === initial) return; }
  throw new Error('Next did not wrap within the bounded menu.');
}
async function sceneScales(id) {
  const viewport = page.viewportSize();
  const originalStyle = await page.locator('.device-screen').evaluate(element => element.style.cssText);
  for (const [label, pixels, size] of [['logical-480', 480, { width: 800, height: 1100 }], ['nominal-2.1in', 201.6, { width: 390, height: 844 }]]) {
    await page.setViewportSize(size);
    // CSSOM changes only this test's display sizing, with the production CSP
    // intact. Content-box keeps the bezel outside the measured display diameter.
    await page.locator('.device-screen').evaluate((element, pixels) => {
      element.style.setProperty('box-sizing', 'content-box', 'important');
      element.style.setProperty('width', `${pixels}px`, 'important'); element.style.setProperty('height', `${pixels}px`, 'important');
      element.style.setProperty('margin-inline', 'auto', 'important');
    }, pixels);
    const bounds = await surface.boundingBox(); assert.ok(Math.abs(bounds.width - pixels) < 1); assert.ok(Math.abs(bounds.height - pixels) < 1);
    const logical = await page.locator('#display').evaluate(canvas => [canvas.width, canvas.height]); assert.deepEqual(logical, [480, 480]);
    await background(id); await cycleChoices(); await geometry(`${id} ${label}`); await photo(`wild-${id}-${label}`);
    scaleMeasurements.push({ scene: id, label, surfaceCssPixels: [bounds.width, bounds.height], canvasPixels: logical, viewport: size });
  }
  await page.locator('.device-screen').evaluate((element, original) => { element.style.cssText = original; }, originalStyle);
  await page.setViewportSize(viewport);
}
function nativeNext(save, type, value = 0) {
  assert.equal(save.baseSequence, 0);
  return JSON.parse(execFileSync(resolve('build/digivice-core'), ['--replay', String(save.seed)], {
    input: [...save.events, { type, value }].map(event => `${event.type} ${event.value}\n`).join(''), encoding: 'utf8', timeout: 2000, maxBuffer: 32768,
  }));
}

try {
  if (evidence) mkdirSync('docs/evidence', { recursive: true });
  // Preserve the existing native Mote encounter oracle across onboarding.
  await page.addInitScript(identity => {
    if (!localStorage.getItem('digivice.dev.identity.v1')) localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(identity));
  }, identity);
  // Pause the actual background warmup while the gallery still selects the
  // starter. Resume must target that background, not the gallery selection.
  const firstRanges = [];
  await page.route('**/api/assets/packs/scene-forest-v1/1', async route => {
    const range = route.request().headers().range; firstRanges.push(range);
    if (range === 'bytes=0-16383') await route.continue();
    else { await new Promise(resolve => { releaseForest = resolve; }); try { await route.abort('internetdisconnected'); } catch { /* The user's Pause may already cancel this request. */ } }
  });
  await page.goto(`${base}/?controls=buttons`); await onScreen('home');
  saved = await read();
  await page.waitForFunction(() => /Downloading Forest scenery/.test(document.querySelector('#asset-status').textContent), null, { timeout: 25000 });
  for (let count = 0; count < 100 && !releaseForest; count++) await page.waitForTimeout(20);
  assert.ok(releaseForest, 'second forest range is held for the actual Pause gesture');
  await menu('settings'); await choose('artwork'); await choose('open-assets'); await choose('pack-starter-v2'); await onScreen('asset-progress');
  assert.equal(await page.locator('#asset-pack-select').inputValue(), 'starter-v2');
  assert.match(await screen.textContent(), /Forest scenery/);
  await choose('asset-pause');
  await page.waitForFunction(() => document.querySelector('#asset-status').textContent.includes('Download paused'));
  assert.equal(await page.locator('#asset-pack-select').inputValue(), 'scene-forest-v1', 'Pause keeps the active transfer selected for Resume');
  await geometry('paused download'); await photo('download-paused-480');
  releaseForest();
  assert.deepEqual(firstRanges, ['bytes=0-16383', 'bytes=16384-32767']);
  assert.equal((await assetState()).stage.data.byteLength, 16384);
  await page.unroute('**/api/assets/packs/scene-forest-v1/1');
  const buttonResumeRanges = [];
  await page.route('**/api/assets/packs/scene-forest-v1/1', async route => {
    const range = route.request().headers().range; buttonResumeRanges.push(range);
    if (range === 'bytes=16384-32767') await route.continue(); else await route.abort('internetdisconnected');
  });
  await choose('asset-install');
  await page.waitForFunction(() => document.querySelector('#asset-status').textContent.includes('you can retry'));
  assert.deepEqual(buttonResumeRanges, ['bytes=16384-32767', 'bytes=32768-49151']);
  assert.equal((await assetState()).stage.data.byteLength, 32768); await geometry('interrupted download'); await photo('download-error-480');
  await page.unroute('**/api/assets/packs/scene-forest-v1/1');
  await home(); await background('meadow'); await geometry('startup');
  const resumed = [];
  await page.route('**/api/assets/packs/scene-forest-v1/1', async route => { resumed.push(route.request().headers().range); await route.continue(); });
  // A signed entry with damaged transport bytes must not activate as a scene.
  await page.route('**/api/assets/packs/scene-beach-v1/1', async route => {
    const response = await route.fetch(), bytes = Buffer.from(await response.body()); bytes[Math.min(10, bytes.length - 1)] ^= 1;
    await route.fulfill({ response, body: bytes });
  });
  await page.reload(); await onScreen('home'); await installed('scene-forest-v1');
  await page.waitForFunction(() => document.querySelector('#asset-status').textContent.includes('you can retry'));
  assert.equal(resumed[0], 'bytes=32768-49151');
  const corrupted = await assetState();
  assert.equal(corrupted.packs.some(pack => pack.key === 'scene-beach-v1@1'), false);
  assert.equal(corrupted.stage, null, 'bad hash discards only the corrupt download');
  await background('meadow'); await geometry('corrupt pack fallback');
  await page.unroute('**/api/assets/packs/scene-forest-v1/1'); await page.unroute('**/api/assets/packs/scene-beach-v1/1');
  await page.reload(); await onScreen('home');
  for (const id of ['starter-v2', 'tide-v1', 'ember-v1', ...scenes.map(scene => `scene-${scene}-v1`)]) await installed(id);

  assert.equal((await read()).revision, 0, 'cosmetic pause/resume/reload never changes the game save');
  await background('meadow'); await geometry('480 viewport'); await photo('home-480');
  await choose('menu'); await onScreen('menu'); await cycleChoices(); await photo('menu-480');
  await choose('care'); await geometry(); await photo('care-480');
  const beforeFeedPosts = posts.length; await command('feed', '/api/save-sync', null, true);
  assert.equal(posts.length, beforeFeedPosts + 1, 'repeated Confirm sends one care action');
  await menu('stats'); await geometry(); await photo('stats-480'); await choose('skills'); await geometry(); await photo('moves-480');
  await choose('type-chart'); await geometry(); await photo('types-480');
  await menu('explore'); await command('walk'); await onScreen('battle'); await background('meadow');
  await cycleChoices(); await photo('wild-meadow-480'); await sceneScales('meadow');
  let before = await read(); await command('attack'); assert.deepEqual(saved.state, nativeNext(before, 'attack'));
  before = await read(); await command('magic'); assert.deepEqual(saved.state, nativeNext(before, 'magic'));
  await choose('cards'); await geometry(); await photo('cards-480'); await command('card-shelter');
  await back('battle');
  for (let step = 0; step < 8 && saved.state.wildHp > Math.floor(saved.state.wildMaxHp / 2); step++) {
    const current = await read();
    const options = ['attack', 'magic', 'heavy'].map(type => ({ type, state: nativeNext(current, type) })).filter(option => option.state.phase === 'encounter');
    options.sort((a, b) => a.state.wildHp - b.state.wildHp); assert.ok(options.length);
    await command(options[0].type); assert.deepEqual(saved.state, options[0].state);
  }
  assert.equal(saved.state.phase, 'encounter');
  const beforeCapture = await read(), beforeCapturePosts = posts.length;
  await choose('capture'); await onScreen('capture'); await photo('capture-480'); await back('battle');
  await page.waitForTimeout(650); assert.equal(posts.length, beforeCapturePosts); assert.deepEqual(await read(), beforeCapture);
  for (let attempt = 0; attempt < 3 && saved.state.phase === 'encounter'; attempt++) await command('capture', '/api/save-sync', async () => {
    if (evidence) { await page.waitForTimeout(100); await photo('capture-result-480'); }
  });
  assert.equal(saved.state.collection.length, 2); await photo('captured-480');
  const member = saved.state.collection.find(entry => entry.id !== 1), collectionBefore = structuredClone(saved.state.collection);
  await menu('companions'); await geometry(); await photo('roster-480'); await choose(`member-${member.id}`); await geometry();
  await command('select-companion'); assert.equal(saved.state.activeCreatureId, member.id); assert.deepEqual(saved.state.collection, collectionBefore);
  await photo('partner-480'); await choose('member-stats'); await geometry();
  for (const key of ['maxHp', 'attack', 'defense', 'magic', 'resistance']) assert.equal(Number(await screen.locator(`[data-stat="${key}"]`).textContent()), member.combat[key]);

  // Actual native encounters visit every field. Background stays unchanged
  // through an exchange and navigation, with no hidden test scene override.
  await page.setViewportSize({ width: 390, height: 844 });
  for (let encounter = 2; encounter <= 8; encounter++) {
    await menu('care'); await command('rest'); await menu('explore'); await command('walk'); await onScreen('battle');
    assert.equal(saved.state.encounters, encounter); await background(scenes[encounter - 1]); await cycleChoices();
    await photo(`wild-${scenes[encounter - 1]}-390`);
    await sceneScales(scenes[encounter - 1]);
    for (let turn = 0; turn < 24 && saved.state.phase === 'encounter'; turn++) await command('attack');
    assert.equal(saved.state.phase, 'home');
  }

  // Finish a practice exchange, then lose an already committed finishing reply.
  // Back cannot discard the pending command, and reload retries the exact ID.
  const beforePractice = await read();
  await menu('battle-mode'); await choose('practice-start'); await onScreen('battle-select-mode');
  await command('practice-tactical', '/api/battle/start'); await onScreen('battle-choice');
  await cycleChoices(); await geometry(); await photo('practice-attack-390');
  await command('practice-heavy', '/api/battle/act'); await onScreen('battle-result'); await geometry(); await photo('practice-result-390');
  await choose('practice-continue'); await onScreen('battle-choice'); await geometry(); await photo('practice-defense-390');
  await command('practice-counter', '/api/battle/act'); await onScreen('battle-result'); await choose('practice-continue'); await back('battle-mode');
  let committed;
  await page.route('**/api/battle/act', async route => { const response = await route.fetch(); committed = await response.json(); await route.abort('failed'); });
  await choose('practice-retreat'); await onScreen('battle-resolve');
  await page.waitForFunction(() => !document.querySelector('[data-device-action="practice-retry"]')?.disabled);
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1'));
  await geometry(); await photo('pending-390'); await back('battle-resolve');
  assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.battle.pending.v1')), pending);
  await page.unroute('**/api/battle/act');
  // Only asset networking is disconnected. App shell and native game service
  // remain on loopback; this is not a browser service-worker/offline-game claim.
  let assetRequests = 0;
  await page.route('**/api/assets/**', async route => { ++assetRequests; await route.abort('internetdisconnected'); });
  await page.reload(); await onScreen('battle-resolve');
  await command('practice-retry', '/api/battle/act'); await onScreen('battle-result'); assert.deepEqual(battle, committed);
  assert.equal(posts.at(-1).body.requestId, JSON.parse(JSON.parse(pending).body).requestId);
  await choose('practice-continue'); await home(); await background('meadow');
  assert.deepEqual(await read(), beforePractice); assert.equal(assetRequests, 0, 'all cached scenery restores without asset HTTP');
  await geometry('cached mobile reload'); await photo('cached-home-390');
  // A real service failure adds a third detail action and a longer notice.
  // This previously overlapped Back despite the ordinary roster tests passing.
  await page.route('**/api/battle', route => route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ error: 'Practice unavailable.' }) }));
  await page.reload(); await onScreen('home'); await menu('companions'); await choose(`member-${member.id}`); await onScreen('companion');
  assert.equal(await screen.locator('[data-device-action="partner-refresh"]').count(), 1);
  await cycleChoices(); await geometry('practice unavailable detail'); await photo('partner-unavailable-390');
  const previousMemberStyle = await page.locator('.device-screen').evaluate(element => {
    const original = element.style.cssText;
    element.style.setProperty('box-sizing', 'content-box', 'important'); element.style.setProperty('width', '201.6px', 'important');
    element.style.setProperty('height', '201.6px', 'important'); element.style.setProperty('margin-inline', 'auto', 'important'); return original;
  });
  await cycleChoices(); await geometry('practice unavailable nominal 2.1in'); await photo('partner-unavailable-nominal-2.1in');
  await page.locator('.device-screen').evaluate((element, original) => { element.style.cssText = original; }, previousMemberStyle);
  assert.deepEqual(await read(), beforePractice, 'unavailable practice and detail navigation leave the complete pet save untouched');
  assert.equal(await page.locator('#playtest-tools').evaluate(details => details.open), false);
  assert.deepEqual(errors, []); assert.deepEqual(sourceHashes(), testedSource, 'evidence must come from one unchanged application revision'); completed = true;
  console.log('PASS: real two-button loop; all eight native encounter backgrounds; circular 480/390 and nominal 201.6 CSS px layouts; stats/moves/types/care/SetPartner; physical/magic/cards/capture cancel and capture; practice attack/defense/result; durable finishing retry after reload; real IndexedDB interrupted-range resume, corrupt-pack fallback, and asset-offline cached reload.');
} finally {
  releaseForest?.();
  const video = page.video();
  await context.close(); await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close();
  if (completed && evidence) {
    const source = await video.path();
    execFileSync('/opt/homebrew/bin/ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', '-i', source, '-vf', 'setpts=0.5*PTS,fps=20', '-an', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '25', '-movflags', '+faststart', 'docs/evidence/background-playable-loop.mp4'], { timeout: 60000 });
    const videoInfo = JSON.parse(execFileSync('/opt/homebrew/bin/ffprobe', ['-v', 'error', '-show_entries', 'format=duration,size:stream=width,height,avg_frame_rate', '-of', 'json', 'docs/evidence/background-playable-loop.mp4'], { encoding: 'utf8' }));
    writeFileSync('docs/evidence/background-browser-artifacts.json', JSON.stringify({ testedSource, screenshots, video: 'docs/evidence/background-playable-loop.mp4', videoPlaybackSpeed: 2,
      videoEncoding: { width: videoInfo.streams[0].width, height: videoInfo.streams[0].height, frameRate: videoInfo.streams[0].avg_frame_rate,
        durationSeconds: Number(videoInfo.format.duration), bytes: Number(videoInfo.format.size), note: 'Encoded browser recording, not measured device FPS.' },
      logicalCanvas: [480, 480], viewports: [[480, 960], [390, 844], [800, 1100]], scaleMeasurements,
      contrast: 'All eight scenes: main heading, enabled action/back buttons and their secondary labels >=4.5:1 against their opaque live CSS backing plates.',
      nominalPhysicalPreview: 'Display surface 2.1in = 201.6 CSS px at nominal96CSSpx/in; not calibrated to the actual display. Use a ruler/device for physical size.',
      offlineScope: 'Asset URLs blocked on reload; local app shell and native game service remain available.', art: 'User-approved generated scenery and original project creature art; no personal imports.' }, null, 2) + '\n');
  }
  rmSync(dataDir, { recursive: true, force: true }); rmSync(videoDir, { recursive: true, force: true });
}

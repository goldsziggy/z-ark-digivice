// Real native-core roster/journal/release checks. Source metadata is read only;
// gameplay fixtures are legal events, and navigation uses the two device buttons.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { startServer } from '../service/server.ts';
import { holdDeviceBack } from './browser-tools.mjs';

if (!process.env.PLAYWRIGHT_MODULE) throw new Error('Use an existing Playwright installation.');
const { chromium } = await import(pathToFileURL(resolve(process.env.PLAYWRIGHT_MODULE)).href);
const directory = mkdtempSync(join(tmpdir(), 'digivice-roster-browser-'));
const binaryPaths = { corePath: resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core'), battleCorePath: resolve(process.env.DIGIVICE_TEST_BATTLE_PATH || 'build/digivice-battle') };
const native = events => JSON.parse(execFileSync(binaryPaths.corePath, ['--replay-onboarding', '12345'], { input: events.map(e => `${e.type} ${e.value}\n`).join(''), encoding: 'utf8', timeout: 3000, maxBuffer: 65536 }));
let app = await startServer({ ...binaryPaths, dataDir: directory, port: 0 });
const port = app.server.address().port, base = `http://127.0.0.1:${port}`;
const browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {}) });
let context, page, screen, left, right, identity;
const posts = [], artRequests = [], errors = [], geometries = [], screenshots = [], artDemandChecks = [];
async function http(path, body, credential = identity) {
  const response = await fetch(`${base}${path}`, { method: body === undefined ? 'GET' : 'POST', headers: {
    ...(body === undefined ? {} : { 'Content-Type': 'application/json' }), ...(credential ? { Authorization: `Bearer ${credential.token}` } : {}) },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  assert.ok(response.ok, `${path}: ${response.status} ${response.ok ? '' : await response.text()}`); return response.json();
}
const read = () => http('/api/save');
async function seed(events) { let save = await read(); for (let offset = 0; offset < events.length; offset += 100) save = await http('/api/save-sync', { rulesVersion: save.state.rulesVersion, baseRevision: save.revision, batchId: crypto.randomUUID(), events: events.slice(offset, offset + 100) }); return save; }
async function open(storageState) {
  context = await browser.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce', ...(storageState ? { storageState } : {}) });
  if (!storageState) await context.addInitScript(value => localStorage.setItem('digivice.dev.identity.v1', JSON.stringify(value)), identity);
  page = await context.newPage(); page.setDefaultTimeout(8000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => {
    if (request.method() === 'POST' && request.url().endsWith('/api/save-sync')) posts.push(request.postDataJSON());
    if (/\/api\/roster\/art\/\d+$/.test(request.url())) artRequests.push(Number(request.url().split('/').at(-1)));
  });
  screen = page.locator('#device-ui'); left = page.locator('#device-back-button'); right = page.locator('#device-confirm-button');
  await page.goto(`${base}/?controls=buttons`); await page.waitForFunction(id => document.querySelector('#device-id')?.textContent === id, identity.deviceId);
}
const onScreen = value => page.waitForFunction(value => document.querySelector('#device-ui')?.dataset.screen === value, value);
async function choose(id, execute = true) {
  await page.waitForFunction(() => document.querySelector('#device-ui [data-selected="true"]'));
  for (let n = 0; n < 32; n++) {
    if (await screen.locator('[data-selected="true"]').getAttribute('data-device-action') === id) { if (execute) await right.click(); return; }
    await left.click();
  }
  throw new Error(`Cannot reach ${id} on ${await screen.getAttribute('data-screen')}`);
}
async function back(expected) { await holdDeviceBack(page); if (expected) await onScreen(expected); }
async function home() {
  if (await screen.getAttribute('data-screen') === 'wild-auto-result') await choose('wild-auto-done');
  if (await screen.getAttribute('data-screen') === 'release-result') await choose('release-done');
  for (let n = 0; n < 18 && await screen.getAttribute('data-screen') !== 'home'; n++) await back();
  await onScreen('home');
}
async function menu(id) { await home(); await choose('menu'); await choose(id); await onScreen(id); }
async function command(id) {
  await choose(id, false);
  const result = page.waitForResponse(response => response.request().method() === 'POST' && response.url().endsWith('/api/save-sync') && response.status() === 200);
  await right.click(); const saved = await (await result).json();
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') === null && !document.querySelector('#device-confirm-button').disabled); return saved;
}
async function practice(id) {
  await choose(id, false);
  const result = page.waitForResponse(response => response.request().method() === 'POST' && /\/api\/battle\/(start|act)$/.test(response.url()) && response.status() === 200);
  await right.click(); return (await result).json();
}
async function geometry(label) {
  const result = await screen.evaluate(root => {
    const surface = document.querySelector('#screen-surface').getBoundingClientRect(), cx = surface.x + surface.width / 2, cy = surface.y + surface.height / 2;
    const rows = [...root.querySelectorAll('button')].filter(node => node.getClientRects().length).map(node => ({ name: node.textContent, box: node.getBoundingClientRect() }));
    const failures = [];
    if (root.dataset.screen === 'roster-notes') {
      for (const button of root.querySelectorAll('button[data-selected=true]')) {
        const card = button.getBoundingClientRect();
        for (const text of button.querySelectorAll('span,small')) {
          const box = text.getBoundingClientRect();
          if (box.left < card.left - 1 || box.right > card.right + 1 || box.top < card.top - 1 || box.bottom > card.bottom + 1 || text.scrollHeight > text.clientHeight + 1) failures.push(`clipped note ${text.textContent}`);
        }
      }
    }
    for (const { name, box } of rows) if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > surface.width / 2 + 2)) failures.push(`clipped ${name}`);
    for (let a = 0; a < rows.length; a++) for (let b = a + 1; b < rows.length; b++) {
      const x = rows[a].box, y = rows[b].box;
      if (Math.min(x.right, y.right) - Math.max(x.left, y.left) > 1 && Math.min(x.bottom, y.bottom) - Math.max(x.top, y.top) > 1) failures.push(`overlap ${rows[a].name}/${rows[b].name}`);
    }
    return { width: surface.width, failures };
  });
  assert.deepEqual(result.failures, [], label); geometries.push({ label, width: result.width });
}
async function checkScene(label) {
  await geometry(`${label} mobile`);
  const previous = await page.locator('.device-screen').evaluate(node => {
    const old = node.style.cssText; node.style.setProperty('box-sizing', 'content-box', 'important'); node.style.setProperty('width', '201.6px', 'important'); node.style.setProperty('height', '201.6px', 'important'); node.style.setProperty('margin-inline', 'auto', 'important'); return old;
  });
  await geometry(`${label} nominal2.1in`);
  await page.locator('.device-screen').evaluate(node => { node.style.setProperty('width', '140.16px', 'important'); node.style.setProperty('height', '140.16px', 'important'); });
  await geometry(`${label} nominal1.46in`);
  await page.locator('.device-screen').evaluate((node, old) => { node.style.cssText = old; }, previous);
  if (process.env.ROSTER_EVIDENCE === '1') { mkdirSync('docs/evidence', { recursive: true }); const path = `docs/evidence/world-ds-${label}.png`; await page.locator('#screen-surface').screenshot({ path }); screenshots.push(path); }
}
async function detail(id) { await choose(`roster-form-${id}`); await page.waitForFunction(id => document.querySelector('#device-ui')?.dataset.catalogFormId === String(id), id); }
async function journalDetail(id) {
  await menu('journal');
  for (let n = 0; n < 64; n++) {
    await page.waitForFunction(() => document.querySelector('#device-ui [data-device-action^="roster-form-"]'));
    if (await screen.locator(`[data-device-action="roster-form-${id}"]`).count()) return detail(id);
    assert.ok(await screen.locator('[data-device-action="journal-next"]').count(), `Journal has discovered form${id}`);
    const first = await screen.locator('[data-device-action^="roster-form-"]').first().getAttribute('data-device-action');
    await choose('journal-next');
    await page.waitForFunction(first => { const row = document.querySelector('#device-ui [data-device-action^="roster-form-"]'); return row && row.dataset.deviceAction !== first; }, first);
  }
  throw new Error('Journal pagination exceeded512-form bound');
}

async function referenceMove(id, prefix, stage, expected, variant) {
  await menu('roster'); await choose('roster-filters'); await choose('roster-letters'); await choose(`roster-letter-${prefix}`);
  await choose('roster-filters'); await choose('roster-stages'); await choose(`roster-stage-${stage}`);
  await detail(id); await choose('roster-notes'); await choose('roster-note-4', false);
  const selected = screen.locator('[data-selected=true]');
  assert.match(await selected.textContent(), /Encyclopedia move/); assert.ok((await selected.textContent()).includes(expected));
  assert.equal((await selected.textContent()).includes('Sheet identity needs review'), variant);
  await checkScene(variant ? 'encyclopedia-variant' : 'encyclopedia-long-name');
  const overflow = await selected.evaluate(button => {
    const box = button.getBoundingClientRect();
    return [...button.querySelectorAll('span,small')].filter(node => {
      const text = node.getBoundingClientRect();
      return text.left < box.left - 1 || text.right > box.right + 1 || text.top < box.top - 1 || text.bottom > box.bottom + 1;
    }).map(node => node.textContent);
  });
  assert.deepEqual(overflow, [], 'encyclopedia text stays within its round carousel card');
  await back('roster-detail'); await choose('roster-references');
  assert.ok(await page.locator('#roster-references a[href^="https://digimon.net/reference_en/"]').count());
  // Use the existing browser-tools disclosure after inspecting its source link.
  await page.locator('#playtest-tools > summary').click();
}
async function carried(id) {
  await menu('companions');
  for (let n = 0; n < 3; n++) {
    if (await screen.locator(`[data-device-action="member-${id}"]`).count()) { await choose(`member-${id}`); return; }
    await choose('companions-next');
  }
  throw new Error(`Missing carried member ${id}`);
}
try {
  const pairing = await http('/api/pairing/start', {}, null), claimed = await http('/api/pairing/claim', { code: pairing.code }, null);
  identity = { deviceId: claimed.deviceId, token: claimed.token };
  // The rarity-weighted current pool no longer makes encounter six Calumon.
  // Generate bounded legal native history instead of overriding RNG, form or HP.
  // This accelerates fixture setup; it does not claim 147 button-played battles.
  const fixtureEvents = [{ type: 'hatch', value: 1 }, { type: 'mode', value: 1 }];
  let simulated = native(fixtureEvents), highMember;
  for (let fight = 0; fight < 200 && !highMember; fight++) {
    if (simulated.collection.length === simulated.collectionCapacity) fixtureEvents.push({ type: 'release', value: simulated.collection.find(member => ![1, 2].includes(member.id)).id });
    fixtureEvents.push(...Array.from({ length: simulated.recoveryRestCount }, () => ({ type: 'rest', value: 0 })), { type: 'walk', value: 100 }, { type: 'auto', value: 0 });
    simulated = native(fixtureEvents); highMember = simulated.collection.find(member => member.formId === 276);
  }
  assert.ok(highMember, 'bounded native history captures high form276 under the current weighted pool');
  fixtureEvents.push({ type: 'rest', value: 0 });
  const initial = await seed(fixtureEvents);
  assert.deepEqual(initial.state, native(fixtureEvents));
  const highMemberId = highMember.id;
  await open(); await menu('roster'); await page.waitForFunction(() => document.querySelector('[data-device-action=roster-filters]'));
  const beforeBrowsing = await read(), beforePosts = posts.length;
  assert.deepEqual([...new Set(artRequests)], [initial.state.formId], 'cold Home and roster list load only the active partner');
  artDemandChecks.push({ scene: 'cold Home and roster page', requested: [...new Set(artRequests)], expected: [initial.state.formId] });
  const beforeFilterArt = artRequests.length;
  await choose('roster-filters'); await choose('roster-letters'); await choose('roster-letter-c');
  assert.ok(artRequests.slice(beforeFilterArt).every(id => id === initial.state.formId), 'letter-filtered rows do not preload their artwork');
  artDemandChecks.push({ scene: 'filtered roster page', requestDelta: artRequests.slice(beforeFilterArt) });
  await detail(276); await checkScene('catalog-calumon');
  const metadata = (await http('/api/roster/276')).form;
  assert.ok((await screen.textContent()).includes(metadata.art.status === 'unavailable' ? 'Artwork unavailable' : 'Private local art available'));
  await choose('roster-stats'); for (const key of ['maxHp', 'attack', 'defense', 'magic', 'resistance']) assert.equal(await screen.locator(`[data-stat="${key}"]`).textContent(), String(metadata.combat[key]));
  await checkScene('catalog-stats'); await choose('roster-growth');
  assert.equal(await screen.locator('[data-stat=magic]').textContent(), `${metadata.baseStats.magic} · +${metadata.growth.magic}`);
  await choose('roster-moves'); await checkScene('catalog-moves'); await back(); await back(); await back('roster-detail');
  await choose('roster-notes'); await choose('roster-note-1', false); await checkScene('catalog-route-note');
  await referenceMove(272, 'p', 6, 'Blood Dance: Maracas Version', false);
  await referenceMove(89, 'f', 2, 'Scratch Smash', true);
  await menu('roster'); await choose('roster-filters'); await choose('roster-clear');
  assert.equal(posts.length, beforePosts); assert.deepEqual(await read(), beforeBrowsing);
  await journalDetail(276); assert.match(await screen.textContent(), /Discovered/);

  // Exact local art is fetched only after its own form becomes visible. No
  // private screenshot enters public evidence.
  await menu('roster'); await choose('roster-filters'); await choose('roster-letters'); await choose('roster-letter-a'); await detail(18);
  const privateArtAvailable = (await http('/api/roster/18')).form.art.status === 'private-local-available';
  if (privateArtAvailable) await page.waitForFunction(() => document.querySelector('.screen-portrait')?.getAttribute('aria-label') === 'Agumon artwork');
  else assert.match(await screen.textContent(), /Artwork unavailable/);
  assert.ok(artRequests.includes(18)); const visited = [initial.state.formId, 276, 272, 89, 18];
  assert.ok(artRequests.every(id => visited.includes(id)), `only explicitly visited actors request art: ${[...new Set(artRequests)]}`);
  artDemandChecks.push({ scene: 'five explicitly visited actors', requested: [...new Set(artRequests)], allowed: visited, requestsIncludingRevisits: artRequests.length });

  await carried(highMemberId); const selected = await command('select-companion'); assert.equal(selected.state.formId, 276);
  assert.equal(await screen.locator('[data-device-action=release-review]').isDisabled(), true, 'active partner cannot be released');
  await carried(2); await choose('release-review'); await checkScene('release-review'); await choose('release-confirmation'); await checkScene('release-confirm');
  const beforeRelease = await read(), beforeReleasePosts = posts.length;
  await back('release-review'); assert.equal(posts.length, beforeReleasePosts); assert.deepEqual(await read(), beforeRelease);
  await choose('release-confirmation');
  let committed, release;
  await page.route('**/api/save-sync', async route => { const response = await route.fetch(); committed = await response.json(); await new Promise(resolve => { release = resolve; }); await route.abort('failed'); });
  const box = await right.boundingBox(); await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
  await page.waitForFunction(() => localStorage.getItem('digivice.dev.pending.v1') !== null);
  await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
  for (let n = 0; n < 100 && !release; n++) await page.waitForTimeout(10); assert.ok(release); release();
  await onScreen('saving'); await page.waitForFunction(() => !document.querySelector('[data-device-action=retry]')?.disabled);
  assert.equal(posts.length, beforeReleasePosts + 1);
  const pending = await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1'));
  await back('saving'); assert.equal(await page.evaluate(() => localStorage.getItem('digivice.dev.pending.v1')), pending);
  const storageState = await context.storageState(); await context.close();
  await new Promise(resolve => app.server.close(resolve)); app.close(); app = await startServer({ ...binaryPaths, dataDir: directory, port });
  await open(storageState); await onScreen('saving'); const retried = await command('retry'); await onScreen('release-result');
  assert.deepEqual(retried, committed); assert.equal(posts.at(-1).batchId, JSON.parse(pending).batchId);
  assert.equal(retried.state.collection.some(member => member.id === 2), false);
  assert.equal(retried.state.activeCreatureId, highMemberId); assert.equal(retried.state.formId, 276);
  assert.deepEqual(retried.state.journal, beforeRelease.state.journal); assert.equal(retried.state.nextMemberId, beforeRelease.state.nextMemberId);
  assert.deepEqual(retried.state.collection, beforeRelease.state.collection.filter(member => member.id !== 2));
  await checkScene('release-result'); await choose('release-done');
  await journalDetail(4); assert.match(await screen.textContent(), /Discovered/, 'released form remains in journal');

  // Later captures allocate fresh IDs instead of reusing released2.
  let saved = await read(); const nextMemberId = saved.state.nextMemberId;
  for (let n = 0; n < 30 && saved.state.nextMemberId === nextMemberId; n++) {
    for (let rest = 0; rest < 20 && saved.state.hp < saved.state.combat.maxHp; rest++) saved = await seed([{ type: 'rest', value: 0 }]);
    saved = await seed([{ type: 'walk', value: 100 }, { type: 'auto', value: 0 }]);
  }
  assert.ok(saved.state.collection.some(member => member.id === nextMemberId)); assert.equal(saved.state.collection.some(member => member.id === 2), false);
  await page.reload(); await home(); await menu('battle-mode'); await choose('practice-start'); const battle = await practice('practice-tactical');
  assert.equal(battle.battle.rulesVersion, 7); assert.equal(battle.battle.playerFormId, 276); assert.equal(battle.battle.companion.id, highMemberId);
  await carried(1); assert.equal(await screen.locator('[data-device-action=release-review]').isDisabled(), true); assert.match(await screen.textContent(), /Finish or retreat from practice/);
  await menu('battle-mode'); await practice('practice-retreat'); await choose('practice-continue');
  await carried(nextMemberId); const chosen = await command('select-companion'); assert.equal(chosen.state.activeCreatureId, nextMemberId);
  await menu('explore'); await choose('wild-mode'); await command('wild-tactical'); await onScreen('explore');
  const beforeWalk = await read(); await command('walk'); await onScreen('battle'); assert.equal((await read()).state.steps, beforeWalk.state.steps + 100);
  const encounter = (await read()).state; assert.ok(['brace', 'ward', 'counter'].includes(encounter.wildGuard));
  assert.match(await screen.textContent(), new RegExp(`Rival: ${encounter.wildGuard === 'brace' ? 'Brace' : encounter.wildGuard === 'ward' ? 'Ward' : 'Counter'}`));
  await choose('heavy', false); assert.match(await screen.locator('[data-selected=true]').textContent(), /Counter/); await checkScene('wild-guard');
  // The round Cards screen is the simulated NFC input, not a physical scan.
  await choose('cards'); await onScreen('cards'); assert.match(await screen.textContent(), /SIMULATED NFC/);
  const beforeCard = await read(), card = await command('card-spark'); await onScreen('battle');
  assert.equal(card.state.cardUsed, true); assert.equal(card.state.attackBoost, 5);
  assert.equal(card.state.wildHp, beforeCard.state.wildHp); assert.equal(card.state.wildTurn, beforeCard.state.wildTurn);
  assert.deepEqual(posts.at(-1).events, [{ type: 'card', value: 1 }]);
  assert.equal(await screen.locator('[data-device-action=cards]').isDisabled(), true, 'one card per encounter');
  const afterCardAttack = await command('attack'); assert.equal(afterCardAttack.state.attackBoost, 0);
  assert.ok(afterCardAttack.state.wildHp < card.state.wildHp || afterCardAttack.state.phase === 'home');
  if (afterCardAttack.state.phase === 'encounter') {
    await carried(1); assert.equal(await screen.locator('[data-device-action=release-review]').isDisabled(), false, 'current encounter permits explicit nonactive Make room release');
  }
  assert.deepEqual(errors, []);
  const report = { result: 'PASS', sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    binaries: Object.values(binaryPaths).map(path => ({ path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex') })),
    fixtureSetup: { method: 'Legal current native events replayed through authenticated HTTP in batches of at most100; no state/RNG injection', encounters: initial.state.encounters, events: fixtureEvents.length, highFormId: highMember.formId, highMemberId },
    browserJourneys: { twoButtonGameNavigation: true, catalogSearchAndReferenceNotes: true, highFormPartnerAndPractice: true, journalBeforeAndAfterRelease: true,
      releaseBackNoPost: true, releaseLostAckRestartExactRetry: true, releasedIdNotReused: true, newMemberId: nextMemberId,
      simulatedWalk100: true, simulatedNfcSpark: { event: { type: 'card', value: 1 }, attackBoost: card.state.attackBoost, onePerEncounter: true, consumedByAttack: true },
      caveat: 'External reference handoff opens Playtest tools; its disclosure is closed by a direct click. Game navigation/actions use only left Next/hold Back and right Confirm.' },
    geometries, artDemandChecks, pageErrors: errors.length, livePreviewTouched: false };
  if (process.env.ROSTER_REPORT) writeFileSync(process.env.ROSTER_REPORT, JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
  console.log('PASS: paged255-entry roster, two-button letter filters, native highform276 stats/growth/moves/reference notes, journal, exact-form lazy artwork or honest unavailable fallback, highform capture/partner/practice, explicit nonactive release Back/noPOST, lost ACK plus browser/service restart exactretry once, journal retained, monotonic member IDs, practice lock and current wild Make room access and visible Counter tradeoff; zero page errors.');
  console.log(JSON.stringify({ geometries, screenshots, artDemandChecks, privateArtAvailable, privateArtRequests: [...new Set(artRequests)], physicalSizing: '201.6 and 140.16 CSS px nominal at 96px/in; uncalibrated, not physical LCD readability proof' }));
} finally { await browser.close(); await new Promise(resolve => app.server.close(resolve)); app.close(); rmSync(directory, { recursive: true, force: true }); }

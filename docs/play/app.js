import { CAPTURE_RING, sampleCaptureRing, captureRingChance } from './shared/capture-ring.js';
import { createCaptureRingInput } from './shared/capture-ring-input.js';
import { orderedMembers, partyChoice, validParty } from './shared/party.js';
import { validateStarterCatalog, paintStarterEgg } from './shared/starter-onboarding.js';
import { loadGameArt } from './game-art.js';
import { createDeviceTouchInput } from './device-touch-input.js';
import { createDeviceView } from './device-view.js';
import { buildBattleFrames, createBattlePlayback } from './battle-playback.js';

// Presentation only. All game decisions, random draws and saved state belong to
// the unchanged native C++ core. No service API, account or device is involved.
const SAVE_KEY = 'zark.browser-demo.v1.rules15';
const LOCK_KEY = `${SAVE_KEY}.write`;
const FORMAT = 1, DEFAULT_SEED = 12345;
const $ = id => document.getElementById(id);
const ctx = $('screen').getContext('2d');
const capturePreview = $('capture-preview').getContext('2d');
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
let core, state, starters = [], selectedStarter = 1, busy = false, needsReset = false;
let currentTab = 'play', captureMode = false, captureEpoch = 0, captureRevision = 0, ringInput;
let persistent = !!navigator.locks?.request, lastSavedRaw = null, releaseId = 0;
let releaseIntendedState = null, resetIntent = null;
let lastFrame = 0, lastActionAt = 0, displayMessage = '', logs = [];
const eggCanvases = new Map();
let gameArt = null, memberThumbs = [];
let deviceView, touchInput;
let lastCaptureInputAt = -Infinity;
const playback = createBattlePlayback();
let playbackIndex = -1, playbackScene = null, playbackForms=[];
const playbackBack = [{id:'battle-skip',label:'SKIP ANIMATION',x:126,y:348,w:160,h:38,enabled:true}];
const gameScenes = ['meadow','forest','beach','ruins','cavern','snow','volcanic','digital'];
const sceneId = () => playbackScene || (isEncounter() ? gameScenes[Math.max(0,(state.encounters || 1)-1)%gameScenes.length] : 'meadow');
function prepareArt() {
  if (!gameArt || !state) return;
  const ids = state.phase === 'egg' ? [] : [state.formId, ...(isEncounter() ? [state.wildFormId] : [])];
  if (currentTab === 'box') ids.push(...state.collection.map(member => member.formId));
  ids.push(...(deviceView?.artIds() || []));
  ids.push(...playbackForms);
  void gameArt.prepare([...new Set(ids)],sceneId());
}
function artChanged() {
  if (!state) return;
  if (currentTab === 'box') for (const {canvas,member} of memberThumbs) paintMemberThumb(canvas,member);
  paint(performance.now());
}
const color = { grove:'#91c896', tide:'#75cbd5', ember:'#efa878', neutral:'#bca0e1' };
const escapeHTML = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const fmt = value => Number(value || 0).toLocaleString();
const canPlay = () => !!state && !busy && !needsReset && !playback.active();
const isHome = () => state?.phase === 'home';
const isEncounter = () => state?.phase === 'encounter';
const canCapture = () => canPlay() && isEncounter() && state.wildCaptureChance > 0
  && (state.battleMode === 'tactical' || state.autoCapture === 1);
const show = (id, visible) => { $(id).hidden = !visible; };

function native(name, types = [], args = []) {
  const raw = core.ccall(name, 'string', types, args);
  if (typeof raw !== 'string' || raw.length > 200000) throw new Error('The game engine returned an invalid response.');
  return JSON.parse(raw);
}
function accept(result) {
  if (!result?.state || result.rulesVersion !== 15 || result.schemaVersion !== 22
    || !['egg','home','encounter'].includes(result.state.phase) || !validParty(result.state))
    throw new Error('This demo and its game engine are incompatible. Please reload the page.');
  state = result.state;
  return result;
}
function status(message, error = false) {
  displayMessage = message;
  $('game-status').textContent = message;
  $('game-status').dataset.error = String(error);
}
function log(message) {
  logs.unshift({ sequence: state?.sequence || 0, message });
  logs = logs.slice(0, 24);
  $('event-log').replaceChildren(...logs.map(entry => {
    const li = document.createElement('li');
    li.textContent = `${String(entry.sequence).padStart(3, '0')} / ${entry.message}`;
    return li;
  }));
}
function storageWarning(message) {
  $('storage-notice').textContent = message;
  show('storage-notice', true);
}
function sessionOnly(reason) {
  persistent = false;
  storageWarning(`${reason} You can keep playing, but progress will last only in this tab.`);
  $('save-status').textContent = 'Session only · not saved';
}
function readRaw() {
  if (!persistent) return null;
  try { return localStorage.getItem(SAVE_KEY); }
  catch { sessionOnly('Browser storage is unavailable.'); return null; }
}
function restore(raw) {
  if (typeof raw !== 'string' || raw.length > 8000) throw new Error('The saved demo could not be read.');
  const value = JSON.parse(raw);
  if (value.format !== FORMAT || value.rulesVersion !== 15 || value.schemaVersion !== 22
    || typeof value.snapshot !== 'string' || !/^[a-fA-F0-9]{5928}$/.test(value.snapshot))
    throw new Error('The saved demo uses an unsupported format.');
  const result = native('demo_load', ['string'], [value.snapshot]);
  if (!result.ok) throw new Error('The saved demo failed its native integrity check.');
  accept(result);
  lastSavedRaw = raw;
}
function persist() {
  if (!persistent) return;
  const snapshot = core.ccall('demo_snapshot', 'string', [], []);
  if (!/^[a-fA-F0-9]{5928}$/.test(snapshot)) throw new Error('The game engine could not save this state.');
  const raw = JSON.stringify({format:FORMAT,rulesVersion:15,schemaVersion:22,snapshot});
  try {
    localStorage.setItem(SAVE_KEY, raw);
    lastSavedRaw = raw;
    $('save-status').innerHTML = '<i class="status-dot"></i> Saved in this browser';
  } catch { sessionOnly('This browser could not save progress.'); }
}
async function locked(task) {
  if (!persistent) return task();
  return navigator.locks.request(LOCK_KEY, {mode:'exclusive'}, task);
}
function syncSaved() {
  const raw = readRaw();
  if (persistent && raw !== lastSavedRaw) {
    finishBattlePlayback({reveal:false});
    closeCapture();
    if (raw === null) accept(native('demo_reset', ['number'], [DEFAULT_SEED]));
    else restore(raw);
    lastSavedRaw = raw;
    log('Demo progress updated from another tab.');
    return true;
  }
  return false;
}
function damagedSave(error) {
  finishBattlePlayback({reveal:false});
  needsReset = true;
  closeCapture();
  storageWarning(`${error.message} Your existing save has been left untouched. Use Reset demo to start a fresh adventure.`);
}
async function command(name, value = 0) {
  if (!canPlay()) return;
  const intendedState = state;
  busy = true;
  closeCapture();
  render();
  try {
    await locked(() => {
      try {
        if (syncSaved() || state !== intendedState) {
          status('Another tab changed this adventure. Review the latest state, then try your action again.');
          return;
        }
      } catch (error) { damagedSave(error); return; }
      const result = accept(native('demo_command', ['string','number'], [name, value]));
      if (!result.ok) { status(result.error || 'That action is unavailable right now.', true); return; }
      // Native egg snapshots deliberately have no selectable battle preference.
      // Apply this browser demo's default at hatch, through the real Mode action.
      // Restored partners keep their saved choice, including Manual.
      if (name === 'hatch') {
        const mode = accept(native('demo_command', ['string','number'], ['mode', 1]));
        if (!mode.ok) throw new Error(mode.error || 'The default battle mode could not be selected.');
      }
      persist();
      lastActionAt = performance.now();
      let message = state.message || 'Ready for your next move.';
      if (name === 'demo-encounter') message = `${result.demoSteps || 0} simulated steps. ${message}`;
      if (name === 'mode') message = `Next battle: ${state.battleMode === 'auto' ? 'auto, with manual capture' : 'manual controls'}.`;
      if (name === 'ring-capture') {
        const resultName = state.lastCapture.result === 'captured' ? 'Captured!' : state.lastCapture.result === 'escaped' ? 'Escaped this attempt.' : 'Capture missed.';
        message = `${resultName} ${state.lastCapture.chance}% chance · attempt ${state.lastCapture.attempt} of 3. ${state.message}`;
      }
      if (result.trace?.steps?.length) {
        if (state.autoCapture === 1) message = `Auto paused after ${result.trace.steps.length} exchanges. Your turn to aim a capture.`;
      }
      const frames = buildBattleFrames(intendedState,result,name);
      if (frames.length && !document.hidden && currentTab === 'play') {
        playbackScene = gameScenes[Math.max(0,(intendedState.encounters||1)-1)%gameScenes.length]; playbackIndex = -1;
        playbackForms=[intendedState.formId,intendedState.wildFormId];
        playback.start(frames,{state,message,openCapture:state.phase==='encounter'&&state.autoCapture===1},performance.now());
      } else { status(message); log(message); }
    });
  } catch (error) { status(error.message || 'The action could not be completed.', true); }
  finally {
    busy = false;
    if (!playback.active()) deviceView?.sync();
    // Like the installed touch UI, Auto opens its manual timing screen as soon
    // as the shared core pauses at the capture opportunity.
    if (!playback.active() && !document.hidden && currentTab==='play' && state?.phase === 'encounter' && state.autoCapture === 1 && ['auto-fight','ring-capture'].includes(name)) openCapture(false);
    render();
  }
}

function setTab(tab, focus = false) {
  if (!['play','box','evolve'].includes(tab)) return;
  finishBattlePlayback({reveal:false});
  currentTab = tab;
  deviceView?.selectTab(tab);
  if (tab !== 'play') closeCapture();
  for (const name of ['play','box','evolve']) {
    $(`tab-${name}`).setAttribute('aria-selected', String(name === tab));
    $(`tab-${name}`).tabIndex = name === tab ? 0 : -1;
    show(`panel-${name}`, name === tab);
  }
  if (focus) $(`tab-${tab}`).focus();
  prepareArt();
  if (tab === 'box') for (const {canvas,member} of memberThumbs) paintMemberThumb(canvas,member);
  ringInput?.refresh();
}
function stat(label, value, max, css = '') {
  const percent = Math.max(0, Math.min(100, Number(value) / Math.max(1, Number(max)) * 100));
  return `<div><div class="stat-heading"><span>${escapeHTML(label)}</span><b>${fmt(value)} / ${fmt(max)}</b></div><div class="meter ${css}" role="meter" aria-label="${escapeHTML(label)}" aria-valuenow="${Number(value)}" aria-valuemin="0" aria-valuemax="${Number(max)}"><span style="width:${percent}%"></span></div></div>`;
}
function renderStarters() {
  $('starter-grid').replaceChildren(...starters.map(starter => {
    const button = document.createElement('button');
    button.type = 'button'; button.className = 'starter-card'; button.dataset.starter = starter.id;
    button.setAttribute('aria-pressed', String(starter.id === selectedStarter));
    button.setAttribute('aria-label', `${starter.name}, ${starter.combat.type} type`);
    button.disabled = !canPlay();
    const canvas = document.createElement('canvas'); canvas.width = canvas.height = 128; canvas.setAttribute('aria-hidden','true');
    paintStarterEgg(canvas, starter.id); eggCanvases.set(starter.id, canvas);
    const strong = document.createElement('strong'); strong.textContent = starter.name;
    const small = document.createElement('small'); small.textContent = starter.combat.type;
    button.append(canvas,strong,small);
    return button;
  }));
  const selected = starters.find(s => s.id === selectedStarter);
  $('starter-detail').innerHTML = selected ? `<b>${escapeHTML(selected.name)}</b> · ${escapeHTML(selected.combat.type)} type<br>Physical: ${escapeHTML(selected.combat.skills.physical)} · Magic: ${escapeHTML(selected.combat.skills.magic)}` : '';
  $('hatch').disabled = !canPlay();
}
function renderHome() {
  $('partner-name').textContent = state.creature;
  $('partner-level').textContent = `LV ${state.level}`;
  $('partner-meta').textContent = `${state.stage || 'Partner'} · ${state.combat?.type || 'neutral'} type · ${state.bond} bond · ${state.xpToNext ? `${state.xpToNext} XP to next level` : 'Maximum level'}`;
  $('care-stats').innerHTML = stat('Health',state.hp,state.combat.maxHp,'hp')+stat('Energy',state.energy,100)+stat('Fullness',state.fullness,100)+stat('Mood',state.mood,100);
  document.querySelectorAll('.care-buttons button').forEach(button => { button.disabled = !canPlay() || !isHome() || (button.dataset.command === 'play' && state.energy < 5 && state.mood < 100); });
  $('encounter').disabled = !canPlay() || !isHome();
  for (const [id,mode] of [['mode-manual','tactical'],['mode-auto','auto']]) { $(id).setAttribute('aria-pressed',String(state.battleMode === mode)); $(id).disabled = !canPlay() || !isHome(); }
}
function renderBattle() {
  $('wild-name').textContent = state.wildName;
  $('wild-meta').textContent = `Level ${state.wildLevel} · ${state.wildCombat.type} type · ${state.wildRarity || 'wild'}${state.wildGuard ? ` · ${state.wildGuard} guard` : ''}`;
  $('battle-eyebrow').textContent = captureMode ? 'MANUAL CAPTURE' : `WILD ENCOUNTER · ${state.battleMode === 'auto' ? 'AUTO' : 'MANUAL'}`;
  $('battle-stats').innerHTML = stat(state.wildName,state.wildHp,state.wildMaxHp,'hp')+stat(state.creature,state.hp,state.combat.maxHp);
  const manual = state.battleMode === 'tactical';
  show('manual-actions',manual && !captureMode);
  show('auto-actions',!manual && state.autoCapture !== 1 && !captureMode);
  for (const [action,key] of [['attack','physical'],['heavy','heavy'],['magic','magic']]) {
    $(`${key}-skill`).textContent = `${state.combat.skills[key]} · ${key === 'heavy' ? '6' : '2'} energy`;
    document.querySelector(`#manual-actions [data-command="${action}"]`).disabled = !canPlay() || !manual || (action === 'heavy' && state.energy < 6);
  }
  $('auto-fight').disabled = !canPlay();
  show('capture-card',!captureMode);
  show('capture-controls',captureMode);
  $('attempts').textContent = `${state.captureAttempts} / 3 USED`;
  let help = `Lower wild HP to half or below to open capture. Keep an eye on your partner’s health.`;
  if (state.collection.length >= state.collectionCapacity) help = 'Your box is full. Open Box and release a non-active Digimon to make room.';
  else if (state.wildCaptureChance > 0) help = `Eligible capture: ${state.wildCaptureChance}% at green timing. You have ${3-state.captureAttempts} ${3-state.captureAttempts === 1 ? 'attempt' : 'attempts'} left.`;
  $('capture-help').textContent = help;
  $('capture-open').disabled = !canCapture();
  $('capture-press').disabled = !canCapture() || !captureMode;
  show('auto-resume',!manual && state.autoCapture === 1 && !captureMode);
  $('auto-resume').disabled = !canPlay();
}
function paintMemberThumb(canvas, member) {
  const c = canvas.getContext('2d'); c.clearRect(0,0,canvas.width,canvas.height);
  if (!gameArt?.drawForm(c,member.formId,{x:canvas.width/2,y:canvas.height/2,maxSide:110,time:0,animation:'idle'})) {
    const unavailable=['missing','error'].includes(gameArt?.formStatus(member.formId));
    c.fillStyle='#6d706a';c.font='10px Arial';c.textAlign='center';c.fillText(unavailable?'Art unavailable':'Loading art…',canvas.width/2,canvas.height/2);
  }
}
function renderBox() {
  memberThumbs = [];
  const members = orderedMembers(state);
  $('box-count').textContent = String(members.length);
  $('box-capacity').textContent = `${members.length} / ${state.collectionCapacity}`;
  $('box-help').textContent = state.phase === 'egg' ? 'Hatch your egg to meet your first partner.' : isEncounter() ? 'Finish this encounter before switching partners or XP companions.' : 'Choose your active partner. Add up to three others as XP companions; they earn full battle XP without sharing care.';
  $('party-summary').textContent = state.phase === 'egg' ? 'A new collection starts with a single egg.' : `${state.partyMemberIds.length} / ${state.partyCapacity} XP companions · ${state.journal.obtainedFormIds.length} forms met`;
  $('collection').replaceChildren(...members.map(member => {
    const active = member.id === state.activeCreatureId;
    const choice = partyChoice(state,member.id);
    const article = document.createElement('article'); article.className = `member${active ? ' active' : ''}`;
    const canvas = document.createElement('canvas'); canvas.width = 124; canvas.height = 136; canvas.setAttribute('aria-hidden','true'); paintMemberThumb(canvas,member);memberThumbs.push({canvas,member});
    const details = document.createElement('div');
    details.innerHTML = `<h3>${escapeHTML(member.name)}${active ? '<span>PARTNER</span>' : choice.slot ? `<span>XP ${choice.slot}</span>` : ''}</h3><p>LV ${member.level} · ${escapeHTML(member.stage || 'Partner')} · ${escapeHTML(member.combat.type)}<br>HP ${member.hp}/${member.combat.maxHp} · bond ${member.bond} · ${member.xp} XP</p>`;
    const actions = document.createElement('div'); actions.className = 'member-actions';
    if (!active) {
      const select = document.createElement('button'); select.textContent = 'Make partner'; select.disabled = !canPlay() || !isHome(); select.addEventListener('click',()=>command('select',member.id));
      const party = document.createElement('button'); party.textContent = choice.label; party.disabled = !canPlay() || !!choice.reason; party.title = choice.detail; party.addEventListener('click',()=>command(choice.type,member.id));
      const release = document.createElement('button'); release.className = 'release'; release.textContent = 'Release'; release.disabled = !canPlay(); release.addEventListener('click',()=>{
        releaseId = member.id; releaseIntendedState = state; $('release-description').textContent = `${member.name} will leave this demo’s box. Your journal will still remember this form. This cannot be undone.`; $('release-dialog').showModal();
      });
      actions.append(select,party,release);
    }
    article.append(canvas,details); if (!active) article.append(actions);
    return article;
  }));
}
function renderEvolution() {
  const egg = state.phase === 'egg';
  $('evolution-help').textContent = egg ? 'Hatch your egg to discover its evolution paths.' : `${state.creature} · level ${state.level} · ${state.bond} bond. Care and adventures open the next path.`;
  $('evolution-progress').innerHTML = egg ? '' : stat('Level',state.level,state.maxLevel)+stat('Bond',state.bond,200);
  const options = state.evolution?.options || [];
  $('evolution-options').replaceChildren(...options.map(option => {
    const article = document.createElement('article'); article.className = 'evolution-option';
    article.innerHTML = `<h3>${escapeHTML(option.name)}</h3><p>${escapeHTML(option.stage)} · ${escapeHTML(option.combat.type)} type</p><div class="requirement"><span class="${state.level >= option.requiredLevel ? 'met' : 'unmet'}">${state.level >= option.requiredLevel ? '✓' : '○'} Level ${state.level} / ${option.requiredLevel}</span><span class="${state.bond >= option.requiredBond ? 'met' : 'unmet'}">${state.bond >= option.requiredBond ? '✓' : '○'} Bond ${state.bond} / ${option.requiredBond}</span></div>`;
    const button = document.createElement('button'); button.className = 'button secondary full-width'; button.textContent = option.eligible ? `Evolve into ${option.name} →` : isEncounter() ? 'Finish the encounter first' : 'Keep growing together'; button.disabled = !canPlay() || !option.eligible; button.addEventListener('click',()=>command('evolve',option.formId));
    article.append(button); return article;
  }));
  if (!egg && !options.length) { const empty = document.createElement('p'); empty.className = 'empty'; empty.textContent = 'This form has no further evolution in the current game catalog. You can still build its level and bond.'; $('evolution-options').append(empty); }
}
function render() {
  if (!state) return;
  if (!playback.active()) deviceView?.sync();
  show('loading',false);
  show('egg-controls',state.phase === 'egg'); show('home-controls',isHome()&&!playback.active()); show('battle-controls',isEncounter()&&!playback.active());
  $('phase-label').textContent = playback.active() ? 'BATTLE IN MOTION' : captureMode ? 'CAPTURE TIMING' : state.phase === 'egg' ? 'CHOOSE YOUR EGG' : isHome() ? 'PARTNER / HOME' : 'WILD / BATTLE';
  if (state.phase === 'egg') renderStarters();
  if (isHome()) renderHome();
  if (isEncounter()) renderBattle();
  prepareArt();
  renderBox(); renderEvolution();
  $('reset-open').disabled = !core || busy;
  $('touch-encounter').hidden = !isHome() || playback.active();
  $('touch-encounter').disabled = !canPlay();
  if (deviceView) {
    $('touch-state').textContent = deviceView.description();
    $('screen').dataset.touchScreen = deviceView.page();
  }
  $('screen').setAttribute('aria-label',state.phase === 'egg' ? `Selected egg: ${starters.find(s=>s.id===selectedStarter)?.name || 'starter'}. Choose a starter and hatch using the controls.` : isEncounter() ? `${state.creature}: ${state.hp} of ${state.combat.maxHp} health. Wild ${state.wildName}: ${state.wildHp} of ${state.wildMaxHp} health.${captureMode ? ' Capture timing ring is active. Press D or use the capture button.' : ''}` : `${state.creature}, level ${state.level}. Health ${state.hp}, energy ${state.energy}, fullness ${state.fullness}, mood ${state.mood}, bond ${state.bond}.`);
  if (deviceView) $('screen').setAttribute('aria-label',deviceView.description());
  touchInput?.refresh(); ringInput?.refresh();
  paint(performance.now());
}

function closeCapture() { captureMode = false; captureRevision++; ringInput?.refresh(); }
function openCapture(focus = true) {
  if (!canCapture()) return;
  if (currentTab !== 'play') setTab('play');
  captureMode = true; captureEpoch = performance.now(); captureRevision++; render();
  if (focus) $('capture-press').focus({preventScroll:true});
}
function ringSample(time) { return sampleCaptureRing(Math.max(0,Math.floor(time-captureEpoch)),state.wildFormId); }
function submitCapture(sample, time = performance.now()) {
  // One repeat guard across the screen, keyboard and optional HTML button.
  if (!captureMode || !canCapture() || time < lastCaptureInputAt || time-lastCaptureInputAt < 450) return;
  lastCaptureInputAt=time;
  return command('ring-capture',sample.phaseMs);
}

// Native commands and persistence finish once, before playback. This queue is
// disposable presentation: finishing, skipping, hiding or reloading never calls
// the core, spends RNG or awards XP a second time.
function finishBattlePlayback({reveal=true}={}) {
  const completed = playback.finish();
  if (!completed) return false;
  playbackScene=null; playbackIndex=-1; playbackForms=[];
  touchInput?.cancel('battle-finished');
  $('screen').dataset.battleActor=''; $('screen').dataset.battlePhase='';
  if (completed.state !== state) return false;
  status(completed.message); log(completed.message);
  deviceView?.sync();
  if (reveal && !document.hidden && currentTab==='play' && !busy && !needsReset
      && !$('reset-dialog').open && !$('release-dialog').open && completed.openCapture) openCapture(false);
  return true;
}
function battlePlaybackSample(time) {
  const sample=playback.sample(time);
  if (!sample) return null;
  if (sample.complete) {finishBattlePlayback(); render(); return null;}
  if (sample.index !== playbackIndex) {
    playbackIndex=sample.index;
    const who=sample.actor==='player'?sample.scene.creature:sample.scene.wildName;
    const target=sample.target==='player'?sample.scene.creature:sample.scene.wildName;
    const feedback=sample.damage===null?'Hit!':sample.damage===0?(sample.reflected?'Reflected!':'Guarded!'):`${sample.damage} damage`;
    const message=`Turn ${sample.turn} · ${who} → ${target}: ${sample.skill}. ${feedback}`;
    status(message); log(message);
    $('touch-state').textContent=`${message} Watch the exchange, or skip its animation.`;
    $('screen').setAttribute('aria-label',message);
  }
  $('screen').dataset.battleActor=sample.actor;
  $('screen').dataset.battlePhase=sample.phase;
  $('screen').dataset.battleTurn=String(sample.turn);
  $('screen').dataset.battleHp=`${sample.playerHp ?? '?'}/${sample.enemyHp ?? '?'}`;
  return sample;
}
function paintBattlePlayback(sample,time) {
  const s=sample.scene,p=sample.progress;
  const moving=!reducedMotion.matches;
  const lunge=moving?(p<.18?-6*Math.sin(p/.18*Math.PI):p<.38?42*Math.sin((p-.18)/.20*Math.PI/2):p<.72?42*(1-(p-.38)/.34):0):0;
  const hit=sample.impacted, recoil=moving&&sample.damage!==0&&p>=.38&&p<.74?Math.sin((p-.38)/.36*Math.PI)*12:0;
  const attackerX=sample.actor==='player'?118+lunge:294-lunge;
  const playerX=sample.actor==='player'?attackerX:118-recoil;
  const enemyX=sample.actor==='enemy'?attackerX:294+recoil;
  badge(`TURN ${sample.turn} · ${s.battleMode==='auto'?'AUTO':'MANUAL'} BATTLE`,206,63,10,'#a4ddbc',252);
  badge(s.creature,118,95,12,'#a4ddbc',155);badge(s.wildName,294,95,12,'#ffd387',155);
  badge(`HP ${sample.playerHp ?? '?'}/${s.combat.maxHp}`,118,114,9,'#d8e6d7',116);
  badge(`HP ${sample.enemyHp ?? '?'}/${s.wildMaxHp}`,294,114,9,'#d8e6d7',116);
  healthBar(64,123,108,sample.playerHp??0,s.combat.maxHp);
  if(sample.enemyHp!==null)healthBar(240,123,108,sample.enemyHp,s.wildMaxHp,'#ffd387');
  const draw=(id,x,facing,isTarget)=>{
    ctx.save();
    if(moving&&sample.damage!==0&&hit&&p<.64&&isTarget)ctx.globalAlpha=.45+.55*Math.abs(Math.cos(p*44));
    actor(id,x,193,112,time,facing);ctx.restore();
  };
  draw(s.formId,playerX,'right',sample.target==='player');
  draw(s.wildFormId,enemyX,'left',sample.target==='enemy');
  const targetX=sample.target==='player'?playerX:enemyX;
  if(hit&&p<.8) {
    if(moving&&sample.damage!==0) {
      ctx.save();ctx.strokeStyle=sample.move==='magic'?'#adf7f5':'#fff1b5';ctx.lineWidth=3;
      for(let i=0;i<8;i++){const angle=i*Math.PI/4,r=17+(p-.38)*42;ctx.beginPath();ctx.moveTo(targetX+Math.cos(angle)*r,187+Math.sin(angle)*r);ctx.lineTo(targetX+Math.cos(angle)*(r+10),187+Math.sin(angle)*(r+10));ctx.stroke();}
      ctx.restore();
    }
    badge(sample.damage===null?'HIT!':sample.damage===0?(sample.reflected?'REFLECTED':'GUARDED'):`−${sample.damage}`,targetX,153-(moving?(p-.38)*18:0),15,'#fff1b5',sample.damage===0?100:64);
  }
  const who=sample.actor==='player'?s.creature:s.wildName;
  badge(`${who} ${sample.actor==='player'?'→':'←'}`,206,270,13,sample.actor==='player'?'#a4ddbc':'#ffd387',276);
  badge(sample.skill,206,299,15,'#f5f3df',284);
  text(`${sample.index+1} / ${sample.total} ACTIONS`,206,326,9,'#c6d7c3');
  deviceView?.paintTargets(ctx,playbackBack);
}

// Sprite pixels, scene images, facing and framing come from the installed game.
// This browser HUD and its controls are presentation; the C++ core owns all rules.
function rounded(x,y,w,h,r,fill) { ctx.fillStyle=fill; ctx.beginPath(); ctx.roundRect(x,y,w,h,r); ctx.fill(); }
function text(value,x,y,size=12,fill='#f5f3df',weight=500) { ctx.fillStyle=fill;ctx.font=`${weight} ${size}px Arial`;ctx.textAlign='center';ctx.fillText(value,x,y); }
function healthBar(x,y,w,value,max,tint='#a4ddbc') { rounded(x,y,w,5,2,'#102824a8');rounded(x,y,w*Math.max(0,Math.min(1,value/Math.max(1,max))),5,2,tint); }
function badge(value,x,y,size=11,tint='#f5f3df',width=224) { rounded(x-width/2,y-size-6,width,size+15,5,'#132b2ae8');text(value,x,y,size,tint,600); }
function actor(formId,x,y,maxSide,time,facing) {
  if (gameArt?.drawForm(ctx,formId,{x,y,maxSide,time:reducedMotion.matches?0:time,animation:'idle',facing})) return;
  const status=gameArt?.formStatus(formId);
  badge(status==='missing'?'EXACT ART UNAVAILABLE':status==='error'?'ART COULD NOT LOAD':'LOADING ART…',x,y,8,'#eadcb4',maxSide+22);
}
function paint(time) {
  const battleSample=playback.active()?battlePlaybackSample(time):null;
  ctx.setTransform(2,0,0,2,0,0);ctx.imageSmoothingEnabled=false;ctx.clearRect(0,0,412,412);
  ctx.fillStyle='#183331';ctx.fillRect(0,0,412,412);
  const scene=state?sceneId():'meadow';
  if (gameArt?.drawBackground(ctx,scene,0,0,412,412)) {
    // Native scene rendering dims its JPEG to 60% beneath the UI and sprites.
    ctx.fillStyle='#0006';ctx.fillRect(0,0,412,412);
  }
  if(!state){text('z-ark',206,188,38,'#e2ecdb',700);text('A LITTLE WORLD IS WAKING UP',206,222,10,'#c6d7c3');return;}
  if(battleSample){paintBattlePlayback(battleSample,time);return;}
  if (deviceView && !captureMode) {
    deviceView.paint(ctx,{art:gameArt,eggs:eggCanvases,time,reducedMotion:reducedMotion.matches});
    return;
  }
  const egg = state.phase === 'egg';
  badge(egg?'CHOOSE YOUR PARTNER':captureMode?'CAPTURE / TIMING':isEncounter()?(state.battleMode==='auto'?'AUTO BATTLE':'MANUAL BATTLE'):'YOUR LITTLE ADVENTURE',206,65,10,'#a4ddbc');
  if(egg){
    const starter=starters.find(s=>s.id===selectedStarter);const image=eggCanvases.get(selectedStarter);
    if(image){const bob=reducedMotion.matches?0:Math.sin(time*.0018)*3;ctx.drawImage(image,116,102+bob,180,180);}
    badge(starter?.name||'Choose an egg',206,314,22,'#f5f3df',254);badge('AUTO BATTLES · MANUAL CAPTURE',206,348,9,'#c6d7c3',236);
  }else if(captureMode){
    const sample=ringSample(time);const tint={green:'#9ee2a9',orange:'#ffbd66',red:'#ff806b'}[sample.grade];
    const x=206,y=176;
    actor(state.wildFormId,x,y,176,time);
    ctx.strokeStyle='#162c2adb';ctx.lineWidth=CAPTURE_RING.bandHalfWidth*2;ctx.beginPath();ctx.arc(x,y,sample.targetRadius,0,Math.PI*2);ctx.stroke();
    ctx.strokeStyle='#e4f4de';ctx.lineWidth=2;ctx.setLineDash([4,5]);ctx.beginPath();ctx.arc(x,y,sample.targetRadius,0,Math.PI*2);ctx.stroke();ctx.setLineDash([]);
    ctx.strokeStyle=tint;ctx.lineWidth=5;ctx.beginPath();ctx.arc(x,y,sample.radiusQ8/256,0,Math.PI*2);ctx.stroke();
    badge(state.wildName,206,306,19,'#f5f3df',258);
    badge(`${sample.grade.toUpperCase()} · ${captureRingChance(state.wildCaptureChance,sample.grade)}% CHANCE`,206,339,11,tint,244);
    if (deviceView) deviceView.paintTargets(ctx);
    else badge('TAP THE SCREEN / PRESS D',206,364,9,'#c6d7c3',226);
    // This live mirror keeps the same timing visible beside mobile controls.
    capturePreview.clearRect(0,0,412,310);capturePreview.imageSmoothingEnabled=false;
    capturePreview.drawImage($('screen'),0,94,824,620,0,0,412,310);
  }else if(isEncounter()){
    badge(state.creature,118,95,12,'#a4ddbc',155);badge(state.wildName,294,95,12,'#ffd387',155);
    badge(`HP ${state.hp} / ${state.combat.maxHp}`,118,114,9,'#d8e6d7',114);badge(`HP ${state.wildHp} / ${state.wildMaxHp}`,294,114,9,'#d8e6d7',114);
    healthBar(64,120,108,state.hp,state.combat.maxHp);healthBar(240,120,108,state.wildHp,state.wildMaxHp,'#ffd387');
    actor(state.formId,118,185,112,time,'right');actor(state.wildFormId,294,185,112,time,'left');
    badge(state.autoCapture===1?'YOUR TURN TO CAPTURE':'READY FOR THE NEXT EXCHANGE',206,291,12,'#a4ddbc',282);
    badge(`LV ${state.level}  /  WILD LV ${state.wildLevel}`,206,321,10,'#d8e6d7',212);
    badge(state.battleMode==='auto'?'AUTO FIGHTS · YOU AIM THE CAPTURE':'CHOOSE A MOVE IN THE CONTROLS',206,350,9,'#d8e6d7',264);
  }else{
    badge(state.creature,206,97,22,'#f5f3df',258);
    actor(state.formId,206,196,176,time);
    badge(`LV ${state.level} · ${(state.stage||'PARTNER').toUpperCase()} · ${state.combat.type.toUpperCase()}`,206,306,10,'#a4ddbc',248);
    badge(`${state.collection.length} IN BOX  /  ${state.partyMemberIds.length} XP COMPANIONS`,206,339,9,'#d8e6d7',242);
    badge(`${state.battleMode==='auto'?'AUTO':'MANUAL'} BATTLES · MANUAL CAPTURE`,206,364,8,'#d8e6d7',224);
  }
}

function frame(time) {
  if(!document.hidden && (time-lastFrame>32 || captureMode)){paint(time);lastFrame=time;}
  requestAnimationFrame(frame);
}

document.querySelectorAll('[data-tab]').forEach(button=>{
  button.addEventListener('click',()=>setTab(button.dataset.tab));
  button.addEventListener('keydown',event=>{
    const tabs=['play','box','evolve'];let index=tabs.indexOf(currentTab);
    if(event.key==='ArrowRight')index=(index+1)%3;else if(event.key==='ArrowLeft')index=(index+2)%3;else if(event.key==='Home')index=0;else if(event.key==='End')index=2;else return;
    event.preventDefault();setTab(tabs[index],true);
  });
});
$('starter-grid').addEventListener('click',event=>{
  const button=event.target.closest('[data-starter]');if(!button||!canPlay())return;
  selectedStarter=Number(button.dataset.starter);render();document.querySelector(`[data-starter="${selectedStarter}"]`)?.focus({preventScroll:true});paint(performance.now());
});
$('hatch').addEventListener('click',()=>command('hatch',selectedStarter));
document.querySelectorAll('[data-command]').forEach(button=>button.addEventListener('click',()=>command(button.dataset.command)));
document.querySelectorAll('[data-mode]').forEach(button=>button.addEventListener('click',()=>command('mode',Number(button.dataset.mode))));
$('encounter').addEventListener('click',()=>command('demo-encounter'));
$('touch-encounter').addEventListener('click',()=>command('demo-encounter'));
$('auto-fight').addEventListener('click',()=>command('auto-fight'));
$('auto-resume').addEventListener('click',()=>command('auto-resume'));
$('capture-open').addEventListener('click',openCapture);
$('capture-cancel').addEventListener('click',()=>{closeCapture();render();$('capture-open').focus({preventScroll:true});});
$('reset-open').addEventListener('click',()=>{finishBattlePlayback({reveal:false});closeCapture();render();resetIntent={state,raw:readRaw()};$('reset-dialog').showModal();$('reset-cancel').focus();});
$('reset-cancel').addEventListener('click',()=>$('reset-dialog').close());
$('reset-confirm').addEventListener('click',async()=>{
  if(!core||busy)return;
  $('reset-dialog').close();busy=true;closeCapture();
  try{await locked(()=>{
    if(!resetIntent || state!==resetIntent.state || (persistent && readRaw()!==resetIntent.raw)){
      try{syncSaved();}catch(error){damagedSave(error);}
      status('Another tab changed this adventure. Review the latest state before resetting.');
      return;
    }
    accept(native('demo_reset',['number'],[DEFAULT_SEED]));needsReset=false;selectedStarter=1;lastSavedRaw=null;persist();if(persistent)show('storage-notice',false);logs=[];status('A fresh egg. A fresh little adventure.');log('Demo reset.');setTab('play');
  });}
  catch(error){status(error.message,true);}finally{resetIntent=null;busy=false;render();}
});
$('release-cancel').addEventListener('click',()=>$('release-dialog').close());
$('release-confirm').addEventListener('click',()=>{
  $('release-dialog').close();
  if(releaseId && state===releaseIntendedState)command('release',releaseId);
  else status('This adventure changed. Review the current box before releasing a Digimon.');
  releaseId=0;releaseIntendedState=null;
});
window.addEventListener('storage',event=>{
  if(event.key!==SAVE_KEY||!core||!persistent)return;
  locked(()=>{try{if(syncSaved()){status('Demo progress updated from another tab.');render();}}catch(error){damagedSave(error);render();}}).catch(error=>status(error.message,true));
});
document.addEventListener('visibilitychange',()=>{if(document.hidden){const playing=finishBattlePlayback({reveal:false});if(captureMode||playing){closeCapture();render();}}});
window.addEventListener('pagehide',()=>{finishBattlePlayback({reveal:false});closeCapture();ringInput?.cancel('pagehide');});
$('screen').addEventListener('keydown',event=>{
  if(playback.active()&&event.key==='Escape'){event.preventDefault();finishBattlePlayback();render();return;}
  if(!deviceView||!canPlay()||captureMode||event.repeat||event.altKey||event.ctrlKey||event.metaKey
    ||$('reset-dialog').open||$('release-dialog').open)return;
  if(event.key==='ArrowLeft'||event.key==='ArrowRight'){event.preventDefault();deviceView.horizontal(event.key==='ArrowRight');}
  else if(event.key==='ArrowUp'&&deviceView.mode()==='battle'){event.preventDefault();deviceView.battleCommit();}
  else if(event.key==='Escape'){event.preventDefault();deviceView.activate('back');}
  else if(['Enter',' '].includes(event.key)){
    const primary=deviceView.targets().find(t=>t.enabled&&!['back','previous','next'].includes(t.id));
    if(primary){event.preventDefault();deviceView.activate(primary.id);}
  }
});

async function init() {
  requestAnimationFrame(frame);
  try {
    if(typeof WebAssembly!=='object')throw new Error('This browser does not support WebAssembly.');
    const {default:createDemoCore}=await import('./runtime/demo-core.js');
    core=await createDemoCore();
    starters=validateStarterCatalog(native('demo_starters'));
    accept(native('demo_reset',['number'],[DEFAULT_SEED]));
    if(!persistent)sessionOnly('This browser cannot safely coordinate a shared save across tabs.');
    await locked(()=>{
      const raw=readRaw();
      if(raw !== null){try{restore(raw);}catch(error){damagedSave(error);}}
      else persist();
    });
    if(persistent&&!needsReset)$('save-status').innerHTML='<i class="status-dot"></i> Saved in this browser';
    status(needsReset?'Use Reset demo to replace the unreadable save.':state.phase==='egg'?'Choose an egg and hatch your first partner.':state.message);
    log(state.phase==='egg'?'Your browser adventure begins.':'Continued your saved browser adventure.');
    deviceView=createDeviceView({
      getState:()=>state,getStarters:()=>starters,getStarter:()=>selectedStarter,
      setStarter:id=>{selectedStarter=id;renderStarters();},canPlay,canCapture,command,
      isCapture:()=>captureMode,openCapture:()=>openCapture(false),closeCapture,
      notify:message=>status(message),changed:()=>render(),
    });
    // Registered first: round-screen pointers use native device geometry.
    // The unchanged shared helper below still owns D and accessible capture.
    touchInput=createDeviceTouchInput($('screen'),{
      canInteract:()=>!!state&&!busy&&!needsReset&&!$('reset-dialog').open&&!$('release-dialog').open,
      getContext:()=>`${state?.sequence}/${deviceView.context()}/${captureRevision}/${busy}/${needsReset}/${playback.active()}`,
      getMode:()=>playback.active()?'buttons':deviceView.mode(),getTargets:()=>playback.active()?playbackBack:deviceView.targets(),getBrowseBand:()=>deviceView.browseBand(),
      onTarget:id=>{if(id==='battle-skip'){finishBattlePlayback();render();}else deviceView.activate(id);},onHorizontal:delta=>deviceView.horizontal(delta>0),
      onBattleCommit:()=>deviceView.battleCommit(),onCapture:({time})=>submitCapture(ringSample(time),time),
    });
    ringInput=createCaptureRingInput($('screen'),{
      actionButton:$('capture-press'),canArm:()=>captureMode&&currentTab==='play'&&canCapture()&&!touchInput?.contactActive(),
      getRevision:()=>captureRevision,sample:ringSample,
      onPress:({sample})=>submitCapture(sample),
      onCancel:reason=>{if(['blur','hidden','resize'].includes(reason)&&captureMode){closeCapture();render();}},
    });
    if(state.phase==='encounter'&&state.autoCapture===1)openCapture(false);
    render();
    try { gameArt=await loadGameArt({onChange:artChanged});prepareArt(); }
    catch { $('art-status').textContent='In-game art could not load. Reload to try again.'; }
  } catch(error) {
    show('loading',false);
    $('fatal').textContent=`The playable engine could not start. ${error.message} Reload to try again, or return to the showcase for the recorded samples.`;
    show('fatal',true);status('Game engine unavailable.',true);$('save-status').textContent='Game not started';
  }
}
init();

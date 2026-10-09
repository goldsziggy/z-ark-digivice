import { CAPTURE_RING, sampleCaptureRing, captureRingChance } from './shared/capture-ring.js';
import { createCaptureRingInput } from './shared/capture-ring-input.js';
import { orderedMembers, partyChoice, validParty } from './shared/party.js';
import { validateStarterCatalog, paintStarterEgg, paintRookiePlaceholder } from './shared/starter-onboarding.js';

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
const color = { grove:'#91c896', tide:'#75cbd5', ember:'#efa878', neutral:'#bca0e1' };
const escapeHTML = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const fmt = value => Number(value || 0).toLocaleString();
const canPlay = () => !!state && !busy && !needsReset;
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
        for (const step of result.trace.steps) log(`Turn ${step.turn}: ${step.action}; partner HP ${step.playerHpAfter}, wild HP ${step.enemyHpAfter}.`);
        if (state.autoCapture === 1) message = `Auto paused after ${result.trace.steps.length} exchanges. Your turn to aim a capture.`;
      }
      status(message);
      log(message);
    });
  } catch (error) { status(error.message || 'The action could not be completed.', true); }
  finally { busy = false; render(); }
}

function setTab(tab, focus = false) {
  if (!['play','box','evolve'].includes(tab)) return;
  currentTab = tab;
  if (tab !== 'play') closeCapture();
  for (const name of ['play','box','evolve']) {
    $(`tab-${name}`).setAttribute('aria-selected', String(name === tab));
    $(`tab-${name}`).tabIndex = name === tab ? 0 : -1;
    show(`panel-${name}`, name === tab);
  }
  if (focus) $(`tab-${tab}`).focus();
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
  paintRookiePlaceholder(c,canvas.width/2,canvas.height*.36,canvas.width*.85,member.combat?.type);
}
function renderBox() {
  const members = orderedMembers(state);
  $('box-count').textContent = String(members.length);
  $('box-capacity').textContent = `${members.length} / ${state.collectionCapacity}`;
  $('box-help').textContent = state.phase === 'egg' ? 'Hatch your egg to meet your first partner.' : isEncounter() ? 'Finish this encounter before switching partners or XP companions.' : 'Choose your active partner. Add up to three others as XP companions; they earn full battle XP without sharing care.';
  $('party-summary').textContent = state.phase === 'egg' ? 'A new collection starts with a single egg.' : `${state.partyMemberIds.length} / ${state.partyCapacity} XP companions · ${state.journal.obtainedFormIds.length} forms met`;
  $('collection').replaceChildren(...members.map(member => {
    const active = member.id === state.activeCreatureId;
    const choice = partyChoice(state,member.id);
    const article = document.createElement('article'); article.className = `member${active ? ' active' : ''}`;
    const canvas = document.createElement('canvas'); canvas.width = 124; canvas.height = 136; canvas.setAttribute('aria-hidden','true'); paintMemberThumb(canvas,member);
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
  show('loading',false);
  show('egg-controls',state.phase === 'egg'); show('home-controls',isHome()); show('battle-controls',isEncounter());
  $('phase-label').textContent = captureMode ? 'CAPTURE TIMING' : state.phase === 'egg' ? 'CHOOSE YOUR EGG' : isHome() ? 'PARTNER / HOME' : 'WILD / BATTLE';
  if (state.phase === 'egg') renderStarters();
  if (isHome()) renderHome();
  if (isEncounter()) renderBattle();
  renderBox(); renderEvolution();
  $('reset-open').disabled = !core || busy;
  $('screen').setAttribute('aria-label',state.phase === 'egg' ? `Selected egg: ${starters.find(s=>s.id===selectedStarter)?.name || 'starter'}. Choose a starter and hatch using the controls.` : isEncounter() ? `${state.creature}: ${state.hp} of ${state.combat.maxHp} health. Wild ${state.wildName}: ${state.wildHp} of ${state.wildMaxHp} health.${captureMode ? ' Capture timing ring is active. Press D or use the capture button.' : ''}` : `${state.creature}, level ${state.level}. Health ${state.hp}, energy ${state.energy}, fullness ${state.fullness}, mood ${state.mood}, bond ${state.bond}.`);
  ringInput?.refresh();
  paint(performance.now());
}

function closeCapture() { captureMode = false; captureRevision++; ringInput?.refresh(); }
function openCapture() {
  if (!canCapture()) return;
  captureMode = true; captureEpoch = performance.now(); captureRevision++; render();
  $('capture-press').focus({preventScroll:true});
}
function ringSample(time) { return sampleCaptureRing(Math.max(0,Math.floor(time-captureEpoch)),state.wildFormId); }

// The scenery, lighting and movement below are presentation only, drawn here.
function rounded(x,y,w,h,r,fill) { ctx.fillStyle=fill; ctx.beginPath(); ctx.roundRect(x,y,w,h,r); ctx.fill(); }
function text(value,x,y,size=12,fill='#284337',weight=500) { ctx.fillStyle=fill;ctx.font=`${weight} ${size}px Arial`;ctx.textAlign='center';ctx.fillText(value,x,y); }
function healthBar(x,y,w,value,max,tint='#48734d') { rounded(x,y,w,5,2,'#16372a20');rounded(x,y,w*Math.max(0,Math.min(1,value/Math.max(1,max))),5,2,tint); }
function scenery(time) {
  const sky=ctx.createLinearGradient(0,0,0,412);sky.addColorStop(0,'#dce8ce');sky.addColorStop(.63,'#eef0d7');sky.addColorStop(1,'#bdcca5');ctx.fillStyle=sky;ctx.fillRect(0,0,412,412);
  ctx.fillStyle='#fffce075';ctx.beginPath();ctx.arc(301,111,33,0,Math.PI*2);ctx.fill();
  ctx.fillStyle='#c4d3ae';ctx.beginPath();ctx.moveTo(0,265);ctx.quadraticCurveTo(96,144,229,264);ctx.quadraticCurveTo(335,185,412,250);ctx.lineTo(412,412);ctx.lineTo(0,412);ctx.fill();
  ctx.fillStyle='#aac294';ctx.beginPath();ctx.moveTo(0,319);ctx.quadraticCurveTo(124,208,265,311);ctx.quadraticCurveTo(346,261,412,289);ctx.lineTo(412,412);ctx.lineTo(0,412);ctx.fill();
  ctx.strokeStyle='#eef0cf50';ctx.lineWidth=1;
  for(let i=0;i<9;i++){ctx.beginPath();ctx.moveTo(i*65-50,412);ctx.lineTo(206+(i-4)*25,278);ctx.stroke();}
  for(const y of [321,356,399]){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(412,y);ctx.stroke();}
  ctx.fillStyle='#789661';for(let i=0;i<11;i++){const x=(i*137+23)%412,y=291+(i*43)%86;ctx.fillRect(x,y,3,5);ctx.fillRect(x+4,y+3,3,4);}
  if(!reducedMotion.matches){ctx.fillStyle='#f9f4cf88';for(let i=0;i<7;i++){const x=(i*67+time*.006)%412,y=110+(i*51)%180+Math.sin(time*.001+i)*6;ctx.fillRect(x,y,2,2);}}
}
function buddy(x,y,size,type,time) {
  const bob=reducedMotion.matches?0:Math.sin(time*.0025)*2;
  ctx.fillStyle='#4a674628';ctx.beginPath();ctx.ellipse(x,y+size*.36,size*.3,size*.075,0,0,Math.PI*2);ctx.fill();
  paintRookiePlaceholder(ctx,x,y+bob,size,type);
}
function paint(time) {
  ctx.setTransform(2,0,0,2,0,0); ctx.clearRect(0,0,412,412); scenery(time);
  if(!state){text('z-ark',206,188,38,'#284337',700);text('A LITTLE WORLD IS WAKING UP',206,222,10,'#607356');return;}
  const egg = state.phase === 'egg';
  rounded(94,34,224,31,15,'#f8f9e6b5');
  text(egg?'CHOOSE YOUR PARTNER':captureMode?'CAPTURE / TIMING':isEncounter()?'WILD ENCOUNTER':'YOUR LITTLE ADVENTURE',206,54,10,'#3f5844',600);
  if(egg){
    const starter=starters.find(s=>s.id===selectedStarter);const image=eggCanvases.get(selectedStarter);
    if(image){const bob=reducedMotion.matches?0:Math.sin(time*.0018)*3;ctx.drawImage(image,116,102+bob,180,180);}
    text(starter?.name||'Choose an egg',206,312,25,'#284337',600);text('A NEW ADVENTURE STARTS HERE',206,340,9,'#55704c');
  }else if(captureMode){
    const sample=ringSample(time);const tint={green:'#367548',orange:'#bf7920',red:'#d24935'}[sample.grade];
    const x=206,y=185;
    ctx.fillStyle='#f5f6dd65';ctx.beginPath();ctx.arc(x,y,112,0,Math.PI*2);ctx.fill();
    ctx.strokeStyle='#53755022';ctx.lineWidth=CAPTURE_RING.bandHalfWidth*2;ctx.beginPath();ctx.arc(x,y,sample.targetRadius,0,Math.PI*2);ctx.stroke();
    ctx.strokeStyle='#375b4075';ctx.lineWidth=2;ctx.setLineDash([4,5]);ctx.beginPath();ctx.arc(x,y,sample.targetRadius,0,Math.PI*2);ctx.stroke();ctx.setLineDash([]);
    buddy(x,y-3,73,state.wildCombat.type,time);
    ctx.strokeStyle=tint;ctx.lineWidth=5;ctx.beginPath();ctx.arc(x,y,sample.radiusQ8/256,0,Math.PI*2);ctx.stroke();
    text(state.wildName,206,313,20,'#284337',600);
    text(`${sample.grade.toUpperCase()} · ${captureRingChance(state.wildCaptureChance,sample.grade)}% CHANCE`,206,340,11,tint,700);
    text('TAP THE SCREEN / PRESS D',206,361,9,'#55704c');
    // A cropped live mirror keeps the timing visible beside the capture button
    // on a narrow screen. It samples no new timing and introduces no game rules.
    capturePreview.clearRect(0,0,412,310);
    capturePreview.drawImage($('screen'),0,110,824,620,0,0,412,310);
  }else if(isEncounter()){
    const pulse=!reducedMotion.matches && time-lastActionAt<280?Math.sin((time-lastActionAt)/280*Math.PI)*8:0;
    text(state.wildName,250,95,17,'#284337',600);text(`LV ${state.wildLevel} · ${state.wildCombat.type.toUpperCase()}`,250,112,9,'#55704c');healthBar(185,121,130,state.wildHp,state.wildMaxHp,'#cd5b42');
    buddy(268-pulse,182,105,state.wildCombat.type,time+700);
    buddy(145+pulse,269,110,state.combat.type,time);
    text(state.creature,148,356,17,'#284337',600);healthBar(83,367,130,state.hp,state.combat.maxHp);
    text(`${state.hp} / ${state.combat.maxHp}`,270,370,10,'#3d593e');
  }else{
    text(state.creature,206,108,27,'#284337',600);text(`LV ${state.level} · ${(state.stage||'PARTNER').toUpperCase()} · ${state.combat.type.toUpperCase()}`,206,130,9,'#55704c');
    buddy(206,229,165,state.combat.type,time);
    rounded(119,334,174,29,14,'#f8f9e6b5');text(`${state.collection.length} IN BOX  /  ${state.partyMemberIds.length} XP COMPANIONS`,206,353,8,'#3f5844',600);
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
  selectedStarter=Number(button.dataset.starter);renderStarters();document.querySelector(`[data-starter="${selectedStarter}"]`)?.focus({preventScroll:true});paint(performance.now());
});
$('hatch').addEventListener('click',()=>command('hatch',selectedStarter));
document.querySelectorAll('[data-command]').forEach(button=>button.addEventListener('click',()=>command(button.dataset.command)));
document.querySelectorAll('[data-mode]').forEach(button=>button.addEventListener('click',()=>command('mode',Number(button.dataset.mode))));
$('encounter').addEventListener('click',()=>command('demo-encounter'));
$('auto-fight').addEventListener('click',()=>command('auto-fight'));
$('auto-resume').addEventListener('click',()=>command('auto-resume'));
$('capture-open').addEventListener('click',openCapture);
$('capture-cancel').addEventListener('click',()=>{closeCapture();render();$('capture-open').focus({preventScroll:true});});
$('reset-open').addEventListener('click',()=>{closeCapture();render();resetIntent={state,raw:readRaw()};$('reset-dialog').showModal();$('reset-cancel').focus();});
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
document.addEventListener('visibilitychange',()=>{if(document.hidden&&captureMode){closeCapture();render();}});
window.addEventListener('pagehide',()=>ringInput?.cancel('pagehide'));

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
    ringInput=createCaptureRingInput($('screen'),{
      actionButton:$('capture-press'),canArm:()=>captureMode&&currentTab==='play'&&canCapture(),
      getRevision:()=>captureRevision,sample:ringSample,
      onPress:({sample})=>command('ring-capture',sample.phaseMs),
      onCancel:reason=>{if(['blur','hidden','resize'].includes(reason)&&captureMode){closeCapture();render();}},
    });
    render();
  } catch(error) {
    show('loading',false);
    $('fatal').textContent=`The playable engine could not start. ${error.message} Reload to try again, or return to the showcase for the recorded samples.`;
    show('fatal',true);status('Game engine unavailable.',true);$('save-status').textContent='Game not started';
  }
}
init();

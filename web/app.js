import { validCare, validLastCapture, careSummary, captureReport } from './care-capture-state.js';
import { validWalkingState } from './walking-state.js';
import builtinPack from './builtin-pack.js';
import { setupAssets, prepareFrames } from './asset-library.js';
import { setupPersonalArt } from './personal-art.js';
import { AudioEngine } from './audio-engine.js';
import { setupGarageLibrary } from './garage-library.js';
import { createDeviceNavigation, handleDeviceKey } from './device-navigation.js';
import { createDeviceScreen } from './device-screen.js';
import { createBattleClient } from './battle-client.js';
import { setupTwoButtonInput } from './two-button-input.js';
import { createBackgroundPlayer, BACKGROUND_SCENES } from './background-player.js';
import { validateStarterCatalog, isEggState, paintStarterEgg, paintRookiePlaceholder } from './starter-onboarding.js';
import { validateAutoTrace, autoStepText, autoResultTitle, captureChoice } from './auto-battle.js';
import { validProgressCombat, validateEvolutionOptions, evolutionRequirements, paintFormPlaceholder } from './progression.js';
import { createFormArt } from './form-art.js';
import { fetchRosterPage, fetchRosterDetail, fetchEvolutionGraph, fetchRosterIds, rosterReferences, rosterEncyclopediaMoves, ROSTER_PAGE_SIZE, ROSTER_STAGES } from './roster-client.js';
import { validRecoveryCount, recoveryReview, reviewedRecoveryEvents, validEncounterRarity, rarityLabel } from './care-actions.js';
import { createCaptureGesture } from './capture-gesture.js';
import { packCaptureFlick, decodeCaptureFlick, captureFlightPoint } from './capture-trajectory.js';

// This browser harness sends commands to the service's native game-core runner.
// Canvas animation is presentation only; no game rules are duplicated in JS.
const $ = (id) => document.getElementById(id);
const KEYS = { identity: 'digivice.dev.identity.v1', pending: 'digivice.dev.pending.v1', playtests: 'digivice.dev.playtests.v1' };
let storageCorrupt = false;
const canvas = $('display');
const ctx = canvas.getContext('2d');
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
let identity = readStored(KEYS.identity);
let pending = readStored(KEYS.pending);
let repairAttempt = null;
function needsTestEncounterRepair(state = game) {
  const pendingId = state?.walking?.pendingEncounter?.formId;
  return Boolean(state && (state.phase === 'encounter' && state.wildFormId >= 1 && state.wildFormId <= 10 || pendingId >= 1 && pendingId <= 10));
}
let game = null;
let revision = 0;
let pack = builtinPack;
let originalPacks = { [builtinPack.packId]: builtinPack };
let personalPack = null;
let artGeneration = 0;
let collectionSignature = '';
let playerFrames = null;
let wildFrames = null;
let preparedPlayer = null;
let preparedWild = null;
let effectFrames = null;
let effectStarted = 0;
let effectTarget = 'wild';
let transitionBusy = false;
let captureWindup = null;
let captureGesture = null, captureAim = null, captureFlight = null;
let captureFlickSupported = false;
let sceneState = null;
let presentation = null;
let stageWait = null;
let savedPlaytests = [];
let playtestsCorrupt = false;
let playtestOptionsSignature = '';
let online = false;
let busy = false;
let conflict = false;
let pairingCode = null;
let connectionError = '';
let storageAvailable = true;
let ownsWriter = false;
let releaseWriter = null;
let lastFrame = 0;
let animationFrame = 0;
const notes = [];
const audio = new AudioEngine({ storage: (() => { try { return localStorage; } catch { return null; } })() });
let deviceReady = false;
let assetLibrary = null;
let assetState = { ready: false, busy: false, packs: [], selectedId: 'scene-meadow-v1', phase: 'loading', received: 0, total: 0, message: 'Restoring saved artwork…' };
const backgroundPlayer = createBackgroundPlayer({ onChange: () => { if (deviceReady) { syncBackgroundStatus(); scheduleDraw(); } } });
const battleClient = createBattleClient({ storage: (() => { try { return localStorage; } catch { return null; } })(), onChange: () => { if (deviceReady) render(); } });
const formArt = createFormArt({ getCredential: () => identity, onChange: () => { artGeneration++; if (deviceReady) render(); } });
const RULES_VERSION = 13;
const SCHEMA_VERSION = 17;
const MAX_MEMBER_ID = 4294967294;
let startersOwner = null;
let starters = [], startersBusy = false, startersError = '', draftStarterId = 1;
let hatchedIdentity = null;
const hatchEgg = document.createElement('canvas'); hatchEgg.width = hatchEgg.height = 128;
const CREATURE_DESCRIPTIONS = {
  mote: 'A curious forest sprout with unequal leaf ears.',
  glint: 'A bright woodland companion with a growing leaf crest.',
  lumen: 'A luminous forest guardian with a lasting bond.',
  flicker: 'A small lantern moth with amber wings.',
  rill: 'A cheerful tide-pool wanderer with a forked tail.',
  brine: 'A reef companion with broad, ribbed sail fins.',
  pelagia: 'A crowned sea dragon with a sweeping tail.',
  cinder: 'A warm little ember with a flickering crown.',
  scoria: 'A bright fire companion with jagged shoulders.',
  pyrel: 'A spirited guardian with sweeping fire wings.',
};

let twoButtonInput = null;
let inputScreen = null;
let companionDetailId = 1;
let companionPage = 0;
const COMPANIONS_PER_PAGE = 3;
let partnerConfirmation = null;
let savesPage = 0;
let devicePendingReturn = null;
let lastDevicePhase = null;
let deviceInputMode = new URLSearchParams(location.search).get('controls') === 'buttons' ? 'buttons' : 'touch';
$('device-input-mode').value = deviceInputMode;
document.body.dataset.deviceInput = deviceInputMode;
const deviceScreen = createDeviceScreen($('device-ui'), { activate: deviceAction, select: index => navigation.select(index), move: delta => navigation.move(delta), paintPortrait: paintRosterPortrait });
const navigation = createDeviceNavigation({ getItems: screen => deviceView(screen).items, onChange: renderDevice });
deviceReady = true;
const PRACTICE_LABELS = { physical: 'Quick Claw', heavy: 'Power Crush', magic: 'Spark Bolt', brace: 'Brace', counter: 'Reversal', ward: 'Rune Ward' };
let practiceLastAction = null;
let practiceRequestRunning = false;
let practiceDraftMode = 'tactical';
let wildAutoTrace = null;
let autoPlayback = null;
let progressionMemberId = 1, draftEvolutionId = null, treeNodeId = null;
let evolutionDraft = null, evolutionConfirmation = null;
let recoveryDraft = null;
let releaseDraft = null, releaseConfirmation = null;
let evolutionTreeBusy = false, evolutionTreeError = '';
let evolutionGraph = null, graphNode = null, graphFocusId = null, graphOffset = 0, graphLinksOffset = 0, graphRequest = 0;
let graphLinkNames = new Map();
let rosterQuery = { offset: 0, q: '', prefix: '', stage: '' };
let rosterPage = null, rosterDetail = null, rosterSelectedId = null;
let rosterBusy = false, rosterError = '', rosterRequest = 0;
let journalOffset = 0, journalPage = null;

async function loadRoster(mode = 'page', id = null) {
  const request = ++rosterRequest;
  rosterBusy = true; rosterError = '';
  if (mode === 'detail') { rosterSelectedId = id; rosterDetail = null; }
  renderDevice();
  try {
    const ids = (game?.journal.obtainedFormIds || []).filter(id => id >= 11);
    const result = mode === 'detail' ? await fetchRosterDetail(id) : mode === 'journal'
      ? ids.length ? await fetchRosterIds(ids.slice(journalOffset, journalOffset + ROSTER_PAGE_SIZE)) : { entries: [], total: 0 }
      : await fetchRosterPage(rosterQuery);
    if (request !== rosterRequest) return;
    if (mode === 'detail') rosterDetail = result;
    else if (mode === 'journal') journalPage = result;
    else rosterPage = result;
  } catch { if (request === rosterRequest) rosterError = 'Catalog unavailable. Your game and journal are kept. Try again when connected.'; }
  finally { if (request === rosterRequest) { rosterBusy = false; navigation.refresh(); renderRosterTools(); } }
}
function showRosterForm(id) { navigation.go('roster-detail'); void loadRoster('detail', id); }
function rosterNotes(form) {
  if (!form) return [];
  return [
    ['Obtainability', form.obtainable ? `Encounter and capture this form in the appropriate stage range. ${form.evolution.children.length ? 'Reviewed prototype routes are also available.' : ''}` : 'This form is not currently encounter-obtainable.'],
    ['Evolution', form.evolution.children.length ? `${form.evolution.children.length} authored prototype route${form.evolution.children.length === 1 ? '' : 's'}; not a canonical evolution claim.` : form.evolution.reason || 'No outgoing route is currently assigned.'],
    ['Source stage', `${form.source?.stage || form.stage || 'Original'} in the source-game roster.`],
    ['Official reference', form.official?.name ? `${form.official.name} · ${form.official.stage || 'Stage unverified'}${form.official.sheetVariantReviewRequired ? ' · Sheet variant needs review' : ''}` : 'No verified official mapping is available for this exact source entry.'],
    ...rosterEncyclopediaMoves(form).map(move => ['Encyclopedia move', move.detail]),
    ['Authored balance', `${form.role} · ${typeName(form.type)} battle type. Numbers and combat roles are authored for this prototype.`],
    ['Artwork', form.art.status === 'private-local-available' ? 'Audited private local sheet available. Loads only for a visible form; never published.' : 'Artwork unavailable. No sprite-sheet download or completed animation is implied.'],
    ['Encounter rarity', `${rarityLabel(form.encounterRarity)} · Authored encounter frequency within an eligible stage pool. This is not a capture percentage.`],
  ];
}
function renderRosterTools() {
  if (!$('roster-results')) return;
  $('roster-results').replaceChildren(...(rosterPage?.entries || []).map(row => {
    const button = document.createElement('button'); button.className = 'button secondary'; button.type = 'button';
    button.dataset.rosterForm = String(row.formId); button.textContent = `${row.name} · ${row.stage || 'Original'} · #${row.formId}`; return button;
  }));
  $('roster-search-status').textContent = rosterError || (rosterPage ? `${rosterPage.total} matching source entries · ${rosterPage.offset + 1}–${Math.min(rosterPage.total, rosterPage.offset + rosterPage.entries.length)}` : 'Search the source roster without loading artwork.');
  const form = rosterDetail;
  $('roster-reference-title').textContent = form ? `${form.name} · references` : 'Select a form to see its references.';
  $('roster-references').replaceChildren(...rosterReferences(form).map(reference => {
    const anchor = document.createElement('a'); anchor.href = reference.url; anchor.target = '_blank'; anchor.rel = 'noopener noreferrer'; anchor.textContent = reference.label; return anchor;
  }));
}

let statsTarget = 'active';
let combatCatalog = null;
let catalogBusy = false;
let catalogError = '';

function combatProfile() {
  if (statsTarget === 'member') {
    const member = game?.collection.find(entry => entry.id === companionDetailId);
    return member ? { name: member.name, artId: member.artId, level: member.level, hp: member.hp, combat: member.combat, care: member.care, stage: member.stage } : null;
  }
  if (statsTarget === 'wild') return game?.wildCombat ? { name: game.wildName, hp: game.wildHp, combat: game.wildCombat } : null;
  const practice = battleClient.getState().state;
  if (statsTarget === 'practice-player') return practice ? { name: practice.playerFormName || practice.companion.name, level: practice.playerLevel, hp: practice.playerHp, combat: practice.playerCombat } : null;
  if (statsTarget === 'practice-enemy') return practice ? { name: practice.enemyFormName || practice.enemy.name, artId: practice.rulesVersion === 2 ? practice.enemySpecies : null, level: practice.enemyLevel, hp: practice.enemyHp, combat: practice.enemyCombat } : null;
  return game ? { name: game.creature, artId: game.artId, level: game.level, hp: game.hp, combat: game.combat, care: game.care, stage: game.stage } : null;
}
const typeName = value => ({ grove: 'Grove', tide: 'Tide', ember: 'Ember', neutral: 'Neutral' }[value] || value);
function rosterMember(member) {
  const artId = member.artId || member.name.toLowerCase();
  return { id: member.id, name: member.name, species: member.species,
    family: `${member.species[0].toUpperCase()}${member.species.slice(1)} family`,
    type: member.combat.type, typeLabel: typeName(member.combat.type), stage: member.level, stageLabel: member.stage || `Level ${member.level}`,
    hp: member.hp, maxHp: member.combat.maxHp, bond: member.bond,
    current: member.id === game.activeCreatureId,
    artId, personal: Boolean(member.stage), rookie: member.stage === 'Rookie', pendingForm: Boolean(member.stage), artAvailable: Boolean(member.stage ? findArt(artId) : findOriginalArt(artId)) };
}
function partnerLockReason(allowWildRelease = false) {
  if (!identity?.token || !game) return 'Set up your device to choose a partner.';
  if (game.phase === 'encounter' && !allowWildRelease) return 'Finish the wild encounter before changing partner.';
  const practice = battleClient.getState();
  if (practice.state?.status === 'active') return 'Finish or retreat from practice before changing partner.';
  if (!practice.loaded || practice.error || practice.ownerDeviceId !== identity.deviceId) return 'Check practice status before changing partner.';
  if (!online) return 'Reconnect to save a partner change.';
  if (!storageAvailable || !ownsWriter) return 'Restore your connection and browser save access first.';
  return '';
}
function canSetPartner(id) {
  return canAct() && !partnerLockReason() && game.collection.some(member => member.id === id && member.formId >= 11) && id !== game.activeCreatureId;
}
function releaseLockReason(member) {
  if (!member) return 'This companion is no longer carried.';
  if (member.id === game?.activeCreatureId) return 'Set another partner before releasing this companion.';
  if (game?.phase === 'encounter' && game.wildRules < 10) return 'Finish this earlier-rules encounter before releasing a companion.';
  return partnerLockReason(game?.phase === 'encounter' && game.wildRules >= 10);
}
function canRelease(member) { return Boolean(member && canAct() && !releaseLockReason(member)); }
function validCombat(value) {
  return validProgressCombat(value);
}
function progressionMember() { return game?.collection.find(member => member.id === progressionMemberId); }
function evolutionOption() { return progressionMemberId === game?.activeCreatureId ? game.evolution.options.find(option => option.formId === draftEvolutionId) : null; }
function evolutionLockReason() {
  if (!game || !identity) return 'Set up your device first.';
  if (progressionMemberId !== game.activeCreatureId) return 'Set this companion as partner to review its current choices.';
  if (game.phase !== 'home') return 'Finish the wild encounter before Digivolving.';
  const practice = battleClient.getState();
  if (practice.state?.status === 'active') return 'Finish or retreat from practice before Digivolving.';
  if (!practice.loaded || practice.error || practice.ownerDeviceId !== identity.deviceId) return 'Check practice status before Digivolving.';
  if (!online || !storageAvailable || !ownsWriter) return 'Restore the connection and local save access first.';
  return '';
}
function canEvolve() { return Boolean(canAct() && evolutionOption()?.eligible && !evolutionLockReason()); }
function showProgression(memberId = game?.activeCreatureId) {
  progressionMemberId = memberId; draftEvolutionId = null; evolutionDraft = null; navigation.go('progression');
}
async function loadEvolutionTree(focusFormId = graphFocusId || progressionMember()?.formId, offset = graphOffset) {
  if (!focusFormId) return;
  const request = ++graphRequest;
  graphFocusId = focusFormId; graphOffset = offset; evolutionGraph = null;
  evolutionTreeBusy = true; evolutionTreeError = ''; renderDevice();
  try { const result = await fetchEvolutionGraph(focusFormId, offset); if (request === graphRequest) evolutionGraph = result; }
  catch { if (request === graphRequest) evolutionTreeError = 'Evolution graph unavailable. Your current form is kept. Try again when connected.'; }
  finally { if (request === graphRequest) { evolutionTreeBusy = false; navigation.refresh(); renderDevice(); } }
}
async function showGraphNode(formId) {
  const cached = evolutionGraph?.forms.find(form => form.formId === formId);
  graphNode = cached || null; treeNodeId = formId; graphLinksOffset = 0; graphLinkNames = new Map();
  const request = ++graphRequest;
  evolutionTreeBusy = !cached; evolutionTreeError = '';
  if (navigation.state().screen === 'evolution-links') { navigation.back(); navigation.setScreen('evolution-node'); }
  else if (navigation.state().screen !== 'evolution-node') navigation.go('evolution-node');
  if (cached?.children.length) return;
  try {
    const form = await fetchRosterDetail(formId);
    if (request === graphRequest) graphNode = { formId, name: form.name, stage: form.stage, artId: formId <= 10 ? form.name.toLowerCase() : form.art.artId,
      minLevel: form.requiredLevel, minBond: form.requiredBond, previewLevel: form.previewLevel, combat: form.combat, ...form.evolution };
  } catch { if (request === graphRequest) evolutionTreeError = 'This connected form is unavailable. Go back or retry.'; }
  finally { if (request === graphRequest) { evolutionTreeBusy = false; navigation.refresh(); renderDevice(); } }
}
function graphLinks() {
  if (!graphNode) return [];
  const name = id => graphLinkNames.get(id) || evolutionGraph?.forms.find(form => form.formId === id)?.name || `Form #${id}`;
  return [...graphNode.parents.map(id => ({ id: `graph-parent-${id}`, label: name(id), detail: 'Prior form · inspect its outgoing route for requirements' })),
    ...graphNode.edges.map(edge => ({ id: `graph-child-${edge.toFormId}`, label: name(edge.toFormId), detail: `Next form · Level ${edge.requiredLevel} · Bond ${edge.requiredBond}` }))];
}
async function loadGraphLinkNames() {
  const nodeId = graphNode?.formId, offset = graphLinksOffset;
  const ids = graphLinks().slice(offset, offset + 8).map(link => Number(link.id.split('-')[2]));
  graphLinkNames = new Map();
  if (!ids.length) return;
  try {
    const page = await fetchRosterIds(ids);
    if (graphNode?.formId === nodeId && graphLinksOffset === offset) { graphLinkNames = new Map(page.entries.map(form => [form.formId, form.name])); renderDevice(); }
  } catch { /* Stable IDs remain usable when optional names cannot be loaded. */ }
}
function showStats(target) {
  statsTarget = target;
  navigation.go('stats');
}
async function loadCombatCatalog() {
  if (catalogBusy) return;
  catalogBusy = true; catalogError = ''; renderDevice();
  try {
    const result = await api('/api/combat/catalog');
    const types = ['grove', 'tide', 'ember', 'neutral'];
    if (result.rulesVersion !== RULES_VERSION || !Array.isArray(result.typeChart) || result.typeChart.length !== 4
      || new Set(result.typeChart.map(row => row.attacker)).size !== 4
      || result.typeChart.some(row => !types.includes(row.attacker) || ![null, ...types].includes(row.strongAgainst) || ![null, ...types].includes(row.weakAgainst))
      || !Array.isArray(result.profiles) || result.profiles.length > 512 || !result.profiles.every(profile => validCombat(profile.combat))) throw new Error('Unsupported combat catalog');
    combatCatalog = result;
  }
  catch { catalogError = 'The type chart is unavailable. Reconnect and try again.'; }
  finally { catalogBusy = false; renderDevice(); }
}

async function loadStarters() {
  if (startersBusy || !identity?.token || !isEggState(game)) return;
  // A saved pending action must be reconciled first, including older rules.
  // No offer-seed request is allowed to advance its expected revision.
  if (pending || !ownsWriter || !storageAvailable) { starters = []; return; }
  const deviceId = identity.deviceId;
  startersBusy = true; starters = []; startersError = ''; renderDevice();
  try {
    if (!game.onboarding.offerSeed) {
      const saved = await api('/api/starter-offers', { method: 'POST', body: {}, authenticated: true });
      if (identity?.deviceId !== deviceId || pending) return;
      if (saved.deviceId !== deviceId) throw new Error('Starter offer identity mismatch.');
      acceptSave(saved);
    }
    const fixed = validateStarterCatalog(await api('/api/starters'));
    const extras = await Promise.all(game.onboarding.offers.map(async (formId, index) => {
      const form = await fetchRosterDetail(formId);
      if (form.stage !== 'Rookie' || form.previewLevel !== 1) throw new Error('Unsupported starter offer.');
      return { id: 9 + index, species: form.lineage, formId, artId: form.art.artId, name: form.name, stage: 'Rookie', combat: form.combat, offered: true };
    }));
    if (identity?.deviceId === deviceId && !pending && isEggState(game)) {
      starters = [...fixed, ...extras]; startersOwner = deviceId;
    }
  } catch { startersError = 'The saved egg list is unavailable. Reconnect, then try again.'; }
  finally { startersBusy = false; renderDevice(); }
}
const chosenStarter = () => startersOwner === identity?.deviceId ? starters.find(starter => starter.id === draftStarterId) : undefined;
function canWriteGame() { return Boolean(identity?.token && game && !pending && !busy && !transitionBusy && !conflict && storageAvailable && ownsWriter && online && !document.hidden); }
function canHatch() { const practice = battleClient.getState(); return canWriteGame() && isEggState(game) && Boolean(chosenStarter()) && !startersBusy && game.onboarding.offerSeed > 0 && !practice.pending && !practice.recoveryRequired; }
function initialDeviceScreen() { return isEggState(game) ? 'starter-select' : 'home'; }

function autoBattleView(screen, view, item) {
  const kind = screen.startsWith('wild-') ? 'wild' : 'practice';
  const trace = kind === 'wild' ? wildAutoTrace : battleClient.getState().autoTrace;
  const progress = screen.endsWith('-progress');
  const index = progress && autoPlayback?.kind === kind ? autoPlayback.index : (trace?.steps.length || 1) - 1;
  const step = trace?.steps[index];
  Object.assign(view, { title: progress ? `${trace?.player.name || 'Partner'} battles…` : autoResultTitle(trace),
    eyebrow: progress ? `AUTO · TURN ${index + 1} / ${trace?.steps.length || 1}` : `${kind === 'wild' ? 'WILD' : 'PRACTICE'} · AUTO RESULT`,
    battleMode: 'auto', layout: progress ? 'auto-progress' : 'auto-result-carousel', focusActions: progress, scene: Boolean(trace), back: false,
    autoStep: step ? { turn: index + 1, total: trace.steps.length } : null,
    meter: step ? `YOU ${step.playerHpAfter} HP · RIVAL ${step.enemyHpAfter} HP` : '',
    detail: progress && step ? autoStepText(step, trace) : trace ? kind === 'wild'
      ? trace.outcome === 'captured' ? `${trace.enemy.name} joined your collection. The result is saved.` : trace.outcome === 'retreated' ? 'Your partner returned safely. Rest whenever you are ready.' : `${trace.steps.length} turns completed. Your progress is saved.`
      : `${trace.steps.length} exchanges completed. Care and collection are unchanged.` : 'No saved Auto battle is available.',
    items: progress ? [item('auto-pause', autoPlayback?.paused ? 'Resume replay' : 'Pause replay'), item('auto-finish', 'Show saved result')] : [item(kind === 'wild' ? 'wild-auto-done' : 'practice-auto-done', kind === 'wild' ? 'Return home' : 'Back to practice'),
      ...(trace ? [item(kind === 'wild' ? 'wild-auto-replay' : 'practice-auto-replay', 'Replay saved battle')] : [])],
    footer: progress ? autoPlayback?.paused ? 'REPLAY PAUSED · RESULT ALREADY SAVED' : 'SAVED RESULT · REPLAY ONLY' : 'TAP A CHOICE TO CONTINUE' });
  return view;
}

function autoVisualScene(trace, index = trace.steps.length - 1) {
  const step = trace.steps[index];
  return { creature: trace.player.name, formId: trace.player.formId, level: trace.player.level, hp: step.playerHpAfter, combat: trace.player.combat,
    energy: 100, phase: 'encounter', wildName: trace.enemy.name, wildArtId: trace.enemy.name.toLowerCase(),
    wildFormId: trace.enemy.formId, wildHp: step.enemyHpAfter, wildMaxHp: trace.enemy.combat.maxHp, wildCombat: trace.enemy.combat };
}

async function playAutoBattle(kind, { animate = true } = {}) {
  const trace = kind === 'wild' ? wildAutoTrace : battleClient.getState().autoTrace;
  if (!trace) return;
  presentation = null; sceneState = null;
  if (!animate || reducedMotion || document.hidden) { navigation.setScreen(kind === 'wild' ? 'wild-auto-result' : 'battle-auto-result'); render(); return; }
  autoPlayback = { kind, index: 0, trace, paused: false, finished: false };
  transitionBusy = true;
  navigation.setScreen(kind === 'wild' ? 'wild-auto-progress' : 'battle-auto-progress');
  try {
    for (let index = 0; index < trace.steps.length; index++) {
      if (autoPlayback.finished || document.hidden) break;
      autoPlayback.index = index;
      prepareCompanion(trace.steps[index].phase === 'attack' ? 'attack' : 'hurt', 'hurt');
      if (!document.hidden) audio.playCue(trace.steps[index].action === 'capture' ? (trace.steps[index].captured ? 'capture-success' : 'capture-fail') : trace.steps[index].action === 'magic' ? 'attack-magic' : trace.steps[index].phase === 'defend' ? 'hit' : 'attack-physical');
      render(); await waitStage(650);
      // Honour Pause even during the final turn; the saved result never changes.
      while (autoPlayback.paused && !autoPlayback.finished && !document.hidden) await waitStage(100);
    }
  } finally {
    autoPlayback = null; transitionBusy = false;
    navigation.setScreen(kind === 'wild' ? 'wild-auto-result' : 'battle-auto-result');
    prepareCompanion('celebrate'); render();
  }
}

function practiceView(screen, view, item) {
  const client = battleClient.getState();
  const battle = client.state;
  const ready = canAct() && client.loaded && !client.recoveryRequired;
  const active = battle?.status === 'active';
  const moveHelp = { physical: 'Fast physical', heavy: 'Reversal risk', magic: 'Ward resists', brace: 'Vs physical', counter: 'Vs heavy', ward: 'Vs magic' };
  const playerMove = token => battle?.playerCombat?.skills?.[token] || PRACTICE_LABELS[token];
  const enemyMove = token => battle?.enemyCombat?.skills?.[token] || PRACTICE_LABELS[token];
  const choice = token => item(`practice-${token}`, playerMove(token), !ready, moveHelp[token]);
  const hp = battle ? [['Your health', `${battle.playerHp}/${battle.playerCombat.maxHp}`], ['Rival health', `${battle.enemyHp}/${battle.enemyCombat.maxHp}`]] : [];
  const hint = battle?.enemyHint?.map(token => enemyMove(token) || token).join(' or ');
  view.eyebrow = 'PRACTICE BATTLE';
  switch (screen) {
    case 'battle-select-mode':
      Object.assign(view, { title: 'Choose your style', eyebrow: 'PRACTICE BATTLE', layout: 'carousel',
        items: [item('practice-tactical', 'Tactical', !ready, 'Choose attacks and counters', '⚔'), item('practice-auto', 'Auto', !ready, 'Start, then watch the battle', '▶')], footer: 'MODE STAYS FIXED FOR THIS BATTLE' }); break;
    case 'battle-auto-confirm':
      Object.assign(view, { title: 'Ready for Auto?', eyebrow: 'PRACTICE · AUTO', layout: 'auto-confirm',
        detail: 'Your partner chooses every move. No cards or timing inputs. Care and collection stay unchanged.',
        items: [item('practice-auto-start', 'Start Auto battle', !ready)], footer: 'BACK TO CHANGE · START TO COMMIT' }); break;
    case 'battle-mode':
      Object.assign(view, { title: 'Practice battle', detail: client.error ? client.error : !identity ? 'Pair your device before a practice battle.' : !client.loaded ? 'Loading your practice battle…' : active ? `${battle.companion.name} is ready to continue. Practice health stays separate from care.` : game?.phase === 'encounter' ? 'Finish your wild encounter first. Practice starts with full health and leaves care unchanged.' : 'Read a partial hint. Choose your attack or defence. Your rival has already committed.',
        items: [!identity ? item('connection', 'Set up device') : !client.loaded || client.error ? item('practice-refresh', 'Load battle', client.busy || !ownsWriter) : active ? item('practice-resume', 'Continue battle', !ready) : item('practice-start', 'Choose battle mode', !ready || game?.phase === 'encounter')], footer: 'TACTICAL OR AUTO · ONE PLAYER' });
      if (active) view.items.push(item('practice-retreat', 'Leave practice', !ready));
      if (client.mode === 'auto' && client.autoTrace) view.items.push(item('practice-auto-result', 'Last Auto result'));
      if (battle) view.items.push(item('practice-stats', 'Combat stats', !ready));
      break;
    case 'battle-choice':
      Object.assign(view, { title: battle?.phase === 'defend' ? 'Your defence' : 'Your attack', eyebrow: `Practice · ${(battle?.exchanges || 0) + 1} / ${battle?.maxExchanges || 30}`, layout: 'practice-carousel', stats: hp,
        detail: hint ? `Rival may choose ${hint}.` : 'The rival has committed its next move.',
        items: (battle?.phase === 'defend' ? ['brace', 'counter', 'ward'] : ['physical', 'heavy', 'magic']).map(choice),
        footer: battle?.lastEnemyMoves?.length ? `LAST: ${battle.lastEnemyMoves.map(token => enemyMove(token) || token).join(' / ')}` : 'TWO POSSIBILITIES · NOT A CERTAINTY' });
      view.items.push(item('battle-cards', battle?.cardUsed ? 'Practice card used' : 'Use a field card', !ready || battle?.cardUsed));
      view.items.push(item('practice-stats', 'Combat stats', !ready));
      break;
    case 'battle-cards':
      Object.assign(view, { title: 'One practice card', detail: 'Use one card per battle. Spark boosts your next attack; Shelter guards against damage.', items: [item('practice-card-spark', 'Spark', !ready || battle?.cardUsed, 'Boost attack'), item('practice-card-shelter', 'Shelter', !ready || battle?.cardUsed, 'Add guard')] }); break;
    case 'battle-resolve':
      Object.assign(view, { title: client.recoveryRequired ? 'Battle needs recovery' : client.busy ? 'Reading the moves…' : client.pending ? 'Battle action kept' : 'Battle needs attention', detail: client.error || 'Your move is being saved. Wait for the result, or retry if the connection is interrupted.',
        items: [item(client.canArchiveLegacy ? 'practice-legacy-review' : client.recoveryRequired ? 'practice-recovery' : client.pending ? 'practice-retry' : 'practice-refresh', client.canArchiveLegacy ? 'Review earlier command' : client.recoveryRequired ? 'Recovery information ↗' : client.pending ? 'Retry battle action' : 'Load current battle', client.busy || !ownsWriter || !storageAvailable)], backDisabled: client.busy || client.pending || client.recoveryRequired, footer: client.recoveryRequired ? 'EXISTING DATA KEPT' : client.pending ? 'SENT MOVES CANNOT BE UNDONE' : '' }); break;
    case 'battle-recovery':
      Object.assign(view, { title: 'Earlier battle command', detail: 'This saved request uses earlier battle rules. It will not be replayed. Keep it, or archive its exact contents to continue with the current battle.', items: [item('practice-legacy-confirm', 'Review archive', !client.canArchiveLegacy), item('practice-recovery-back', 'Keep request', client.busy)] }); break;
    case 'battle-recovery-confirm':
      Object.assign(view, { title: 'Archive this request?', detail: 'Keep a local copy and remove this old pending request. Your current battle is refreshed first and no old move is sent.', items: [item('practice-archive-legacy', 'Archive old command', !client.canArchiveLegacy), item('practice-legacy-review', 'Keep request', client.busy)] }); break;
    case 'battle-result': {
      const turn = battle?.lastTurn;
      const finished = battle?.phase === 'finished';
      const title = battle?.status === 'won' ? 'Practice victory!' : battle?.status === 'lost' ? 'A lesson learned.' : battle?.status === 'draw' ? 'An even match.' : battle?.status === 'retreated' ? 'Practice ended.' : practiceLastAction === 'card' ? 'Your card is ready.' : 'Moves revealed';
      const detail = battle?.status === 'retreated' ? 'Practice ended. Your companion is ready to head home.' : practiceLastAction === 'card' ? (battle?.attackBoost ? `Spark boost ${battle.attackBoost} ready.` : `Shelter guard ${battle?.shield || 0} ready.`) : turn ? `${playerMove(turn.playerChoice) || turn.playerChoice} / ${enemyMove(turn.enemyChoice) || turn.enemyChoice}. You −${turn.playerDamage} HP · Rival −${turn.enemyDamage} HP.${turn.reflected ? ' Reversed!' : ''}` : 'Your companion’s care and collection are unchanged.';
      Object.assign(view, { title, scene: true, detail, items: [item('practice-continue', finished ? 'Back to practice' : 'Next exchange', !ready)], footer: battle ? `${battle.playerHp} HP · RIVAL ${battle.enemyHp} HP` : '' }); break;
    }
  }
  if (['battle-choice', 'battle-cards', 'battle-result'].includes(screen)) view.battleMode = 'tactical';
  return view;
}

function visualScene() {
  if (needsTestEncounterRepair()) return null;
  if (autoPlayback) return autoVisualScene(autoPlayback.trace, autoPlayback.index);
  if (navigation.state().screen === 'wild-auto-result' && wildAutoTrace) return autoVisualScene(wildAutoTrace);
  const practiceAuto = battleClient.getState().autoTrace;
  if (navigation.state().screen === 'battle-auto-result' && practiceAuto) return autoVisualScene(practiceAuto);
  const battle = battleClient.getState().state;
  if (battle && ['battle-result', 'battle-resolve'].includes(navigation.state().screen)) {
    return { creature: battle.playerFormName || battle.companion.name, formId: battle.playerFormId, wildFormId: battle.enemyFormId, level: battle.playerLevel, hp: battle.playerHp, combat: battle.playerCombat, energy: 100, phase: 'encounter', wildName: battle.enemyFormName || battle.enemy?.name || 'Practice rival', wildArtId: battle.enemyFormName?.toLowerCase() || battle.enemySpecies || battle.enemy?.species || 'flicker', wildHp: battle.enemyHp, wildMaxHp: battle.enemyCombat.maxHp, wildCombat: battle.enemyCombat };
  }
  return sceneState || game;
}

function backgroundSceneId() {
  const screen = navigation.state().screen;
  const practice = battleClient.getState();
  if (screen.startsWith('battle-') || screen === 'practice-stats' || ['stats', 'skills'].includes(screen) && statsTarget.startsWith('practice-')) {
    // Revision minus command sequence identifies a duel without touching game RNG.
    const startRevision = (practice.revision || 1) - (practice.state?.sequence || 0);
    return BACKGROUND_SCENES[Math.max(0, startRevision - 1) % BACKGROUND_SCENES.length];
  }
  if ((['battle', 'capture', 'cards'].includes(screen) || screen.startsWith('wild-auto')) && (game?.phase === 'encounter' || wildAutoTrace)
    || ['stats', 'skills'].includes(screen) && statsTarget === 'wild'
    || transitionBusy && ['encounter', 'attack', 'retaliation', 'capture-result', 'card', 'win'].includes(presentation?.stage)) {
    return BACKGROUND_SCENES[Math.max(0, (game?.encounters || 1) - 1) % BACKGROUND_SCENES.length];
  }
  return ['home', 'care', 'explore', 'companions', 'companion', 'stats', 'skills', 'starter-select', 'starter-review', 'starter-hatched'].includes(screen) ? 'meadow' : 'digital';
}
function syncBackgroundStatus() {
  const state = backgroundPlayer.getState();
  const surface = $('screen-surface');
  surface.dataset.backgroundId = state.id || 'meadow'; surface.dataset.backgroundStatus = state.status;
  surface.dataset.backgroundVisible = state.visibleId || 'fallback';
  $('background-status').textContent = state.status === 'ready' ? `${state.visibleId[0].toUpperCase()}${state.visibleId.slice(1)} scenery · cosmetic only`
    : state.status === 'loading' ? 'Preparing scenery · built-in landscape available'
      : state.status === 'error' ? 'Scenery unavailable · built-in landscape shown. Retry in Artwork.'
        : 'Built-in landscape · scenery downloads in the Pack library';
}

async function loadPractice() {
  if (!identity?.token || !ownsWriter) return;
  await battleClient.load(identity);
  render();
}

async function practiceCommand(type, value = 0) {
  const client = battleClient.getState();
  const retry = type === 'retry';
  if (!identity?.token || !ownsWriter || !storageAvailable || pending || busy || transitionBusy || practiceRequestRunning || client.busy) return;
  if (!retry && (!canAct() || !client.loaded || client.recoveryRequired)) return;
  practiceRequestRunning = true;
  if (!retry) practiceLastAction = type;
  navigation.setScreen('battle-resolve');
  try {
    if (type === 'start') await battleClient.start(practiceDraftMode);
    else if (retry) await battleClient.retry();
    else await battleClient.act({ type, value });
    const result = battleClient.getState();
    if (result.pending || result.recoveryRequired || result.error) { render(); return; }
    if (result.mode === 'auto' && result.autoTrace) await playAutoBattle('practice', { animate: !retry });
    else if (type === 'start' || retry && !practiceLastAction) navigation.setScreen(result.state?.phase === 'finished' ? 'battle-result' : 'battle-choice');
    else {
      navigation.setScreen('battle-result');
      const turn = result.state?.lastTurn;
      prepareCompanion(turn?.phase === 'attack' ? 'attack' : turn?.playerDamage ? 'hurt' : 'idle', turn?.phase === 'defend' ? 'attack' : 'hurt');
      if (result.state?.status === 'won') audio.playCue('win');
      else if (type === 'retreat') audio.playCue('retreat');
      else if (type === 'card') audio.playCue('card');
      else {
        const attackChoice = turn?.phase === 'defend' ? turn.enemyChoice : turn?.playerChoice;
        audio.playCue(turn?.playerDamage ? 'hit' : attackChoice === 'magic' ? 'attack-magic' : 'attack-physical');
      }
    }
  } finally { practiceRequestRunning = false; render(); }
}


function progressionView(screen, view, item) {
  const member = progressionMember();
  const option = evolutionOption();
  const active = member?.id === game?.activeCreatureId;
  const tree = evolutionGraph;
  const lock = evolutionLockReason();
  const portrait = form => { const id = form.artId || (form.formId > 10 ? `ds-form-${form.formId}` : form.name.toLowerCase());
    return { artId: id, name: form.name, type: form.combat.type, personal: form.formId > 10, rookie: form.stage === 'Rookie', pendingForm: form.formId > 10, artAvailable: Boolean(findArt(id)) }; };
  Object.assign(view, { progression: member ? { memberId: member.id, formId: member.formId, xp: member.xp, level: member.level } : null });
  switch (screen) {
    case 'progression':
      Object.assign(view, { title: member?.name || 'Progression', eyebrow: member ? `${member.stage || 'ORIGINAL'} · LEVEL ${member.level} / ${game.maxLevel}` : 'YOUR COMPANION', layout: 'growth-carousel',
        portrait: member ? portrait(member) : null, detail: !member ? 'Choose a companion first.' : active ? 'Battle experience raises level. Digivolution is your choice.' : 'Set as partner to review current branch eligibility.',
        stats: member ? [['XP', member.xp, 'xp'], ['Next level', member.xpToNext || 'MAX', 'xpToNext'], ['Level', member.level, 'level'], ['Bond', member.bond, 'bond']] : [],
        items: member ? [item('evolution-options', active && game.evolution.options.length ? 'Digivolution choices' : active ? 'No further forms' : 'Set Partner to evolve', !active || !game.evolution.options.length), item('evolution-tree', 'Evolution graph'), ...(!active ? [item('progression-set-partner', 'Set Partner', !canSetPartner(member.id))] : [])] : [item('companions', 'Choose companion')], footer: member ? `COMPANION #${String(member.id).padStart(2, '0')} · XP IS SAVED` : '' }); break;
    case 'evolution-options':
      Object.assign(view, { title: 'Choose a path', eyebrow: member?.name || 'DIGIVOLUTION', layout: 'carousel',
        items: active ? game.evolution.options.map(form => ({ ...item(`evolve-${form.formId}`, form.name, false, `${form.stage || 'Form'} · Lv ${form.requiredLevel} · Bond ${form.requiredBond}`, '◇'), formId: form.formId, eligible: form.eligible })) : [],
        footer: 'PREVIEW FIRST · NOTHING CHANGES' }); break;
    case 'evolution-preview':
      Object.assign(view, { title: option?.name || 'Form preview', eyebrow: option ? `${option.stage || 'FORM'} · PREVIEW LEVEL ${option.previewLevel}` : 'DIGIVOLUTION', layout: 'evolution-stats-carousel',
        portrait: option ? portrait(option) : null, detail: option ? `Current → preview${option.artId || findArt(option.name.toLowerCase()) ? '' : ' · Artwork pending'}` : 'This choice is no longer available.',
        stats: option && member ? [['Max HP', 'maxHp'], ['Attack', 'attack'], ['Defence', 'defense'], ['Magic', 'magic'], ['Resistance', 'resistance']].map(([name, key]) => [name, `${member.combat[key]} → ${option.combat[key]}`, key]) : [],
        items: [item('evolution-requirements', 'Requirements', !option), item('evolution-skills', 'Compare moves', !option), item('evolution-review', 'Review Digivolution', !canEvolve())] }); break;
    case 'evolution-requirements':
      Object.assign(view, { title: 'Ready to Digivolve?', eyebrow: option?.name || 'REQUIREMENTS', layout: 'evolution-requirements',
        facts: option && member ? evolutionRequirements(member, option) : [], detail: lock || (option?.eligible ? 'All requirements met. Review your choice before saving.' : 'Raise the missing level or bond, then return here.'),
        items: [item('evolution-review', 'Review Digivolution', !canEvolve())] }); break;
    case 'evolution-skills':
      Object.assign(view, { title: 'Compare moves', eyebrow: option?.name || 'DIGIVOLUTION', layout: 'carousel',
        items: option && member ? ['physical', 'heavy', 'magic'].map(key => item(`evo-skill-${key}`, key[0].toUpperCase() + key.slice(1), false, `${member.combat.skills[key]} → ${option.combat.skills[key]}`, '✦')) : [], footer: 'CURRENT → PREVIEW' }); break;
    case 'evolution-confirm': {
      const reviewed = evolutionDraft?.deviceId === identity?.deviceId && evolutionDraft.revision === revision && evolutionDraft.memberId === member?.id && evolutionDraft.formId === option?.formId;
      Object.assign(view, { title: 'Digivolve this partner?', eyebrow: option?.stage || 'CONFIRM YOUR CHOICE', layout: 'auto-confirm',
        detail: option && member ? `${member.name} → ${option.name}. Same companion #${String(member.id).padStart(2, '0')}; this form choice is saved.` : 'Return to review current choices.',
        items: [item('confirm-evolution', 'Confirm Digivolution', !reviewed || !canEvolve())], footer: 'BACK KEEPS THE CURRENT FORM' }); break;
    }
    case 'evolution-result': {
      const confirmed = evolutionConfirmation?.deviceId === identity?.deviceId && evolutionConfirmation.memberId === member?.id && evolutionConfirmation.formId === member?.formId;
      Object.assign(view, { title: member?.name || 'Digivolution saved', eyebrow: member ? `${member.stage || 'FORM'} · LEVEL ${member.level}` : 'SAVED', scene: Boolean(member && active), layout: 'auto-result-carousel', back: false,
        detail: confirmed ? `Digivolution saved for companion #${String(member.id).padStart(2, '0')}. XP, bond and identity are kept.` : 'Earlier Digivolution confirmed. Your latest saved form is kept.',
        items: [item('evolution-done', 'View progression'), item('evolution-home', 'Return home')], footer: 'SAVED · NO SECOND EVOLUTION' }); break;
    }
    case 'evolution-tree':
      Object.assign(view, { title: 'Evolution graph', eyebrow: tree ? `${tree.total} CONNECTED FORMS · PAGE ${Math.floor(tree.offset / 8) + 1}` : 'REVIEWED PROTOTYPE ROUTES', layout: tree ? 'carousel' : 'auto-confirm',
        graph: { focusFormId: graphFocusId, offset: graphOffset, total: tree?.total || 0 },
        detail: evolutionTreeBusy ? 'Loading one graph page…' : evolutionTreeError,
        items: tree ? tree.forms.map(form => ({ ...item(`tree-${form.formId}`, form.name, false, `${form.stage || 'Original'} · ${form.parents.length} prior · ${form.children.length} next${form.formId === member?.formId ? ' · CURRENT' : ''}`, form.formId === member?.formId ? '✳' : '◇'), formId: form.formId })) : [item('reload-evolution-tree', 'Retry graph', evolutionTreeBusy)],
        footer: '8 PER PAGE · BROWSING CHANGES NOTHING' });
      if (tree?.nextOffset !== null && tree) view.items.push(item('graph-next', 'Next page ›'));
      if (tree?.offset > 0) view.items.push(item('graph-previous', '‹ Previous page'));
      break;
    case 'evolution-node': {
      const node = graphNode;
      Object.assign(view, { title: node?.name || 'Connected form', eyebrow: node ? `${node.stage || 'ORIGINAL'} · PREVIEW LEVEL ${node.previewLevel}` : 'EVOLUTION GRAPH', layout: node ? 'growth-carousel' : 'auto-confirm',
        graph: { focusFormId: graphFocusId, nodeId: treeNodeId, offset: graphOffset, total: tree?.total || 0 }, portrait: node ? portrait(node) : null,
        stats: node ? [['Max HP', 'maxHp'], ['Attack', 'attack'], ['Defence', 'defense'], ['Magic', 'magic'], ['Resistance', 'resistance']].map(([name, key]) => [name, node.combat[key], key]) : [],
        detail: node ? `${node.parents.length} prior · ${node.children.length} next forms. Routes show their own requirements.` : evolutionTreeBusy ? 'Reading connected form…' : evolutionTreeError,
        items: node ? [item('evolution-links', 'Connected routes'), item('evolution-node-skills', 'Combat moves'), item('evolution-tree', 'Back to graph')] : [item('graph-node-retry', 'Retry form', evolutionTreeBusy)] }); break;
    }
    case 'evolution-links': {
      const links = graphLinks();
      Object.assign(view, { title: graphNode?.name || 'Connected routes', eyebrow: 'PRIOR FORMS & NEXT CHOICES', layout: links.length ? 'carousel' : 'auto-confirm',
        detail: links.length ? '' : graphNode?.reason || 'No reviewed evolution route yet. This form remains available through its listed obtainability.',
        items: links.slice(graphLinksOffset, graphLinksOffset + 8).map(link => item(link.id, link.label, false, link.detail, link.id.includes('parent') ? '‹' : '›')),
        footer: 'AUTHORED ROUTES · NO FORM CHANGE' });
      if (graphLinksOffset + 8 < links.length) view.items.push(item('graph-links-next', 'More routes ›'));
      if (graphLinksOffset > 0) view.items.push(item('graph-links-previous', '‹ Earlier routes'));
      if (!links.length) view.items.push(item('evolution-tree', 'Back to graph'));
      break;
    }
    case 'evolution-node-skills':
      Object.assign(view, { title: graphNode?.name || 'Combat moves', eyebrow: 'NATIVE PREVIEW · AUTHORED MOVES', layout: 'carousel',
        items: graphNode ? ['physical', 'heavy', 'magic'].map(key => item(`graph-skill-${key}`, graphNode.combat.skills[key], false, key[0].toUpperCase() + key.slice(1), '✦')) : [], footer: 'REFERENCE · NO GAME CHANGE' }); break;

  }
  return view;
}

function catalogView(screen, view, item) {
  const form = rosterDetail;
  const stats = [['HP', 'maxHp'], ['Attack', 'attack'], ['Defence', 'defense'], ['Magic', 'magic'], ['Resistance', 'resistance']];
  const portrait = form ? { artId: form.formId <= 10 ? form.name.toLowerCase() : form.art.artId, name: form.name, type: form.type,
    personal: form.formId > 10, rookie: form.stage === 'Rookie', pendingForm: form.formId > 10,
    artAvailable: Boolean(findArt(form.formId <= 10 ? form.name.toLowerCase() : form.art.artId)) } : null;
  switch (screen) {
    case 'roster': {
      const entries = rosterPage?.entries || [];
      Object.assign(view, { title: 'World DS roster', eyebrow: rosterPage ? `${rosterPage.total} MATCHES · SOURCE ROSTER` : 'SOURCE ROSTER', layout: 'carousel',
        detail: rosterBusy ? 'Loading one page…' : rosterError || (!entries.length ? 'No matching forms. Change the filters.' : ''),
        items: rosterBusy ? [] : entries.map(row => ({ ...item(`roster-form-${row.formId}`, row.name, false, `${row.stage || 'Original'} · ${rarityLabel(row.encounterRarity)} · #${row.formId}`, '◇'), formId: row.formId })),
        footer: [rosterQuery.prefix ? `${rosterQuery.prefix.toUpperCase()}…` : '', rosterQuery.stage, rosterQuery.q].filter(Boolean).join(' · ') || '8 PER PAGE · ART LOADS SEPARATELY' });
      if (!rosterBusy) {
        if (rosterPage && rosterPage.offset + entries.length < rosterPage.total) view.items.push(item('roster-next', 'Next page ›'));
        if (rosterQuery.offset > 0) view.items.push(item('roster-previous', '‹ Previous page'));
        view.items.push(item('roster-filters', 'Search & filters'));
        if (rosterError) view.items.push(item('roster-retry', 'Retry catalog'));
      }
      break;
    }
    case 'roster-filters':
      Object.assign(view, { title: 'Find a form', layout: 'carousel', items: [item('roster-letters', 'First letter', false, 'A–Z · use the two buttons'), item('roster-stages', 'Stage', false, rosterQuery.stage || 'All source stages'), item('roster-clear', 'Clear filters'), item('roster-text-search', 'Type a name ↗', false, 'Opens browser search tools')] }); break;
    case 'roster-letters':
      Object.assign(view, { title: 'Starts with…', layout: 'carousel', items: [item('roster-letter-all', 'All letters'), ...'abcdefghijklmnopqrstuvwxyz'.split('').map(letter => item(`roster-letter-${letter}`, letter.toUpperCase(), false, `Names beginning with ${letter.toUpperCase()}`))] }); break;
    case 'roster-stages':
      Object.assign(view, { title: 'Source-game stage', layout: 'carousel', items: [item('roster-stage-all', 'All stages'), ...ROSTER_STAGES.map((stage, index) => item(`roster-stage-${index}`, stage))], footer: 'SOURCE LABELS · OFFICIAL LEVEL MAY DIFFER' }); break;
    case 'journal': {
      const obtained = (game?.journal.obtainedFormIds || []).filter(id => id >= 11);
      Object.assign(view, { title: 'Discovery journal', eyebrow: `${obtained.length} FORMS DISCOVERED`, layout: 'carousel',
        detail: rosterBusy ? 'Reading this journal page…' : rosterError || (!obtained.length ? 'Hatch your first partner to start your journal.' : ''),
        items: rosterBusy ? [] : (journalPage?.entries || []).map(row => ({ ...item(`roster-form-${row.formId}`, row.name, false, `${row.stage || 'Original'} · #${row.formId} · Discovered`, '✧'), formId: row.formId })),
        footer: 'RELEASE KEEPS DISCOVERED FORMS' });
      if (!rosterBusy) {
        if (journalOffset + ROSTER_PAGE_SIZE < obtained.length) view.items.push(item('journal-next', 'Next page ›'));
        if (journalOffset > 0) view.items.push(item('journal-previous', '‹ Previous page'));
        if (rosterError) view.items.push(item('journal-retry', 'Retry journal'));
        view.items.push(item('roster', 'Browse full roster'));
      }
      break;
    }
    case 'roster-detail':
      Object.assign(view, { title: form?.name || 'Form details', eyebrow: form ? `#${form.formId} · ${form.stage || 'Original'}` : 'CATALOG', layout: 'growth-carousel', portrait,
        detail: form ? `${game?.journal.obtainedFormIds.includes(form.formId) ? 'Discovered' : 'Not yet discovered'} · ${form.art.status === 'unavailable' ? 'Artwork unavailable' : 'Private local art available'}` : rosterBusy ? 'Reading this form…' : rosterError,
        stats: form ? [['Type', typeName(form.type)], ['Role', form.role], ['Preview Lv', form.previewLevel], ['Routes', form.evolution.children.length]] : [],
        items: form ? [item('roster-stats', 'Stats & growth'), item('roster-moves', 'Combat moves'), item('roster-notes', 'Field notes'), item('roster-references', 'References ↗', false, 'Source labels vs authored balance'), item('roster-graph', 'Evolution graph')] : rosterBusy ? [] : [item('roster-detail-retry', 'Retry details')], footer: 'REFERENCE · NO GAME CHANGE' }); break;
    case 'roster-stats': case 'roster-growth':
      Object.assign(view, { title: form?.name || 'Authored profile', eyebrow: screen === 'roster-stats' ? `AUTHORED · PREVIEW LEVEL ${form?.previewLevel || '—'}` : 'BASE · GROWTH PER LEVEL', layout: 'growth-carousel', portrait,
        stats: form ? stats.map(([label, key]) => [label, screen === 'roster-stats' ? form.combat[key] : `${form.baseStats[key]} · +${form.growth[key]}`, key]) : [],
        detail: screen === 'roster-stats' ? 'Native preview values. Battle balance is authored for this prototype.' : 'Declared base and growth. The native preview remains authoritative.',
        items: [item(screen === 'roster-stats' ? 'roster-growth' : 'roster-stats', screen === 'roster-stats' ? 'Base & growth' : 'Preview stats'), item('roster-moves', 'Combat moves')] }); break;
    case 'roster-moves':
      Object.assign(view, { title: form?.name || 'Combat moves', eyebrow: 'AUTHORED MOVE ASSIGNMENTS', layout: 'carousel',
        items: form ? [['physical', 'Physical · Brace reduces this'], ['heavy', 'Heavy · Counter reflects this'], ['magic', 'Magic · Ward reduces this']].map(([key, note]) => item(`roster-move-${key}`, form.combat.skills[key], false, note, '✦')) : [], footer: 'OFFICIAL MOVE LABELS ARE SEPARATE' }); break;
    case 'roster-notes':
      Object.assign(view, { title: form?.name || 'Field notes', eyebrow: 'SOURCE FACTS & PROTOTYPE DESIGN', layout: 'carousel', items: rosterNotes(form).map(([label, detail], index) => item(`roster-note-${index}`, label, false, detail)), footer: 'REFERENCE · NO CANONICAL ROUTE CLAIM' }); break;
  }
  if (form && screen.startsWith('roster-') && !['roster-filters', 'roster-letters', 'roster-stages'].includes(screen)) view.catalogFormId = form.formId;
  return view;
}

function deviceView(screen) {
  const encounter = game?.phase === 'encounter';
  const act = canAct();
  const sound = audio.getState();
  const item = (id, label, disabled = false, detail = '', icon = '') => ({ id, label, disabled, detail, icon });
  const view = { screen, title: 'Digivice', eyebrow: '', items: [], back: screen !== 'home', footer: '', scene: false };
  const navItem = (id, label, icon) => item(id, label, false, '', icon);
  if (needsTestEncounterRepair() && !['saving', 'recovery', 'recovery-confirm'].includes(screen)) return { ...view, title: 'Updating your encounter', detail: 'Keeping your companions and progress while removing an earlier test encounter.', back: false, scene: false, items: [item('reconnect', 'Retry update', busy || transitionBusy || Boolean(pending))], footer: 'SAVE KEPT · NO BATTLE COST' };
  switch (screen) {
    case 'starter-select': {
      const choices = startersOwner === identity?.deviceId ? starters : [];
      Object.assign(view, { title: 'Choose your egg', eyebrow: 'YOUR FIRST PARTNER', layout: 'egg-carousel', back: false,
        detail: choices.length ? '' : startersBusy ? 'Preparing your saved eggs…' : startersError || 'Load the eggs to begin.',
        items: choices.length ? choices.map(starter => ({ ...item(`starter-${starter.id}`, starter.name, false, `Hatches into ${starter.name} · ${starter.stage}`), egg: { ...starter, artAvailable: Boolean(starter.artId && findArt(starter.artId)), type: starter.combat.type, rookie: true, personal: true } })) : [item('reload-starters', startersBusy ? 'Loading…' : 'Try again', startersBusy)], footer: 'L NEXT · R CHOOSE' }); break;
    }
    case 'starter-review': {
      const starter = chosenStarter();
      Object.assign(view, { title: starter ? `Hatch ${starter.name}?` : 'Choose an egg', eyebrow: 'YOUR CHOICE', layout: 'egg-review', egg: starter ? { ...starter, artAvailable: Boolean(starter.artId && findArt(starter.artId)), type: starter.combat.type, rookie: true, personal: true } : null,
        detail: starter ? `${starter.name} · ${starter.stage}\nYour first partner. This choice is saved.` : 'Return to choose your egg.',
        items: [item('hatch-starter', starter ? `Hatch ${starter.name}` : 'Hatch', !canHatch())], footer: 'HOLD L TO CHANGE' }); break;
    }
    case 'starter-hatched':
      Object.assign(view, { title: `Hello, ${game?.creature || 'partner'}!`, eyebrow: 'ROOKIE · YOUR PARTNER', layout: 'hatch-result', scene: true, back: false,
        detail: game ? `${game.creature} is ready for your first adventure.` : '', items: [item('meet-starter', 'Meet your partner', busy || transitionBusy)], footer: 'YOUR CHOICE IS SAVED' }); break;
    case 'home':
      if (identity && !game) {
        Object.assign(view, { title: 'Save not loaded', eyebrow: 'PAIRED DEVICE', layout: 'auto-confirm',
          detail: busy ? 'Loading your saved companion…' : 'Your paired identity is kept. Reconnect to load your companion and progress.',
          items: [item('connection', 'Restore saved game')], footer: 'NO NEW GAME CREATED' }); break;
      }
      Object.assign(view, { title: activeDisplayName(), eyebrow: game ? `${game.stage ? `${game.stage} · ` : ''}Level ${game.level} · Bond ${game.bond}` : 'Your first companion', scene: true, meter: game?.combat ? `${game.hp} / ${game.combat.maxHp} HP` : '',
        footer: pending ? 'SAVE WAITING' : game ? `${game.steps.toLocaleString()} STEPS · ${game.bond} BOND` : 'LOCAL VIRTUAL DEVICE',
        items: encounter ? [item(game.battleMode === 'auto' ? 'wild-auto-confirm' : 'battle', 'Encounter'), item('menu', 'Menu')] : [item(game ? 'menu' : 'connection', game ? 'Open menu' : 'Set up device'), ...(wildAutoTrace ? [item('wild-auto-result', 'Last Auto result')] : [])], focusActions: !encounter && Boolean(wildAutoTrace) }); break;
    case 'menu':
      Object.assign(view, { title: 'Your little world', eyebrow: '', layout: 'carousel', items: [navItem('care', 'Care', '◒'), navItem('companions', 'Companions', '✳'), navItem('explore', 'Explore', '↗'), navItem('cards', 'Cards', '▱'), navItem('settings', 'Settings', '⚙'), navItem('battle-mode', 'Battle', '⚔'), navItem('stats', 'Stats', '▥'), navItem('progression', 'Progression', '◇'), navItem('type-chart', 'Type chart', '↻'), navItem('roster', 'World DS roster', '▤'), navItem('journal', 'Discovery journal', '✧')] }); break;
    case 'roster': case 'roster-filters': case 'roster-letters': case 'roster-stages': case 'roster-detail': case 'roster-stats': case 'roster-growth': case 'roster-moves': case 'roster-notes': case 'journal':
      catalogView(screen, view, item); break;
    case 'progression': case 'evolution-options': case 'evolution-preview': case 'evolution-skills': case 'evolution-requirements':
    case 'evolution-confirm': case 'evolution-result': case 'evolution-tree': case 'evolution-node': case 'evolution-links': case 'evolution-node-skills':
      progressionView(screen, view, item); break;
    case 'type-chart':
      Object.assign(view, { title: 'Type matchups', eyebrow: 'KNOW YOUR ADVANTAGE',
        detail: combatCatalog ? '' : catalogBusy ? 'Loading the type chart…' : catalogError || 'Load the current type chart.',
        facts: combatCatalog?.typeChart.map(row => [typeName(row.attacker), row.strongAgainst ? `Strong: ${typeName(row.strongAgainst)} · Weak: ${typeName(row.weakAgainst)}` : 'No advantage or weakness']) || [],
        items: combatCatalog ? [] : [item('reload-catalog', 'Load chart', catalogBusy)] }); break;
    case 'stats': {
      const profile = combatProfile();
      const combat = profile?.combat;
      Object.assign(view, { title: profile?.name ? `${profile.name}’s stats` : 'Combat stats', eyebrow: profile?.level ? `Level ${profile.level} · ${typeName(combat?.type)}` : 'Combat stats',
        layout: 'stats-carousel', portrait: combat ? { artId: profile.artId || profile.name.toLowerCase(), name: profile.name, type: combat.type, artAvailable: Boolean(findArt(profile.artId || profile.name.toLowerCase())), personal: true, pendingForm: true, rookie: profile.stage === 'Rookie' } : null,
        creatureType: combat?.type, detail: combat ? `${typeName(combat.type)} · ${profile.hp} / ${combat.maxHp} HP · ${careSummary(profile) || 'Base stats'}` : 'Pair a device to see its combat stats.',
        stats: combat ? [['Max HP', combat.maxHp, 'maxHp'], ['Attack', combat.attack, 'attack'], ['Defence', combat.defense, 'defense'], ['Magic', combat.magic, 'magic'], ['Resistance', combat.resistance, 'resistance']] : [],
        items: [item('skills', 'Moves', !combat), item('type-chart', 'Type chart')] }); break;
    }
    case 'skills': {
      const profile = combatProfile();
      const combat = profile?.combat;
      Object.assign(view, { title: profile?.name || 'Moves', eyebrow: 'COMBAT MOVES', creatureType: combat?.type,
        facts: combat ? [['Physical', combat.skills.physical], ['Heavy', combat.skills.heavy], ['Magic', combat.skills.magic]] : [],
        items: [item('type-chart', 'Type chart')] }); break;
    }
    case 'practice-stats': {
      const battle = battleClient.getState().state;
      Object.assign(view, { title: 'Know your rival', eyebrow: 'PRACTICE BATTLE', items: [item('practice-player-stats', battle?.companion.name || 'Your companion', !battle), item('practice-enemy-stats', battle?.enemy.name || 'Practice rival', !battle), item('type-chart', 'Type chart')] }); break;
    }
    case 'care':
      Object.assign(view, { title: `Care for ${game?.creature || 'Partner'}`, stats: [['Health', game?.hp ?? '—'], ['Energy', game?.energy ?? '—'], ['Fullness', game?.fullness ?? '—'], ['Mood', game?.mood ?? '—']],
        layout: 'care-carousel', portrait: { artId: playerId(), name: activeDisplayName(), type: game?.combat?.type || 'grove', artAvailable: Boolean(findArt(playerId())), personal: true, rookie: game?.stage === 'Rookie' },
        detail: encounter ? 'Finish this encounter before care.' : game ? `${game.bond} bond · ${careSummary(game.collection.find(member => member.id === game.activeCreatureId))}` : 'Set up your device first.',
        items: [item('recover-review', 'Recover fully', !act || !recoveryReview(game, identity?.deviceId, revision), '', '☾'), item('feed', 'Feed', !act || encounter, '', '◒'), item('play', 'Play', !act || encounter, '', '✧'), item('rest', 'Rest once', !act || encounter, '', '☾')] }); break;
    case 'recover-confirm': {
      const reviewed = reviewedRecoveryEvents(recoveryDraft, game, identity?.deviceId, revision);
      Object.assign(view, { title: 'Recover fully?', eyebrow: game?.creature || 'YOUR PARTNER', layout: 'auto-confirm',
        detail: reviewed ? `${reviewed.length} ordinary rest${reviewed.length === 1 ? '' : 's'} fill health and energy. Saved together; XP stays the same.` : 'Your save changed. Return to Care to review recovery again.',
        items: [item('confirm-recovery', 'Confirm recovery', !act || !reviewed)], footer: 'BACK KEEPS YOUR CURRENT STATE' }); break;
    }
    case 'companions': {
      const members = game?.collection || [];
      const pages = Math.max(1, Math.ceil(members.length / COMPANIONS_PER_PAGE));
      companionPage = Math.min(companionPage, pages - 1);
      Object.assign(view, { title: 'Companions', eyebrow: `${members.length} / 8 COMPANIONS${members.length === 8 ? ' · FULL' : ''}`,
        artGeneration, layout: 'roster', detail: members.length ? '' : 'Set up your device to meet your first partner.',
        items: members.slice(companionPage * COMPANIONS_PER_PAGE, (companionPage + 1) * COMPANIONS_PER_PAGE)
          .map(member => ({ ...item(`member-${member.id}`, member.name), member: rosterMember(member) })),
        footer: members.length ? `PAGE ${companionPage + 1} / ${pages}` : '' });
      if (pages > 1) view.items.push(item('companions-next', companionPage === pages - 1 ? 'First page ↻' : 'Next page ›', false, `${companionPage + 1} / ${pages}`));
      if (!members.length) view.items.push(item('connection', 'Set up device'));
      break;
    }
    case 'companion': {
      const member = game?.collection.find(entry => entry.id === companionDetailId);
      const current = member?.id === game?.activeCreatureId;
      const lockReason = member?.formId < 11 ? 'Earlier test companion retained in your save. Choose a named partner.' : partnerLockReason();
      const confirmation = partnerConfirmation?.deviceId === identity?.deviceId && partnerConfirmation.memberId === member?.id ? partnerConfirmation : null;
      const artMissing = member && !findOriginalArt(member.name.toLowerCase());
      Object.assign(view, { title: member?.name || 'Companion', eyebrow: member ? `#${String(member.id).padStart(2, '0')} · ${member.capturedAtSequence === 0 ? 'STARTER' : 'CAPTURED'} COMPANION` : 'YOUR COLLECTION',
        artGeneration, layout: 'member', creatureType: member?.combat.type, member: member ? rosterMember(member) : null,
        detail: !member ? 'This companion is not in the current save.' : encounter && canRelease(member) ? 'Release this non-partner to make room. Your encounter will wait.' : lockReason || (confirmation && current ? `Partner set · #${String(member.id).padStart(2, '0')} ${member.name}. Saved.`
          : confirmation && !current ? 'Earlier change confirmed. Your latest partner is kept.' : current ? 'Your partner for care, walks and battles.' : 'Choose this companion for your next adventure.'),
        notice: lockReason && member ? 'locked' : confirmation && current ? 'success' : '',
        stats: member ? [['Energy', member.energy], ['Fullness', member.fullness], ['Mood', member.mood], ['Bond', member.bond]] : [],
        items: member ? [item('select-companion', current ? 'Current partner' : 'Set Partner', !canSetPartner(member.id), current ? '✓' : '', '✳'), item('member-stats', 'Stats & moves'), item('member-progression', 'Progression'), item('release-review', 'Release companion', !canRelease(member))]
          : [item(game ? 'companions' : 'connection', game ? 'Back to collection' : 'Set up device')] });
      if (member && lockReason.startsWith('Check practice')) view.items.push(item('partner-refresh', 'Check practice status'));
      else if (artMissing) view.items.push(item('open-assets', 'Pack library ↗', false, 'Art not saved'));
      // Recovery adds another action; retain a full-size profile and cycle one
      // focused action with the same two buttons instead of shrinking the text.
      view.focusActions = view.items.length > 2;
      break;
    }
    case 'release-review': {
      const member = game?.collection.find(entry => entry.id === companionDetailId);
      Object.assign(view, { title: 'Let this companion go?', eyebrow: member ? `#${member.id} · ${member.name}` : 'RELEASE COMPANION', layout: 'auto-confirm',
        detail: releaseLockReason(member) || (encounter ? 'Free one carried slot. This individual’s care and XP are removed; its journal entry and your waiting encounter stay.' : 'This individual leaves your carried team. Its care and XP record is removed. Your journal keeps the form you discovered.'),
        items: [item('release-confirmation', 'Review release', !canRelease(member))], footer: 'BACK KEEPS YOUR COMPANION' }); break;
    }
    case 'release-confirm': {
      const member = game?.collection.find(entry => entry.id === releaseDraft?.memberId);
      const reviewed = releaseDraft?.deviceId === identity?.deviceId && releaseDraft?.revision === revision;
      Object.assign(view, { title: `Release ${member?.name || 'companion'}?`, eyebrow: member ? `COMPANION #${member.id}` : 'CONFIRM RELEASE', layout: 'auto-confirm',
        detail: 'This cannot be undone. A future capture is a new individual. Your active partner and journal stay with you.',
        items: [item('confirm-release', 'Release companion', !reviewed || !canRelease(member))], footer: 'HOLD BACK TO KEEP THEM' }); break;
    }
    case 'release-result': {
      const result = releaseConfirmation?.deviceId === identity?.deviceId ? releaseConfirmation : null;
      Object.assign(view, { title: 'Release saved', eyebrow: result ? `COMPANION #${result.memberId}` : 'CARRIED TEAM UPDATED', layout: 'auto-confirm', back: false,
        detail: `${result?.name || 'Your companion'} has left your carried team. The discovered form remains in your journal.`,
        items: [item('release-done', encounter ? 'Return to encounter' : 'View companions')], footer: 'YOUR PARTNER IS KEPT' }); break;
    }
    case 'explore':
      Object.assign(view, { title: 'One little walk', eyebrow: `${(game?.steps || 0).toLocaleString()} STEPS · ${game?.battleMode === 'auto' ? 'AUTO' : 'TACTICAL'}`, detail: encounter ? `${game.wildName} is waiting.${game.queuedEncounters ? ` ${game.queuedEncounters} more encounter${game.queuedEncounters === 1 ? '' : 's'} queued.` : ''} Finish this encounter first.` : game?.queuedEncounters ? `${game.queuedEncounters} encounter${game.queuedEncounters === 1 ? '' : 's'} queued. The next real step starts one; this button simulates 100 steps.` : `${game?.stepsToNextEncounter ?? 100} more steps to an encounter. This button simulates 100 steps; choose your style first.`,
        walkProgress: game ? { queued: game.queuedEncounters, remaining: game.stepsToNextEncounter } : null,
        items: [item(encounter ? game.battleMode === 'auto' ? 'wild-auto-confirm' : 'battle' : 'walk', encounter ? 'Return to encounter' : 'Explore +100', !encounter && !act, '', '↗'), item('wild-mode', 'Choose battle mode', !act || encounter)] }); break;
    case 'wild-mode':
      Object.assign(view, { title: 'Choose your style', eyebrow: 'WILD ENCOUNTERS', layout: 'carousel',
        items: [item('wild-tactical', 'Tactical', !act || encounter, 'Choose attacks, cards and captures', '⚔'), item('wild-auto', 'Auto', !act || encounter, 'Confirm a battle, then watch', '▶')], footer: 'CHOOSE BEFORE WALKING' }); break;
    case 'wild-auto-confirm':
      Object.assign(view, { title: 'Ready for Auto?', eyebrow: `WILD ${game?.wildName || 'ENCOUNTER'} · AUTO`, layout: 'auto-confirm', encounterRarity: game?.wildRarity,
        detail: 'Your partner fights and tries to capture. No cards or timing inputs. Start saves the whole encounter.',
        items: [item('wild-auto-start', 'Start Auto battle', !act || !encounter || game.battleMode !== 'auto', rarityLabel(game?.wildRarity)), ...(encounter && game.collection.length >= game.collectionCapacity && game.wildRules >= 10 ? [item('make-room', 'Make room', !act, 'Release a non-partner first')] : [])], footer: 'BACK TO WAIT · START TO COMMIT', focusActions: true }); break;
    case 'wild-auto-progress':
    case 'wild-auto-result':
    case 'battle-auto-progress':
    case 'battle-auto-result':
      autoBattleView(screen, view, item); break;
    case 'battle':
      Object.assign(view, { title: encounter ? `Wild ${game.wildName}` : 'Back from the field', eyebrow: encounter ? `${game.wildHp} / ${game.wildMaxHp} HP · ${rarityLabel(game.wildRarity)}` : '', scene: true, layout: 'wild-carousel', encounterRarity: game?.wildRarity,
        battleMode: 'tactical',
        detail: transitionBusy ? presentation?.title : `${game?.creature || 'Partner'} ${game?.hp || 0} HP · ${game?.wildGuard ? `Rival: ${game.wildGuard === 'brace' ? 'Brace' : game.wildGuard === 'ward' ? 'Ward' : 'Counter'}` : `Energy ${game?.energy || 0}`}`,
        items: [item('attack', game?.combat.skills.physical || 'Attack', !act || !encounter, game?.wildGuard === 'brace' ? 'Physical · Brace halves damage' : 'Physical · 2 energy'), item('heavy', game?.combat.skills.heavy || 'Heavy', !act || !encounter || game.energy < 6, game?.wildGuard === 'counter' ? 'Heavy · Counter reflects this hit' : 'Heavy · 6 energy · Watch Counter'), item('magic', game?.combat.skills.magic || 'Magic', !act || !encounter, game?.wildGuard === 'ward' ? 'Magic · Ward halves damage' : 'Magic · 2 energy'), item('capture', captureChoice(game).label, !act || !captureChoice(game).available, captureChoice(game).detail), item('cards', 'Cards', !act || !encounter || game.cardUsed, 'One card per encounter'), item('wild-stats', 'Stats', !game?.wildCombat, 'Meet this wild companion')],
        footer: encounter ? game.collection.length >= game.collectionCapacity ? game.wildRules >= 10 ? 'Collection full · Make room to capture' : 'Collection full · Finish battle to release' : game.cardUsed ? (game.attackBoost ? `Spark +${game.attackBoost} ready` : game.shield ? `Shelter ${game.shield} ready` : 'Field card used') : 'L Next · R Use' : 'RETURN HOME' });
      if (encounter && game.collection.length >= game.collectionCapacity && game.wildRules >= 10) view.items.splice(4, 0, item('make-room', 'Make room', !act, 'Release a non-partner · Encounter waits'));
      break;
    case 'capture':
      Object.assign(view, { title: 'Ready the beam…', eyebrow: `${Math.max(0, 3 - (game?.captureAttempts || 0))} THROWS LEFT`, detail: 'No request sent yet. Back cancels without using an attempt. The saved throw chance appears after the throw.', items: [item('cancel-capture', 'Cancel capture')], backLabel: '‹ Cancel' }); break;
    case 'capture-aim':
      Object.assign(view, { title: 'Flick to capture', eyebrow: '', scene: true, layout: 'capture-aim', detail: '', items: [], backLabel: 'Cancel', footer: '' }); break;
    case 'cards':
      Object.assign(view, { title: 'Field cards', eyebrow: 'SIMULATED NFC', detail: game?.battleMode === 'auto' ? 'Auto battles choose every move. Cards are available in Tactical mode.' : encounter ? game.cardUsed ? 'One card already used this encounter.' : 'Choose one card for this encounter.' : 'Cards can be used during a wild encounter.',
        items: [item('card-spark', 'Spark', !act || !encounter || game?.cardUsed || game?.battleMode !== 'tactical', '+5 next hit', '✦'), item('card-shelter', 'Shelter', !act || !encounter || game?.cardUsed || game?.battleMode !== 'tactical', 'Guard next hit', '◇')] }); break;
    case 'settings':
      Object.assign(view, { title: 'Settings', items: [navItem('sound', 'Sound', '♫'), navItem('artwork', 'Artwork', '✳'), navItem('connection', 'Connection', '⌁'), navItem('saves', 'Saved playtests', '▣')] }); break;
    case 'sound':
      Object.assign(view, { title: 'Sound', detail: !sound.supported ? 'This browser does not support sound.' : !sound.unlocked ? 'Sound starts only when you choose it.' : 'Original music and effects. Pauses while away.',
        items: [item('toggle-sound', !sound.unlocked ? 'Enable sound' : sound.muted ? 'Sound: off' : 'Sound: on', !sound.supported), item('toggle-music', `Music: ${sound.musicEnabled ? 'on' : 'off'}`, !sound.supported), item('volume-down', 'Quieter', !sound.supported || sound.volume <= 0, `${Math.round(sound.volume * 100)}%`), item('volume-up', 'Louder', !sound.supported || sound.volume >= 1)] }); break;
    case 'artwork': {
      const available = Object.values(originalPacks).filter(candidate => candidate.formatVersion === 1);
      const names = available.map(candidate => ({ 'starter-v2': 'Forest', 'tide-v1': 'Tide', 'ember-v1': 'Ember' }[candidate.packId] || candidate.packId));
      Object.assign(view, { title: 'Artwork', detail: `${names.length ? `Installed: ${names.join(' · ')}.` : 'Named partner placeholders are ready.'} ${personalPack ? 'Personal appearances are on.' : 'Local form artwork is on.'}`,
        items: [item('toggle-roster-art', `Local form art: ${formArt.state().enabled ? 'on' : 'off'}`, false, 'Audited private sheets · loaded per form'), item('toggle-personal', personalPack ? 'Use local form art' : 'Use personal art', $('personal-use').disabled), item('open-assets', 'Pack library'), item('open-personal', 'Import personal art ↗')], footer: 'FILE IMPORT USES BROWSER TOOLS', focusActions: true });
      if (backgroundPlayer.getState().status === 'error') view.items.push(item('retry-background', 'Retry scenery'));
      break;
    }
    case 'asset-packs':
      Object.assign(view, { title: 'Pack library', eyebrow: '', layout: 'carousel',
        detail: assetState.packs.length ? '' : assetState.message,
        items: assetState.packs.map(entry => item(`pack-${entry.id}`, entry.name, !assetLibrary, entry.installed ? 'Saved on this device' : entry.kind === 'background' ? 'Cosmetic scenery · 480 × 480' : 'Original companion artwork', entry.kind === 'background' ? '▧' : '✳')) }); break;
    case 'asset-progress': {
      const id = assetState.busy ? assetState.id : assetState.selectedId;
      const entry = assetState.packs.find(pack => pack.id === id);
      const currentTransfer = assetState.id === id;
      const phase = currentTransfer ? assetState.phase : entry?.installed ? 'ready' : 'idle';
      Object.assign(view, { title: assetState.busy ? 'Getting artwork' : phase === 'paused' ? 'Download paused' : phase === 'error' ? 'Couldn’t get art' : entry?.installed ? 'Artwork is ready' : 'Bring it along',
        eyebrow: entry?.name || 'Pack library', detail: assetState.busy || ['paused', 'error'].includes(phase) ? assetState.message : entry?.installed ? 'Saved locally. Your game progress is kept.' : 'Download verified artwork to keep on this device.',
        progress: currentTransfer && assetState.total ? { value: assetState.received, total: assetState.total } : null,
        items: assetState.busy ? [item('asset-pause', 'Pause download')] : [item('asset-install', phase === 'paused' ? 'Resume download' : phase === 'error' ? 'Retry download' : entry?.installed ? 'Check for update' : 'Download pack', !assetLibrary || !assetState.ready), item('asset-packs', 'Choose a pack')] }); break;
    }
    case 'connection':
      Object.assign(view, { title: !online ? 'Service unavailable' : connectionError ? 'Connection needs attention' : 'Service connected', eyebrow: 'LOCAL CONNECTION', detail: !storageAvailable ? 'Browser storage needs recovery. Existing data is kept.' : !ownsWriter ? 'Another tab controls saves. Close it, then reconnect.' : connectionError ? connectionError : identity ? `Virtual device paired · save ${revision}. Uses your local service; no native phone app.` : pairingCode ? `Pairing code: ${pairingCode}. Claim this virtual device to begin.` : 'Create a local pairing code, then claim this virtual device.',
        items: [identity ? item('reconnect', 'Refresh connection', busy || transitionBusy) : pairingCode ? item('claim-device', 'Claim device', $('claim-device').disabled) : item('start-pairing', 'Create pairing code', $('start-pairing').disabled), ...(!online && !identity ? [item('reconnect', 'Reconnect', busy)] : []), ...(!identity && pairingCode ? [item('new-pairing-code', 'New pairing code', busy || !online || !storageAvailable || !ownsWriter)] : [])], footer: identity ? 'BROWSER IDENTITY · LOCAL ONLY' : '' }); break;
    case 'saves': {
      savesPage = Math.min(savesPage, Math.max(0, Math.ceil(savedPlaytests.length / 3) - 1));
      Object.assign(view, { title: 'Saved playtests', eyebrow: `${savedPlaytests.length} / 8 LOCAL SLOTS`, detail: playtestsCorrupt ? 'Saved list unreadable. Existing data is kept.' : 'Every action saves. Loading keeps the other game.',
        items: savedPlaytests.slice(savesPage * 3, savesPage * 3 + 3).map(saved => item(`load-${saved.deviceId}`, saved.label, !act || playtestsCorrupt || saved.deviceId === identity?.deviceId, saved.deviceId === identity?.deviceId ? 'CURRENT' : 'LOAD')) });
      if (savedPlaytests.length > 3) view.items.push(item('saves-next', 'More saves ›'));
      view.items.push(item('save-confirm', 'New playtest', !act || playtestsCorrupt || savedPlaytests.length >= 8)); break;
    }
    case 'save-confirm':
      Object.assign(view, { title: 'A fresh adventure?', detail: 'Your current game will be kept. Return to Saved playtests any time. Up to eight saves are supported.', items: [item('new-playtest', 'Keep current & start', !act || savedPlaytests.length >= 8), item('back', 'Stay here')] }); break;
    case 'recovery':
      Object.assign(view, { title: 'Review saved action', eyebrow: `LATEST SAVE · ${revision}`, detail: 'The latest save is loaded. This request was rejected or uses earlier rules. Removing the local request keeps that saved game.', items: [item('recovery-confirm', 'Review removal', busy), item('recovery-retry', 'Check again', busy)], backLabel: '‹ Save status' }); break;
    case 'recovery-confirm':
      Object.assign(view, { title: 'Keep the latest save?', detail: 'Remove only this rejected local request? Your saved companions and progress stay. A fresh save is checked before removal.', items: [item('discard-local', 'Keep save · remove request', busy), item('recovery-back', 'Keep request', busy)] }); break;
    case 'saving':
      Object.assign(view, { title: conflict ? 'Save needs review' : 'Confirming your save', eyebrow: 'ACTION SAFELY KEPT', detail: busy ? 'Waiting for the service. Your command is stored for safe retry.' : $('notice-text').textContent || 'A response is still needed. Retry the same saved action.',
        items: [item(conflict ? 'open-recovery' : 'retry', conflict ? 'Review saved action' : 'Retry saved action', busy || !ownsWriter || !storageAvailable)], backDisabled: true, footer: 'BACK CANNOT UNDO A SENT ACTION' }); break;
    case 'battle-mode':
    case 'battle-select-mode':
    case 'battle-auto-confirm':
    case 'battle-choice':
    case 'battle-resolve':
    case 'battle-result':
    case 'battle-recovery':
    case 'battle-recovery-confirm':
    case 'battle-cards':
      practiceView(screen, view, item); break;
    default: view.items = [item('menu', 'Open menu')];
  }
  if ((busy || transitionBusy) && !autoPlayback && screen !== 'capture' && screen !== 'saving') {
    view.items = view.items.map(entry => ({ ...entry, disabled: true })); view.backDisabled = true;
  }
  if (transitionBusy && !autoPlayback && !captureWindup && !pending && presentation) {
    Object.assign(view, { title: presentation.title, eyebrow: 'Saved', detail: presentation.detail,
      layout: 'feedback', scene: true, portrait: null, member: null, stats: [], facts: [], items: [], back: false, footer: 'Your adventure continues…' });
  }
  if (captureFlight && performance.now() - captureFlight.started < 500) {
    Object.assign(view, { title: 'Ball away…', eyebrow: '', layout: 'capture-flight', scene: true, detail: '', items: [], back: false, footer: '' });
  }
  return view;
}

function renderDevice() {
  const current = navigation.state().screen;
  if (current !== 'capture-aim' && captureAim) { captureAim = null; captureGesture?.cancel('navigation'); }
  if (current === 'capture-aim' && (!captureAim || captureAim.revision !== revision || game?.phase !== 'encounter')) {
    captureAim = null; captureGesture?.cancel('state-changed'); navigation.setScreen(game?.phase === 'encounter' ? 'battle' : 'home'); return;
  }
  $('screen-surface').dataset.captureAim = String(current === 'capture-aim');
  captureGesture?.refresh();
  if (inputScreen !== current) { twoButtonInput?.cancel(); inputScreen = current; }
  const phase = game?.phase || null;
  if (pending && !['saving', 'recovery', 'recovery-confirm'].includes(current)) {
    const selection = pending.events?.at(-1);
    if (selection?.type === 'select' && Number.isInteger(selection.value)) {
      companionDetailId = selection.value; devicePendingReturn = 'companion';
    } else if (selection?.type === 'evolve') {
      progressionMemberId = pending.subjectMemberId || game?.activeCreatureId || 1; devicePendingReturn = 'evolution-result';
    } else if (selection?.type === 'release') {
      devicePendingReturn = 'release-result';
    } else if (pending.intent === 'recover') devicePendingReturn = 'care';
    else if (selection?.type === 'auto') devicePendingReturn = 'wild-auto-result';
    else if (selection?.type === 'mode') devicePendingReturn = 'explore';
    else devicePendingReturn = current;
    navigation.setScreen('saving'); return;
  }
  const practice = battleClient.getState();
  if ((practice.pending || practice.recoveryRequired) && !['battle-resolve', 'battle-recovery', 'battle-recovery-confirm'].includes(current)) { navigation.setScreen('battle-resolve'); return; }
  if (captureWindup && current !== 'capture') { navigation.setScreen('capture'); return; }
  if (!pending && !captureWindup && (['saving', 'recovery', 'recovery-confirm', 'capture'].includes(current))) {
    const target = phase === 'egg' ? 'starter-select' : phase === 'encounter' ? game.battleMode === 'auto' ? 'wild-auto-confirm' : 'battle' : ['saving', 'recovery', 'recovery-confirm', 'capture', 'battle'].includes(devicePendingReturn) ? 'home' : devicePendingReturn || 'home';
    devicePendingReturn = null; lastDevicePhase = phase; navigation.setScreen(target); return;
  }
  if (isEggState(game) && !['starter-select', 'starter-review', 'connection', 'saving', 'recovery', 'recovery-confirm'].includes(current)) { navigation.setScreen('starter-select'); return; }
  if (!isEggState(game) && (['starter-select', 'starter-review'].includes(current) || current === 'starter-hatched' && hatchedIdentity !== identity?.deviceId)) { navigation.setScreen('home'); return; }
  if (phase === 'encounter' && game.battleMode === 'auto' && current === 'battle') { navigation.setScreen('wild-auto-confirm'); return; }
  if (practice.mode === 'auto' && ['battle-choice', 'battle-cards', 'battle-result'].includes(current)) { navigation.setScreen('battle-auto-result'); return; }
  if (phase !== lastDevicePhase) {
    lastDevicePhase = phase;
    if (phase === 'encounter') { navigation.setScreen(game.battleMode === 'auto' ? 'wild-auto-confirm' : 'battle'); return; }
    if (current === 'battle') { navigation.setScreen('home'); return; }
  }
  const state = navigation.state();
  backgroundPlayer.select(backgroundSceneId()); syncBackgroundStatus();
  const scene = visualScene();
  const selectedMember = current === 'companion' || current === 'release-review' ? game?.collection.find(member => member.id === companionDetailId)
    : current.startsWith('evolution') || current === 'progression' ? progressionMember() : null;
  formArt.select(current === 'starter-select' && starters[state.index]?.formId ? [starters[state.index].formId] : current === 'starter-review' && chosenStarter()?.formId ? [chosenStarter().formId] : graphNode && current === 'evolution-node' ? [graphNode.formId] : rosterDetail && ['roster-detail', 'roster-stats', 'roster-growth'].includes(current) ? [rosterDetail.formId] : selectedMember ? [selectedMember.formId] : [scene?.formId, scene?.phase === 'encounter' ? scene.wildFormId : null]);
  if (preparedPlayer !== artKey(playerId(scene)) || preparedWild !== artKey(wildId(scene))) prepareCompanion();
  audio.setScene(state.screen.startsWith('battle-') && practice.state?.status === 'active' ? 'battle' : game?.phase === 'encounter' ? 'battle' : 'home');
  deviceScreen.render({ ...deviceView(state.screen), index: state.index, inputMode: deviceInputMode });
  $('device-confirm-button').disabled = !navigation.selected();
  $('device-back-button').disabled = !navigation.selected() && !canDeviceBack();
}

function openTools(id) {
  $('playtest-tools').open = true;
  const target = $(id); target?.scrollIntoView({ behavior: reducedMotion ? 'instant' : 'smooth', block: 'center' });
  target?.focus({ preventScroll: true });
}

function canDeviceBack() {
  if (navigation.state().screen === 'starter-select') return false;
  const practice = battleClient.getState();
  if (pending && conflict && ['recovery', 'recovery-confirm'].includes(navigation.state().screen) && !busy) return true;
  if (practice.legacyPending && ['battle-recovery', 'battle-recovery-confirm'].includes(navigation.state().screen) && !practice.busy) return true;
  return !pending && !practice.pending && !practice.busy && !practice.recoveryRequired && !practiceRequestRunning
    && (!(busy || transitionBusy) || Boolean(captureWindup)) && navigation.state().screen !== 'home';
}

function deviceBack() {
  if (canDeviceBack()) audio.playCue('menu-back');
  if (navigation.state().screen === 'starter-select') return;
  if (navigation.state().screen === 'starter-hatched' && !busy && !transitionBusy && !pending) { hatchedIdentity = null; navigation.setScreen('home'); return; }
  if (pending && conflict && !busy) {
    const current = navigation.state().screen;
    if (current === 'recovery-confirm') { navigation.setScreen('recovery'); return; }
    if (current === 'recovery') { navigation.setScreen('saving'); return; }
  }
  if ((busy || transitionBusy) && !captureWindup && !pending) return;
  if (navigation.state().screen === 'evolution-node' && !pending && !battleClient.getState().pending) {
    while (navigation.state().screen !== 'evolution-tree' && navigation.state().canBack) navigation.back();
    if (navigation.state().screen !== 'evolution-tree') navigation.setScreen('evolution-tree');
    return;
  }
  if (navigation.state().screen === 'evolution-result') { navigation.setScreen('progression'); return; }
  if (navigation.state().screen === 'release-result') { navigation.setScreen('companions'); return; }
  if (navigation.state().screen === 'wild-auto-result') { navigation.setScreen('home'); return; }
  if (navigation.state().screen === 'battle-auto-result') { navigation.setScreen('battle-mode'); return; }
  const practice = battleClient.getState();
  if (practice.legacyPending && !practice.busy) {
    const current = navigation.state().screen;
    if (current === 'battle-recovery-confirm') { navigation.setScreen('battle-recovery'); return; }
    if (current === 'battle-recovery') { navigation.setScreen('battle-resolve'); return; }
  }
  if (practice.busy || practice.recoveryRequired || practiceRequestRunning) return;
  if (navigation.state().screen === 'battle-choice') { navigation.setScreen('battle-mode'); return; }
  const result = navigation.back({ captureWindup: Boolean(captureWindup), pendingSync: Boolean(pending || practice.pending) });
  if (result === 'cancel-capture') cancelCapture();
  renderDevice();
}

async function deviceAction(id, event) {
  if (id !== 'back' && event?.isTrusted && !busy && (!transitionBusy || autoPlayback)) audio.playCue('menu-confirm');
  // Replay controls never modify the already committed game result.
  if (autoPlayback && id === 'auto-pause') { autoPlayback.paused = !autoPlayback.paused; renderDevice(); return; }
  if (autoPlayback && id === 'auto-finish') { autoPlayback.finished = true; renderDevice(); return; }
  if (id === 'back') return deviceBack();
  if (pending) {
    if (id === 'retry' || id === 'recovery-retry') return retryPending();
    if (conflict && !busy) {
      if (id === 'open-recovery' || id === 'recovery-back') navigation.setScreen('recovery');
      else if (id === 'recovery-confirm') navigation.setScreen('recovery-confirm');
      else if (id === 'discard-local' && navigation.state().screen === 'recovery-confirm') await discardConflict();
    }
    return;
  }
  const practice = battleClient.getState();
  if (practice.pending || practice.recoveryRequired) {
    if (practice.canArchiveLegacy) {
      if (id === 'practice-legacy-review') { navigation.setScreen('battle-recovery'); return; }
      if (id === 'practice-legacy-confirm') { navigation.setScreen('battle-recovery-confirm'); return; }
      if (id === 'practice-archive-legacy' && navigation.state().screen === 'battle-recovery-confirm') {
        await battleClient.archiveLegacyPending();
        if (!battleClient.getState().pending) navigation.setScreen('battle-mode');
        else navigation.setScreen('battle-resolve');
        return;
      }
    }
    if (id === 'practice-recovery-back') { navigation.setScreen('battle-resolve'); return; }
    if (id === 'practice-retry') return practiceCommand('retry');
    if (id === 'practice-recovery') { showNotice(`${practice.error || 'Saved battle data needs recovery.'} The saved request has not been removed. Reopen the browser profile that owns this playtest or restore its saved browser data before continuing.`); openTools('notice'); }
    return;
  }
  if (captureWindup) { if (id === 'cancel-capture') cancelCapture(); return; }
  if (busy || transitionBusy || practice.busy || practiceRequestRunning) return;
  if (id === 'reload-starters') return loadStarters();
  if (id === 'roster') { navigation.go('roster'); return loadRoster(); }
  if (id === 'journal') { journalOffset = 0; navigation.go('journal'); return loadRoster('journal'); }
  if (id === 'roster-retry') return loadRoster();
  if (id === 'roster-detail-retry') return loadRoster('detail', rosterSelectedId);
  if (id === 'roster-next' || id === 'roster-previous') {
    rosterQuery.offset = Math.max(0, rosterQuery.offset + (id === 'roster-next' ? ROSTER_PAGE_SIZE : -ROSTER_PAGE_SIZE)); return loadRoster();
  }
  if (id === 'journal-next' || id === 'journal-previous' || id === 'journal-retry') {
    if (id !== 'journal-retry') journalOffset = Math.max(0, journalOffset + (id === 'journal-next' ? ROSTER_PAGE_SIZE : -ROSTER_PAGE_SIZE));
    return loadRoster('journal');
  }
  if (/^roster-form-\d+$/.test(id)) { showRosterForm(Number(id.slice(12))); return; }
  if (id.startsWith('roster-letter-') || id.startsWith('roster-stage-') || id === 'roster-clear') {
    if (id === 'roster-clear') rosterQuery = { offset: 0, q: '', prefix: '', stage: '' };
    else if (id.startsWith('roster-letter-')) { rosterQuery.prefix = id === 'roster-letter-all' ? '' : id.slice(14); rosterQuery.q = ''; }
    else rosterQuery.stage = id === 'roster-stage-all' ? '' : ROSTER_STAGES[Number(id.slice(13))];
    rosterQuery.offset = 0; navigation.setScreen('roster'); return loadRoster();
  }
  if (id === 'roster-text-search') { openTools('roster-search'); return; }
  if (id === 'roster-references') { renderRosterTools(); openTools('roster-reference-title'); return; }
  if (id.startsWith('roster-move-') || id.startsWith('roster-note-')) return;
  if (/^starter-[1-8]$/.test(id) && isEggState(game)) {
    draftStarterId = Number(id.slice(8)); navigation.go('starter-review'); return;
  }
  if (id === 'hatch-starter') { if (canHatch()) return sendAction('hatch', draftStarterId); return; }
  if (id === 'meet-starter') { hatchedIdentity = null; navigation.setScreen('home'); return; }
  if (id === 'recover-review') {
    if (!canAct()) return;
    recoveryDraft = recoveryReview(game, identity?.deviceId, revision);
    if (recoveryDraft) navigation.go('recover-confirm');
    return;
  }
  if (id === 'confirm-recovery') {
    const events = reviewedRecoveryEvents(recoveryDraft, game, identity?.deviceId, revision);
    if (navigation.state().screen === 'recover-confirm' && canAct() && events) return sendAction('rest', 0, events);
    return;
  }
  if (id === 'progression') { showProgression(); return; }
  if (id === 'member-progression') { showProgression(companionDetailId); return; }
  if (id === 'make-room') { if (canAct() && game.phase === 'encounter' && game.wildRules >= 10) { companionPage = 0; navigation.go('companions'); } return; }
  if (id === 'release-review') { navigation.go('release-review'); return; }
  if (id === 'release-confirmation') {
    const member = game?.collection.find(entry => entry.id === companionDetailId);
    if (!canRelease(member)) return;
    releaseDraft = { deviceId: identity.deviceId, revision, memberId: member.id, name: member.name };
    navigation.go('release-confirm'); return;
  }
  if (id === 'confirm-release') {
    const member = game?.collection.find(entry => entry.id === releaseDraft?.memberId);
    if (navigation.state().screen === 'release-confirm' && releaseDraft?.deviceId === identity?.deviceId && releaseDraft?.revision === revision && canRelease(member)) return sendAction('release', member.id);
    return;
  }
  if (id === 'release-done') { releaseDraft = null; navigation.setScreen(game?.phase === 'encounter' ? game.battleMode === 'auto' ? 'wild-auto-confirm' : 'battle' : 'companions'); return; }
  if (id === 'toggle-roster-art') { formArt.setEnabled(!formArt.state().enabled); return; }
  if (id === 'progression-set-partner') return requestAction('select', progressionMemberId);
  if (/^evolve-\d+$/.test(id)) {
    const formId = Number(id.slice(7));
    if (progressionMemberId === game?.activeCreatureId && game.evolution.options.some(option => option.formId === formId)) {
      draftEvolutionId = formId; evolutionDraft = null; navigation.go('evolution-preview');
    }
    return;
  }
  if (id === 'evolution-review') {
    if (!canEvolve()) return;
    evolutionDraft = { deviceId: identity.deviceId, memberId: progressionMemberId, formId: draftEvolutionId, revision };
    navigation.go('evolution-confirm'); return;
  }
  if (id === 'confirm-evolution') {
    if (navigation.state().screen === 'evolution-confirm' && canEvolve() && evolutionDraft?.deviceId === identity.deviceId
      && evolutionDraft.memberId === game.activeCreatureId && evolutionDraft.formId === draftEvolutionId && evolutionDraft.revision === revision) return sendAction('evolve', draftEvolutionId);
    return;
  }
  if (id === 'evolution-done') { navigation.setScreen('progression'); return; }
  if (id === 'evolution-home') { navigation.setScreen('home'); return; }
  if (id === 'evolution-tree' || id === 'roster-graph') {
    const fromNode = ['evolution-node', 'evolution-links', 'evolution-node-skills'].includes(navigation.state().screen);
    if (fromNode) { while (navigation.state().screen !== 'evolution-tree' && navigation.state().canBack) navigation.back(); if (navigation.state().screen !== 'evolution-tree') navigation.setScreen('evolution-tree'); if (!evolutionGraph) await loadEvolutionTree(); }
    else {
      if (id === 'roster-graph') progressionMemberId = game?.activeCreatureId || 0;
      const focus = id === 'roster-graph' ? rosterDetail?.formId : progressionMember()?.formId;
      if (!focus) return;
      navigation.go('evolution-tree'); await loadEvolutionTree(focus, 0);
    }
    return;
  }
  if (id === 'reload-evolution-tree') return loadEvolutionTree();
  if (id === 'graph-next' && evolutionGraph?.nextOffset !== null) return loadEvolutionTree(graphFocusId, evolutionGraph.nextOffset);
  if (id === 'graph-previous') return loadEvolutionTree(graphFocusId, Math.max(0, graphOffset - 8));
  if (/^tree-\d+$/.test(id)) return showGraphNode(Number(id.slice(5)));
  if (/^graph-(parent|child)-\d+$/.test(id)) return showGraphNode(Number(id.split('-')[2]));
  if (id === 'graph-node-retry') return showGraphNode(treeNodeId);
  if (id === 'evolution-links') { graphLinksOffset = 0; navigation.go(id); void loadGraphLinkNames(); return; }
  if (id === 'evolution-node-skills') { navigation.go(id); return; }
  if (id === 'graph-links-next' || id === 'graph-links-previous') { graphLinksOffset += id.endsWith('next') ? 8 : -8; navigation.refresh(); void loadGraphLinkNames(); return; }
  if (id.startsWith('graph-skill-')) return;
  if (id.startsWith('evo-skill-')) return;
  if (id === 'wild-tactical' || id === 'wild-auto') {
    if (!canAct() || game.phase !== 'home') return;
    devicePendingReturn = 'explore'; await sendAction('mode', id === 'wild-auto' ? 1 : 0);
    if (!pending) navigation.setScreen('explore'); return;
  }
  if (id === 'wild-auto-start') { if (game?.phase === 'encounter' && game.battleMode === 'auto') return sendAction('auto'); return; }
  if (id === 'wild-auto-done') { presentation = null; navigation.setScreen('home'); return; }
  if (id === 'wild-auto-replay') return playAutoBattle('wild');
  if (id === 'practice-auto-start') { practiceDraftMode = 'auto'; return practiceCommand('start'); }
  if (id === 'practice-auto-result') { navigation.go('battle-auto-result'); return; }
  if (id === 'practice-auto-done') { navigation.setScreen('battle-mode'); return; }
  if (id === 'practice-auto-replay') return playAutoBattle('practice');
  if (id === 'practice-refresh') { await loadPractice(); const updated = battleClient.getState(); if (updated.loaded && !updated.pending && !updated.recoveryRequired) navigation.setScreen('battle-mode'); return; }
  if (id === 'partner-refresh') { await loadPractice(); renderDevice(); return; }
  if (id === 'practice-start') { navigation.go('battle-select-mode'); return; }
  if (id === 'practice-tactical') { practiceDraftMode = 'tactical'; return practiceCommand('start'); }
  if (id === 'practice-auto') { practiceDraftMode = 'auto'; navigation.go('battle-auto-confirm'); return; }
  if (id === 'practice-retry') return practiceCommand('retry');
  if (id === 'practice-resume' || id === 'practice-continue') { navigation.setScreen(practice.state?.status === 'active' ? 'battle-choice' : 'battle-mode'); return; }
  if (id === 'practice-card-spark' || id === 'practice-card-shelter') return practiceCommand('card', id === 'practice-card-spark' ? 1 : 2);
  if (id === 'practice-player-stats') return showStats('practice-player');
  if (id === 'practice-enemy-stats') return showStats('practice-enemy');
  if (id === 'practice-stats') { navigation.go(id); return; }
  if (id.startsWith('practice-')) return practiceCommand(id.slice(9));
  if (id === 'battle-mode' && !practice.loaded) { navigation.go(id); await loadPractice(); return; }
  if (id === 'member-stats') return showStats('member');
  if (id === 'wild-stats') return showStats('wild');
  if (id === 'stats') return showStats('active');
  if (id === 'type-chart') { navigation.go(id); if (!combatCatalog) await loadCombatCatalog(); return; }
  if (id === 'reload-catalog') return loadCombatCatalog();
  if (id.startsWith('member-')) { companionDetailId = Number(id.slice(7)); navigation.go('companion'); return; }
  if (id.startsWith('load-')) { $('saved-playtest-select').value = id.slice(5); await loadPlaytest(); renderDevice(); return; }
  if (['feed', 'play', 'rest', 'walk', 'attack', 'heavy', 'magic', 'capture'].includes(id)) return requestAction(id, id === 'walk' ? 100 : 0);
  if (id === 'select-companion') return requestAction('select', companionDetailId);
  if (id === 'card-spark' || id === 'card-shelter') return requestAction('card', id === 'card-spark' ? 1 : 2);
  if (id === 'companions-next') { companionPage = (companionPage + 1) % Math.max(1, Math.ceil((game?.collection.length || 0) / COMPANIONS_PER_PAGE)); navigation.refresh(); return; }
  if (id === 'saves-next') { savesPage = (savesPage + 1) % Math.ceil(savedPlaytests.length / 3); renderDevice(); return; }
  if (id === 'start-pairing' || id === 'new-pairing-code') return startPairing();
  if (id === 'claim-device') { await claimDevice(); if (game) navigation.setScreen(initialDeviceScreen()); return; }
  if (id === 'reconnect') return reconnect();
  if (id === 'new-playtest') { $('new-playtest-confirm').hidden = false; await newPlaytest(); if (!$('new-playtest-confirm').hidden) return; navigation.setScreen(initialDeviceScreen()); return; }
  if (id === 'toggle-sound') { await toggleSound(event); renderDevice(); return; }
  if (id === 'toggle-music') { const enabled = !audio.getState().musicEnabled; audio.setMusicEnabled(enabled); if (enabled) await audio.unlock(event); renderSound(); renderDevice(); return; }
  if (id === 'volume-down' || id === 'volume-up') { audio.setVolume(audio.getState().volume + (id === 'volume-up' ? .1 : -.1)); renderSound(); renderDevice(); return; }
  if (id === 'toggle-personal') { $('personal-use').checked = !$('personal-use').checked; $('personal-use').dispatchEvent(new Event('change')); renderDevice(); return; }
  if (id === 'open-assets') { navigation.go('asset-packs'); return; }
  if (id.startsWith('pack-')) { assetLibrary?.select(id.slice(5)); navigation.go('asset-progress'); return; }
  if (id === 'asset-install') { void assetLibrary?.install(assetState.selectedId); return; }
  if (id === 'asset-pause') { assetLibrary?.pause(); return; }
  if (id === 'retry-background') { backgroundPlayer.retry(); renderDevice(); return; }
  if (id === 'open-personal') return openTools('personal-files');
  navigation.go(id);
}

function playerId(state = game) { return isEggState(state) ? 'egg' : state?.artId || (state?.formId > 10 ? `ds-form-${state.formId}` : (state?.creature || 'Partner').toLowerCase()); }
function wildId(state = game) { return state?.wildFormId > 10 ? `ds-form-${state.wildFormId}` : (state?.wildArtId || state?.wildName || state?.wildSpecies || 'Rival').toLowerCase(); }
function findOriginalArt(id, category = 'sprites') {
  return Object.values(originalPacks).find(candidate => candidate?.[category]?.[id])
    || (builtinPack?.[category]?.[id] ? builtinPack : null);
}
function personalArtKey(id) {
  if (personalPack?.sprites?.[id]) return id;
  // This existing locally validated, restricted pack has an explicit Agumon
  // appearance in its old Mote slot. Do not rename game species or publish art.
  if (personalPack?.packId === 'personal-ds-line') {
    const matching = Object.entries(personalPack.sprites).find(([, sprite]) => sprite.name.toLowerCase() === id);
    if (matching) return matching[0];
  }
  return null;
}
function findArt(id) { return /^ds-form-\d+$/.test(id) ? formArt.get(Number(id.slice(8))) : personalArtKey(id) ? personalPack : findOriginalArt(id); }
function resolvedArtKey(id, art = findArt(id)) { return art === personalPack ? personalArtKey(id) || id : id; }
function artKey(id) {
  const candidate = findArt(id);
  return `${artGeneration}/${candidate?.packId || 'missing'}/${candidate?.version || 0}/${id}`;
}
function activeDisplayName() { return isEggState(game) ? 'Choose your egg' : personalPack?.sprites?.[personalArtKey(playerId())]?.name || game?.creature || 'Your partner'; }
function canAct() { return canWriteGame() && !needsTestEncounterRepair() && game.onboarding.completed && battleClient.getState().canSwitch; }

class ApiError extends Error {
  constructor(message, status, code = '') { super(message); this.status = status; this.code = code; }
}

function readStored(key) {
  try {
    const raw = localStorage.getItem(key);
    if (raw === null) return null;
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) storageCorrupt = true;
    return parsed;
  } catch { storageCorrupt = true; return null; }
}

function refreshStored() {
  identity = readStored(KEYS.identity);
  pending = readStored(KEYS.pending);
  if (identity !== null && (typeof identity?.token !== 'string' || !identity.token || typeof identity?.deviceId !== 'string' || !identity.deviceId)) {
    storageCorrupt = true; identity = null;
  }
  if (pending !== null && (!Array.isArray(pending?.events) || !pending.events.length || typeof pending?.batchId !== 'string'
    || !Number.isSafeInteger(pending?.baseRevision) || pending.baseRevision < 0 || typeof pending?.deviceId !== 'string'
    || pending.subjectMemberId !== undefined && (!Number.isInteger(pending.subjectMemberId) || pending.subjectMemberId < 1 || pending.subjectMemberId > MAX_MEMBER_ID)
    || pending.subjectMemberName !== undefined && (typeof pending.subjectMemberName !== 'string' || pending.subjectMemberName.length > 64))) {
    storageCorrupt = true; pending = null;
  }
  if (storageCorrupt) storageAvailable = false;
}

async function acquireWriter() {
  if (ownsWriter || !navigator.locks) return;
  await new Promise((ready, reject) => {
    navigator.locks.request('digivice-local-device-writer-v1', { ifAvailable: true }, async lock => {
      ownsWriter = Boolean(lock);
      if (!lock) { ready(); return; }
      // Browser releases the lock on navigation/crash; do not expire it mid-request.
      const lifetime = new Promise(resolve => { releaseWriter = resolve; });
      ready();
      await lifetime;
    }).catch(reject);
  });
}

function clearPending(batchId) {
  const saved = readStored(KEYS.pending);
  if (storageCorrupt || saved?.batchId !== batchId) {
    storageAvailable = false;
    throw new Error('The saved action changed unexpectedly. Browser data was kept for recovery.');
  }
  localStorage.removeItem(KEYS.pending);
}

function store(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); }
  catch { throw new Error('This browser cannot save locally. Allow site storage before sending actions.'); }
}

function readPlaytests() {
  try {
    const raw = localStorage.getItem(KEYS.playtests);
    if (raw === null) { savedPlaytests = []; return; }
    const list = JSON.parse(raw);
    if (!Array.isArray(list) || list.length > 8 || new Set(list.map(item => item.deviceId)).size !== list.length
      || list.some(item => typeof item?.deviceId !== 'string' || typeof item?.token !== 'string'
        || typeof item?.label !== 'string' || !item.deviceId || !item.token || item.label.length > 64)) throw new Error('invalid playtest list');
    savedPlaytests = list;
  } catch { playtestsCorrupt = true; }
}

function rememberIdentity(value) {
  if (playtestsCorrupt) throw new Error('The saved-playtest list is unreadable. Your current identity and existing browser data have been kept.');
  if (!value?.token || savedPlaytests.some(item => item.deviceId === value.deviceId)) return;
  if (savedPlaytests.length >= 8) throw new Error('This browser already keeps eight playtests. Load an existing save to continue.');
  const next = [...savedPlaytests, { deviceId: value.deviceId, token: value.token, label: `Playtest ${savedPlaytests.length + 1}` }];
  store(KEYS.playtests, next);
  savedPlaytests = next;
}

function setOnline(value) {
  online = value;
  $('connection').dataset.status = value ? 'online' : 'offline';
  $('connection-text').textContent = value ? 'Local service connected' : 'Local service unavailable';
}

function showNotice(message, mode = '') {
  $('notice-text').textContent = message;
  $('notice').hidden = !message;
  $('retry-sync').hidden = mode !== 'retry';
  $('discard-conflict').hidden = mode !== 'conflict';
  $('reconnect').hidden = mode !== 'service';
  $('retry-sync').textContent = pending && pending.rulesVersion !== RULES_VERSION ? 'Check previous action' : 'Retry saved action';
  $('discard-conflict').textContent = pending && pending.rulesVersion !== RULES_VERSION ? 'Discard pending request' : 'Discard local action';
}

async function api(path, { method = 'GET', body, authenticated = false, credential = identity } = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 8000);
  const headers = { Accept: 'application/json' };
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  if (authenticated && credential?.token) headers.Authorization = `Bearer ${credential.token}`;
  try {
    const response = await fetch(path, {
      method, headers, cache: 'no-store', signal: controller.signal,
      ...(body !== undefined ? { body: JSON.stringify(body) } : {}),
    });
    let result;
    try { result = await response.json(); }
    catch { throw new ApiError('The local service returned an unreadable response.', response.status); }
    if (!response.ok) {
      const message = typeof result.message === 'string' ? result.message
        : typeof result.error === 'string' ? result.error : result.error?.message;
      const code = typeof result.error === 'string' ? result.error : typeof result.error?.code === 'string' ? result.error.code : '';
      throw new ApiError(message || `The service rejected this request (${response.status}).`, response.status, code);
    }
    setOnline(true);
    return result;
  } catch (error) {
    if (!(error instanceof ApiError) || error.status >= 500) setOnline(false);
    throw error;
  } finally { clearTimeout(timeout); }
}

function errorText(error) {
  if (error.name === 'AbortError') return 'The local service did not respond in time.';
  if (error instanceof TypeError) return 'Cannot reach the local service. Check that npm run dev is still running.';
  return error.message || 'The local service could not complete this request.';
}

function displayMessage(value) {
  if (typeof value !== 'string' || !value) return 'A new little adventure awaits.';
  return value.replaceAll('_', ' ');
}

function addNote(text, detail = 'Saved to your local device') {
  notes.unshift({ text, detail });
  notes.splice(8);
  $('journal').replaceChildren(...notes.map(note => {
    const li = document.createElement('li');
    const dot = document.createElement('span'); dot.className = 'journal-dot';
    const p = document.createElement('p'); p.textContent = note.text;
    const small = document.createElement('small'); small.textContent = note.detail;
    p.append(small); li.append(dot, p); return li;
  }));
}

function validateSave(save) {
  const state = save?.state;
  validateAutoTrace(save?.autoTrace, 'wild');
  validateEvolutionOptions(state?.evolution?.options);
  const onboarding = state?.onboarding;
  const validOnboarding = onboarding && typeof onboarding.completed === 'boolean'
    && Number.isInteger(onboarding.offerSeed) && onboarding.offerSeed >= 0 && onboarding.offerSeed <= 0xffffffff
    && Array.isArray(onboarding.offers) && onboarding.offers.length === 3
    && (onboarding.offerSeed === 0 ? onboarding.offers.every(id => id === 0) : new Set(onboarding.offers).size === 3 && onboarding.offers.every(id => Number.isInteger(id) && id >= 1 && id <= 512))
    && (onboarding.starterId === null || Number.isInteger(onboarding.starterId) && onboarding.starterId >= 1 && onboarding.starterId <= 11);
  const validEgg = isEggState(state) && onboarding.starterId === null && Array.isArray(state.collection) && state.collection.length === 0
    && state.activeCreatureId === 0 && state.creature === null && state.species === null && state.combat === null && state.wildCombat === null;
  const validCompleted = onboarding?.completed && ['home', 'encounter'].includes(state.phase)
    && Array.isArray(state.collection) && state.collection.length >= 1 && state.collection.length <= 8
    && validCombat(state.combat) && state.collection.every(member => validCombat(member.combat) && validCare(member))
    && (state.phase === 'encounter' ? validCombat(state.wildCombat) : state.wildCombat === null)
    && state.collection.some(member => member.id === state.activeCreatureId);
  if (!save || !Number.isSafeInteger(save.revision) || save.revision < 0 || !state || new TextEncoder().encode(JSON.stringify(state)).byteLength >= 12288 || state.schemaVersion !== SCHEMA_VERSION || state.rulesVersion !== RULES_VERSION
    || !Number.isInteger(state.wildCaptureChance) || state.wildCaptureChance < 0 || state.wildCaptureChance > 100
    || !Number.isSafeInteger(state.foregroundSequence) || state.foregroundSequence < 0 || state.foregroundSequence > state.sequence
    || !validLastCapture(state.lastCapture, state.foregroundSequence) || !validWalkingState(state.walking, state.phase) || !validRecoveryCount(state.recoveryRestCount) || state.phase !== 'home' && state.recoveryRestCount !== 0 || !validEncounterRarity(state.wildRarity)
    || !Number.isSafeInteger(state.queuedEncounters) || state.queuedEncounters < 0 || state.queuedEncounters > 4294967295
    || !Number.isInteger(state.stepsToNextEncounter) || state.stepsToNextEncounter < 0 || state.stepsToNextEncounter > 100
    || !validOnboarding || !(validEgg || validCompleted) || state.collectionCapacity !== 8 || !['tactical', 'auto'].includes(state.battleMode) || state.maxLevel !== 20
    || !state.journal || state.journal.capacity !== 512 || !Array.isArray(state.journal.obtainedFormIds) || state.journal.obtainedFormIds.length > 512
    || !state.journal.obtainedFormIds.every((id, index, ids) => Number.isInteger(id) && id >= 1 && id <= 512 && (index === 0 || id > ids[index - 1]))
    || !Number.isInteger(state.nextMemberId) || state.nextMemberId < 1 || state.nextMemberId > 4294967295
    || !state.collection.every(member => Number.isInteger(member.id) && member.id >= 1 && member.id <= MAX_MEMBER_ID && Number.isInteger(member.formId) && member.formId >= 1 && member.formId <= 512
      && Number.isInteger(member.level) && member.level >= 1 && member.level <= 20 && Number.isInteger(member.xp) && member.xp >= 0 && member.xp <= 7600
      && Number.isInteger(member.xpToNext) && member.xpToNext >= 0 && member.xpToNext <= 7600)) {
    throw new Error(`Unsupported save format. This browser requires schema ${SCHEMA_VERSION} and game rules ${RULES_VERSION}. Any pending action has been kept.`);
  }
}

function acceptSave(save) {
  validateSave(save);
  revision = save.revision;
  game = save.state;
  wildAutoTrace = save.autoTrace;
  render();
}

function paintRosterPortrait(surface, member) {
  paintThumbnail(surface, member.artId, { original: !member.personal, type: member.type, rookie: member.rookie, pendingForm: member.pendingForm });
}

function paintThumbnail(surface, id, { original = false, type = 'neutral', rookie = false, pendingForm = false } = {}) {
  const art = original ? findOriginalArt(id) : findArt(id);
  const target = surface.getContext('2d');
  const size = surface.width;
  target.imageSmoothingEnabled = false;
  target.clearRect(0, 0, size, size);
  if (!art) {
    if (rookie || starters.some(starter => starter.species === id)) { paintRookiePlaceholder(target, size / 2, size * .4, size * .65, type); return; }
    if (pendingForm) { paintFormPlaceholder(target, size / 2, size * .4, size * .65); return; }
    // A type sigil is an explicit missing-art placeholder, never a different monster.
    target.fillStyle = { grove: '#b9df9b', tide: '#8cdaeb', ember: '#f3b282', neutral: '#d2b8ec' }[type] || '#d2b8ec';
    target.font = `bold ${Math.floor(size * .55)}px sans-serif`; target.textAlign = 'center';
    target.fillText({ grove: '✳', tide: '≈', ember: '△', neutral: '◇' }[type] || '◇', size / 2, size * .67); return;
  }
  const spriteId = resolvedArtKey(id, art);
  const item = art.sprites[spriteId];
  if (art.formatVersion === 1) {
    const first = prepareFrames(art, 'sprites', spriteId)?.frames[0];
    if (!first) return;
    const scale = Math.max(1, Math.floor(size / first.width));
    target.drawImage(first, (size - first.width * scale) / 2, (size - first.height * scale) / 2, first.width * scale, first.height * scale);
  } else {
    const scale = Math.floor(size / item.width);
    item.frames[0].forEach((row, y) => [...row].forEach((pixel, x) => {
      if (pixel === '.') return;
      target.fillStyle = art.palette[pixel]; target.fillRect(x * scale, y * scale, scale, scale);
    }));
  }
}

function renderCollection() {
  const members = game?.collection || [];
  const capacity = game?.collectionCapacity || 8;
  $('collection-count').textContent = `${members.length} / ${capacity}`;
  $('collection-status').textContent = isEggState(game) ? 'Choose and hatch your egg to meet your first partner.' : !game ? 'Pair a virtual device to meet your first companion.'
    : partnerLockReason() || (members.length === capacity ? 'Your collection is full. Choose any companion for care, walks and battles.'
        : 'Choose who travels with you. Care, energy, bond and evolution are saved for each companion.');
  $('collection-history').hidden = !game?.legacyCaptures;
  $('collection-history').textContent = game?.legacyCaptures ? `Earlier saves also recorded ${game.legacyCaptures} captures without individual companion details.` : '';
  const signature = JSON.stringify([members, game?.activeCreatureId, artGeneration]);
  if (signature !== collectionSignature) {
    collectionSignature = signature;
    $('collection-grid').replaceChildren(...members.map(member => {
      const item = document.createElement('article'); item.className = 'collection-member';
      item.setAttribute('role', 'listitem'); item.dataset.memberId = String(member.id);
      item.dataset.active = String(member.id === game.activeCreatureId);
      const artwork = document.createElement('canvas'); artwork.width = artwork.height = 64; artwork.className = 'collection-art';
      artwork.setAttribute('role', 'img'); artwork.setAttribute('aria-label', `${member.name} companion artwork`);
      paintThumbnail(artwork, member.artId || member.name.toLowerCase(), { type: member.combat.type, rookie: member.stage === 'Rookie', pendingForm: Boolean(member.stage) });
      const name = document.createElement('h3'); name.textContent = member.name;
      const detail = document.createElement('p'); detail.className = 'collection-member-detail';
      detail.textContent = `No. ${String(member.id).padStart(2, '0')} · Level ${member.level}`;
      const bond = document.createElement('p'); bond.className = 'collection-member-bond'; bond.dataset.memberBond = '';
      bond.textContent = `${member.bond} bond`;
      const stats = document.createElement('p'); stats.className = 'collection-member-stats';
      stats.textContent = `Energy ${member.energy} · Fullness ${member.fullness}`;
      const select = document.createElement('button'); select.type = 'button'; select.className = 'button secondary';
      select.dataset.selectMember = String(member.id);
      select.textContent = member.id === game.activeCreatureId ? 'Current partner' : `Set ${member.name} as partner`;
      select.setAttribute('aria-label', member.id === game.activeCreatureId ? `${member.name}, active companion ${member.id}` : `Select ${member.name}, companion ${member.id}`);
      select.setAttribute('aria-pressed', String(member.id === game.activeCreatureId));
      item.append(artwork, name, detail, bond, stats, select);
      if (!findArt(member.artId || (member.formId > 10 ? `ds-form-${member.formId}` : member.name.toLowerCase()))) {
        const missing = document.createElement('span'); missing.className = 'collection-art-hint'; missing.textContent = member.stage ? 'Artwork pending · original placeholder' : 'Family art available in the pack library'; item.append(missing);
      }
      return item;
    }));
  }
  for (const button of $('collection-grid').querySelectorAll('[data-select-member]')) {
    button.disabled = !canSetPartner(Number(button.dataset.selectMember));
  }
}

function render() {
  const paired = Boolean(identity?.token);
  $('setup-unpaired').hidden = paired;
  $('setup-paired').hidden = !paired;
  $('start-pairing').disabled = busy || !online || !storageAvailable || !ownsWriter;
  $('claim-device').disabled = busy || !pairingCode || !storageAvailable || !ownsWriter;
  $('retry-sync').disabled = busy || transitionBusy || !ownsWriter || !storageAvailable;
  $('discard-conflict').disabled = busy || transitionBusy || !ownsWriter || !storageAvailable;
  $('reconnect').disabled = busy || transitionBusy;
  const actionsEnabled = canAct();
  const encounter = game?.phase === 'encounter' && !needsTestEncounterRepair();
  for (const button of document.querySelectorAll('[data-event]')) {
    const type = button.dataset.event;
    const battle = ['attack', 'heavy', 'magic', 'capture'].includes(type);
    button.disabled = !actionsEnabled || (battle ? !encounter || game.battleMode !== 'tactical' : encounter);
    if (type === 'heavy') button.disabled ||= game?.energy < 6;
    if (type === 'capture') {
      button.disabled ||= !captureChoice(game).available;
      button.textContent = captureChoice(game).label;
    }
  }
  $('swipe-card').disabled = !actionsEnabled || !encounter || game?.cardUsed || game?.battleMode !== 'tactical';
  $('card-choice').disabled = !actionsEnabled || !encounter || game?.cardUsed || game?.battleMode !== 'tactical';
  $('battle-actions').hidden = !encounter;
  if (encounter) $('encounter-copy').textContent = `A wild ${game.wildName} appeared · ${game.wildHp} / ${game.wildMaxHp} health. ${captureChoice(game).detail}.`;
  $('battle-modifiers').textContent = game?.attackBoost ? `Spark ready · +${game.attackBoost} to the next attack`
    : game?.shield ? `Shelter active · ${game.shield} shield remaining` : game?.cardUsed ? 'Field card used for this encounter' : 'One field card available';
  $('capture-windup').hidden = !captureWindup;
  $('cancel-capture').disabled = !captureWindup;
  for (const key of ['hp', 'energy', 'fullness', 'mood']) {
    const value = game?.[key];
    const maximum = key === 'hp' ? game?.combat?.maxHp || 100 : 100;
    $(key + '-value').textContent = Number.isFinite(value) ? `${value} / ${maximum}` : '—';
    $('stat-' + key).max = maximum;
    $('stat-' + key).value = Number.isFinite(value) ? value : 0;
  }
  const scene = visualScene();
  if (preparedPlayer !== artKey(playerId(scene)) || preparedWild !== artKey(wildId(scene))) prepareCompanion();
  $('creature-name').textContent = activeDisplayName();
  $('creature-description').textContent = isEggState(game) ? 'Your egg choice becomes your saved first partner.' : personalArtKey(playerId())
    ? `Personal artwork: ${activeDisplayName()}; uses ${game?.creature || 'Partner'} game rules.`
    : CREATURE_DESCRIPTIONS[playerId()] || 'Your travelling companion.';
  const missingArt = !isEggState(game) && (!findArt(playerId()) || (encounter && !findArt(wildId())));
  $('companion-art-status').hidden = !missingArt;
  $('companion-art-status').textContent = missingArt ? game?.stage ? `Artwork pending. An original placeholder represents ${game.creature}; its saved form is ${game.stage}.` : 'This companion’s original art is not saved yet. Download its family in the pack library below; your game and care stats are ready.' : '';
  $('level').textContent = isEggState(game) ? 'EGG' : `LV. ${game?.level || 1}`;
  $('steps').textContent = (game?.steps || 0).toLocaleString();
  $('bond').textContent = game?.bond ?? '—';
  $('captures').textContent = game?.captures ?? '—';
  $('revision').textContent = String(revision);
  $('device-id').textContent = identity?.deviceId || '';
  const activeArt = findArt(playerId());
  $('pack-name').textContent = activeArt ? `${activeArt.packId} · v${activeArt.version}` : 'Family pack not saved';
  renderCollection();
  renderPlaytest();
  renderPlaytests();
  renderDevice();
  canvas.setAttribute('aria-label', needsTestEncounterRepair() ? 'Updating a saved encounter. Your companions and progress are kept.' : isEggState(game) ? 'Choose an egg. Each egg clearly identifies the Rookie it hatches into.' : game
    ? `${game.creature}, level ${game.level}. ${game.phase}. ${displayMessage(game.message)}. ${game.steps} steps. Health ${game.hp}. Energy ${game.energy}.`
    : 'Round virtual device. Pair a virtual device and choose your first egg to begin.');
  const repairKey = `${identity?.deviceId}/${revision}`;
  if (needsTestEncounterRepair() && canWriteGame() && repairAttempt !== repairKey) {
    repairAttempt = repairKey; queueMicrotask(() => { void sendAction('resolve-test-encounter', 0); });
  }
}

function renderPlaytest() {
  const view = needsTestEncounterRepair() ? { stage: 'recovery', title: 'Updating your encounter', detail: 'Your saved companions and progress are kept.' } : presentation || { stage: game?.phase === 'encounter' ? 'battle' : 'ready',
    title: game?.phase === 'encounter' ? `A wild ${game.wildName} appeared.` : game ? `Ready, ${activeDisplayName()}?` : 'Say hello to your companion.',
    detail: game ? (game.phase === 'encounter' ? 'Use a field card, attack, then try a capture.' : 'Care for your companion or explore a little further.') : 'Pair a virtual device below to begin.' };
  $('playtest-feedback').dataset.stage = pending ? 'save-pending' : view.stage;
  $('playtest-stage').textContent = pending ? 'WAITING FOR A CONFIRMED SAVE' : view.stage.replaceAll('-', ' ').toUpperCase();
  $('playtest-title').textContent = pending ? 'Your action is kept safely.' : view.title;
  $('playtest-detail').textContent = pending ? 'A response is still needed. Retry the saved action if the connection is interrupted.' : view.detail;
  $('device-shell').dataset.stage = view.stage;
  audio.setScene(view.stage === 'walking' ? 'explore' : (sceneState || game)?.phase === 'encounter' ? 'battle' : 'home');
  renderSound();
}

function renderSound() {
  const state = audio.getState();
  $('sound-toggle').disabled = !state.supported;
  $('sound-toggle').textContent = !state.supported ? 'Sound unavailable' : !state.unlocked ? 'Enable sound' : state.muted ? 'Sound off' : 'Sound on';
  $('sound-toggle').setAttribute('aria-pressed', String(state.unlocked && !state.muted));
  $('sound-volume').value = String(Math.round(state.volume * 100));
  $('sound-music').checked = state.musicEnabled;
  $('sound-status').textContent = state.hidden ? 'Audio paused while away.' : state.unlocked && !state.muted ? 'Gentle game sounds enabled.' : 'Sound is optional.';
}

function renderPlaytests() {
  $('saved-playtests').hidden = !identity?.token;
  const signature = JSON.stringify([savedPlaytests.map(({ deviceId, label }) => ({ deviceId, label })), identity?.deviceId]);
  if (signature !== playtestOptionsSignature) {
    playtestOptionsSignature = signature;
    $('saved-playtest-select').replaceChildren(...savedPlaytests.map(item => new Option(item.label, item.deviceId, false, item.deviceId === identity?.deviceId)));
  }
  const locked = !canAct() || playtestsCorrupt;
  $('saved-playtest-select').disabled = locked;
  $('load-playtest').disabled = locked || !savedPlaytests.length;
  $('new-playtest').disabled = locked || savedPlaytests.length >= 8;
  $('confirm-new-playtest').disabled = locked || savedPlaytests.length >= 8;
  $('playtest-save-note').textContent = playtestsCorrupt ? 'The saved-playtest list is unreadable. Existing browser data has been kept.'
    : pending || !battleClient.getState().canSwitch ? 'Resolve the pending action or practice battle request before changing playtests.'
      : savedPlaytests.length >= 8 ? 'Eight playtests are saved. Load one to continue; no save has been deleted.' : 'Your current game is saved after every action.';
}

function prepareCompanion(animation = 'idle', wildAnimation = 'idle') {
  const scene = visualScene();
  const playerArt = findArt(playerId(scene));
  const wildArt = findArt(wildId(scene));
  playerFrames = playerArt?.formatVersion === 1 ? prepareFrames(playerArt, 'sprites', resolvedArtKey(playerId(scene), playerArt), animation) : null;
  wildFrames = wildArt?.formatVersion === 1 ? prepareFrames(wildArt, 'sprites', resolvedArtKey(wildId(scene), wildArt), wildAnimation) : null;
  preparedPlayer = artKey(playerId(scene));
  preparedWild = artKey(wildId(scene));
}

function setStage(stage, title, detail, scene, { animation = 'idle', wildAnimation = 'idle', effect, target = 'wild', cue } = {}) {
  presentation = { stage, title, detail, started: performance.now() };
  sceneState = scene;
  prepareCompanion(animation, wildAnimation);
  effectFrames = effect ? prepareFrames(findOriginalArt(effect, 'effects'), 'effects', effect) : null;
  effectTarget = target;
  effectStarted = performance.now();
  if (cue && !document.hidden) audio.playCue(cue);
  render();
}

function waitStage(milliseconds) {
  if (document.hidden) return Promise.resolve();
  return new Promise(resolve => {
    const done = () => { clearTimeout(timer); if (stageWait === done) stageWait = null; resolve(); };
    const timer = setTimeout(done, reducedMotion ? Math.min(80, milliseconds) : milliseconds);
    stageWait = done;
  });
}

function finishPresentation() {
  sceneState = null; effectFrames = null; transitionBusy = false;
  prepareCompanion(); render();
}

async function presentResult(type, before, after) {
  if (!before || document.hidden) return;
  transitionBusy = true;
  const captured = after.collection.length > before.collection.length;
  const retreated = before.phase === 'encounter' && after.phase === 'home' && /retreat/i.test(after.message);
  const won = ['attack', 'heavy', 'magic'].includes(type) && before.phase === 'encounter' && after.phase === 'home' && !retreated;
  try {
    if (type === 'hatch' && after.onboarding.completed) {
      paintStarterEgg(hatchEgg, after.onboarding.starterId);
      hatchedIdentity = identity.deviceId;
      navigation.setScreen('starter-hatched');
      setStage('hatching', 'A new beginning…', `${after.creature} hatched into your first partner.`, after, { effect: 'spark', target: 'player', cue: 'evolution' });
      addNote(`${after.creature} hatched!`, 'Your Rookie partner is saved');
      await waitStage(reducedMotion ? 0 : 800);
    } else if (type === 'evolve') {
      setStage('digivolution', `Hello, ${after.creature}!`, `${after.stage || 'New form'} · Same companion, new form.`, after,
        { animation: 'celebrate', effect: 'spark', target: 'player', cue: 'evolution' });
      await waitStage(650);
    } else if (type === 'walk' || type === 'explore') {
      setStage('walking', 'A little further…', `${after.steps - before.steps} simulated steps explored.`, before, { cue: 'steps' });
      await waitStage(380);
      if (after.phase === 'encounter') {
        setStage('encounter', `${after.wildName} appeared!`, 'A new encounter is ready. Choose your next move.', after, { cue: 'encounter' });
        addNote(`${after.wildName} appeared.`, `${after.steps} total simulated steps`);
      } else {
        setStage('walking', 'Keep exploring…', `${after.walking.remainingSteps} eligible steps until the next encounter.`, after);
      }
      await waitStage(450);
    } else if (['attack', 'heavy', 'magic'].includes(type)) {
      const wildDamage = retreated ? Math.max(0, before.wildHp - after.wildHp) : Math.max(0, before.wildHp - (won ? 0 : after.wildHp));
      const reflected = type === 'heavy' && before.wildGuard === 'counter';
      // Display only observed health changes; the core owns damage and chance.
      const attacked = { ...before, wildHp: won ? 0 : retreated ? before.wildHp : after.wildHp };
      setStage('attack', `${before.creature} attacks!`, reflected ? `${before.wildName}'s Counter reflects the heavy attack.` : won ? `${before.wildName} is defeated.` : retreated ? 'This encounter ended with a gentle retreat.' : `${wildDamage} wild health lost.`, attacked, { animation: 'attack', wildAnimation: reflected ? 'idle' : 'hurt', effect: reflected ? 'spark' : 'hit', cue: type === 'magic' ? 'attack-magic' : 'attack-physical' });
      addNote(`${before.creature} attacked ${before.wildName}.`, won ? 'Battle won' : retreated ? 'The core ended this encounter with a retreat' : `${wildDamage} wild health lost`);
      await waitStage(420);
      if (!won && !retreated) {
        const loss = Math.max(0, before.hp - after.hp);
        const responseTitle = reflected ? 'The heavy attack was reflected.' : loss ? `${before.wildName} struck back.` : 'Your guard held.';
        setStage('retaliation', responseTitle, loss ? `${loss} companion health lost · ${after.hp} health remaining.` : `${after.hp} health remaining.`, after,
          { animation: loss ? 'hurt' : 'idle', wildAnimation: 'attack', effect: loss ? 'hit' : 'spark', target: 'player', cue: loss ? 'hit' : null });
        addNote(responseTitle, `${after.hp} companion health remaining`);
        await waitStage(420);
      }
      if (won) { setStage('win', 'A friendly battle won.', displayMessage(after.message), after, { animation: 'celebrate', cue: 'win' }); await waitStage(600); }
    } else if (type === 'capture' || type === 'flick') {
      const aim = type === 'flick' && captureFlight ? decodeCaptureFlick(captureFlight.value) : null;
      // Saved metadata is the only source of displayed odds. Animation never
      // rolls, updates the save, or changes a throw's original probability.
      const committed = after.lastCapture?.sequence === after.foregroundSequence ? captureReport(after.lastCapture) : null;
      const report = committed || (before.wildRules < 12 ? { missed: Boolean(aim && !aim.hit),
        odds: aim && !aim.hit ? 'Miss · no catch' : `${before.wildCaptureChance}% throw chance`,
        attempts: `Throw ${before.captureAttempts + 1}/3${captured ? '' : ` · ${2 - before.captureAttempts} left`}` } : null);
      const missed = report ? report.missed : Boolean(aim && !aim.hit);
      if (captureFlight) await waitStage(Math.max(0, 460 - (performance.now() - captureFlight.started)));
      if (!missed) {
        setStage('capture-impact', 'Connected…', report ? `${report.odds} · ${report.attempts}` : 'The saved throw connected.', before, { cue: 'hit' });
        await waitStage(180);
        for (let wiggle = 0; wiggle < 3; wiggle++) {
          setStage('capture-check', 'Holding…', report ? `${report.odds} · ${report.attempts}` : 'Revealing the saved capture result.', before, { cue: 'capture-wiggle' });
          await waitStage(520);
        }
      }
      const lostHp = Math.max(0, before.hp - after.hp);
      const detail = report ? `${report.odds} · ${report.attempts}` : `${displayMessage(after.message)}${lostHp ? ` ${lostHp} companion health lost.` : ''}`;
      captureFlight = null;
      setStage('capture-result', captured ? `${before.wildName} joined your crew!` : missed ? 'The throw missed.' : `${before.wildName} slipped free.`, detail, after,
        { animation: captured ? 'celebrate' : 'idle', effect: captured ? 'capture' : null, cue: captured ? 'capture-success' : 'capture-fail' });
      addNote(captured ? `${before.wildName} joined your collection.` : `${before.wildName} was not captured.`, `${detail} · Saved revision ${revision}`);
      await waitStage(1600);
    } else if (type === 'card') {
      setStage('card', after.attackBoost ? 'Spark is ready.' : 'Shelter surrounds you.', displayMessage(after.message), after, { animation: 'celebrate', effect: 'spark', target: 'player', cue: 'card' });
      addNote(displayMessage(after.message), 'One field card used for this encounter'); await waitStage(450);
    } else {
      const animation = { feed: 'care', play: 'celebrate', rest: 'sleep' }[type] || 'idle';
      setStage(type === 'select' ? 'companion-selected' : 'care', type === 'select' ? `${after.creature} is coming along.` : displayMessage(after.message), `Saved · revision ${revision}`, after,
        { animation, effect: type === 'feed' || type === 'play' ? 'heart' : null, target: 'player', cue: type });
      addNote(displayMessage(after.message), `Saved · revision ${revision}`); await waitStage(420);
    }
    if (retreated) { setStage('retreat', 'Safely back home.', displayMessage(after.message), after, { cue: 'retreat' }); addNote('A gentle retreat brought you home.', 'Rest whenever you are ready'); await waitStage(600); }
    if (!['hatch', 'evolve'].includes(type) && before.activeCreatureId === after.activeCreatureId && after.level > before.level) {
      setStage('level-up', `Level ${after.level}!`, 'Battle experience helped your companion grow. Its form is unchanged.', after, { animation: 'celebrate', effect: 'spark', target: 'player', cue: 'evolution' });
      addNote(`${after.creature} reached level ${after.level}.`, `${after.xp} saved experience`); await waitStage(700);
    }
  } finally { captureFlight = null; finishPresentation(); }
}

let testFixturesSupported = false;
async function reconnect() {
  if (busy || transitionBusy) return;
  repairAttempt = null;
  connectionError = '';
  busy = true; render();
  try {
    await acquireWriter();
    refreshStored();
    readPlaytests();
    if (identity && ownsWriter && storageAvailable && !playtestsCorrupt) {
      try { rememberIdentity(identity); }
      catch { playtestsCorrupt = true; } // Optional switcher storage must not prevent restoring the current game.
    }
    const health = await api('/api/health'); captureFlickSupported = health.capabilities?.captureFlick === 1;
    testFixturesSupported = health.capabilities?.testFixtures === 1;

    if (identity?.token) {
      acceptSave(await api('/api/save', { authenticated: true }));
      if (isEggState(game) && !pending) await loadStarters();
      if (!pending && wildAutoTrace && wildAutoTrace.endSequence === game.foregroundSequence) navigation.setScreen('wild-auto-result');
      void loadPractice();
      if (!notes.length) addNote('Welcome back, little explorer.', `Save revision ${revision} restored`);
    }
    if (!storageAvailable) showNotice('Browser storage is blocked or contains unreadable setup or pending action data. Existing data was kept. Restore site storage or use a fresh local browser profile.');
    else if (!ownsWriter) showNotice(navigator.locks ? 'This tab is read-only because another Digivice tab controls this device. Close that tab, then reconnect here.' : 'This browser cannot safely coordinate device saves. Use a current browser with Web Locks support.', 'service');
    else if (pending) showNotice(pending.rulesVersion === RULES_VERSION
      ? 'An action is waiting for a confirmed save. Retry it safely before continuing.'
      : 'An action from earlier or unknown game rules is saved in this browser. Check its status before continuing; it will not be relabelled as a new action.', 'retry');
    else showNotice('');
  } catch (error) {
    connectionError = errorText(error);
    showNotice(connectionError, pending ? 'retry' : 'service');
  } finally { busy = false; render(); }
}

async function startPairing() {
  if (busy || !ownsWriter || !storageAvailable) return;
  connectionError = '';
  busy = true; render();
  try {
    const result = await api('/api/pairing/start', { method: 'POST', body: {} });
    pairingCode = result.code;
    $('pairing-code').textContent = pairingCode;
    const expiry = new Date(result.expiresAt);
    $('pairing-expiry').textContent = Number.isNaN(expiry.getTime()) ? 'Temporary development code' : `Expires at ${expiry.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}`;
    $('pairing-box').hidden = false;
    showNotice('');
  } catch (error) { connectionError = errorText(error); showNotice(connectionError, 'service'); }
  finally { busy = false; render(); }
}

async function claimDevice() {
  if (!pairingCode || busy || !ownsWriter || !storageAvailable) return;
  connectionError = '';
  busy = true; render();
  try {
    const result = await api('/api/pairing/claim', { method: 'POST', body: { code: pairingCode } });
    const nextIdentity = { deviceId: result.deviceId, token: result.token };
    validateSave(result);
    rememberIdentity(nextIdentity);
    store(KEYS.identity, nextIdentity);
    identity = nextIdentity;
    acceptSave(result);
    if (isEggState(game) && !pending) await loadStarters();
    void loadPractice();
    hatchedIdentity = null;
    navigation.setScreen(initialDeviceScreen());
    addNote(isEggState(game) ? 'Your first egg is waiting.' : `Say hello to ${game.creature}.`, 'Your virtual device is paired');
    showNotice('');
  } catch (error) { connectionError = errorText(error); showNotice(connectionError, 'service'); }
  finally { busy = false; render(); }
}

async function loadPlaytest() {
  if (!canAct() || playtestsCorrupt) return;
  const target = savedPlaytests.find(item => item.deviceId === $('saved-playtest-select').value);
  if (!target || target.deviceId === identity.deviceId) return;
  busy = true; render();
  try {
    const result = await api('/api/save', { authenticated: true, credential: target });
    validateSave(result);
    const nextIdentity = { deviceId: target.deviceId, token: target.token };
    store(KEYS.identity, nextIdentity);
    identity = nextIdentity; hatchedIdentity = null; presentation = null; sceneState = null; notes.length = 0;
    acceptSave(result);
    if (isEggState(game) && !pending) await loadStarters();
    void loadPractice();
    addNote(`${target.label} restored.`, 'Your companions and progress are ready');
    $('new-playtest-confirm').hidden = true;
    showNotice('');
  } catch (error) { showNotice(errorText(error), 'service'); }
  finally { busy = false; render(); }
}

async function newPlaytest() {
  if (!canAct() || playtestsCorrupt || savedPlaytests.length >= 8 || $('new-playtest-confirm').hidden) return;
  busy = true; render();
  try {
    // Keep the previous credential before requesting a fresh device. Nothing
    // overwrites its server save or deletes any existing collection.
    rememberIdentity(identity);
    const pairing = await api('/api/pairing/start', { method: 'POST', body: {} });
    const result = await api('/api/pairing/claim', { method: 'POST', body: { code: pairing.code } });
    validateSave(result);
    const nextIdentity = { deviceId: result.deviceId, token: result.token };
    rememberIdentity(nextIdentity);
    store(KEYS.identity, nextIdentity);
    identity = nextIdentity; hatchedIdentity = null; presentation = null; sceneState = null; notes.length = 0;
    acceptSave(result);
    if (isEggState(game) && !pending) await loadStarters();
    void loadPractice();
    $('new-playtest-confirm').hidden = true;
    addNote('A new playtest is ready.', 'Your earlier game is kept in Saved playtests');
    showNotice('');
  } catch (error) { showNotice(`${errorText(error)} Your previous playtest is still kept.`, 'service'); }
  finally { busy = false; render(); }
}

function cancelCapture(reason = 'Capture cancelled before sending.') {
  if (!captureWindup) return;
  clearTimeout(captureWindup.timer);
  captureWindup = null;
  presentation = { stage: 'cancelled', title: 'Capture cancelled.', detail: 'No action was sent and your save is unchanged.' };
  addNote(reason, 'No capture attempt was used');
  finishPresentation();
}

async function requestAction(type, value = 0) {
  if (!canAct()) return;
  if (['attack', 'heavy', 'magic', 'capture', 'card'].includes(type) && game.battleMode !== 'tactical') return;
  if (type === 'select' && !canSetPartner(value)) return;
  if (type !== 'capture') return sendAction(type === 'walk' ? 'explore' : type, value);
  if (!captureChoice(game).available) return;
  if (deviceInputMode === 'touch') {
    if (!captureFlickSupported) { showNotice('This service needs the capture-flick update. Two-button capture remains available.'); return; }
    captureAim = { revision, preview: null, hint: 'Drag the ball, then flick up at your new friend.' };
    captureFlight = null; audio.playCue('capture-arm'); navigation.go('capture-aim'); return;
  }
  // A short cancellable presentation comes before any request or durable batch.
  // It is not a timing mini-game and cannot change core capture odds.
  if (game.phase !== 'encounter') return;
  transitionBusy = true;
  captureWindup = { timer: null, revision };
  setStage('capture-windup', 'Preparing the capture beam…', 'Cancel now to keep your capture attempt.', game, { effect: 'capture', cue: 'capture-arm' });
  captureWindup.timer = setTimeout(() => {
    if (!captureWindup || document.hidden || captureWindup.revision !== revision) { cancelCapture(); return; }
    captureWindup = null; transitionBusy = false; sceneState = null; effectFrames = null;
    audio.playCue('capture-throw');
    // sendAction sets the durable pending batch synchronously before yielding.
    void sendAction('capture', 0);
  }, 1200);
}

async function sendAction(type, value = 0, recoveryEvents = null) {
  if (type === 'resolve-test-encounter' ? !needsTestEncounterRepair() || !canWriteGame() : type === 'hatch' ? !canHatch() : !canAct()) return;
  if (type === 'select' && !canSetPartner(value)) return;
  if (type === 'evolve' && (!canEvolve() || evolutionOption().formId !== value)) return;
  if (type === 'release' && !canRelease(game.collection.find(member => member.id === value))) return;
  if (['attack', 'heavy', 'magic', 'capture', 'flick', 'card'].includes(type) && game.battleMode !== 'tactical') return;
  if (type === 'flick' && (!captureFlickSupported || !decodeCaptureFlick(value) || !captureChoice(game).available)) return;
  if (recoveryEvents && (type !== 'rest' || value !== 0 || JSON.stringify(recoveryEvents) !== JSON.stringify(reviewedRecoveryEvents(recoveryDraft, game, identity?.deviceId, revision)))) return;
  try {
    // Preserve request identity before sending. A timeout may happen after commit.
    const next = { deviceId: identity.deviceId, rulesVersion: RULES_VERSION, baseRevision: revision, batchId: crypto.randomUUID(), events: recoveryEvents || [{ type, value }], ...(recoveryEvents ? { intent: 'recover', subjectMemberId: game.activeCreatureId } : {}), ...(type === 'evolve' ? { subjectMemberId: game.activeCreatureId } : {}), ...(type === 'release' ? { subjectMemberId: value, subjectMemberName: game.collection.find(member => member.id === value).name } : {}) };
    store(KEYS.pending, next);
    pending = next;
    if (type === 'select') partnerConfirmation = null;
    conflict = false;
    await retryPending();
  } catch (error) { captureFlight = null; showNotice(errorText(error)); render(); }
}

async function retryPending() {
  if (!pending || busy || transitionBusy || !identity?.token || !ownsWriter || !storageAvailable) return;
  if (pending.deviceId !== identity.deviceId) {
    conflict = true;
    showNotice('This saved action belongs to another virtual device. Discard it before using the current device.', 'conflict');
    render(); return;
  }
  busy = true; render();
  let acknowledged = false;
  const before = game ? structuredClone(game) : null;
  const beforeRevision = revision;
  const actionType = pending.events.at(-1)?.type;
  const recovery = pending.intent === 'recover';
  const selectedMemberId = actionType === 'select' ? pending.events.at(-1)?.value : null;
  const evolvingMemberId = actionType === 'evolve' ? pending.subjectMemberId || game?.activeCreatureId : null;
  const evolvingFormId = actionType === 'evolve' ? pending.events.at(-1)?.value : null;
  const releasing = actionType === 'release' ? { deviceId: identity.deviceId, memberId: pending.events.at(-1)?.value, name: pending.subjectMemberName || `Companion #${pending.events.at(-1)?.value}` } : null;
  try {
    const result = await api('/api/save-sync', {
      method: 'POST', authenticated: true,
      // Preserve an old request's missing/version-1 marker. The service will
      // require reconciliation, never reinterpret it under current game rules.
      body: { rulesVersion: pending.rulesVersion, baseRevision: pending.baseRevision, batchId: pending.batchId, events: pending.events },
    });
    const receiptIsOlder = result.revision < revision;
    if (!receiptIsOlder) acceptSave(result);
    clearPending(pending.batchId);
    pending = null;
    acknowledged = true;
    conflict = false;
    // Idempotent receipts may describe an earlier revision. Never roll the UI back.
    if (receiptIsOlder) acceptSave(await api('/api/save', { authenticated: true }));
    if (actionType === 'select' && game.collection.some(member => member.id === selectedMemberId)) {
      companionDetailId = selectedMemberId;
      companionPage = Math.floor(game.collection.findIndex(member => member.id === selectedMemberId) / COMPANIONS_PER_PAGE);
      devicePendingReturn = 'companion';
      partnerConfirmation = { deviceId: identity.deviceId, memberId: selectedMemberId };
    }
    showNotice('');
    // A replayed receipt confirms an earlier save; don't replay its battle
    // animation over a newer authoritative state or invent damage deltas.
    if (actionType === 'resolve-test-encounter') {
      presentation = null; sceneState = null;
      navigation.setScreen(game.phase === 'encounter' ? game.battleMode === 'auto' ? 'wild-auto-confirm' : 'battle' : 'home');
      addNote('Earlier test encounter cleared.', 'Companions, progress and battle costs unchanged');
    } else if (recovery) {
      recoveryDraft = null; devicePendingReturn = 'care';
      if (result.revision > beforeRevision && !receiptIsOlder) await presentResult('rest', before, game);
      presentation = null; navigation.setScreen('care');
      addNote(receiptIsOlder ? 'Earlier recovery confirmed.' : 'Recovery saved.', `Latest companion health ${game.hp} / ${game.combat.maxHp} · energy ${game.energy}`);
    } else if (actionType === 'release') {
      releaseConfirmation = releasing; releaseDraft = null; presentation = null;
      companionPage = Math.min(companionPage, Math.max(0, Math.ceil(game.collection.length / COMPANIONS_PER_PAGE) - 1));
      devicePendingReturn = 'release-result'; navigation.setScreen('release-result');
      addNote('Release saved.', 'Your partner and discovered forms are kept');
    } else if (actionType === 'evolve') {
      progressionMemberId = evolvingMemberId;
      evolutionConfirmation = { deviceId: identity.deviceId, memberId: evolvingMemberId, formId: evolvingFormId };
      devicePendingReturn = 'evolution-result'; evolutionDraft = null;
      if (result.revision > beforeRevision && !receiptIsOlder) await presentResult('evolve', before, game);
      presentation = null; navigation.setScreen('evolution-result');
      addNote('Digivolution confirmed.', 'The same companion and its saved XP are kept');
    } else if (actionType === 'auto') {
      addNote(autoResultTitle(wildAutoTrace), 'Saved once · playback never changes the result');
      await playAutoBattle('wild', { animate: result.revision > beforeRevision && !receiptIsOlder });
    } else if (actionType === 'mode') {
      presentation = null; navigation.setScreen('explore');
      addNote(`${game.battleMode === 'auto' ? 'Auto' : 'Tactical'} mode saved.`, 'Locked when the next encounter begins');
    } else if (result.revision > beforeRevision && !receiptIsOlder) await presentResult(actionType, before, game);
    else addNote('Saved action confirmed.', `Latest save revision ${revision} kept`);
  } catch (error) {
    if (acknowledged) {
      showNotice(`Your action was confirmed saved. ${errorText(error)} Reconnect to refresh the latest state.`, 'service');
    } else if (error.status === 409) {
      conflict = true;
      try {
        acceptSave(await api('/api/save', { authenticated: true }));
        if (error.code === 'partner_locked') {
          await loadPractice();
          showNotice(actionType === 'evolve' ? 'Finish or retreat from practice before Digivolving. This form change was not applied; review and remove the local request to continue.' : actionType === 'release' ? 'Finish or retreat from practice before releasing a companion. This release was not applied; review and remove the local request to continue.' : 'Finish or retreat from the current practice battle before changing partner. This selection was not applied; review and remove the local request to continue.', 'conflict');
        } else showNotice(pending.rulesVersion === RULES_VERSION
          ? 'A newer save has been loaded. This local action was based on an older save and was not applied. Discard it to continue from the newer save.'
          : 'Your latest save is loaded. This earlier-rules request was not replayed; it may already be reflected in that save. Review your companions, then discard this pending request to continue.', 'conflict');
      } catch (refreshError) {
        showNotice(`A save conflict needs a fresh copy of your device state. ${errorText(refreshError)} Your action is still kept.`, 'retry');
      }
    } else if (error.status === 400 || error.status === 422) {
      conflict = true;
      showNotice(`The game rejected this action: ${errorText(error)} Discard it to continue.`, 'conflict');
    } else {
      showNotice(`${errorText(error)} Your action is kept in this browser. Retrying uses the same request ID so it cannot apply twice.`, 'retry');
    }
  } finally {
    // Flight is only a presentation of this request. Errors keep the durable
    // command, while equal/older receipts and hidden pages skip presentResult.
    // None of those paths may leave the old orb painted over the next screen.
    captureFlight = null;
    busy = false; render();
  }
}

async function discardConflict() {
  if (!conflict || busy || !ownsWriter || !storageAvailable) return;
  busy = true; render();
  try {
    // Fetch again so a second tab cannot make the displayed revision stale.
    acceptSave(await api('/api/save', { authenticated: true }));
    clearPending(pending.batchId);
    pending = null; conflict = false;
    if (isEggState(game)) await loadStarters();
    addNote('Loaded the latest save.', 'The conflicting local action was discarded');
    showNotice('');
  } catch (error) { showNotice(errorText(error), 'conflict'); }
  finally { busy = false; render(); }
}

function roundedRect(x, y, width, height, radius, color) {
  ctx.fillStyle = color;
  ctx.beginPath(); ctx.roundRect(x, y, width, height, radius); ctx.fill();
}

function text(value, x, y, size = 14, color = '#4e6344', weight = 'normal', align = 'center') {
  ctx.fillStyle = color;
  ctx.font = `${weight} ${size}px ui-monospace, Menlo, monospace`;
  ctx.textAlign = align; ctx.fillText(String(value), x, y);
}

function wrappedText(value, x, y, maxWidth, size = 13) {
  ctx.font = `${size}px ui-monospace, Menlo, monospace`;
  const words = String(value).split(/\s+/);
  let line = ''; const lines = [];
  for (const word of words) {
    const test = line ? `${line} ${word}` : word;
    if (line && ctx.measureText(test).width > maxWidth) { lines.push(line); line = word; }
    else line = test;
  }
  if (line) lines.push(line);
  lines.slice(0, 2).forEach((item, index) => text(item, x, y + index * (size + 6), size));
}

function sprite(id, x, y, scale, time, role = 'player') {
  const art = findArt(id);
  const spec = art?.sprites[resolvedArtKey(id, art)];
  if (!spec) {
    if (game?.stage === 'Rookie' && id === playerId() || starters.some(starter => starter.species === id)) {
      const scene = visualScene();
      paintRookiePlaceholder(ctx, x, y - 12, 112, (role === 'wild' ? scene?.wildCombat : scene?.combat)?.type); return;
    }
    if (role === 'player' && visualScene()?.stage || game?.collection.some(member => member.name.toLowerCase() === id && member.stage)) {
      paintFormPlaceholder(ctx, x, y - 12, 112); return;
    }
    roundedRect(x - 48, y - 44, 96, 91, 16, '#fff6df');
    ctx.strokeStyle = '#0e4145'; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(x, y - 7, 23, 0, Math.PI * 2); ctx.stroke();
    text('?', x, y + 2, 26, '#0e4145');
    text('ARTWORK PENDING', x, y + 35, 9, '#0e4145');
    return;
  }
  if (art.formatVersion === 1) {
    const prepared = role === 'wild' ? wildFrames : playerFrames;
    if (!prepared?.frames.length) return;
    const frame = prepared.frames[reducedMotion ? 0 : Math.floor(time / prepared.frameMs) % prepared.frames.length];
    const pixelScale = Math.max(1, Math.round(16 * scale / frame.width));
    ctx.drawImage(frame, Math.round(x - frame.width * pixelScale / 2), Math.round(y - frame.height * pixelScale / 2), frame.width * pixelScale, frame.height * pixelScale);
    return;
  }
  const frameMs = Math.max(150, Math.min(2000, spec.frameMs || 480));
  const frame = spec.frames[reducedMotion ? 0 : Math.floor(time / frameMs) % spec.frames.length];
  const left = Math.round(x - spec.width * scale / 2);
  const top = Math.round(y - spec.height * scale / 2);
  frame.forEach((row, rowIndex) => [...row].forEach((pixel, colIndex) => {
    if (pixel === '.') return;
    ctx.fillStyle = art.palette[pixel];
    ctx.fillRect(left + colIndex * scale, top + rowIndex * scale, scale, scale);
  }));
}

function landscape(time) {
  ctx.fillStyle = '#eff2d9'; ctx.fillRect(0, 0, 480, 480);
  ctx.fillStyle = '#e4eac9';
  ctx.beginPath(); ctx.ellipse(100, 306, 175, 118, -.12, 0, Math.PI * 2); ctx.fill();
  ctx.fillStyle = '#dbe5c0';
  ctx.beginPath(); ctx.ellipse(390, 317, 175, 114, .13, 0, Math.PI * 2); ctx.fill();
  ctx.fillStyle = '#ccdcb2';
  ctx.beginPath(); ctx.moveTo(0, 298); ctx.quadraticCurveTo(250, 337, 480, 295); ctx.lineTo(480, 480); ctx.lineTo(0, 480); ctx.fill();
  ctx.fillStyle = '#d7e4bd';
  ctx.beginPath(); ctx.ellipse(255, 355, 228, 42, 0, 0, Math.PI * 2); ctx.fill();
  for (let i = 0; i < 8; i++) {
    const x = 42 + ((i * 59) % 400);
    const y = 168 + ((i * 37) % 135) + (reducedMotion ? 0 : Math.round(Math.sin(time / 1400 + i) * 3));
    ctx.fillStyle = i % 2 ? '#b2c391' : '#c6d19c';
    ctx.fillRect(x, y, 3, 3);
    if (i % 3 === 0) { ctx.fillRect(x - 3, y + 3, 3, 3); ctx.fillRect(x, y + 6, 3, 3); ctx.fillRect(x + 3, y + 3, 3, 3); }
  }
  // Tiny, hand-drawn tufts and pebbles; these are presentation, not map/GPS data.
  for (const [x,y] of [[65,287],[382,302],[117,327],[341,340]]) {
    ctx.fillStyle = '#9cb582';ctx.fillRect(x, y, 3, 7);ctx.fillRect(x-4,y-2,3,6);ctx.fillRect(x+4,y-4,3,8);
  }
}

function scheduleDraw() {
  if (!animationFrame && !document.hidden) animationFrame = requestAnimationFrame(time => { animationFrame = 0; draw(time); });
}

function draw(time) {
  if (document.hidden) return;
  if (time - lastFrame < 80) { scheduleDraw(); return; }
  lastFrame = time;
  ctx.imageSmoothingEnabled = true;
  if (!backgroundPlayer.draw(ctx)) landscape(time);
  ctx.imageSmoothingEnabled = false;
  if (captureAim || captureFlight) { drawCapture(time); scheduleDraw(); return; }
  // Utility panels and choice cards own their portraits. Never show an unrelated
  // home/wild actor behind a practice menu; results use visualScene's battle data.
  if ($('device-ui').dataset.scene !== 'true') { scheduleDraw(); return; }
  if (transitionBusy && presentation?.stage === 'hatching' && !reducedMotion && time - presentation.started < 520) {
    const elapsed = Math.max(0, time - presentation.started);
    ctx.save(); ctx.translate(240, 238); ctx.rotate(Math.sin(elapsed / 42) * .07);
    ctx.drawImage(hatchEgg, -70, -70, 140, 140);
    if (elapsed > 220) { ctx.strokeStyle = '#234a4d'; ctx.lineWidth = 3; ctx.beginPath(); ctx.moveTo(-12, -30); ctx.lineTo(7, -13); ctx.lineTo(-7, 3); ctx.lineTo(13, 20); ctx.stroke(); }
    ctx.restore(); scheduleDraw(); return;
  }
  const scene = visualScene();
  const encounter = scene?.phase === 'encounter';



  if (pack) {
    if (encounter) {
      const wildControls = navigation.state().screen === 'battle' && !transitionBusy;
      const lift = wildControls ? -30 : 0;
      ctx.fillStyle = '#073d3b55';ctx.beginPath();ctx.ellipse(wildControls ? 240 : 169,305 + lift,47,9,0,0,Math.PI*2);ctx.fill();
      ctx.beginPath();ctx.ellipse(310,277 + lift,34,7,0,0,Math.PI*2);ctx.fill();
      const lunge = !reducedMotion && presentation?.stage === 'attack' && transitionBusy ? 12 : 0;
      const recoil = !reducedMotion && presentation?.stage === 'retaliation' && transitionBusy ? -5 : 0;
      if (wildControls) sprite(wildId(scene), 240, 224, 8, time + 130, 'wild');
      else { sprite(playerId(scene), 164 + lunge + recoil, 257, 6, time); sprite(wildId(scene), 310, 231, 5, time + 130, 'wild'); }
    } else {
      ctx.fillStyle = '#073d3b55';ctx.beginPath();ctx.ellipse(239,306,54,10,0,0,Math.PI*2);ctx.fill();
      sprite(playerId(scene), 240, 230, 9, time);
    }
    if (effectFrames && time - effectStarted < 1200 && !reducedMotion) {
      // A queued animation-frame timestamp may precede a just-started effect.
      const frame = effectFrames.frames[Math.floor(Math.max(0, time - effectStarted) / effectFrames.frameMs) % effectFrames.frames.length];
      const centerX = encounter ? (effectTarget === 'player' ? 164 : 310) : 240;
      ctx.drawImage(frame, centerX - frame.width * 2, 202 - frame.height * 2, frame.width * 4, frame.height * 4);
    }


  } else {
    text('◌', 240, 251, 52, '#91a777');
    text('A LITTLE WORLD IS LOADING', 240, 345, 12, '#5c744b');
    wrappedText(online ? 'Fetching original sprites…' : 'Start the local service to begin.', 240, 371, 265, 12);
  }

  scheduleDraw();
}

function drawCapture(time) {
  const scale = 480 / 412;
  ctx.save(); ctx.scale(scale, scale);
  // Original turquoise signal orb and aiming ring; no franchise ball artwork.
  ctx.strokeStyle = '#247f76'; ctx.lineWidth = 3;
  ctx.setLineDash([5, 5]); ctx.beginPath(); ctx.arc(206, 120, 48, 0, Math.PI * 2); ctx.stroke(); ctx.setLineDash([]);
  ctx.restore();
  sprite(wildId(captureFlight?.scene || game), 206 * scale, 120 * scale, 4, time, 'wild');
  ctx.save(); ctx.scale(scale, scale);
  const t = captureFlight ? Math.min(1, Math.max(0, (time - captureFlight.started) / 460)) : 0;
  const point = captureFlight ? captureFlightPoint(captureFlight.value, t, captureFlight.start)
    : { x: (captureAim?.preview?.x ?? .5) * 412, y: (captureAim?.preview?.y ?? 300 / 412) * 412, radius: 21 };
  if (point) {
    ctx.save();
    if (!reducedMotion && presentation?.stage === 'capture-check') {
      ctx.translate(point.x, point.y); ctx.rotate(Math.sin(time / 65) * .18); ctx.translate(-point.x, -point.y);
    }
    const glow = ctx.createRadialGradient(point.x - point.radius * .3, point.y - point.radius * .35, 1, point.x, point.y, point.radius);
    glow.addColorStop(0, '#d5ffe3'); glow.addColorStop(.45, '#35b49c'); glow.addColorStop(1, '#0e5559');
    ctx.fillStyle = glow; ctx.strokeStyle = '#0e4145'; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(point.x, point.y, point.radius, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    ctx.strokeStyle = '#e9ffe1'; ctx.beginPath(); ctx.arc(point.x, point.y, point.radius * .72, -.2, 1.2); ctx.stroke();
    ctx.fillStyle = '#fff6df'; ctx.beginPath(); ctx.moveTo(point.x, point.y - 7); ctx.lineTo(point.x + 5, point.y); ctx.lineTo(point.x, point.y + 7); ctx.lineTo(point.x - 5, point.y); ctx.closePath(); ctx.fill();
    ctx.restore();
  }
  ctx.font = 'bold 13px "Trebuchet MS", Arial, sans-serif'; ctx.textAlign = 'center'; ctx.fillStyle = '#0e4145';
  if (captureAim) {
    ctx.fillText(`${3 - game.captureAttempts} throws left · Odds after throwing`, 206, 193);
    ctx.font = '13px "Trebuchet MS", Arial, sans-serif';
    ctx.fillText(captureAim.preview ? 'Release upward to throw' : 'Touch the ball · flick upward', 206, 219);
    ctx.fillText(captureAim.hint.startsWith('No throw') ? 'No throw sent. Try another flick.' : 'A gentle flick reaches the ring.', 206, 240);
  }
  ctx.restore();
}

captureGesture = createCaptureGesture($('screen-surface'), {
  ball: { x: .5, y: 300 / 412, radius: 36 / 412 },
  canArm: () => Boolean(captureAim && navigation.state().screen === 'capture-aim' && canAct() && captureChoice(game).available),
  getRevision: () => `${identity?.deviceId}:${revision}:${game?.wildFormId}:${game?.captureAttempts}`,
  onPreview: preview => { if (captureAim) { captureAim.preview = preview; scheduleDraw(); } },
  onCancel: () => { if (captureAim) { captureAim.preview = null; captureAim.hint = 'No throw sent. Try another flick.'; scheduleDraw(); } },
  onRelease: gesture => {
    if (!captureAim || captureAim.revision !== revision || !canAct() || !captureChoice(game).available) return;
    const value = packCaptureFlick(gesture); if (value === null) return;
    captureFlight = { value, started: performance.now(), start: { x: gesture.x * 412, y: gesture.y * 412 }, scene: structuredClone(game) };
    const thisFlight = captureFlight;
    setTimeout(() => { if (captureFlight === thisFlight) renderDevice(); }, 510);
    captureAim = null; audio.playCue('capture-throw');
    void sendAction('flick', value);
  },
});

twoButtonInput = setupTwoButtonInput({
  left: $('device-back-button'), right: $('device-confirm-button'), holdMs: 600,
  onNext: () => navigation.move(1), onBack: deviceBack, canBack: canDeviceBack,
  onConfirm: event => { const item = navigation.selected(); if (item) deviceAction(item.id, event); },
});
$('device-input-mode').addEventListener('change', event => {
  if (captureAim) { captureAim = null; captureGesture.cancel('mode-change'); navigation.setScreen('battle'); }
  deviceInputMode = event.target.value === 'buttons' ? 'buttons' : 'touch';
  document.body.dataset.deviceInput = deviceInputMode;
  const url = new URL(location.href); url.searchParams.set('controls', deviceInputMode);
  history.replaceState(null, '', url);
  twoButtonInput.cancel(); renderDevice();
});
document.addEventListener('keydown', event => {
  const inDevice = event.target.closest?.('#screen-surface, .device-hardware-buttons');
  if (!inDevice && event.target !== document.body) return;
  // Native buttons retain their own Enter action, especially Back. Arrow keys
  // move focus to the screen so the next Enter confirms the highlighted item.
  if (event.key === 'Enter' && event.target.closest?.('button')) { if (event.repeat) event.preventDefault(); return; }
  const consumed = handleDeviceKey(event, navigation, { confirm: item => deviceAction(item.id, event), back: deviceBack });
  if (consumed && event.key.startsWith('Arrow')) $('screen-surface').focus({ preventScroll: true });
});
$('device-ui').addEventListener('focusin', event => {
  const index = event.target.dataset?.deviceIndex;
  if (index !== undefined) navigation.select(Number(index));
});

$('start-pairing').addEventListener('click', startPairing);
$('claim-device').addEventListener('click', claimDevice);
$('retry-sync').addEventListener('click', retryPending);
$('discard-conflict').addEventListener('click', discardConflict);
$('reconnect').addEventListener('click', reconnect);
$('swipe-card').addEventListener('click', () => requestAction('card', Number($('card-choice').value)));
$('cancel-capture').addEventListener('click', () => cancelCapture());
$('new-playtest').addEventListener('click', () => { if (canAct()) $('new-playtest-confirm').hidden = false; });
$('cancel-new-playtest').addEventListener('click', () => { $('new-playtest-confirm').hidden = true; });
$('confirm-new-playtest').addEventListener('click', newPlaytest);
$('load-playtest').addEventListener('click', loadPlaytest);
async function toggleSound(event) {
  const previous = audio.getState();
  const ready = await audio.unlock(event);
  if (ready) audio.setMuted(previous.unlocked ? !previous.muted : false);
  renderSound();
}
$('sound-toggle').addEventListener('click', toggleSound);
$('sound-volume').addEventListener('input', () => { audio.setVolume(Number($('sound-volume').value) / 100); renderSound(); });
$('sound-music').addEventListener('change', async event => {
  const enabled = $('sound-music').checked;
  // Persist the latest checkbox intent before an asynchronous context resume.
  // A subsequent uncheck must win even if this earlier unlock finishes later.
  audio.setMusicEnabled(enabled); renderSound();
  if (enabled) await audio.unlock(event);
  renderSound();
});
$('collection-grid').addEventListener('click', event => {
  const button = event.target.closest('[data-select-member]');
  if (button && !button.disabled) requestAction('select', Number(button.dataset.selectMember));
});
$('roster-search-form').addEventListener('submit', event => {
  event.preventDefault(); rosterQuery = { offset: 0, q: $('roster-search').value.trim().slice(0, 64), prefix: '', stage: '' };
  navigation.go('roster'); void loadRoster();
});
$('roster-results').addEventListener('click', event => {
  const button = event.target.closest('[data-roster-form]'); if (button) showRosterForm(Number(button.dataset.rosterForm));
});
for (const button of document.querySelectorAll('[data-event]')) {
  button.addEventListener('click', () => requestAction(button.dataset.event, Number(button.dataset.value || 0)));
}
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    cancelAnimationFrame(animationFrame); animationFrame = 0;
    cancelCapture('Capture cancelled because the playtest was hidden.');
    stageWait?.();
  } else { lastFrame = 0; scheduleDraw(); }
  render();
});

try { localStorage.setItem('digivice.storage-probe', '1'); localStorage.removeItem('digivice.storage-probe'); }
catch { storageAvailable = false; }
refreshStored();
readPlaytests();
render();
scheduleDraw();
const initialReconnect = reconnect();
initialReconnect.then(() => {
  if (!storageAvailable) showNotice('Browser storage is blocked or contains unreadable setup or pending action data. Existing data was kept. Restore site storage or use a fresh local browser profile.');
});

initialReconnect.then(() => setupAssets((candidate, availablePacks = {}) => {
  pack = candidate || builtinPack;
  originalPacks = { ...availablePacks, [pack.packId]: pack };
  backgroundPlayer.setPacks(originalPacks);
  artGeneration += 1;
  prepareCompanion(); render();
}, state => { assetState = state; if (deviceReady) renderDevice(); }, { includeTestFixtures: testFixturesSupported })).then(controller => { assetLibrary = controller; assetState = controller.getState(); renderDevice(); });
setupPersonalArt(candidate => {
  personalPack = candidate;
  artGeneration += 1;
  prepareCompanion(); render();
}).then(personal => {
  if (personal?.importTexts) setupGarageLibrary({ getCredential: () => identity, importPersonal: personal.importTexts });
});

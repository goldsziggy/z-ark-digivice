// Read-only battle presentation. The native command has already committed;
// neither frame construction nor playback executes a command or changes a save.
const ATTACKS = new Map([['attack', 'physical'], ['heavy', 'heavy'], ['magic', 'magic']]);
const MOVES = new Set(['physical', 'heavy', 'magic', 'counter']);
const hp = value => Number.isInteger(value) && value >= 0;
const loss = (before, after) => after === null ? null : Math.max(0, before - after);

function actorFrames(scene, exchange) {
  const {turn, action, opponentAction, reflected, guard} = exchange;
  const common = {turn, reflected: !!reflected, guard: guard || null, scene};
  const frames = [{
    ...common, actor: 'player', target: 'enemy', move: action,
    skill: scene.combat?.skills?.[action] || action,
    damage: loss(exchange.enemyHpBefore, exchange.enemyHpAfter),
    playerHpBefore: exchange.playerHpBefore, playerHpAfter: exchange.playerHpBefore,
    enemyHpBefore: exchange.enemyHpBefore, enemyHpAfter: exchange.enemyHpAfter,
  }];
  if (opponentAction) frames.push({
    ...common, actor: 'enemy', target: 'player', move: opponentAction,
    skill: opponentAction === 'counter' ? 'Counter' : scene.wildCombat?.skills?.[opponentAction] || opponentAction,
    damage: loss(exchange.playerHpBefore, exchange.playerHpAfter),
    playerHpBefore: exchange.playerHpBefore, playerHpAfter: exchange.playerHpAfter,
    enemyHpBefore: exchange.enemyHpAfter, enemyHpAfter: exchange.enemyHpAfter,
  });
  return frames;
}

/**
 * Expand each committed native exchange into its player action and, when
 * recorded, the opponent response. Keep the pre-command identities/profiles:
 * terminal saves clear the foe and can grow/recover the partner's saved HP.
 * Tactical defeat does not expose the surviving foe's HP in this ABI. That
 * outgoing HP/damage is null (unknown), never a fabricated kill or miss.
 */
export function buildBattleFrames(before, result, command) {
  const after = result?.state;
  if (!result?.ok || before?.phase !== 'encounter' || !after
    || after.sequence !== before.sequence + 1 || before.activeCreatureId !== after.activeCreatureId) return [];
  // Copy, so later view-only decoration cannot alter a command snapshot.
  const scene = JSON.parse(JSON.stringify(before));
  if (command === 'auto-fight' || command === 'auto-resume') {
    const trace = result.trace;
    if (!trace || trace.startSequence !== before.sequence || trace.endSequence !== after.sequence
      || !Array.isArray(trace.steps) || !trace.steps.length || trace.steps.length > 48
      || trace.player?.formId !== before.formId || trace.enemy?.formId !== before.wildFormId) return [];
    let playerHp = before.hp, enemyHp = before.wildHp;
    const frames = [];
    for (const [index, step] of trace.steps.entries()) {
      if (step.turn !== index + 1 || step.phase !== 'attack' || !ATTACKS.has(step.action === 'physical' ? 'attack' : step.action)
        || !(step.opponentAction === null || MOVES.has(step.opponentAction))
        || ![step.playerHpBefore,step.playerHpAfter,step.enemyHpBefore,step.enemyHpAfter].every(hp)
        || step.playerHpBefore !== playerHp || step.enemyHpBefore !== enemyHp
        || step.playerHpAfter > playerHp || step.enemyHpAfter > enemyHp
        || (!step.opponentAction && step.playerHpAfter !== playerHp)
        || (step.reflected && (step.action !== 'heavy' || step.opponentAction !== 'counter' || step.enemyHpAfter !== enemyHp))) return [];
      frames.push(...actorFrames(scene, {...step, turn: before.wildTurn + step.turn}));
      playerHp = step.playerHpAfter; enemyHp = step.enemyHpAfter;
    }
    return frames;
  }
  const action = ATTACKS.get(command);
  if (!action || before.battleMode !== 'tactical') return [];
  const terminal = after.phase === 'home';
  if (!terminal && (after.phase !== 'encounter' || after.wildFormId !== before.wildFormId
    || after.encounters !== before.encounters || after.wildTurn !== before.wildTurn + 1)) return [];
  const retreat = terminal && after.message === 'A gentle retreat. Rest whenever you are ready.';
  const won = terminal && ['A friendly battle won.', 'Your companion gained a level from battle experience!'].includes(after.message);
  if (terminal && !retreat && !won) return [];
  const reflected = action === 'heavy' && before.wildGuard === 'counter';
  const playerHpAfter = terminal ? retreat ? 0 : before.hp : after.hp;
  const enemyHpAfter = won ? 0 : retreat ? reflected ? before.wildHp : null : after.wildHp;
  if (![before.hp,before.wildHp,playerHpAfter].every(hp) || (enemyHpAfter !== null && !hp(enemyHpAfter))
    || playerHpAfter > before.hp || enemyHpAfter > before.wildHp) return [];
  return actorFrames(scene, {
    turn: before.wildTurn + 1, action, reflected, guard: before.wildGuard,
    opponentAction: won ? null : reflected ? 'counter' : before.wildTurn % 2 ? 'magic' : 'physical',
    playerHpBefore: before.hp, playerHpAfter, enemyHpBefore: before.wildHp, enemyHpAfter,
  });
}

export const BATTLE_ACTOR_MS = 1100;
export const BATTLE_IMPACT_PROGRESS = 0.38;

/** A bounded, disposable visual timeline with no wall-clock catch-up loop. */
export function createBattlePlayback({actorMs = BATTLE_ACTOR_MS} = {}) {
  if (!Number.isFinite(actorMs) || actorMs <= 0) throw new RangeError('Invalid battle actor duration');
  let frames = [], metadata, index = 0, entered = 0, running = false, completed = false, lastTime = 0, impactShown = false;
  return {
    start(nextFrames, nextMetadata, time) {
      if (running || !Array.isArray(nextFrames) || !nextFrames.length || !Number.isFinite(time)) return false;
      frames = nextFrames.slice(); metadata = nextMetadata; index = 0;
      entered = lastTime = time; running = true; completed = false; impactShown = false; return true;
    },
    sample(time) {
      if (!running) return null;
      const now = Number.isFinite(time) ? Math.max(lastTime, time) : lastTime;
      lastTime = now;
      // A delayed animation frame can advance one actor at most. Rebase the
      // next actor to now, preserving its full readable action on slow devices.
      if (!completed && now - entered >= actorMs) {
        if (!impactShown) entered = now - actorMs * BATTLE_IMPACT_PROGRESS;
        else if (index < frames.length - 1) { index++; entered = now; impactShown = false; }
        else completed = true;
      }
      const frame = frames[index];
      const progress = completed ? 1 : Math.min(1, Math.max(0, (now - entered) / actorMs));
      const phase = completed ? 'done' : progress < 0.20 ? 'windup' : progress < BATTLE_IMPACT_PROGRESS ? 'lunge'
        : progress < 0.62 ? 'impact' : progress < 0.84 ? 'recover' : 'hold';
      const impacted = progress >= BATTLE_IMPACT_PROGRESS;
      if (impacted) impactShown = true;
      return {...frame, index, total: frames.length, progress, phase, impacted, complete: completed,
        playerHp: impacted ? frame.playerHpAfter : frame.playerHpBefore,
        enemyHp: impacted ? frame.enemyHpAfter : frame.enemyHpBefore};
    },
    active() { return running; },
    // Also used to cancel on navigation/hidden/reload. Returns the commit's
    // metadata exactly once; callers decide whether to show its final notice.
    finish() {
      if (!running) return null;
      const result = metadata;
      frames = []; metadata = undefined; running = false; completed = false; index = 0;
      return result;
    },
  };
}

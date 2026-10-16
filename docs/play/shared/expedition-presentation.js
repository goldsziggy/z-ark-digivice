// Presentation reads saved expedition context; no selection, RNG or reward rules.
export function expeditionView(state) {
  const run = state?.expeditions;
  if (!run?.active || !Number.isInteger(run.scenarioIndex)) return null;
  const scenario = run.scenarios?.[run.scenarioIndex];
  if (!scenario) return null;
  return { ...scenario, tier: run.tier, tiers: run.tiers, awaitingNext: run.awaitingNext,
    boss: scenario.kind === 'boss' || run.tier === run.tiers,
    label: scenario.kind === 'boss' ? 'BOSS' : `TIER ${run.tier} / ${run.tiers}` };
}

// A quiet, pixel-stepped halo fits behind the actor's allocated rectangle. The
// static reduced-motion variant conveys exactly the same boss distinction.
export function paintBossAura(ctx, x, y, side, time = 0, reducedMotion = false) {
  const pulse = reducedMotion ? 1 : .9 + .1 * Math.sin(time / 420);
  ctx.save(); ctx.globalAlpha *= .65 * pulse;
  ctx.strokeStyle = '#f5c368'; ctx.lineWidth = 3;
  const half = side / 2 - 2, cut = side * .20;
  ctx.beginPath();ctx.moveTo(x-half+cut,y-half);ctx.lineTo(x+half-cut,y-half);
  ctx.lineTo(x+half,y-half+cut);ctx.lineTo(x+half,y+half-cut);
  ctx.lineTo(x+half-cut,y+half);ctx.lineTo(x-half+cut,y+half);
  ctx.lineTo(x-half,y+half-cut);ctx.lineTo(x-half,y-half+cut);ctx.closePath();ctx.stroke();
  ctx.fillStyle = '#ffe8a0';
  for (const [dx,dy] of [[-1,-1],[1,-1],[-1,1],[1,1]]) ctx.fillRect(x+dx*(half-9)-2,y+dy*(half-9)-2,4,4);
  ctx.restore();
}

import assert from 'node:assert/strict';

// A settled capture checkpoint has no selected confirm action: the ball needs
// a real gesture. Keep command waiters from mistaking that for a blocked save.
export const gameReady = page => page.waitForFunction(() => {
  const root = document.querySelector('#device-ui');
  return localStorage.getItem('digivice.dev.pending.v1') === null
    && !['feedback', 'capture-flight'].includes(root?.dataset.layout)
    && (!document.querySelector('#device-confirm-button').disabled
      || root?.dataset.screen === 'capture-aim' && !root.querySelector('#device-back').disabled);
});

export const autoCheckpoint = page => page.waitForFunction(() => {
  const root = document.querySelector('#device-ui');
  return ['capture-aim', 'wild-auto-result'].includes(root?.dataset.screen)
    && !['feedback', 'capture-flight'].includes(root?.dataset.layout);
});

export async function resumeFighting(page, back) {
  assert.equal(await page.locator('#device-ui').getAttribute('data-screen'), 'capture-aim');
  assert.equal(await page.locator('#device-back').textContent(), 'Resume fighting');
  const response = page.waitForResponse(r => r.url().endsWith('/api/save-sync') && r.request().method() === 'POST' && r.status() === 200);
  await back();
  const result = await (await response).json();
  assert.deepEqual((await response).request().postDataJSON().events, [{ type: 'auto-resume', value: 0 }]);
  assert.equal(result.state.phase, 'home'); assert.equal(result.state.autoCapture, 0);
  assert.ok(result.autoTrace.steps.every(step => step.action !== 'capture'));
  await page.waitForFunction(() => document.querySelector('#device-ui')?.dataset.screen === 'wild-auto-result');
  return result;
}

// Trusted CDP touch events, actual elapsed time, no injected handler/timestamp.
export async function flickBall(page, cdp) {
  const box = await page.locator('#screen-surface').boundingBox(); assert.ok(box);
  const touch = (type, points = []) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map(([x, y]) => ({ x: box.x + x * box.width / 412, y: box.y + y * box.height / 412, id: 1, radiusX: 3, radiusY: 3, force: 1 })) });
  await touch('touchStart', [[206, 300]]); const began = performance.now();
  for (let i = 0; i < 3; i++) {
    await page.waitForTimeout(20); const elapsed = performance.now() - began;
    assert.ok(elapsed < 200, 'trusted touchscreen dispatch stays inside the round display');
    await touch('touchMove', [[206, 300 - elapsed * 1.2]]);
  }
  await touch('touchEnd');
}

// Normal walking may need two 100-step inputs after the opening encounter.
// Exercise each real UI input and keep its exact command; do not seed a battle.
export async function exploreEncounter(command) {
  let saved;
  for (let inputs = 1; inputs <= 3; inputs++) {
    saved = await command('walk');
    if (saved.state.phase === 'encounter') return { saved, inputs };
    assert.equal(saved.state.phase, 'home');
  }
  throw new Error('Three explicit 100-step inputs failed to reach a Normal encounter.');
}

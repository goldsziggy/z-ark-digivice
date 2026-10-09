import assert from 'node:assert/strict';

// Only actual touches on the round screen change navigation. DOM inspection is
// used for assertions; it never clicks hidden controls or changes game state.
export function touchDevice(page) {
  const screen = page.locator('#device-ui');
  const control = id => screen.locator(`[data-device-action="${id}"]`).first();
  const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
  async function reveal(id) {
    await control(id).waitFor({ state: 'attached' });
    for (let tries = 0; tries < 33; tries++) {
      if (await control(id).isVisible()) return control(id);
      await screen.locator('#device-next').tap();
    }
    throw new Error(`Touch cannot reveal ${id} on ${await screen.getAttribute('data-screen')}`);
  }
  async function tap(id, expected) {
    await (await reveal(id)).tap();
    if (expected) await onScreen(expected);
  }
  async function back(expected) {
    await screen.locator('#device-back').tap();
    if (expected) await onScreen(expected);
  }
  async function home() {
    for (let count = 0; count < 16 && await screen.getAttribute('data-screen') !== 'home'; count++) {
      const current = await screen.getAttribute('data-screen');
      if (current === 'wild-auto-result') await tap('wild-auto-done');
      else if (current === 'evolution-result') await tap('evolution-home');
      else await back();
    }
    await onScreen('home');
  }
  async function menu(id) { await home(); await tap('menu', 'menu'); await tap(id, id); }
  async function geometry(label, { expectedWidth = 412 } = {}) {
    const observed = await screen.evaluate(root => {
      const surface = document.querySelector('#screen-surface').getBoundingClientRect();
      const cx = surface.x + surface.width / 2, cy = surface.y + surface.height / 2, radius = surface.width / 2;
      const buttons = [...root.querySelectorAll('button')].filter(button => button.getClientRects().length && getComputedStyle(button).visibility !== 'hidden');
      const records = buttons.map(button => {
        const box = button.getBoundingClientRect();
        const hit = document.elementFromPoint(box.x + box.width / 2, box.y + box.height / 2);
        return { id: button.id || button.dataset.deviceAction, label: button.textContent.trim(), disabled: button.disabled,
          box: { x: box.x, y: box.y, left: box.left, right: box.right, top: box.top, bottom: box.bottom, width: box.width, height: box.height }, hit: hit === button || button.contains(hit) };
      });
      const failures = [];
      for (const { id, box, hit } of records) {
        if (box.width < 43.9 || box.height < 43.9) failures.push(`small target ${id}: ${box.width.toFixed(1)} × ${box.height.toFixed(1)}`);
        if ([[box.left, box.top], [box.right, box.top], [box.left, box.bottom], [box.right, box.bottom]].some(([x, y]) => Math.hypot(x - cx, y - cy) > radius + 2)) failures.push(`outside round display: ${id}`);
        if (!hit) failures.push(`covered touch centre: ${id}`);
      }
      for (let a = 0; a < records.length; a++) for (let b = a + 1; b < records.length; b++) {
        const x = records[a].box, y = records[b].box;
        if (Math.min(x.right, y.right) - Math.max(x.left, y.left) > 1 && Math.min(x.bottom, y.bottom) - Math.max(x.top, y.top) > 1) failures.push(`overlap ${records[a].id} / ${records[b].id}`);
      }
      return { screen: root.dataset.screen, width: surface.width, height: surface.height, buttons: records, failures };
    });
    if (expectedWidth !== null) assert.ok(Math.abs(observed.width - expectedWidth) < 0.5 && Math.abs(observed.height - expectedWidth) < 0.5, `${label}: actual surface must be ${expectedWidth} × ${expectedWidth} CSS pixels`);
    else assert.ok(observed.width > 0 && observed.width <= 412 && Math.abs(observed.height - observed.width) < 0.5, `${label}: scaled round display`);
    assert.deepEqual(observed.failures, [], label);
    return { label, ...observed };
  }
  return { screen, control, onScreen, reveal, tap, back, home, menu, geometry };
}

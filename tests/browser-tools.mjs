// Existing detailed-dashboard tests opt into the now-collapsed developer tools.
// Device navigation is exercised separately without opening this panel.
export async function openPlaytestTools(page) {
  await page.locator('#playtest-tools').evaluate(details => { details.open = true; });
}

// A newly paired identity now starts as an egg. Exercise the real two-button
// flow before older gameplay smoke scenarios, which use relative revisions.
export async function hatchFirstEgg(page) {
  await page.locator('#device-input-mode').selectOption('buttons');
  const onScreen = name => page.waitForFunction(name => document.querySelector('#device-ui')?.dataset.screen === name, name);
  const confirm = page.locator('#device-confirm-button');
  await onScreen('starter-select');
  await page.waitForFunction(() => !document.querySelector('#device-confirm-button').disabled && document.querySelector('[data-device-action="starter-1"]'));
  await confirm.click(); await onScreen('starter-review');
  await confirm.click(); await onScreen('starter-hatched');
  await confirm.click(); await onScreen('home');
}

// Actual pointer lifecycle: the left physical button taps Next and holds Back.
export async function holdDeviceBack(page) {
  const box = await page.locator('#device-back-button').boundingBox();
  if (!box) throw new Error('The left device button is not visible.');
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down(); await page.waitForTimeout(660); await page.mouse.up();
}

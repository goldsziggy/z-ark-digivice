import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { test } from 'node:test';
import { validateStarterCatalog, isEggState } from '../web/starter-onboarding.js';

const catalog = JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH ?? 'build/digivice-core', ['--starters'], { encoding: 'utf8', maxBuffer: 16384 }));
test('starter metadata comes from native catalog; copies protect imported stats', () => {
  const result = validateStarterCatalog(catalog);
  assert.equal(result.length, 8); assert.equal(result[0].name, 'Impmon');
  assert.deepEqual(result.map(entry => entry.id), [1, 2, 3, 4, 5, 6, 7, 8]);
  assert.notEqual(result[0].combat, catalog.starters[0].combat);
  assert.ok(Object.isFrozen(result[0].combat.skills));
});
test('malformed catalogs cannot expose a different hatch value or unbounded profile', () => {
  for (const mutate of [
    value => value.formatVersion++, value => value.rulesVersion++, value => value.starters.pop(),
    value => value.starters.push(value.starters[0]), value => value.starters[1].id = 1,
    value => value.starters[0].id = 0, value => value.starters[0].species = null,
    value => value.starters[0].species = '../mote', value => value.starters[1].species = value.starters[0].species,
    value => value.starters[0].stage = 'Champion', value => value.starters[0].name = 'x'.repeat(65),
    value => value.starters[0].combat.attack = 1001, value => value.starters[0].combat.magic = -1,
    value => value.starters[0].combat.type = 'invented', value => value.starters[0].combat.skills.magic = '',
  ]) {
    const candidate = structuredClone(catalog); mutate(candidate);
    assert.throws(() => validateStarterCatalog(candidate), /unavailable or unsupported/);
  }
});
test('only explicit pending onboarding is an egg; sequence zero never implies a new user', () => {
  assert.equal(isEggState(null), false);
  assert.equal(isEggState({ sequence: 0, phase: 'home', onboarding: { completed: true, starterId: null } }), false);
  assert.equal(isEggState({ phase: 'egg', onboarding: { completed: false, starterId: null } }), true);
  assert.equal(isEggState({ phase: 'egg', onboarding: { completed: true } }), false);
});

import test from 'node:test';
import assert from 'node:assert/strict';
import { MAX_RECOVERY_RESTS, validRecoveryCount, recoveryReview, reviewedRecoveryEvents, validEncounterRarity, rarityLabel } from '../web/care-actions.js';

const state = { phase: 'home', activeCreatureId: 7, sequence: 19, recoveryRestCount: 8 };
test('recovery submits exactly the native count of ordinary Rest events', () => {
  const review = recoveryReview(state, 'local-test', 3);
  const events = reviewedRecoveryEvents(review, state, 'local-test', 3);
  assert.deepEqual(events, Array.from({ length: 8 }, () => ({ type: 'rest', value: 0 })));
  assert.notEqual(events[0], events[1]);
  assert.equal(state.sequence, 19);
});
test('review is bound to device, member, sequence, revision and native count', () => {
  const review = recoveryReview(state, 'local-test', 3);
  for (const change of [{ activeCreatureId: 8 }, { sequence: 20 }, { recoveryRestCount: 9 }, { phase: 'encounter' }])
    assert.equal(reviewedRecoveryEvents(review, { ...state, ...change }, 'local-test', 3), null);
  assert.equal(reviewedRecoveryEvents(review, state, 'another-device', 3), null);
  assert.equal(reviewedRecoveryEvents(review, state, 'local-test', 4), null);
});
test('full, blocked and malformed counts never produce care actions', () => {
  for (const count of [0, -1, 41, 1.5, null, undefined, '8', Infinity])
    assert.equal(recoveryReview({ ...state, recoveryRestCount: count }, 'local-test', 3), null);
  assert.equal(recoveryReview({ ...state, phase: 'egg' }, 'local-test', 3), null);
  assert.equal(validRecoveryCount(0), true);
  assert.equal(validRecoveryCount(MAX_RECOVERY_RESTS), true);
  assert.equal(reviewedRecoveryEvents(recoveryReview({ ...state, recoveryRestCount: 40 }, 'local-test', 3), { ...state, recoveryRestCount: 40 }, 'local-test', 3).length, 40);
});
test('rarity is a native authored category, never a computed chance', () => {
  assert.equal(rarityLabel('rare'), 'Rare');
  assert.equal(rarityLabel(null), 'Earlier encounter');
  for (const value of [null, 'common', 'uncommon', 'rare']) assert.equal(validEncounterRarity(value), true);
  for (const value of [undefined, 'legendary', .01, {}]) assert.equal(validEncounterRarity(value), false);
});

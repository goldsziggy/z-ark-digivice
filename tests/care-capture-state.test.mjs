import test from 'node:test';
import assert from 'node:assert/strict';
import { validCare, validLastCapture, captureReport, validTraceCapture, validTraceCare } from '../web/care-capture-state.js';
import { captureChoice } from '../web/auto-battle.js';

test('capture presentation uses only committed odds, with no misleading miss percentage', () => {
  for (const chance of [10, 25, 30, 45, 50, 89, 90]) {
    const record = { sequence: 14, targetFormId: 80, targetLevel: 2, chance, attempt: 2, result: 'escaped' };
    assert.ok(validLastCapture(record, 14)); assert.equal(captureReport(record).odds, `${chance}% throw chance`);
    assert.equal(captureReport(record).remaining, 1);
  }
  const miss = { sequence: 14, targetFormId: 80, targetLevel: 2, chance: 0, attempt: 3, result: 'miss' };
  assert.equal(captureReport(miss).odds, 'Miss · no catch'); assert.equal(captureReport(miss).remaining, 0);
  for (const change of [{ sequence: 15 }, { chance: 50 }, { attempt: 4 }, { result: 'maybe' }, { targetLevel: 0 }, { privateRoll: 0 }]) assert.equal(validLastCapture({ ...miss, ...change }, 14), false);
  assert.equal(captureReport({ sequence: 0, targetFormId: 0, targetLevel: 0, chance: 0, attempt: 0, result: 'none' }), null);
});

test('capture preparation exposes remaining throws without leaking prethrow percentages', () => {
  for (let attempt = 0; attempt < 3; attempt++) {
    const view = captureChoice({ phase: 'encounter', captureAttempts: attempt, wildCaptureChance: 50 });
    assert.equal(view.available, true); assert.equal(view.label, `Capture · ${3 - attempt} left`); assert.doesNotMatch(view.detail, /\d+%/);
  }
});

test('trace modifiers and throw metadata reject extra fields and out-of-range values', () => {
  assert.ok(validTraceCare({ offenseBonus: 5, protectionBonus: 0 })); assert.equal(validTraceCare({ offenseBonus: 6, protectionBonus: 0 }), false);
  assert.ok(validTraceCapture({ chance: 50, attempt: 2, result: 'escaped' }));
  for (const value of [{ chance: 0, attempt: 1, result: 'captured' }, { chance: 100, attempt: 1, result: 'captured' }, { chance: 50, attempt: 4, result: 'escaped' }, { chance: 50, attempt: 1, result: 'captured', roll: 1 }]) assert.equal(validTraceCapture(value), false);
});

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { validParty, orderedMembers, partyChoice } from '../web/party.js';
const state = (partyMemberIds = []) => ({ phase: 'home', activeCreatureId: 1, partyCapacity: 3, partyMemberIds,
  collection: Array.from({ length: 60 }, (_, index) => ({ id: index + 1, formId: index + 11 })) });
test('compact party accepts zero through three distinct owned nonactive IDs only', () => {
  for (const ids of [[], [60], [60, 2], [60, 2, 50]]) assert.equal(validParty(state(ids)), true);
  for (const ids of [[1], [0], [-1], [2.1], [61], [2, 2], [2, 3, 4, 5], [2, 0, 0], null]) assert.equal(validParty(state(ids)), false);
  assert.equal(validParty({ ...state(), partyCapacity: 4 }), false);
  assert.equal(validParty({ ...state(), collection: [], activeCreatureId: 0 }), true);
});
test('derived box order pins partner, selection order, then newest local acquisition without mutating save', () => {
  const save = state([4, 60, 2]), original = structuredClone(save);
  const ids = orderedMembers(save).map(member => member.id);
  assert.deepEqual(ids.slice(0, 7), [1, 4, 60, 2, 59, 58, 57]);
  assert.equal(new Set(ids).size, 60); assert.deepEqual(save, original);
  // Forms/levels do not affect order, and gaps from release never imply age.
  save.collection[3].formId = 400;
  save.collection = save.collection.filter(member => member.id !== 59);
  save.activeCreatureId = 4; save.partyMemberIds = [60, 2];
  assert.deepEqual(orderedMembers(save).slice(0, 6).map(member => member.id), [4, 60, 2, 58, 57, 56]);
  save.collection.push({ id: 61, formId: 30 });
  assert.deepEqual(orderedMembers(save).slice(0, 5).map(member => member.id), [4, 60, 2, 61, 58]);
});
test('party controls expose full state, allow removal, and lock wild or active members', () => {
  const save = state([2, 3, 4]);
  assert.match(partyChoice(save, 5).reason, /All 3 XP slots/);
  assert.equal(partyChoice(save, 3).type, 'party-remove');
  assert.equal(partyChoice(save, 3).slot, 2); assert.equal(partyChoice(save, 3).reason, '');
  assert.match(partyChoice(save, 1).reason, /active partner/);
  assert.match(partyChoice(save, 61).reason, /no longer carried/);
  for (const id of [2, 5]) assert.match(partyChoice({ ...save, phase: 'encounter' }, id).reason, /Finish the wild/);
  assert.equal(partyChoice(state(), 60).type, 'party-add');
  assert.equal(partyChoice(state(), 60).reason, '');
});

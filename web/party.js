// Presentation and contract checks only. The native core awards all XP.
export const PARTY_CAPACITY = 3;
export function validParty(state) {
  const ids = state?.partyMemberIds;
  return state?.partyCapacity === PARTY_CAPACITY && Array.isArray(ids) && ids.length <= PARTY_CAPACITY
    && Array.isArray(state.collection) && new Set(ids).size === ids.length
    && ids.every(id => Number.isInteger(id) && id >= 1 && id <= 0xfffffffe
      && id !== state.activeCreatureId && state.collection.some(member => member.id === id));
}
export function orderedMembers(state) {
  const members = [...(state?.collection || [])];
  const pinned = [state?.activeCreatureId, ...(state?.partyMemberIds || [])];
  const rank = id => { const index = pinned.indexOf(id); return index < 0 ? pinned.length : index; };
  return members.sort((a, b) => rank(a.id) - rank(b.id) || b.id - a.id);
}
export function partyChoice(state, id) {
  const slot = (state?.partyMemberIds || []).indexOf(id) + 1;
  const count = state?.partyMemberIds?.length || 0;
  const reason = !state?.collection?.some(member => member.id === id) ? 'This Digimon is no longer carried.'
    : state.activeCreatureId === id ? 'Your active partner already earns battle XP.'
    : state.phase !== 'home' ? 'Finish the wild encounter before changing XP companions.'
    : !slot && count >= PARTY_CAPACITY ? 'All 3 XP slots are filled. Remove one companion first.' : '';
  return { slot, count, type: slot ? 'party-remove' : 'party-add', reason,
    label: slot ? 'Remove XP companion' : 'Add XP companion',
    detail: reason || (slot ? `XP companion ${slot} of ${count} · remove from party` : `${count} / 3 XP companions · free battle XP`) };
}

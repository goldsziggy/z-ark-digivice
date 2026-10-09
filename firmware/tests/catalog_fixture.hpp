#pragma once
#include "game.hpp"
#include "forms.hpp"

// Synthetic valid history with released IDs2..18 and a selected member19.
// Exercises stable identity independently of the bounded carried collection slots.
inline digivice::State stableMemberFixture(std::uint32_t formId) {
    using namespace digivice;
    auto state = newGame();
    const auto* form = forms::find(formId);
    if (!form) return state;
    state.sequence = 100; state.captures = state.encounters = 18;
    state.steps = 1800; state.nextMemberId = 20; state.collectionCount = 2; state.activeCreatureId = 19;
    auto& member = state.collection[1];
    member.id = 19; member.species = static_cast<Species>(form->lineage); member.formId = formId;
    member.level = form->minLevel; member.xp = xpForLevel(member.level); member.bond = form->minBond;
    member.hp = combat::formProfile(formId, member.level).stats.maxHp;
    member.energy = 80; member.fullness = 70; member.mood = 80; member.capturedAtSequence = 99;
    state.hp = member.hp; state.level = member.level; state.bond = member.bond;
    state.energy = member.energy; state.fullness = member.fullness; state.mood = member.mood;
    state.journal[(formId - 1) / 32] |= 1u << ((formId - 1) % 32);
    return state;
}

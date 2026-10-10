#include "evolution_choice.hpp"

namespace digivice::controls {
bool EvolutionChoice::propose(const State& state, std::uint32_t targetForm) {
    return propose(state, targetForm, 0);
}
bool EvolutionChoice::propose(const State& state, std::uint32_t targetForm, std::uint32_t memberId) {
    cancel();
    const auto* active = activeMember(state);
    const bool bench = memberId && (!active || memberId != active->id);
    const auto action = bench ? Action::EvolveMember : Action::Evolve;
    const auto owner = bench ? memberId : (active ? active->id : 0);
    const auto value = bench ? ((memberId << 16) | (targetForm & 0xffffu)) : targetForm;
    State probe = state;
    error_ = apply(probe, action, value);
    if (error_ != Error::None) return false;
    const auto* member = findMember(state, owner);
    if (!member) { cancel(); error_ = Error::InvalidState; return false; }
    target_ = targetForm;
    sequence_ = state.sequence;
    member_ = owner;
    form_ = member->formId;
    action_ = action;
    return true;
}
bool EvolutionChoice::confirm(const State& state, State& candidate) {
    const auto target = target_;
    const auto action = action_;
    const auto memberId = member_;
    const auto form = form_;
    const auto sequence = sequence_;
    cancel();
    const auto* member = findMember(state, memberId);
    if (!target || !member || !isValid(state) || sequence != state.sequence || form != member->formId ||
        (action == Action::Evolve && memberId != state.activeCreatureId)) {
        error_ = Error::EvolutionUnavailable;
        return false;
    }
    State probe = state;
    const auto value = action == Action::EvolveMember ? ((memberId << 16) | (target & 0xffffu)) : target;
    error_ = apply(probe, action, value);
    if (error_ != Error::None) return false;
    candidate = probe;
    return true;
}
} // namespace digivice::controls

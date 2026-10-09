#include "evolution_choice.hpp"

namespace digivice::controls {
bool EvolutionChoice::propose(const State& state, std::uint32_t targetForm) {
    cancel();
    State probe = state;
    error_ = apply(probe, Action::Evolve, targetForm);
    if (error_ != Error::None) return false;
    target_ = targetForm; sequence_ = state.sequence; member_ = state.activeCreatureId;
    const auto* member = activeMember(state);
    if (!member) { cancel(); error_ = Error::InvalidState; return false; }
    form_ = member->formId;
    return true;
}
bool EvolutionChoice::confirm(const State& state, State& candidate) {
    const auto target = target_;
    cancel();
    const auto* member = activeMember(state);
    if (!target || !member || !isValid(state) || sequence_ != state.sequence || member_ != state.activeCreatureId ||
        form_ != member->formId) {
        error_ = Error::EvolutionUnavailable;
        return false;
    }
    State probe = state;
    error_ = apply(probe, Action::Evolve, target);
    if (error_ != Error::None) return false;
    candidate = probe;
    return true;
}
} // namespace digivice::controls

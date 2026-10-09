#include "recovery_choice.hpp"
namespace digivice::controls {
bool RecoveryChoice::propose(const State& state) {
    cancel();
    const auto count=recoveryRestCount(state);
    const auto* member=activeMember(state);
    if(!count || count>40 || !member) return false;
    sequence_=state.sequence;member_=member->id;form_=member->formId;count_=count;
    return true;
}
bool RecoveryChoice::confirm(const State& state, State& candidate) {
    const auto count=count_;cancel();
    const auto* member=activeMember(state);
    if(!count || !member || member->id!=member_ || member->formId!=form_ ||
       state.sequence!=sequence_ || recoveryRestCount(state)!=count) return false;
    State next=state;
    for(std::uint32_t i=0;i<count;++i) if(apply(next,Action::Rest)!=Error::None)return false;
    candidate=next;return true;
}
}

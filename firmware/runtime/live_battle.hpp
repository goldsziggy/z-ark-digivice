#pragma once
#include "game.hpp"

namespace digivice::livebattle {
inline bool automatic(Action action) {
    return action==Action::Auto || action==Action::AutoFight || action==Action::AutoResume || action==Action::Focus;
}
// Device owner policy. Historical core Auto/Fight/Resume/Focus events remain
// unchanged for replay clients; live rules20+ resolves one committed exchange.
inline Error applyAction(State& state,Action action,std::uint32_t value,autobattle::Trace* trace) {
    if(automatic(action)) {
        if((action==Action::Focus && value>kFocusNoTap) || (action!=Action::Focus && value)) return Error::InvalidValue;
        if(state.wildRules>=20 && action!=Action::Auto)
            return applyAutoStep(state,action==Action::Focus?value+2:action==Action::AutoResume?1u:0u,trace);
        if(action==Action::Auto) return applyAuto(state,trace);
        if(action==Action::AutoFight) return applyAutoFight(state,trace);
        if(action==Action::AutoResume) return applyAutoResume(state,trace);
        return applyFocus(state,value,trace);
    }
    return apply(state,action,value);
}
} // namespace digivice::livebattle

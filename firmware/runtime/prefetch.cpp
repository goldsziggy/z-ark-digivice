#include "prefetch.hpp"
#include "forms.hpp"

#include <cstring>

namespace digivice::assets {
namespace {
void append(Plan& output, const char* id) {
    const auto length = std::strlen(id);
    if (output.count >= 4 || length >= sizeof(output.ids[0])) return;
    for (std::size_t i = 0; i < output.count; ++i) if (std::strcmp(output.ids[i], id) == 0) return;
    std::memcpy(output.ids[output.count++], id, length + 1);
}
void sprite(Plan& output, const char* displayName) {
    char id[48] = "sprite-";
    std::size_t used = 7;
    for (const char* next = displayName; *next; ++next) {
        if (used + 4 >= sizeof(id)) return;
        const char ch = *next;
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) return;
        id[used++] = ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
    std::memcpy(id + used, "-v1", 4);
    append(output, id);
}
}
Plan plan(const State& state, bool developmentTestAssets) {
    Plan output;
    if (!isValid(state) || !state.onboardingComplete) return output;
    // Native manifests currently contain only the original authored roster.
    // A missing form uses a neutral indicator until exact artwork is supplied;
    // do not enqueue an invented asset ID that would block the one-request pump.
    const auto* member = activeMember(state);
    if (!member) return output;
    const auto* form = forms::find(member->formId);
    if (form && form->artId && (developmentTestAssets || forms::productionForm(form->id))) sprite(output, form->artId);
    if (state.phase == Phase::Encounter) {
        const auto* wild = forms::find(state.wildFormId);
        if (wild && wild->artId && (developmentTestAssets || forms::productionForm(wild->id))) sprite(output, wild->artId);
    }
    // A saved pending foe is already selected, even if the partner changes.
    // Otherwise preview the next pool; this hint never advances gameplay.
    if (state.encounters < UINT32_MAX) {
        const auto next = state.pendingEncounter.formId ? state.pendingEncounter.formId :
            selectWildForm(state.encounters + 1, worldSelectionSeed(state), member->formId, member->level);
        const auto* wild = forms::find(next);
        if (wild && wild->artId && (developmentTestAssets || forms::productionForm(wild->id))) sprite(output, wild->artId);
    }
    append(output, "scene-forest-412-v1");
    return output;
}
} // namespace digivice::assets

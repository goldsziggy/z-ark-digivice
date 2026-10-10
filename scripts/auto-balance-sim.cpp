// Auto-battle balance probe: real generated encounters through the shipping
// AutoFight flow (attack-only chunk, capture skipped with AutoResume), so the
// numbers describe what a player who lets Auto play actually experiences.
// Usage: auto-balance-sim [encounters-per-cell] [focus]
//   focus=1 answers every rules-18 focus moment with a green tap,
//   focus=2 with a red tap, otherwise focus moments are skipped.
#include "game.hpp"
#include "forms.hpp"
#include "capture_ring.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace digivice;
namespace {
struct Cell { unsigned fights = 0, wins = 0, knockouts = 0, focus = 0; std::vector<unsigned> exchanges; double hpLeft = 0; };

unsigned tierFor(unsigned level) { return level < 18 ? 2 : level < 28 ? 3 : level < 40 ? 4 : 5; }

std::vector<unsigned> partnersFor(unsigned tier, unsigned level) {
    std::vector<unsigned> ids;
    for (unsigned id = forms::kFirstProductionFormId; id <= forms::kFormCount; ++id)
        if (static_cast<unsigned>(forms::combatTier(id)) == tier && forms::find(id)->minLevel <= level) ids.push_back(id);
    return ids;
}

// Adds the probe Digimon as a captured member 2 and makes it the partner.
bool become(State& s, unsigned formId, unsigned level) {
    const auto* f = forms::find(formId);
    s.sequence = s.foregroundSequence = 400; s.collectionCount = 2; s.captures = s.encounters = 1; s.steps = 100; s.nextMemberId = 3;
    auto& m = s.collection[1];
    m = {}; m.id = 2; m.capturedAtSequence = 1; m.fullness = 60;
    m.formId = formId; m.species = static_cast<Species>(f->lineage);
    m.level = level; m.xp = xpForLevel(level);
    m.bond = 150; m.mood = 80; m.fullness = 60; m.careState = 100;
    m.hp = combat::formProfile(formId, level).stats.maxHp; m.energy = 100;
    s.journal[(formId - 1) / 32] |= 1u << ((formId - 1) % 32);
    if (!isValid(s)) return false;
    return apply(s, Action::Select, 2) == Error::None;
}
void heal(State& s) {
    auto& m = *const_cast<CreatureMember*>(activeMember(s));
    m.hp = s.hp = combat::formProfile(m.formId, s.level).stats.maxHp;
    m.energy = s.energy = 100; m.careState &= (1u << 27) - 1u; // clear rules-17 injury/mistakes for a clean probe
    m.level = s.level; m.xp = xpForLevel(s.level);
}
std::uint32_t greenPhase(unsigned formId) { for (unsigned p = 0; p < capturering::kCycleMs; ++p) if (capturering::sample(p, formId).grade == capturering::Grade::Green) return p; return 0; }
std::uint32_t redPhase(unsigned formId) { for (unsigned p = 0; p < capturering::kCycleMs; ++p) if (capturering::sample(p, formId).grade == capturering::Grade::Red) return p; return 0; }
}

int main(int argc, char** argv) {
    const unsigned perCell = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1])) : 40;
    const int focusMode = argc > 2 ? std::atoi(argv[2]) : 0;
    const unsigned levels[] = {5, 10, 17, 18, 22, 27, 28, 34, 39, 40, 45, 50};
    std::printf("rules %u  focus=%s\n", kRulesVersion, focusMode == 1 ? "green" : focusMode == 2 ? "red" : "skip");
    std::printf("level tier | fights  win%%  KO%%  exch med/p90  HP left%%  focus/fight\n");
    unsigned totalWins = 0, totalFights = 0;
    for (const auto level : levels) {
        const auto tier = tierFor(level);
        const auto partners = partnersFor(tier, level);
        Cell cell;
        for (unsigned n = 0; n < perCell; ++n) {
            auto s = newDevice(1000u + level * 977u + n * 7919u);
            if (apply(s, Action::Hatch, 1) != Error::None) return 1;
            if (!become(s, partners[(n * 37u) % partners.size()], level)) { std::fprintf(stderr, "invalid partner setup\n"); return 1; }
            if (apply(s, Action::Mode, 1) != Error::None) return 1;
            for (unsigned warm = 0; warm < 1 + n % 5; ++warm) { // vary encounter index
                if (apply(s, Action::Explore, 1000) != Error::None) return 1;
                if (apply(s, Action::Retreat) != Error::None) return 1;
                heal(s);
            }
            if (apply(s, Action::Explore, 1000) != Error::None) return 1;
            unsigned exchanges = 0, focusCount = 0;
            for (unsigned guard = 0; guard < 12 && s.phase == Phase::Encounter; ++guard) {
                autobattle::Trace trace;
                Error e;
                if (s.autoCapture == AutoCapture::Awaiting) e = applyAutoResume(s, &trace);
#ifdef DIGIVICE_HAS_FOCUS
                else if (s.autoCapture == AutoCapture::FocusStrike || s.autoCapture == AutoCapture::FocusBlock) {
                    ++focusCount;
                    const auto phase = focusMode == 1 ? greenPhase(s.wildFormId) : focusMode == 2 ? redPhase(s.wildFormId) : kFocusNoTap;
                    e = applyFocus(s, phase, &trace);
                }
#endif
                else e = applyAutoFight(s, &trace);
                if (e != Error::None) { std::fprintf(stderr, "auto error %s level %u\n", errorText(e), level); return 1; }
                exchanges += trace.count;
            }
            ++cell.fights; cell.exchanges.push_back(exchanges); cell.focus += focusCount;
            const auto maximum = combat::formProfile(activeMember(s)->formId, s.level).stats.maxHp;
            if (s.lastAutoOutcome == autobattle::Outcome::Won || s.message == Message::Won || s.message == Message::Trained) { ++cell.wins; cell.hpLeft += 100.0 * s.hp / maximum; }
            else ++cell.knockouts;
        }
        std::sort(cell.exchanges.begin(), cell.exchanges.end());
        std::printf("L%-3u %u    | %5u  %5.1f %5.1f    %3u / %3u     %5.1f     %4.2f\n", level, tier, cell.fights,
            100.0 * cell.wins / cell.fights, 100.0 * cell.knockouts / cell.fights,
            cell.exchanges[cell.exchanges.size() / 2], cell.exchanges[cell.exchanges.size() * 9 / 10],
            cell.wins ? cell.hpLeft / cell.wins : 0.0, 1.0 * cell.focus / cell.fights);
        totalWins += cell.wins; totalFights += cell.fights;
    }
    std::printf("overall win %.1f%%\n", 100.0 * totalWins / totalFights);
    return 0;
}

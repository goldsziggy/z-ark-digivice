#include "battle_trace.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include "legacy_combat_v3.hpp"
#include "legacy_combat_v5.hpp"
#include "legacy_forms_v5.hpp"
#include "legacy_combat_v6.hpp"
#include "legacy_forms_v6.hpp"
#include "legacy_forms_v7.hpp"
#include "legacy_combat_v7.hpp"
#include "legacy_forms_v8.hpp"
#include "legacy_combat_v8.hpp"
#include "legacy_forms_v9.hpp"
#include "legacy_combat_v9.hpp"
#include <cstdarg>
#include <cstdio>

namespace digivice::autobattle {
const char* moveName(Move value) {
    constexpr const char* names[]{"none", "physical", "heavy", "magic", "brace", "counter", "ward", "capture"};
    const auto i = static_cast<unsigned>(value);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "invalid";
}
const char* outcomeName(Outcome value) {
    constexpr const char* names[]{"none", "won", "captured", "retreated", "lost", "draw"};
    const auto i = static_cast<unsigned>(value);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "invalid";
}
std::uint32_t nextRandom(std::uint32_t& state) {
    if (!state) state = 0x6d2b79f5u;
    auto x = state; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return state = x;
}
std::size_t writeJson(const Trace& t, char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0';
    namespace frozen = digivice::legacy_v3::combat;
    const auto valid = [&](std::uint32_t species, std::uint32_t formId, std::uint32_t level) {
        return t.combatRulesVersion == 3 ? (!formId && frozen::validProfile(species, level)) :
            t.combatRulesVersion == 9 ? legacy_v9::forms::validForLineage(formId, species) && legacy_v9::combat::validFormProfile(formId, level) : t.combatRulesVersion == 8 ? legacy_v8::forms::validForLineage(formId, species) && legacy_v8::combat::validFormProfile(formId, level) :
            t.combatRulesVersion == 7 ? legacy_v7::forms::validForLineage(formId, species) && legacy_v7::combat::validFormProfile(formId, level) :
            t.combatRulesVersion == 6 ? legacy_v6::forms::validForLineage(formId, species) && legacy_v6::combat::validFormProfile(formId, level) :
            t.combatRulesVersion == 5 ? legacy_v5::forms::validForLineage(formId, species) && legacy_v5::combat::validFormProfile(formId, level) :
            (t.combatRulesVersion == 4 || t.combatRulesVersion == 12) && forms::validForLineage(formId, species) && combat::validFormProfile(formId, level);
    };
    if (!t.count || t.count > kMaxTraceSteps || static_cast<unsigned>(t.kind) > 1 ||
        t.outcome == Outcome::None || static_cast<unsigned>(t.outcome) > 5 || t.endSequence <= t.startSequence ||
        !valid(t.playerSpecies, t.playerFormId, t.playerLevel) || !valid(t.enemySpecies, t.enemyFormId, t.enemyLevel))
        return 0;
    if (t.combatRulesVersion == 12 && (!combat::validCareBonus({t.playerOffenseBonus,t.playerProtectionBonus}) ||
        !combat::validCareBonus({t.enemyOffenseBonus,t.enemyProtectionBonus}))) return 0;
    std::size_t used = 0; bool ok = true;
    const auto append = [&](const char* format, ...) {
        if (!ok) return;
        va_list args; va_start(args, format);
        const int n = std::vsnprintf(output + used, capacity - used, format, args);
        va_end(args);
        if (n < 0 || static_cast<std::size_t>(n) >= capacity - used) { ok = false; return; }
        used += static_cast<std::size_t>(n);
    };
    append("{\"formatVersion\":1,\"mode\":\"auto\",\"kind\":\"%s\",\"startSequence\":%u,\"endSequence\":%u,\"outcome\":\"%s\"",
           t.kind == Kind::Wild ? "wild" : "practice", static_cast<unsigned>(t.startSequence),
           static_cast<unsigned>(t.endSequence), outcomeName(t.outcome));
    if(t.combatRulesVersion==12)
        append(",\"combatRulesVersion\":12,\"playerCare\":{\"offenseBonus\":%u,\"protectionBonus\":%u},\"enemyCare\":{\"offenseBonus\":%u,\"protectionBonus\":%u}",
               static_cast<unsigned>(t.playerOffenseBonus),static_cast<unsigned>(t.playerProtectionBonus),
               static_cast<unsigned>(t.enemyOffenseBonus),static_cast<unsigned>(t.enemyProtectionBonus));
    const auto participant = [&](const char* key, std::uint32_t species, std::uint32_t level, std::uint32_t formId) {
        char stats[combat::kProfileJsonCapacity];
        const auto count = t.combatRulesVersion == 3 ? frozen::writeProfileJson(species, level, stats, sizeof(stats)) :
            t.combatRulesVersion == 9 ? legacy_v9::combat::writeFormProfileJson(formId, level, stats, sizeof(stats)) : t.combatRulesVersion == 8 ? legacy_v8::combat::writeFormProfileJson(formId, level, stats, sizeof(stats)) :
            t.combatRulesVersion == 7 ? legacy_v7::combat::writeFormProfileJson(formId, level, stats, sizeof(stats)) :
            t.combatRulesVersion == 6 ? legacy_v6::combat::writeFormProfileJson(formId, level, stats, sizeof(stats)) :
            t.combatRulesVersion == 5 ? legacy_v5::combat::writeFormProfileJson(formId, level, stats, sizeof(stats)) :
            combat::writeFormProfileJson(formId, level, stats, sizeof(stats));
        if (!count) { ok = false; return; }
        const char* name = t.combatRulesVersion == 3 ? frozen::profile(species, level).name : t.combatRulesVersion == 9 ? legacy_v9::combat::formProfile(formId,level).name : t.combatRulesVersion == 8 ? legacy_v8::combat::formProfile(formId,level).name : t.combatRulesVersion == 7 ? legacy_v7::combat::formProfile(formId,level).name : t.combatRulesVersion == 6 ? legacy_v6::combat::formProfile(formId,level).name : t.combatRulesVersion == 5 ? legacy_v5::combat::formProfile(formId,level).name : combat::formProfile(formId, level).name;
        append(",\"%s\":{\"species\":\"%s\",\"name\":\"%s\",\"level\":%u,\"combat\":%s", key,
               (t.combatRulesVersion == 9 ? legacy_v9::combat::speciesName(species) : t.combatRulesVersion == 8 ? legacy_v8::combat::speciesName(species) : t.combatRulesVersion == 7 ? legacy_v7::combat::speciesName(species) : t.combatRulesVersion == 6 ? legacy_v6::combat::speciesName(species) : t.combatRulesVersion == 5 ? legacy_v5::combat::speciesName(species) : combat::speciesName(species)), name, static_cast<unsigned>(level), stats);
        if(t.includeFormIds) append(",\"formId\":%u",static_cast<unsigned>(formId));
        append("}");
    };
    participant("player", t.playerSpecies, t.playerLevel, t.playerFormId);
    participant("enemy", t.enemySpecies, t.enemyLevel, t.enemyFormId);
    append(",\"steps\":[");
    for (std::size_t i = 0; i < t.count; ++i) {
        const auto& s = t.steps[i];
        if (s.action == Move::None || static_cast<unsigned>(s.action) > 7 || static_cast<unsigned>(s.opponentAction) > 7) { ok = false; break; }
        append("%s{\"turn\":%u,\"phase\":\"%s\",\"action\":\"%s\",\"opponentAction\":", i ? "," : "",
               static_cast<unsigned>(i + 1), s.defending ? "defend" : "attack", moveName(s.action));
        if (s.opponentAction == Move::None) append("null");
        else append("\"%s\"", moveName(s.opponentAction));
        append(",\"playerHpBefore\":%u,\"playerHpAfter\":%u,\"enemyHpBefore\":%u,\"enemyHpAfter\":%u,\"reflected\":%s,\"captured\":%s",
               static_cast<unsigned>(s.playerHpBefore), static_cast<unsigned>(s.playerHpAfter),
               static_cast<unsigned>(s.enemyHpBefore), static_cast<unsigned>(s.enemyHpAfter),
               s.reflected ? "true" : "false", s.captured ? "true" : "false");
        if(t.combatRulesVersion==12 && s.action==Move::Capture) {
            if(s.captureAttempt<1 || s.captureAttempt>3 || s.captureResult<1 || s.captureResult>3 ||
               (s.captureResult==1 ? s.captureChance!=0 : (s.captureChance<10 || s.captureChance>90)) ||
               s.captured!=(s.captureResult==3) || s.opponentAction!=Move::None) {ok=false;break;}
            constexpr const char* result[]{"none","miss","escaped","captured"};
            append(",\"capture\":{\"chance\":%u,\"attempt\":%u,\"result\":\"%s\"}",
                   static_cast<unsigned>(s.captureChance),static_cast<unsigned>(s.captureAttempt),result[s.captureResult]);
        }
        if(s.guard!=Move::None) {
            if(s.guard!=Move::Brace&&s.guard!=Move::Ward&&s.guard!=Move::Counter){ok=false;break;}
            append(",\"guard\":\"%s\"",moveName(s.guard));
        }
        append("}");
    }
    append("]}");
    if (!ok) { output[0] = '\0'; return 0; }
    return used;
}
} // namespace digivice::autobattle

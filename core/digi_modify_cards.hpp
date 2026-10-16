#pragma once
#include <cstdint>
namespace digivice::digimodify {
enum class Effect : std::uint8_t { None=0, CriticalBoost=1, PowerBoost=2, GuardShield=3, Recovery=4, ElementShift=5 };
struct Card { std::uint32_t id; Effect effect; const char* name; };
// Logical content IDs; never treat raw NFC UIDs as these IDs.
inline constexpr Card kCards[] = {
    {0xD101,Effect::CriticalBoost,"CRITICAL BOOST"},
    {0xD102,Effect::PowerBoost,"POWER BOOST"},
    {0xD103,Effect::GuardShield,"GUARD SHIELD"},
    {0xD104,Effect::Recovery,"RECOVERY"},
    {0xD106,Effect::ElementShift,"ELEMENT SHIFT"}
};
constexpr const Card* find(std::uint32_t id) {for(const auto& c:kCards)if(c.id==id)return &c;return nullptr;}
constexpr const char* readyName(unsigned effect) {
    return effect==1?"CRITICAL READY":effect==2?"POWER READY":effect==3?"SHIELD READY":effect==5?"GROVE READY":effect==6?"TIDE READY":effect==7?"EMBER READY":"";
}
constexpr const char* shiftName(unsigned element) {
    return element==1?"ELEMENT SHIFT: GROVE":element==2?"ELEMENT SHIFT: TIDE":element==3?"ELEMENT SHIFT: EMBER":"ELEMENT SHIFT";
}
constexpr const char* shiftHitName(unsigned element,bool critical) {
    return element==1?(critical?"GROVE CRITICAL":"GROVE SHIFT HIT"):element==2?(critical?"TIDE CRITICAL":"TIDE SHIFT HIT"):(critical?"EMBER CRITICAL":"EMBER SHIFT HIT");
}
}

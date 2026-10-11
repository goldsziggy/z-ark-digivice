#include "expedition_raid.hpp"
#include "battle_trace.hpp"
#include "forms.hpp"
#include <algorithm>
#include <cstring>

namespace digivice::nearby {
namespace {
bool attack(Choice choice) { return choice >= Choice::Physical && choice <= Choice::Magic; }
bool defense(Choice choice) { return choice >= Choice::Brace && choice <= Choice::Ward; }
unsigned cost(Choice choice) { return choice == Choice::Heavy ? 6u : 2u; }
std::uint32_t chipFloor(std::uint32_t defenderMax) {
    auto floor = (defenderMax + 23u) / 24u;
    if (floor < 4u) floor = 4u;
    if (floor > 32u) floor = 32u;
    return floor;
}
std::uint32_t movePower(std::uint32_t level) {
    const auto power = level / 3u;
    return power > 16u ? 16u : power;
}
std::uint32_t maximum(const Fighter& fighter) {
    return combat::formProfile(fighter.formId, fighter.level).stats.maxHp;
}
void put(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint32_t get(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
    return value;
}
combat::Move asMove(Choice choice) {
    return choice == Choice::Heavy ? combat::Move::Heavy : choice == Choice::Magic ? combat::Move::Magic : combat::Move::Physical;
}
combat::Defense asGuard(Choice choice) {
    return choice == Choice::Brace ? combat::Defense::Brace : choice == Choice::Counter ? combat::Defense::Counter : combat::Defense::Ward;
}
}

std::uint32_t raidSeed(std::uint32_t formId, std::uint32_t level, std::uint32_t scenario) {
    return (formId & 0xffffu) | ((level & 0xffu) << 16) | ((scenario & 7u) << 24);
}
bool decodeRaidSeed(std::uint32_t seed, std::uint32_t scenario, std::uint32_t& formId, std::uint32_t& level) {
    if ((seed >> 27) != 0 || ((seed >> 24) & 7u) != scenario || scenario > 5) return false;
    formId = seed & 0xffffu;
    level = (seed >> 16) & 0xffu;
    return forms::productionForm(formId) && forms::hasInstalledArt(formId) && combat::validFormProfile(formId, level);
}
bool validRaid(const Raid& raid) {
    std::uint32_t form = 0, level = 0;
    if (!validFighter(raid.players[0]) || !validFighter(raid.players[1]) ||
        !decodeRaidSeed(raid.seed, raid.scenario, form, level) || form != raid.foeForm || level != raid.foeLevel)
        return false;
    const auto foeMax = combat::formProfile(raid.foeForm, raid.foeLevel).stats.maxHp;
    const auto playerMax = std::max(maximum(raid.players[0]), maximum(raid.players[1]));
    if (!foeMax || raid.foeMax != foeMax || raid.foeHp > raid.foeMax || raid.sequence > kMaxRaidRounds ||
        static_cast<unsigned>(raid.status) > 2 || raid.foeTarget > 1 || raid.lastFoeDamage > raid.foeMax ||
        raid.lastPlayerDamage > playerMax)
        return false;
    for (unsigned i = 0; i < 2; ++i)
        if (raid.hp[i] > maximum(raid.players[i]) || raid.energy[i] > 100) return false;
    if (!raid.sequence) {
        return raid.status == RaidStatus::Active && raid.lastAttack == Choice::None && raid.lastDefense == Choice::None &&
            !raid.lastFoeDamage && !raid.lastPlayerDamage && !raid.lastCritical && raid.foeHp && (raid.hp[0] || raid.hp[1]);
    }
    if (!attack(raid.lastAttack) || !defense(raid.lastDefense)) return false;
    if (raid.status == RaidStatus::Active) return raid.sequence < kMaxRaidRounds && raid.foeHp && (raid.hp[0] || raid.hp[1]);
    if (raid.status == RaidStatus::Cleared) return raid.foeHp == 0;
    return raid.sequence == kMaxRaidRounds || (!raid.hp[0] && !raid.hp[1]);
}
bool beginRaid(const Fighter& host, const Fighter& guest, std::uint32_t seed, Raid& out) {
    std::uint32_t form = 0, level = 0;
    const auto scenario = (seed >> 24) & 7u;
    if (!validFighter(host) || !validFighter(guest) || !decodeRaidSeed(seed, scenario, form, level)) return false;
    Raid next;
    next.players[0] = host;
    next.players[1] = guest;
    next.foeForm = form;
    next.foeLevel = level;
    next.foeMax = next.foeHp = combat::formProfile(form, level).stats.maxHp;
    next.hp[0] = maximum(host);
    next.hp[1] = maximum(guest);
    next.energy[0] = next.energy[1] = 100;
    next.seed = next.rng = seed;
    next.scenario = static_cast<std::uint8_t>(scenario);
    if (!validRaid(next)) return false;
    out = next;
    return true;
}
bool legalRaidChoice(const Raid& raid, unsigned actor, Choice choice) {
    if (!validRaid(raid) || raid.status != RaidStatus::Active || actor > 1) return false;
    const bool attacking = actor == raid.sequence % 2u;
    if (!attacking) return defense(choice);
    return attack(choice) && raid.energy[actor] >= cost(choice);
}
bool resolveRaid(Raid& state, std::uint32_t sequence, Choice host, Choice guest) {
    if (sequence != state.sequence || !legalRaidChoice(state, 0, host) || !legalRaidChoice(state, 1, guest)) return false;
    Raid next = state;
    const unsigned attacker = next.sequence % 2u;
    const unsigned defender = 1u - attacker;
    const Choice choices[]{host, guest};
    const auto strike = choices[attacker];
    const auto guardChoice = choices[defender];
    const combat::Defense foeGuards[]{combat::Defense::Brace, combat::Defense::Ward, combat::Defense::Counter};
    const auto& striker = next.players[attacker];
    const auto& guarder = next.players[defender];
    const combat::CareBonus strikerCare{striker.offenseBonus, striker.protectionBonus};
    const combat::CareBonus guarderCare{guarder.offenseBonus, guarder.protectionBonus};
    auto hit = combat::resolveCareForms(striker.formId, striker.level, next.foeForm, next.foeLevel, asMove(strike),
        foeGuards[next.sequence % 3u], strikerCare, {}, chipFloor(next.foeMax), movePower(striker.level));
    next.lastCritical = false;
    if (!hit.reflected && autobattle::nextRandom(next.rng) % 100u < 5u) {
        const auto scaled = hit.damage * 3u / 2u;
        hit.damage = scaled ? scaled : 1u;
        next.lastCritical = true;
    }
    if (!hit.damage) hit.damage = 1;
    next.energy[attacker] = next.energy[attacker] > cost(strike) ? next.energy[attacker] - cost(strike) : 0;
    next.lastFoeDamage = next.lastPlayerDamage = 0;
    if (hit.reflected) {
        const auto received = hit.damage < next.hp[attacker] ? hit.damage : next.hp[attacker];
        next.hp[attacker] -= received;
        next.lastPlayerDamage = received;
        next.foeTarget = static_cast<std::uint8_t>(attacker);
    } else {
        const auto received = hit.damage < next.foeHp ? hit.damage : next.foeHp;
        next.foeHp -= received;
        next.lastFoeDamage = received;
    }
    if (next.foeHp) {
        const auto replyMove = next.sequence % 2u ? combat::Move::Magic : combat::Move::Physical;
        const auto bonus = std::min<std::uint32_t>(40u, 4u * next.foeLevel);
        auto reply = combat::resolveCareForms(next.foeForm, next.foeLevel, guarder.formId, guarder.level, replyMove,
            asGuard(guardChoice), {}, guarderCare, chipFloor(maximum(guarder)), movePower(next.foeLevel));
        auto damage = reply.damage * (100u + bonus) / 100u;
        if (!damage) damage = 1;
        if (reply.reflected) {
            const auto received = damage < next.foeHp ? damage : next.foeHp;
            next.foeHp -= received;
            next.lastFoeDamage += received;
        } else {
            const auto received = damage < next.hp[defender] ? damage : next.hp[defender];
            next.hp[defender] -= received;
            next.lastPlayerDamage = received;
            next.foeTarget = static_cast<std::uint8_t>(defender);
        }
    }
    next.lastAttack = strike;
    next.lastDefense = guardChoice;
    ++next.sequence;
    if (!next.foeHp) next.status = RaidStatus::Cleared;
    else if (!next.hp[0] && !next.hp[1]) next.status = RaidStatus::Wiped;
    else if (next.sequence >= kMaxRaidRounds) next.status = RaidStatus::Wiped;
    else next.status = RaidStatus::Active;
    if (!validRaid(next)) return false;
    state = next;
    return true;
}
bool encodeRaid(const Raid& raid, std::uint8_t* bytes, std::size_t capacity) {
    if (!bytes || capacity < kRaidBytes || !validRaid(raid)) return false;
    const std::uint32_t words[]{
        kRules, kCatalog,
        raid.players[0].memberId, raid.players[0].formId, raid.players[0].level, raid.players[0].offenseBonus, raid.players[0].protectionBonus,
        raid.players[1].memberId, raid.players[1].formId, raid.players[1].level, raid.players[1].offenseBonus, raid.players[1].protectionBonus,
        raid.foeForm, raid.foeLevel, raid.foeHp, raid.foeMax, raid.hp[0], raid.hp[1], raid.energy[0], raid.energy[1],
        raid.seed, raid.rng, raid.sequence,
        raid.scenario | (static_cast<unsigned>(raid.status) << 8) | (static_cast<unsigned>(raid.lastAttack) << 16) | (static_cast<unsigned>(raid.lastDefense) << 24),
        raid.lastFoeDamage, raid.lastPlayerDamage,
        raid.foeTarget | (raid.lastCritical ? 0x100u : 0u)};
    static_assert(sizeof(words) == kRaidBytes);
    for (unsigned i = 0; i < sizeof(words) / 4; ++i) put(bytes + 4 * i, words[i]);
    return true;
}
bool decodeRaid(const std::uint8_t* bytes, std::size_t length, Raid& out) {
    if (!bytes || length != kRaidBytes || get(bytes) != kRules || get(bytes + 4) != kCatalog) return false;
    std::uint32_t words[27];
    for (unsigned i = 0; i < 27; ++i) words[i] = get(bytes + 4 * i);
    const auto packed = words[23];
    const auto flags = words[26];
    if ((packed & 0xffu) > 5 || ((packed >> 8) & 0xffu) > 2 || ((packed >> 16) & 0xffu) > 6 || (packed >> 24) > 6) return false;
    if ((flags & 0xffu) > 1 || (flags >> 9) != 0) return false;
    Raid raid;
    raid.players[0] = {words[2], words[3], words[4], words[5], words[6]};
    raid.players[1] = {words[7], words[8], words[9], words[10], words[11]};
    raid.foeForm = words[12];
    raid.foeLevel = words[13];
    raid.foeHp = words[14];
    raid.foeMax = words[15];
    raid.hp[0] = words[16];
    raid.hp[1] = words[17];
    raid.energy[0] = words[18];
    raid.energy[1] = words[19];
    raid.seed = words[20];
    raid.rng = words[21];
    raid.sequence = words[22];
    raid.scenario = static_cast<std::uint8_t>(packed & 0xffu);
    raid.status = static_cast<RaidStatus>((packed >> 8) & 0xffu);
    raid.lastAttack = static_cast<Choice>((packed >> 16) & 0xffu);
    raid.lastDefense = static_cast<Choice>(packed >> 24);
    raid.lastFoeDamage = words[24];
    raid.lastPlayerDamage = words[25];
    raid.foeTarget = static_cast<std::uint8_t>(flags & 0xffu);
    raid.lastCritical = (flags & 0x100u) != 0;
    if (!validRaid(raid)) return false;
    out = raid;
    return true;
}
bool sameRaid(const Raid& left, const Raid& right) {
    std::uint8_t a[kRaidBytes], b[kRaidBytes];
    return encodeRaid(left, a, sizeof(a)) && encodeRaid(right, b, sizeof(b)) && !std::memcmp(a, b, sizeof(a));
}
} // namespace digivice::nearby

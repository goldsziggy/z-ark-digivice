#pragma once
#include "practice_battle.hpp"
#include "combat.hpp"
#include <cstdint>
#include <limits>

namespace digivice::practice::auto_policy {
// Deliberately excludes hidden commitments, opponent RNG and the optional hint.
// Auto does not use cards. The caller supplies only this public projection.
struct View {
    Phase phase;
    std::uint32_t rulesVersion;
    std::uint32_t playerForm, playerLevel, enemyForm, enemyLevel;
    std::uint32_t playerHp, enemyHp;
};
inline Move choose(const View& view, std::uint32_t& policyRng) {
    const bool attacking = view.phase == Phase::Attack;
    if (view.phase != Phase::Attack && view.phase != Phase::Defend) return Move::None;
    const auto first = attacking ? Move::Physical : Move::Brace;
    const auto opponentFirst = attacking ? Move::Brace : Move::Physical;
    const auto defenderMaxHp = combat::formProfile(attacking ? view.enemyForm : view.playerForm,
        attacking ? view.enemyLevel : view.playerLevel).stats.maxHp;
    const auto floor = minimumRawDamage(view.rulesVersion, defenderMaxHp);
    if (!floor) return Move::None;
    int best = std::numeric_limits<int>::min();
    Move tied[3]{};
    unsigned count = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const auto choice = static_cast<Move>(static_cast<unsigned>(first) + i);
        int score = 0;
        for (unsigned j = 0; j < 3; ++j) {
            const auto opponent = static_cast<Move>(static_cast<unsigned>(opponentFirst) + j);
            const auto attack = attacking ? choice : opponent;
            const auto defend = attacking ? opponent : choice;
            const auto move = attack == Move::Physical ? combat::Move::Physical :
                attack == Move::Heavy ? combat::Move::Heavy : combat::Move::Magic;
            const auto guard = defend == Move::Brace ? combat::Defense::Brace :
                defend == Move::Counter ? combat::Defense::Counter : combat::Defense::Ward;
            const auto hit = combat::resolveForms(attacking ? view.playerForm : view.enemyForm,
                attacking ? view.playerLevel : view.enemyLevel,
                attacking ? view.enemyForm : view.playerForm,
                attacking ? view.enemyLevel : view.playerLevel, move, guard, floor);
            if (!hit.damage) return Move::None;
            const bool hurtsEnemy = attacking != hit.reflected;
            const auto hp = hurtsEnemy ? view.enemyHp : view.playerHp;
            const auto damage = hit.damage < hp ? hit.damage : hp;
            score += hurtsEnemy ? static_cast<int>(damage) : -static_cast<int>(damage);
        }
        if (score > best) { best = score; count = 0; }
        if (score == best) tied[count++] = choice;
    }
    return count == 1 ? tied[0] : tied[autobattle::nextRandom(policyRng) % count];
}
} // namespace digivice::practice::auto_policy

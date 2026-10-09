#pragma once
#include "combat.hpp"
#include <cstddef>
#include <cstdint>

// Friendly, ephemeral two-person duels. Never applies a companion-game action.
namespace digivice::nearby {
constexpr std::uint32_t kRules = 12, kCatalog = 6, kMaxExchanges = 40;
constexpr std::size_t kMatchBytes = 116;
enum class Mode : std::uint8_t { Tactical, Auto };
enum class Status : std::uint8_t { Active, HostWon, GuestWon, Draw };
enum class Choice : std::uint8_t { None, Physical, Heavy, Magic, Brace, Counter, Ward };
struct Fighter { std::uint32_t memberId=0, formId=0, level=0, offenseBonus=0, protectionBonus=0; };
struct Match {
    Fighter fighters[2]{}; // Host0, guest1. Match-scoped identity, never a copied save.
    std::uint32_t hp[2]{}, energy[2]{};
    std::uint32_t seed=0, rng=0, sequence=0;
    Mode mode=Mode::Tactical;
    Status status=Status::Active;
    std::uint8_t attacker=0, lastAttacker=0;
    Choice lastAttack=Choice::None, lastDefense=Choice::None;
    std::uint32_t lastDamage[2]{};
    bool lastReflected=false;
};
bool sameFighter(const Fighter&,const Fighter&);
bool validFighter(const Fighter&);
bool valid(const Match&);
bool begin(const Fighter& host,const Fighter& guest,Mode,std::uint32_t seed,Match& out);
bool legalChoice(const Match&,unsigned actor,Choice);
// Both choices commit together. Wrong/duplicate sequence leaves state unchanged.
bool resolve(Match&,std::uint32_t expectedSequence,Choice host,Choice guest);
// Host calls once per acknowledged/paced exchange; two random basic attacks use
// equal odds when that actor attacks, defense uses the existing three guards.
bool resolveAuto(Match&,std::uint32_t expectedSequence);
bool encode(const Match&,std::uint8_t* bytes,std::size_t capacity);
bool decode(const std::uint8_t* bytes,std::size_t length,Match& out);
const char* choiceName(Choice);
} // namespace digivice::nearby

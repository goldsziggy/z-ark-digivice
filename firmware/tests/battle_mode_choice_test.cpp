#include "battle_mode_choice.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

using namespace digivice::controls;
unsigned checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); \
} } while (false)
int main() {
    BattleModeChoice choice;
    BattleChoice result = BattleChoice::Auto;
    CHECK(sizeof(choice) <= 8);
    CHECK(!choice.pending() && !choice.confirm(0, result));
    CHECK(result == BattleChoice::Auto);
    CHECK(choice.propose(BattleChoice::Auto, 17));
    CHECK(choice.pending() && choice.selected() == BattleChoice::Auto);
    CHECK(!choice.confirm(18, result) && !choice.pending());
    CHECK(!choice.confirm(17, result)); // Old sequence cannot resurrect the choice.
    CHECK(choice.propose(BattleChoice::Auto, 18));
    choice.cancel();
    CHECK(!choice.pending() && !choice.confirm(18, result));
    CHECK(choice.propose(BattleChoice::Tactical, 18));
    CHECK(choice.confirm(18, result) && result == BattleChoice::Tactical);
    CHECK(!choice.confirm(18, result)); // Confirm emits once, even before a save callback.
    CHECK(choice.propose(BattleChoice::Auto, 19));
    CHECK(choice.propose(BattleChoice::Tactical, 19));
    CHECK(choice.confirm(19, result) && result == BattleChoice::Tactical);
    CHECK(choice.propose(BattleChoice::Auto, UINT32_MAX));
    CHECK(!choice.confirm(0, result)); // Sequence comparison does not wrap.
    CHECK(choice.propose(BattleChoice::Auto, 1));
    CHECK(!choice.propose(static_cast<BattleChoice>(255), 1));
    CHECK(!choice.pending() && !choice.confirm(1, result));
    CHECK(result == BattleChoice::Tactical);
    std::printf("PASS battle mode confirmation: %u checks, %zu bytes; no game/storage writes\n", checks, sizeof(choice));
}

#include "starter.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace digivice::onboarding;
unsigned checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); \
} } while (false)

int main() {
    StarterController controller;
    CHECK(sizeof(controller) <= 3);
    CHECK(controller.stage() == Stage::Egg && controller.selectedId() == 1 && controller.choiceCount() == 8);
    CHECK(controller.input(Input::Next).hatchId == 0 && controller.stage() == Stage::Egg);
    CHECK(controller.input(Input::Previous).hatchId == 0 && controller.stage() == Stage::Egg && controller.selectedId() == 1);
    CHECK(controller.input(Input::HoldBack).hatchId == 0 && controller.stage() == Stage::Egg);
    CHECK(controller.input(Input::Confirm).hatchId == 0 && controller.stage() == Stage::Selecting);
    for (unsigned round = 0; round < 20; ++round) for (unsigned i = 1; i <= kChoices; ++i) {
        CHECK(controller.selectedId() == i);
        CHECK(controller.input(Input::Next).hatchId == 0);
    }
    CHECK(controller.input(Input::Previous).hatchId == 0 && controller.selectedId() == 8);
    CHECK(controller.input(Input::Next).hatchId == 0 && controller.selectedId() == 1);
    controller.input(Input::Next);
    CHECK(controller.selectedId() == 2);
    controller.input(Input::Confirm);
    CHECK(controller.stage() == Stage::Confirming);
    controller.input(Input::Next);
    CHECK(controller.selectedId() == 2 && controller.stage() == Stage::Confirming);
    CHECK(controller.input(Input::Previous).hatchId == 0 && controller.selectedId() == 2 && controller.stage() == Stage::Confirming);
    controller.input(Input::HoldBack);
    CHECK(controller.stage() == Stage::Selecting && controller.selectedId() == 2);
    controller.input(Input::HoldBack);
    CHECK(controller.stage() == Stage::Egg && controller.selectedId() == 2);
    controller.input(Input::HoldBack);
    CHECK(controller.stage() == Stage::Egg);
    controller.input(Input::Confirm);
    controller.input(Input::Confirm);
    CHECK(controller.input(Input::Confirm).hatchId == 2);
    CHECK(controller.stage() == Stage::AwaitingCommit);
    for (auto input : {Input::Next, Input::Previous, Input::Confirm, Input::HoldBack}) {
        CHECK(controller.input(input).hatchId == 0);
        CHECK(controller.stage() == Stage::AwaitingCommit && controller.selectedId() == 2);
    }
    controller.resolve(false);
    CHECK(controller.stage() == Stage::Confirming && controller.selectedId() == 2);
    CHECK(controller.input(Input::Confirm).hatchId == 2);
    controller.resolve(true);
    CHECK(controller.stage() == Stage::Complete);
    for (auto input : {Input::Next, Input::Previous, Input::Confirm, Input::HoldBack})
        CHECK(controller.input(input).hatchId == 0 && controller.stage() == Stage::Complete);
    controller.resolve(false);
    CHECK(controller.stage() == Stage::Complete);
    controller.reset(true);
    CHECK(controller.stage() == Stage::Complete && controller.selectedId() == 1);
    controller.reset(false);
    CHECK(controller.stage() == Stage::Egg && controller.selectedId() == 1);
    controller.resolve(true); // A save callback cannot complete an unrequested hatch.
    CHECK(controller.stage() == Stage::Egg);

    // Extended roster is navigation over caller-owned offers, with the same
    // two separate confirmations and durable-commit boundary as the fixed eight.
    StarterController extended;
    CHECK(extended.configureChoices(11));
    CHECK(extended.stage() == Stage::Egg && extended.choiceCount() == 11 && extended.selectedId() == 1);
    CHECK(extended.input(Input::Confirm).hatchId == 0 && extended.stage() == Stage::Selecting);
    CHECK(extended.input(Input::Previous).hatchId == 0 && extended.selectedId() == 11);
    CHECK(extended.input(Input::Next).hatchId == 0 && extended.selectedId() == 1);
    for (unsigned id = 1; id <= 11; ++id) {
        CHECK(extended.selectedId() == id);
        CHECK(extended.input(Input::Next).hatchId == 0);
    }
    for (unsigned id = 11; id; --id) {
        CHECK(extended.input(Input::Previous).hatchId == 0 && extended.selectedId() == id);
    }
    for (unsigned i = 1; i < 7; ++i) extended.input(Input::Next);
    CHECK(extended.configureChoices(8) && extended.selectedId() == 7 && extended.stage() == Stage::Selecting);
    CHECK(extended.configureChoices(11) && extended.selectedId() == 7 && extended.stage() == Stage::Selecting);
    CHECK(extended.input(Input::Confirm).hatchId == 0 && extended.stage() == Stage::Confirming);
    CHECK(extended.configureChoices(8) && extended.selectedId() == 7 && extended.stage() == Stage::Confirming);
    CHECK(extended.configureChoices(11) && extended.selectedId() == 7 && extended.stage() == Stage::Confirming);
    CHECK(extended.input(Input::HoldBack).hatchId == 0 && extended.stage() == Stage::Selecting);
    for (unsigned i = 7; i < 11; ++i) extended.input(Input::Next);
    CHECK(extended.input(Input::Confirm).hatchId == 0 && extended.selectedId() == 11 && extended.stage() == Stage::Confirming);
    // Removing a reviewed candidate revokes that review; configuration cannot
    // turn a later confirmation into a hatch of an unseen replacement.
    CHECK(extended.configureChoices(8) && extended.selectedId() == 8 && extended.stage() == Stage::Selecting);
    CHECK(extended.input(Input::Confirm).hatchId == 0 && extended.stage() == Stage::Confirming);
    CHECK(extended.input(Input::Confirm).hatchId == 8 && extended.stage() == Stage::AwaitingCommit);
    CHECK(!extended.configureChoices(11) && extended.choiceCount() == 8 && extended.selectedId() == 8 && extended.stage() == Stage::AwaitingCommit);
    CHECK(extended.configureChoices(8) && extended.stage() == Stage::AwaitingCommit);
    extended.resolve(false);
    CHECK(extended.configureChoices(11) && extended.selectedId() == 8 && extended.stage() == Stage::Confirming);
    CHECK(extended.input(Input::HoldBack).hatchId == 0 && extended.stage() == Stage::Selecting);
    CHECK(extended.input(Input::HoldBack).hatchId == 0 && extended.stage() == Stage::Egg && extended.selectedId() == 8);
    CHECK(extended.configureChoices(11) && extended.selectedId() == 8 && extended.stage() == Stage::Egg);
    extended.reset(false);
    CHECK(extended.choiceCount() == 11 && extended.selectedId() == 1 && extended.stage() == Stage::Egg);
    extended.reset(true);
    CHECK(extended.choiceCount() == 11 && extended.selectedId() == 1 && extended.stage() == Stage::Complete);

    for (unsigned id = 9; id <= 11; ++id) {
        StarterController offered;
        CHECK(offered.configureChoices(11));
        CHECK(offered.input(Input::Confirm).hatchId == 0);
        for (unsigned i = 1; i < id; ++i) CHECK(offered.input(Input::Next).hatchId == 0);
        CHECK(offered.input(Input::Confirm).hatchId == 0 && offered.stage() == Stage::Confirming);
        CHECK(offered.input(Input::HoldBack).hatchId == 0 && offered.selectedId() == id && offered.stage() == Stage::Selecting);
        CHECK(offered.input(Input::Confirm).hatchId == 0);
        CHECK(offered.input(Input::Confirm).hatchId == id && offered.stage() == Stage::AwaitingCommit);
        for (auto input : {Input::Next, Input::Previous, Input::Confirm, Input::HoldBack})
            CHECK(offered.input(input).hatchId == 0 && offered.selectedId() == id && offered.stage() == Stage::AwaitingCommit);
        CHECK(!offered.configureChoices(8) && offered.selectedId() == id && offered.choiceCount() == 11);
        offered.resolve(false);
        CHECK(offered.stage() == Stage::Confirming && offered.selectedId() == id);
        CHECK(offered.input(Input::Confirm).hatchId == id);
        offered.resolve(true);
        CHECK(offered.stage() == Stage::Complete && offered.selectedId() == id);
    }

    // Invalid configuration is inert in every stage, including a pending save.
    StarterController bounds;
    for (unsigned step = 0; step < 5; ++step) {
        const auto before = bounds.stage();
        for (const auto count : {0u, 12u, 255u}) {
            CHECK(!bounds.configureChoices(static_cast<std::uint8_t>(count)));
            CHECK(bounds.choiceCount() == 8 && bounds.selectedId() == 1 && bounds.stage() == before);
        }
        if (before == Stage::AwaitingCommit) bounds.resolve(true);
        else bounds.input(Input::Confirm);
    }
    // Small dynamic rosters, including a singleton, cannot wrap to zero or
    // manufacture an out-of-range request in either navigation direction.
    for (std::uint8_t count = 1; count <= kMaxChoices; ++count) {
        StarterController bounded;
        CHECK(bounded.configureChoices(count));
        CHECK(bounded.input(Input::Confirm).hatchId == 0);
        for (unsigned round = 0; round < 2; ++round) {
            for (unsigned id = 1; id <= count; ++id) {
                CHECK(bounded.selectedId() == id);
                CHECK(bounded.input(Input::Next).hatchId == 0);
            }
            for (unsigned id = count; id; --id)
                CHECK(bounded.input(Input::Previous).hatchId == 0 && bounded.selectedId() == id);
        }
        CHECK(bounded.input(Input::Previous).hatchId == 0 && bounded.selectedId() == count);
        CHECK(bounded.input(Input::Confirm).hatchId == 0);
        CHECK(bounded.input(Input::Confirm).hatchId == count);
    }
    std::printf("PASS starter navigation: %u checks, %zu bytes, no game/storage writes\n", checks, sizeof(controller));
}

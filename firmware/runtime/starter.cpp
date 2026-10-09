#include "starter.hpp"

namespace digivice::onboarding {
bool StarterController::configureChoices(std::uint8_t count) {
    if (!count || count > kMaxChoices) return false;
    if (count == choices_) return true;
    if (stage_ == Stage::AwaitingCommit) return false;
    choices_ = count;
    if (selected_ > choices_) {
        selected_ = choices_;
        if (stage_ == Stage::Confirming) stage_ = Stage::Selecting;
    }
    return true;
}
void StarterController::reset(bool alreadyComplete) {
    selected_ = 1;
    stage_ = alreadyComplete ? Stage::Complete : Stage::Egg;
}
Request StarterController::input(Input input) {
    if (stage_ == Stage::Complete || stage_ == Stage::AwaitingCommit) return {};
    if (input == Input::HoldBack) {
        if (stage_ == Stage::Confirming) stage_ = Stage::Selecting;
        else if (stage_ == Stage::Selecting) stage_ = Stage::Egg;
    } else if (input == Input::Next && stage_ == Stage::Selecting) {
        selected_ = selected_ == choices_ ? 1 : static_cast<std::uint8_t>(selected_ + 1);
    } else if (input == Input::Previous && stage_ == Stage::Selecting) {
        selected_ = selected_ == 1 ? choices_ : static_cast<std::uint8_t>(selected_ - 1);
    } else if (input == Input::Confirm) {
        if (stage_ == Stage::Egg) stage_ = Stage::Selecting;
        else if (stage_ == Stage::Selecting) stage_ = Stage::Confirming;
        else if (stage_ == Stage::Confirming) {
            stage_ = Stage::AwaitingCommit;
            return {selected_};
        }
    }
    return {};
}
void StarterController::resolve(bool committed) {
    if (stage_ == Stage::AwaitingCommit)
        stage_ = committed ? Stage::Complete : Stage::Confirming;
}
const char* stageName(Stage stage) {
    switch (stage) {
        case Stage::Egg: return "egg";
        case Stage::Selecting: return "choose starter";
        case Stage::Confirming: return "confirm starter";
        case Stage::AwaitingCommit: return "saving starter";
        case Stage::Complete: return "complete";
    }
    return "invalid";
}
} // namespace digivice::onboarding

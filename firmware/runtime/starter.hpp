#pragma once

#include <cstdint>

namespace digivice::onboarding {

// Navigation only. A caller applies Hatch and durably checkpoints the resulting
// game state before resolve(true). This controller owns no identity or save data.
constexpr std::uint8_t kChoices = 8;
constexpr std::uint8_t kMaxChoices = 11;
enum class Stage : std::uint8_t { Egg, Selecting, Confirming, AwaitingCommit, Complete };
enum class Input : std::uint8_t { Next, Confirm, HoldBack, Previous };
struct Request { std::uint8_t hatchId = 0; };

class StarterController {
public:
    // The caller supplies the available, already-persisted roster. This only
    // changes navigation bounds; it never creates offers or requests a hatch.
    // A clamped reviewed choice needs a fresh review. An in-flight request
    // rejects changed bounds until its durable outcome is resolved.
    bool configureChoices(std::uint8_t count);
    void reset(bool alreadyComplete);
    Request input(Input input);
    void resolve(bool committed);
    Stage stage() const { return stage_; }
    std::uint8_t selectedId() const { return selected_; }
    std::uint8_t choiceCount() const { return choices_; }
private:
    Stage stage_ = Stage::Egg;
    std::uint8_t selected_ = 1;
    std::uint8_t choices_ = kChoices;
};
const char* stageName(Stage stage);
} // namespace digivice::onboarding

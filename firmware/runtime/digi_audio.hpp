#pragma once
#include "audio_cues.hpp"
#include "digi_modify_cards.hpp"
#include "battle_presentation.hpp"
namespace digivice::device {
// Shared by the firmware owner and host recorder; successful save is the caller's gate.
inline AudioCue cardCue(digimodify::Effect effect) {
    switch(effect) {
    case digimodify::Effect::None: return AudioCue::Count;
    case digimodify::Effect::CriticalBoost: return AudioCue::DigiCritical;
    case digimodify::Effect::PowerBoost: return AudioCue::DigiPower;
    case digimodify::Effect::GuardShield: return AudioCue::DigiGuard;
    case digimodify::Effect::Recovery: return AudioCue::DigiRecovery;
    case digimodify::Effect::ElementShift: return AudioCue::DigiElement;
    }
    return AudioCue::Count;
}
inline AudioCue battleCue(battlepresentation::Cue cue,const battlepresentation::View& view) {
    using C=battlepresentation::Cue;
    switch(cue) {
    case C::Attack:return AudioCue::Attack;
    case C::Magic:return AudioCue::Magic;
    case C::Hit:return view.critical&&!view.reflected?AudioCue::DigiCriticalHit:AudioCue::Hit;
    case C::CaptureThrow:return AudioCue::CaptureThrow;
    case C::CaptureSuccess:return AudioCue::CaptureSuccess;
    case C::CaptureFail:return AudioCue::CaptureFail;
    case C::Win:return AudioCue::Win;
    case C::Retreat:return AudioCue::Retreat;
    case C::None:return AudioCue::Count;
    }
    return AudioCue::Count;
}
} // namespace digivice::device

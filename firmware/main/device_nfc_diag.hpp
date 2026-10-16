#pragma once
#include "../runtime/pn532_hsu.hpp"
#include "../runtime/nfc_cards.hpp"
#include "../runtime/nfc_test.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace digivice::nfcdiag {
class Device final : private Link {
public:
    // Observations come from completed transactions, never repeated cached
    // presence. Allow one bounded scan plus scheduling; owner stalls still
    // cancel at250ms independently in poll().
    static constexpr std::uint64_t kObservationGapMs=Session::kSessionMs+100;
    Device() : session_(*this) {}
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    bool busy() const { return session_.busy() || installed_; }
    bool diagnosticBusy() const {return owner_==Owner::Diagnostic && session_.busy();}
    bool quiescent() const {return !session_.busy()&&!installed_&&!pins_;}
    bool cardsConfigured() const;
    const nfctest::View& testView() const { return test_.view(); }
    bool testAvailable() const;
    void setTestEnabled(bool active, std::uint64_t now);
    void retryTest(std::uint64_t now);
    void resetTest() { test_.reset(); }

    bool command(const char* line, std::uint64_t now, bool allowed);
    void poll(std::uint64_t now, bool allowed, bool cardsAllowed=false,std::uint32_t encounter=0);
    bool takeCardSample(digimodify::Sample& sample) {if(!pending_)return false;sample=sample_;pending_=false;return true;}
    void status() const;
private:
    bool open();
    void close();
    int write(const std::uint8_t*, std::size_t) override;
    int read(std::uint8_t*, std::size_t) override;
    bool flush() override;
    enum class Owner : std::uint8_t { None, Diagnostic, Cards, Test };
    bool pollTest(std::uint64_t now, bool allowed, bool stale);
    nfctest::Tracker test_;
    bool testActive_=false, testFault_=false, testCancelled_=false;
    void invalidate() {sample_={};pending_=true;identity_.reset();}
    Session session_;
    nfccards::Identity identity_;
    digimodify::Sample sample_{};
    std::uint64_t nextScan_=0,lastPoll_=0;
    std::uint32_t encounter_=0;
    Owner owner_=Owner::None;
    bool pending_=false,stopped_=false,cancelCards_=false,seenPoll_=false;
    QueueHandle_t events_ = nullptr;
    bool installed_ = false, pins_ = false;
    int error_ = 0;
};
} // namespace digivice::nfcdiag

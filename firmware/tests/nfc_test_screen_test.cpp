#include "../main/device_nfc_diag.hpp"
#include "../main/board_hal.hpp"
#include "nfc_uart_mock.hpp"
#include "esp_err.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace digivice;
static std::uint64_t clockMs=0;
std::int64_t esp_timer_get_time(){return static_cast<std::int64_t>(clockMs*1000);}
const char* esp_err_to_name(esp_err_t e){return e?"IO_ERROR":"ESP_OK";}
static board::ProfileId profile=board::ProfileId::Waveshare21;
namespace digivice::board {const BoardProfile& selectedProfile(){return board::profile(::profile);}}
static void run(nfcdiag::Device& d,unsigned ms,bool allowed=true){for(unsigned i=0;i<ms;++i)d.poll(++clockMs,allowed);}
static void noGameplay(nfcdiag::Device& d){digimodify::Sample s;while(d.takeCardSample(s)){assert(s.presence==digimodify::Presence::Unavailable);}}
int main(){
 nfcmock::reset();nfcdiag::Device d;d.setTestEnabled(true,clockMs);run(d,1600);
 assert(d.testView().connected&&d.testView().firmwareMinor==6&&d.testView().swipes==0);
 nfcmock::uid={1,2,3,4};run(d,1800);assert(d.testView().swipes==1&&!std::strcmp(d.testView().uid,"01020304"));noGameplay(d);
 run(d,2000);assert(d.testView().swipes==1); // Held card never repeats.
 nfcmock::uid={5,6,7,8};run(d,1000);assert(d.testView().swipes==1); // UID change alone cannot re-arm.
 d.resetTest();run(d,1200);assert(d.testView().swipes==0); // Reset held card does not recount.
 nfcmock::uid.clear();run(d,1500);assert(d.testView().armed);
 nfcmock::uid={1,2,3,4,5,6,7,8,9,10};run(d,1500);assert(d.testView().swipes==1&&std::strlen(d.testView().uid)==20);
 nfcmock::silent=true;run(d,4500);assert(!d.testView().connected&&!d.testView().scanning&&!d.busy());
 assert(!std::strcmp(d.testView().state,"READER ERROR")&&!d.testView().rfOff);
 const auto opens=nfcmock::opens;run(d,8000);assert(nfcmock::opens==opens); // No runaway retry.
 nfcmock::silent=false;d.retryTest(clockMs);run(d,1500);assert(d.testView().connected&&d.testView().swipes==1);
 nfcmock::uid.clear();run(d,1400);nfcmock::uid={1,2,3,4};run(d,1200);assert(d.testView().swipes==2);noGameplay(d);
 run(d,1,false);run(d,3000,false);assert(d.quiescent()&&!std::strcmp(d.testView().state,"PAUSED"));
 const auto sleeping=nfcmock::opens;run(d,1000,false);assert(nfcmock::opens==sleeping);
 run(d,1500);assert(d.testView().swipes==2); // Wake while card held requires removal.
 d.setTestEnabled(false,clockMs);run(d,3000);assert(d.quiescent()&&!nfcmock::installed);
 const auto closed=nfcmock::calls;run(d,3000);assert(nfcmock::calls==closed);noGameplay(d);
 // Exit in every startup phase, including silent-reader timeouts.
 for(unsigned at:{0u,1u,50u,80u,130u,300u,1100u}){
  nfcmock::reset();nfcmock::silent=true;nfcdiag::Device exit;
  exit.setTestEnabled(true,clockMs);run(exit,at);exit.setTestEnabled(false,clockMs);run(exit,7500);assert(exit.quiescent());
 }
 nfcmock::reset();profile=board::ProfileId::Waveshare146;nfcdiag::Device small;small.setTestEnabled(true,clockMs);run(small,9000);
 assert(!small.testView().available&&nfcmock::calls==0);
 // Tracker rejects failed cleanup and only re-arms after consecutive clean absence.
 nfctest::Tracker t;t.begin(true);nfcdiag::Result r;r.error="none";r.samReady=r.firmwareValid=r.rfOff=true;r.noCard=true;
 t.fault("ack_timeout");t.observe(r,0);t.observe(r,50);assert(!t.view().armed);t.observe(r,200);assert(t.view().armed);
 r.noCard=false;r.uidValid=true;r.uidSize=4;t.observe(r,300);assert(t.view().swipes==1);
 r.rfOff=false;t.observe(r,400);assert(!t.view().connected&&t.view().swipes==1);
 puts("NFC Test: unknown UID, held/change debounce, removal/rearm, reset/retry, timeout, RF failure, sleep/wake, all-phase exit, profile gate and no gameplay delivery passed. SIMULATED UART ONLY.");
}

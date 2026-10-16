// Resource double for runtime/save tests only. PN532 itself has a separate
// actual-adapter protocol suite; these tests never open a UART.
#include "device_nfc_diag.hpp"
namespace digivice::nfcdiag {
bool Device::command(const char*,std::uint64_t,bool){return false;}
void Device::poll(std::uint64_t,bool,bool,std::uint32_t){}
bool Device::cardsConfigured() const{return false;}
void Device::status() const{}
bool Device::open(){return false;}
void Device::close(){if(events_ || installed_ || pins_ || error_ || nextScan_ || lastPoll_ || encounter_ || stopped_ || cancelCards_ || seenPoll_){events_=nullptr;installed_=pins_=false;error_=0;nextScan_=lastPoll_=encounter_=0;stopped_=cancelCards_=seenPoll_=false;}}
int Device::write(const std::uint8_t*,std::size_t){return -1;}
int Device::read(std::uint8_t*,std::size_t){return -1;}
bool Device::flush(){return false;}
}

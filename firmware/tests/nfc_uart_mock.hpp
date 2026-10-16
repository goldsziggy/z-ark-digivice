#pragma once
#include <cstdint>
#include <vector>
namespace nfcmock {
extern bool installed,silent,corrupt,failOff,badFirmware,wrongResponse;
extern unsigned calls,writes,reads,opens,closes,flushes,scans,offCommands,readChunk,writeChunk;
extern int eventError;
extern unsigned responseDelayMs,samCommands,firmwareBeforeSam;
extern std::vector<std::uint8_t> uid;
void reset();
}

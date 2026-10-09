// Actual POSIX implementation + existing real mbedTLS; no board or private data.
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "mbedtls/sha256.h"

namespace {
int checks = 0;
std::set<int> transferDescriptors;
unsigned peakDescriptors = 0;
int failSync = 0, failRead = 0, writeBudget = -1, interruptedWrites = 0;
void check(bool yes, const char* why) { ++checks; if (!yes) throw std::runtime_error(why); }
}
int transfer_test_open(const char* path, int flags, ...) {
    va_list args; va_start(args, flags); const int mode = va_arg(args, int); va_end(args);
    const int fd = ::open(path, flags, mode);
    if (fd >= 0) {
        transferDescriptors.insert(fd);
        peakDescriptors = std::max<unsigned>(peakDescriptors, transferDescriptors.size());
        check(transferDescriptors.size() <= 1, "only one transfer descriptor (cache owns other slot)");
    }
    return fd;
}
int transfer_test_close(int fd) { check(transferDescriptors.erase(fd) == 1, "close owned descriptor"); return ::close(fd); }
ssize_t transfer_test_read(int fd, void* data, size_t bytes) {
    check(bytes <= 512, "bounded read");
    if (failRead) { --failRead; errno = EIO; return -1; }
    return ::read(fd, data, bytes);
}
ssize_t transfer_test_write(int fd, const void* data, size_t bytes) {
    check(bytes <= 512, "bounded write");
    if (interruptedWrites) { --interruptedWrites; errno = EINTR; return -1; }
    if (writeBudget == 0) { errno = ENOSPC; return -1; }
    if (writeBudget > 0) bytes = std::min<std::size_t>(bytes, static_cast<std::size_t>(writeBudget));
    const auto written = ::write(fd, data, bytes);
    if (writeBudget > 0 && written > 0) writeBudget -= static_cast<int>(written);
    return written;
}
int transfer_test_fsync(int fd) {
    if (failSync) { --failSync; errno = EIO; return -1; }
    return ::fsync(fd);
}
#define open transfer_test_open
#define close transfer_test_close
#define read transfer_test_read
#define write transfer_test_write
#define fsync transfer_test_fsync
#include "../runtime/usb_sd_transfer.cpp"
#undef open
#undef close
#undef read
#undef write
#undef fsync

namespace {
using digivice::assets::UsbSdTransfer;
struct Directory {
    std::string root;
    Directory() { char name[] = "/tmp/digivice-usb-test.XXXXXX"; const char* made = ::mkdtemp(name); check(made, "temporary directory"); root = made; }
    ~Directory() { std::filesystem::remove_all(root); }
    std::string path(const char* name) const { return root + "/" + name; }
};
std::string encode(const std::string& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char byte : bytes) { out += digits[byte >> 4]; out += digits[byte & 15]; }
    return out;
}
std::string sha(const std::string& bytes) {
    unsigned char out[32];
    check(mbedtls_sha256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), out, 0) == 0, "real SHA256");
    return encode(std::string(reinterpret_cast<char*>(out), sizeof(out)));
}
void put(const std::string& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary); file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); check(file.good(), "write fixture");
}
std::string get(const std::string& path) { std::ifstream file(path, std::ios::binary); check(file.good(), "read fixture"); return {std::istreambuf_iterator<char>(file), {}}; }
std::string command(UsbSdTransfer& transfer, const std::string& text) {
    char response[UsbSdTransfer::kResponseBytes]{};
    check(transfer.handle(text.c_str(), response, sizeof(response)), "recognized command");
    check(transfer.quiescent() && transferDescriptors.empty(), "operation returns with no live descriptor");
    return response;
}
void starts(const std::string& value, const char* prefix) { check(value.rfind(prefix, 0) == 0, prefix); }
std::string spec(const char* verb, const char* name, const std::string& data) {
    return std::string("sdput ") + verb + " " + name + " " + std::to_string(data.size()) + " " + sha(data);
}
std::string chunk(std::size_t offset, const std::string& data) { return "sdput chunk " + std::to_string(offset) + " " + encode(data); }
void send(UsbSdTransfer& transfer, const std::string& data, std::size_t offset = 0) {
    for (; offset < data.size();) {
        const auto bytes = data.substr(offset, 512); offset += bytes.size();
        check(command(transfer, chunk(offset - bytes.size(), bytes)) == "SDPUT ACK offset=" + std::to_string(offset), "durable chunk ACK");
    }
}
struct Cooperation { unsigned calls = 0; bool proceed = true; UsbSdTransfer* engine = nullptr; };
bool cooperate(void* context) {
    auto& state = *static_cast<Cooperation*>(context); ++state.calls;
    if (state.engine) check(!state.engine->quiescent() && !state.engine->pause(), "busy callback cannot close/reenter owner operation");
    return state.proceed;
}
void flow() {
    Directory dir; Cooperation cooperation;
    UsbSdTransfer transfer(dir.root.c_str(), cooperate, &cooperation); cooperation.engine = &transfer;
    check(command(transfer, "sdput status") == "SDPUT IDLE", "fresh status");
    std::string data(1300, '\0'); for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 13);
    starts(command(transfer, spec("verify", "DSF00001.DVA", data)), "SDPUT ERROR code=MISSING");
    const auto first = command(transfer, spec("begin", "DSF00001.DVA", data));
    starts(first, "SDPUT READY name=DSF00001.DVA size=1300 offset=0 ");
    check(first.find("prefixSha256=" + sha("")) != std::string::npos, "empty prefix hash");
    check(transfer.active(), "active session");
    check(command(transfer, chunk(0, data.substr(0, 512))) == "SDPUT ACK offset=512", "first chunk");
    check(command(transfer, chunk(0, data.substr(0, 512))) == "SDPUT ACK offset=512", "lost-ACK duplicate idempotent");
    starts(command(transfer, chunk(0, std::string(512, 'x'))), "SDPUT ERROR code=CHUNK_CONFLICT");
    starts(command(transfer, chunk(1024, data.substr(1024))), "SDPUT ERROR code=OFFSET");
    starts(command(transfer, spec("begin", "INDEX.JSON", "other")), "SDPUT ERROR code=BUSY");
    check(command(transfer, "sdput abort") == "SDPUT PAUSED" && !transfer.active(), "abort preserves stage");
    check(get(dir.path("DVXFER.TMP")) == data.substr(0, 512), "aborted bytes intact");
    UsbSdTransfer resumed(dir.root.c_str());
    starts(command(resumed, "sdput status"), "SDPUT PENDING name=DSF00001.DVA size=1300 offset=512 ");
    const auto ready = command(resumed, spec("begin", "DSF00001.DVA", data));
    check(ready.find("offset=512 ") != std::string::npos && ready.find("prefixSha256=" + sha(data.substr(0, 512))) != std::string::npos, "restart resumes with actual prefix proof");
    send(resumed, data, 512);
    starts(command(resumed, "sdput finish"), "SDPUT DONE name=DSF00001.DVA size=1300 ");
    starts(command(resumed, "sdput finish"), "SDPUT DONE ");
    check(get(dir.path("DSF00001.DVA")) == data, "published bytes exact");
    check(!std::filesystem::exists(dir.path("DVXFER.MET")) && !std::filesystem::exists(dir.path("DVXFER.TMP")), "only owned staging removed");
    check(command(resumed, "sdput status") == "SDPUT IDLE", "finished status");
    starts(command(resumed, spec("verify", "DSF00001.DVA", data)), "SDPUT VERIFIED ");
    starts(command(resumed, spec("begin", "DSF00001.DVA", data)), "SDPUT EXISTS ");
    starts(command(resumed, spec("begin", "DSF00001.DVA", std::string(1300, 'x'))), "SDPUT ERROR code=CONFLICT");
    check(get(dir.path("DSF00001.DVA")) == data, "existing differing file preserved");
}
void conflicts() {
    Directory dir; UsbSdTransfer transfer(dir.root.c_str());
    put(dir.path("DVXFER.TMP"), "unknown");
    starts(command(transfer, spec("begin", "INDEX.JSON", "{}")), "SDPUT ERROR code=STAGING_CONFLICT");
    check(get(dir.path("DVXFER.TMP")) == "unknown" && !std::filesystem::exists(dir.path("DVXFER.MET")), "orphan blocks all creation");
    std::filesystem::remove(dir.path("DVXFER.TMP"));
    put(dir.path("DVXFER.MET"), std::string(128, '\0'));
    starts(command(transfer, spec("begin", "INDEX.JSON", "{}")), "SDPUT ERROR code=STAGING_CONFLICT");
    check(get(dir.path("DVXFER.MET")) == std::string(128, '\0'), "invalid metadata preserved");
    std::filesystem::remove(dir.path("DVXFER.MET"));
    std::filesystem::create_directory(dir.path("INDEX.JSON"));
    starts(command(transfer, spec("verify", "INDEX.JSON", "{}")), "SDPUT ERROR code=CONFLICT");
    std::filesystem::remove(dir.path("INDEX.JSON"));
    put(dir.path("unknown.bin"), "{}" );
    check(::symlink("unknown.bin", dir.path("INDEX.JSON").c_str()) == 0, "symlink fixture");
    starts(command(transfer, spec("begin", "INDEX.JSON", "{}")), "SDPUT ERROR code=CONFLICT");
    check(get(dir.path("unknown.bin")) == "{}", "symlink target unchanged");
    std::filesystem::remove(dir.path("INDEX.JSON"));
    starts(command(transfer, spec("begin", "INDEX.JSON", "{}")), "SDPUT READY ");
    send(transfer, "{}");
    put(dir.path("INDEX.JSON"), "different");
    starts(command(transfer, "sdput finish"), "SDPUT ERROR code=CONFLICT");
    check(get(dir.path("INDEX.JSON")) == "different" && get(dir.path("DVXFER.TMP")) == "{}", "destination introduced mid-transfer never overwritten");
}
void interrupted() {
    Directory dir; UsbSdTransfer transfer(dir.root.c_str());
    const std::string data(700, 'p');
    failSync = 1;
    starts(command(transfer, spec("begin", "ATTRIB.TXT", data)), "SDPUT ERROR code=IO");
    check(std::filesystem::exists(dir.path("DVXFER.MET")) && !std::filesystem::exists(dir.path("DVXFER.TMP")), "metadata-first interruption leaves no foreign temp");
    starts(command(transfer, spec("begin", "ATTRIB.TXT", data)), "SDPUT READY ");
    writeBudget = 173;
    starts(command(transfer, chunk(0, data.substr(0, 512))), "SDPUT ERROR code=IO");
    writeBudget = -1;
    check(get(dir.path("DVXFER.TMP")).size() == 173, "partial write retained after full-card error");
    check(transfer.pause(), "pause after write failure");
    UsbSdTransfer resumed(dir.root.c_str());
    const auto reply = command(resumed, spec("begin", "ATTRIB.TXT", data));
    check(reply.find("offset=173 ") != std::string::npos && reply.find("prefixSha256=" + sha(data.substr(0, 173))) != std::string::npos, "unaligned partial offset resumes");
    failSync = 1;
    starts(command(resumed, chunk(173, data.substr(173, 512))), "SDPUT ERROR code=IO");
    check(command(resumed, chunk(173, data.substr(173, 512))) == "SDPUT ACK offset=685", "retry sync after failed durable ACK");
    interruptedWrites = 2;
    send(resumed, data, 685);
    failRead = 1;
    starts(command(resumed, "sdput finish"), "SDPUT ERROR code=IO");
    check(!std::filesystem::exists(dir.path("ATTRIB.TXT")), "readback failure prevents publish");
    starts(command(resumed, "sdput finish"), "SDPUT DONE ");
    check(get(dir.path("ATTRIB.TXT")) == data, "resumed contents exact");

    // Simulate power loss after the FAT rename but before metadata cleanup.
    starts(command(resumed, spec("begin", "MEADOW.JPG", "scene")), "SDPUT READY "); send(resumed, "scene");
    std::filesystem::rename(dir.path("DVXFER.TMP"), dir.path("MEADOW.JPG"));
    UsbSdTransfer rebooted(dir.root.c_str());
    starts(command(rebooted, spec("begin", "MEADOW.JPG", "scene")), "SDPUT EXISTS ");
    check(!std::filesystem::exists(dir.path("DVXFER.MET")), "matching published-file recovery cleans owned metadata");
    // Torn metadata initialization is intentionally a retained, explicit blocker.
    put(dir.path("DVXFER.MET"), "DVUSB001partial");
    starts(command(rebooted, spec("begin", "FOREST.JPG", "scene")), "SDPUT ERROR code=STAGING_CONFLICT");
    check(get(dir.path("DVXFER.MET")) == "DVUSB001partial", "torn metadata not guessed or removed");
}
void hashAndBounds() {
    Directory dir; Cooperation cooperation; UsbSdTransfer transfer(dir.root.c_str(), cooperate, &cooperation); cooperation.engine = &transfer;
    std::string data(UsbSdTransfer::kMaximumFileBytes, 'm');
    put(dir.path("ATTRIB.TXT"), data);
    starts(command(transfer, spec("verify", "ATTRIB.TXT", data)), "SDPUT VERIFIED ");
    check(cooperation.calls == data.size() / 512, "maximum-size hashing cooperates every block");
    cooperation.proceed = false;
    starts(command(transfer, spec("verify", "ATTRIB.TXT", data)), "SDPUT ERROR code=INTERRUPTED");
    cooperation.proceed = true;
    check(get(dir.path("ATTRIB.TXT")) == data, "cancelled hash is read-only");
    for (auto* name : {"DSF00000.DVA", "DSF00513.DVA", "../INDEX.JSON", "DVASSET1.CCH", "index.json", "FOO.JPG", "DSF00001.DVA/x"})
        starts(command(transfer, spec("begin", name, "x")), "SDPUT ERROR code=SYNTAX");
    for (auto* name : {"DSF00001.DVA", "DSF00512.DVA", "MEADOW.JPG", "FOREST.JPG", "BEACH.JPG", "RUINS.JPG", "CAVERN.JPG", "SNOW.JPG", "VOLCANIC.JPG", "DIGITAL.JPG", "INDEX.JSON", "ATTRIB.TXT"}) check(UsbSdTransfer::allowedName(name), "complete pack allowlist");
    for (auto* size : {"0", "2097153", "4294967297", "-1", "2x"}) starts(command(transfer, std::string("sdput begin INDEX.JSON ") + size + " " + sha("xx")), "SDPUT ERROR code=SYNTAX");
    starts(command(transfer, "sdput chunk 0 " + std::string(1026, 'a')), "SDPUT ERROR code=SYNTAX");
    starts(command(transfer, "sdput chunk 0 XX"), "SDPUT ERROR code=SYNTAX");
    starts(command(transfer, "sdput status extra"), "SDPUT ERROR code=SYNTAX");
    char response[384] = "unchanged";
    check(!transfer.handle("status", response, sizeof(response)) && std::strcmp(response, "unchanged") == 0, "unrecognized command has no effect");
    starts(command(transfer, spec("begin", "INDEX.JSON", "good")), "SDPUT READY "); send(transfer, "evil");
    starts(command(transfer, "sdput finish"), "SDPUT ERROR code=HASH_MISMATCH");
    check(!std::filesystem::exists(dir.path("INDEX.JSON")) && get(dir.path("DVXFER.TMP")) == "evil", "bad full digest not published or deleted");
}
}
int main() {
    try { flow(); conflicts(); interrupted(); hashAndBounds(); check(peakDescriptors == 1 && transferDescriptors.empty(), "descriptor budget preserved"); std::cout << "PASS " << checks << " USB SD transfer checks; real mbedTLS, POSIX, ASan/UBSan; no hardware.\n"; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

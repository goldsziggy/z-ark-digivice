#pragma once

#include <cstddef>
#include <cstdint>

namespace digivice::net {

// Deliberately bounded and independent of the SDK, sockets, game state and heap.
struct Config {
    char ssid[33]{};
    char password[65]{};
    char endpoint[193]{}; // Empty = Wi-Fi only; otherwise origin, never a URL path.
    bool allowPrivateHttp = false;
};
enum class ConfigError { None, Ssid, Password, Endpoint, PrivateHttpDisabled };
ConfigError validateConfig(const Config& config, bool allowPrivateHttpBuild = false);
ConfigError validateEndpoint(const char* endpoint, bool allowPrivateHttp = false);
const char* configErrorText(ConfigError error); // Static text; never echoes input.

constexpr std::size_t kConfigBytes = 304;
// v1 configured origins remain byte-for-byte compatible. Wi-Fi-only saves use
// v2, which this reader accepts; old firmware leaves that future record untouched.
struct ConfigSnapshot { std::uint8_t bytes[kConfigBytes]{}; };
enum class ConfigRead { Ok, Corrupt, Unsupported };
bool encodeConfig(const Config& config, ConfigSnapshot& output, bool allowPrivateHttpBuild = false);
ConfigRead decodeConfig(const std::uint8_t* bytes, std::size_t length, Config& output,
                        bool allowPrivateHttpBuild = false);

// Online refers only to the configured service, never general Internet access.
enum class State { Unconfigured, Joining, LocalOnly, Online, Backoff };
enum class CommandType { None, Connect, Disconnect, ProbeService };
struct Command { CommandType type = CommandType::None; std::uint32_t token = 0; };
struct Status {
    State state = State::Unconfigured;
    bool configured = false;
    bool hasIp = false;
    bool serviceReachable = false; // Only a validated service response sets this.
    bool probePending = false;
    bool retriesExhausted = false;
    bool paused = false;
    std::uint8_t joinAttempts = 0;
    std::uint32_t generation = 0;
    std::uint64_t deadlineMs = 0;
};
constexpr std::uint8_t kMaxJoinAttempts = 6;
constexpr std::uint64_t kJoinTimeoutMs = 15000;
constexpr std::uint64_t kProbeTimeoutMs = 8000;
constexpr std::uint64_t kServiceIntervalMs = 60000;
constexpr std::uint64_t kSlowRetryMs = 300000;

class Controller {
public:
    // Caller serializes calls and supplies monotonic milliseconds. No waits.
    void configure(bool configured, std::uint64_t nowMs, std::uint32_t jitterSeed = 1);
    void retry(std::uint64_t nowMs); // Explicitly begin a fresh finite join cycle.
    void pause(bool paused, std::uint64_t nowMs);
    Command tick(std::uint64_t nowMs);
    void gotIp(std::uint32_t generation, std::uint64_t nowMs);
    void disconnected(std::uint32_t generation, std::uint64_t nowMs);
    void serviceResult(std::uint32_t token, bool reachable, std::uint64_t nowMs);
    const Status& status() const { return status_; }
private:
    Status status_{};
    std::uint32_t random_ = 1;
    std::uint32_t token_ = 0;
    bool connectDue_ = false;
    bool disconnectDue_ = false;
    void invalidateProbe();
    void invalidateConnection();
    void failedJoin(std::uint64_t nowMs);
};
const char* stateName(State state);

} // namespace digivice::net

#include "network.hpp"

#include <cstring>
#include <limits>

namespace digivice::net {
namespace {
std::size_t boundedLength(const char* text, std::size_t capacity) {
    if (!text) return capacity;
    std::size_t n = 0;
    while (n < capacity && text[n]) ++n;
    return n;
}
bool alphaNum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
bool hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool ipv4(const char* start, std::size_t length, unsigned (&octets)[4]) {
    std::size_t pos = 0;
    for (unsigned part = 0; part < 4; ++part) {
        const auto begin = pos; unsigned value = 0;
        while (pos < length && start[pos] >= '0' && start[pos] <= '9') {
            value = value * 10 + static_cast<unsigned>(start[pos++] - '0');
            if (pos - begin > 3 || value > 255) return false;
        }
        if (pos == begin || (pos - begin > 1 && start[begin] == '0')) return false;
        octets[part] = value;
        if (part < 3 && (pos >= length || start[pos++] != '.')) return false;
    }
    return pos == length;
}
std::uint32_t crc(const std::uint8_t* data, std::size_t length) {
    std::uint32_t value = 0xffffffffU;
    for (std::size_t i = 0; i < length; ++i) {
        value ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
    }
    return ~value;
}
std::uint64_t after(std::uint64_t now, std::uint64_t delay) {
    return now > std::numeric_limits<std::uint64_t>::max() - delay ?
        std::numeric_limits<std::uint64_t>::max() : now + delay;
}
} // namespace

ConfigError validateEndpoint(const char* endpoint, bool allowPrivateHttp) {
    const auto length = boundedLength(endpoint, 193);
    if (!length || length > 192) return ConfigError::Endpoint;
    bool insecure = false; std::size_t host = 0;
    if (length > 8 && std::memcmp(endpoint, "https://", 8) == 0) host = 8;
    else if (length > 7 && std::memcmp(endpoint, "http://", 7) == 0) { host = 7; insecure = true; }
    else return ConfigError::Endpoint;
    auto end = length;
    if (endpoint[end - 1] == '/') --end;
    auto hostEnd = host;
    while (hostEnd < end && endpoint[hostEnd] != ':') ++hostEnd;
    if (hostEnd == host) return ConfigError::Endpoint;
    // DNS names are admitted only for HTTPS; private HTTP uses an unambiguous
    // RFC1918 IPv4 literal so DNS cannot resolve the exception to a public IP.
    bool allNumeric = true;
    std::size_t label = host;
    for (auto i = host; i < hostEnd; ++i) {
        const char c = endpoint[i];
        if (!alphaNum(c) && c != '-' && c != '.') return ConfigError::Endpoint;
        if ((c < '0' || c > '9') && c != '.') allNumeric = false;
        if (c == '.') {
            if (i == label || i - label > 63 || endpoint[label] == '-' || endpoint[i - 1] == '-') return ConfigError::Endpoint;
            label = i + 1;
        }
    }
    if (label == hostEnd || hostEnd - label > 63 || endpoint[label] == '-' || endpoint[hostEnd - 1] == '-') return ConfigError::Endpoint;
    unsigned octets[4]{};
    const bool literal = ipv4(endpoint + host, hostEnd - host, octets);
    if (allNumeric && !literal) return ConfigError::Endpoint;
    if (hostEnd < end) {
        unsigned port = 0;
        if (++hostEnd == end) return ConfigError::Endpoint;
        if (end - hostEnd > 5) return ConfigError::Endpoint;
        for (auto i = hostEnd; i < end; ++i) {
            if (endpoint[i] < '0' || endpoint[i] > '9') return ConfigError::Endpoint;
            port = port * 10 + static_cast<unsigned>(endpoint[i] - '0');
        }
        if (!port || port > 65535) return ConfigError::Endpoint;
    }
    if (insecure) {
        if (!allowPrivateHttp) return ConfigError::PrivateHttpDisabled;
        const bool privateIp = literal && (octets[0] == 10 ||
            (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
            (octets[0] == 192 && octets[1] == 168));
        if (!privateIp) return ConfigError::Endpoint;
    }
    return ConfigError::None;
}

ConfigError validateConfig(const Config& config, bool allowPrivateHttpBuild) {
    const auto ssid = boundedLength(config.ssid, sizeof(config.ssid));
    if (!ssid || ssid > 32) return ConfigError::Ssid;
    for (std::size_t i = 0; i < ssid; ++i) {
        const auto c = static_cast<unsigned char>(config.ssid[i]);
        if (c < 32 || c == 127) return ConfigError::Ssid;
    }
    const auto password = boundedLength(config.password, sizeof(config.password));
    if (password < 8 || password > 64) return ConfigError::Password;
    for (std::size_t i = 0; i < password; ++i) {
        const auto c = static_cast<unsigned char>(config.password[i]);
        if (c < 32 || c > 126 || (password == 64 && !hex(config.password[i]))) return ConfigError::Password;
    }
    if (config.allowPrivateHttp && !allowPrivateHttpBuild) return ConfigError::PrivateHttpDisabled;
    if (!config.endpoint[0]) return config.allowPrivateHttp ? ConfigError::Endpoint : ConfigError::None;
    return validateEndpoint(config.endpoint, config.allowPrivateHttp && allowPrivateHttpBuild);
}

const char* configErrorText(ConfigError error) {
    switch (error) {
    case ConfigError::None: return "valid";
    case ConfigError::Ssid: return "SSID must contain 1..32 non-control bytes";
    case ConfigError::Password: return "WPA2 password must be 8..63 printable bytes or a 64-digit hex PSK";
    case ConfigError::Endpoint: return "service origin must be HTTPS, or explicitly enabled RFC1918 IPv4 HTTP";
    case ConfigError::PrivateHttpDisabled: return "private HTTP requires both build and configuration opt-in";
    }
    return "invalid network configuration";
}

bool encodeConfig(const Config& config, ConfigSnapshot& output, bool allowPrivateHttpBuild) {
    if (validateConfig(config, allowPrivateHttpBuild) != ConfigError::None) return false;
    ConfigSnapshot next{};
    std::memcpy(next.bytes, "DNET", 4); next.bytes[4] = config.endpoint[0] ? 1 : 2;
    next.bytes[5] = config.allowPrivateHttp ? 1 : 0;
    next.bytes[6] = static_cast<std::uint8_t>(std::strlen(config.ssid));
    next.bytes[7] = static_cast<std::uint8_t>(std::strlen(config.password));
    next.bytes[8] = static_cast<std::uint8_t>(std::strlen(config.endpoint));
    std::memcpy(next.bytes + 12, config.ssid, next.bytes[6]);
    std::memcpy(next.bytes + 44, config.password, next.bytes[7]);
    std::memcpy(next.bytes + 108, config.endpoint, next.bytes[8]);
    const auto checksum = crc(next.bytes, 300);
    for (unsigned i = 0; i < 4; ++i) next.bytes[300 + i] = static_cast<std::uint8_t>(checksum >> (8 * i));
    output = next; return true;
}

ConfigRead decodeConfig(const std::uint8_t* bytes, std::size_t length, Config& output, bool allowPrivateHttpBuild) {
    if (!bytes || length < 5 || std::memcmp(bytes, "DNET", 4)) return ConfigRead::Corrupt;
    if (bytes[4] != 1 && bytes[4] != 2) return ConfigRead::Unsupported;
    if (length != kConfigBytes || bytes[5] > 1 || bytes[6] > 32 || bytes[7] > 64 || bytes[8] > 192 ||
        bytes[9] || bytes[10] || bytes[11]) return ConfigRead::Corrupt;
    if ((bytes[4] == 1 && bytes[8] == 0) || (bytes[4] == 2 && bytes[8] != 0)) return ConfigRead::Corrupt;
    std::uint32_t expected = 0;
    for (unsigned i = 0; i < 4; ++i) expected |= static_cast<std::uint32_t>(bytes[300 + i]) << (8 * i);
    if (expected != crc(bytes, 300)) return ConfigRead::Corrupt;
    Config next{};
    next.allowPrivateHttp = bytes[5] != 0;
    std::memcpy(next.ssid, bytes + 12, bytes[6]);
    std::memcpy(next.password, bytes + 44, bytes[7]);
    std::memcpy(next.endpoint, bytes + 108, bytes[8]);
    if (validateConfig(next, allowPrivateHttpBuild) != ConfigError::None) return ConfigRead::Corrupt;
    ConfigSnapshot canonical{};
    if (!encodeConfig(next, canonical, allowPrivateHttpBuild) || std::memcmp(bytes, canonical.bytes, kConfigBytes)) return ConfigRead::Corrupt;
    output = next; return ConfigRead::Ok;
}

void Controller::invalidateProbe() {
    status_.probePending = false;
    if (++token_ == 0) ++token_;
}
void Controller::invalidateConnection() {
    if (++status_.generation == 0) ++status_.generation;
}
void Controller::configure(bool configured, std::uint64_t nowMs, std::uint32_t jitterSeed) {
    const bool wasConfigured = status_.configured;
    const auto generation = status_.generation;
    status_ = {}; status_.configured = configured;
    status_.generation = generation; invalidateConnection();
    random_ = jitterSeed ? jitterSeed : 1;
    invalidateProbe();
    disconnectDue_ = disconnectDue_ || wasConfigured;
    connectDue_ = configured;
    status_.state = configured ? State::LocalOnly : State::Unconfigured;
    status_.deadlineMs = nowMs;
}
void Controller::retry(std::uint64_t nowMs) {
    if (!status_.configured || status_.paused) return;
    invalidateProbe(); status_.serviceReachable = false;
    if (status_.hasIp) { status_.state = State::LocalOnly; status_.deadlineMs = nowMs; return; }
    status_.joinAttempts = 0; status_.retriesExhausted = false;
    disconnectDue_ = disconnectDue_ || status_.state == State::Joining;
    invalidateConnection();
    connectDue_ = true; status_.state = State::LocalOnly; status_.deadlineMs = nowMs;
}
void Controller::pause(bool paused, std::uint64_t nowMs) {
    if (status_.paused == paused) return;
    status_.paused = paused;
    if (!paused) { retry(nowMs); return; }
    disconnectDue_ = disconnectDue_ || status_.configured; connectDue_ = false;
    invalidateProbe(); invalidateConnection();
    status_.hasIp = false; status_.serviceReachable = false;
    status_.state = status_.configured ? State::LocalOnly : State::Unconfigured;
    status_.deadlineMs = 0;
}
void Controller::failedJoin(std::uint64_t nowMs) {
    invalidateProbe(); invalidateConnection(); status_.hasIp = false; status_.serviceReachable = false;
    if (status_.joinAttempts >= kMaxJoinAttempts) {
        status_.state = State::LocalOnly; status_.retriesExhausted = true; status_.deadlineMs = after(nowMs, kSlowRetryMs);
        return;
    }
    random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
    const unsigned shift = status_.joinAttempts ? status_.joinAttempts - 1 : 0;
    const std::uint64_t base = (1000ULL << shift) > 30000 ? 30000 : (1000ULL << shift);
    status_.state = State::Backoff;
    status_.deadlineMs = after(nowMs, base + random_ % 501U);
}
Command Controller::tick(std::uint64_t nowMs) {
    if (disconnectDue_) { disconnectDue_ = false; return {CommandType::Disconnect, 0}; }
    if (!status_.configured || status_.paused) return {};
    if (status_.retriesExhausted && nowMs >= status_.deadlineMs) {
        status_.joinAttempts = 0; status_.retriesExhausted = false; connectDue_ = true;
    }
    if (connectDue_ || (status_.state == State::Backoff && nowMs >= status_.deadlineMs)) {
        connectDue_ = false; ++status_.joinAttempts; invalidateConnection();
        status_.state = State::Joining; status_.deadlineMs = after(nowMs, kJoinTimeoutMs);
        return {CommandType::Connect, status_.generation};
    }
    if (status_.state == State::Joining && nowMs >= status_.deadlineMs) {
        failedJoin(nowMs); return {CommandType::Disconnect, 0};
    }
    if (status_.hasIp && nowMs >= status_.deadlineMs) {
        if (status_.probePending) {
            invalidateProbe(); status_.serviceReachable = false; status_.state = State::LocalOnly;
            status_.deadlineMs = after(nowMs, kServiceIntervalMs); return {};
        }
        invalidateProbe(); status_.probePending = true;
        status_.deadlineMs = after(nowMs, kProbeTimeoutMs);
        return {CommandType::ProbeService, token_};
    }
    return {};
}
void Controller::gotIp(std::uint32_t generation, std::uint64_t nowMs) {
    if (!status_.configured || status_.paused || generation != status_.generation ||
        status_.state != State::Joining || nowMs >= status_.deadlineMs) return;
    invalidateProbe(); status_.hasIp = true; status_.serviceReachable = false;
    status_.joinAttempts = 0; status_.retriesExhausted = false;
    status_.state = State::LocalOnly; status_.deadlineMs = nowMs;
}
void Controller::disconnected(std::uint32_t generation, std::uint64_t nowMs) {
    // Repeated disconnect notices cannot postpone an already scheduled retry.
    if (!status_.configured || status_.paused || generation != status_.generation || (!status_.hasIp && status_.state != State::Joining)) return;
    failedJoin(nowMs);
}
void Controller::serviceResult(std::uint32_t token, bool reachable, std::uint64_t nowMs) {
    if (!status_.hasIp || !status_.probePending || token != token_ || nowMs >= status_.deadlineMs) return;
    invalidateProbe(); status_.serviceReachable = reachable;
    status_.state = reachable ? State::Online : State::LocalOnly;
    status_.deadlineMs = after(nowMs, kServiceIntervalMs);
}
const char* stateName(State state) {
    switch (state) {
    case State::Unconfigured: return "unconfigured";
    case State::Joining: return "joining";
    case State::LocalOnly: return "local-only";
    case State::Online: return "online";
    case State::Backoff: return "backoff";
    }
    return "unconfigured";
}
} // namespace digivice::net

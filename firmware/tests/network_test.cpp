#include "network.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>

namespace {
unsigned checks = 0, failures = 0;
#define CHECK(expression) do { ++checks; if (!(expression)) { ++failures; std::printf("FAIL line%d: %s\n", __LINE__, #expression); } } while (false)
using namespace digivice::net;
Config example() {
    Config config{};
    std::strcpy(config.ssid, "Pocket hotspot");
    std::strcpy(config.password, "local-test-password");
    std::strcpy(config.endpoint, "https://example.com:443");
    return config;
}
void configuration() {
    auto config = example(); CHECK(validateConfig(config) == ConfigError::None);
    for (const auto* good : {"https://example.com", "https://10.0.0.1:8443/", "https://service.local", "https://a-b.example:65535", "https://0x7f000001/"}) {
        CHECK(validateEndpoint(good) == ConfigError::None);
    }
    for (const auto* bad : {"", "http://example.com", "https://user:pass@example.com", "https://example.com/path",
        "https://example.com?token=x", "https://example.com#fragment", "https://[::1]", "https://", "https://-bad.example",
        "https://a..b", "https://a:0", "https://a:65536", "https://a:bad", "https://example.com\\x", "https://127.01.2.3",
        "https://1", "https://example.com\r\nHost:evil"}) {
        CHECK(validateEndpoint(bad) != ConfigError::None);
    }
    for (const auto* good : {"http://10.1.2.3:8787", "http://172.16.0.1", "http://172.31.255.254/", "http://192.168.1.5"}) {
        CHECK(validateEndpoint(good, true) == ConfigError::None);
        CHECK(validateEndpoint(good, false) == ConfigError::PrivateHttpDisabled);
    }
    for (const auto* bad : {"http://8.8.8.8", "http://127.0.0.1", "http://169.254.1.2", "http://172.15.0.1", "http://172.32.0.1",
        "http://localhost", "http://router.local", "http://192.168.01.2", "http://10.0.0.1.evil", "http://10.0.0.1@evil"}) {
        CHECK(validateEndpoint(bad, true) != ConfigError::None);
    }
    config.allowPrivateHttp = true; std::strcpy(config.endpoint, "http://192.168.1.4:8787");
    CHECK(validateConfig(config) == ConfigError::PrivateHttpDisabled);
    CHECK(validateConfig(config, true) == ConfigError::None);
    config = example(); std::memset(config.ssid, 'x', sizeof(config.ssid)); CHECK(validateConfig(config) == ConfigError::Ssid);
    config = example(); config.ssid[1] = '\n'; CHECK(validateConfig(config) == ConfigError::Ssid);
    config = example(); std::strcpy(config.password, "short"); CHECK(validateConfig(config) == ConfigError::Password);
    config = example(); std::memset(config.password, 'a', 64); CHECK(validateConfig(config) == ConfigError::None);
    config.password[63] = 'x'; CHECK(validateConfig(config) == ConfigError::Password);
    config = example(); std::memset(config.endpoint, 'a', sizeof(config.endpoint)); CHECK(validateConfig(config) == ConfigError::Endpoint);
}
void storage() {
    Config original = example(), restored{}; ConfigSnapshot bytes{};
    CHECK(encodeConfig(original, bytes)); CHECK(sizeof(bytes) == 304);
    CHECK(decodeConfig(bytes.bytes, sizeof(bytes), restored) == ConfigRead::Ok);
    CHECK(std::strcmp(original.ssid, restored.ssid) == 0 && std::strcmp(original.password, restored.password) == 0 &&
          std::strcmp(original.endpoint, restored.endpoint) == 0);
    for (std::size_t i = 0; i < sizeof(bytes); ++i) {
        auto damaged = bytes; damaged.bytes[i] ^= 0x20;
        Config untouched = example();
        CHECK(decodeConfig(damaged.bytes, sizeof(damaged), untouched) != ConfigRead::Ok);
        CHECK(std::strcmp(untouched.password, original.password) == 0);
    }
    CHECK(decodeConfig(bytes.bytes, sizeof(bytes) - 1, restored) == ConfigRead::Corrupt);
    bytes.bytes[4] = 3; CHECK(decodeConfig(bytes.bytes, sizeof(bytes), restored) == ConfigRead::Unsupported);
    CHECK(decodeConfig(nullptr, 0, restored) == ConfigRead::Corrupt);
    original.allowPrivateHttp = true; std::strcpy(original.endpoint, "http://10.0.0.1");
    CHECK(encodeConfig(original, bytes, true));
    CHECK(decodeConfig(bytes.bytes, sizeof(bytes), restored, false) != ConfigRead::Ok);
    CHECK(decodeConfig(bytes.bytes, sizeof(bytes), restored, true) == ConfigRead::Ok);
    // Wi-Fi without a service uses explicit v2, never a fabricated origin.
    original = example(); original.endpoint[0] = 0;
    CHECK(validateConfig(original) == ConfigError::None);
    CHECK(encodeConfig(original, bytes) && bytes.bytes[4] == 2);
    CHECK(decodeConfig(bytes.bytes, sizeof(bytes), restored) == ConfigRead::Ok);
    CHECK(restored.endpoint[0] == 0 && !restored.allowPrivateHttp);
    CHECK(std::strcmp(restored.password, original.password) == 0);
    original.allowPrivateHttp = true;
    CHECK(validateConfig(original, true) == ConfigError::Endpoint);
    original = example(); CHECK(encodeConfig(original, bytes) && bytes.bytes[4] == 1);
}
void lifecycle() {
    Controller c; CHECK(c.status().state == State::Unconfigured);
    CHECK(c.tick(0).type == CommandType::None); c.retry(1); CHECK(c.tick(1).type == CommandType::None);
    c.configure(true, 10, 42); const auto join = c.tick(10); CHECK(join.type == CommandType::Connect);
    CHECK(c.status().state == State::Joining && c.status().joinAttempts == 1);
    c.gotIp(join.token + 1, 11); CHECK(!c.status().hasIp);
    c.gotIp(join.token, 12); CHECK(c.status().hasIp && c.status().state == State::LocalOnly);
    CHECK(!c.status().serviceReachable);
    const auto probe = c.tick(12); CHECK(probe.type == CommandType::ProbeService);
    c.serviceResult(probe.token + 1, true, 13); CHECK(!c.status().serviceReachable);
    c.serviceResult(probe.token, true, 14); CHECK(c.status().state == State::Online);
    c.disconnected(join.token, 15); CHECK(!c.status().hasIp && c.status().state == State::Backoff);
    const auto deadline = c.status().deadlineMs;
    c.disconnected(join.token, 16); CHECK(c.status().deadlineMs == deadline);
    c.serviceResult(probe.token, true, 16); CHECK(!c.status().serviceReachable);
    CHECK(c.tick(deadline - 1).type == CommandType::None);
    const auto second = c.tick(deadline); CHECK(second.type == CommandType::Connect && second.token != join.token);
    c.gotIp(join.token, deadline + 1); CHECK(!c.status().hasIp);
    c.disconnected(join.token, deadline + 1); CHECK(c.status().state == State::Joining);
    c.gotIp(second.token, deadline + 2); CHECK(c.status().hasIp);
    auto p = c.tick(deadline + 2); CHECK(p.type == CommandType::ProbeService);
    const auto expired = c.status().deadlineMs;
    c.serviceResult(p.token, true, expired); CHECK(!c.status().serviceReachable);
    CHECK(c.tick(expired).type == CommandType::None && !c.status().probePending);
    CHECK(c.status().deadlineMs == expired + kServiceIntervalMs);
    p = c.tick(c.status().deadlineMs); CHECK(p.type == CommandType::ProbeService);
    c.serviceResult(p.token, false, expired + kServiceIntervalMs + 1); CHECK(c.status().state == State::LocalOnly);
}
void retryAndPause() {
    Controller c, same; c.configure(true, 0, 999); same.configure(true, 0, 999);
    std::uint64_t now = 0;
    for (unsigned attempt = 1; attempt <= kMaxJoinAttempts; ++attempt) {
        const auto command = c.tick(now); const auto repeat = same.tick(now);
        CHECK(command.type == CommandType::Connect && repeat.type == command.type);
        CHECK(c.status().joinAttempts == attempt);
        now += kJoinTimeoutMs;
        c.gotIp(command.token, now); CHECK(!c.status().hasIp);
        CHECK(c.tick(now).type == CommandType::Disconnect); CHECK(same.tick(now).type == CommandType::Disconnect);
        CHECK(c.status().deadlineMs == same.status().deadlineMs);
        if (attempt < kMaxJoinAttempts) {
            const auto base = 1000ULL << (attempt - 1);
            CHECK(c.status().state == State::Backoff && c.status().deadlineMs >= now + base && c.status().deadlineMs <= now + base + 500);
        }
        now = c.status().deadlineMs;
    }
    CHECK(c.status().state == State::LocalOnly && c.status().retriesExhausted);
    CHECK(c.tick(now - 1).type == CommandType::None);
    CHECK(c.tick(now).type == CommandType::Connect && c.status().joinAttempts == 1);
    c.pause(true, now + 1); c.pause(false, now + 2);
    CHECK(c.tick(now + 2).type == CommandType::Disconnect);
    CHECK(c.tick(now + 2).type == CommandType::Connect);
    c.pause(true, now + 3); CHECK(c.tick(now + 3).type == CommandType::Disconnect);
    CHECK(c.status().paused && c.tick(now + kSlowRetryMs * 2).type == CommandType::None);
    c.retry(now + 4); CHECK(c.status().paused);
    c.pause(false, now + 5); CHECK(c.tick(now + 5).type == CommandType::Connect);
    c.configure(false, now + 6); c.configure(true, now + 7);
    CHECK(c.tick(now + 7).type == CommandType::Disconnect);
    CHECK(c.tick(now + 7).type == CommandType::Connect);
    c.configure(false, now + 8); CHECK(c.tick(now + 8).type == CommandType::Disconnect);
    CHECK(c.tick(now + 9).type == CommandType::None && c.status().state == State::Unconfigured);
    c.configure(true, now + 10); CHECK(c.tick(now + 10).type == CommandType::Connect);
    c.configure(false, now + 11); c.pause(true, now + 11);
    CHECK(c.tick(now + 11).type == CommandType::Disconnect);
    c.configure(true, std::numeric_limits<std::uint64_t>::max() - 10);
    CHECK(c.tick(std::numeric_limits<std::uint64_t>::max() - 10).type == CommandType::Connect);
    CHECK(c.status().deadlineMs == std::numeric_limits<std::uint64_t>::max());
}
} // namespace
int main() {
    configuration(); storage(); lifecycle(); retryAndPause();
    std::printf("network: %u checks, %u failures; Controller=%zuB Config=%zuB Snapshot=%zuB\n", checks, failures,
                sizeof(digivice::net::Controller), sizeof(digivice::net::Config), sizeof(digivice::net::ConfigSnapshot));
    return failures ? 1 : 0;
}

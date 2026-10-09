#include "game.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define DEMO_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define DEMO_EXPORT
#endif

#ifndef DEMO_SOURCE_COMMIT
#define DEMO_SOURCE_COMMIT "171cda7e698cf2915b50aa46bc766d8d1d0ee50d"
#endif

// This adapter owns only a separate browser-demo State. All rules, validation,
// probabilities, catalogs and serialization come from the unchanged native core.
namespace {
using namespace digivice;
State state = newDevice(kDevelopmentSeed);
char stateJson[kJsonCapacity];
char traceJson[autobattle::kTraceJsonCapacity];
char reply[kJsonCapacity + autobattle::kTraceJsonCapacity + 1024];
char metadata[combat::kEvolutionGraphJsonCapacity];
char snapshotHex[kSnapshotSize * 2 + 1];
constexpr char digits[] = "0123456789abcdef";

void quote(const char* source, char* destination, std::size_t capacity) {
    std::size_t n = 0;
    for (const auto* p = source; *p && n + 7 < capacity; ++p) {
        const auto ch = static_cast<unsigned char>(*p);
        if (ch == '"' || ch == '\\') { destination[n++] = '\\'; destination[n++] = *p; }
        else if (ch < 32) {
            destination[n++] = '\\'; destination[n++] = 'u';
            destination[n++] = '0'; destination[n++] = '0';
            destination[n++] = digits[ch >> 4]; destination[n++] = digits[ch & 15];
        } else destination[n++] = *p;
    }
    destination[n] = '\0';
}

const char* response(const char* error = nullptr, const autobattle::Trace* trace = nullptr,
                     std::uint32_t demoSteps = 0) {
    if (!writeJson(state, stateJson, sizeof(stateJson)))
        return "{\"ok\":false,\"error\":\"Native state serialization failed\",\"state\":null,\"trace\":null}";
    if (trace && !autobattle::writeJson(*trace, traceJson, sizeof(traceJson)))
        return "{\"ok\":false,\"error\":\"Native trace serialization failed\",\"state\":null,\"trace\":null}";
    char escaped[512]; quote(error ? error : "", escaped, sizeof(escaped));
    const int length = std::snprintf(reply, sizeof(reply),
        "{\"ok\":%s,\"error\":%s%s%s,\"state\":%s,\"trace\":%s,"
        "\"sourceCommit\":\"%s\",\"rulesVersion\":%u,\"schemaVersion\":%u,\"demoSteps\":%u}",
        error ? "false" : "true", error ? "\"" : "", error ? escaped : "null", error ? "\"" : "",
        stateJson, trace ? traceJson : "null", DEMO_SOURCE_COMMIT,
        static_cast<unsigned>(kRulesVersion), static_cast<unsigned>(kSchemaVersion),
        static_cast<unsigned>(demoSteps));
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(reply))
        return "{\"ok\":false,\"error\":\"Native reply buffer exhausted\",\"state\":null,\"trace\":null}";
    return reply;
}

bool allowed(Action action) {
    switch (action) {
    case Action::Feed: case Action::Play: case Action::Rest: case Action::Card:
    case Action::Attack: case Action::Heavy: case Action::Magic: case Action::Hatch:
    case Action::Mode: case Action::Select: case Action::Evolve: case Action::Release:
    case Action::RingCapture: case Action::PartyAdd: case Action::PartyRemove:
        return true;
    default: return false;
    }
}

int nibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

const char* metadataError() { return "{\"ok\":false,\"error\":\"Invalid native catalog request\"}"; }
}

extern "C" {
DEMO_EXPORT const char* demo_state() { return response(); }

DEMO_EXPORT const char* demo_reset(std::uint32_t seed) {
    state = digivice::newDevice(seed);
    return response();
}

DEMO_EXPORT const char* demo_command(const char* command, std::uint32_t value) {
    using namespace digivice;
    if (!command) return response("A demo command is required");
    // The convenience button supplies synthetic Explore events, stopping at the
    // native encounter boundary. No enemy/seed/HP/XP/RNG field is injected.
    if (std::strcmp(command, "demo-encounter") == 0) {
        if (value) return response("Demo encounter does not take a value");
        if (state.phase != Phase::Home) return response(errorText(Error::WrongPhase));
        State candidate = state;
        std::uint32_t steps = 0;
        while (candidate.phase == Phase::Home && steps < 1000) {
            auto count = encounterStepsRemaining(candidate);
            if (!count) count = 1; // Let the core initialize its pacing target.
            if (count > 1000 - steps) count = 1000 - steps;
            const auto error = apply(candidate, Action::Explore, count);
            if (error != Error::None) return response(errorText(error));
            steps += count;
        }
        if (candidate.phase != Phase::Encounter)
            return response("No encounter reached within the bounded demo step budget");
        state = candidate;
        return response(nullptr, nullptr, steps);
    }
    if (std::strcmp(command, "auto-fight") == 0 || std::strcmp(command, "auto-resume") == 0) {
        if (value) return response("Auto battle commands do not take a value");
        autobattle::Trace trace;
        const auto error = std::strcmp(command, "auto-fight") == 0
            ? applyAutoFight(state, &trace) : applyAutoResume(state, &trace);
        return error == Error::None ? response(nullptr, &trace) : response(errorText(error));
    }
    Action action;
    if (!parseAction(command, action) || !allowed(action))
        return response("That command is not available in this browser demo");
    const auto error = apply(state, action, value);
    return error == Error::None ? response() : response(errorText(error));
}

// Hex contains the core's canonical little-endian snapshot and CRC, never native
// struct padding, browser credentials, or a device save from outside this demo.
DEMO_EXPORT const char* demo_snapshot() {
    digivice::Snapshot snapshot;
    if (!digivice::encodeSnapshot(state, snapshot)) return "";
    for (std::size_t i = 0; i < sizeof(snapshot.bytes); ++i) {
        snapshotHex[i * 2] = digits[snapshot.bytes[i] >> 4];
        snapshotHex[i * 2 + 1] = digits[snapshot.bytes[i] & 15];
    }
    snapshotHex[sizeof(snapshot.bytes) * 2] = '\0';
    return snapshotHex;
}

DEMO_EXPORT const char* demo_load(const char* hex) {
    using namespace digivice;
    if (!hex) return response("A demo snapshot is required");
    // This versioned demo accepts current snapshots only; the product's migration
    // surface and physical-device save import are deliberately not exposed.
    const auto length = std::strlen(hex);
    if (length != kSnapshotSize * 2) return response("Invalid demo snapshot length");
    Snapshot snapshot;
    for (std::size_t i = 0; i < sizeof(snapshot.bytes); ++i) {
        const int high = nibble(hex[i * 2]), low = nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return response("Invalid demo snapshot encoding");
        snapshot.bytes[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    State candidate;
    const auto status = decodeSnapshot(snapshot.bytes, sizeof(snapshot.bytes), candidate);
    if (status != SnapshotStatus::Ok) return response(snapshotStatusText(status));
    state = candidate;
    return response();
}

DEMO_EXPORT const char* demo_starters() {
    return digivice::combat::writeStarterJson(metadata, sizeof(metadata)) ? metadata : metadataError();
}

DEMO_EXPORT const char* demo_form(std::uint32_t id) {
    return digivice::combat::writeFormCatalogJson(id, metadata, sizeof(metadata)) ? metadata : metadataError();
}

DEMO_EXPORT const char* demo_catalog(std::uint32_t offset, std::uint32_t limit) {
    return digivice::combat::writeCatalogPageJson(offset, limit, metadata, sizeof(metadata)) ? metadata : metadataError();
}

DEMO_EXPORT const char* demo_evolutions(std::uint32_t id, std::uint32_t offset, std::uint32_t limit) {
    return digivice::combat::writeEvolutionGraphJson(id, offset, limit, metadata, sizeof(metadata)) ? metadata : metadataError();
}
}

#ifdef DIGIVICE_DEMO_NATIVE
#include <charconv>
#include <system_error>
namespace {
bool number(const char* text, std::uint32_t& output) {
    const auto* end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}
}
// Native parity harness: one command per line, one response per line.
// Commands match the C ABI; "snapshot" alone returns the raw hex string.
int main() {
    char line[16384];
    while (std::fgets(line, sizeof(line), stdin)) {
        char* tokens[5]{}; std::size_t count = 0;
        for (char* token = std::strtok(line, " \t\r\n"); token && count < 5; token = std::strtok(nullptr, " \t\r\n")) tokens[count++] = token;
        if (!count) continue;
        std::uint32_t values[3]{};
        const bool load = std::strcmp(tokens[0], "load") == 0;
        bool valid = count <= 4;
        for (std::size_t i = 1; valid && !load && i < count; ++i) valid = number(tokens[i], values[i - 1]);
        const char* result = nullptr;
        if (!valid) result = response("Invalid native demo input");
        else if (std::strcmp(tokens[0], "state") == 0 && count == 1) result = demo_state();
        else if (std::strcmp(tokens[0], "reset") == 0 && count == 2) result = demo_reset(values[0]);
        else if (std::strcmp(tokens[0], "snapshot") == 0 && count == 1) result = demo_snapshot();
        else if (load && count == 2) result = demo_load(tokens[1]);
        else if (std::strcmp(tokens[0], "starters") == 0 && count == 1) result = demo_starters();
        else if (std::strcmp(tokens[0], "form") == 0 && count == 2) result = demo_form(values[0]);
        else if (std::strcmp(tokens[0], "catalog") == 0 && count == 3) result = demo_catalog(values[0], values[1]);
        else if (std::strcmp(tokens[0], "evolutions") == 0 && count == 4) result = demo_evolutions(values[0], values[1], values[2]);
        else if (count <= 2) result = demo_command(tokens[0], values[0]);
        else result = response("Invalid native demo input");
        std::puts(result); std::fflush(stdout);
    }
}
#endif

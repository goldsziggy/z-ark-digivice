#include "practice_battle.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include <charconv>
#include <cstdio>
#include <cstring>

namespace {
bool number(const char* text, std::uint32_t& value) {
    const auto* end = text + std::strlen(text);
    const auto result = std::from_chars(text, end, value);
    return text != end && result.ec == std::errc{} && result.ptr == end;
}
bool species(const char* text, std::uint32_t& value) {
    value=digivice::forms::lineageFromSlug(text);
    return value!=0;
}
int fail(const char* message) { std::fprintf(stderr, "%s\n", message); return 2; }
constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int base64Value(char ch) {
    if (ch == '\0') return -1;
    const auto* found = std::strchr(kBase64, ch);
    return found ? static_cast<int>(found - kBase64) : -1;
}
bool decodeBase64(const char* text, std::uint8_t* bytes, std::size_t capacity, std::size_t& size) {
    const auto length = std::strlen(text);
    size = 0;
    if (!length || length % 4 || length > ((capacity + 2) / 3) * 4) return false;
    for (std::size_t i = 0; i < length; i += 4) {
        const bool pad2 = text[i + 2] == '=';
        const bool pad3 = text[i + 3] == '=';
        const int a = base64Value(text[i]), b = base64Value(text[i + 1]);
        const int c = pad2 ? 0 : base64Value(text[i + 2]);
        const int d = pad3 ? 0 : base64Value(text[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad3) ||
            ((pad2 || pad3) && i + 4 != length) || (pad2 && (b & 15)) || (pad3 && (c & 3)))
            return false;
        const std::size_t count = pad2 ? 1 : pad3 ? 2 : 3;
        if (size + count > capacity) return false;
        const auto value = (static_cast<std::uint32_t>(a) << 18) | (static_cast<std::uint32_t>(b) << 12) |
                           (static_cast<std::uint32_t>(c) << 6) | static_cast<std::uint32_t>(d);
        bytes[size++] = static_cast<std::uint8_t>(value >> 16);
        if (count >= 2) bytes[size++] = static_cast<std::uint8_t>(value >> 8);
        if (count == 3) bytes[size++] = static_cast<std::uint8_t>(value);
    }
    return true;
}
void encodeBase64(const digivice::practice::Snapshot& snapshot, std::size_t length, char* output) {
    std::size_t used = 0;
    for (std::size_t i = 0; i < length; i += 3) {
        const auto left = length - i;
        const auto value = (static_cast<std::uint32_t>(snapshot.bytes[i]) << 16) |
            (left > 1 ? static_cast<std::uint32_t>(snapshot.bytes[i + 1]) << 8 : 0) |
            (left > 2 ? static_cast<std::uint32_t>(snapshot.bytes[i + 2]) : 0);
        output[used++] = kBase64[(value >> 18) & 63];
        output[used++] = kBase64[(value >> 12) & 63];
        output[used++] = left > 1 ? kBase64[(value >> 6) & 63] : '=';
        output[used++] = left > 2 ? kBase64[value & 63] : '=';
    }
    output[used] = '\0';
}
} // namespace
int main(int argc, char** argv) {
    if(argc==5 && std::strcmp(argv[1],"--practice-rival")==0) {
        std::uint32_t seed,form,level;
        if(!number(argv[2],seed)||!number(argv[3],form)||!number(argv[4],level))return fail("invalid rival arguments");
        const auto id=digivice::practice::selectProductionRivalForm(seed,form,level);const auto* f=digivice::forms::find(id);
        if(!f)return fail("invalid rival profile");
        std::printf("{\"formId\":%u,\"species\":\"%s\",\"level\":%u}\n",static_cast<unsigned>(id),digivice::combat::speciesName(f->lineage),static_cast<unsigned>(level));return 0;
    }
    namespace p = digivice::practice;
    if (argc == 2 && std::strcmp(argv[1], "--budget") == 0) {
        std::printf("{\"stateBytes\":%zu,\"snapshotBytes\":%zu,\"jsonBufferBytes\":%zu}\n",
                    sizeof(p::State), sizeof(p::Snapshot), p::kJsonCapacity);
        return 0;
    }
    p::State state;
    bool automatic = false;
    const bool withForms = (argc == 9 || argc == 10) && std::strcmp(argv[1], "--practice-start-forms") == 0;
    if (withForms || ((argc == 7 || argc == 8) && std::strcmp(argv[1], "--practice-start") == 0)) {
        std::uint32_t seed, playerSpecies, playerLevel, enemySpecies, enemyLevel;
        const unsigned enemyIndex = withForms ? 6 : 5, modeIndex = withForms ? 9 : 7;
        if (!number(argv[2], seed) || !species(argv[3], playerSpecies) || !number(argv[4], playerLevel) ||
            !species(argv[enemyIndex], enemySpecies) || !number(argv[enemyIndex + 1], enemyLevel))
            return fail("start requires an unsigned seed and valid species/level profiles");
        auto playerForm = digivice::forms::initialForm(playerSpecies), enemyForm = digivice::forms::initialForm(enemySpecies);
        if (withForms && ((std::strcmp(argv[5], "initial") && !number(argv[5], playerForm)) ||
                         (std::strcmp(argv[8], "initial") && !number(argv[8], enemyForm)))) return fail("invalid form ID");
        state = p::newBattleWithForms(seed, playerSpecies, playerLevel, playerForm, enemySpecies, enemyLevel, enemyForm);
        if (!p::isValid(state)) return fail("form must belong to its lineage and allow this level");
        if (argc > static_cast<int>(modeIndex)) {
            if (std::strcmp(argv[modeIndex], "auto") == 0) automatic = true;
            else if (std::strcmp(argv[modeIndex], "tactical") != 0) return fail("mode must be tactical or auto");
        }
    } else {
        const bool read = argc == 3 && std::strcmp(argv[1], "--practice-read") == 0;
        const bool act = (argc == 4 || argc == 5) && std::strcmp(argv[1], "--practice-act") == 0;
        const bool migrate = argc == 5 && std::strcmp(argv[1], "--practice-migrate-v1") == 0;
        automatic = argc == 3 && std::strcmp(argv[1], "--practice-auto") == 0;
        if (!read && !act && !migrate && !automatic) return fail("usage: digivice-battle --practice-start-forms <seed> <playerSpecies> <playerLevel> <formId|initial> <enemySpecies> <enemyLevel> <formId|initial> [tactical|auto] | --practice-start <seed> <playerSpecies> <playerLevel> <enemySpecies> <enemyLevel> [tactical|auto] | --practice-auto <initialSnapshotBase64> | --practice-read <snapshotBase64> | --practice-act <snapshotBase64> <action> [value] | --practice-migrate-v1 <snapshotBase64> <playerSpecies> <playerLevel>");
        std::uint8_t bytes[p::kSnapshotSize]; std::size_t length = 0;
        if (!decodeBase64(argv[2], bytes, sizeof(bytes), length)) return fail("invalid practice snapshot encoding");
        if (migrate) {
            std::uint32_t playerSpecies, playerLevel;
            if (!species(argv[3], playerSpecies) || !number(argv[4], playerLevel) || !p::migrateV1(bytes, length, playerSpecies, playerLevel, state))
                return fail("legacy practice snapshot/profile mismatch or invalid snapshot");
        } else if (!p::decodeSnapshot(bytes, length, state))
            return fail("invalid or unsupported practice snapshot");
        if (act) {
            std::uint32_t value = 0;
            if (argc == 5 && !number(argv[4], value)) return fail("value must be an unsigned 32-bit integer");
            const auto error = p::apply(state, argv[3], value);
            if (error != p::Error::None) return fail(p::errorText(error));
        }
    }
    char initialEncoded[((p::kSnapshotSize + 2) / 3) * 4 + 1]{};
    digivice::autobattle::Trace trace{};
    if (automatic) {
        p::Snapshot initial;
        if (!p::encodeSnapshot(state, initial)) return fail("invalid auto initial state");
        encodeBase64(initial, p::snapshotSize(state), initialEncoded);
        if (p::runAuto(state, trace) != p::Error::None)
            return fail("auto requires an untouched full-health practice start");
    }
    p::Snapshot snapshot;
    char json[p::kJsonCapacity];
    char encoded[((p::kSnapshotSize + 2) / 3) * 4 + 1];
    if (!p::encodeSnapshot(state, snapshot) || !p::writePublicJson(state, json, sizeof(json)))
        return fail("failed to serialize practice state");
    encodeBase64(snapshot, p::snapshotSize(state), encoded);
    if (automatic) {
        char traceJson[digivice::autobattle::kTraceJsonCapacity];
        if (!digivice::autobattle::writeJson(trace, traceJson, sizeof(traceJson))) return fail("failed to serialize auto trace");
        std::printf("{\"state\":%s,\"snapshotBase64\":\"%s\",\"mode\":\"auto\",\"initialSnapshotBase64\":\"%s\",\"trace\":%s}\n",
                    json, encoded, initialEncoded, traceJson);
    } else std::printf("{\"state\":%s,\"snapshotBase64\":\"%s\"}\n", json, encoded);
    return 0;
}

#include "game.hpp"
#include "capture_ring.hpp"
#include "combat.hpp"
#include "legacy_v1.hpp"
#include "legacy_v2.hpp"
#include "legacy_v3.hpp"
#include "legacy_v4.hpp"
#include "legacy_v5.hpp"
#include "legacy_v6.hpp"
#include "legacy_v7.hpp"
#include "legacy_v8.hpp"
#include "legacy_v9.hpp"
#include "legacy_v10.hpp"
#include "legacy_v11.hpp"
#include "legacy_v12.hpp"
#include "legacy_v13.hpp"
#include "legacy_v14.hpp"
#include "legacy_v15.hpp"
#include "legacy_v16.hpp"
#include "legacy_v17.hpp"
#include "legacy_v18.hpp"
#include "forms.hpp"

#include <charconv>
#include <cstdio>
#include <cstring>

namespace {
bool number(const char* first, const char* last, std::uint32_t& value) {
    if (first == last) return false;
    const auto parsed = std::from_chars(first, last, value);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}
bool whitespace(char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; }
int fail(std::uint32_t line, const char* message) {
    if (line) std::fprintf(stderr, "line %u: %s\n", static_cast<unsigned>(line), message);
    else std::fprintf(stderr, "%s\n", message);
    return 2;
}
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
void encodeBase64(const digivice::Snapshot& snapshot, char* output) {
    std::size_t used = 0;
    for (std::size_t i = 0; i < sizeof(snapshot.bytes); i += 3) {
        const auto left = sizeof(snapshot.bytes) - i;
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
    if(argc==2 && std::strcmp(argv[1],"--capture-ring-contract")==0) {
        using namespace digivice::capturering;
        std::printf("{\"inputVersion\":1,\"action\":\"ring-capture\",\"cycleMs\":%u,\"factors\":{\"red\":%u,\"orange\":%u,\"green\":%u}}\n",
            kCycleMs,factorPercent(Grade::Red),factorPercent(Grade::Orange),factorPercent(Grade::Green));
        return 0;
    }
    if(argc==5 && std::strcmp(argv[1],"--capture-ring-sample")==0) {
        using namespace digivice::capturering;
        std::uint32_t phase=0,form=0,base=0;
        if(!number(argv[2],argv[2]+std::strlen(argv[2]),phase) || phase>=kCycleMs ||
           !number(argv[3],argv[3]+std::strlen(argv[3]),form) ||
           !number(argv[4],argv[4]+std::strlen(argv[4]),base) || base>90)
            return fail(0,"capture ring needs phase0..2399, uint32 form, and base odds0..90");
        const auto ring=sample(phase,form);
        std::printf("{\"phaseMs\":%u,\"radiusQ8\":%u,\"targetRadius\":%u,\"grade\":\"%s\",\"factorPercent\":%u,\"chance\":%u}\n",
            ring.phaseMs,static_cast<unsigned>(ring.radiusQ8),static_cast<unsigned>(ring.targetRadius),gradeName(ring.grade),
            factorPercent(ring.grade),chanceForGrade(base,ring.grade));
        return 0;
    }
    if(argc==3 && std::strcmp(argv[1],"--flick-trajectory")==0) {
        std::uint32_t value=0;
        digivice::FlickTrajectory trajectory;
        if(!number(argv[2],argv[2]+std::strlen(argv[2]),value) || !digivice::decodeFlick(value,trajectory))
            return fail(0,"flick value must be an integer from 0 through 82175");
        std::printf("{\"inputVersion\":%u,\"landingX\":%d,\"landingY\":%d,\"hit\":%s}\n",
            static_cast<unsigned>(digivice::kFlickInputVersion),static_cast<int>(trajectory.landingX),
            static_cast<int>(trajectory.landingY),trajectory.hit?"true":"false");
        return 0;
    }
    if((argc==4||argc==5) && std::strcmp(argv[1],"--evolution-graph")==0) {
        std::uint32_t id=0,offset=0,limit=16;
        if(!number(argv[2],argv[2]+std::strlen(argv[2]),id)||!number(argv[3],argv[3]+std::strlen(argv[3]),offset)||
           (argc==5&&!number(argv[4],argv[4]+std::strlen(argv[4]),limit))) return fail(0,"invalid evolution graph bounds");
        char json[digivice::combat::kEvolutionGraphJsonCapacity];
        if(!digivice::combat::writeEvolutionGraphJson(id,offset,limit,json,sizeof(json))) return fail(0,"invalid evolution graph page");
        std::puts(json); return 0;
    }
    if((argc==3||argc==4) && std::strcmp(argv[1],"--catalog-page")==0) {
        std::uint32_t offset=0,limit=16;
        if(!number(argv[2],argv[2]+std::strlen(argv[2]),offset) || (argc==4&&!number(argv[3],argv[3]+std::strlen(argv[3]),limit))) return fail(0,"invalid page bounds");
        char json[digivice::combat::kCatalogPageJsonCapacity];
        if(!digivice::combat::writeCatalogPageJson(offset,limit,json,sizeof(json)))return fail(0,"invalid catalog page");
        std::puts(json);return 0;
    }
    if(argc==3 && std::strcmp(argv[1],"--form")==0) {
        std::uint32_t id=0;char json[digivice::combat::kFormCatalogJsonCapacity];
        if(!number(argv[2],argv[2]+std::strlen(argv[2]),id)||!digivice::combat::writeFormCatalogJson(id,json,sizeof(json)))return fail(0,"unknown form");
        std::puts(json);return 0;
    }
    if(argc==3 && std::strcmp(argv[1],"--evolutions")==0) {
        const auto species=digivice::forms::lineageFromSlug(argv[2]);
        char catalog[digivice::combat::kCatalogJsonCapacity];
        if(!digivice::combat::writeEvolutionJson(species,catalog,sizeof(catalog))) return fail(0,"unknown evolution lineage");
        std::puts(catalog); return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--starters") == 0) {
        char catalog[digivice::combat::kStarterJsonCapacity];
        if (!digivice::combat::writeStarterJson(catalog, sizeof(catalog))) return fail(0, "failed to serialize starters");
        std::puts(catalog);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--roster") == 0) {
        char catalog[digivice::combat::kCatalogJsonCapacity];
        if (!digivice::combat::writeCatalogJson(catalog, sizeof(catalog))) return fail(0, "failed to serialize roster");
        std::puts(catalog);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--budget") == 0) {
        std::printf("{\"stateBytes\":%zu,\"snapshotBytes\":%zu,\"jsonBufferBytes\":%zu,"
                    "\"maxReplayEvents\":%u,\"coreHeapAllocations\":0,\"schemaVersion\":%u,\"rulesVersion\":%u,\"collectionCapacity\":%zu,\"partyCapacity\":%zu}\n",
                    sizeof(digivice::State), sizeof(digivice::Snapshot),
                    digivice::kJsonCapacity, static_cast<unsigned>(digivice::kMaxReplayEvents),
                    static_cast<unsigned>(digivice::kSchemaVersion), static_cast<unsigned>(digivice::kRulesVersion), digivice::kCollectionCapacity, digivice::kPartyCapacity);
        return 0;
    }
    const bool frozen8Trace = argc==3 && (std::strcmp(argv[1],"--replay-v8-trace")==0 ||
        std::strcmp(argv[1],"--replay-v8-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v8-snapshot-trace")==0);
    const bool frozen9Trace = argc==3 && (std::strcmp(argv[1],"--replay-v9-trace")==0 ||
        std::strcmp(argv[1],"--replay-v9-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v9-snapshot-trace")==0);
    const bool frozen10Trace = argc==3 && (std::strcmp(argv[1],"--replay-v10-trace")==0 ||
        std::strcmp(argv[1],"--replay-v10-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v10-snapshot-trace")==0);
    const bool frozen11Trace = argc==3 && (std::strcmp(argv[1],"--replay-v11-trace")==0 ||
        std::strcmp(argv[1],"--replay-v11-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v11-snapshot-trace")==0);
    const bool frozen12Trace = argc==3 && (std::strcmp(argv[1],"--replay-v12-trace")==0 ||
        std::strcmp(argv[1],"--replay-v12-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v12-snapshot-trace")==0);
    const bool frozen13Trace = argc==3 && (std::strcmp(argv[1],"--replay-v13-trace")==0 ||
        std::strcmp(argv[1],"--replay-v13-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v13-snapshot-trace")==0);
    const bool frozen14Trace = argc==3 && (std::strcmp(argv[1],"--replay-v14-trace")==0 ||
        std::strcmp(argv[1],"--replay-v14-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v14-snapshot-trace")==0);
    const bool frozen15Trace = argc==3 && (std::strcmp(argv[1],"--replay-v15-trace")==0 ||
        std::strcmp(argv[1],"--replay-v15-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v15-snapshot-trace")==0);
    const bool frozen16Trace = argc==3 && (std::strcmp(argv[1],"--replay-v16-trace")==0 ||
        std::strcmp(argv[1],"--replay-v16-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v16-snapshot-trace")==0);
    const bool frozen17Trace = argc==3 && (std::strcmp(argv[1],"--replay-v17-trace")==0 ||
        std::strcmp(argv[1],"--replay-v17-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v17-snapshot-trace")==0);
    const bool frozen18Trace = argc==3 && (std::strcmp(argv[1],"--replay-v18-trace")==0 ||
        std::strcmp(argv[1],"--replay-v18-onboarding-trace")==0 || std::strcmp(argv[1],"--replay-v18-snapshot-trace")==0);
    const bool migrateV1 = argc == 3 && std::strcmp(argv[1], "--migrate-v1") == 0;
    const bool migrateV2 = argc == 3 && std::strcmp(argv[1], "--migrate-v2") == 0;
    const bool migrateSnapshotV2 = argc == 3 && std::strcmp(argv[1], "--migrate-v2-snapshot") == 0;
    const bool migrateV3=argc==3 && std::strcmp(argv[1],"--migrate-v3")==0;
    const bool migrateOnboardingV3=argc==3 && std::strcmp(argv[1],"--migrate-v3-onboarding")==0;
    const bool migrateSnapshotV3=argc==3 && std::strcmp(argv[1],"--migrate-v3-snapshot")==0;
    const bool migrateV4=argc==3 && std::strcmp(argv[1],"--migrate-v4")==0;
    const bool migrateOnboardingV4=argc==3 && std::strcmp(argv[1],"--migrate-v4-onboarding")==0;
    const bool migrateSnapshotV4=argc==3 && std::strcmp(argv[1],"--migrate-v4-snapshot")==0;
    const bool migrateV5=argc==3 && std::strcmp(argv[1],"--migrate-v5")==0;
    const bool migrateOnboardingV5=argc==3 && std::strcmp(argv[1],"--migrate-v5-onboarding")==0;
    const bool migrateSnapshotV5=argc==3 && std::strcmp(argv[1],"--migrate-v5-snapshot")==0;
    const bool migrateV6=argc==3 && std::strcmp(argv[1],"--migrate-v6")==0;
    const bool migrateOnboardingV6=argc==3 && std::strcmp(argv[1],"--migrate-v6-onboarding")==0;
    const bool migrateSnapshotV6=argc==3 && std::strcmp(argv[1],"--migrate-v6-snapshot")==0;
    const bool migrateV7=argc==3 && std::strcmp(argv[1],"--migrate-v7")==0;
    const bool migrateV8=argc==3 && (std::strcmp(argv[1],"--migrate-v8")==0 || std::strcmp(argv[1],"--replay-v8-trace")==0);
    const bool migrateV9=argc==3 && (std::strcmp(argv[1],"--migrate-v9")==0 || std::strcmp(argv[1],"--replay-v9-trace")==0);
    const bool migrateV10=argc==3 && (std::strcmp(argv[1],"--migrate-v10")==0 || std::strcmp(argv[1],"--replay-v10-trace")==0);
    const bool migrateV11=argc==3 && (std::strcmp(argv[1],"--migrate-v11")==0 || std::strcmp(argv[1],"--replay-v11-trace")==0);
    const bool migrateV12=argc==3 && (std::strcmp(argv[1],"--migrate-v12")==0 || std::strcmp(argv[1],"--replay-v12-trace")==0);
    const bool migrateV13=argc==3 && (std::strcmp(argv[1],"--migrate-v13")==0 || std::strcmp(argv[1],"--replay-v13-trace")==0);
    const bool migrateV14=argc==3 && (std::strcmp(argv[1],"--migrate-v14")==0 || std::strcmp(argv[1],"--replay-v14-trace")==0);
    const bool migrateV15=argc==3 && (std::strcmp(argv[1],"--migrate-v15")==0 || std::strcmp(argv[1],"--replay-v15-trace")==0);
    const bool migrateV16=argc==3 && (std::strcmp(argv[1],"--migrate-v16")==0 || std::strcmp(argv[1],"--replay-v16-trace")==0);
    const bool migrateV17=argc==3 && (std::strcmp(argv[1],"--migrate-v17")==0 || std::strcmp(argv[1],"--replay-v17-trace")==0);
    const bool migrateV18=argc==3 && (std::strcmp(argv[1],"--migrate-v18")==0 || std::strcmp(argv[1],"--replay-v18-trace")==0);
    const bool migrateOnboardingV7=argc==3 && std::strcmp(argv[1],"--migrate-v7-onboarding")==0;
    const bool migrateOnboardingV8=argc==3 && (std::strcmp(argv[1],"--migrate-v8-onboarding")==0 || std::strcmp(argv[1],"--replay-v8-onboarding-trace")==0);
    const bool migrateOnboardingV9=argc==3 && (std::strcmp(argv[1],"--migrate-v9-onboarding")==0 || std::strcmp(argv[1],"--replay-v9-onboarding-trace")==0);
    const bool migrateOnboardingV10=argc==3 && (std::strcmp(argv[1],"--migrate-v10-onboarding")==0 || std::strcmp(argv[1],"--replay-v10-onboarding-trace")==0);
    const bool migrateOnboardingV11=argc==3 && (std::strcmp(argv[1],"--migrate-v11-onboarding")==0 || std::strcmp(argv[1],"--replay-v11-onboarding-trace")==0);
    const bool migrateOnboardingV12=argc==3 && (std::strcmp(argv[1],"--migrate-v12-onboarding")==0 || std::strcmp(argv[1],"--replay-v12-onboarding-trace")==0);
    const bool migrateOnboardingV13=argc==3 && (std::strcmp(argv[1],"--migrate-v13-onboarding")==0 || std::strcmp(argv[1],"--replay-v13-onboarding-trace")==0);
    const bool migrateOnboardingV14=argc==3 && (std::strcmp(argv[1],"--migrate-v14-onboarding")==0 || std::strcmp(argv[1],"--replay-v14-onboarding-trace")==0);
    const bool migrateOnboardingV15=argc==3 && (std::strcmp(argv[1],"--migrate-v15-onboarding")==0 || std::strcmp(argv[1],"--replay-v15-onboarding-trace")==0);
    const bool migrateOnboardingV16=argc==3 && (std::strcmp(argv[1],"--migrate-v16-onboarding")==0 || std::strcmp(argv[1],"--replay-v16-onboarding-trace")==0);
    const bool migrateOnboardingV17=argc==3 && (std::strcmp(argv[1],"--migrate-v17-onboarding")==0 || std::strcmp(argv[1],"--replay-v17-onboarding-trace")==0);
    const bool migrateOnboardingV18=argc==3 && (std::strcmp(argv[1],"--migrate-v18-onboarding")==0 || std::strcmp(argv[1],"--replay-v18-onboarding-trace")==0);
    const bool migrateSnapshotV7=argc==3 && std::strcmp(argv[1],"--migrate-v7-snapshot")==0;
    const bool migrateSnapshotV8=argc==3 && (std::strcmp(argv[1],"--migrate-v8-snapshot")==0 || std::strcmp(argv[1],"--replay-v8-snapshot-trace")==0);
    const bool migrateSnapshotV9=argc==3 && (std::strcmp(argv[1],"--migrate-v9-snapshot")==0 || std::strcmp(argv[1],"--replay-v9-snapshot-trace")==0);
    const bool migrateSnapshotV10=argc==3 && (std::strcmp(argv[1],"--migrate-v10-snapshot")==0 || std::strcmp(argv[1],"--replay-v10-snapshot-trace")==0);
    const bool migrateSnapshotV11=argc==3 && (std::strcmp(argv[1],"--migrate-v11-snapshot")==0 || std::strcmp(argv[1],"--replay-v11-snapshot-trace")==0);
    const bool migrateSnapshotV12=argc==3 && (std::strcmp(argv[1],"--migrate-v12-snapshot")==0 || std::strcmp(argv[1],"--replay-v12-snapshot-trace")==0);
    const bool migrateSnapshotV13=argc==3 && (std::strcmp(argv[1],"--migrate-v13-snapshot")==0 || std::strcmp(argv[1],"--replay-v13-snapshot-trace")==0);
    const bool migrateSnapshotV14=argc==3 && (std::strcmp(argv[1],"--migrate-v14-snapshot")==0 || std::strcmp(argv[1],"--replay-v14-snapshot-trace")==0);
    const bool migrateSnapshotV15=argc==3 && (std::strcmp(argv[1],"--migrate-v15-snapshot")==0 || std::strcmp(argv[1],"--replay-v15-snapshot-trace")==0);
    const bool migrateSnapshotV16=argc==3 && (std::strcmp(argv[1],"--migrate-v16-snapshot")==0 || std::strcmp(argv[1],"--replay-v16-snapshot-trace")==0);
    const bool migrateSnapshotV17=argc==3 && (std::strcmp(argv[1],"--migrate-v17-snapshot")==0 || std::strcmp(argv[1],"--replay-v17-snapshot-trace")==0);
    const bool migrateSnapshotV18=argc==3 && (std::strcmp(argv[1],"--migrate-v18-snapshot")==0 || std::strcmp(argv[1],"--replay-v18-snapshot-trace")==0);
    const bool migrate = migrateV18 || migrateOnboardingV18 || migrateSnapshotV18 || migrateV17 || migrateOnboardingV17 || migrateSnapshotV17 || migrateV16 || migrateOnboardingV16 || migrateSnapshotV16 || migrateV15 || migrateOnboardingV15 || migrateSnapshotV15 || migrateV14 || migrateOnboardingV14 || migrateSnapshotV14 || migrateV13 || migrateOnboardingV13 || migrateSnapshotV13 || migrateV12 || migrateOnboardingV12 || migrateSnapshotV12 || migrateV11 || migrateOnboardingV11 || migrateSnapshotV11 || migrateV10 || migrateOnboardingV10 || migrateSnapshotV10 || migrateV9 || migrateOnboardingV9 || migrateSnapshotV9 || migrateV8 || migrateOnboardingV8 || migrateSnapshotV8 || migrateV7 || migrateOnboardingV7 || migrateSnapshotV7 || migrateV6 || migrateOnboardingV6 || migrateSnapshotV6 || migrateV5 || migrateOnboardingV5 || migrateSnapshotV5 || migrateV4 || migrateOnboardingV4 || migrateSnapshotV4 || migrateV1 || migrateV2 || migrateSnapshotV2 || migrateV3 || migrateOnboardingV3 || migrateSnapshotV3;
    const bool traceRequested = argc == 3 && (std::strcmp(argv[1], "--replay-trace") == 0 ||
        std::strcmp(argv[1], "--replay-onboarding-trace") == 0 || std::strcmp(argv[1], "--replay-snapshot-trace") == 0);
    const bool snapshotReplay = argc == 3 && (std::strcmp(argv[1], "--replay-snapshot") == 0 || std::strcmp(argv[1], "--replay-snapshot-trace") == 0);
    const bool replay = argc == 3 && (std::strcmp(argv[1], "--replay") == 0 || std::strcmp(argv[1], "--replay-trace") == 0);
    const bool onboarding = argc == 3 && (std::strcmp(argv[1], "--replay-onboarding") == 0 || std::strcmp(argv[1], "--replay-onboarding-trace") == 0);
    std::uint32_t seed = digivice::kDevelopmentSeed;
    if ((!migrate && !snapshotReplay && !replay && !onboarding) ||
        (!snapshotReplay && !migrateSnapshotV2 && !migrateSnapshotV3 && !migrateSnapshotV4 && !migrateSnapshotV5 && !migrateSnapshotV6 && !migrateSnapshotV7 && !migrateSnapshotV8 && !migrateSnapshotV9 && !migrateSnapshotV10 && !migrateSnapshotV11 && !migrateSnapshotV12 && !migrateSnapshotV13 && !migrateSnapshotV14 && !migrateSnapshotV15 && !migrateSnapshotV16 && !migrateSnapshotV17 && !migrateSnapshotV18 && !number(argv[2], argv[2] + std::strlen(argv[2]), seed)))
        return fail(0, "usage: digivice-core --replay[-trace] <seed> | --replay-onboarding[-trace] <seed> | --migrate-v1 <seed> | --migrate-v2 <seed> | --migrate-v2-snapshot <base64> | --replay-snapshot[-trace] <base64> | --migrate-v3 <seed> | --migrate-v3-onboarding <seed> | --migrate-v3-snapshot <base64> | --migrate-v4[-onboarding|-snapshot] <seed|base64> | --migrate-v5[-onboarding|-snapshot] <seed|base64> | --migrate-v6[-onboarding|-snapshot] <seed|base64> | --migrate-v7[-onboarding|-snapshot] <seed|base64> | --migrate-v8[-onboarding|-snapshot] <seed|base64> | --replay-v8[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v9[-onboarding|-snapshot] <seed|base64> | --replay-v9[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v10[-onboarding|-snapshot] <seed|base64> | --replay-v10[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v11[-onboarding|-snapshot] <seed|base64> | --replay-v11[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v12[-onboarding|-snapshot] <seed|base64> | --replay-v12[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v13[-onboarding|-snapshot] <seed|base64> | --replay-v13[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v14[-onboarding|-snapshot] <seed|base64> | --replay-v14[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v15[-onboarding|-snapshot] <seed|base64> | --replay-v15[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v16[-onboarding|-snapshot] <seed|base64> | --replay-v16[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v17[-onboarding|-snapshot] <seed|base64> | --replay-v17[-onboarding|-snapshot]-trace <seed|base64> | --migrate-v18[-onboarding|-snapshot] <seed|base64> | --replay-v18[-onboarding|-snapshot]-trace <seed|base64> | --evolution-graph <formId> <offset> [limit] | --evolutions <species> | --starters | --roster | --budget");
    auto state = onboarding ? digivice::newDevice(seed) : digivice::newGame(seed);
    auto legacy = digivice_v1::newGame(seed);
    auto previous = digivice_v2::newGame(seed);
    auto prior=migrateOnboardingV3 ? digivice::legacy_v3::newDevice(seed) : digivice::legacy_v3::newGame(seed);
    auto prior4=migrateOnboardingV4 ? digivice::legacy_v4::newDevice(seed) : digivice::legacy_v4::newGame(seed);
    auto prior5=migrateOnboardingV5 ? digivice::legacy_v5::newDevice(seed) : digivice::legacy_v5::newGame(seed);
    auto prior6=migrateOnboardingV6 ? digivice::legacy_v6::newDevice(seed) : digivice::legacy_v6::newGame(seed);
    auto prior7=migrateOnboardingV7 ? digivice::legacy_v7::newDevice(seed) : digivice::legacy_v7::newGame(seed);
    auto prior8=migrateOnboardingV8 ? digivice::legacy_v8::newDevice(seed) : digivice::legacy_v8::newGame(seed);
    auto prior9=migrateOnboardingV9 ? digivice::legacy_v9::newDevice(seed) : digivice::legacy_v9::newGame(seed);
    auto prior10=migrateOnboardingV10 ? digivice::legacy_v10::newDevice(seed) : digivice::legacy_v10::newGame(seed);
    auto prior11=migrateOnboardingV11 ? digivice::legacy_v11::newDevice(seed) : digivice::legacy_v11::newGame(seed);
    auto prior12=migrateOnboardingV12 ? digivice::legacy_v12::newDevice(seed) : digivice::legacy_v12::newGame(seed);
    auto prior13=migrateOnboardingV13 ? digivice::legacy_v13::newDevice(seed) : digivice::legacy_v13::newGame(seed);
    auto prior14=migrateOnboardingV14 ? digivice::legacy_v14::newDevice(seed) : digivice::legacy_v14::newGame(seed);
    auto prior15=migrateOnboardingV15 ? digivice::legacy_v15::newDevice(seed) : digivice::legacy_v15::newGame(seed);
    auto prior16=migrateOnboardingV16 ? digivice::legacy_v16::newDevice(seed) : digivice::legacy_v16::newGame(seed);
    auto prior17=migrateOnboardingV17 ? digivice::legacy_v17::newDevice(seed) : digivice::legacy_v17::newGame(seed);
    auto prior18=migrateOnboardingV18 ? digivice::legacy_v18::newDevice(seed) : digivice::legacy_v18::newGame(seed);
    digivice::autobattle::Trace lastTrace;
    if (snapshotReplay || migrateSnapshotV2 || migrateSnapshotV3 || migrateSnapshotV4 || migrateSnapshotV5 || migrateSnapshotV6 || migrateSnapshotV7 || migrateSnapshotV8 || migrateSnapshotV9 || migrateSnapshotV10 || migrateSnapshotV11 || migrateSnapshotV12 || migrateSnapshotV13 || migrateSnapshotV14 || migrateSnapshotV15 || migrateSnapshotV16 || migrateSnapshotV17 || migrateSnapshotV18) {
        std::uint8_t bytes[digivice::kSnapshotSize];
        std::size_t length;
        if (!decodeBase64(argv[2], bytes, sizeof(bytes), length)) return fail(0, "invalid snapshot base64");
        if(migrateSnapshotV18) {
            const auto status=digivice::legacy_v18::decodeSnapshot(bytes,length,prior18);
            if(status!=digivice::legacy_v18::SnapshotStatus::Ok && status!=digivice::legacy_v18::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v18::snapshotStatusText(status));
        } else if(migrateSnapshotV17) {
            const auto status=digivice::legacy_v17::decodeSnapshot(bytes,length,prior17);
            if(status!=digivice::legacy_v17::SnapshotStatus::Ok && status!=digivice::legacy_v17::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v17::snapshotStatusText(status));
        } else if(migrateSnapshotV16) {
            const auto status=digivice::legacy_v16::decodeSnapshot(bytes,length,prior16);
            if(status!=digivice::legacy_v16::SnapshotStatus::Ok && status!=digivice::legacy_v16::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v16::snapshotStatusText(status));
        } else if(migrateSnapshotV15) {
            const auto status=digivice::legacy_v15::decodeSnapshot(bytes,length,prior15);
            if(status!=digivice::legacy_v15::SnapshotStatus::Ok && status!=digivice::legacy_v15::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v15::snapshotStatusText(status));
        } else if(migrateSnapshotV14) {
            const auto status=digivice::legacy_v14::decodeSnapshot(bytes,length,prior14);
            if(status!=digivice::legacy_v14::SnapshotStatus::Ok && status!=digivice::legacy_v14::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v14::snapshotStatusText(status));
        } else if(migrateSnapshotV13) {
            const auto status=digivice::legacy_v13::decodeSnapshot(bytes,length,prior13);
            if(status!=digivice::legacy_v13::SnapshotStatus::Ok && status!=digivice::legacy_v13::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v13::snapshotStatusText(status));
        } else if(migrateSnapshotV12) {
            const auto status=digivice::legacy_v12::decodeSnapshot(bytes,length,prior12);
            if(status!=digivice::legacy_v12::SnapshotStatus::Ok && status!=digivice::legacy_v12::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v12::snapshotStatusText(status));
        } else if(migrateSnapshotV11) {
            const auto status=digivice::legacy_v11::decodeSnapshot(bytes,length,prior11);
            if(status!=digivice::legacy_v11::SnapshotStatus::Ok && status!=digivice::legacy_v11::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v11::snapshotStatusText(status));
        } else if(migrateSnapshotV10) {
            const auto status=digivice::legacy_v10::decodeSnapshot(bytes,length,prior10);
            if(status!=digivice::legacy_v10::SnapshotStatus::Ok && status!=digivice::legacy_v10::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v10::snapshotStatusText(status));
        } else if(migrateSnapshotV9) {
            const auto status=digivice::legacy_v9::decodeSnapshot(bytes,length,prior9);
            if(status!=digivice::legacy_v9::SnapshotStatus::Ok && status!=digivice::legacy_v9::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v9::snapshotStatusText(status));
        } else if(migrateSnapshotV8) {
            const auto status=digivice::legacy_v8::decodeSnapshot(bytes,length,prior8);
            if(status!=digivice::legacy_v8::SnapshotStatus::Ok && status!=digivice::legacy_v8::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v8::snapshotStatusText(status));
        } else if(migrateSnapshotV7) {
            const auto status=digivice::legacy_v7::decodeSnapshot(bytes,length,prior7);
            if(status!=digivice::legacy_v7::SnapshotStatus::Ok && status!=digivice::legacy_v7::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v7::snapshotStatusText(status));
        } else if(migrateSnapshotV6) {
            const auto status=digivice::legacy_v6::decodeSnapshot(bytes,length,prior6);
            if(status!=digivice::legacy_v6::SnapshotStatus::Ok && status!=digivice::legacy_v6::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v6::snapshotStatusText(status));
        } else if(migrateSnapshotV5) {
            const auto status=digivice::legacy_v5::decodeSnapshot(bytes,length,prior5);
            if(status!=digivice::legacy_v5::SnapshotStatus::Ok && status!=digivice::legacy_v5::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v5::snapshotStatusText(status));
        } else if(migrateSnapshotV4) {
            const auto status=digivice::legacy_v4::decodeSnapshot(bytes,length,prior4);
            if(status!=digivice::legacy_v4::SnapshotStatus::Ok && status!=digivice::legacy_v4::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v4::snapshotStatusText(status));
        } else if(migrateSnapshotV3) {
            const auto status=digivice::legacy_v3::decodeSnapshot(bytes,length,prior);
            if(status!=digivice::legacy_v3::SnapshotStatus::Ok && status!=digivice::legacy_v3::SnapshotStatus::Migrated)
                return fail(0,digivice::legacy_v3::snapshotStatusText(status));
        } else if (migrateSnapshotV2) {
            // Run old events before scaling HP or changing combat rules.
            const auto status = digivice_v2::decodeSnapshot(bytes, length, previous);
            if (status != digivice_v2::SnapshotStatus::Ok && status != digivice_v2::SnapshotStatus::Migrated)
                return fail(0, digivice_v2::snapshotStatusText(status));
        } else {
            const auto status = digivice::decodeSnapshot(bytes, length, state);
            if (status != digivice::SnapshotStatus::Ok && status != digivice::SnapshotStatus::Migrated)
                return fail(0, digivice::snapshotStatusText(status));
        }
    }
    std::uint32_t lineNumber = 0;
    for (;;) {
        char line[64];
        std::size_t used = 0;
        int ch;
        while ((ch = std::fgetc(stdin)) != EOF && ch != '\n') {
            if (ch == 0) return fail(lineNumber + 1, "NUL byte in input");
            if (used + 1 >= sizeof(line)) return fail(lineNumber + 1, "input line exceeds 63 bytes");
            line[used++] = static_cast<char>(ch);
        }
        if (ch == EOF && used == 0) break;
        if (++lineNumber > digivice::kMaxReplayEvents) return fail(lineNumber, "replay exceeds 10000 events");
        line[used] = '\0';
        char* cursor = line;
        while (whitespace(*cursor)) ++cursor;
        char* name = cursor;
        while (*cursor && !whitespace(*cursor)) ++cursor;
        if (*cursor) *cursor++ = '\0';
        while (whitespace(*cursor)) ++cursor;
        char* valueStart = cursor;
        while (*cursor && !whitespace(*cursor)) ++cursor;
        char* valueEnd = cursor;
        while (whitespace(*cursor)) ++cursor;
        if (*cursor) return fail(lineNumber, "expected one action and at most one value");
        digivice::Action action;
        if (!digivice::parseAction(name, action)) return fail(lineNumber, "unknown or missing action");
        std::uint32_t value = 0;
        if (valueStart != valueEnd && !number(valueStart, valueEnd, value))
            return fail(lineNumber, "value must be an unsigned 32-bit integer");
        if (valueStart == valueEnd && (action == digivice::Action::AccrueSteps || action == digivice::Action::Explore || action == digivice::Action::EncounterRate || action == digivice::Action::EncounterSeed || action == digivice::Action::WorldSeed || action == digivice::Action::Walk || action == digivice::Action::Card ||
                                      action == digivice::Action::Select || action == digivice::Action::Hatch || action == digivice::Action::Mode || action == digivice::Action::Evolve || action == digivice::Action::Release || action == digivice::Action::Flick || action == digivice::Action::RingCapture || action == digivice::Action::PartyAdd || action == digivice::Action::PartyRemove || action == digivice::Action::CareMinute || action == digivice::Action::EvolveMember || action == digivice::Action::Focus))
            return fail(lineNumber, "this action requires an explicit value");
        if (migrateV1) {
            digivice_v1::Action legacyAction;
            if (!digivice_v1::parseAction(name, legacyAction)) return fail(lineNumber, "action unavailable in legacy rules");
            const auto error = digivice_v1::apply(legacy, legacyAction, value);
            if (error != digivice_v1::Error::None) return fail(lineNumber, digivice_v1::errorText(error));
        } else if (migrateV2 || migrateSnapshotV2) {
            digivice_v2::Action previousAction;
            if (!digivice_v2::parseAction(name, previousAction)) return fail(lineNumber, "action unavailable in legacy rules");
            const auto error = digivice_v2::apply(previous, previousAction, value);
            if (error != digivice_v2::Error::None) return fail(lineNumber, digivice_v2::errorText(error));
        } else if(migrateV18 || migrateOnboardingV18 || migrateSnapshotV18) {
            digivice::legacy_v18::Action previousAction;
            if(!digivice::legacy_v18::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules18");
            const auto error=frozen18Trace && previousAction==digivice::legacy_v18::Action::Auto && value==0 ?
                digivice::legacy_v18::applyAuto(prior18,&lastTrace) : frozen18Trace && previousAction==digivice::legacy_v18::Action::AutoFight && value==0 ?
                digivice::legacy_v18::applyAutoFight(prior18,&lastTrace) : frozen18Trace && previousAction==digivice::legacy_v18::Action::AutoResume && value==0 ?
                digivice::legacy_v18::applyAutoResume(prior18,&lastTrace) : frozen18Trace && previousAction==digivice::legacy_v18::Action::Focus ?
                digivice::legacy_v18::applyFocus(prior18,value,&lastTrace) : digivice::legacy_v18::apply(prior18,previousAction,value);
            if(error!=digivice::legacy_v18::Error::None) return fail(lineNumber,digivice::legacy_v18::errorText(error));
            if(lastTrace.count && lastTrace.outcome==digivice::autobattle::Outcome::None &&
               (prior18.autoCapture==digivice::legacy_v18::AutoCapture::None || lastTrace.endSequence!=prior18.foregroundSequence))lastTrace.count=0;
        } else if(migrateV17 || migrateOnboardingV17 || migrateSnapshotV17) {
            digivice::legacy_v17::Action previousAction;
            if(!digivice::legacy_v17::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules17");
            const auto error=frozen17Trace && previousAction==digivice::legacy_v17::Action::Auto && value==0 ?
                digivice::legacy_v17::applyAuto(prior17,&lastTrace) : frozen17Trace && previousAction==digivice::legacy_v17::Action::AutoFight && value==0 ?
                digivice::legacy_v17::applyAutoFight(prior17,&lastTrace) : frozen17Trace && previousAction==digivice::legacy_v17::Action::AutoResume && value==0 ?
                digivice::legacy_v17::applyAutoResume(prior17,&lastTrace) : digivice::legacy_v17::apply(prior17,previousAction,value);
            if(error!=digivice::legacy_v17::Error::None) return fail(lineNumber,digivice::legacy_v17::errorText(error));
            if(lastTrace.count && lastTrace.outcome==digivice::autobattle::Outcome::None &&
               (prior17.autoCapture!=digivice::legacy_v17::AutoCapture::Awaiting || lastTrace.endSequence!=prior17.foregroundSequence))lastTrace.count=0;
        } else if(migrateV16 || migrateOnboardingV16 || migrateSnapshotV16) {
            digivice::legacy_v16::Action previousAction;
            if(!digivice::legacy_v16::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules16");
            const auto error=frozen16Trace && previousAction==digivice::legacy_v16::Action::Auto && value==0 ?
                digivice::legacy_v16::applyAuto(prior16,&lastTrace) : frozen16Trace && previousAction==digivice::legacy_v16::Action::AutoFight && value==0 ?
                digivice::legacy_v16::applyAutoFight(prior16,&lastTrace) : frozen16Trace && previousAction==digivice::legacy_v16::Action::AutoResume && value==0 ?
                digivice::legacy_v16::applyAutoResume(prior16,&lastTrace) : digivice::legacy_v16::apply(prior16,previousAction,value);
            if(error!=digivice::legacy_v16::Error::None) return fail(lineNumber,digivice::legacy_v16::errorText(error));
            if(lastTrace.count && lastTrace.outcome==digivice::autobattle::Outcome::None &&
               (prior16.autoCapture!=digivice::legacy_v16::AutoCapture::Awaiting || lastTrace.endSequence!=prior16.foregroundSequence))lastTrace.count=0;
        } else if(migrateV15 || migrateOnboardingV15 || migrateSnapshotV15) {
            digivice::legacy_v15::Action previousAction;
            if(!digivice::legacy_v15::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules15");
            const auto error=frozen15Trace && previousAction==digivice::legacy_v15::Action::Auto && value==0 ?
                digivice::legacy_v15::applyAuto(prior15,&lastTrace) : frozen15Trace && previousAction==digivice::legacy_v15::Action::AutoFight && value==0 ?
                digivice::legacy_v15::applyAutoFight(prior15,&lastTrace) : frozen15Trace && previousAction==digivice::legacy_v15::Action::AutoResume && value==0 ?
                digivice::legacy_v15::applyAutoResume(prior15,&lastTrace) : digivice::legacy_v15::apply(prior15,previousAction,value);
            if(error!=digivice::legacy_v15::Error::None) return fail(lineNumber,digivice::legacy_v15::errorText(error));
            if(lastTrace.count && lastTrace.outcome==digivice::autobattle::Outcome::None &&
               (prior15.autoCapture!=digivice::legacy_v15::AutoCapture::Awaiting || lastTrace.endSequence!=prior15.foregroundSequence))lastTrace.count=0;
        } else if(migrateV14 || migrateOnboardingV14 || migrateSnapshotV14) {
            digivice::legacy_v14::Action previousAction;
            if(!digivice::legacy_v14::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules14");
            const auto error=frozen14Trace && previousAction==digivice::legacy_v14::Action::Auto && value==0 ?
                digivice::legacy_v14::applyAuto(prior14,&lastTrace) : frozen14Trace && previousAction==digivice::legacy_v14::Action::AutoFight && value==0 ?
                digivice::legacy_v14::applyAutoFight(prior14,&lastTrace) : frozen14Trace && previousAction==digivice::legacy_v14::Action::AutoResume && value==0 ?
                digivice::legacy_v14::applyAutoResume(prior14,&lastTrace) : digivice::legacy_v14::apply(prior14,previousAction,value);
            if(error!=digivice::legacy_v14::Error::None) return fail(lineNumber,digivice::legacy_v14::errorText(error));
            if(lastTrace.count && lastTrace.outcome==digivice::autobattle::Outcome::None &&
               (prior14.autoCapture!=digivice::legacy_v14::AutoCapture::Awaiting || lastTrace.endSequence!=prior14.foregroundSequence))lastTrace.count=0;
        } else if(migrateV13 || migrateOnboardingV13 || migrateSnapshotV13) {
            digivice::legacy_v13::Action previousAction;
            if(!digivice::legacy_v13::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules13");
            const auto error=frozen13Trace && previousAction==digivice::legacy_v13::Action::Auto && value==0 ?
                digivice::legacy_v13::applyAuto(prior13,&lastTrace) : frozen13Trace && previousAction==digivice::legacy_v13::Action::AutoFight && value==0 ?
                digivice::legacy_v13::applyAutoFight(prior13,&lastTrace) : frozen13Trace && previousAction==digivice::legacy_v13::Action::AutoResume && value==0 ?
                digivice::legacy_v13::applyAutoResume(prior13,&lastTrace) : digivice::legacy_v13::apply(prior13,previousAction,value);
            if(error!=digivice::legacy_v13::Error::None) return fail(lineNumber,digivice::legacy_v13::errorText(error));
            if(lastTrace.count && lastTrace.outcome==digivice::autobattle::Outcome::None &&
               (prior13.autoCapture!=digivice::legacy_v13::AutoCapture::Awaiting || lastTrace.endSequence!=prior13.foregroundSequence))lastTrace.count=0;
        } else if(migrateV12 || migrateOnboardingV12 || migrateSnapshotV12) {
            digivice::legacy_v12::Action previousAction;
            if(!digivice::legacy_v12::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules12");
            const auto error=frozen12Trace && previousAction==digivice::legacy_v12::Action::Auto && value==0 ?
                digivice::legacy_v12::applyAuto(prior12,&lastTrace) : digivice::legacy_v12::apply(prior12,previousAction,value);
            if(error!=digivice::legacy_v12::Error::None) return fail(lineNumber,digivice::legacy_v12::errorText(error));
        } else if(migrateV11 || migrateOnboardingV11 || migrateSnapshotV11) {
            digivice::legacy_v11::Action previousAction;
            if(!digivice::legacy_v11::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules11");
            const auto error=frozen11Trace && previousAction==digivice::legacy_v11::Action::Auto && value==0 ?
                digivice::legacy_v11::applyAuto(prior11,&lastTrace) : digivice::legacy_v11::apply(prior11,previousAction,value);
            if(error!=digivice::legacy_v11::Error::None) return fail(lineNumber,digivice::legacy_v11::errorText(error));
        } else if(migrateV10 || migrateOnboardingV10 || migrateSnapshotV10) {
            digivice::legacy_v10::Action previousAction;
            if(!digivice::legacy_v10::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules10");
            const auto error=frozen10Trace && previousAction==digivice::legacy_v10::Action::Auto && value==0 ?
                digivice::legacy_v10::applyAuto(prior10,&lastTrace) : digivice::legacy_v10::apply(prior10,previousAction,value);
            if(error!=digivice::legacy_v10::Error::None) return fail(lineNumber,digivice::legacy_v10::errorText(error));
        } else if(migrateV9 || migrateOnboardingV9 || migrateSnapshotV9) {
            digivice::legacy_v9::Action previousAction;
            if(!digivice::legacy_v9::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules9");
            const auto error=frozen9Trace && previousAction==digivice::legacy_v9::Action::Auto && value==0 ?
                digivice::legacy_v9::applyAuto(prior9,&lastTrace) : digivice::legacy_v9::apply(prior9,previousAction,value);
            if(error!=digivice::legacy_v9::Error::None) return fail(lineNumber,digivice::legacy_v9::errorText(error));
        } else if(migrateV8 || migrateOnboardingV8 || migrateSnapshotV8) {
            digivice::legacy_v8::Action previousAction;
            if(!digivice::legacy_v8::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules8");
            const auto error=frozen8Trace && previousAction==digivice::legacy_v8::Action::Auto && value==0 ?
                digivice::legacy_v8::applyAuto(prior8,&lastTrace) : digivice::legacy_v8::apply(prior8,previousAction,value);
            if(error!=digivice::legacy_v8::Error::None) return fail(lineNumber,digivice::legacy_v8::errorText(error));
        } else if(migrateV7 || migrateOnboardingV7 || migrateSnapshotV7) {
            digivice::legacy_v7::Action previousAction;
            if(!digivice::legacy_v7::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules7");
            const auto error=digivice::legacy_v7::apply(prior7,previousAction,value);
            if(error!=digivice::legacy_v7::Error::None) return fail(lineNumber,digivice::legacy_v7::errorText(error));
        } else if(migrateV6 || migrateOnboardingV6 || migrateSnapshotV6) {
            digivice::legacy_v6::Action previousAction;
            if(!digivice::legacy_v6::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules6");
            const auto error=digivice::legacy_v6::apply(prior6,previousAction,value);
            if(error!=digivice::legacy_v6::Error::None) return fail(lineNumber,digivice::legacy_v6::errorText(error));
        } else if(migrateV5 || migrateOnboardingV5 || migrateSnapshotV5) {
            digivice::legacy_v5::Action previousAction;
            if(!digivice::legacy_v5::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules5");
            const auto error=digivice::legacy_v5::apply(prior5,previousAction,value);
            if(error!=digivice::legacy_v5::Error::None) return fail(lineNumber,digivice::legacy_v5::errorText(error));
        } else if(migrateV4 || migrateOnboardingV4 || migrateSnapshotV4) {
            digivice::legacy_v4::Action previousAction;
            if(!digivice::legacy_v4::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules4");
            const auto error=digivice::legacy_v4::apply(prior4,previousAction,value);
            if(error!=digivice::legacy_v4::Error::None) return fail(lineNumber,digivice::legacy_v4::errorText(error));
        } else if(migrateV3 || migrateOnboardingV3 || migrateSnapshotV3) {
            digivice::legacy_v3::Action previousAction;
            if(!digivice::legacy_v3::parseAction(name,previousAction)) return fail(lineNumber,"action unavailable in rules3");
            const auto error=digivice::legacy_v3::apply(prior,previousAction,value);
            if(error!=digivice::legacy_v3::Error::None) return fail(lineNumber,digivice::legacy_v3::errorText(error));
        } else {
            const auto error = traceRequested && action == digivice::Action::Auto && value == 0 ?
                digivice::applyAuto(state, &lastTrace) : traceRequested && action==digivice::Action::AutoFight && value==0 ?
                digivice::applyAutoFight(state,&lastTrace) : traceRequested && action==digivice::Action::AutoResume && value==0 ?
                digivice::applyAutoResume(state,&lastTrace) : traceRequested && action==digivice::Action::Focus ?
                digivice::applyFocus(state,value,&lastTrace) : digivice::apply(state, action, value);
            if (error != digivice::Error::None) return fail(lineNumber, digivice::errorText(error));
            if(lastTrace.count && lastTrace.outcome==digivice::autobattle::Outcome::None &&
               (state.autoCapture==digivice::AutoCapture::None || lastTrace.endSequence!=state.foregroundSequence))lastTrace.count=0;
        }
    }
    if (std::ferror(stdin)) return fail(0, "failed to read replay input");
    if(frozen8Trace) {
        char frozenJson[digivice::legacy_v8::kJsonCapacity];
        if(!digivice::legacy_v8::writeJson(prior8,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen8 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen8 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen9Trace) {
        char frozenJson[digivice::legacy_v9::kJsonCapacity];
        if(!digivice::legacy_v9::writeJson(prior9,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen9 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen9 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen10Trace) {
        char frozenJson[digivice::legacy_v10::kJsonCapacity];
        if(!digivice::legacy_v10::writeJson(prior10,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen10 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen10 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen11Trace) {
        char frozenJson[digivice::legacy_v11::kJsonCapacity];
        if(!digivice::legacy_v11::writeJson(prior11,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen11 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen11 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen12Trace) {
        char frozenJson[digivice::legacy_v12::kJsonCapacity];
        if(!digivice::legacy_v12::writeJson(prior12,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen12 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen12 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen13Trace) {
        char frozenJson[digivice::legacy_v13::kJsonCapacity];
        if(!digivice::legacy_v13::writeJson(prior13,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen13 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen13 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen18Trace) {
        char frozenJson[digivice::legacy_v18::kJsonCapacity];
        if(!digivice::legacy_v18::writeJson(prior18,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen18 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen18 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen17Trace) {
        char frozenJson[digivice::legacy_v17::kJsonCapacity];
        if(!digivice::legacy_v17::writeJson(prior17,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen17 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen17 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen16Trace) {
        char frozenJson[digivice::legacy_v16::kJsonCapacity];
        if(!digivice::legacy_v16::writeJson(prior16,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen16 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen16 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen15Trace) {
        char frozenJson[digivice::legacy_v15::kJsonCapacity];
        if(!digivice::legacy_v15::writeJson(prior15,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen15 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen15 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if(frozen14Trace) {
        char frozenJson[digivice::legacy_v14::kJsonCapacity];
        if(!digivice::legacy_v14::writeJson(prior14,frozenJson,sizeof(frozenJson))) return fail(0,"failed to serialize frozen14 state");
        if(lastTrace.count) {
            char traceJson[digivice::autobattle::kTraceJsonCapacity];
            if(!digivice::autobattle::writeJson(lastTrace,traceJson,sizeof(traceJson))) return fail(0,"failed to serialize frozen14 trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n",frozenJson,traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n",frozenJson);
        return 0;
    }
    if (migrateV1) {
        digivice_v1::Snapshot old;
        if (!digivice_v1::encodeSnapshot(legacy, old) ||
            digivice::decodeSnapshot(old.bytes, sizeof(old.bytes), state) != digivice::SnapshotStatus::Migrated)
            return fail(0, "failed to migrate legacy replay");
    }
    if (migrateV2 || migrateSnapshotV2) {
        digivice_v2::Snapshot old;
        if (!digivice_v2::encodeSnapshot(previous, old) ||
            digivice::decodeSnapshot(old.bytes, sizeof(old.bytes), state) != digivice::SnapshotStatus::Migrated)
            return fail(0, "failed to migrate rules-2 replay");
    }
    if(migrateV3 || migrateOnboardingV3 || migrateSnapshotV3) {
        digivice::legacy_v3::Snapshot old;
        if(!digivice::legacy_v3::encodeSnapshot(prior,old) || digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules3 replay");
    }
    if(migrateV4 || migrateOnboardingV4 || migrateSnapshotV4) {
        digivice::legacy_v4::Snapshot old;
        if(!digivice::legacy_v4::encodeSnapshot(prior4,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules4 replay");
    }
    if(migrateV5 || migrateOnboardingV5 || migrateSnapshotV5) {
        digivice::legacy_v5::Snapshot old;
        if(!digivice::legacy_v5::encodeSnapshot(prior5,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules5 replay");
    }
    if(migrateV6 || migrateOnboardingV6 || migrateSnapshotV6) {
        digivice::legacy_v6::Snapshot old;
        if(!digivice::legacy_v6::encodeSnapshot(prior6,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules6 replay");
    }
    if(migrateV7 || migrateOnboardingV7 || migrateSnapshotV7) {
        digivice::legacy_v7::Snapshot old;
        if(!digivice::legacy_v7::encodeSnapshot(prior7,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules7 replay");
    }
    if(migrateV8 || migrateOnboardingV8 || migrateSnapshotV8) {
        digivice::legacy_v8::Snapshot old;
        if(!digivice::legacy_v8::encodeSnapshot(prior8,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules8 replay");
    }
    if(migrateV9 || migrateOnboardingV9 || migrateSnapshotV9) {
        digivice::legacy_v9::Snapshot old;
        if(!digivice::legacy_v9::encodeSnapshot(prior9,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules9 replay");
    }
    if(migrateV10 || migrateOnboardingV10 || migrateSnapshotV10) {
        digivice::legacy_v10::Snapshot old;
        if(!digivice::legacy_v10::encodeSnapshot(prior10,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules10 replay");
    }
    if(migrateV11 || migrateOnboardingV11 || migrateSnapshotV11) {
        digivice::legacy_v11::Snapshot old;
        if(!digivice::legacy_v11::encodeSnapshot(prior11,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules11 replay");
    }
    if(migrateV12 || migrateOnboardingV12 || migrateSnapshotV12) {
        digivice::legacy_v12::Snapshot old;
        if(!digivice::legacy_v12::encodeSnapshot(prior12,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules12 replay");
    }
    if(migrateV13 || migrateOnboardingV13 || migrateSnapshotV13) {
        digivice::legacy_v13::Snapshot old;
        if(!digivice::legacy_v13::encodeSnapshot(prior13,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules13 replay");
    }
    if(migrateV18 || migrateOnboardingV18 || migrateSnapshotV18) {
        digivice::legacy_v18::Snapshot old;
        if(!digivice::legacy_v18::encodeSnapshot(prior18,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules18 replay");
    }
    if(migrateV17 || migrateOnboardingV17 || migrateSnapshotV17) {
        digivice::legacy_v17::Snapshot old;
        if(!digivice::legacy_v17::encodeSnapshot(prior17,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules17 replay");
    }
    if(migrateV16 || migrateOnboardingV16 || migrateSnapshotV16) {
        digivice::legacy_v16::Snapshot old;
        if(!digivice::legacy_v16::encodeSnapshot(prior16,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules16 replay");
    }
    if(migrateV15 || migrateOnboardingV15 || migrateSnapshotV15) {
        digivice::legacy_v15::Snapshot old;
        if(!digivice::legacy_v15::encodeSnapshot(prior15,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules15 replay");
    }
    if(migrateV14 || migrateOnboardingV14 || migrateSnapshotV14) {
        digivice::legacy_v14::Snapshot old;
        if(!digivice::legacy_v14::encodeSnapshot(prior14,old)||digivice::decodeSnapshot(old.bytes,sizeof(old.bytes),state)!=digivice::SnapshotStatus::Migrated)
            return fail(0,"failed to migrate rules14 replay");
    }
    char json[digivice::kJsonCapacity];
    if (!digivice::writeJson(state, json, sizeof(json))) return fail(0, "failed to serialize state");
    if (migrate) {
        digivice::Snapshot snapshot;
        if (!digivice::encodeSnapshot(state, snapshot)) return fail(0, "failed to encode migrated snapshot");
        char encoded[((digivice::kSnapshotSize + 2) / 3) * 4 + 1];
        encodeBase64(snapshot, encoded);
        std::printf("{\"state\":%s,\"snapshotBase64\":\"%s\"}\n", json, encoded);
    } else if (traceRequested) {
        char traceJson[digivice::autobattle::kTraceJsonCapacity];
        if (lastTrace.count) {
            if (!digivice::autobattle::writeJson(lastTrace, traceJson, sizeof(traceJson))) return fail(0, "failed to serialize auto trace");
            std::printf("{\"state\":%s,\"trace\":%s}\n", json, traceJson);
        } else std::printf("{\"state\":%s,\"trace\":null}\n", json);
    } else std::puts(json);
    return 0;
}

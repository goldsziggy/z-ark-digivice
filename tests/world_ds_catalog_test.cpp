#include "forms.hpp"
#include "combat.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace f = digivice::forms;
namespace c = digivice::combat;
unsigned checks = 0, failures = 0;
void check(bool result, const char* message) {
    ++checks; if (!result) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
int main(int argc, char** argv) {
    constexpr auto capacity = c::kFormCatalogJsonCapacity;
    std::array<char, capacity + 2> storage{};
    std::size_t maximumDetail = 0, maximumPage = 0;
    unsigned productionDetails = 0, publishedRoutes = 0, retainedFixtureRoutes = 0;
    const bool dump = argc == 2 && std::strcmp(argv[1], "--dump") == 0;
    // Stable historical IDs remain decodable, but release catalogs contain
    // only IDs11..276 and their 166 authored routes.
    for (unsigned id = 1; id <= f::kFormCount; ++id) {
        const auto* form = f::find(id);
        check(form && form->id == id, "all IDs addressable");
        check(f::validForLineage(id, form->lineage), "form lineage");
        check(f::lineageFromSlug(f::lineageSlug(form->lineage)) == form->lineage, "stable lineage slug roundtrip");
        check(f::combatTier(id) != f::CombatTier::Unknown, "explicit combat tier");
        check(f::productionForm(id) == (id >= 11), "released ID boundary");
        check(f::encounterObtainable(id) == (id >= 11), "only production forms are obtainable");
        if (f::outgoing(id, 0)) check(f::leafReason(id) == nullptr, "nonleaf has no false terminal label");
        else check(f::leafReason(id) != nullptr, "leaf has an honest reason");
        storage.fill('Z');
        const auto n = c::writeFormCatalogJson(id, storage.data() + 1, capacity);
        if (id <= 10) {
            check(n == 0 && storage[1] == '\0', "historical fixture detail is not published");
            check(storage.front() == 'Z' && storage.back() == 'Z', "rejected detail canaries intact");
            storage.fill('Z');
            check(c::writeEvolutionGraphJson(id, 0, 16, storage.data() + 1, capacity) == 0 && storage[1] == '\0', "historical fixture graph is not published");
            check(storage.front() == 'Z' && storage.back() == 'Z', "rejected graph canaries intact");
            for (unsigned i = 0; const auto* edge = f::outgoing(id, i); ++i) {
                check(edge->to <= 10, "historical routes cannot enter production roster");
                ++retainedFixtureRoutes;
            }
            continue;
        }
        ++productionDetails;
        check(n > 0 && n < capacity && std::strlen(storage.data() + 1) == n, "bounded native detail");
        check(storage.front() == 'Z' && storage.back() == 'Z', "detail canaries intact");
        check(std::strstr(storage.data() + 1, "\"obtainable\":true") != nullptr, "published detail is obtainable");
        unsigned expectedRoutes = 0, actualRoutes = 0;
        for (unsigned i = 0; const auto* edge = f::outgoing(id, i); ++i) {
            check(f::productionForm(edge->to), "production route cannot select a historical fixture");
            char route[128];
            const auto need = f::evolutionNeed(*edge);
            std::snprintf(route, sizeof(route), "{\"toFormId\":%u,\"requiredLevel\":%u,\"requiredBond\":%u,\"requiredCare\":%u}",
                static_cast<unsigned>(edge->to), static_cast<unsigned>(need.level), static_cast<unsigned>(need.bond), static_cast<unsigned>(need.care));
            check(std::strstr(storage.data() + 1, route) != nullptr, "published route preserves its destination and gates");
            ++expectedRoutes;
        }
        for (const char* p = storage.data() + 1; (p = std::strstr(p, "\"toFormId\":")); ++p) ++actualRoutes;
        check(actualRoutes == expectedRoutes, "detail publishes exactly its authored outgoing routes");
        publishedRoutes += actualRoutes;
        if (n > maximumDetail) maximumDetail = n;
        if (dump) std::puts(storage.data() + 1);
        const auto exact = n;
        storage.fill('Z');
        check(c::writeFormCatalogJson(id, storage.data() + 1, exact) == 0 && storage[1] == '\0', "detail needs terminator byte and clears partial output");
        check(storage[exact + 1] == 'Z', "detail respects undersized boundary");
        check(c::writeFormCatalogJson(id, storage.data() + 1, exact + 1) == exact, "exact detail capacity succeeds");
    }
    check(productionDetails == 266 && publishedRoutes == 166, "complete production roster and routes are published");
    check(retainedFixtureRoutes == 6 && f::edgeCount() == 172, "six historical routes remain available for old replay");
    for (unsigned offset = 0; offset <= f::kProductionFormCount; ++offset) {
        storage.fill('Z');
        const auto n = c::writeCatalogPageJson(offset, 16, storage.data() + 1, c::kCatalogPageJsonCapacity);
        check(n > 0 && n < c::kCatalogPageJsonCapacity, "every possible page fits bound");
        check(storage.front() == 'Z' && storage.back() == 'Z', "page canaries intact");
        check(std::strstr(storage.data() + 1, "\"rulesVersion\":16") && std::strstr(storage.data() + 1, "\"total\":266,"), "page identifies the current production projection");
        unsigned count = 0;
        for (const char* p = storage.data() + 1; (p = std::strstr(p, "\"formId\":")); ++p) {
            unsigned id = 0;
            check(std::sscanf(p, "\"formId\":%u", &id) == 1 && id == offset + count + 11 && f::productionForm(id), "page IDs are consecutive production forms only");
            ++count;
        }
        const auto remaining = 266u - offset;
        check(count == (remaining < 16 ? remaining : 16), "page has the exact remaining production count");
        if (offset + count == 266) check(std::strstr(storage.data() + 1, "\"nextOffset\":null") != nullptr, "last and empty pages terminate pagination");
        if (n > maximumPage) maximumPage = n;
        check(c::writeCatalogPageJson(offset, 16, storage.data() + 1, n) == 0 && storage[1] == '\0', "short page clears output");
    }
    storage.fill('Z');
    check(c::writeCatalogPageJson(0, 0, storage.data() + 1, capacity) == 0, "zero page size rejected");
    check(c::writeCatalogPageJson(0, 17, storage.data() + 1, capacity) == 0, "oversized page rejected");
    check(c::writeCatalogPageJson(267, 16, storage.data() + 1, capacity) == 0 && storage[1] == '\0', "offset past production roster rejected");
    check(c::writeCatalogPageJson(std::numeric_limits<unsigned>::max(), 16, storage.data() + 1, capacity) == 0, "page offset overflow rejected");
    check(c::writeFormCatalogJson(0, storage.data() + 1, capacity) == 0, "zero form rejected");
    check(c::writeFormCatalogJson(f::kFormCount + 1, storage.data() + 1, capacity) == 0, "future form rejected");
    check(c::writeFormCatalogJson(11, nullptr, capacity) == 0, "null output rejected");
    check(c::writeFormCatalogJson(11, storage.data() + 1, 0) == 0, "zero capacity rejected");
    check(f::find(0) == nullptr && f::find(f::kFormCount + 1) == nullptr, "invalid form lookup");
    check(f::lineageFromSlug(nullptr) == 0 && f::lineageFromSlug("not-a-lineage") == 0, "unknown lineage rejected");
    check(f::combatTier(0) == f::CombatTier::Unknown, "invalid tier rejected");
    std::fprintf(stderr, "production projection: %u forms, %u routes; %u historical-only routes retained\n", productionDetails, publishedRoutes, retainedFixtureRoutes);
    std::fprintf(stderr, "catalog: %u checks, %u failures; max detail=%zu/%zu, page=%zu/%zu; Form=%zu CatalogEntry=%zu bytes (host)\n",
        checks, failures, maximumDetail, capacity, maximumPage, c::kCatalogPageJsonCapacity, sizeof(f::Form), sizeof(f::CatalogEntry));
    return failures ? 1 : 0;
}

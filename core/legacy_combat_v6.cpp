// Frozen rules-6/schema-9 content captured before catalog revision3. Do not retune or regenerate.
#include "legacy_combat_v6.hpp"
#include "legacy_forms_v6.hpp"
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace digivice::legacy_v6::combat {
namespace {
std::uint32_t percentFor(const char* attacker, const char* defender) {
    const auto type = [](const char* name) {
        return std::strcmp(name, "grove") == 0 ? 1u : std::strcmp(name, "tide") == 0 ? 2u :
               std::strcmp(name, "ember") == 0 ? 3u : 0u;
    };
    const auto a=type(attacker), d=type(defender);
    return !a || !d || a==d ? 100u : a%3+1==d ? 125u : 80u;
}
}
bool validFormProfile(std::uint32_t formId, std::uint32_t level) {
    const auto* form=forms::find(formId);
    return form && level >= form->minLevel && level <= forms::kMaxRpgLevel;
}
bool validProfile(std::uint32_t value, std::uint32_t level) {
    return validFormProfile(forms::initialForm(value),level);
}
Profile formProfile(std::uint32_t formId, std::uint32_t level) {
    const auto* f=forms::find(formId);
    if (!f || !validFormProfile(formId,level)) return {"Unknown","neutral","Unknown","Unknown","Unknown",{}};
    return {f->name,f->type,f->physicalSkill,f->heavySkill,f->magicSkill,forms::stats(formId,level)};
}
Profile profile(std::uint32_t value, std::uint32_t level) { return formProfile(forms::initialForm(value),level); }
const char* speciesName(std::uint32_t value) {
    const auto* name = forms::lineageSlug(value);
    return name ? name : "none";
}
bool isStarterSpecies(std::uint32_t value) { return value >= 5 && value <= kSpeciesCount; }
std::uint32_t starterSpecies(std::uint32_t id) { return id >= 1 && id <= kStarterCount ? id + 4 : 0; }
const char* starterName(std::uint32_t id) { return starterSpecies(id) ? profile(starterSpecies(id),1).name : nullptr; }
const char* stageName(std::uint32_t value) { return isStarterSpecies(value) ? "Rookie" : nullptr; }
std::uint32_t typePercent(std::uint32_t attacker, std::uint32_t defender) {
    const auto* a = forms::find(forms::initialForm(attacker));
    const auto* d = forms::find(forms::initialForm(defender));
    return a && d ? percentFor(a->type, d->type) : 100;
}
Hit resolveForms(std::uint32_t attackerForm, std::uint32_t attackerLevel,
                 std::uint32_t defenderForm, std::uint32_t defenderLevel, Move move, Defense defense) {
    if (!validFormProfile(attackerForm,attackerLevel) || !validFormProfile(defenderForm,defenderLevel) ||
        static_cast<unsigned>(move)>2 || static_cast<unsigned>(defense)>3) return {0,false,100};
    const auto a=formProfile(attackerForm,attackerLevel),d=formProfile(defenderForm,defenderLevel);
    const auto offense=move==Move::Magic ? a.stats.magic : a.stats.attack;
    const auto protection=move==Move::Magic ? d.stats.resistance : d.stats.defense;
    const auto difference=static_cast<int>(offense)+(move==Move::Heavy ? 16 : 8)-static_cast<int>(protection);
    const auto raw=static_cast<std::uint32_t>(difference<4 ? 4 : difference);
    const auto percent=percentFor(a.type,d.type);
    auto damage=raw*percent/100;
    const bool reflected=move==Move::Heavy && defense==Defense::Counter;
    if (reflected || (move==Move::Physical && defense==Defense::Brace) || (move==Move::Magic && defense==Defense::Ward)) damage/=2;
    return {damage ? damage : 1,reflected,percent};
}
Hit resolve(std::uint32_t attackerSpecies,std::uint32_t attackerLevel,std::uint32_t defenderSpecies,std::uint32_t defenderLevel,Move move,Defense defense) {
    return resolveForms(forms::initialForm(attackerSpecies),attackerLevel,forms::initialForm(defenderSpecies),defenderLevel,move,defense);
}
std::size_t writeFormProfileJson(std::uint32_t value, std::uint32_t level, char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0';
    if (!validFormProfile(value, level)) return 0;
    const auto p = formProfile(value, level);
    const int n = std::snprintf(output, capacity,
        "{\"maxHp\":%" PRIu32 ",\"attack\":%" PRIu32 ",\"defense\":%" PRIu32
        ",\"magic\":%" PRIu32 ",\"resistance\":%" PRIu32 ",\"type\":\"%s\","
        "\"skills\":{\"physical\":\"%s\",\"heavy\":\"%s\",\"magic\":\"%s\"}}",
        p.stats.maxHp,p.stats.attack,p.stats.defense,p.stats.magic,p.stats.resistance,p.type,
        p.physicalSkill,p.heavySkill,p.magicSkill);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { output[0] = '\0'; return 0; }
    return static_cast<std::size_t>(n);
}
std::size_t writeProfileJson(std::uint32_t species,std::uint32_t level,char* output,std::size_t capacity) {
    return writeFormProfileJson(forms::initialForm(species),level,output,capacity);
}
std::size_t writeCatalogJson(char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0'; std::size_t used = 0;
    const auto append = [&](const char* text) {
        const auto length = std::strlen(text);
        if (length >= capacity - used) return false;
        std::memcpy(output + used, text, length + 1); used += length; return true;
    };
    if (!append("{\"rulesVersion\":6,\"types\":[\"grove\",\"tide\",\"ember\",\"neutral\"],\"typeChart\":["
        "{\"attacker\":\"grove\",\"strongAgainst\":\"tide\",\"weakAgainst\":\"ember\"},"
        "{\"attacker\":\"tide\",\"strongAgainst\":\"ember\",\"weakAgainst\":\"grove\"},"
        "{\"attacker\":\"ember\",\"strongAgainst\":\"grove\",\"weakAgainst\":\"tide\"},"
        "{\"attacker\":\"neutral\",\"strongAgainst\":null,\"weakAgainst\":null}],\"profiles\":[")) return 0;
    bool comma = false;
    for (std::uint32_t s = 1; s <= kSpeciesCount; ++s) for (std::uint32_t level = 1; level <= 1; ++level) {
        char entry[640], stats[kProfileJsonCapacity];
        if (!writeProfileJson(s,level,stats,sizeof(stats))) return 0;
        const int n = std::snprintf(entry,sizeof(entry),"%s{\"species\":\"%s\",\"level\":%" PRIu32 ",\"name\":\"%s\",\"combat\":%s}",comma ? "," : "",speciesName(s),level,profile(s,level).name,stats);
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(entry) || !append(entry)) { output[0]='\0'; return 0; }
        comma = true;
    }
    if (!append("]}")) { output[0]='\0'; return 0; }
    return used;
}
std::size_t writeStarterJson(char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0'; std::size_t used = 0;
    const auto append = [&](const char* text) {
        const auto length = std::strlen(text);
        if (length >= capacity - used) { output[0] = '\0'; return false; }
        std::memcpy(output + used, text, length + 1); used += length; return true;
    };
    if (!append("{\"formatVersion\":1,\"rulesVersion\":6,\"starters\":[")) return 0;
    for (std::uint32_t id = 1; id <= kStarterCount; ++id) {
        char entry[640], stats[kProfileJsonCapacity];
        const auto s = starterSpecies(id);
        if (!writeProfileJson(s, 1, stats, sizeof(stats))) { output[0] = '\0'; return 0; }
        const int n = std::snprintf(entry, sizeof(entry),
            "%s{\"id\":%" PRIu32 ",\"species\":\"%s\",\"name\":\"%s\",\"stage\":\"Rookie\",\"combat\":%s}",
            id == 1 ? "" : ",", id, speciesName(s), starterName(id), stats);
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(entry) || !append(entry)) { output[0] = '\0'; return 0; }
    }
    if (!append("]}")) return 0;
    return used;
}
std::size_t writeEvolutionJson(std::uint32_t species,char* output,std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0]='\0'; if (!forms::initialForm(species)) return 0;
    std::size_t used=0; bool ok=true;
    const auto append=[&](const char* format,...) {
        if (!ok) return;
        va_list args; va_start(args,format);
        const auto n=std::vsnprintf(output+used,capacity-used,format,args); va_end(args);
        if(n<0 || static_cast<std::size_t>(n)>=capacity-used) {ok=false;return;}
        used+=static_cast<std::size_t>(n);
    };
    append("{\"formatVersion\":1,\"rulesVersion\":6,\"species\":\"%s\",\"forms\":[",speciesName(species));
    bool comma=false;
    for(std::uint32_t id=1;id<=forms::kFormCount;++id) {
        const auto* f=forms::find(id); if (!f || f->lineage!=species) continue;
        char stats[kProfileJsonCapacity];
        if(!writeFormProfileJson(id,f->minLevel,stats,sizeof(stats))) {ok=false;break;}
        append("%s{\"formId\":%u,\"parentId\":%u,\"children\":[",comma ? "," : "",static_cast<unsigned>(id),static_cast<unsigned>(f->parent));
        if(f->children[0]) append("%u",static_cast<unsigned>(f->children[0]));
        if(f->children[1]) append(",%u",static_cast<unsigned>(f->children[1]));
        append("],\"name\":\"%s\",\"stage\":",f->name);
        if(f->stage==forms::Stage::Original) append("null"); else append("\"%s\"",forms::stageName(f->stage));
        append(",\"requiredLevel\":%u,\"requiredBond\":%u,\"previewLevel\":%u,\"artId\":",static_cast<unsigned>(f->minLevel),static_cast<unsigned>(f->minBond),static_cast<unsigned>(f->minLevel));
        if(f->artId) append("\"%s\"",f->artId); else append("null");
        append(",\"combat\":%s}",stats); comma=true;
    }
    append("]}"); if(!ok){output[0]='\0';return 0;} return used;
}
namespace {
class CatalogWriter {
public:
    CatalogWriter(char* output, std::size_t capacity) : output_(output), capacity_(capacity), ok_(output && capacity) {
        if (ok_) output_[0] = '\0';
    }
    void append(const char* format, ...) {
        if (!ok_) return;
        va_list args; va_start(args, format);
        const auto count = std::vsnprintf(output_ + used_, capacity_ - used_, format, args);
        va_end(args);
        if (count < 0 || static_cast<std::size_t>(count) >= capacity_ - used_) { ok_ = false; return; }
        used_ += static_cast<std::size_t>(count);
    }
    void text(const char* value) { if (value) append("\"%s\"", value); else append("%s", "null"); }
    std::size_t finish() {
        if (!ok_) { if (output_ && capacity_) output_[0] = '\0'; return 0; }
        return used_;
    }
private:
    char* output_; std::size_t capacity_, used_ = 0; bool ok_;
};
void writeStats(CatalogWriter& json, const Stats& s) {
    json.append("{\"maxHp\":%" PRIu32 ",\"attack\":%" PRIu32 ",\"defense\":%" PRIu32
                ",\"magic\":%" PRIu32 ",\"resistance\":%" PRIu32 "}", s.maxHp, s.attack, s.defense, s.magic, s.resistance);
}
void writeBrief(CatalogWriter& json, const forms::Form& f) {
    json.append("{\"formId\":%u,\"name\":\"%s\",\"lineageSlug\":\"%s\",\"stage\":\"%s\",\"combatTier\":\"%s\","
                "\"type\":\"%s\",\"minLevel\":%u,\"artId\":", static_cast<unsigned>(f.id), f.name,
                speciesName(f.lineage), forms::stageName(f.stage), forms::combatTierName(forms::combatTier(f.id)), f.type,
                static_cast<unsigned>(f.minLevel));
    json.text(f.artId);
    json.append(",\"obtainable\":%s}", forms::encounterObtainable(f.id) ? "true" : "false");
}
void writeGraphLinks(CatalogWriter& json, std::uint32_t formId) {
    json.append("\"parents\":[");
    bool comma = false;
    for (std::size_t i = 0; i < forms::edgeCount(); ++i) {
        const auto* edge = forms::edgeAt(i);
        if (edge->to == formId) {
            json.append("%s%u", comma ? "," : "", static_cast<unsigned>(edge->from)); comma = true;
        }
    }
    json.append("],\"children\":[");
    for (unsigned i = 0; const auto* edge = forms::outgoing(formId, i); ++i)
        json.append("%s%u", i ? "," : "", static_cast<unsigned>(edge->to));
    json.append("],\"edges\":[");
    for (unsigned i = 0; const auto* edge = forms::outgoing(formId, i); ++i)
        json.append("%s{\"toFormId\":%u,\"requiredLevel\":%u,\"requiredBond\":%u}", i ? "," : "",
                    static_cast<unsigned>(edge->to), static_cast<unsigned>(edge->minLevel), static_cast<unsigned>(edge->minBond));
    json.append("]");
}
}
std::size_t writeEvolutionGraphJson(std::uint32_t formId, std::uint32_t offset,
    std::uint32_t limit, char* output, std::size_t capacity) {
    if (output && capacity) output[0] = '\0';
    if (!output || !capacity || !forms::find(formId) || !limit || limit > 16) return 0;
    // Undirected connectivity is for browsing only; game legality uses directed
    // outgoing edges. Fixed bitset and bounded passes keep work independent of input.
    std::uint32_t connected[(forms::kFormCount + 31) / 32]{};
    const auto contains = [&](std::uint32_t id) { return (connected[(id - 1) / 32] & (1u << ((id - 1) % 32))) != 0; };
    const auto add = [&](std::uint32_t id) { connected[(id - 1) / 32] |= 1u << ((id - 1) % 32); };
    add(formId);
    for (unsigned pass = 0; pass < forms::kFormCount; ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < forms::edgeCount(); ++i) {
            const auto* edge = forms::edgeAt(i);
            if (contains(edge->from) != contains(edge->to)) { add(edge->from); add(edge->to); changed = true; }
        }
        if (!changed) break;
    }
    unsigned total = 0;
    for (unsigned id = 1; id <= forms::kFormCount; ++id) if (contains(id)) ++total;
    if (offset > total) return 0;
    const auto count = limit < total - offset ? limit : total - offset;
    CatalogWriter json(output, capacity);
    json.append("{\"formatVersion\":2,\"rulesVersion\":6,\"catalogVersion\":%u,\"focusFormId\":%u,"
                "\"offset\":%u,\"limit\":%u,\"total\":%u,\"nextOffset\":", static_cast<unsigned>(forms::kCatalogVersion),
                static_cast<unsigned>(formId), static_cast<unsigned>(offset), static_cast<unsigned>(limit), total);
    if (offset + count < total) json.append("%u", static_cast<unsigned>(offset + count)); else json.append("null");
    json.append(",\"forms\":[");
    unsigned seen = 0, written = 0;
    for (unsigned id = 1; id <= forms::kFormCount && written < count; ++id) if (contains(id)) {
        if (seen++ < offset) continue;
        const auto* f = forms::find(id);
        char profileJson[kProfileJsonCapacity];
        if (!writeFormProfileJson(id, f->minLevel, profileJson, sizeof(profileJson))) { output[0] = '\0'; return 0; }
        json.append("%s{\"formId\":%u,\"name\":\"%s\",\"stage\":", written ? "," : "", id, f->name);
        json.text(f->stage == forms::Stage::Original ? nullptr : forms::stageName(f->stage));
        json.append(",\"artId\":"); json.text(f->artId);
        json.append(",\"minLevel\":%u,\"minBond\":%u,\"previewLevel\":%u,\"combat\":%s,",
                    static_cast<unsigned>(f->minLevel), static_cast<unsigned>(f->minBond), static_cast<unsigned>(f->minLevel), profileJson);
        writeGraphLinks(json, id);
        json.append("}"); ++written;
    }
    json.append("]}"); return json.finish();
}
std::size_t writeCatalogPageJson(std::uint32_t offset, std::uint32_t limit, char* output, std::size_t capacity) {
    if (output && capacity) output[0] = '\0';
    if (!output || !capacity || !limit || limit > 16 || offset > forms::kFormCount) return 0;
    CatalogWriter json(output, capacity);
    const auto count = limit < forms::kFormCount - offset ? limit : forms::kFormCount - offset;
    json.append("{\"formatVersion\":1,\"rulesVersion\":6,\"catalogVersion\":%" PRIu32
                ",\"total\":%" PRIu32 ",\"offset\":%" PRIu32 ",\"nextOffset\":", forms::kCatalogVersion, forms::kFormCount, offset);
    if (offset + count < forms::kFormCount) json.append("%" PRIu32, offset + count); else json.append("%s", "null");
    json.append(",\"forms\":[");
    for (std::uint32_t i = 0; i < count; ++i) {
        if (i) json.append(",");
        writeBrief(json, *forms::find(offset + i + 1));
    }
    json.append("]}"); return json.finish();
}
std::size_t writeFormCatalogJson(std::uint32_t formId, char* output, std::size_t capacity) {
    if (output && capacity) output[0] = '\0';
    const auto* f = forms::find(formId);
    if (!f || !output || !capacity) return 0;
    const auto* entry = forms::catalogEntry(formId);
    CatalogWriter json(output, capacity);
    json.append("{\"formId\":%u,\"name\":\"%s\",\"lineageId\":%u,\"lineageSlug\":\"%s\",\"stage\":\"%s\",\"combatTier\":\"%s\","
                "\"type\":\"%s\",\"role\":\"%s\",\"parent\":%u,\"children\":[", static_cast<unsigned>(f->id), f->name,
                static_cast<unsigned>(f->lineage), speciesName(f->lineage), forms::stageName(f->stage), forms::combatTierName(forms::combatTier(formId)),
                f->type, formId <= forms::kPreservedFormCount ? "Preserved" : entry->role, static_cast<unsigned>(f->parent));
    if (f->children[0]) json.append("%u", static_cast<unsigned>(f->children[0]));
    if (f->children[1]) json.append(",%u", static_cast<unsigned>(f->children[1]));
    json.append("],\"minLevel\":%u,\"minBond\":%u,\"skills\":{\"physical\":\"%s\",\"heavy\":\"%s\",\"magic\":\"%s\"},\"baseStats\":",
                static_cast<unsigned>(f->minLevel), static_cast<unsigned>(f->minBond), f->physicalSkill, f->heavySkill, f->magicSkill);
    writeStats(json, f->baseStats); json.append(",\"growth\":"); writeStats(json, f->growth);
    json.append(",\"preserved\":%s,\"statModel\":\"%s\",\"statsByLevel\":[", formId <= forms::kPreservedFormCount ? "true" : "false",
                formId <= forms::kPreservedFormCount ? "preserved-v4" : "linear-v1");
    for (unsigned level = f->minLevel; level <= forms::kMaxRpgLevel; ++level) {
        const auto s = forms::stats(formId, level);
        json.append("%s{\"level\":%u,\"maxHp\":%" PRIu32 ",\"attack\":%" PRIu32 ",\"defense\":%" PRIu32
                    ",\"magic\":%" PRIu32 ",\"resistance\":%" PRIu32 "}", level == f->minLevel ? "" : ",", level,
                    s.maxHp, s.attack, s.defense, s.magic, s.resistance);
    }
    json.append("],\"artId\":"); json.text(f->artId);
    json.append(",\"entryKey\":"); json.text(entry ? entry->entryKey : nullptr);
    json.append(",\"obtainable\":%s,\"leafReason\":", forms::encounterObtainable(formId) ? "true" : "false");
    json.text(forms::leafReason(formId));
    json.append(",\"evolution\":{"); writeGraphLinks(json, formId);
    json.append(",\"status\":"); json.text(forms::evolutionStatus(formId));
    json.append(",\"reason\":"); json.text(forms::leafReason(formId));
    json.append("}}"); return json.finish();
}
} // namespace digivice::legacy_v6::combat

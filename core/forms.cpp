#include "forms.hpp"
#include <cstring>

namespace digivice::forms {
namespace {
using Stats = combat::Stats;
enum class Role : std::uint8_t { Balanced, Physical, Guardian, Mystic, Hybrid };

constexpr Stats kRookieBase[8] = {
    {92,16,12,24,14}, {104,24,14,14,11}, {104,20,18,13,12}, {96,14,13,18,20},
    {108,16,23,12,11}, {104,13,16,22,12}, {112,16,19,12,14}, {92,23,12,18,13}
};
constexpr Stats kOldTier2Bonus{16,6,5,7,6};
constexpr Stats kOldTier3Bonus{36,14,12,16,14};

constexpr Stats add(Stats a, Stats b) {
    return {a.maxHp+b.maxHp,a.attack+b.attack,a.defense+b.defense,
            a.magic+b.magic,a.resistance+b.resistance};
}
constexpr Stats times(Stats value, std::uint32_t count) {
    return {value.maxHp*count,value.attack*count,value.defense*count,
            value.magic*count,value.resistance*count};
}
constexpr Stats growth(Role role) {
    switch (role) {
        case Role::Balanced: return {4,1,1,1,1};
        case Role::Physical: return {4,2,1,1,1};
        case Role::Guardian: return {5,1,2,1,2};
        case Role::Mystic: return {3,1,1,2,2};
        case Role::Hybrid: return {4,2,1,2,1};
    }
    return {};
}
constexpr Stats stageBonus(Stage stage) {
    switch (stage) {
        case Stage::Champion: return {28,10,9,11,10};
        case Stage::Ultimate: return {64,24,22,26,24};
        case Stage::Mega: return {104,40,36,43,40};
        default: return {};
    }
}
constexpr std::uint8_t stageRank(Stage stage) {
    return stage == Stage::Champion ? 1 : stage == Stage::Ultimate ? 2 :
           stage == Stage::Mega ? 3 : 0;
}
constexpr Stats roleBonus(Role role, std::uint32_t rank) {
    switch (role) {
        case Role::Balanced: return {2*rank,rank,rank,rank,rank};
        case Role::Physical: return {0,2*rank,0,0,0};
        case Role::Guardian: return {4*rank,0,2*rank,0,2*rank};
        case Role::Mystic: return {0,0,0,2*rank,2*rank};
        case Role::Hybrid: return {0,rank,0,rank,0};
    }
    return {};
}
constexpr const char* lineageType(std::uint8_t lineage) {
    return lineage == 5 || lineage == 6 ? "ember" :
           lineage == 7 || lineage == 11 ? "tide" :
           lineage == 9 || lineage == 10 ? "grove" : "neutral";
}
constexpr Form original(std::uint8_t id, std::uint8_t lineage, std::uint8_t parent,
                        std::uint8_t child, std::uint8_t level, std::uint8_t bond,
                        const char* name, const char* type, const char* physical,
                        const char* heavy, const char* magic, const char* art,
                        Stats base, Stats perLevel) {
    return {id,lineage,Stage::Original,parent,{child,0},level,bond,
            name,type,physical,heavy,magic,art,base,perLevel};
}
constexpr Form rookie(std::uint8_t id, std::uint8_t lineage, Role role,
                      const char* name, const char* physical, const char* heavy,
                      const char* magic) {
    return {id,lineage,Stage::Rookie,0,
            {static_cast<std::uint8_t>(id+1),static_cast<std::uint8_t>(id+4)},1,0,
            name,lineageType(lineage),physical,heavy,magic,nullptr,
            kRookieBase[lineage-5],growth(role)};
}
constexpr Form evolved(std::uint8_t id, std::uint8_t lineage, Stage stage,
                       std::uint8_t parent, std::uint8_t child, Role role,
                       const char* name, const char* physical, const char* heavy,
                       const char* magic) {
    const auto rank = stageRank(stage);
    return {id,lineage,stage,parent,{child,0},static_cast<std::uint8_t>(rank*5),
            static_cast<std::uint8_t>(rank == 1 ? 20 : rank == 2 ? 50 : 80),
            name,lineageType(lineage),physical,heavy,magic,nullptr,
            add(add(kRookieBase[lineage-5],stageBonus(stage)),roleBonus(role,rank)),
            growth(role)};
}

// All new evolution skill names below are authored presentation labels. They
// select the shared Physical/Heavy/Magic mechanics, not species-specific powers.
// Original and Rookie skill labels are retained from the pre-RPG prototype.
constexpr Form kForms[kFormCount] = {
    original(1,1,0,2,1,0,"Mote","grove","Twig Tap","Root Ram","Seed Spark","mote",
             {100,18,14,16,16},{3,1,1,1,1}),
    original(2,1,1,3,5,20,"Glint","grove","Briar Swipe","Timber Rush","Bloom Flash","glint",
             {116,24,19,23,22},{3,1,1,1,1}),
    original(3,1,2,0,10,50,"Lumen","grove","Canopy Claw","Ancient Crash","Solar Bloom","lumen",
             {164,42,36,46,44},{4,1,1,2,2}),
    original(4,2,0,0,1,0,"Flicker","neutral","Quick Peck","Comet Dive","Glimmer Pulse","flicker",
             {88,20,12,14,12},{3,1,1,1,1}),
    original(5,3,0,6,1,0,"Rill","tide","Fin Slap","River Rush","Bubble Burst","rill",
             {104,14,16,20,18},{3,1,1,1,1}),
    original(6,3,5,7,5,20,"Brine","tide","Reef Strike","Breaker Bash","Tidal Orb","brine",
             {120,20,23,29,26},{4,1,1,2,1}),
    original(7,3,6,0,10,50,"Pelagia","tide","Trident Sweep","Maelstrom Ram","Abyssal Wave","pelagia",
             {168,38,38,50,46},{4,1,1,2,2}),
    original(8,4,0,9,1,0,"Cinder","ember","Coal Claw","Furnace Charge","Ember Shot","cinder",
             {96,22,12,18,12},{3,2,1,1,1}),
    original(9,4,8,10,5,20,"Scoria","ember","Obsidian Slash","Magma Crash","Lava Lance","scoria",
             {112,32,18,25,18},{3,2,1,1,1}),
    original(10,4,9,0,10,50,"Pyrel","ember","Inferno Talon","Volcano Break","Phoenix Flare","pyrel",
             {132,44,25,34,26},{4,2,1,2,1}),

    rookie(11,5,Role::Mystic,"Impmon","Prank Jab","Imp Rush","Night of Fire"),
    evolved(12,5,Stage::Champion,11,13,Role::Mystic,"Wizardmon","Staff Tap","Rune Crash","Hex Spark"),
    evolved(13,5,Stage::Ultimate,12,14,Role::Mystic,"Baalmon","Charm Slash","Script Break","Dusk Seal"),
    evolved(14,5,Stage::Mega,13,0,Role::Mystic,"Beelzemon","Claw Sweep","Demon Rush","Night Barrage"),
    evolved(15,5,Stage::Champion,11,16,Role::Hybrid,"Devimon","Wing Rake","Shadow Lunge","Void Claw"),
    evolved(16,5,Stage::Ultimate,15,17,Role::Hybrid,"Myotismon","Cape Slash","Midnight Crush","Bat Swarm"),
    evolved(17,5,Stage::Mega,16,0,Role::Hybrid,"VenomMyotismon","Venom Rake","Abyss Stomp","Toxic Eclipse"),

    rookie(18,6,Role::Physical,"Agumon","Claw Jab","Dino Charge","Pepper Breath"),
    evolved(19,6,Stage::Champion,18,20,Role::Physical,"Greymon","Horn Sweep","Tyrant Charge","Blazing Breath"),
    evolved(20,6,Stage::Ultimate,19,21,Role::Physical,"MetalGreymon (Vaccine)","Steel Claw","Trident Crash","Missile Flare"),
    evolved(21,6,Stage::Mega,20,0,Role::Physical,"WarGreymon","Drill Claw","Brave Rush","Terra Blaze"),
    evolved(22,6,Stage::Champion,18,23,Role::Guardian,"Tyrannomon","Tail Swipe","Dino Stomp","Ember Roar"),
    evolved(23,6,Stage::Ultimate,22,24,Role::Guardian,"MetalTyrannomon","Alloy Talon","Siege Stomp","Reactor Bolt"),
    evolved(24,6,Stage::Mega,23,0,Role::Guardian,"RustTyrannomon","Rust Claw","Fortress Break","Core Cannon"),

    rookie(25,7,Role::Balanced,"Gabumon","Horn Jab","Horn Rush","Blue Blaster"),
    evolved(26,7,Stage::Champion,25,27,Role::Physical,"Garurumon","Fang Snap","Wolf Rush","Frost Howl"),
    evolved(27,7,Stage::Ultimate,26,28,Role::Physical,"WereGarurumon","Wolf Kick","Crescent Rush","Moon Pulse"),
    evolved(28,7,Stage::Mega,27,0,Role::Physical,"MetalGarurumon","Steel Fang","Glacier Charge","Ice Barrage"),
    evolved(29,7,Stage::Champion,25,30,Role::Guardian,"Leomon","Lion Palm","Pride Rush","Roaring Fist"),
    evolved(30,7,Stage::Ultimate,29,31,Role::Guardian,"GrapLeomon","Gear Jab","Cyclone Slam","Spiral Burst"),
    evolved(31,7,Stage::Mega,30,0,Role::Guardian,"BanchoLeomon","Brave Fist","King Rush","Justice Roar"),

    rookie(32,8,Role::Mystic,"Patamon","Wing Slap","Air Tackle","Air Shot"),
    evolved(33,8,Stage::Champion,32,34,Role::Mystic,"Angemon","Staff Strike","Halo Dive","Radiant Fist"),
    evolved(34,8,Stage::Ultimate,33,35,Role::Mystic,"MagnaAngemon","Holy Edge","Gate Rush","Radiant Seal"),
    evolved(35,8,Stage::Mega,34,0,Role::Mystic,"Seraphimon","Seraph Edge","Heavenfall","Sevenfold Ray"),
    evolved(36,8,Stage::Champion,32,37,Role::Balanced,"Unimon","Horn Strike","Cloud Charge","Sky Bolt"),
    evolved(37,8,Stage::Ultimate,36,38,Role::Balanced,"HippoGryphonmon","Talon Sweep","Gale Pounce","Sky Cyclone"),
    evolved(38,8,Stage::Mega,37,0,Role::Balanced,"Gryphonmon","Royal Talon","Mythic Dive","Storm Cry"),

    rookie(39,9,Role::Guardian,"Tentomon","Shell Tap","Beetle Bash","Super Shocker"),
    evolved(40,9,Stage::Champion,39,41,Role::Guardian,"Kabuterimon","Horn Thrust","Shell Break","Thunder Orb"),
    evolved(41,9,Stage::Ultimate,40,42,Role::Guardian,"MegaKabuterimon (Red)","Crimson Horn","Atlas Charge","Volt Cannon"),
    evolved(42,9,Stage::Mega,41,0,Role::Guardian,"HerculesKabuterimon","Titan Horn","Hercules Crush","Storm Nova"),
    evolved(43,9,Stage::Champion,39,44,Role::Physical,"Kuwagamon","Pincer Cut","Scissor Rush","Sonic Edge"),
    evolved(44,9,Stage::Ultimate,43,45,Role::Physical,"Okuwamon","Obsidian Claw","Hollow Cleave","Void Cutter"),
    evolved(45,9,Stage::Mega,44,0,Role::Physical,"GranKuwagamon","Grand Pincer","Dimension Rush","Abyss Edge"),

    rookie(46,10,Role::Mystic,"Palmon","Vine Lash","Root Slam","Poison Ivy"),
    evolved(47,10,Stage::Champion,46,48,Role::Mystic,"Togemon","Cactus Jab","Needle Slam","Thorn Volley"),
    evolved(48,10,Stage::Ultimate,47,49,Role::Mystic,"Lillymon","Petal Strike","Bloom Rush","Floral Ray"),
    evolved(49,10,Stage::Mega,48,0,Role::Mystic,"Rosemon","Rose Lash","Thorn Waltz","Crimson Bloom"),
    evolved(50,10,Stage::Champion,46,51,Role::Guardian,"Woodmon","Branch Swipe","Trunk Slam","Sap Snare"),
    evolved(51,10,Stage::Ultimate,50,52,Role::Guardian,"Cherrymon","Bough Sweep","Forest Crush","Cherry Burst"),
    evolved(52,10,Stage::Mega,51,0,Role::Guardian,"Puppetmon","Wooden Hammer","Marionette Slam","String Storm"),

    rookie(53,11,Role::Guardian,"Gomamon","Flipper Slap","Iceberg Rush","Marching Fishes"),
    evolved(54,11,Stage::Champion,53,55,Role::Guardian,"Ikkakumon","Tusk Jab","Icebreaker","Harpoon Burst"),
    evolved(55,11,Stage::Ultimate,54,56,Role::Guardian,"Zudomon","Hammer Tap","Glacier Smash","Thunder Tide"),
    evolved(56,11,Stage::Mega,55,0,Role::Guardian,"Vikemon","Mace Sweep","Viking Crash","Polar Tempest"),
    evolved(57,11,Stage::Champion,53,58,Role::Mystic,"Shellmon","Shell Strike","Reef Crash","Water Jet"),
    evolved(58,11,Stage::Ultimate,57,59,Role::Mystic,"Whamon","Fin Sweep","Deepwater Crush","Ocean Surge"),
    evolved(59,11,Stage::Mega,58,0,Role::Mystic,"Plesiomon","Neck Sweep","Abyss Ram","Sorrow Tide"),

    rookie(60,12,Role::Hybrid,"Renamon","Palm Strike","Fox Rush","Diamond Storm"),
    evolved(61,12,Stage::Champion,60,62,Role::Mystic,"Kyubimon","Fox Claw","Spiral Rush","Spirit Flame"),
    evolved(62,12,Stage::Ultimate,61,63,Role::Mystic,"Taomon","Talisman Swipe","Seal Rush","Radiant Script"),
    evolved(63,12,Stage::Mega,62,0,Role::Mystic,"Sakuyamon","Ritual Staff","Spirit Descent","Celestial Seal"),
    evolved(64,12,Stage::Champion,60,65,Role::Hybrid,"Youkomon","Ghost Claw","Phantom Rush","Foxfire Ring"),
    evolved(65,12,Stage::Ultimate,64,66,Role::Hybrid,"Doumon","Scroll Lash","Shadow Seal","Dusk Script"),
    evolved(66,12,Stage::Mega,65,0,Role::Hybrid,"Kuzuhamon","Sacred Staff","Twilight Descent","Veiled Mandala"),
// Remaining IDs are generated from reviewed metadata; old66 above never normalize.
#include "world_ds_forms_generated.inc"
};
#include "world_ds_catalog_generated.inc"
#include "world_ds_evolutions_generated.inc"

constexpr std::uint32_t interpolate(std::uint32_t from, std::uint32_t to,
                                    std::uint32_t step, std::uint32_t span) {
    return from+(to-from)*step/span;
}
constexpr Stats interpolate(Stats from, Stats to, std::uint32_t step, std::uint32_t span) {
    return {interpolate(from.maxHp,to.maxHp,step,span),
            interpolate(from.attack,to.attack,step,span),
            interpolate(from.defense,to.defense,step,span),
            interpolate(from.magic,to.magic,step,span),
            interpolate(from.resistance,to.resistance,step,span)};
}
constexpr Stats compute(const Form& form, std::uint32_t level) {
    if (level < form.minLevel || level > kMaxRpgLevel) return {};
    if (form.id > kPreservedFormCount || form.stage != Stage::Rookie)
        return add(form.baseStats,times(form.growth,level-form.minLevel));
    const auto bonus = level <= 5 ? interpolate({},kOldTier2Bonus,level-1,4) :
                       level <= 10 ? interpolate(kOldTier2Bonus,kOldTier3Bonus,level-5,5) :
                       add(kOldTier3Bonus,times(form.growth,level-10));
    return add(form.baseStats,bonus);
}
constexpr bool bounded(Stats s) {
    return s.maxHp >= 1 && s.maxHp <= 2048 && s.attack >= 1 && s.attack <= 256 &&
           s.defense >= 1 && s.defense <= 256 && s.magic >= 1 && s.magic <= 256 &&
           s.resistance >= 1 && s.resistance <= 256;
}
constexpr bool safeLabel(const char* s) {
    if (!s || !s[0]) return false;
    std::size_t n = 0;
    for (; s[n]; ++n)
        if (n >= 64 || static_cast<unsigned char>(s[n]) < 32 || s[n] == 127 || s[n] == '"' || s[n] == '\\') return false;
    return n > 0;
}
constexpr bool validTable() {
    for (std::size_t i = 0; i < kFormCount; ++i) {
        const auto& f = kForms[i];
        if (f.id != i+1 || f.lineage < 1 || (f.id <= kPreservedFormCount && f.lineage > 12) || !f.minLevel ||
            f.minLevel > kMaxRpgLevel || f.minBond > 200 ||
            !safeLabel(f.name) || !safeLabel(f.type) || !safeLabel(f.physicalSkill) ||
            !safeLabel(f.heavySkill) || !safeLabel(f.magicSkill)) return false;
        if ((f.id <= 10) != (f.artId != nullptr) || (f.artId && !safeLabel(f.artId))) return false;
        if (f.parent) {
            if (f.parent > kFormCount) return false;
            const auto& p = kForms[f.parent-1];
            if (p.lineage != f.lineage || p.minLevel >= f.minLevel ||
                (p.children[0] != f.id && p.children[1] != f.id)) return false;
        }
        for (const auto child : f.children) if (child) {
            if (child > kFormCount || kForms[child-1].parent != f.id ||
                kForms[child-1].lineage != f.lineage) return false;
        }
        if (f.children[0] && f.children[0] == f.children[1]) return false;
        for (auto level = f.minLevel; level <= kMaxRpgLevel; ++level)
            if (!bounded(compute(f,level))) return false;
    }
    return true;
}
static_assert(validTable(), "Form IDs, edges, text, art and bounded stat curves must be valid");
constexpr bool validEdges() {
    unsigned outgoingCounts[kFormCount]{};
    for (const auto& edge : kEvolutionEdges) {
        if (!edge.from || edge.from > kFormCount || !edge.to || edge.to > kFormCount ||
            edge.from == edge.to || ++outgoingCounts[edge.from - 1] > 2 ||
            edge.minLevel < kForms[edge.to - 1].minLevel || edge.minLevel > kMaxRpgLevel ||
            edge.minBond < kForms[edge.to - 1].minBond || edge.minBond > 200) return false;
    }
    return true;
}
static_assert(validEdges(), "Evolution route IDs, choices and gates must be bounded");
constexpr bool earnedEvolutionNeeds() {
    for (const auto& edge : kEvolutionEdges) {
        const auto& src = kForms[edge.from - 1];
        const auto& dest = kForms[edge.to - 1];
        const auto need = evolutionNeedFor(src.minLevel, src.minBond, dest.stage, dest.minLevel, dest.minBond, edge.minLevel, edge.minBond);
        if (need.level <= src.minLevel || need.level > kMaxRpgLevel || need.care < 12 || need.care > 100 || need.bond > 200) return false;
        if (src.minBond < 200 && need.bond <= src.minBond) return false;
    }
    return true;
}
static_assert(earnedEvolutionNeeds(), "Every evolution route must be earned and still reachable by level 50");
} // namespace

const Form* find(std::uint32_t id) {
    return id >= 1 && id <= kFormCount ? &kForms[id-1] : nullptr;
}
bool productionForm(std::uint32_t id) { return id>=kFirstProductionFormId && id<=kFormCount; }
std::size_t edgeCount() { return sizeof(kEvolutionEdges) / sizeof(kEvolutionEdges[0]); }
const EvolutionEdge* edgeAt(std::size_t index) { return index < edgeCount() ? &kEvolutionEdges[index] : nullptr; }
const EvolutionEdge* outgoing(std::uint32_t id, std::uint32_t index) {
    if (!find(id) || index >= 2) return nullptr;
    for (const auto& edge : kEvolutionEdges) if (edge.from == id) {
        if (!index) return &edge;
        --index;
    }
    return nullptr;
}
bool canReach(std::uint32_t from, std::uint32_t to) {
    if (!find(from) || !find(to)) return false;
    std::uint32_t reached[(kFormCount + 31) / 32]{};
    const auto contains = [&](std::uint32_t id) { return (reached[(id - 1) / 32] & (1u << ((id - 1) % 32))) != 0; };
    const auto add = [&](std::uint32_t id) { reached[(id - 1) / 32] |= 1u << ((id - 1) % 32); };
    add(from);
    for (std::uint32_t pass = 0; pass < kFormCount && !contains(to); ++pass) {
        bool changed = false;
        for (const auto& edge : kEvolutionEdges) if (contains(edge.from) && !contains(edge.to)) {
            add(edge.to); changed = true;
        }
        if (!changed) break;
    }
    return contains(to);
}
bool validForLineage(std::uint32_t id, std::uint32_t lineage) {
    const auto* form = find(id);
    return form && form->lineage == lineage;
}
std::uint32_t initialForm(std::uint32_t lineage) {
    constexpr std::uint8_t originalIds[]{0,1,4,5,8};
    if (lineage <= 4) return originalIds[lineage];
    if (lineage <= 12) return 11+7*(lineage-5);
    for (const auto& f : kForms) if (f.lineage == lineage && !f.parent) return f.id;
    return 0;
}
std::uint32_t migrateLegacyForm(std::uint32_t lineage, std::uint32_t level) {
    if (!lineage || lineage > 12 || !level || level > 3 || (lineage == 2 && level != 1)) return 0;
    const auto first = initialForm(lineage);
    return lineage <= 4 && lineage != 2 ? first+level-1 : first;
}
const char* stageName(Stage stage) {
    switch (stage) {
        case Stage::Original: return "Original";
        case Stage::Rookie: return "Rookie";
        case Stage::Champion: return "Champion";
        case Stage::Ultimate: return "Ultimate";
        case Stage::Mega: return "Mega";
        case Stage::Fresh: return "Fresh";
        case Stage::InTraining: return "In-Training";
        case Stage::Armor: return "Armor";
        case Stage::NoLevel: return "No Level";
    }
    return nullptr;
}
combat::Stats stats(std::uint32_t id, std::uint32_t level) {
    const auto* form = find(id);
    return form ? compute(*form,level) : combat::Stats{};
}
CombatTier combatTier(std::uint32_t id) {
    const auto* f = find(id);
    if (!f) return CombatTier::Unknown;
    if (f->authoredTier != CombatTier::Unknown) return f->authoredTier;
    return f->minLevel >= 15 ? CombatTier::Mega : f->minLevel >= 10 ? CombatTier::Ultimate :
        f->minLevel >= 5 ? CombatTier::Champion : CombatTier::Rookie;
}
const char* combatTierName(CombatTier tier) {
    switch (tier) {
    case CombatTier::Fresh: return "Fresh";
    case CombatTier::InTraining: return "In-Training";
    case CombatTier::Rookie: return "Rookie";
    case CombatTier::Champion: return "Champion";
    case CombatTier::Ultimate: return "Ultimate";
    case CombatTier::Mega: return "Mega";
    default: return nullptr;
    }
}
const CatalogEntry* catalogEntry(std::uint32_t id) {
    for (const auto& entry : kCatalogEntries) if (entry.formId == id) return &entry;
    return nullptr;
}
const char* lineageSlug(std::uint32_t lineage) {
    constexpr const char* old[]{nullptr,"mote","flicker","rill","cinder","impmon","agumon","gabumon",
                               "patamon","tentomon","palmon","gomamon","renamon"};
    if (lineage <= 12) return old[lineage];
    const auto* entry = catalogEntry(initialForm(lineage));
    return entry ? entry->entryKey : nullptr;
}
std::uint32_t lineageFromSlug(const char* slug) {
    if (!slug) return 0;
    for (const auto& f : kForms) if (!f.parent) {
        const auto* name = lineageSlug(f.lineage);
        if (name && std::strcmp(name, slug) == 0) return f.lineage;
    }
    return 0;
}
bool encounterObtainable(std::uint32_t id) { return productionForm(id); }
const char* leafReason(std::uint32_t id) {
    return find(id) ? kEvolutionLeafReasons[id - 1] : nullptr;
}
const char* evolutionStatus(std::uint32_t id) {
    if (!find(id)) return nullptr;
    return outgoing(id, 0) ? "progression" : kEvolutionTerminalLeaves[id - 1] ? "terminal" : "independent";
}
} // namespace digivice::forms

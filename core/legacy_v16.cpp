// Frozen rules 16 / schema 23 before rules 17 care mistakes, care-gated routes and injury.
#include "legacy_v16.hpp"
#include "capture_ring.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include "legacy_forms_v6.hpp"
#include "legacy_forms_v7.hpp"
#include "legacy_forms_v8.hpp"
#include "legacy_combat_v8.hpp"
#include "legacy_combat_v7.hpp"
#include "legacy_combat_v3.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace digivice::legacy_v16 {
const CreatureMember* findMember(const State& state,std::uint32_t id) {
    if(!id || state.collectionCount>kCollectionCapacity) return nullptr;
    for(std::size_t i=0;i<state.collectionCount;++i) if(state.collection[i].id==id) return &state.collection[i];
    return nullptr;
}
const CreatureMember* activeMember(const State& state) { return findMember(state,state.activeCreatureId); }
bool isPartyMember(const State& s,std::uint32_t id) {
    if(!id)return false;
    for(const auto selected:s.partyMemberIds)if(selected==id)return true;
    return false;
}
std::size_t partyCount(const State& s) {
    std::size_t count=0;for(const auto id:s.partyMemberIds)if(id)++count;return count;
}
void reconcileParty(State& s) {
    std::uint32_t retained[kPartyCapacity]{};std::size_t count=0;
    for(const auto id:s.partyMemberIds) {
        if(!id||id==s.activeCreatureId||!findMember(s,id))continue;
        bool duplicate=false;for(std::size_t i=0;i<count;++i)duplicate|=retained[i]==id;
        if(!duplicate)retained[count++]=id;
    }
    for(std::size_t i=0;i<kPartyCapacity;++i)s.partyMemberIds[i]=retained[i];
}
const CreatureMember* collectionMemberAtDisplayIndex(const State& s,std::size_t index) {
    if(s.collectionCount>kCollectionCapacity||index>=s.collectionCount)return nullptr;
    if(index==0)return activeMember(s);
    --index;
    for(const auto id:s.partyMemberIds)if(id){if(index==0)return findMember(s,id);--index;}
    for(std::size_t i=s.collectionCount;i>0;--i){const auto& m=s.collection[i-1];
        if(m.id==s.activeCreatureId||isPartyMember(s,m.id))continue;
        if(index==0)return &m;
        --index;
    }
    return nullptr;
}
std::size_t displayIndexForMember(const State& s,std::uint32_t id) {
    if(!id||s.collectionCount>kCollectionCapacity)return s.collectionCount;
    if(id==s.activeCreatureId)return 0;
    std::size_t index=1;
    for(const auto selected:s.partyMemberIds)if(selected){if(selected==id)return index;++index;}
    for(std::size_t i=s.collectionCount;i>0;--i){const auto candidate=s.collection[i-1].id;
        if(candidate==s.activeCreatureId||isPartyMember(s,candidate))continue;
        if(candidate==id)return index;
        ++index;
    }
    return s.collectionCount;
}

combat::CareBonus memberCare(const CreatureMember& member){return combat::careBonus(member.bond,member.fullness,member.mood);}
combat::Profile memberBattleProfile(const State& state,const CreatureMember& member){
    if(state.phase==Phase::Encounter&&member.id==state.activeCreatureId&&state.wildRules<12){
        if(state.wildRules<8){const auto p=legacy_v7::combat::formProfile(member.formId,member.level);return {p.name,p.type,p.physicalSkill,p.heavySkill,p.magicSkill,{p.stats.maxHp,p.stats.attack,p.stats.defense,p.stats.magic,p.stats.resistance}};}
        if(state.wildRules==8){const auto p=legacy_v8::combat::formProfile(member.formId,member.level);return {p.name,p.type,p.physicalSkill,p.heavySkill,p.magicSkill,{p.stats.maxHp,p.stats.attack,p.stats.defense,p.stats.magic,p.stats.resistance}};}
        return combat::formProfile(member.formId,member.level);
    }
    return combat::careProfile(member.formId,member.level,memberCare(member));
}
const char* captureResultName(CaptureResult result){
    switch(result){case CaptureResult::None:return "none";case CaptureResult::Miss:return "miss";case CaptureResult::Escaped:return "escaped";case CaptureResult::Captured:return "captured";}return "invalid";
}
namespace {
// Reviewed catalog6 Rookies with Lv1 profiles, named skills, an outgoing route,
// and exact-form artwork in the installed251 pack. See STARTER_OFFERS.md.
constexpr std::uint32_t offerPool[]{78,79,80,81,82,84,85,88,89,90,91,92,93,94,95,96,97,98,99,100,101,103,105,106,109,110,112,113,114,116,117,118};
void drawStarterOffers(std::uint32_t seed,std::uint32_t (&offers)[3]){
    std::uint32_t pool[sizeof(offerPool)/sizeof(offerPool[0])];std::memcpy(pool,offerPool,sizeof(pool));
    auto rng=seed;std::size_t count=sizeof(pool)/sizeof(pool[0]);
    for(auto& offer:offers){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;const auto index=rng%count;offer=pool[index];pool[index]=pool[--count];}
}
}
bool validStarterOfferForm(std::uint32_t formId){for(const auto id:offerPool)if(id==formId)return true;return false;}
std::uint32_t starterForm(const State& state,std::uint32_t slot){
    if(slot>=1&&slot<=8)return forms::initialForm(combat::starterSpecies(slot));
    if(slot>=9&&slot<=11&&state.starterOfferSeed&&validStarterOfferForm(state.starterOffers[slot-9]))return state.starterOffers[slot-9];
    return 0;
}
bool hasObtained(const State& state,std::uint32_t id) {
    return id>=1 && id<=kJournalCapacity && (state.journal[(id-1)/32]&(1u<<((id-1)%32)));
}
combat::Defense wildGuard(const State& state) {
    if(state.phase!=Phase::Encounter || state.wildRules<5) return combat::Defense::None;
    constexpr combat::Defense guards[]{combat::Defense::Brace,combat::Defense::Ward,combat::Defense::Counter};
    return guards[(state.encounters-1+state.wildTurn)%3];
}
namespace {
bool ownsExactForm(const State& state, std::uint32_t formId) {
    if (!formId) return false;
    for (std::size_t i = 0; i < state.collectionCount && i < kCollectionCapacity; ++i)
        if (state.collection[i].formId == formId) return true;
    return false;
}
}
std::uint32_t captureChance(const State& state) {
    const bool mergeable = state.wildRules >= 16 && ownsExactForm(state, state.wildFormId);
    if(!isValid(state) || state.phase!=Phase::Encounter || state.captureDeferred ||
       (state.collectionCount>=kCollectionCapacity && !mergeable) ||
       state.nextMemberId==std::numeric_limits<std::uint32_t>::max() || state.captures==std::numeric_limits<std::uint32_t>::max() ||
       state.sequence==std::numeric_limits<std::uint32_t>::max() || state.captureAttempts>=3 || state.wildHp>state.wildMaxHp/2) return 0;
    if(state.wildRules<8) return 70u+(state.level<3?state.level:3u)*5u;
    const auto base=50u+40u*(state.wildMaxHp-2u*state.wildHp)/state.wildMaxHp;
    if(state.wildRules<12)return base;
    const auto rarity=encounters::rarityForForm(state.wildFormId);
    const auto rarityPenalty=rarity==encounters::Rarity::Rare?20u:rarity==encounters::Rarity::Uncommon?10u:0u;
    const auto difference=state.wildLevel>state.level?state.wildLevel-state.level:0u;
    const auto levelPenalty=5u*(difference>5?5:difference);
    const auto penalty=rarityPenalty+levelPenalty;
    return base>penalty+10?base-penalty:10u;
}
std::uint32_t ringCaptureChance(const State& state, std::uint32_t phaseMs) {
    if (phaseMs >= capturering::kCycleMs ||
        needsTestEncounterResolution(state) ||
        (state.battleMode == BattleMode::Auto && state.autoCapture != AutoCapture::Awaiting)) return 0;
    return capturering::chanceForGrade(captureChance(state), capturering::sample(phaseMs, state.wildFormId).grade);
}
bool decodeFlick(std::uint32_t value, FlickTrajectory& result) {
    if(value>kFlickMaxValue) return false;
    const auto dx=static_cast<std::int32_t>(value/256u)-160;
    const auto reach=static_cast<std::int32_t>(value%256u);
    const auto dy=180-reach;
    result={206+dx,300-reach,dx*dx+dy*dy<=48*48};
    return true;
}
std::uint32_t worldSelectionSeed(const State& state) { return state.worldSeed ? state.worldSeed : state.seed; }
std::uint32_t selectWildForm(std::uint32_t encounter,std::uint32_t seed,std::uint32_t partnerFormId,std::uint32_t rivalLevel) {
    return encounters::rules18::selectProduction(encounter,seed,partnerFormId,rivalLevel);
}
std::uint32_t wildEncounterLevel(std::uint32_t encounter,std::uint32_t seed,std::uint32_t center) {
    auto value=seed^(encounter*0x9e3779b9u);
    value^=value>>16; value*=0x7feb352du; value^=value>>15; value*=0x846ca68bu; value^=value>>16;
    const auto offset=static_cast<int>(value%3u)-1;
    auto level=static_cast<long long>(center)+offset;
    if(level<1) level=1;
    if(level>static_cast<long long>(kMaxLevel)) level=kMaxLevel;
    return static_cast<std::uint32_t>(level);
}
namespace {
CreatureMember& active(State& state) { return *const_cast<CreatureMember*>(activeMember(state)); }
void obtain(State& state,std::uint32_t id) { if(id>=1 && id<=kJournalCapacity) state.journal[(id-1)/32]|=1u<<((id-1)%32); }

constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
void drawEncounterTarget(State& state,bool afterEarned=false) {
    auto x=state.encounterRng;
    if(!x) {
        x=state.seed^0xa3c59ac3u;
        if(!x)x=0x6d2b79f5u;
    }
    x^=x<<13;x^=x>>17;x^=x<<5;
    state.encounterRng=x;
    state.encounterTarget=state.encounters==0 && !afterEarned ? 2u*(40u+x%41u) : 2u*(80u+x%61u);
}
std::uint32_t cappedAdd(std::uint32_t current, std::uint32_t amount, std::uint32_t cap) {
    return amount >= cap - current ? cap : current + amount;
}
std::uint32_t legacyLevelFor(Species species, std::uint32_t bond) {
    return species == Species::Flicker ? 1 : bond >= 100 ? 3 : bond >= 40 ? 2 : 1;
}
std::uint32_t legacyMaxHp(Species species,std::uint32_t level) {
    return legacy_v3::combat::profile(static_cast<std::uint32_t>(species),level).stats.maxHp;
}
std::uint32_t maxHp(std::uint32_t formId, std::uint32_t level) {
    return combat::formProfile(formId, level).stats.maxHp;
}
// Old encounters retain their numeric profiles until they reach Home.
std::uint32_t maxHpForRules(std::uint32_t formId,std::uint32_t level,std::uint32_t rules) {
    return rules && rules<9 ? legacy_v8::combat::formProfile(formId,level).stats.maxHp : maxHp(formId,level);
}
std::uint32_t memberMaxHp(const State& state,const CreatureMember& member) {
    return maxHpForRules(member.formId,member.level,
        state.phase==Phase::Encounter && member.id==state.activeCreatureId ? state.wildRules : 0);
}
std::uint32_t rootForm(Species species) { return forms::initialForm(static_cast<std::uint32_t>(species)); }
std::uint32_t scaleHp(std::uint32_t hp, std::uint32_t oldMax, std::uint32_t newMax) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(hp) * newMax + oldMax - 1) / oldMax);
}
CreatureMember freshMember(std::uint32_t id, Species species, std::uint32_t sequence) {
    return {id, species, maxHp(rootForm(species), 1), 80, 70, 80, 0, 1, sequence, 0, rootForm(species)};
}
void storeActive(State& state) {
    auto& member = active(state);
    member.hp = state.hp; member.energy = state.energy; member.fullness = state.fullness;
    member.mood = state.mood; member.bond = state.bond; member.level = state.level;
}
void loadActive(State& state) {
    const auto& member = active(state);
    state.hp = member.hp; state.energy = member.energy; state.fullness = member.fullness;
    state.mood = member.mood; state.bond = member.bond; state.level = member.level;
}
std::uint32_t random(State& state) {
    std::uint32_t x = state.rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return state.rngState = x;
}
void addBond(State& state, std::uint32_t amount) { state.bond = cappedAdd(state.bond, amount, 200); }
void addXp(State& state,std::uint32_t amount,std::uint32_t rules=0) {
    auto& member=active(state);
    const auto oldMax=maxHpForRules(member.formId,state.level,rules);
    member.xp=cappedAdd(member.xp,amount,kMaxXp);
    const auto level=levelForXp(member.xp);
    if(level>state.level) {
        state.hp=scaleHp(state.hp,oldMax,maxHpForRules(member.formId,level,rules));
        state.level=level; state.message=Message::Trained;
    }
}
// Exactly one terminal wild reward call. Extras use their own level/HP scale;
// the active partner's cap never reduces their award, and care is untouched.
void addPartyXp(State& state,std::uint32_t amount,std::uint32_t skip=0) {
    for(const auto id:state.partyMemberIds)if(id && id!=skip){
        auto& member=*const_cast<CreatureMember*>(findMember(state,id));
        const auto oldMax=maxHp(member.formId,member.level);
        member.xp=cappedAdd(member.xp,amount,kMaxXp);
        const auto level=levelForXp(member.xp);
        if(level>member.level){member.hp=scaleHp(member.hp,oldMax,maxHp(member.formId,level));member.level=level;}
    }
}
void addPartyBond(State& state,std::uint32_t amount,std::uint32_t skip=0) {
    for(const auto id:state.partyMemberIds)if(id && id!=skip){
        auto& member=*const_cast<CreatureMember*>(findMember(state,id));
        member.bond=cappedAdd(member.bond,amount,200);
    }
}
std::uint32_t carePointsOf(std::uint32_t packed) { return packed & 0x7fu; }
std::uint32_t toiletOf(std::uint32_t packed) { return (packed >> 7) & 0x7fu; }
bool careMissedOf(std::uint32_t packed) { return ((packed >> 14) & 1u) != 0; }
std::uint32_t careCooldown(std::uint32_t packed, unsigned shift) { return (packed >> shift) & 7u; }
std::uint32_t packCare(std::uint32_t care, std::uint32_t toilet, bool missed, std::uint32_t feed, std::uint32_t play, std::uint32_t rest, std::uint32_t toiletCd) {
    return (care & 0x7fu) | ((toilet & 0x7fu) << 7) | (missed ? 1u << 14 : 0u) |
        ((feed & 7u) << 15) | ((play & 7u) << 18) | ((rest & 7u) << 21) | ((toiletCd & 7u) << 24);
}
bool validCareState(std::uint32_t packed) {
    return (packed >> 27) == 0 && carePointsOf(packed) <= 100 && toiletOf(packed) <= 100;
}
void grantMemberXp(State& state, CreatureMember& member, std::uint32_t amount, std::uint32_t rules = 0) {
    if (member.id == state.activeCreatureId) { addXp(state, amount, rules); return; }
    const auto oldMax = maxHp(member.formId, member.level);
    member.xp = cappedAdd(member.xp, amount, kMaxXp);
    const auto level = levelForXp(member.xp);
    if (level > member.level) {
        member.hp = scaleHp(member.hp, oldMax, maxHp(member.formId, level));
        member.level = level;
    }
}
void grantMemberBond(State& state, CreatureMember& member, std::uint32_t amount) {
    if (member.id == state.activeCreatureId) addBond(state, amount);
    else member.bond = cappedAdd(member.bond, amount, 200);
}
CreatureMember* oldestExact(State& state, std::uint32_t formId) {
    CreatureMember* best = nullptr;
    for (std::size_t i = 0; i < state.collectionCount; ++i) {
        auto& member = state.collection[i];
        if (member.formId != formId) continue;
        if (!best || member.capturedAtSequence < best->capturedAtSequence ||
            (member.capturedAtSequence == best->capturedAtSequence && member.id < best->id)) best = &member;
    }
    return best;
}
bool evolveOwned(State& next, CreatureMember& member, std::uint32_t formId) {
    const auto* target = forms::find(formId);
    const forms::EvolutionEdge* edge = nullptr;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto* candidate = forms::rules18::outgoing(member.formId, i);
        if (candidate && candidate->to == formId) edge = candidate;
    }
    if (!target || !edge) return false;
    const auto need = forms::evolutionNeed(*edge);
    const auto level = member.id == next.activeCreatureId ? next.level : member.level;
    const auto bond = member.id == next.activeCreatureId ? next.bond : member.bond;
    if (level < need.level || bond < need.bond || carePointsOf(member.careState) < need.care) return false;
    const auto oldMax = maxHp(member.formId, level);
    member.formId = formId;
    member.species = static_cast<Species>(target->lineage);
    obtain(next, formId);
    const auto scaled = scaleHp(member.id == next.activeCreatureId ? next.hp : member.hp, oldMax, maxHp(formId, level));
    if (member.id == next.activeCreatureId) next.hp = scaled;
    else member.hp = scaled;
    return true;
}
enum class CareKind : std::uint8_t { Feed, Play, Rest, Toilet };
void noteCareReward(State& state, CreatureMember& member, CareKind kind) {
    const auto shift = kind == CareKind::Feed ? 15u : kind == CareKind::Play ? 18u : kind == CareKind::Rest ? 21u : 24u;
    if (careCooldown(member.careState, shift)) return;
    grantMemberXp(state, member, 2);
    const auto care = carePointsOf(member.careState);
    const auto toilet = toiletOf(member.careState);
    const auto missed = careMissedOf(member.careState);
    auto feed = careCooldown(member.careState, 15);
    auto play = careCooldown(member.careState, 18);
    auto rest = careCooldown(member.careState, 21);
    auto toiletCd = careCooldown(member.careState, 24);
    const auto cooldown = state.careMinute ? 3u : 1u;
    if (kind == CareKind::Feed) feed = cooldown;
    else if (kind == CareKind::Play) play = cooldown;
    else if (kind == CareKind::Rest) rest = cooldown;
    else toiletCd = cooldown;
    member.careState = packCare(care < 100 ? care + 4 : 100, toilet, missed, feed, play, rest, toiletCd);
}
void home(State& state) {
    state.phase = Phase::Home;
    state.autoCapture = AutoCapture::None;
    state.captureDeferred = 0;
    state.lastCritical = 0;
    state.wildHp = state.wildMaxHp = state.captureAttempts = 0;
    state.attackBoost = state.shield = 0;
    state.cardUsed = false;
    state.wildSpecies = Species::None;
    state.wildLevel=state.wildTurn=state.wildFormId=state.wildRules=0;
}
void receiveDamage(State& state,std::uint32_t damage) {
    const auto maximum=maxHpForRules(active(state).formId,state.level,state.wildRules);
    const auto blocked=state.shield<damage ? state.shield : damage; state.shield-=blocked;
    const auto received=damage-blocked;
    if(received>=state.hp) {home(state);state.hp=(maximum+9)/10;state.message=Message::Retreated;}
    else state.hp-=received;
}
// An encounter retains the resolver and profiles selected when it was created.
combat::Hit encounterHit(const State& state,std::uint32_t attacker,std::uint32_t attackerLevel,
        std::uint32_t defender,std::uint32_t defenderLevel,combat::Move move,combat::Defense guard,bool playerAttacking=true) {
    if(state.wildRules<8) {
        const auto hit=legacy_v7::combat::resolveForms(attacker,attackerLevel,defender,defenderLevel,
            static_cast<legacy_v7::combat::Move>(move),static_cast<legacy_v7::combat::Defense>(guard));
        return {hit.damage,hit.reflected,hit.typePercent};
    }
    if(state.wildRules==8) {
        const auto hit=legacy_v8::combat::resolveForms(attacker,attackerLevel,defender,defenderLevel,
            static_cast<legacy_v8::combat::Move>(move),static_cast<legacy_v8::combat::Defense>(guard));
        return {hit.damage,hit.reflected,hit.typePercent};
    }
    if(state.wildRules>=12){const auto bonus=memberCare(*activeMember(state));
        return combat::resolveCareForms(attacker,attackerLevel,defender,defenderLevel,move,guard,playerAttacking?bonus:combat::CareBonus{},playerAttacking?combat::CareBonus{}:bonus);}
    return combat::resolveForms(attacker,attackerLevel,defender,defenderLevel,move,guard);
}
void applyCrit(State& state, combat::Hit& hit, bool record) {
    if (state.wildRules < 16 || hit.reflected) return;
    const bool critical = random(state) % 100 < 5;
    if (critical) {
        const auto scaled = hit.damage * 3 / 2;
        hit.damage = scaled ? scaled : 1;
    }
    if (record) state.lastCritical = critical ? 1u : 0u;
}
void wildResponse(State& state) {
    const auto move=state.wildTurn%2 ? combat::Move::Magic : combat::Move::Physical;
    auto hit=encounterHit(state,state.wildFormId,state.wildLevel,active(state).formId,state.level,move,combat::Defense::None,false);
    applyCrit(state, hit, false);
    ++state.wildTurn; receiveDamage(state,hit.damage);
}
void put32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint32_t get32(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (i * 8);
    return value;
}
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t length) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
} // namespace

std::uint32_t xpForLevel(std::uint32_t level) { return level>=1 && level<=kMaxLevel ? 20*(level-1)*level : kMaxXp; }
std::uint32_t levelForXp(std::uint32_t xp) {
    std::uint32_t level=1; while(level<kMaxLevel && xp>=xpForLevel(level+1)) ++level; return level;
}
State newGame(std::uint32_t seed) {
    State state;
    state.seed = seed;
    state.rngState = seed ? seed : 0x6d2b79f5u;
    state.collection[0] = freshMember(1, Species::Mote, 0);
    obtain(state,state.collection[0].formId);
    loadActive(state);
    return state;
}

State newDevice(std::uint32_t seed) {
    State state;
    state.seed = seed;
    state.rngState = seed ? seed : 0x6d2b79f5u;
    state.hp = state.energy = state.fullness = state.mood = state.level = 0;
    state.activeCreatureId = state.collectionCount = 0;
    state.nextMemberId=1;
    state.phase = Phase::Egg;
    state.message = Message::EggReady;
    state.onboardingComplete = false;
    return state;
}

static bool emptyMember(const CreatureMember& m) {
    return !m.id && m.species == Species::None && !m.hp && !m.energy && !m.fullness &&
           !m.mood && !m.bond && !m.level && !m.capturedAtSequence && !m.xp && !m.formId && !m.careState;
}

static bool validLegacyForVersion(const State& state, bool legacy) {
    if (static_cast<unsigned>(state.battleMode) > 1 || static_cast<unsigned>(state.lastAutoOutcome) > 3) return false;
    if (state.lastAutoOutcome == autobattle::Outcome::None) {
        if (state.lastAutoTurns || state.lastAutoSequence) return false;
    } else if (!state.lastAutoTurns || state.lastAutoTurns > autobattle::kMaxTraceSteps ||
               !state.lastAutoSequence || state.lastAutoSequence > state.sequence ||
               (state.lastAutoOutcome == autobattle::Outcome::Captured && !state.captures)) return false;
    if (!state.onboardingComplete) {
        if (legacy || state.phase != Phase::Egg || state.starterId || state.sequence ||
            state.rngState != (state.seed ? state.seed : 0x6d2b79f5u) ||
            state.steps || state.stepCredit || state.hp || state.energy || state.fullness ||
            state.mood || state.bond || state.level || state.captures || state.encounters ||
            state.wildHp || state.wildMaxHp || state.captureAttempts || state.cardUsed ||
            state.attackBoost || state.shield || state.legacyCaptures || state.activeCreatureId ||
            state.collectionCount || state.wildSpecies != Species::None || state.message != Message::EggReady ||
            state.battleMode != BattleMode::Tactical || state.lastAutoOutcome != autobattle::Outcome::None)
            return false;
        for (const auto& m : state.collection) if (!emptyMember(m)) return false;
        return true;
    }
    if (state.starterId > combat::kStarterCount || (state.starterId && (!state.sequence || legacy || state.legacyCaptures)) ||
        state.message == Message::EggReady ||
        (state.message == Message::Hatched && !state.starterId)) return false;
    if (state.collectionCount < 1 || state.collectionCount > kLegacyCollectionCapacity ||
        state.activeCreatureId < 1 || state.activeCreatureId > state.collectionCount ||
        state.legacyCaptures > state.captures ||
        state.captures - state.legacyCaptures != state.collectionCount - 1) return false;
    for (std::size_t i = 0; i < kCollectionCapacity; ++i) {
        const auto& member = state.collection[i];
        if (i >= state.collectionCount) {
            if (!emptyMember(member)) return false;
            continue;
        }
        if (member.id != i + 1 || !legacy_v3::combat::validProfile(static_cast<std::uint32_t>(member.species), member.level) ||
            member.bond > 200 || member.level != legacyLevelFor(member.species, member.bond) ||
            member.hp < 1 || member.hp > (legacy ? 100 : legacyMaxHp(member.species, member.level)) || member.energy > 100 || member.fullness > 100 ||
            member.mood > 100 ||
            member.capturedAtSequence > state.sequence) return false;
        if (i == 0) {
            const auto founder = state.starterId ? static_cast<Species>(combat::starterSpecies(state.starterId)) : Species::Mote;
            if (member.species != founder || member.capturedAtSequence != 0) return false;
        } else if (member.species < Species::Flicker || member.species > Species::Cinder ||
                   member.capturedAtSequence <= state.collection[i - 1].capturedAtSequence) return false;
    }
    const auto& active = (*activeMember(state));
    if (state.hp != active.hp || state.energy != active.energy || state.fullness != active.fullness ||
        state.mood != active.mood || state.bond != active.bond || state.level != active.level) return false;
    if (!state.rngState || state.steps < state.stepCredit ||
        (state.steps - state.stepCredit) % 100 != 0 ||
        state.encounters != (state.steps - state.stepCredit) / 100 ||
        state.captures > state.encounters || state.encounters > state.sequence ||
        static_cast<unsigned>(state.message) > static_cast<unsigned>(Message::Trained)) return false;
    if (state.phase == Phase::Home) {
        return state.wildHp == 0 && state.wildMaxHp == 0 && state.captureAttempts == 0 &&
               !state.cardUsed && state.attackBoost == 0 && state.shield == 0 && state.wildSpecies == Species::None;
    }
    if (state.phase != Phase::Encounter || !state.encounters ||
        state.wildSpecies < Species::Flicker || state.wildSpecies > Species::Cinder || (legacy ? (state.wildMaxHp < 24 || state.wildMaxHp > 30) :
        state.wildMaxHp != legacyMaxHp(state.wildSpecies, 1)) || state.wildHp < 1 || state.wildHp > state.wildMaxHp ||
        state.captureAttempts > 3 || (state.attackBoost != 0 && state.attackBoost != 5) ||
        state.shield > 12 || (state.attackBoost && state.shield) ||
        (!state.cardUsed && (state.attackBoost || state.shield))) return false;
    // Auto has no partially resolved durable state. It waits untouched for an
    // explicit Auto event, then commits only the terminal result.
    if (state.battleMode == BattleMode::Auto && (state.wildHp != state.wildMaxHp ||
        state.captureAttempts || state.cardUsed || state.attackBoost || state.shield)) return false;
    return true;
}

static bool validRules4State(const State& state, bool /* legacy */) {
    if (static_cast<unsigned>(state.battleMode) > 1 || static_cast<unsigned>(state.lastAutoOutcome) > 3) return false;
    if (state.lastAutoOutcome == autobattle::Outcome::None) {
        if (state.lastAutoTurns || state.lastAutoSequence) return false;
    } else if (!state.lastAutoTurns || state.lastAutoTurns > autobattle::kMaxTraceSteps ||
               !state.lastAutoSequence || state.lastAutoSequence > state.sequence ||
               (state.lastAutoOutcome == autobattle::Outcome::Captured && !state.captures)) return false;
    if (!state.onboardingComplete) {
        if (state.phase != Phase::Egg || state.starterId || state.sequence ||
            state.rngState != (state.seed ? state.seed : 0x6d2b79f5u) ||
            state.steps || state.stepCredit || state.hp || state.energy || state.fullness ||
            state.mood || state.bond || state.level || state.captures || state.encounters ||
            state.wildHp || state.wildMaxHp || state.captureAttempts || state.wildTurn || state.cardUsed ||
            state.attackBoost || state.shield || state.legacyCaptures || state.activeCreatureId ||
            state.collectionCount || state.wildSpecies != Species::None || state.wildLevel || state.wildTurn || state.message != Message::EggReady ||
            state.battleMode != BattleMode::Tactical || state.lastAutoOutcome != autobattle::Outcome::None)
            return false;
        for (const auto& m : state.collection) if (!emptyMember(m)) return false;
        return true;
    }
    if (state.starterId > combat::kStarterCount || (state.starterId && (!state.sequence || state.legacyCaptures)) ||
        state.message == Message::EggReady ||
        (state.message == Message::Hatched && !state.starterId)) return false;
    if (state.collectionCount < 1 || state.collectionCount > kLegacyCollectionCapacity ||
        state.activeCreatureId < 1 || state.activeCreatureId > state.collectionCount ||
        state.legacyCaptures > state.captures ||
        state.captures - state.legacyCaptures != state.collectionCount - 1) return false;
    for (std::size_t i = 0; i < kCollectionCapacity; ++i) {
        const auto& member = state.collection[i];
        if (i >= state.collectionCount) {
            if (!emptyMember(member)) return false;
            continue;
        }
        if (member.id != i + 1 || member.formId > 66 || !forms::validForLineage(member.formId,static_cast<std::uint32_t>(member.species)) || !combat::validFormProfile(member.formId,member.level) ||
            member.bond > 200 || member.bond < forms::find(member.formId)->minBond ||
            member.xp > kMaxXp || member.level != levelForXp(member.xp) ||
            member.hp < 1 || member.hp > maxHp(member.formId, member.level) || member.energy > 100 || member.fullness > 100 ||
            member.mood > 100 ||
            member.capturedAtSequence > state.sequence) return false;
        if (i == 0) {
            const auto founder = state.starterId ? static_cast<Species>(combat::starterSpecies(state.starterId)) : Species::Mote;
            if (member.species != founder || member.capturedAtSequence != 0) return false;
        } else if (member.species < Species::Flicker || member.species > Species::Cinder ||
                   member.capturedAtSequence <= state.collection[i - 1].capturedAtSequence) return false;
    }
    const auto& active = (*activeMember(state));
    if (state.hp != active.hp || state.energy != active.energy || state.fullness != active.fullness ||
        state.mood != active.mood || state.bond != active.bond || state.level != active.level) return false;
    if (!state.rngState || state.steps < state.stepCredit ||
        (state.steps - state.stepCredit) % 100 != 0 ||
        state.encounters != (state.steps - state.stepCredit) / 100 ||
        state.captures > state.encounters || state.encounters > state.sequence ||
        static_cast<unsigned>(state.message) > static_cast<unsigned>(Message::Trained)) return false;
    if (state.phase == Phase::Home) {
        return state.wildHp == 0 && state.wildMaxHp == 0 && state.captureAttempts == 0 &&
               !state.cardUsed && state.attackBoost == 0 && state.shield == 0 && state.wildSpecies == Species::None && !state.wildLevel && !state.wildTurn;
    }
    if (state.phase != Phase::Encounter || !state.encounters ||
        state.wildSpecies < Species::Flicker || state.wildSpecies > Species::Cinder || (state.wildLevel<1 || state.wildLevel>kMaxLevel || state.wildTurn>1000 || state.wildMaxHp != maxHp(rootForm(state.wildSpecies), state.wildLevel)) || state.wildHp < 1 || state.wildHp > state.wildMaxHp ||
        state.captureAttempts > 3 || (state.attackBoost != 0 && state.attackBoost != 5) ||
        state.shield > 12 || (state.attackBoost && state.shield) ||
        (!state.cardUsed && (state.attackBoost || state.shield))) return false;
    // Auto has no partially resolved durable state. It waits untouched for an
    // explicit Auto event, then commits only the terminal result.
    if (state.battleMode == BattleMode::Auto && (state.wildHp != state.wildMaxHp ||
        state.captureAttempts || state.wildTurn || state.cardUsed || state.attackBoost || state.shield)) return false;
    return true;
}

static bool validForVersion(const State& s,bool) {
    if(s.foregroundSequence>s.sequence)return false;
    bool partyTail=false;
    for(std::size_t i=0;i<kPartyCapacity;++i){const auto id=s.partyMemberIds[i];
        if(!id){partyTail=true;continue;}
        if(partyTail||!s.onboardingComplete||id==s.activeCreatureId||!findMember(s,id))return false;
        for(std::size_t j=0;j<i;++j)if(s.partyMemberIds[j]==id)return false;
    }
    if(static_cast<unsigned>(s.autoCapture)>1)return false;
    if(s.lastCritical>1 || s.captureDeferred>1)return false;
    if(s.captureDeferred && (s.phase!=Phase::Encounter || !s.captureAttempts))return false;
    const bool duplicateReady=s.wildRules>=16 && ownsExactForm(s,s.wildFormId);
    if(s.autoCapture==AutoCapture::Awaiting && (s.phase!=Phase::Encounter || s.battleMode!=BattleMode::Auto ||
       (s.collectionCount>=kCollectionCapacity && !duplicateReady) || s.nextMemberId==kMax || s.captures==kMax ||
       !s.wildHp || s.wildHp>s.wildMaxHp/2 || !s.wildTurn || s.captureAttempts>=3 || s.captureDeferred))return false;
    const auto& capture=s.lastCapture;
    if(static_cast<unsigned>(capture.result)>3)return false;
    if(capture.result==CaptureResult::None){if(capture.sequence||capture.targetFormId||capture.chance||capture.attempt||capture.targetLevel)return false;}
    else if(!capture.sequence||capture.sequence>s.sequence||!combat::validFormProfile(capture.targetFormId,capture.targetLevel)||capture.attempt<1||capture.attempt>3||
            (capture.result==CaptureResult::Miss?capture.chance!=0:(capture.chance<1||capture.chance>90))||
            (capture.result==CaptureResult::Captured&&!s.captures))return false;
    if(s.starterOfferSeed){std::uint32_t offers[3];drawStarterOffers(s.starterOfferSeed,offers);for(unsigned i=0;i<3;++i)if(s.starterOffers[i]!=offers[i])return false;}
    else for(const auto offer:s.starterOffers)if(offer)return false;
    if(static_cast<unsigned>(s.encounterRate)>3 || s.walkingEncounters>s.explorationSteps ||
       (s.encounterTarget==0 && (s.encounterRng || s.encounterProgress || s.explorationSteps || s.walkingEncounters)) ||
       (s.encounterTarget!=0 && (!s.encounterRng || s.encounterTarget<80 || s.encounterTarget>280 ||
                               (s.encounterTarget%2)!=0 || s.encounterProgress>=s.encounterTarget))) return false;
    const auto& pending=s.pendingEncounter;
    if(pending.formId) {
        if(!s.onboardingComplete || !combat::validFormProfile(pending.formId,pending.level) ||
           (pending.rules!=12&&pending.rules!=13&&pending.rules!=14&&pending.rules!=15&&pending.rules!=16) || (pending.rules>=13&&!forms::rules18::productionForm(pending.formId)) || !s.explorationSteps || !s.encounterTarget || s.encounterTarget<160 ||
           s.encounterProgress || s.encounters==kMax) return false;
    } else if(pending.level || pending.rules) return false;
    static_assert(forms::rules18::kFormCount<=kJournalCapacity);
    if(static_cast<unsigned>(s.battleMode)>1 || static_cast<unsigned>(s.lastAutoOutcome)>3) return false;
    if(s.lastAutoOutcome==autobattle::Outcome::None) {if(s.lastAutoTurns || s.lastAutoSequence)return false;}
    else if(!s.lastAutoTurns || s.lastAutoTurns>48 || !s.lastAutoSequence || s.lastAutoSequence>s.sequence ||
            (s.lastAutoOutcome==autobattle::Outcome::Captured && !s.captures)) return false;
    for(std::uint32_t id=forms::rules18::kFormCount+1;id<=kJournalCapacity;++id) if(hasObtained(s,id)) return false;
    if(!s.onboardingComplete) {
        if(s.phase!=Phase::Egg || s.starterId || s.sequence!=(s.starterOfferSeed?1u:0u) || s.rngState!=(s.seed?s.seed:0x6d2b79f5u) ||
           s.steps || s.stepCredit || s.hp || s.energy || s.fullness || s.mood || s.bond || s.level || s.captures || s.encounters ||
           s.wildHp || s.wildMaxHp || s.captureAttempts || s.cardUsed || s.attackBoost || s.shield || s.legacyCaptures ||
           s.activeCreatureId || s.collectionCount || s.wildSpecies!=Species::None || s.wildLevel || s.wildTurn || s.wildFormId || s.wildRules ||
           s.explorationSteps || s.walkingEncounters || s.encounterRng || s.encounterTarget || s.encounterProgress || s.worldSeed ||
           s.careMinute || s.lastCritical || s.captureDeferred ||
           capture.result!=CaptureResult::None || s.encounterRate!=EncounterRate::Normal || s.nextMemberId!=1 || s.receivedTrades || s.message!=Message::EggReady || s.battleMode!=BattleMode::Tactical || s.lastAutoOutcome!=autobattle::Outcome::None) return false;
        for(const auto& m:s.collection) if(!emptyMember(m))return false;
        for(const auto word:s.journal) if(word)return false;
        return true;
    }
    if(s.starterId>11 || (s.starterId>=9&&!s.starterOfferSeed) || (s.starterId&&(!s.sequence||s.legacyCaptures)) || s.message==Message::EggReady ||
       (s.message==Message::Hatched&&!s.starterId) || s.collectionCount<1 || s.collectionCount>kCollectionCapacity || !activeMember(s) ||
       s.legacyCaptures>s.captures || s.nextMemberId<2 ||
       s.receivedTrades>s.sequence || static_cast<std::uint64_t>(s.captures-s.legacyCaptures)+s.receivedTrades+2!=s.nextMemberId) return false;
    for(std::size_t i=0;i<kCollectionCapacity;++i) {
        const auto& m=s.collection[i];
        if(i>=s.collectionCount){if(!emptyMember(m))return false;continue;}
        const auto* f=forms::find(m.formId);
        if(!m.id || m.id>=s.nextMemberId || (i&&m.id<=s.collection[i-1].id) || !f ||
           !forms::validForLineage(m.formId,static_cast<unsigned>(m.species)) || !combat::validFormProfile(m.formId,m.level) ||
           m.bond>200 || m.bond<f->minBond || m.xp>kMaxXp || m.level!=levelForXp(m.xp) || !validCareState(m.careState) || !m.hp || m.hp>memberMaxHp(s,m) ||
           m.energy>100 || m.fullness>100 || m.mood>100 || m.capturedAtSequence>s.sequence || !hasObtained(s,m.formId)) return false;
        if(m.id==1) {
            const auto founder=s.starterId?starterForm(s,s.starterId):forms::initialForm(1u);
            if(!forms::rules18::canReach(founder,m.formId) || m.capturedAtSequence) return false;
        } else if(!m.capturedAtSequence || (i&&m.capturedAtSequence<=s.collection[i-1].capturedAtSequence)) return false;
    }
    const auto& m=*activeMember(s);
    if(s.hp!=m.hp || s.energy!=m.energy || s.fullness!=m.fullness || s.mood!=m.mood || s.bond!=m.bond || s.level!=m.level ||
       !s.rngState || s.steps<s.stepCredit || (s.steps-s.stepCredit)%100 || static_cast<std::uint64_t>(s.encounters)!=(s.steps-s.stepCredit)/100+static_cast<std::uint64_t>(s.walkingEncounters) ||
       s.captures>s.encounters || s.encounters>s.sequence || static_cast<unsigned>(s.message)>static_cast<unsigned>(Message::Toileted)) return false;
    if(s.phase==Phase::Home) return !s.wildHp&&!s.wildMaxHp&&!s.captureAttempts&&!s.captureDeferred&&!s.cardUsed&&!s.attackBoost&&!s.shield&&
        s.wildSpecies==Species::None&&!s.wildLevel&&!s.wildTurn&&!s.wildFormId&&!s.wildRules;
    if(s.phase!=Phase::Encounter || !s.encounters || !forms::validForLineage(s.wildFormId,static_cast<unsigned>(s.wildSpecies)) ||
       !combat::validFormProfile(s.wildFormId,s.wildLevel) || (s.wildRules!=4&&s.wildRules!=5&&s.wildRules!=6&&s.wildRules!=7&&s.wildRules!=8&&s.wildRules!=9&&s.wildRules!=10&&s.wildRules!=11&&s.wildRules!=12&&s.wildRules!=13&&s.wildRules!=14&&s.wildRules!=15&&s.wildRules!=16) ||
       (s.wildRules>=13&&!forms::rules18::productionForm(s.wildFormId)) ||
       (s.wildRules==4&&(s.wildSpecies<Species::Flicker||s.wildSpecies>Species::Cinder||s.wildFormId!=rootForm(s.wildSpecies))) ||
       s.wildTurn>1000 || s.wildMaxHp!=maxHpForRules(s.wildFormId,s.wildLevel,s.wildRules) || !s.wildHp || s.wildHp>s.wildMaxHp || s.captureAttempts>3 || (s.wildRules>=12&&s.wildRules<16&&s.captureAttempts==3) ||
       (s.attackBoost&&s.attackBoost!=5) || s.shield>12 || (s.attackBoost&&s.shield) || (!s.cardUsed&&(s.attackBoost||s.shield)))return false;
    if(s.battleMode==BattleMode::Auto && s.autoCapture==AutoCapture::None && s.wildRules<16 && (s.wildHp!=s.wildMaxHp||s.captureAttempts||s.wildTurn||s.cardUsed||s.attackBoost||s.shield))return false;
    return true;
}

const char* encounterRateName(EncounterRate rate) {
    switch(rate){case EncounterRate::Off:return "Off";case EncounterRate::Relaxed:return "Relaxed";
    case EncounterRate::Normal:return "Normal";case EncounterRate::Frequent:return "Frequent";}
    return "Unknown";
}
void encounterStepRange(EncounterRate rate,bool first,std::uint32_t& minimum,std::uint32_t& maximum) {
    const auto factor=static_cast<unsigned>(rate);
    if(!factor || factor>3){minimum=maximum=0;return;}
    minimum=((first?80u:160u)+factor-1)/factor;
    maximum=((first?160u:280u)+factor-1)/factor;
}
std::uint32_t encounterStepsRemaining(const State& state) {
    const auto factor=static_cast<unsigned>(state.encounterRate);
    if(!factor || factor>3 || !state.onboardingComplete || state.pendingEncounter.formId || !state.encounterTarget || state.encounterProgress>=state.encounterTarget)return 0;
    return (state.encounterTarget-state.encounterProgress+factor-1)/factor;
}
bool isValid(const State& state) { return validForVersion(state, false); }
bool needsTestEncounterResolution(const State& state) {
    return (state.phase==Phase::Encounter && state.wildFormId>=1 && state.wildFormId<forms::kFirstProductionFormId) ||
           (state.pendingEncounter.formId>=1 && state.pendingEncounter.formId<forms::kFirstProductionFormId);
}

Error apply(State& state, Action action, std::uint32_t value) {
    if (!isValid(state)) return Error::InvalidState;
    if (static_cast<unsigned>(action) > static_cast<unsigned>(Action::EvolveMember)) return Error::InvalidAction;
    if (state.sequence == kMax) return Error::CounterOverflow;
    if(needsTestEncounterResolution(state)&&action!=Action::ResolveTestEncounter)return Error::InvalidAction;
    if(action==Action::StarterOfferSeed){
        if(!value)return Error::InvalidValue;
        if(state.onboardingComplete||state.phase!=Phase::Egg)return Error::WrongPhase;
        if(state.starterOfferSeed)return Error::InvalidAction;
    } else if (action == Action::Hatch) {
        if (!starterForm(state,value)) return Error::InvalidValue;
        if (state.onboardingComplete) return Error::AlreadyHatched;
    } else if (!state.onboardingComplete) {
        return Error::WrongPhase;
    } else if (action == Action::ResolveTestEncounter) {
        if(value)return Error::InvalidValue;
        if(!needsTestEncounterResolution(state))return Error::InvalidAction;
    } else if (action == Action::Evolve) {
        if (!forms::rules18::productionForm(value)) return Error::InvalidValue;
        if (state.phase != Phase::Home) return Error::WrongPhase;
    } else if (action == Action::Mode) {
        if (value > 1) return Error::InvalidValue;
        if (state.phase != Phase::Home) return Error::WrongPhase;
    } else if (action == Action::WorldSeed) {
        if (!value) return Error::InvalidValue;
        if (state.worldSeed) return Error::InvalidAction;
    } else if (action == Action::EncounterSeed) {
        if (!value) return Error::InvalidValue;
        if (state.encounterTarget || state.encounterRng) return Error::InvalidAction;
    } else if (action == Action::EncounterRate) {
        if (value > 3) return Error::InvalidValue;
        if (state.phase != Phase::Home) return Error::WrongPhase;
    } else if (action == Action::AccrueSteps) {
        if (value < 1 || value > 1000) return Error::InvalidValue;
        if (state.encounterRate == EncounterRate::Off) return Error::InvalidAction;
        if (state.explorationSteps > kMax - value || state.encounters == kMax) return Error::CounterOverflow;
    } else if (action == Action::PresentEncounter) {
        if (value) return Error::InvalidValue;
        if (state.phase != Phase::Home) return Error::WrongPhase;
        if (!state.pendingEncounter.formId) return Error::InvalidAction;
    } else if (action == Action::Explore) {
        if (value < 1 || value > 1000) return Error::InvalidValue;
        if (state.phase != Phase::Home) return Error::WrongPhase;
        if (state.encounterRate == EncounterRate::Off || state.pendingEncounter.formId) return Error::InvalidAction;
        if (state.explorationSteps > kMax - value || state.encounters == kMax) return Error::CounterOverflow;
    } else if (action == Action::Walk) {
        if (state.pendingEncounter.formId) return Error::InvalidAction;
        if (value < 1 || value > 1000) return Error::InvalidValue;
        if (state.steps > kMax - value || state.stepCredit > kMax - value)
            return Error::CounterOverflow;
    } else if (action == Action::Card) {
        if (value != 1 && value != 2) return Error::InvalidValue;
    } else if (action == Action::Flick) {
        if (value > kFlickMaxValue) return Error::InvalidValue;
    } else if (action == Action::RingCapture) {
        if (value >= capturering::kCycleMs) return Error::InvalidValue;
    } else if (action == Action::PartyAdd || action == Action::PartyRemove) {
        if(value<1||value==kMax)return Error::InvalidValue;
        if(state.phase!=Phase::Home)return Error::WrongPhase;
    } else if (action == Action::CareMinute) {
        if (!value) return Error::InvalidValue;
    } else if (action == Action::EvolveMember) {
        const auto memberId = value >> 16;
        const auto formId = value & 0xffffu;
        if (!memberId || memberId == kMax || !forms::rules18::productionForm(formId)) return Error::InvalidValue;
        if (state.phase != Phase::Home) return Error::WrongPhase;
    } else if (action == Action::Select || action == Action::Release) {
        if (value < 1 || value == kMax) return Error::InvalidValue;
    } else if (value != 0) {
        return Error::InvalidValue;
    }
    if (action == Action::Auto) return applyAuto(state);
    if (action == Action::AutoFight) return applyAutoFight(state);
    if (action == Action::AutoResume) return applyAutoResume(state);
    if (state.phase == Phase::Encounter && state.battleMode == BattleMode::Auto && action != Action::Walk &&
        action != Action::AccrueSteps && action != Action::EncounterSeed && action != Action::WorldSeed && action != Action::ResolveTestEncounter &&
        action != Action::Retreat &&
        !(action==Action::Release && state.wildRules>=10) &&
        !((action==Action::Flick || action==Action::RingCapture) && state.autoCapture==AutoCapture::Awaiting))
        return Error::WrongMode;
    State next = state;
    // Background pacing must not clear a crit that the family has not acted on.
    if (action != Action::Attack && action != Action::Heavy && action != Action::Magic &&
        action != Action::WorldSeed && action != Action::EncounterSeed && action != Action::AccrueSteps)
        next.lastCritical = 0;
    switch (action) {
    case Action::ResolveTestEncounter:
        if(next.phase==Phase::Encounter && !forms::rules18::productionForm(next.wildFormId)){
            home(next);next.message=Message::EncounterCleared;
        }
        if(next.pendingEncounter.formId && !forms::rules18::productionForm(next.pendingEncounter.formId))next.pendingEncounter={};
        break;
    case Action::PartyAdd:
        if(!findMember(next,value))return Error::UnknownMember;
        if(value==next.activeCreatureId)return Error::ActiveMemberParty;
        if(isPartyMember(next,value))return Error::PartyMemberExists;
        if(partyCount(next)>=kPartyCapacity)return Error::PartyFull;
        next.partyMemberIds[partyCount(next)]=value;next.message=Message::PartyAdded;break;
    case Action::PartyRemove:
        if(!findMember(next,value))return Error::UnknownMember;
        if(!isPartyMember(next,value))return Error::NotPartyMember;
        for(auto& id:next.partyMemberIds)if(id==value)id=0;
        reconcileParty(next);next.message=Message::PartyRemoved;break;
    case Action::Release: {
        if(next.phase!=Phase::Home && !(next.phase==Phase::Encounter && next.wildRules>=10)) return Error::WrongPhase;
        if(value==next.activeCreatureId) return Error::ActiveMemberRelease;
        if(!findMember(next,value)) return Error::UnknownMember;
        std::size_t index=0; while(next.collection[index].id!=value) ++index;
        for(;index+1<next.collectionCount;++index) next.collection[index]=next.collection[index+1];
        next.collection[--next.collectionCount]={}; reconcileParty(next); next.message=Message::Released; break;
    }
    case Action::Evolve:
        if (!evolveOwned(next, active(next), value)) return Error::EvolutionUnavailable;
        next.message = Message::Evolved; break;
    case Action::EvolveMember: {
        const auto memberId = value >> 16;
        auto* member = const_cast<CreatureMember*>(findMember(next, memberId));
        if (!member) return Error::UnknownMember;
        if (!evolveOwned(next, *member, value & 0xffffu)) return Error::EvolutionUnavailable;
        next.message = Message::Evolved; break;
    }
    case Action::Retreat: {
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        const auto maximum = maxHpForRules(active(next).formId, next.level, next.wildRules);
        home(next);
        next.hp = (maximum + 9) / 10;
        next.message = Message::Retreated; break;
    }
    case Action::CareMinute: {
        if (value < next.careMinute || value == next.careMinute) return Error::InvalidAction;
        if (value != next.careMinute + 1 || next.phase != Phase::Home) { next.careMinute = value; break; }
        next.careMinute = value;
        auto& member = active(next);
        auto care = carePointsOf(member.careState);
        auto toilet = toiletOf(member.careState);
        auto missed = careMissedOf(member.careState);
        auto feed = careCooldown(member.careState, 15);
        auto play = careCooldown(member.careState, 18);
        auto rest = careCooldown(member.careState, 21);
        auto toiletCd = careCooldown(member.careState, 24);
        if (feed) { --feed; }
        if (play) { --play; }
        if (rest) { --rest; }
        if (toiletCd) { --toiletCd; }
        if (toilet < 100) { toilet += 8; }
        if (toilet > 100) { toilet = 100; }
        if (next.fullness) --next.fullness;
        if (toilet == 100 && !missed) {
            missed = true;
            next.mood = next.mood > 10 ? next.mood - 10 : 0;
            next.bond = next.bond > 2 ? next.bond - 2 : 0;
        }
        member.careState = packCare(care, toilet, missed, feed, play, rest, toiletCd);
        break;
    }
    case Action::Toilet: {
        if (next.phase != Phase::Home) return Error::WrongPhase;
        auto& member = active(next);
        if (toiletOf(member.careState) < 25 && !careMissedOf(member.careState)) return Error::InvalidAction;
        const auto care = carePointsOf(member.careState);
        const auto feed = careCooldown(member.careState, 15);
        const auto play = careCooldown(member.careState, 18);
        const auto rest = careCooldown(member.careState, 21);
        const auto toiletCd = careCooldown(member.careState, 24);
        member.careState = packCare(care, 0, false, feed, play, rest, toiletCd);
        next.mood = cappedAdd(next.mood, 10, 100);
        addBond(next, 3);
        next.message = Message::Toileted;
        noteCareReward(next, active(next), CareKind::Toilet);
        break;
    }
    case Action::Mode:
        next.battleMode = static_cast<BattleMode>(value);
        break;
    case Action::Auto: return Error::InvalidAction; // Handled above without nested state mutation.
    case Action::StarterOfferSeed:
        next.starterOfferSeed=value;drawStarterOffers(value,next.starterOffers);break;
    case Action::Hatch: {
        const auto chosen=starterForm(next,value);const auto* form=forms::find(chosen);
        next.collection[0] = freshMember(1, static_cast<Species>(form->lineage), 0);
        next.collection[0].formId=chosen;next.collection[0].hp=maxHp(chosen,1);
        next.collectionCount = next.activeCreatureId = 1;
        next.nextMemberId=2; obtain(next,next.collection[0].formId);
        next.starterId = value;
        next.onboardingComplete = true;
        next.phase = Phase::Home;
        next.message = Message::Hatched;
        loadActive(next);
        break; }
    case Action::Feed: {
        if (next.phase != Phase::Home) return Error::WrongPhase;
        const bool useful=next.fullness<100;
        next.fullness = cappedAdd(next.fullness, 15, 100);
        next.energy = cappedAdd(next.energy, 3, 100);
        next.mood = cappedAdd(next.mood, 2, 100);
        next.message = Message::Fed;
        if(useful) addBond(next, 2);
        if(useful) noteCareReward(next, active(next), CareKind::Feed);
        break; }
    case Action::Play: {
        if (next.phase != Phase::Home) return Error::WrongPhase;
        const bool useful=next.mood<100;
        if (useful&&next.energy < 5) return Error::LowEnergy;
        if(useful)next.energy -= 5;
        next.mood = cappedAdd(next.mood, 12, 100);
        next.message = Message::Played;
        if(useful) addBond(next, 5);
        if(useful) noteCareReward(next, active(next), CareKind::Play);
        break; }
    case Action::Rest: {
        if (next.phase != Phase::Home) return Error::WrongPhase;
        const auto maximum=maxHp(active(next).formId,next.level);
        const bool useful=next.hp<maximum || next.energy<100;
        next.hp = cappedAdd(next.hp, 25, maximum);
        next.energy = cappedAdd(next.energy, 25, 100);
        next.message = Message::Rested;
        if(useful) addBond(next, 1);
        if(useful) noteCareReward(next, active(next), CareKind::Rest);
        break; }
    case Action::WorldSeed:
        next.worldSeed=value;
        break;
    case Action::EncounterSeed:
        next.encounterRng=value;
        drawEncounterTarget(next);
        break;
    case Action::EncounterRate:
        next.encounterRate=static_cast<EncounterRate>(value);
        break;
    case Action::AccrueSteps: {
        next.explorationSteps+=value;
        // A full slot has no hidden backlog and does not advance either RNG.
        if(next.pendingEncounter.formId) break;
        if(!next.encounterTarget) drawEncounterTarget(next);
        const auto effort=value*static_cast<unsigned>(next.encounterRate);
        if(effort>=next.encounterTarget-next.encounterProgress) {
            const auto level=wildEncounterLevel(next.encounters+1,worldSelectionSeed(next),next.level);
            const auto form=selectWildForm(next.encounters+1,worldSelectionSeed(next),active(next).formId,level);
            if(!forms::find(form)) return Error::InvalidState;
            next.pendingEncounter={form,level,kRulesVersion};
            next.encounterProgress=0;
            drawEncounterTarget(next,true);
        } else next.encounterProgress+=effort;
        break;
    }
    case Action::PresentEncounter: {
        const auto pending=next.pendingEncounter;
        ++next.walkingEncounters; ++next.encounters;
        next.phase=Phase::Encounter;
        next.wildFormId=pending.formId; next.wildLevel=pending.level; next.wildRules=pending.rules;
        next.wildSpecies=static_cast<Species>(forms::find(pending.formId)->lineage);
        next.wildMaxHp=next.wildHp=maxHpForRules(pending.formId,pending.level,pending.rules);
        next.wildTurn=next.captureAttempts=next.attackBoost=next.shield=0; next.cardUsed=false;
        next.captureDeferred=0; next.lastCritical=0;
        next.pendingEncounter={}; next.message=Message::Encounter;
        break;
    }
    case Action::Explore: {
        next.explorationSteps+=value;
        next.message=Message::Walked;
        if(!next.encounterTarget) drawEncounterTarget(next);
        const auto effort=value*static_cast<unsigned>(next.encounterRate);
        if(effort>=next.encounterTarget-next.encounterProgress) {
            ++next.walkingEncounters;
            ++next.encounters;
            next.phase=Phase::Encounter;
            next.wildLevel=wildEncounterLevel(next.encounters,worldSelectionSeed(next),next.level); next.wildTurn=0; next.wildRules=kRulesVersion;
            next.wildFormId=selectWildForm(next.encounters,worldSelectionSeed(next),active(next).formId,next.wildLevel);
            const auto* foe=forms::find(next.wildFormId); if(!foe)return Error::InvalidState;
            next.wildSpecies=static_cast<Species>(foe->lineage);
            next.wildMaxHp=next.wildHp=maxHp(next.wildFormId,next.wildLevel);
            next.captureDeferred=0; next.lastCritical=0;
            next.message=Message::Encounter;
            next.encounterProgress=0; // Never retain surplus credit or create queued fights.
            drawEncounterTarget(next);
        } else next.encounterProgress+=effort;
        break;
    }
    case Action::Walk:
        next.steps += value;
        next.stepCredit += value;
        next.message = Message::Walked;
        if (next.phase == Phase::Home && next.stepCredit >= 100) {
            next.stepCredit -= 100;
            ++next.encounters;
            next.phase = Phase::Encounter;
            next.wildLevel=wildEncounterLevel(next.encounters,worldSelectionSeed(next),next.level); next.wildTurn=0; next.wildRules=kRulesVersion; // New events use the production roster/resolver.
            next.wildFormId=selectWildForm(next.encounters,worldSelectionSeed(next),active(next).formId,next.wildLevel);
            const auto* foe=forms::find(next.wildFormId); if(!foe)return Error::InvalidState;
            next.wildSpecies=static_cast<Species>(foe->lineage);
            next.wildMaxHp=next.wildHp=maxHp(next.wildFormId,next.wildLevel);
            next.captureDeferred=0; next.lastCritical=0;
            next.message = Message::Encounter;
        }
        break;
    case Action::Card:
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        if (next.cardUsed) return Error::CardAlreadyUsed;
        next.cardUsed = true;
        if (value == 1) {
            next.attackBoost = 5;
            next.message = Message::AttackCard;
        } else {
            next.shield = 12;
            next.message = Message::ShieldCard;
        }
        break;
    case Action::Attack:
    case Action::Heavy:
    case Action::Magic: {
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        const auto cost = action == Action::Heavy ? 6u : 2u;
        if (action == Action::Heavy && next.energy < cost) return Error::LowEnergy;
        const auto move = action == Action::Heavy ? combat::Move::Heavy :
                          action == Action::Magic ? combat::Move::Magic : combat::Move::Physical;
        next.captureDeferred=0; next.lastCritical=0;
        auto hit=encounterHit(next,active(next).formId,next.level,next.wildFormId,next.wildLevel,move,wildGuard(next));
        applyCrit(next,hit,true);
        const auto critical=next.lastCritical;
        next.energy=next.energy>cost?next.energy-cost:0;
        if(hit.reflected) {
            ++next.wildTurn; receiveDamage(next,hit.damage);
            if(next.phase==Phase::Encounter)next.message=Message::Attacked;
            break; // Counter replaces the normal response; Spark remains prepared.
        }
        const auto damage=hit.damage+next.attackBoost; next.attackBoost=0;
        if (damage >= next.wildHp) {
            const auto xpReward=20+6*next.wildLevel, encounterRules=next.wildRules;
            home(next);
            next.lastCritical=critical;
            next.message = Message::Won;
            addBond(next, 8); addXp(next,xpReward,encounterRules); addPartyXp(next,xpReward); addPartyBond(next,8);
        } else {
            next.wildHp -= damage;
            next.message = Message::Attacked;
            wildResponse(next);
            if(next.phase==Phase::Home) next.lastCritical=0;
            else next.lastCritical=critical;
        }
        break;
    }
    case Action::Capture:
    case Action::Flick:
    case Action::RingCapture: {
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        const bool mergeable=next.wildRules>=16 && ownsExactForm(next,next.wildFormId);
        if (next.collectionCount >= kCollectionCapacity && !mergeable) return Error::CollectionFull;
        if(next.nextMemberId==kMax || next.captures==kMax) return Error::CounterOverflow;
        if (next.captureDeferred) return Error::InvalidAction;
        if (next.wildHp > next.wildMaxHp / 2) return Error::WildTooStrong;
        if (next.captureAttempts >= 3) return Error::CaptureLimit;
        const auto chance=action==Action::RingCapture ? ringCaptureChance(next,value) : captureChance(next);
        ++next.captureAttempts;
        FlickTrajectory trajectory;
        // Legacy aim misses keep spending no RNG. Every legal RingCapture grade
        // draws once using its reduced/full eligible odds, including red timing.
        const bool aimed=action==Action::Capture || action==Action::RingCapture || (decodeFlick(value,trajectory) && trajectory.hit);
        const bool calm=action==Action::RingCapture || next.wildRules>=12 || next.autoCapture==AutoCapture::Awaiting;
        if(calm)next.lastCapture={next.sequence+1,next.wildFormId,aimed?chance:0u,
            static_cast<std::uint8_t>(next.captureAttempts),static_cast<std::uint8_t>(next.wildLevel),aimed?CaptureResult::Escaped:CaptureResult::Miss};
        if (aimed && random(next) % 100 < chance) {
            if(calm)next.lastCapture.result=CaptureResult::Captured;
            const auto xpReward=20+6*next.wildLevel, encounterRules=next.wildRules;
            auto* match=next.wildRules>=16 ? oldestExact(next,next.wildFormId) : nullptr;
            if(match) {
                const auto matchId=match->id;
                grantMemberXp(next,*match,xpReward,encounterRules);
                if(matchId!=next.activeCreatureId) addXp(next,xpReward,encounterRules);
                addBond(next,12);
                if(isPartyMember(next,matchId)) grantMemberBond(next,*oldestExact(next,next.wildFormId),12);
                addPartyXp(next,xpReward,matchId); addPartyBond(next,12,matchId);
                ++next.nextMemberId; ++next.captures;
            } else {
                auto captured=freshMember(next.nextMemberId,next.wildSpecies,next.sequence+1);
                captured.formId=next.wildFormId; captured.bond=forms::find(captured.formId)->minBond;
                captured.level=next.wildLevel; captured.xp=xpForLevel(captured.level);
                captured.hp=maxHpForRules(captured.formId,captured.level,next.wildRules);
                next.collection[next.collectionCount]=captured;
                ++next.collectionCount; ++next.nextMemberId; obtain(next,captured.formId);
                ++next.captures;
                addBond(next, 12); addXp(next,xpReward,encounterRules); addPartyXp(next,xpReward); addPartyBond(next,12);
            }
            home(next);
            next.message = Message::Captured;
        } else if(calm&&next.captureAttempts==3&&next.wildRules<16){
            home(next);next.message=Message::CaptureEnded;
        } else {
            next.message = Message::CaptureMissed;
            if(next.wildRules>=16){next.captureDeferred=1;next.autoCapture=AutoCapture::None;}
            else if(!calm)wildResponse(next);
        }
        break;
    }
    case Action::Select:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        if (!findMember(next,value)) return Error::UnknownMember;
        if (!forms::rules18::productionForm(findMember(next,value)->formId)) return Error::InvalidAction;
        next.activeCreatureId = value;
        reconcileParty(next);
        loadActive(next);
        next.message = Message::Selected;
        break;
    default:
        return Error::InvalidAction;
    }
    // A legacy fight is entirely resolved under its original HP scale, including
    // rewards/level-up/retreat rounding. Convert only the participants that enter
    // current Home care now; inactive members were converted when loaded.
    if(state.phase==Phase::Encounter && state.wildRules<9 && next.phase==Phase::Home) {
        next.hp=scaleHp(next.hp,maxHpForRules(active(next).formId,next.level,state.wildRules),maxHp(active(next).formId,next.level));
        for(std::size_t i=state.collectionCount;i<next.collectionCount;++i) {
            auto& m=next.collection[i]; m.hp=scaleHp(m.hp,maxHpForRules(m.formId,m.level,state.wildRules),maxHp(m.formId,m.level));
        }
    }
    ++next.sequence;
    // Queue-only repair must preserve the current real battle policy/result.
    const bool pendingOnlyRepair=action==Action::ResolveTestEncounter && state.phase==next.phase;
    if(action!=Action::AccrueSteps&&action!=Action::EncounterSeed&&action!=Action::WorldSeed&&!pendingOnlyRepair)
        next.foregroundSequence=next.sequence;
    if(next.onboardingComplete)storeActive(next);
    if (!isValid(next)) return Error::InvalidState;
    state = next;
    return Error::None;
}

std::uint32_t recoveryRestCount(const State& state) {
    if(!isValid(state) || state.phase!=Phase::Home)return 0;
    const auto maximum=maxHp(activeMember(state)->formId,state.level);
    if(state.hp==maximum && state.energy==100)return 0;
    State candidate=state;
    for(std::uint32_t count=1;count<=40;++count) {
        if(apply(candidate,Action::Rest)!=Error::None)return 0;
        if(candidate.hp==maximum && candidate.energy==100)return count;
    }
    return 0;
}

namespace {
enum class AutoFlow { Legacy, PauseForFlick, ResumeWithoutCapture };
Error autoEngine(State& state, autobattle::Trace* trace, AutoFlow flow) {
    if (trace) { trace->count = 0; trace->outcome = autobattle::Outcome::None; }
    if (!isValid(state)) return Error::InvalidState;
    if (state.phase != Phase::Encounter) return Error::WrongPhase;
    if (state.battleMode != BattleMode::Auto) return Error::WrongMode;
    if((flow==AutoFlow::ResumeWithoutCapture)!=(state.autoCapture==AutoCapture::Awaiting))return Error::InvalidAction;
    if (state.sequence == kMax) return Error::CounterOverflow;
    if(needsTestEncounterResolution(state))return Error::InvalidAction;
    const auto startSequence = state.sequence;
    auto policy = state.seed ^ 0x9e3779b9u ^ (state.encounters * 0x85ebca6bu) ^ state.foregroundSequence;
    State next = state;
    bool captureRecorded=false;
    // Reuse the exact Tactical transitions on a private candidate. Internal
    // turns share the outer event's sequence, including capture timestamps.
    next.battleMode = BattleMode::Tactical;
    next.autoCapture=AutoCapture::None;
    if(flow!=AutoFlow::Legacy){next.lastAutoOutcome=autobattle::Outcome::None;next.lastAutoSequence=next.lastAutoTurns=0;}
    if (trace) {
        trace->kind = autobattle::Kind::Wild;
        trace->startSequence = startSequence; trace->endSequence = startSequence + 1;
        trace->playerSpecies = static_cast<std::uint32_t>((*activeMember(state)).species);
        trace->playerLevel = state.level; trace->combatRulesVersion=state.wildRules<8?7:state.wildRules==8?8:state.wildRules==9?9:state.wildRules>=12?12:4;
        trace->playerFormId=(*activeMember(state)).formId;
        trace->enemyFormId=state.wildFormId; trace->includeFormIds=state.wildRules>=5;
        trace->enemySpecies = static_cast<std::uint32_t>(state.wildSpecies); trace->enemyLevel = state.wildLevel;
        const auto care=state.wildRules>=12 ? memberCare(*activeMember(state)) : combat::CareBonus{};
        trace->playerOffenseBonus=care.offense;trace->playerProtectionBonus=care.protection;
        trace->enemyOffenseBonus=trace->enemyProtectionBonus=0;
    }
    for (std::uint32_t turn = 0; turn < autobattle::kMaxTraceSteps; ++turn) {
        Action chosen;
        // The historical one-event Auto policy in an old encounter keeps its
        // original eight-slot decisions. Current AutoFight/manual throws use
        // all60 slots immediately, including when continuing an old encounter.
        const auto autoCapacity=state.wildRules<=13?kLegacyCollectionCapacity:kCollectionCapacity;
        if (flow==AutoFlow::Legacy && !next.captureDeferred && next.collectionCount < autoCapacity && next.wildHp <= next.wildMaxHp / 2 && next.captureAttempts < 3)
            chosen = Action::Capture;
        else {
            constexpr Action moves[]{Action::Attack, Action::Magic, Action::Heavy};
            if(next.wildRules>=11) {
                // One equal-odds draw for each attack, independent of capture RNG.
                // The saved encounter and outer sequence make the whole trace replayable.
                chosen=(autobattle::nextRandom(policy)&1u)?Action::Magic:Action::Attack;
            } else if(next.wildRules==4) chosen=moves[autobattle::nextRandom(policy)%(next.energy>=6?3u:2u)];
            else {
                // Public profiles and visible guard only. Prefer a basic move on
                // an energy tie; seeded ties never inspect private capture RNG.
                chosen=Action::Attack; std::uint32_t best=0,ties=0;
                for(unsigned i=0;i<(next.energy>=6?3u:2u);++i) {
                    const auto move=i==0?combat::Move::Physical:i==1?combat::Move::Magic:combat::Move::Heavy;
                    const auto hit=encounterHit(next,active(next).formId,next.level,next.wildFormId,next.wildLevel,move,wildGuard(next));
                    if(hit.reflected) continue;
                    if(hit.damage>best){best=hit.damage;chosen=moves[i];ties=1;}
                    else if(hit.damage==best && i<2 && autobattle::nextRandom(policy)%++ties==0) chosen=moves[i];
                }
            }
        }
        autobattle::Step frame;
        frame.action = chosen == Action::Capture ? autobattle::Move::Capture : chosen == Action::Heavy ?
            autobattle::Move::Heavy : chosen == Action::Magic ? autobattle::Move::Magic : autobattle::Move::Physical;
        frame.playerHpBefore = next.hp; frame.enemyHpBefore = next.wildHp;
        auto enemyRemaining = next.wildHp;
        const auto guard=wildGuard(next);
        frame.guard=guard==combat::Defense::Brace?autobattle::Move::Brace:guard==combat::Defense::Ward?autobattle::Move::Ward:guard==combat::Defense::Counter?autobattle::Move::Counter:autobattle::Move::None;
        if (chosen != Action::Capture) {
            const auto move = chosen == Action::Heavy ? combat::Move::Heavy : chosen == Action::Magic ? combat::Move::Magic : combat::Move::Physical;
            const auto hit=encounterHit(next,active(next).formId,next.level,next.wildFormId,next.wildLevel,move,guard);
            frame.reflected=hit.reflected;
            const auto damage=hit.reflected?0:hit.damage+next.attackBoost;
            enemyRemaining = damage >= enemyRemaining ? 0 : enemyRemaining - damage;
        }
        const auto opponentMove=next.wildTurn%2 ? autobattle::Move::Magic : autobattle::Move::Physical;
        const auto members = next.collectionCount;
        const auto result = apply(next, chosen);
        if (result != Error::None) { if (trace) trace->count = 0; return result; }
        frame.captured = next.collectionCount > members ||
            (chosen == Action::Capture && next.phase == Phase::Home && next.message == Message::Captured);
        if (next.phase == Phase::Home && !frame.captured &&
            (next.message == Message::Won || next.message == Message::Trained)) enemyRemaining = 0;
        const bool calmCapture=state.wildRules>=12 && chosen==Action::Capture;
        if(calmCapture) {
            captureRecorded=true;frame.captureChance=next.lastCapture.chance;
            frame.captureAttempt=next.lastCapture.attempt;frame.captureResult=static_cast<std::uint8_t>(next.lastCapture.result);
        }
        // Trace combat HP before post-battle care changes. A gentle retreat
        // restores some saved HP, and a reward may train/grow max HP; neither is
        // an extra combat heal. The terminal State contains those durable effects.
        frame.playerHpAfter = next.phase == Phase::Encounter ? next.hp :
            calmCapture || frame.captured || !enemyRemaining ? frame.playerHpBefore : 0;
        frame.enemyHpAfter = next.phase == Phase::Encounter ? next.wildHp : enemyRemaining;
        frame.opponentAction = calmCapture || frame.captured || !enemyRemaining ? autobattle::Move::None : frame.reflected ? autobattle::Move::Counter : opponentMove;
        if (trace) { trace->steps[turn] = frame; trace->count = turn + 1; }
        if (next.phase == Phase::Home) {
            if(captureRecorded)next.lastCapture.sequence=startSequence+1;
            next.battleMode = BattleMode::Auto;
            next.lastAutoTurns = turn + 1; next.lastAutoSequence = startSequence + 1;
            next.lastAutoOutcome = frame.captured ? autobattle::Outcome::Captured :
                !enemyRemaining ? autobattle::Outcome::Won : autobattle::Outcome::Retreated;
            if (!isValid(next)) { if (trace) trace->count = 0; return Error::InvalidState; }
            if (trace) trace->outcome = next.lastAutoOutcome;
            state = next;
            return Error::None;
        }
        if(flow==AutoFlow::PauseForFlick && captureChance(next)) {
            // The crossing attack AND its response have finished. No throw or
            // capture RNG draw is included in this durable attack-only chunk.
            next.battleMode=BattleMode::Auto;next.autoCapture=AutoCapture::Awaiting;
            if(!isValid(next)){if(trace)trace->count=0;return Error::InvalidState;}
            state=next;return Error::None;
        }
        next.sequence = startSequence;
        next.foregroundSequence = startSequence;
        // The private candidate stays valid between internal turns. Publish the
        // single outer timestamp only once the whole Auto transition commits.
        if(captureRecorded)next.lastCapture.sequence=startSequence;
    }
    // A bounded unresolved fight is a terminal gentle retreat, never a retry loop.
    const auto oldMaximum=maxHpForRules(active(next).formId,next.level,next.wildRules);
    home(next); next.message=Message::Retreated;
    next.hp=scaleHp((oldMaximum+9)/10,oldMaximum,maxHp(active(next).formId,next.level));
    next.sequence=startSequence+1; next.foregroundSequence=next.sequence; next.battleMode=BattleMode::Auto;
    if(captureRecorded)next.lastCapture.sequence=next.sequence;
    next.lastAutoTurns=autobattle::kMaxTraceSteps; next.lastAutoSequence=next.sequence;
    next.lastAutoOutcome=autobattle::Outcome::Retreated; storeActive(next);
    if(!isValid(next)) return Error::InvalidState;
    if(trace) trace->outcome=next.lastAutoOutcome;
    state=next; return Error::None;
}
} // namespace
Error applyAuto(State& state,autobattle::Trace* trace){return autoEngine(state,trace,AutoFlow::Legacy);}
Error applyAutoFight(State& state,autobattle::Trace* trace){return autoEngine(state,trace,AutoFlow::PauseForFlick);}
Error applyAutoResume(State& state,autobattle::Trace* trace){return autoEngine(state,trace,AutoFlow::ResumeWithoutCapture);}

const char* errorText(Error error) {
    switch (error) {
    case Error::None: return "ok";
    case Error::InvalidState: return "invalid state";
    case Error::InvalidAction: return "unknown action";
    case Error::InvalidValue: return "invalid action value";
    case Error::WrongPhase: return "action unavailable in current phase";
    case Error::LowEnergy: return "rest to recover energy for this action";
    case Error::CardAlreadyUsed: return "only one card per encounter";
    case Error::WildTooStrong: return "weaken the wild creature to half health before capture";
    case Error::CaptureLimit: return "three capture attempts already used this encounter";
    case Error::CounterOverflow: return "state counter limit reached";
    case Error::CollectionFull: return "collection is full (60 Digimon); no Digimon was replaced";
    case Error::UnknownMember: return "that creature is not in your collection";
    case Error::AlreadyHatched: return "starter already chosen; no Digimon was replaced";
    case Error::WrongMode: return "action unavailable in this battle mode";
    case Error::ActiveMemberRelease: return "select another active partner before releasing this Digimon";
    case Error::EvolutionUnavailable: return "evolution requires a legal next form and its level/bond thresholds";
    case Error::PartyFull: return "three XP companions are already selected";
    case Error::PartyMemberExists: return "that Digimon is already an XP companion";
    case Error::NotPartyMember: return "that Digimon is not an XP companion";
    case Error::ActiveMemberParty: return "your active partner already earns battle XP";
    case Error::AutoLimit: return "auto battle reached its bounded turn limit; state unchanged";
    }
    return "unknown error";
}
const char* messageText(Message message) {
    switch (message) {
    case Message::PartyAdded: return "XP companion selected.";
    case Message::PartyRemoved: return "XP companion removed.";
    case Message::Welcome: return "Your adventure begins.";
    case Message::Fed: return "A happy snack.";
    case Message::Played: return "Time together builds your bond.";
    case Message::Rested: return "Rested and ready.";
    case Message::Walked: return "Every step counts.";
    case Message::Encounter: return "A wild creature appeared!";
    case Message::AttackCard: return "Spark card read. Your next attack is stronger.";
    case Message::ShieldCard: return "Shelter card read. A gentle shield surrounds you.";
    case Message::Attacked: return "Your creature used a skill.";
    case Message::Won: return "A friendly battle won.";
    case Message::Captured: return "A new Digimon joined your collection!";
    case Message::CaptureMissed: return "The wild creature slipped away from the capture beam.";
    case Message::Retreated: return "A gentle retreat. Rest whenever you are ready.";
    case Message::Evolved: return "Your bond helped your creature evolve!";
    case Message::Selected: return "Your companion is ready.";
    case Message::EggReady: return "Choose an egg to meet your Rookie partner.";
    case Message::Hatched: return "Your Rookie partner has hatched!";
    case Message::CaptureEnded: return "The wild creature wandered on. Three throws used.";
    case Message::EncounterCleared: return "Ready to explore.";
    case Message::Released: return "Your Digimon is free to roam; your journal remembers them.";
    case Message::Trained: return "Your companion gained a level from battle experience!";
    case Message::Toileted: return "Toilet need cleared. Your Digimon feels better.";
    }
    return "Unknown message.";
}
const char* creatureName(const State& state) {
    if (!activeMember(state)) return "Unknown";
    return memberName((*activeMember(state)));
}
const char* speciesId(Species species) {
    return combat::speciesName(static_cast<std::uint32_t>(species));
}
const char* memberName(const CreatureMember& member) {
    if (!combat::validFormProfile(member.formId, member.level)) return "Unknown";
    return combat::formProfile(member.formId,member.level).name;
}
const char* wildName(const State& state) {
    CreatureMember wild;
    wild.species = state.wildSpecies;
    wild.level = state.wildLevel;
    wild.formId=state.wildFormId;
    return memberName(wild);
}
bool parseAction(const char* name, Action& action) {
    if (!name) return false;
    const char* names[] = {"feed", "play", "rest", "walk", "card", "attack", "capture", "select", "heavy", "magic", "hatch", "mode", "auto", "evolve", "release", "flick", "explore", "encounter-rate", "encounter-seed", "starter-offer-seed", "accrue-steps", "present-encounter", "resolve-test-encounter", "auto-fight", "auto-resume", "world-seed", "ring-capture", "party-add", "party-remove", "toilet", "retreat", "care-minute", "evolve-member"};
    if (std::strcmp(name, "physical") == 0) { action = Action::Attack; return true; }
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (std::strcmp(name, names[i]) == 0) {
            action = static_cast<Action>(i);
            return true;
        }
    }
    return false;
}
std::size_t writeJson(const State& s,char* output,std::size_t capacity) {
    if(!output || !capacity) return 0;
    output[0]='\0'; if(!isValid(s)) return 0;
    std::size_t used=0; bool ok=true;
    const auto append=[&](const char* format,...) {
        if(!ok) return;
        va_list args; va_start(args,format);
        const auto n=std::vsnprintf(output+used,capacity-used,format,args); va_end(args);
        if(n<0 || static_cast<std::size_t>(n)>=capacity-used){ok=false;return;}
        used+=static_cast<std::size_t>(n);
    };
    const auto number=[&](std::uint32_t value){return static_cast<unsigned>(value);};
    const auto metadata=[&](std::uint32_t formId) {
        const auto* f=forms::find(formId);
        append("\"stage\":");
        if(!f || f->stage==forms::Stage::Original) append("null"); else append("\"%s\"",forms::stageName(f->stage));
        append(",\"artId\":"); if(f && f->artId) append("\"%s\"",f->artId); else append("null");
    };
    const auto profile=[&](std::uint32_t formId,std::uint32_t level,bool activeProfile=false) {
        char json[combat::kProfileJsonCapacity];
        const auto n=activeProfile&&s.phase==Phase::Encounter&&s.wildRules<8 ? legacy_v7::combat::writeFormProfileJson(formId,level,json,sizeof(json)) :
            activeProfile&&s.phase==Phase::Encounter&&s.wildRules==8 ? legacy_v8::combat::writeFormProfileJson(formId,level,json,sizeof(json)) : combat::writeFormProfileJson(formId,level,json,sizeof(json));
        if(!n){ok=false;return;}
        append("\"combat\":%s",json);
    };
    const auto care=[&](const CreatureMember& m) {
        const auto bonus=s.phase==Phase::Encounter && m.id==s.activeCreatureId && s.wildRules<12 ? combat::CareBonus{} : memberCare(m);
        const auto stats=memberBattleProfile(s,m).stats;
        append(",\"care\":{\"bondRank\":%u,\"offenseBonus\":%u,\"protectionBonus\":%u,\"effective\":{\"maxHp\":%u,\"attack\":%u,\"defense\":%u,\"magic\":%u,\"resistance\":%u}}",
               number(m.bond/50),number(bonus.offense),number(bonus.protection),number(stats.maxHp),number(stats.attack),number(stats.defense),number(stats.magic),number(stats.resistance));
    };
    const auto progress=[&](const CreatureMember& m) {
        append("\"formId\":%u,\"xp\":%u,\"xpToNext\":%u,",number(m.formId),number(m.xp),number(m.level<kMaxLevel ? xpForLevel(m.level+1)-m.xp : 0));
    };
    char message[128];
    if(s.message==Message::Encounter) std::snprintf(message,sizeof(message),"A wild %s appeared!",wildName(s));
    else if(s.message==Message::Captured) {
        const CreatureMember* joined=nullptr;
        for(std::size_t i=0;i<s.collectionCount;++i)
            if(s.collection[i].capturedAtSequence==s.sequence) joined=&s.collection[i];
        const auto* form=forms::find(joined ? joined->formId : s.lastCapture.targetFormId);
        std::snprintf(message,sizeof(message),joined?"%s joined your collection!":"Bonus XP merged into your oldest %s.",form?form->name:"Digimon");
    }
    else if(s.message==Message::CaptureMissed) std::snprintf(message,sizeof(message),"%s slipped away from the capture beam.",wildName(s));
    else std::snprintf(message,sizeof(message),"%s",messageText(s.message));
    append("{\"schemaVersion\":%u,\"rulesVersion\":%u,\"sequence\":%u,\"seed\":%u,\"rngState\":%u,\"steps\":%u,\"stepCredit\":%u,"
           "\"hp\":%u,\"energy\":%u,\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"maxLevel\":%u,\"captures\":%u,\"encounters\":%u,"
           "\"phase\":\"%s\",\"wildHp\":%u,\"wildMaxHp\":%u,\"wildLevel\":%u,\"wildTurn\":%u,\"captureAttempts\":%u,\"cardUsed\":%s,\"attackBoost\":%u,\"shield\":%u,",
           number(kSchemaVersion),number(kRulesVersion),number(s.sequence),number(s.seed),number(s.rngState),number(s.steps),number(s.stepCredit),
           number(s.hp),number(s.energy),number(s.fullness),number(s.mood),number(s.bond),number(s.level),number(kMaxLevel),number(s.captures),number(s.encounters),
           s.phase==Phase::Egg ? "egg" : s.phase==Phase::Home ? "home" : "encounter",number(s.wildHp),number(s.wildMaxHp),number(s.wildLevel),number(s.wildTurn),
           number(s.captureAttempts),s.cardUsed ? "true" : "false",number(s.attackBoost),number(s.shield));
    if(s.onboardingComplete) {
        const auto& m=(*activeMember(s));
        append("\"creature\":\"%s\",\"species\":\"%s\",",memberName(m),speciesId(m.species));
        progress(m); metadata(m.formId); append(","); profile(m.formId,m.level,m.id==s.activeCreatureId); care(m);
        append(",\"carePoints\":%u,\"toilet\":%u,\"careMissed\":%s",number(carePointsOf(m.careState)),number(toiletOf(m.careState)),careMissedOf(m.careState)?"true":"false");
    } else append("\"creature\":null,\"species\":null,\"formId\":0,\"xp\":0,\"xpToNext\":0,\"stage\":null,\"artId\":null,\"combat\":null,\"care\":null,\"carePoints\":0,\"toilet\":0,\"careMissed\":false");
    append(",\"message\":\"%s\",\"activeCreatureId\":%u,\"collectionCapacity\":%u,\"legacyCaptures\":%u,\"onboarding\":{\"completed\":%s,\"starterId\":",
           message,number(s.activeCreatureId),number(kCollectionCapacity),number(s.legacyCaptures),s.onboardingComplete ? "true" : "false");
    if(s.starterId) append("%u",number(s.starterId)); else append("null");
    append(",\"offerSeed\":%u,\"offers\":[%u,%u,%u]}",number(s.starterOfferSeed),number(s.starterOffers[0]),number(s.starterOffers[1]),number(s.starterOffers[2]));
    append(",\"lastCapture\":{\"sequence\":%u,\"targetFormId\":%u,\"targetLevel\":%u,\"chance\":%u,\"attempt\":%u,\"result\":\"%s\"}",
           number(s.lastCapture.sequence),number(s.lastCapture.targetFormId),number(s.lastCapture.targetLevel),number(s.lastCapture.chance),number(s.lastCapture.attempt),captureResultName(s.lastCapture.result));
    append(",\"battleMode\":\"%s\",\"lastAutoBattle\":",s.battleMode==BattleMode::Auto ? "auto" : "tactical");
    if(s.lastAutoOutcome==autobattle::Outcome::None) append("null");
    else append("{\"sequence\":%u,\"turns\":%u,\"outcome\":\"%s\"}",number(s.lastAutoSequence),number(s.lastAutoTurns),autobattle::outcomeName(s.lastAutoOutcome));
    if(s.phase==Phase::Encounter) {
        char json[combat::kProfileJsonCapacity];
        const auto n=s.wildRules<8 ? legacy_v7::combat::writeFormProfileJson(s.wildFormId,s.wildLevel,json,sizeof(json)) :
            s.wildRules==8 ? legacy_v8::combat::writeFormProfileJson(s.wildFormId,s.wildLevel,json,sizeof(json)) : combat::writeFormProfileJson(s.wildFormId,s.wildLevel,json,sizeof(json));
        if(!n) ok=false;
        append(",\"wildSpecies\":\"%s\",\"wildName\":\"%s\",\"wildCombat\":%s",speciesId(s.wildSpecies),wildName(s),json);
    } else append(",\"wildSpecies\":null,\"wildName\":null,\"wildCombat\":null");
    append(",\"wildFormId\":%u,\"wildRules\":%u,\"wildCaptureChance\":%u,\"wildGuard\":",number(s.wildFormId),number(s.wildRules),number(captureChance(s)));
    const auto rarity=s.phase==Phase::Encounter && s.wildRules>=10 ? encounters::rarityName(encounters::rarityForForm(s.wildFormId)) : nullptr;
    // Complete the preceding wildGuard value before the additive Park metadata.
    const auto guard=wildGuard(s);
    if(guard==combat::Defense::None) append("null"); else append("\"%s\"",guard==combat::Defense::Brace?"brace":guard==combat::Defense::Ward?"ward":"counter");
    append(",\"wildRarity\":");if(rarity)append("\"%s\"",rarity);else append("null");
    append(",\"recoveryRestCount\":%u,\"queuedEncounters\":%u,\"stepsToNextEncounter\":%u",number(recoveryRestCount(s)),number(s.stepCredit/100),number(s.stepCredit>=100?0:100-s.stepCredit));
    append(",\"walking\":{\"rate\":%u,\"name\":\"%s\",\"eligibleSteps\":%u,\"encounters\":%u,\"rngState\":%u,\"target\":%u,\"progress\":%u,\"remainingSteps\":%u,\"pendingEncounter\":",
           number(static_cast<unsigned>(s.encounterRate)),encounterRateName(s.encounterRate),number(s.explorationSteps),number(s.walkingEncounters),
           number(s.encounterRng),number(s.encounterTarget),number(s.encounterProgress),number(encounterStepsRemaining(s)));
    if(s.pendingEncounter.formId)append("{\"formId\":%u,\"level\":%u,\"rules\":%u}",number(s.pendingEncounter.formId),number(s.pendingEncounter.level),number(s.pendingEncounter.rules));
    else append("null");
    append("}");
    append(",\"partyCapacity\":%u,\"partyMemberIds\":[",number(kPartyCapacity));
    for(std::size_t i=0;i<partyCount(s);++i)append("%s%u",i?",":"",number(s.partyMemberIds[i]));
    append("]");
    append(",\"careMinute\":%u,\"critical\":%s,\"captureDeferred\":%u",number(s.careMinute),s.lastCritical?"true":"false",number(s.captureDeferred));
    append(",\"foregroundSequence\":%u",number(s.foregroundSequence));
    append(",\"autoCapture\":%u,\"worldSeed\":%u,\"receivedTrades\":%u,\"nextMemberId\":%u,\"journal\":{\"capacity\":512,\"obtainedFormIds\":[",number(static_cast<unsigned>(s.autoCapture)),number(s.worldSeed),number(s.receivedTrades),number(s.nextMemberId));
    bool obtainedComma=false;
    for(std::uint32_t id=1;id<=forms::rules18::kFormCount;++id) if(hasObtained(s,id)){append("%s%u",obtainedComma?",":"",number(id));obtainedComma=true;}
    append("]},\"collection\":[");
    for(std::size_t i=0;i<s.collectionCount;++i) {
        const auto& m=s.collection[i];
        append("%s{\"id\":%u,\"species\":\"%s\",\"name\":\"%s\",\"hp\":%u,\"energy\":%u,\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"capturedAtSequence\":%u,",
               i ? "," : "",number(m.id),speciesId(m.species),memberName(m),number(m.hp),number(m.energy),number(m.fullness),number(m.mood),number(m.bond),number(m.level),number(m.capturedAtSequence));
        progress(m); metadata(m.formId); append(","); profile(m.formId,m.level,m.id==s.activeCreatureId); care(m);
        append(",\"carePoints\":%u,\"toilet\":%u,\"careMissed\":%s}",number(carePointsOf(m.careState)),number(toiletOf(m.careState)),careMissedOf(m.careState)?"true":"false");
    }
    append("],\"evolution\":{\"options\":[");
    if(s.onboardingComplete) {
        const auto& m=(*activeMember(s)); bool comma=false;
        for(std::size_t i=0;i<2;++i) if(const auto* edge=forms::rules18::outgoing(m.formId,i)) {
            const auto id=edge->to;
            if(!forms::rules18::productionForm(id))continue;
            const auto* nextForm=forms::find(id); const auto need=forms::evolutionNeed(*edge);
            const auto preview=m.level<need.level ? need.level : m.level;
            const auto eligible=s.phase==Phase::Home && m.level>=need.level && m.bond>=need.bond && carePointsOf(m.careState)>=need.care;
            append("%s{\"formId\":%u,\"name\":\"%s\",",comma ? "," : "",number(id),nextForm->name); metadata(id);
            append(",\"requiredLevel\":%u,\"requiredBond\":%u,\"requiredCare\":%u,\"previewLevel\":%u,\"eligible\":%s,",number(need.level),number(need.bond),number(need.care),number(preview),eligible ? "true" : "false");
            profile(id,preview); append("}"); comma=true;
        }
    }
    append("]}}"); if(!ok){output[0]='\0';return 0;} return used;
}

bool encodeSnapshot(const State& s, Snapshot& snapshot) {
    static_assert(kSnapshotSize == 8 + (22 + 4 + kCollectionCapacity * 12 + 11 + kJournalWords + 6 + 9 + 4 + 3 + kPartyCapacity + 3) * 4 + 4);
    if (!isValid(s)) return false;
    // Validation is the only failure point. Write directly afterwards so deep
    // durable trade/save calls do not stack another full collection snapshot.
    auto* bytes = snapshot.bytes;
    std::memset(bytes,0,kSnapshotSize);
    std::memcpy(bytes, "DGVS", 4);
    bytes[4] = static_cast<std::uint8_t>(kSchemaVersion);
    constexpr auto payload = kSnapshotSize - 12;
    bytes[6] = static_cast<std::uint8_t>(payload);
    bytes[7] = static_cast<std::uint8_t>(payload >> 8);
    const std::uint32_t fields[] = {
        kRulesVersion, s.sequence, s.seed, s.rngState, s.steps, s.stepCredit,
        s.hp, s.energy, s.fullness, s.mood, s.bond, s.level, s.captures, s.encounters,
        s.wildHp, s.wildMaxHp, s.captureAttempts, static_cast<std::uint32_t>(s.phase),
        s.cardUsed ? 1u : 0u, s.attackBoost, s.shield, static_cast<std::uint32_t>(s.message),
        s.legacyCaptures, s.activeCreatureId, s.collectionCount, static_cast<std::uint32_t>(s.wildSpecies)
    };
    std::size_t offset = 8;
    for (const auto value : fields) { put32(bytes + offset, value); offset += 4; }
    for (const auto& member : s.collection) {
        const std::uint32_t values[] = {member.id, static_cast<std::uint32_t>(member.species),
            member.hp, member.energy, member.fullness, member.mood, member.bond, member.level, member.capturedAtSequence, member.xp, member.formId, member.careState};
        for (const auto value : values) { put32(bytes + offset, value); offset += 4; }
    }
    put32(bytes + offset, s.onboardingComplete ? 1u : 0u);
    put32(bytes + offset + 4, s.starterId);
    put32(bytes + offset + 8, static_cast<std::uint32_t>(s.battleMode));
    put32(bytes + offset + 12, static_cast<std::uint32_t>(s.lastAutoOutcome));
    put32(bytes + offset + 16, s.lastAutoTurns);
    put32(bytes + offset + 20, s.lastAutoSequence);
    put32(bytes + offset + 24,s.wildLevel); put32(bytes + offset + 28,s.wildTurn);
    put32(bytes+offset+32,s.wildFormId); put32(bytes+offset+36,s.wildRules); put32(bytes+offset+40,s.nextMemberId);
    for(std::size_t i=0;i<kJournalWords;++i) put32(bytes+offset+44+i*4,s.journal[i]);
    offset+=44+kJournalWords*4;
    const std::uint32_t walking[]{s.explorationSteps,s.walkingEncounters,s.encounterRng,s.encounterTarget,s.encounterProgress,static_cast<std::uint32_t>(s.encounterRate)};
    for(const auto value:walking){put32(bytes+offset,value);offset+=4;}
    const std::uint32_t added[]{s.lastCapture.sequence,s.lastCapture.targetFormId,s.lastCapture.chance,
        static_cast<std::uint32_t>(s.lastCapture.attempt)|(static_cast<std::uint32_t>(s.lastCapture.targetLevel)<<8),static_cast<std::uint32_t>(s.lastCapture.result),
        s.starterOfferSeed,s.starterOffers[0],s.starterOffers[1],s.starterOffers[2]};
    for(const auto value:added){put32(bytes+offset,value);offset+=4;}
    put32(bytes+offset,s.pendingEncounter.formId); put32(bytes+offset+4,s.pendingEncounter.level); put32(bytes+offset+8,s.pendingEncounter.rules); put32(bytes+offset+12,s.foregroundSequence);
    put32(bytes+offset+16,s.receivedTrades);
    put32(bytes+offset+20,static_cast<std::uint32_t>(s.autoCapture));
    put32(bytes+offset+24,s.worldSeed);
    for(std::size_t i=0;i<kPartyCapacity;++i)put32(bytes+offset+28+4*i,s.partyMemberIds[i]);
    offset+=28+kPartyCapacity*4;
    put32(bytes+offset,s.careMinute); put32(bytes+offset+4,s.lastCritical); put32(bytes+offset+8,s.captureDeferred);
    offset += 12;
    if (offset + 4 != kSnapshotSize) return false;
    put32(bytes + kSnapshotSize - 4, crc32(bytes, kSnapshotSize - 4));
    return true;
}

SnapshotStatus decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state) {
    if (!bytes || length < 8) return SnapshotStatus::InvalidLength;
    if (std::memcmp(bytes, "DGVS", 4) != 0) return SnapshotStatus::BadMagic;
    const auto version = static_cast<unsigned>(bytes[4]) | (static_cast<unsigned>(bytes[5]) << 8);
    if (version < 1 || version > kSchemaVersion) return SnapshotStatus::UnsupportedVersion;
    const auto required = version == 1 ? kLegacySnapshotSize : version == 2 ? kV2SnapshotSize :
                          version < 5 ? kPreviousSnapshotSize : version == 5 ? kV5SnapshotSize : version == 6 ? kV6SnapshotSize : version==7 ? kV7SnapshotSize : version<=13 ? kV13SnapshotSize : version==14 ? kV14SnapshotSize : version==15 ? kV15SnapshotSize : version<=17 ? kV17SnapshotSize : version==18 ? kV18SnapshotSize : version==19 ? kV19SnapshotSize : version==20 ? kV20SnapshotSize : version==21 ? kV21SnapshotSize : version==22 ? kV22SnapshotSize : kSnapshotSize;
    const auto payload = static_cast<unsigned>(bytes[6]) | (static_cast<unsigned>(bytes[7]) << 8);
    if (length != required || payload != length - 12) return SnapshotStatus::InvalidLength;
    if (get32(bytes + length - 4) != crc32(bytes, length - 4)) return SnapshotStatus::BadChecksum;
    if (get32(bytes + 8) != (version < 3 ? 1u : version == 3 ? 2u : version < 7 ? 3u : version==7 ? 4u : version==8 ? 5u : version==9 ? 6u : version==10 ? 7u : version==11 ? 8u : version==12 ? 9u : version==13 ? 10u : version==14 ? 11u : version<=16 ? 12u : version<=20 ? 13u : version==21 ? 14u : version==22 ? 15u : kRulesVersion)) return SnapshotStatus::UnsupportedRules;
    std::size_t offset = 12;
    const auto read = [&]() { const auto value = get32(bytes + offset); offset += 4; return value; };
    State next;
    next.sequence = read(); next.seed = read(); next.rngState = read();
    next.steps = read(); next.stepCredit = read(); next.hp = read();
    next.energy = read(); next.fullness = read(); next.mood = read();
    next.bond = read(); next.level = read(); next.captures = read();
    next.encounters = read(); next.wildHp = read(); next.wildMaxHp = read();
    next.captureAttempts = read();
    const auto phase = read();
    const auto card = read();
    next.attackBoost = read();
    next.shield = version == 1 ? 0 : read();
    const auto message = read();
    const auto lastMessage = version < 3 ? Message::Evolved : version < 5 ? Message::Selected : version<8 ? Message::Trained : version<15 ? Message::Released : version<17 ? Message::CaptureEnded : version<22 ? Message::EncounterCleared : version<23 ? Message::PartyRemoved : Message::Toileted;
    if (phase > (version < 5 ? 1u : 2u) || card > 1 || message > static_cast<unsigned>(lastMessage))
        return SnapshotStatus::InvalidState;
    next.phase = static_cast<Phase>(phase);
    next.cardUsed = card != 0;
    next.message = static_cast<Message>(message);
    if (version < 3) {
        next.legacyCaptures = next.captures;
        next.collection[0] = freshMember(1, Species::Mote, 0);
        next.wildSpecies = next.phase == Phase::Encounter ? Species::Flicker : Species::None;
        storeActive(next);
    } else {
        next.legacyCaptures = read(); next.activeCreatureId = read(); next.collectionCount = read();
        const auto savedCapacity=version<=20?kLegacyCollectionCapacity:kCollectionCapacity;
        if(next.collectionCount>savedCapacity)return SnapshotStatus::InvalidState;
        const auto wild = read();
        if (wild > (version<8 ? static_cast<unsigned>(Species::Cinder) : 65535u)) return SnapshotStatus::InvalidState;
        next.wildSpecies = static_cast<Species>(wild);
        for (std::size_t i=0;i<savedCapacity;++i) {
            auto& member=next.collection[i];
            member.id = read();
            const auto species = read();
            if (species > (version<8 ? static_cast<unsigned>(version < 5 ? Species::Cinder : Species::Renamon) : 65535u)) return SnapshotStatus::InvalidState;
            member.species = static_cast<Species>(species);
            member.hp = read(); member.energy = read(); member.fullness = read();
            member.mood = read(); member.bond = read(); member.level = read(); member.capturedAtSequence = read();
            if(version>=7){member.xp=read();member.formId=read();}
            if(version>=23)member.careState=read();
        }
    }
    if (version >= 5) {
        const auto completed = read();
        if (completed > 1) return SnapshotStatus::InvalidState;
        next.onboardingComplete = completed != 0;
        next.starterId = read();
    }
    if (version >= 6) {
        const auto mode = read(), outcome = read();
        if (mode > 1 || outcome > 3) return SnapshotStatus::InvalidState;
        next.battleMode = static_cast<BattleMode>(mode);
        next.lastAutoOutcome = static_cast<autobattle::Outcome>(outcome);
        next.lastAutoTurns = read(); next.lastAutoSequence = read();
    }
    if(version>=7) {next.wildLevel=read();next.wildTurn=read();}
    if(version>=8) {
        next.wildFormId=read();next.wildRules=read();next.nextMemberId=read();
        for(auto& word:next.journal) word=read();
    }
    if(version>=14) {
        next.explorationSteps=read();next.walkingEncounters=read();next.encounterRng=read();
        next.encounterTarget=read();next.encounterProgress=read();
        const auto rate=read();if(rate>3)return SnapshotStatus::InvalidState;
        next.encounterRate=static_cast<EncounterRate>(rate);
    }
    if(version>=15){
        next.lastCapture.sequence=read();next.lastCapture.targetFormId=read();next.lastCapture.chance=read();
        const auto packed=read(),result=read();if((packed&0xffff0000u)||result>3)return SnapshotStatus::InvalidState;
        next.lastCapture.attempt=static_cast<std::uint8_t>(packed);next.lastCapture.targetLevel=static_cast<std::uint8_t>(packed>>8);next.lastCapture.result=static_cast<CaptureResult>(result);
        next.starterOfferSeed=read();for(auto& offer:next.starterOffers)offer=read();
    }
    if(version>=16){next.pendingEncounter.formId=read();next.pendingEncounter.level=read();next.pendingEncounter.rules=read();next.foregroundSequence=read();}
    else next.foregroundSequence=next.sequence;
    if(version>=18)next.receivedTrades=read();
    if(version>=19){const auto value=read();if(value>1)return SnapshotStatus::InvalidState;next.autoCapture=static_cast<AutoCapture>(value);}
    if(version>=20)next.worldSeed=read();
    if(version>=22)for(auto& id:next.partyMemberIds)id=read();
    if(version>=23){
        next.careMinute=read(); next.lastCritical=read(); next.captureDeferred=read();
        if(next.lastCritical>1 || next.captureDeferred>1) return SnapshotStatus::InvalidState;
    }
    if(version==7 && !validRules4State(next,false)) return SnapshotStatus::InvalidState;
    if(version<7) {
        if(!validLegacyForVersion(next,version<4)) return SnapshotStatus::InvalidState;
        for(std::size_t i=0;i<next.collectionCount;++i) {
            auto& m=next.collection[i]; const auto oldTier=m.level;
            const auto oldMax=version<4 ? 100u : legacyMaxHp(m.species,oldTier);
            m.formId=forms::migrateLegacyForm(static_cast<std::uint32_t>(m.species),oldTier);
            m.level=oldTier==1 ? 1 : oldTier==2 ? 5 : 10; m.xp=xpForLevel(m.level);
            m.hp=scaleHp(m.hp,oldMax,maxHpForRules(m.formId,m.level,8));
        }
        if(next.onboardingComplete) loadActive(next);
        if(next.phase==Phase::Encounter) {
            next.wildLevel=1; next.wildTurn=0;
            const auto maximum=maxHp(rootForm(next.wildSpecies),1);
            next.wildHp=scaleHp(next.wildHp,next.wildMaxHp,maximum); next.wildMaxHp=maximum;
        }
    }
    if(version<8) {
        next.nextMemberId=next.onboardingComplete?next.collectionCount+1:1;
        for(std::size_t i=0;i<next.collectionCount;++i) obtain(next,next.collection[i].formId);
        if(next.phase==Phase::Encounter){next.wildFormId=rootForm(next.wildSpecies);next.wildRules=4;}
    }
    // The older epoch never allowed a founder to leave its original lineage.
    // Validate that stronger historical invariant before accepting new routes.
    if(version==8) {
        const auto* founder=findMember(next,1);
        if((founder&&static_cast<unsigned>(founder->species)!=(next.starterId?combat::starterSpecies(next.starterId):1u)) || next.wildRules>5)
            return SnapshotStatus::InvalidState;
    }
    // Older headers cannot claim a future encounter rules epoch.
    if(version==9) {
        const auto* founder=findMember(next,1);
        const auto root=legacy_v6::forms::initialForm(next.starterId?combat::starterSpecies(next.starterId):1u);
        if(next.wildRules>6 || (founder&&!legacy_v6::forms::canReach(root,founder->formId)))
            return SnapshotStatus::InvalidState;
    }
    if(version==10) {
        const auto* founder=findMember(next,1);
        const auto root=legacy_v7::forms::initialForm(next.starterId?combat::starterSpecies(next.starterId):1u);
        if(next.wildRules>7 || (founder&&!legacy_v7::forms::canReach(root,founder->formId)))
            return SnapshotStatus::InvalidState;
    }
    if(version<=22 && (next.wildRules>15 || next.pendingEncounter.rules>15)) return SnapshotStatus::InvalidState;
    if(version<=21 && (next.wildRules>14 || next.pendingEncounter.rules>14)) return SnapshotStatus::InvalidState;
    if(version<=20 && (next.wildRules>13 || next.pendingEncounter.rules>13)) return SnapshotStatus::InvalidState;
    if(version<=16 && (next.wildRules>12 || next.pendingEncounter.rules>12)) return SnapshotStatus::InvalidState;
    if(version==14 && next.wildRules>11) return SnapshotStatus::InvalidState;
    if(version==13 && next.wildRules>10) return SnapshotStatus::InvalidState;
    if(version==12 && next.wildRules>9) return SnapshotStatus::InvalidState;
    if(version==11 && next.wildRules>8) return SnapshotStatus::InvalidState;
    if(version<12) {
        if(next.onboardingComplete) {
            const auto* m=activeMember(next);
            if(!m || next.hp!=m->hp || next.energy!=m->energy || next.fullness!=m->fullness ||
               next.mood!=m->mood || next.bond!=m->bond || next.level!=m->level) return SnapshotStatus::InvalidState;
        }
        // Validate the old HP domain before converting: a valid CRC is not enough.
        for(std::size_t i=0;i<next.collectionCount && i<kCollectionCapacity;++i) {
            auto& m=next.collection[i]; const auto maximum=maxHpForRules(m.formId,m.level,8);
            if(!maximum || !m.hp || m.hp>maximum) return SnapshotStatus::InvalidState;
            if(next.phase!=Phase::Encounter || m.id!=next.activeCreatureId)
                m.hp=scaleHp(m.hp,maximum,maxHp(m.formId,m.level));
        }
        if(next.onboardingComplete && activeMember(next)) loadActive(next);
    }
    if(!isValid(next)) return SnapshotStatus::InvalidState;
    state = next;
    return version < kSchemaVersion ? SnapshotStatus::Migrated : SnapshotStatus::Ok;
}
const char* snapshotStatusText(SnapshotStatus status) {
    switch (status) {
    case SnapshotStatus::Ok: return "ok";
    case SnapshotStatus::Migrated: return "migrated legacy snapshot to version 23";
    case SnapshotStatus::InvalidLength: return "invalid snapshot length";
    case SnapshotStatus::BadMagic: return "invalid snapshot magic";
    case SnapshotStatus::UnsupportedVersion: return "unsupported snapshot version";
    case SnapshotStatus::UnsupportedRules: return "unsupported rules version";
    case SnapshotStatus::BadChecksum: return "snapshot checksum mismatch";
    case SnapshotStatus::InvalidState: return "snapshot contains invalid game state";
    }
    return "unknown snapshot status";
}
} // namespace digivice::legacy_v16

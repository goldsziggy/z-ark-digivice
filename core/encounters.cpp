#include "encounters.hpp"
#include "forms.hpp"
namespace digivice::encounters {
namespace {
#include "encounter_rarity_generated.inc"
std::uint32_t mix(std::uint32_t value) {
    value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;return value^(value>>16);
}
bool eligible(std::uint32_t id,forms::CombatTier tier,std::uint32_t level) {
    const auto* form=forms::find(id);
    return form && level>=form->minLevel && forms::combatTier(id)<=tier;
}
}
Rarity rarityForForm(std::uint32_t id) { return id>=1 && id<=forms::kFormCount ? kRarities[id-1] : Rarity::Unknown; }
const char* rarityName(Rarity value) {
    switch(value) {case Rarity::Common:return "common";case Rarity::Uncommon:return "uncommon";case Rarity::Rare:return "rare";default:return nullptr;}
}
std::uint32_t select(std::uint32_t encounter,std::uint32_t seed,std::uint32_t partner,std::uint32_t level) {
    if(!encounter || !forms::find(partner) || level<1 || level>forms::kMaxRpgLevel)return 0;
    if(encounter==1)return 4; // One gentle, familiar first encounter; no random draw.
    const auto partnerTier=forms::combatTier(partner);
    const auto tier=partnerTier<forms::CombatTier::Rookie?forms::CombatTier::Rookie:partnerTier;
    std::uint32_t counts[4]{};
    for(std::uint32_t id=1;id<=forms::kFormCount;++id)if(eligible(id,tier,level))++counts[static_cast<unsigned>(rarityForForm(id))];
    constexpr std::uint32_t weights[]{0,70,25,5};
    std::uint32_t total=0;for(unsigned bucket=1;bucket<=3;++bucket)if(counts[bucket])total+=weights[bucket];
    if(!total)return 0;
    const auto key=seed^mix(encounter^0x9e3779b9u);
    auto roll=mix(key^0x52415245u)%total;
    unsigned selected=0;
    for(unsigned bucket=1;bucket<=3;++bucket)if(counts[bucket]){
        if(roll<weights[bucket]){selected=bucket;break;}roll-=weights[bucket];
    }
    if(!selected)return 0;
    auto index=mix(key^0x464f524du)%counts[selected];
    for(std::uint32_t id=1;id<=forms::kFormCount;++id)
        if(static_cast<unsigned>(rarityForForm(id))==selected && eligible(id,tier,level) && index--==0)return id;
    return 0;
}
std::uint32_t selectProduction(std::uint32_t encounter,std::uint32_t seed,std::uint32_t partner,std::uint32_t level) {
    if(!encounter || !forms::find(partner) || level<1 || level>forms::kMaxRpgLevel)return 0;
    const auto partnerTier=forms::combatTier(partner);
    const auto tier=partnerTier<forms::CombatTier::Rookie?forms::CombatTier::Rookie:partnerTier;
    std::uint32_t counts[4]{};
    for(std::uint32_t id=1;id<=forms::kFormCount;++id)if(forms::productionForm(id)&&eligible(id,tier,level))++counts[static_cast<unsigned>(rarityForForm(id))];
    constexpr std::uint32_t weights[]{0,70,25,5};
    std::uint32_t total=0;for(unsigned bucket=1;bucket<=3;++bucket)if(counts[bucket])total+=weights[bucket];
    if(!total)return 0;
    const auto key=seed^mix(encounter^0x9e3779b9u);
    auto roll=mix(key^0x52415245u)%total;
    unsigned selected=0;
    for(unsigned bucket=1;bucket<=3;++bucket)if(counts[bucket]){
        if(roll<weights[bucket]){selected=bucket;break;}roll-=weights[bucket];
    }
    if(!selected)return 0;
    auto index=mix(key^0x464f524du)%counts[selected];
    for(std::uint32_t id=1;id<=forms::kFormCount;++id)
        if(forms::productionForm(id) && static_cast<unsigned>(rarityForForm(id))==selected && eligible(id,tier,level) && index--==0)return id;
    return 0;
}
} // namespace digivice::encounters

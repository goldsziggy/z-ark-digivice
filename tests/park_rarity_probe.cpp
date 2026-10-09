// Native-only frequency/eligibility/repeatability probe, not physical randomness.
#include "game.hpp"
#include "forms.hpp"
#include "encounters.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
namespace g=digivice;
namespace f=digivice::forms;
namespace e=digivice::encounters;
void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"rarity probe: %s\n",why);std::exit(1);}}
int main(){
 constexpr unsigned partners[]{18,19,20,21},levels[]{1,5,10,15};
 require(g::selectWildForm(0,1,18,1)==0,"zero encounter");
 require(g::selectWildForm(2,1,999,1)==0,"invalid partner");
 require(g::selectWildForm(2,1,18,21)==0,"invalid level");
 for(unsigned gate=0;gate<4;++gate){
  std::array<unsigned,513> counts{};unsigned buckets[4]{},pool[4]{},repeat=0;
  for(unsigned form=1;form<=f::kFormCount;++form){const auto* row=f::find(form);
   if(row->minLevel<=levels[gate]&&f::combatTier(form)<=f::combatTier(partners[gate]))++pool[static_cast<unsigned>(e::rarityForForm(form))];}
  for(unsigned index=0;index<64;++index){
   const unsigned seed=0x9e3779b9u*(index+1)^0x5041524bu;
   require(g::selectWildForm(1,seed,partners[gate],levels[gate])==4,"first Flicker");
   unsigned previous=0;
   for(unsigned encounter=2;encounter<258;++encounter){
    const auto form=g::selectWildForm(encounter,seed,partners[gate],levels[gate]);
    require(form==g::selectWildForm(encounter,seed,partners[gate],levels[gate]),"determinism");
    const auto* row=f::find(form);require(row&&row->minLevel<=levels[gate]&&f::combatTier(form)<=f::combatTier(partners[gate]),"eligibility");
    const auto rarity=static_cast<unsigned>(e::rarityForForm(form));require(rarity>=1&&rarity<=3,"rarity range");
    ++counts[form];++buckets[rarity];repeat+=form==previous;previous=form;
   }
  }
  std::printf("{\"partnerForm\":%u,\"level\":%u,\"draws\":16384,\"poolCounts\":[%u,%u,%u],\"bucketCounts\":[%u,%u,%u],\"adjacentRepeats\":%u,\"forms\":[",partners[gate],levels[gate],pool[1],pool[2],pool[3],buckets[1],buckets[2],buckets[3],repeat);
  bool first=true;for(unsigned form=1;form<=f::kFormCount;++form)if(counts[form]){std::printf("%s{\"formId\":%u,\"count\":%u,\"rarity\":\"%s\"}",first?"":",",form,counts[form],e::rarityName(e::rarityForForm(form)));first=false;}
  std::printf("]}\n");
 }
}

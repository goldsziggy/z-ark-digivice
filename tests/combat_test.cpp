#include "combat.hpp"
#include "forms.hpp"
#include "legacy_combat_v3.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace digivice::combat;
int main(){
 unsigned checks=0,failures=0;const auto check=[&](bool ok){++checks;if(!ok){++failures;std::fprintf(stderr,"combat check%u failed\n",checks);}};
 for(unsigned a=1;a<=4;++a)for(unsigned d=1;d<=4;++d){const auto expected=(a==1&&d==3)||(a==3&&d==4)||(a==4&&d==1)?125u:(a==3&&d==1)||(a==4&&d==3)||(a==1&&d==4)?80u:100u;check(typePercent(a,d)==expected);}
 check(resolve(3,1,4,1,Move::Magic,Defense::None).damage==20);
 check(resolve(3,1,4,1,Move::Magic,Defense::Ward).damage==10);
 check(resolve(4,1,1,1,Move::Heavy,Defense::Brace).damage==30);
 const auto reflected=resolve(4,1,1,1,Move::Heavy,Defense::Counter);check(reflected.reflected&&reflected.damage==15);
 check(resolveForms(2,5,5,1,Move::Physical,Defense::None).damage==20);
 check(resolve(5,1,2,1,Move::Magic,Defense::None).damage==20);
 check(resolve(5,1,2,1,Move::Physical,Defense::None).damage==12);
 check(resolve(6,1,2,1,Move::Physical,Defense::None).damage==20);
 check(resolve(6,1,2,1,Move::Magic,Defense::None).damage==10);
 check(resolve(0,1,1,1,Move::Physical,Defense::None).damage==0);
 check(resolve(2,21,1,1,Move::Physical,Defense::None).damage==0);
 check(resolve(1,1,1,1,static_cast<Move>(99),Defense::None).damage==0);
 // Bounded optional raw floor precedes type/guard; default remains identical.
 for(const auto invalid:{0u,3u,33u,UINT32_MAX})check(resolveForms(223,15,223,15,Move::Heavy,Defense::None,invalid).damage==0);
 check(resolveForms(223,15,223,15,Move::Physical,Defense::None,11).damage==11);
 check(resolveForms(223,15,223,15,Move::Physical,Defense::Brace,11).damage==5);
 check(resolveForms(223,15,223,15,Move::Heavy,Defense::Counter,11).damage==5);
 check(resolveForms(223,15,223,15,Move::Heavy,Defense::Counter,11).reflected);
 check(resolveForms(223,15,235,15,Move::Heavy,Defense::None,11).damage==13); // Ember into Grove,125%.
 check(resolveForms(223,15,235,15,Move::Heavy,Defense::Counter,11).damage==6);
 check(resolveForms(223,15,257,15,Move::Magic,Defense::Ward,11).damage==4); //80%, then Ward.

 char json[kProfileJsonCapacity],tree[kCatalogJsonCapacity],small[8];unsigned profiles=0,maxHp=0,maxOther=0;
 for(unsigned id=1;id<=digivice::forms::kFormCount;++id){const auto* f=digivice::forms::find(id);check(f&&digivice::forms::validForLineage(id,f->lineage));
  for(unsigned level=f->minLevel;level<=20;++level){++profiles;const auto p=formProfile(id,level);check(validFormProfile(id,level));check(p.stats.maxHp&&p.stats.maxHp<=400);check(p.stats.attack<=120&&p.stats.defense<=120&&p.stats.magic<=120&&p.stats.resistance<=120);check(writeFormProfileJson(id,level,json,sizeof(json))>0);check(std::strcmp(p.physicalSkill,p.magicSkill)&&std::strcmp(p.physicalSkill,p.heavySkill));
   if(p.stats.maxHp>maxHp)maxHp=p.stats.maxHp;for(const auto v:{p.stats.attack,p.stats.defense,p.stats.magic,p.stats.resistance})if(v>maxOther)maxOther=v;
   for(unsigned move=0;move<3;++move)for(unsigned defense=0;defense<4;++defense){const auto hit=resolveForms(id,level,4,level,static_cast<Move>(move),static_cast<Defense>(defense));check(hit.damage>=1&&hit.damage<=400);const auto explicitDefault=resolveForms(id,level,4,level,static_cast<Move>(move),static_cast<Defense>(defense),4);check(hit.damage==explicitDefault.damage&&hit.reflected==explicitDefault.reflected&&hit.typePercent==explicitDefault.typePercent);}
  }
  check(!validFormProfile(id,21));if(f->minLevel>1)check(!validFormProfile(id,f->minLevel-1));
 }
 for(unsigned starter=1;starter<=8;++starter){const auto lineage=starterSpecies(starter),root=digivice::forms::initialForm(lineage);check(root==11+7*(starter-1));
  for(unsigned tier=1;tier<=3;++tier){const auto level=tier==1?1u:tier==2?5u:10u;const auto now=formProfile(root,level).stats;const auto old=digivice::legacy_v3::combat::profile(lineage,tier).stats;check(now.maxHp==old.maxHp&&now.attack==old.attack&&now.defense==old.defense&&now.magic==old.magic&&now.resistance==old.resistance);}
  const auto n=writeEvolutionJson(lineage,tree,sizeof(tree));check(n>0&&std::strstr(tree,"\"requiredBond\":80")&&std::strstr(tree,"\"stage\":\"Mega\""));check(writeEvolutionJson(lineage,small,sizeof(small))==0&&small[0]=='\0');check(writeEvolutionJson(lineage,tree,n)==0&&tree[0]=='\0');
 }
 check(!validProfile(13,1)&&!validFormProfile(digivice::forms::kFormCount+1,1));check(writeProfileJson(1,1,small,sizeof(small))==0&&small[0]=='\0');
 // Historical profiles above remain addressable; the current catalog exposes
 // only the eight production starters, never the original fixture species.
 const auto catalogBytes=writeCatalogJson(tree,sizeof(tree));check(catalogBytes>0&&std::strstr(tree,"Night of Fire")&&std::strstr(tree,"\"rulesVersion\":15"));
 unsigned catalogProfiles=0;for(const char* p=tree;(p=std::strstr(p,"\"species\":"));++p)++catalogProfiles;check(catalogProfiles==8);
 for(unsigned id=1;id<=10;++id){char excluded[64];std::snprintf(excluded,sizeof(excluded),"\"name\":\"%s\"",digivice::forms::find(id)->name);check(std::strstr(tree,excluded)==nullptr);}
 for(unsigned starter=1;starter<=8;++starter){char expected[64];std::snprintf(expected,sizeof(expected),"\"name\":\"%s\"",profile(starterSpecies(starter),1).name);check(std::strstr(tree,expected)!=nullptr);}
 check(writeCatalogJson(small,sizeof(small))==0);check(writeCatalogJson(nullptr,0)==0);
 char starters[kStarterJsonCapacity];const auto starterBytes=writeStarterJson(starters,sizeof(starters));check(starterBytes>0&&std::strstr(starters,"\"id\":8"));check(writeStarterJson(starters,starterBytes)==0&&starters[0]=='\0');
 std::printf("%u form/level profiles; maxHP%u other%u; initialcatalog%zu starters%zu bytes\n",profiles,maxHp,maxOther,catalogBytes,starterBytes);
 std::printf("%u combat checks, %u failures\n",checks,failures);return failures?1:0;
}

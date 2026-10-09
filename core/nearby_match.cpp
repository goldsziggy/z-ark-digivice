#include "nearby_match.hpp"
#include "battle_trace.hpp"
#include "forms.hpp"

namespace digivice::nearby {
namespace {
bool attack(Choice c){return c>=Choice::Physical&&c<=Choice::Magic;}
bool defense(Choice c){return c>=Choice::Brace&&c<=Choice::Ward;}
unsigned cost(Choice c){return c==Choice::Heavy?6u:2u;}
std::uint32_t maximum(const Match& m,unsigned actor){const auto& f=m.fighters[actor];return combat::formProfile(f.formId,f.level).stats.maxHp;}
void put(std::uint8_t* b,std::uint32_t n){for(unsigned i=0;i<4;++i)b[i]=static_cast<std::uint8_t>(n>>(8*i));}
std::uint32_t get(const std::uint8_t* b){std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=static_cast<std::uint32_t>(b[i])<<(8*i);return n;}
}
bool sameFighter(const Fighter& a,const Fighter& b){return a.memberId==b.memberId&&a.formId==b.formId&&a.level==b.level&&a.offenseBonus==b.offenseBonus&&a.protectionBonus==b.protectionBonus;}
bool validFighter(const Fighter& f){return f.memberId&&f.memberId!=UINT32_MAX&&combat::validFormProfile(f.formId,f.level)&&combat::validCareBonus({f.offenseBonus,f.protectionBonus});}
bool valid(const Match& m){
    static_assert(forms::kCatalogVersion==kCatalog);
    if(!validFighter(m.fighters[0])||!validFighter(m.fighters[1])||!m.seed||!m.rng||
       static_cast<unsigned>(m.mode)>1||static_cast<unsigned>(m.status)>3||m.sequence>kMaxExchanges||
       m.attacker!=m.sequence%2||m.lastAttacker>1)return false;
    for(unsigned i=0;i<2;++i)if(m.hp[i]>maximum(m,i)||m.energy[i]>100||m.lastDamage[i]>maximum(m,i))return false;
    if(!m.sequence){if(m.lastAttack!=Choice::None||m.lastDefense!=Choice::None||m.lastDamage[0]||m.lastDamage[1]||m.lastReflected||m.lastAttacker)return false;}
    else if(m.lastAttacker!=(m.sequence-1)%2||!attack(m.lastAttack)||!defense(m.lastDefense)||
            m.lastReflected!=(m.lastAttack==Choice::Heavy&&m.lastDefense==Choice::Counter)||
            (m.lastDamage[0]&&m.lastDamage[1]))return false;
    if(m.status==Status::Active)return m.sequence<kMaxExchanges&&m.hp[0]&&m.hp[1];
    if(!m.sequence)return false;
    if(m.status==Status::HostWon)return m.hp[0]&&!m.hp[1];
    if(m.status==Status::GuestWon)return !m.hp[0]&&m.hp[1];
    return m.sequence==kMaxExchanges&&m.hp[0]&&m.hp[1];
}
bool begin(const Fighter& host,const Fighter& guest,Mode mode,std::uint32_t seed,Match& out){
    if(!validFighter(host)||!validFighter(guest)||!seed||static_cast<unsigned>(mode)>1)return false;
    Match next;next.fighters[0]=host;next.fighters[1]=guest;next.mode=mode;next.seed=next.rng=seed;
    next.hp[0]=maximum(next,0);next.hp[1]=maximum(next,1);next.energy[0]=next.energy[1]=100;
    if(!valid(next))return false;
    out=next;return true;
}
bool legalChoice(const Match& m,unsigned actor,Choice choice){
    if(!valid(m)||m.status!=Status::Active||actor>1)return false;
    return actor==m.attacker?attack(choice)&&(choice!=Choice::Heavy||m.energy[actor]>=cost(choice)):defense(choice);
}
bool resolve(Match& state,std::uint32_t sequence,Choice host,Choice guest){
    if(sequence!=state.sequence||!legalChoice(state,0,host)||!legalChoice(state,1,guest))return false;
    Match next=state;const unsigned attacker=next.attacker,defender=1u-attacker;
    const Choice choices[]{host,guest};const auto a=choices[attacker],d=choices[defender];
    const auto move=a==Choice::Physical?combat::Move::Physical:a==Choice::Heavy?combat::Move::Heavy:combat::Move::Magic;
    const auto guard=d==Choice::Brace?combat::Defense::Brace:d==Choice::Counter?combat::Defense::Counter:combat::Defense::Ward;
    const auto maxHp=maximum(next,defender);const auto floor=(maxHp+19)/20;
    const auto hit=combat::resolveCareForms(next.fighters[attacker].formId,next.fighters[attacker].level,
        next.fighters[defender].formId,next.fighters[defender].level,move,guard,{next.fighters[attacker].offenseBonus,next.fighters[attacker].protectionBonus},
        {next.fighters[defender].offenseBonus,next.fighters[defender].protectionBonus},floor<4?4:floor>32?32:floor);
    if(!hit.damage)return false;
    next.energy[attacker]=next.energy[attacker]>cost(a)?next.energy[attacker]-cost(a):0;next.lastDamage[0]=next.lastDamage[1]=0;
    const auto target=hit.reflected?attacker:defender;const auto received=hit.damage<next.hp[target]?hit.damage:next.hp[target];
    next.hp[target]-=received;next.lastDamage[target]=received;next.lastAttack=a;next.lastDefense=d;
    next.lastAttacker=attacker;next.lastReflected=hit.reflected;++next.sequence;next.attacker=static_cast<std::uint8_t>(1u-attacker);
    if(!next.hp[0])next.status=Status::GuestWon;else if(!next.hp[1])next.status=Status::HostWon;
    else if(next.sequence==kMaxExchanges)next.status=Status::Draw;
    if(!valid(next))return false;
    state=next;return true;
}
bool resolveAuto(Match& state,std::uint32_t sequence){
    if(!valid(state)||state.mode!=Mode::Auto||sequence!=state.sequence||state.status!=Status::Active)return false;
    Match next=state;const auto a=(autobattle::nextRandom(next.rng)&1u)?Choice::Magic:Choice::Physical;
    const auto d=static_cast<Choice>(static_cast<unsigned>(Choice::Brace)+autobattle::nextRandom(next.rng)%3u);
    if(!resolve(next,sequence,next.attacker?d:a,next.attacker?a:d))return false;
    state=next;return true;
}
bool encode(const Match& m,std::uint8_t* bytes,std::size_t capacity){
    if(!bytes||capacity<kMatchBytes||!valid(m))return false;
    const std::uint32_t words[]{kRules,kCatalog,m.fighters[0].memberId,m.fighters[0].formId,m.fighters[0].level,
      m.fighters[1].memberId,m.fighters[1].formId,m.fighters[1].level,m.hp[0],m.hp[1],m.energy[0],m.energy[1],
      m.seed,m.rng,m.sequence,static_cast<unsigned>(m.mode),static_cast<unsigned>(m.status),m.attacker,m.lastAttacker,
      static_cast<unsigned>(m.lastAttack),static_cast<unsigned>(m.lastDefense),m.lastDamage[0],m.lastDamage[1],m.lastReflected?1u:0u,0,m.fighters[0].offenseBonus,m.fighters[0].protectionBonus,m.fighters[1].offenseBonus,m.fighters[1].protectionBonus};
    static_assert(sizeof(words)==kMatchBytes);for(unsigned i=0;i<29;++i)put(bytes+4*i,words[i]);return true;
}
bool decode(const std::uint8_t* bytes,std::size_t length,Match& out){
    if(!bytes||length!=kMatchBytes||get(bytes)!=kRules||get(bytes+4)!=kCatalog||get(bytes+96))return false;
    std::uint32_t w[29];for(unsigned i=0;i<29;++i)w[i]=get(bytes+4*i);
    if(w[15]>1||w[16]>3||w[17]>1||w[18]>1||w[19]>6||w[20]>6||w[23]>1)return false;
    Match m;m.fighters[0]={w[2],w[3],w[4],w[25],w[26]};m.fighters[1]={w[5],w[6],w[7],w[27],w[28]};m.hp[0]=w[8];m.hp[1]=w[9];
    m.energy[0]=w[10];m.energy[1]=w[11];m.seed=w[12];m.rng=w[13];m.sequence=w[14];m.mode=static_cast<Mode>(w[15]);
    m.status=static_cast<Status>(w[16]);m.attacker=static_cast<std::uint8_t>(w[17]);m.lastAttacker=static_cast<std::uint8_t>(w[18]);
    m.lastAttack=static_cast<Choice>(w[19]);m.lastDefense=static_cast<Choice>(w[20]);m.lastDamage[0]=w[21];m.lastDamage[1]=w[22];m.lastReflected=w[23]!=0;
    if(!valid(m))return false;
    out=m;return true;
}
const char* choiceName(Choice c){constexpr const char* names[]{"none","physical","heavy","magic","brace","counter","ward"};const auto n=static_cast<unsigned>(c);return n<7?names[n]:"invalid";}
} // namespace digivice::nearby

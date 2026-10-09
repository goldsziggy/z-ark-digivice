#include "../main/audio_settings.hpp"
#include "music_synth.hpp"
#include "audio_test_nvs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0;
void check(bool value,const char* text,int line){++checks;if(!value){std::fprintf(stderr,"%d: %s\n",line,text);std::exit(1);}}
#define CHECK(x) check((x),#x,__LINE__)
void recordAndNvs() {
    using namespace digivice::sound;
    for(unsigned volume=0;volume<=100;++volume)for(bool muted:{false,true})for(bool music:{false,true}) {
        Preferences original{static_cast<std::uint8_t>(volume),muted,music},decoded;
        SettingsRecord encoded;CHECK(encodeSettings(original,encoded));CHECK(decodeSettings(encoded,decoded));
        CHECK(samePreferences(original,decoded));
        for(unsigned byte=0;byte<kSettingsBytes;++byte)for(unsigned bit=0;bit<8;++bit) {
            auto corrupt=encoded;corrupt.bytes[byte]^=1u<<bit;const auto before=decoded;
            CHECK(!decodeSettings(corrupt,decoded));CHECK(samePreferences(before,decoded));
        }
    }
    SettingsRecord invalid;CHECK(!encodeSettings({101,false,false},invalid));
    // Exact DAUD v1 fixture emitted by f9: 50%, unmuted, music on. Expanding
    // the valid range requires no record rewrite or game-save migration.
    const SettingsRecord legacy{{0x44,0x41,0x55,0x44,0x01,0x00,0x00,0x00,
        0x32,0x00,0x01,0x00,0x3d,0x92,0xa6,0x0c}};
    audioTestNvs={};audioTestNvs.present=true;
    audioTestNvs.bytes.assign(legacy.bytes,legacy.bytes+kSettingsBytes);
    {
        NvsSettings upgraded;CHECK(upgraded.begin()==ESP_OK);
        CHECK(samePreferences(upgraded.preferences(),{50,false,true}));
        CHECK(upgraded.save({50,false,true})==ESP_OK&&!audioTestNvs.sets);
        CHECK(upgraded.save({100,false,true})==ESP_OK);
        CHECK(audioTestNvs.bytes.size()==16&&audioTestNvs.bytes[4]==1);
    }
    {NvsSettings reboot;CHECK(reboot.begin()==ESP_OK);CHECK(samePreferences(reboot.preferences(),{100,false,true}));}
    audioTestNvs={};
    {
        NvsSettings settings;CHECK(settings.begin()==ESP_OK);CHECK(settings.writable());
        CHECK(samePreferences(settings.preferences(),{15,false,false}));CHECK(!audioTestNvs.sets);
        CHECK(settings.save({15,false,false})==ESP_OK&&!audioTestNvs.sets);
        CHECK(settings.save({30,true,true})==ESP_OK&&audioTestNvs.sets==1&&audioTestNvs.commits==1);
        CHECK(settings.save({30,true,true})==ESP_OK&&audioTestNvs.sets==1);
        CHECK(settings.save({101,false,false})==ESP_ERR_INVALID_ARG&&settings.writable());
    }
    CHECK(audioTestNvs.closes==1);
    {NvsSettings reboot;CHECK(reboot.begin()==ESP_OK);CHECK(samePreferences(reboot.preferences(),{30,true,true}));}
    for(unsigned size:{0u,1u,15u,17u,4096u}) {
        audioTestNvs={};audioTestNvs.present=true;audioTestNvs.bytes.resize(size);
        NvsSettings settings;CHECK(settings.begin()==ESP_ERR_INVALID_SIZE&&!settings.writable());
        CHECK(settings.save({5,true,false})!=ESP_OK&&!audioTestNvs.sets);
    }
    audioTestNvs={};audioTestNvs.present=true;audioTestNvs.bytes.resize(16);
    {NvsSettings settings;CHECK(settings.begin()==ESP_ERR_INVALID_RESPONSE);CHECK(!settings.writable());CHECK(!audioTestNvs.sets);}
    audioTestNvs={};audioTestNvs.openError=ESP_FAIL;
    {NvsSettings settings;CHECK(settings.begin()==ESP_FAIL);CHECK(settings.begin()==ESP_FAIL&&audioTestNvs.opens==1);}
    for(unsigned fault=0;fault<4;++fault) {
        audioTestNvs={};NvsSettings settings;CHECK(settings.begin()==ESP_OK);
        audioTestNvs.writeFail=fault==0;audioTestNvs.commitFail=fault==1;
        audioTestNvs.corruptAfterCommit=fault==2;audioTestNvs.readFail=fault==3;
        CHECK(settings.save({50,true,true})!=ESP_OK);CHECK(!settings.writable());
        CHECK(samePreferences(settings.preferences(),{15,false,false}));
        const auto writes=audioTestNvs.sets;CHECK(settings.save({0,true,false})!=ESP_OK&&audioTestNvs.sets==writes);
        // Lost commit acknowledgement may have landed: reboot validates actual bytes.
        if(fault==1){audioTestNvs.commitFail=false;NvsSettings reboot;CHECK(reboot.begin()==ESP_OK&&reboot.preferences().volume==50);}
    }
}
void synthBoundsAndTransitions() {
    using namespace digivice::device;
    AudioMixer home,battle,muted,capped,excess;
    long long homeEnergy=0,battleEnergy=0;unsigned differences=0;
    for(unsigned frame=0;frame<kAudioSampleRate*12;++frame) {
        const auto a=home.sample(15,true,MusicScene::Home),b=battle.sample(15,true,MusicScene::Battle);
        CHECK(muted.sample(0,true,MusicScene::Home)==0);
        const auto c=capped.sample(100,true,MusicScene::Home),e=excess.sample(255,true,MusicScene::Home);
        CHECK(c==e&&std::abs(c)<=1901);CHECK(std::abs(a)<=286&&std::abs(b)<=286);
        homeEnergy+=std::abs(a);battleEnergy+=std::abs(b);differences+=a!=b;
    }
    CHECK(homeEnergy>100000&&battleEnergy>100000&&differences>10000);
    for(unsigned frame=0;frame<kAudioSampleRate;++frame)home.sample(15,false,MusicScene::Home);
    CHECK(!home.needsSamples(false,MusicScene::Home));CHECK(home.sample(15,false,MusicScene::Home)==0);
    for(unsigned frame=0;frame<kAudioSampleRate;++frame)battle.sample(15,true,MusicScene::Quiet);
    CHECK(!battle.needsSamples(true,MusicScene::Quiet));
    long long resumed=0;for(unsigned frame=0;frame<kAudioSampleRate;++frame)resumed+=std::abs(battle.sample(15,true,MusicScene::Home));
    CHECK(resumed>10000);
    MusicSynth full,ducked;double fullEnergy=0,duckedEnergy=0;
    for(unsigned frame=0;frame<kAudioSampleRate*2;++frame){
        const float f=full.sample(true,MusicScene::Home,false),d=ducked.sample(true,MusicScene::Home,true);
        CHECK(std::isfinite(f)&&std::isfinite(d)&&std::fabs(f)<=.058F&&std::fabs(d)<=.058F);
        if(frame>kAudioSampleRate/10){fullEnergy+=f*f;duckedEnergy+=d*d;}
    }
    CHECK(std::fabs(std::sqrt(duckedEnergy/fullEnergy)-.22)<.0001);
    for(unsigned cue=0;cue<static_cast<unsigned>(AudioCue::Count);++cue) {
        AudioMixer mixed;mixed.start(static_cast<AudioCue>(cue));long long energy=0;
        for(unsigned frame=0;frame<kAudioSampleRate*2;++frame) {
            const auto sample=mixed.sample(100,true,MusicScene::Battle);
            CHECK(std::abs(sample)<23200);energy+=std::abs(sample);
        }
        CHECK(mixed.cueFinished()&&mixed.needsSamples(true,MusicScene::Battle));CHECK(energy>100000);
    }
    CHECK(sizeof(AudioMixer)<=256&&sizeof(MusicSynth)<=64);
}
void everySelectableLevel() {
    using namespace digivice::device;
    constexpr unsigned levels[]{0,5,15,30,50,75,100};
    long long previousEnergy[static_cast<unsigned>(AudioCue::Count)]{};
    long long previousMusic=0;
    for(const auto level:levels) {
        unsigned peak=0;long long total=0,musicEnergy=0;
        for(unsigned cue=0;cue<static_cast<unsigned>(AudioCue::Count);++cue) {
            AudioMixer effects,mixed;effects.start(static_cast<AudioCue>(cue));mixed.start(static_cast<AudioCue>(cue));
            long long energy=0;
            for(unsigned frame=0;frame<kAudioSampleRate*2;++frame) {
                // Music off must not silence SFX; enabling music leaves both
                // bounded, including high-priority overlapping outcome notes.
                const auto effect=std::abs(effects.sample(level,false,MusicScene::Home));
                const auto both=std::abs(mixed.sample(level,true,MusicScene::Battle));
                CHECK(effect<=21299&&both<23200);
                peak=std::max(peak,static_cast<unsigned>(both));energy+=effect;
            }
            CHECK(effects.cueFinished()&&!effects.needsSamples(false,MusicScene::Home));
            CHECK(level==0?energy==0:energy>previousEnergy[cue]);previousEnergy[cue]=energy;total+=energy;
        }
        AudioMixer music;
        for(unsigned frame=0;frame<kAudioSampleRate*2;++frame)
            musicEnergy+=std::abs(music.sample(level,true,MusicScene::Home));
        CHECK(level==0?musicEnergy==0:musicEnergy>previousMusic);previousMusic=musicEnergy;
        std::printf("Audio level %u%%: all19 effects energy=%lld, music energy=%lld, mixed peak=%u/32767 (synthetic)\n",
            level,total,musicEnergy,peak);
    }
}
}
int main(){
    recordAndNvs();synthBoundsAndTransitions();everySelectableLevel();
    std::printf("Audio settings/music: %u checks passed; portable record=%zu B NvsSettings=%zu B MusicSynth=%zu B AudioMixer=%zu B; host only\n",
        checks,sizeof(digivice::sound::SettingsRecord),sizeof(digivice::sound::NvsSettings),sizeof(digivice::device::MusicSynth),sizeof(digivice::device::AudioMixer));
}

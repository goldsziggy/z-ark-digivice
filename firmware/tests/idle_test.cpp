#include "idle.hpp"
#include "../main/idle_settings.hpp"
#include "display_touch.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

namespace {
unsigned checks=0;
void check(bool value,const char* text,int line){++checks;if(!value){std::fprintf(stderr,"%d: %s\n",line,text);std::exit(1);}}
#define CHECK(x) check((x),#x,__LINE__)
struct FakeNvs {
    std::vector<unsigned char> bytes;
    bool present=false,readFail=false,writeFail=false,commitFail=false,corruptAfterCommit=false;
    esp_err_t openError=ESP_OK;
    unsigned opens=0,closes=0,sets=0,commits=0,reads=0;
} nvs;
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* handle){CHECK(!std::strcmp(name,"digi_idle")&&mode==NVS_READWRITE);++nvs.opens;if(nvs.openError!=ESP_OK)return nvs.openError;*handle=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){CHECK(h==1);++nvs.closes;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* bytes,std::size_t* size){
    CHECK(h==1&&!std::strcmp(key,"config"));++nvs.reads;
    if(nvs.readFail)return ESP_FAIL;if(!nvs.present)return ESP_ERR_NVS_NOT_FOUND;
    if(!bytes){*size=nvs.bytes.size();return ESP_OK;}
    CHECK(*size>=nvs.bytes.size());std::memcpy(bytes,nvs.bytes.data(),nvs.bytes.size());*size=nvs.bytes.size();return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* bytes,std::size_t size){
    CHECK(h==1&&!std::strcmp(key,"config")&&size==digivice::idle::kSettingsBytes);++nvs.sets;
    if(nvs.writeFail)return ESP_FAIL;
    nvs.bytes.assign(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);nvs.present=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){CHECK(h==1);++nvs.commits;if(nvs.commitFail)return ESP_FAIL;if(nvs.corruptAfterCommit)nvs.bytes[8]^=1;return ESP_OK;}

namespace digivice::display {
Status current{};bool backlightReady=true,dmaQuiet=true;
unsigned lightCalls=0,lastPercent=99;esp_err_t lightError=ESP_OK;
bool quiescent(){return dmaQuiet;}
esp_err_t backlight(unsigned value){++lightCalls;lastPercent=value;return lightError;}
#define DIGIVICE_PANEL_ENABLED 1
#include "idle_hal.inc"
}
namespace {
using namespace digivice;
using idle::Request;
motion::ImuReading sample(std::uint64_t now,motion::Vector3 accel={0,0,1},motion::Vector3 gyro={}) {
    motion::ImuReading r;r.accelerationG=accel;r.gyroDps=gyro;r.observedAtMs=now;r.valid=true;r.calibrated=true;return r;
}
void timeouts(){
    idle::Controller c;CHECK(c.timeoutSeconds()==60&&!c.blanked());
    CHECK(c.tick(100,false)==Request::None);CHECK(c.tick(60099,false)==Request::None);
    CHECK(c.tick(60100,false)==Request::Prepare);CHECK(c.tick(60101,false)==Request::None&&!c.blanked());
    c.completePrepare(false,60102);CHECK(!c.blanked());CHECK(c.tick(120101,false)==Request::None);
    CHECK(c.tick(120102,false)==Request::Prepare);c.completePrepare(true,120102);CHECK(c.blanked());
    CHECK(c.tick(200000,false)==Request::None);c.activity(200001);CHECK(c.tick(200001,false)==Request::Wake);
    CHECK(c.tick(200002,false)==Request::None);c.completeWake(false,200002);
    CHECK(c.blanked()&&c.tick(201001,false)==Request::None);CHECK(c.tick(201002,false)==Request::Wake);
    c.completeWake(true,201002);CHECK(!c.blanked()&&c.tick(261001,false)==Request::None);
    CHECK(c.tick(261002,false)==Request::Prepare);c.activity(261003);c.completePrepare(true,261004);CHECK(!c.blanked());
    CHECK(c.tick(321003,true)==Request::None);CHECK(c.tick(381002,false)==Request::None);CHECK(c.tick(381003,false)==Request::Prepare);
    c.completePrepare(true,381003);CHECK(c.tick(381004,true)==Request::Wake);c.completeWake(true,381004);
    for(auto seconds:{0u,30u,60u,120u,300u}){
        idle::Controller d;CHECK(d.configure(seconds,10));
        CHECK(d.tick(10,false)==Request::None);
        CHECK(d.tick(10+std::uint64_t(seconds)*1000,false)==(seconds?Request::Prepare:Request::None));
    }
    CHECK(!c.configure(1,400000)&&c.timeoutSeconds()==60);
    idle::Controller off;CHECK(off.configure(30,0));CHECK(off.tick(30000,false)==Request::Prepare);off.completePrepare(true,30000);
    CHECK(off.configure(0,30001)&&off.tick(30001,false)==Request::Wake);off.completeWake(true,30001);CHECK(!off.blanked());
    // Backward clocks must restart observation, not underflow into an idle request.
    idle::Controller clock;CHECK(clock.configure(30,100));CHECK(clock.tick(99,false)==Request::None);
    CHECK(clock.tick(30098,false)==Request::None);CHECK(clock.tick(30099,false)==Request::Prepare);clock.completePrepare(true,30099);
    CHECK(clock.tick(1,false)==Request::Wake);clock.completeWake(true,1);CHECK(!clock.blanked());
}
void motionWake(){
    for(unsigned orientation=0;orientation<3;++orientation){
        idle::Controller c;CHECK(c.configure(30,0));
        motion::Vector3 g{};if(orientation==0)g.x=1;else if(orientation==1)g.y=1;else g.z=1;
        for(unsigned i=0;i<2000;++i){auto noise=g;noise.x+=(int(i%5)-2)*0.003f;noise.y+=(int(i%3)-1)*0.004f;
            CHECK(!c.observeMotion(sample(i*20,noise,{0.3f,-0.2f,0.2f}),i*20));}
        CHECK(c.tick(40000,false)==Request::Prepare);c.completePrepare(true,40000);
        CHECK(!c.observeMotion(sample(40020,g),40020));auto moved=g;moved.y+=0.2f;
        CHECK(!c.observeMotion(sample(40040,moved),40040));CHECK(!c.observeMotion(sample(40040,moved),40060));
        CHECK(c.tick(40060,false)==Request::None);CHECK(c.observeMotion(sample(40060,moved),40060));
        CHECK(c.tick(40060,false)==Request::Wake);c.completeWake(true,40060);CHECK(!c.blanked());
        CHECK(!c.observeMotion(sample(40080,moved),40080));CHECK(!c.observeMotion(sample(40100,moved),40100));
    }
    idle::Controller c;CHECK(c.configure(30,0));CHECK(!c.observeMotion(sample(10),10));
    CHECK(!c.observeMotion(sample(30,{0.3f,0,1}),30));CHECK(!c.observeMotion(sample(50),50)); // Isolated bump.
    CHECK(!c.observeMotion(sample(70,{0,0,1},{30,0,0}),70));CHECK(c.observeMotion(sample(90,{0,0,1},{30,0,0}),90));
    auto uncalibrated=sample(110,{0,0,1},{100,0,0});uncalibrated.calibrated=false;
    CHECK(!c.observeMotion(uncalibrated,110));uncalibrated.observedAtMs=130;CHECK(!c.observeMotion(uncalibrated,130));
    CHECK(!c.observeMotion(sample(150,{0.3f,0,1}),150));CHECK(!c.observeMotion(sample(400,{0.3f,0,1}),400)); // Gap reanchors.
    CHECK(!c.observeMotion(sample(420,{0.5f,0,1}),700)); // Stale never wakes.
    CHECK(!c.observeMotion(sample(721),720)); // Future sample never wakes.
    CHECK(!c.observeMotion(sample(740),740));
    auto invalid=sample(760);invalid.valid=false;CHECK(!c.observeMotion(invalid,760));
    invalid=sample(780);invalid.accelerationG.x=std::numeric_limits<float>::quiet_NaN();CHECK(!c.observeMotion(invalid,780));
    invalid=sample(800);invalid.gyroDps.x=std::numeric_limits<float>::infinity();CHECK(!c.observeMotion(invalid,800));
    CHECK(!c.observeMotion(sample(820),820));CHECK(!c.observeMotion(sample(840,{0.3f,0,1}),840));
    CHECK(!c.observeMotion(sample(830,{0.3f,0,1}),850)); // Reversed sensor clock reanchors.
}
void preferences(){
    for(auto seconds:{0u,30u,60u,120u,300u}){
        idle::SettingsRecord record;CHECK(idle::encodeSettings(seconds,record));unsigned decoded=7;
        CHECK(idle::decodeSettings(record,decoded)&&decoded==seconds);
        for(unsigned byte=0;byte<idle::kSettingsBytes;++byte)for(unsigned bit=0;bit<8;++bit){
            auto corrupt=record;corrupt.bytes[byte]^=1u<<bit;decoded=7;CHECK(!idle::decodeSettings(corrupt,decoded)&&decoded==7);
        }
    }
    idle::SettingsRecord untouched;CHECK(!idle::encodeSettings(1,untouched));
    nvs={};{idle::NvsSettings s;CHECK(s.begin()==ESP_OK&&s.seconds()==60&&s.writable());CHECK(!nvs.sets&&!nvs.commits);
        CHECK(s.setSeconds(60)==ESP_OK&&!nvs.sets);CHECK(s.setSeconds(1)==ESP_ERR_INVALID_ARG&&!nvs.sets&&s.writable());
        CHECK(s.setSeconds(120)==ESP_OK&&s.seconds()==120&&nvs.sets==1&&nvs.commits==1);
        CHECK(s.setSeconds(120)==ESP_OK&&nvs.sets==1);CHECK(s.begin()==ESP_OK&&nvs.opens==1);}
    CHECK(nvs.closes==1);{idle::NvsSettings reboot;CHECK(reboot.begin()==ESP_OK&&reboot.seconds()==120&&nvs.sets==1);}
    nvs={};nvs.openError=ESP_FAIL;{idle::NvsSettings s;CHECK(s.begin()==ESP_FAIL&&!s.writable());CHECK(s.begin()==ESP_FAIL&&nvs.opens==1);}
    for(unsigned size:{0u,1u,15u,17u,4096u}){nvs={};nvs.present=true;nvs.bytes.resize(size);idle::NvsSettings s;CHECK(s.begin()==ESP_ERR_INVALID_SIZE&&!s.writable()&&s.seconds()==60&&!nvs.sets);}
    nvs={};nvs.present=true;nvs.bytes.resize(16);{idle::NvsSettings s;CHECK(s.begin()==ESP_ERR_INVALID_RESPONSE&&!s.writable());CHECK(s.setSeconds(30)==ESP_ERR_INVALID_STATE&&!nvs.sets);}
    for(unsigned fault=0;fault<4;++fault){
        nvs={};{idle::NvsSettings s;CHECK(s.begin()==ESP_OK);
            nvs.writeFail=fault==0;nvs.commitFail=fault==1;nvs.readFail=fault==2;nvs.corruptAfterCommit=fault==3;
            CHECK(s.setSeconds(30)!=ESP_OK&&!s.writable()&&s.seconds()==60);const auto writes=nvs.sets;
            CHECK(s.setSeconds(30)==ESP_ERR_INVALID_STATE&&nvs.sets==writes);}
        if(fault==1){nvs.commitFail=false;idle::NvsSettings reboot;CHECK(reboot.begin()==ESP_OK&&reboot.seconds()==30);} // Commit may have landed despite lost ACK.
    }
}
void backlightHal(){
    using namespace display;current={};current.display=current.touch=ESP_OK;backlightReady=dmaQuiet=true;lightCalls=0;lightError=ESP_OK;
    CHECK(setIdleBlank(true)==ESP_OK&&current.idleBlanked&&!current.suspended&&current.touch==ESP_OK&&lastPercent==0);
    CHECK(setIdleBlank(true)==ESP_OK&&lightCalls==1);CHECK(setIdleBlank(false)==ESP_OK&&!current.idleBlanked&&lastPercent==40);
    dmaQuiet=false;CHECK(setIdleBlank(true)==ESP_ERR_TIMEOUT&&!current.idleBlanked&&lightCalls==2);dmaQuiet=true;
    lightError=ESP_FAIL;CHECK(setIdleBlank(true)==ESP_FAIL&&!current.idleBlanked);lightError=ESP_OK;
    CHECK(setIdleBlank(true)==ESP_OK);lightError=ESP_FAIL;CHECK(setIdleBlank(false)==ESP_FAIL&&current.idleBlanked);lightError=ESP_OK;
    current.suspended=true;CHECK(setIdleBlank(false)==ESP_ERR_INVALID_STATE&&current.idleBlanked);current.suspended=false;
    backlightReady=false;CHECK(setIdleBlank(false)==ESP_ERR_INVALID_STATE&&current.idleBlanked);backlightReady=true;
    CHECK(setIdleBlank(false)==ESP_OK&&!current.idleBlanked);
}
}
int main(){timeouts();motionWake();preferences();backlightHal();std::printf("Idle policy/settings/backlight: %u checks passed; Controller%zuB, record%zuB, NvsSettings%zuB; host doubles, no physical sleep/current claim\n",checks,sizeof(digivice::idle::Controller),sizeof(digivice::idle::SettingsRecord),sizeof(digivice::idle::NvsSettings));}

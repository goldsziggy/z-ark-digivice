#include "device_audio.hpp"
#include "buzzer_patterns.hpp"
#include "board_hal.hpp"
#include "audio_test_nvs.hpp"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <vector>
using namespace digivice;
namespace {
unsigned checks=0,io=0,failAt=0,lastIO=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);std::exit(1);}}while(false)
bool locked=false,busy=false,createFail=false,corrupt=false;
std::uint8_t regs[4]{},allowedMask=0x80;
std::uint64_t now=100,finish=0;
std::vector<std::pair<std::uint64_t,bool>> edges;
std::function<void()> progress;
struct Queue{unsigned capacity;std::size_t size;std::deque<std::vector<unsigned char>> items;};
std::unique_ptr<Queue> queue;
TaskFunction_t worker=nullptr;void* context=nullptr;
struct Stop{};
void fresh(){io=failAt=lastIO=0;locked=busy=createFail=corrupt=false;now=100;finish=0;edges.clear();progress={};regs[1]=0x57;regs[3]=0xff;allowedMask=0x80;queue.reset();worker=nullptr;context=nullptr;audioTestNvs={};audioTestRhythmNvs={};}
void run(unsigned duration){CHECK(worker);finish=now+duration;lastIO=io;try{worker(context);}catch(const Stop&){}CHECK(now>=finish&&!locked);}
void noBus(const std::function<void()>& fn){const auto before=io;fn();CHECK(io==before);}
}
std::int64_t esp_timer_get_time(){return now*1000;}
esp_err_t gpio_set_level(gpio_num_t,std::uint32_t){CHECK(false);return ESP_FAIL;}
esp_err_t gpio_config(const gpio_config_t*){CHECK(false);return ESP_FAIL;}
int gpio_get_level(gpio_num_t){CHECK(false);return 0;}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* p){return p;}
int xSemaphoreTake(SemaphoreHandle_t,TickType_t timeout){CHECK(!locked&&timeout==(allowedMask==0x80?0u:10u));if(busy)return pdFALSE;locked=true;return pdTRUE;}
int xSemaphoreGive(SemaphoreHandle_t){CHECK(locked);locked=false;return pdTRUE;}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* c,i2c_master_bus_handle_t* out){CHECK(c->scl_io_num==7&&c->sda_io_num==15);*out=reinterpret_cast<i2c_master_bus_handle_t>(1);return ESP_OK;}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t* c,i2c_master_dev_handle_t* out){CHECK(locked&&c->device_address==0x20&&c->scl_speed_hz==100000);*out=reinterpret_cast<i2c_master_dev_handle_t>(2);return ESP_OK;}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,const std::uint8_t* reg,std::size_t n,std::uint8_t* data,std::size_t size,int timeout){CHECK(locked&&n==1&&size==1&&timeout==(allowedMask==0x80?10:20));CHECK(*reg==1||*reg==3);if(++io==failAt)return ESP_FAIL;*data=regs[*reg];if(corrupt&&*reg==1)*data^=0x80;return ESP_OK;}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const std::uint8_t* data,std::size_t n,int timeout){CHECK(locked&&n==2&&timeout==(allowedMask==0x80?10:20));CHECK(data[0]==1||data[0]==3);CHECK((data[1]&~allowedMask)==(regs[data[0]]&~allowedMask));if(++io==failAt)return ESP_FAIL;if(data[0]==1&&((data[1]^regs[1])&0x80))edges.emplace_back(now,(data[1]&0x80)!=0);regs[data[0]]=data[1];return ESP_OK;}
BaseType_t xTaskCreate(TaskFunction_t fn,const char*,std::uint32_t stack,void* owner,UBaseType_t priority,TaskHandle_t* task){CHECK(stack==3072&&priority==1);if(createFail)return pdFALSE;worker=fn;context=owner;*task=reinterpret_cast<void*>(1);return pdPASS;}
void vTaskDelay(TickType_t ticks){CHECK(ticks==2);CHECK(io-lastIO<=6);now+=20;if(progress)progress();lastIO=io;if(now>=finish)throw Stop{};}
QueueHandle_t xQueueCreate(unsigned capacity,std::size_t size){CHECK(capacity==4);queue=std::make_unique<Queue>(Queue{capacity,size,{}});return queue.get();}
void vQueueDelete(QueueHandle_t q){CHECK(q==queue.get());queue.reset();}
unsigned uxQueueMessagesWaiting(QueueHandle_t q){CHECK(q==queue.get());return queue->items.size();}
BaseType_t xQueueReset(QueueHandle_t q){CHECK(q==queue.get());queue->items.clear();return pdTRUE;}
BaseType_t xQueueSend(QueueHandle_t q,const void* p,TickType_t wait){CHECK(q==queue.get()&&wait==0);if(queue->items.size()>=queue->capacity)return pdFALSE;const auto* b=static_cast<const unsigned char*>(p);queue->items.emplace_back(b,b+queue->size);return pdTRUE;}
BaseType_t xQueueReceive(QueueHandle_t q,void* p,TickType_t wait){CHECK(q==queue.get()&&wait==0);if(queue->items.empty())return pdFALSE;std::memcpy(p,queue->items.front().data(),queue->size);queue->items.pop_front();return pdTRUE;}
int main(){
 using namespace device;
 fresh();board::initialize();CHECK(board::selectedProfile().buzzerExpanderMask==0x80);
 for(unsigned output=0;output<256;++output)for(unsigned dir=0;dir<256;++dir)for(bool on:{false,true}){
  regs[1]=output;regs[3]=dir;CHECK(board::setBuzzer(on)==ESP_OK);
  CHECK(regs[1]==((output&0x7f)|(on?0x80:0))&&regs[3]==(dir&0x7f)&&!locked);
 }
 for(unsigned fault=1;fault<=6;++fault)for(bool on:{false,true}){
  fresh();regs[1]=0xa5;regs[3]=0xfa;failAt=fault;CHECK(board::setBuzzer(on)==ESP_FAIL);
  CHECK((regs[1]&0x7f)==0x25&&(regs[3]&0x7f)==0x7a&&!locked);
 }
 fresh();busy=true;CHECK(board::setBuzzer(true)==ESP_ERR_NOT_FINISHED&&io==0);busy=false;corrupt=true;CHECK(board::setBuzzer(false)!=ESP_OK&&!locked);
 std::set<std::vector<unsigned>> rhythms;
 for(auto cue:{AudioCue::DigiCritical,AudioCue::DigiPower,AudioCue::DigiGuard,AudioCue::DigiRecovery,AudioCue::DigiElement,AudioCue::DigiCriticalHit}){
  fresh();Audio a;CHECK(a.begin()==ESP_OK&&a.buzzerOnly()&&a.volume()==100&&!a.musicEnabled());CHECK(io==0);CHECK(a.play(cue));CHECK(!a.play(cue));run(1000);
  CHECK(a.ready()&&a.quiescent()&&a.effectsQuiescent()&&!(regs[1]&0x80)&&!(regs[3]&0x80));
  const auto r=buzzerRhythm(cue);CHECK(r.duration()<=600&&edges.size()==r.count+1u);
  std::vector<unsigned> signature;
  for(unsigned i=0;i<r.count;++i){CHECK(edges[i].second==(i%2==0));CHECK(edges[i+1].first-edges[i].first==r.ms[i]);signature.push_back(r.ms[i]);}
  rhythms.insert(signature);
 }
 CHECK(rhythms.size()==6);
 {fresh();Audio a;CHECK(a.begin()==ESP_OK);CHECK(a.setMusicEnabled(false)==ESP_OK&&!a.musicEnabled()&&!audioTestNvs.sets&&!audioTestRhythmNvs.sets);CHECK(a.setVolume(1)==ESP_OK&&a.volume()==100);CHECK(a.play(AudioCue::DigiCritical));unsigned stage=0;
  progress=[&]{if(stage==0&&now>=160){CHECK(regs[1]&0x80);noBus([&]{audioTestNvs.commitFail=true;CHECK(a.setMuted(true)!=ESP_OK);});CHECK(!a.quiescent());stage=1;}
   else if(stage==1&&now>=200){CHECK(a.quiescent()&&!(regs[1]&0x80)&&!a.play(AudioCue::DigiPower));noBus([&]{a.setMuted(false);a.setVolume(0);});CHECK(!a.play(AudioCue::DigiPower));stage=2;}
   else if(stage==2&&now>=260){noBus([&]{a.setVolume(5);});CHECK(a.volume()==100&&a.play(AudioCue::DigiPower));stage=3;}
   else if(stage==3&&now>=300){CHECK(regs[1]&0x80);noBus([&]{a.setScreenBlanked(true);});stage=4;}
   else if(stage==4&&now>=360){CHECK(a.quiescent()&&!a.play(AudioCue::DigiGuard));noBus([&]{a.setScreenBlanked(false);});CHECK(a.play(AudioCue::DigiGuard));stage=5;}
   else if(stage==5&&now>=400){noBus([&]{a.pause(true);});CHECK(!a.quiescent());stage=6;}
   else if(stage==6&&now>=460){CHECK(a.quiescent()&&!(regs[1]&0x80));a.pause(false);stage=7;}};
  run(900);CHECK(stage==7&&a.quiescent());}
 {fresh();Audio a;CHECK(a.begin()==ESP_OK&&a.play(AudioCue::DigiPower));unsigned stage=0;
  progress=[&]{if(stage==0&&now>=160){busy=true;a.pause(true);stage=1;}
   else if(stage==1&&now>=440){CHECK(!a.quiescent()&&(regs[1]&0x80));busy=false;stage=2;}
   else if(stage==2&&now>=500){CHECK(a.quiescent()&&!(regs[1]&0x80));a.pause(false);stage=3;}};
  run(800);CHECK(stage==3&&edges.size()==2&&a.quiescent());}
 for(unsigned failure=1;failure<=6;++failure){fresh();Audio a;CHECK(a.begin()==ESP_OK);unsigned stage=0;
  progress=[&]{if(stage==0&&now>=140){failAt=io+failure;CHECK(a.play(AudioCue::DigiPower));stage=1;}
   else if(stage==1&&now>=240){CHECK(!a.ready()&&!a.quiescent());stage=2;}
   else if(stage==2&&now>=400){CHECK(a.quiescent()&&!(regs[1]&0x80)&&!a.play(AudioCue::DigiCritical));stage=3;}};
  run(600);CHECK(stage==3&&a.lastError()==ESP_FAIL);}
 {fresh();Audio a;CHECK(a.begin()==ESP_OK&&a.play(AudioCue::DigiPower));unsigned stage=0;
  progress=[&]{if(stage==0&&now>=160){corrupt=true;a.pause(true);stage=1;}
   else if(stage==1&&now>=500){CHECK(!a.quiescent()&&!a.ready());corrupt=false;stage=2;}
   else if(stage==2&&now>=700){CHECK(a.quiescent()&&!(regs[1]&0x80));stage=3;}};
  run(900);CHECK(stage==3);}
 {fresh();busy=true;Audio a;CHECK(a.begin()==ESP_OK&&a.play(AudioCue::DigiElement));progress=[&]{if(now>=700)busy=false;};run(1200);CHECK(edges.empty()&&a.quiescent());}
 {fresh();Audio a;CHECK(a.begin()==ESP_OK&&a.play(AudioCue::DigiPower));unsigned stage=0;
  progress=[&]{if(stage==0&&now>=160){CHECK(regs[1]&0x80);allowedMask=2;CHECK(board::setPeripheralReset(board::PeripheralReset::Touch,true)==ESP_OK);allowedMask=0x80;CHECK((regs[1]&0x82)==0x80);stage=1;}
   else if(stage==1&&now>=600){CHECK(a.quiescent()&&!(regs[1]&0x82));allowedMask=2;CHECK(board::setPeripheralReset(board::PeripheralReset::Touch,false)==ESP_OK);allowedMask=0x80;CHECK(regs[1]&2);stage=2;}};
  run(800);CHECK(stage==2);}
 {fresh();Audio a;CHECK(a.begin()==ESP_OK&&a.play(AudioCue::DigiPower));unsigned stage=0;
  progress=[&]{if(stage==0&&now>=160){CHECK(regs[1]&0x80);now=80;stage=1;}else if(stage==1&&now>=200){CHECK(a.quiescent()&&!(regs[1]&0x80));stage=2;}};
  run(700);CHECK(stage==2&&edges.size()==2);}
 {fresh();createFail=true;Audio a;CHECK(a.begin()==ESP_ERR_NO_MEM&&io==0&&!queue&&a.quiescent());}
 // An old PCM music preference cannot opt the user into background buzzing.
 {fresh();sound::SettingsRecord record;CHECK(sound::encodeSettings({100,false,true},record));
  audioTestNvs.present=true;audioTestNvs.bytes.assign(record.bytes,record.bytes+sound::kSettingsBytes);
  Audio a;CHECK(a.begin()==ESP_OK&&!a.musicEnabled());run(1000);CHECK(edges.empty());
  CHECK(!audioTestNvs.sets&&!audioTestRhythmNvs.sets);
  noBus([&]{CHECK(a.setMusicEnabled(true)==ESP_OK);});
  CHECK(a.musicEnabled()&&!audioTestNvs.sets&&audioTestRhythmNvs.commits==1);
  CHECK(a.setMuted(true)==ESP_OK);sound::Preferences decoded;
  std::memcpy(record.bytes,audioTestNvs.bytes.data(),sizeof(record.bytes));
  CHECK(sound::decodeSettings(record,decoded)&&decoded.music&&decoded.muted);
 }
 // Actual worker: exact sparse grids, bounded background duty, no I2S/GPIO,
 // and background playback must never block normal screen-idle eligibility.
 for(auto scene:{MusicScene::Home,MusicScene::Battle}) {
  fresh();Audio a;CHECK(a.begin()==ESP_OK);a.setMusicScene(scene);CHECK(a.setMusicEnabled(true)==ESP_OK);
  progress=[&]{CHECK(a.effectsQuiescent());};run(64000);
  const auto d=a.buzzerDiagnostics();const unsigned motifs=scene==MusicScene::Home?3:4;
  CHECK(d.backgroundStarted==motifs&&d.started==0&&d.onWrites==motifs*4);
  unsigned onTotal=0;
  for(std::size_t i=0;i+1<edges.size();i+=2) {
   CHECK(edges[i].second&&!edges[i+1].second);
   const auto width=edges[i+1].first-edges[i].first;CHECK(width<=80);onTotal+=width;
   if(i>=8)CHECK(edges[i].first-edges[i-8].first==(scene==MusicScene::Home?30000u:20000u));
  }
  CHECK(onTotal<=motifs*(scene==MusicScene::Home?220u:240u));
 }
 {fresh();Audio a;CHECK(a.begin()==ESP_OK&&a.setMusicEnabled(true)==ESP_OK);unsigned stage=0;
  progress=[&]{
   if(stage==0&&now>=140){CHECK(regs[1]&0x80);CHECK(a.effectsQuiescent());CHECK(a.play(AudioCue::DigiPower));stage=1;}
   else if(stage==1&&now>=200){CHECK(!a.effectsQuiescent()&&a.buzzerDiagnostics().started==1);a.setScreenBlanked(true);stage=2;}
   else if(stage==2&&now>=260){CHECK(a.quiescent()&&!(regs[1]&0x80));CHECK(!a.play(AudioCue::Boot)&&a.lastPlayResult()==PlayResult::Paused);a.setScreenBlanked(false);stage=3;}
   else if(stage==3&&now>=400){CHECK(a.quiescent());a.setMusicScene(MusicScene::Quiet);stage=4;}
  };run(1000);CHECK(stage==4&&a.buzzerDiagnostics().backgroundStarted==1);
 }
 // A failure saving OFF still cancels background sound in RAM; saved ON only
 // reappears at reboot. The separate record cannot corrupt PCM preferences.
 {fresh();Audio a;CHECK(a.begin()==ESP_OK&&a.setMusicEnabled(true)==ESP_OK);unsigned stage=0;
  progress=[&]{if(stage==0&&now>=140){CHECK(regs[1]&0x80);audioTestRhythmNvs.commitFail=true;
    noBus([&]{CHECK(a.setMusicEnabled(false)==ESP_FAIL);});CHECK(!a.musicEnabled()&&!a.preferencesWritable());stage=1;}
   else if(stage==1&&now>=200){CHECK(a.quiescent()&&!(regs[1]&0x80));stage=2;}};
  run(1000);CHECK(stage==2&&!audioTestNvs.sets&&a.preferencesError()==ESP_FAIL);
 }
 {fresh();audioTestRhythmNvs.present=true;audioTestRhythmNvs.bytes.resize(16);
  Audio a;CHECK(a.begin()==ESP_OK&&!a.musicEnabled()&&!a.preferencesWritable());
  CHECK(a.preferencesError()==ESP_ERR_INVALID_RESPONSE&&a.play(AudioCue::Boot));run(500);
  CHECK(a.buzzerDiagnostics().started==1&&a.buzzerDiagnostics().onWrites==2&&!audioTestRhythmNvs.sets);
 }
 {fresh();{Audio a;CHECK(a.begin()==ESP_OK&&a.setMusicEnabled(true)==ESP_OK);}
  Audio rebooted;CHECK(rebooted.begin()==ESP_OK&&rebooted.musicEnabled());run(2200);
  CHECK(rebooted.buzzerDiagnostics().backgroundStarted==1&&audioTestRhythmNvs.commits==1&&!audioTestNvs.sets);
 }
 {BuzzerBackground bg;unsigned starts=0;
  for(std::uint64_t t=0;t<60000;t+=20) {
   // Scene/enable churn cannot turn a sparse background into continuous beeps.
   starts+=bg.update(t%80<40?MusicScene::Home:MusicScene::Battle,t%60!=0,false,true,t);
  }CHECK(starts<=3);CHECK(!bg.update(MusicScene::Quiet,true,false,true,60000)&&!bg.on(60000));
 }
 {fresh();Audio a;CHECK(a.begin()==ESP_OK);CHECK(a.setMuted(true)==ESP_OK);
  CHECK(!a.play(AudioCue::Boot)&&a.lastPlayResult()==PlayResult::Muted);
  CHECK(a.setMuted(false)==ESP_OK&&a.setVolume(0)==ESP_OK);
  CHECK(!a.play(AudioCue::Boot)&&a.lastPlayResult()==PlayResult::Off);
  CHECK(a.setVolume(100)==ESP_OK&&a.play(AudioCue::Boot)&&a.lastPlayResult()==PlayResult::Queued);
  CHECK(!a.play(AudioCue::Boot)&&a.lastPlayResult()==PlayResult::Cooldown);
  run(500);const auto d=a.buzzerDiagnostics();CHECK(d.queued==1&&d.started==1&&d.onWrites==2&&d.offWrites==3&&d.outputKnown&&!d.outputOn);
 }
 std::printf("PASS %u buzzer checks; actual Audio worker and board HAL, exhaustive output/direction preservation, six distinct rhythms, pause/mute/zero-volume/idle, contention, partial-write and readback faults; no I2S or GPIO calls.\n",checks);
}

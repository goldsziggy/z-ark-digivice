#include "device_audio.hpp"
#include "board_hal.hpp"
#include "audio_test_nvs.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace {
unsigned checks=0;
void check(bool value,const char* text,int line){++checks;if(!value){std::fprintf(stderr,"%d: %s\n",line,text);std::exit(1);}}
#define CHECK(x) check((x),#x,__LINE__)
struct Stop {};
struct Queue {unsigned capacity;std::size_t size;std::deque<std::vector<unsigned char>> requests;};
std::unique_ptr<Queue> queue;
TaskFunction_t worker=nullptr;void* owner=nullptr;
std::uint64_t now=100,finish=0;
std::function<void()> progress;
bool enabled=false,disableFail=false,writeFail=false,createFail=false,enableFail=false,preloadFail=false,shortPreload=false;
unsigned preloaded=0,enables=0,disables=0,writes=0,nonzeroWrites=0,peak=0;
digivice::board::BoardProfile fakeBoard{digivice::board::ProfileId::Waveshare146};
void fresh() {
    queue.reset();worker=nullptr;owner=nullptr;now=100;finish=0;progress={};
    enabled=disableFail=writeFail=createFail=enableFail=preloadFail=shortPreload=false;
    preloaded=enables=disables=writes=nonzeroWrites=peak=0;
    audioTestNvs={};
}
void advance(unsigned milliseconds) {
    now+=milliseconds;if(progress)progress();if(now>=finish)throw Stop{};
}
void run(unsigned milliseconds) {
    CHECK(worker);finish=now+milliseconds;try{worker(owner);}catch(const Stop&){}
    CHECK(now>=finish);
}
}
namespace digivice::board {const BoardProfile& selectedProfile(){return fakeBoard;}}
std::int64_t esp_timer_get_time(){return static_cast<std::int64_t>(now*1000);}
BaseType_t xTaskCreate(TaskFunction_t fn,const char*,std::uint32_t stack,void* context,UBaseType_t priority,TaskHandle_t* task) {
    CHECK(stack==4096&&priority==3);if(createFail)return pdFALSE;
    worker=fn;owner=context;*task=reinterpret_cast<void*>(1);return pdPASS;
}
void vTaskDelay(TickType_t ticks){CHECK(ticks>0);advance(ticks*10);}
QueueHandle_t xQueueCreate(unsigned capacity,std::size_t size){CHECK(capacity==4);queue=std::make_unique<Queue>(Queue{capacity,size,{}});return queue.get();}
void vQueueDelete(QueueHandle_t value){CHECK(value==queue.get());queue.reset();}
unsigned uxQueueMessagesWaiting(QueueHandle_t value){CHECK(value==queue.get());return queue->requests.size();}
BaseType_t xQueueReset(QueueHandle_t value){CHECK(value==queue.get());queue->requests.clear();return pdTRUE;}
BaseType_t xQueueSend(QueueHandle_t value,const void* source,TickType_t wait){
    CHECK(value==queue.get()&&wait==0);
    if(queue->requests.size()>=queue->capacity)return pdFALSE;
    const auto* bytes=static_cast<const unsigned char*>(source);queue->requests.emplace_back(bytes,bytes+queue->size);return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t value,void* target,TickType_t wait){
    CHECK(value==queue.get()&&(wait==0||wait==2));
    if(queue->requests.empty()){if(wait)advance(wait*10);return pdFALSE;}
    std::memcpy(target,queue->requests.front().data(),queue->size);queue->requests.pop_front();return pdTRUE;
}
esp_err_t i2s_new_channel(const i2s_chan_config_t* config,i2s_chan_handle_t* tx,i2s_chan_handle_t* rx) {
    CHECK(config->dma_desc_num==3&&config->dma_frame_num==256&&config->auto_clear&&rx==nullptr);
    *tx=reinterpret_cast<void*>(1);return ESP_OK;
}
esp_err_t i2s_del_channel(i2s_chan_handle_t channel){CHECK(channel==reinterpret_cast<void*>(1));return ESP_OK;}
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t* config) {
    CHECK(config->clk_cfg.sample_rate_hz==44100);CHECK(config->slot_cfg.data_bit_width==16&&config->slot_cfg.slot_mode==2);
    CHECK(config->gpio_cfg.mclk==-1&&config->gpio_cfg.bclk==48&&config->gpio_cfg.ws==38&&config->gpio_cfg.dout==47&&config->gpio_cfg.din==-1);
    return ESP_OK;
}
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t,const void* buffer,std::size_t bytes,std::size_t* loaded) {
    CHECK(!enabled&&bytes==1024);const auto* samples=static_cast<const std::int16_t*>(buffer);
    CHECK(std::all_of(samples,samples+bytes/2,[](auto value){return value==0;}));
    *loaded=shortPreload?bytes/2:bytes;++preloaded;return preloadFail?ESP_FAIL:ESP_OK;
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t) {
    CHECK(!enabled&&preloaded==3);preloaded=0;++enables;if(enableFail)return ESP_FAIL;enabled=true;return ESP_OK;
}
esp_err_t i2s_channel_disable(i2s_chan_handle_t) {
    CHECK(enabled);++disables;if(disableFail)return ESP_FAIL;enabled=false;return ESP_OK;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t,const void* buffer,std::size_t bytes,std::size_t* written,unsigned timeout) {
    CHECK(enabled&&bytes==1024&&timeout==30);++writes;
    const auto* samples=static_cast<const std::int16_t*>(buffer);bool nonzero=false;
    for(unsigned frame=0;frame<256;++frame){CHECK(samples[frame*2]==samples[frame*2+1]);
        const auto magnitude=static_cast<unsigned>(std::abs(samples[frame*2]));peak=std::max(peak,magnitude);nonzero|=magnitude!=0;}
    nonzeroWrites+=nonzero;*written=bytes;advance(6);return writeFail?ESP_FAIL:ESP_OK;
}

int main() {
    using namespace digivice::device;
    {
        fresh();Audio audio;CHECK(audio.begin()==ESP_OK);CHECK(audio.ready()&&audio.volume()==15&&!audio.muted()&&!audio.musicEnabled());
        CHECK(audio.preferencesWritable()&&!audioTestNvs.sets);CHECK(audio.play(AudioCue::Boot));unsigned stage=0;
        progress=[&] {
            if(stage==0&&now>=700){CHECK(audio.quiescent()&&audio.effectsQuiescent());CHECK(audio.setMusicEnabled(true)==ESP_OK);stage=1;}
            else if(stage==1&&now>=1100){CHECK(!audio.quiescent()&&audio.effectsQuiescent());CHECK(audio.play(AudioCue::Navigate));CHECK(!audio.effectsQuiescent());stage=2;}
            else if(stage==2&&now>=1500){CHECK(audio.effectsQuiescent()&&!audio.quiescent());audio.setMusicScene(MusicScene::Battle);stage=3;}
            else if(stage==3&&now>=1900){audio.setMusicScene(MusicScene::Quiet);CHECK(audioTestNvs.commits==1);stage=4;}
            else if(stage==4&&now>=2200){CHECK(audio.quiescent());CHECK(audio.play(AudioCue::Navigate));stage=5;}
            else if(stage==5&&now>=2500){CHECK(audio.quiescent());audio.setMusicScene(MusicScene::Home);stage=6;}
            else if(stage==6&&now>=2850){CHECK(!audio.quiescent());audio.pause(true);CHECK(!audio.quiescent());stage=7;}
            else if(stage==7&&now>=3200){CHECK(audio.quiescent());CHECK(audioTestNvs.commits==1);audio.pause(false);CHECK(audio.play(AudioCue::CaptureSuccess));stage=8;}
            else if(stage==8&&now>=3250){CHECK(!audio.effectsQuiescent());CHECK(audio.setMuted(true)==ESP_OK);CHECK(!audio.quiescent());stage=9;}
            else if(stage==9&&now>=3400){CHECK(audio.quiescent()&&audio.effectsQuiescent());CHECK(!audio.play(AudioCue::Win));CHECK(audio.setMuted(false)==ESP_OK);stage=10;}
            else if(stage==10&&now>=3800){CHECK(!audio.quiescent()&&audio.effectsQuiescent());CHECK(audio.setMusicEnabled(false)==ESP_OK);stage=11;}
            else if(stage==11&&now>=4050){CHECK(audio.quiescent()&&audio.effectsQuiescent());stage=12;}
        };
        run(4300);CHECK(stage==12&&audioTestNvs.commits==4&&nonzeroWrites>40&&peak>100&&peak<4000);
        CHECK(enables>=4&&disables>=4);CHECK(audio.ready());
    }
    {
        fresh();Audio audio;CHECK(audio.begin()==ESP_OK);CHECK(audio.setVolume(255)==ESP_OK&&audio.volume()==100);
        CHECK(audio.setVolume(100)==ESP_OK&&audioTestNvs.commits==1);
        CHECK(audio.setMusicEnabled(true)==ESP_OK);unsigned stage=0;
        progress=[&] {
            if(stage==0&&now>=400){CHECK(audio.setVolume(0)==ESP_OK);stage=1;}
            else if(stage==1&&now>=500){CHECK(audio.quiescent()&&!enabled);CHECK(audio.setVolume(5)==ESP_OK);stage=2;}
            else if(stage==2&&now>=800){CHECK(!audio.quiescent());audioTestNvs.commitFail=true;CHECK(audio.setMuted(true)!=ESP_OK);CHECK(audio.muted()&&!audio.preferencesWritable());stage=3;}
            else if(stage==3&&now>=900){CHECK(audio.quiescent()&&!enabled);const auto savedWrites=audioTestNvs.sets;CHECK(audio.setMuted(false)!=ESP_OK);CHECK(!audio.muted()&&audioTestNvs.sets==savedWrites);stage=4;}
            else if(stage==4&&now>=1100){CHECK(!audio.quiescent());audio.pause(true);stage=5;}
        };
        run(1200);CHECK(stage==5&&audio.quiescent());
    }
    {
        fresh();Audio audio;CHECK(audio.begin()==ESP_OK);CHECK(audio.setMusicEnabled(true)==ESP_OK);unsigned stage=0;
        progress=[&] {
            if(stage==0&&now>=400){disableFail=true;audio.pause(true);stage=1;}
            else if(stage==1&&now>=500){CHECK(!audio.quiescent()&&enabled&&!audio.ready());disableFail=false;stage=2;}
            else if(stage==2&&now>=600){CHECK(audio.quiescent()&&!enabled);stage=3;}
        };
        run(700);CHECK(stage==3&&audio.lastError()==ESP_FAIL);
    }
    {
        fresh();Audio audio;CHECK(audio.begin()==ESP_OK);CHECK(audio.setMusicEnabled(true)==ESP_OK);unsigned stage=0;
        progress=[&] {
            if(stage==0&&now>=400){writeFail=true;stage=1;}
            else if(stage==1&&now>=500){CHECK(!audio.ready()&&audio.quiescent()&&!enabled);CHECK(!audio.play(AudioCue::Win));stage=2;}
        };
        run(700);CHECK(stage==2&&audio.lastError()==ESP_FAIL);
    }
    {
        fresh();Audio audio;CHECK(audio.begin()==ESP_OK);
        CHECK(audio.play(AudioCue::Feed));CHECK(!audio.play(AudioCue::Feed));CHECK(audio.play(AudioCue::Play));
        CHECK(audio.play(AudioCue::Rest));CHECK(audio.play(AudioCue::Attack));CHECK(!audio.play(AudioCue::Magic));
        CHECK(audio.play(AudioCue::Win));CHECK(uxQueueMessagesWaiting(queue.get())==1);run(1100);CHECK(audio.quiescent());
    }
    {
        fresh();audioTestNvs.present=true;audioTestNvs.bytes.resize(16);Audio audio;
        CHECK(audio.begin()==ESP_OK&&audio.ready());CHECK(!audio.preferencesWritable()&&audio.preferencesError()==ESP_ERR_INVALID_RESPONSE);
        CHECK(audio.volume()==15&&!audio.musicEnabled());CHECK(audio.setMuted(true)!=ESP_OK&&audio.muted()&&!audioTestNvs.sets);
    }
    {
        fresh();createFail=true;Audio audio;CHECK(audio.begin()==ESP_ERR_NO_MEM);CHECK(!audio.ready()&&audio.quiescent()&&!queue);
    }
    for(unsigned fault=0;fault<3;++fault) {
        fresh();Audio audio;CHECK(audio.begin()==ESP_OK);CHECK(audio.play(AudioCue::Boot));
        enableFail=fault==0;preloadFail=fault==1;shortPreload=fault==2;run(200);
        CHECK(!audio.ready()&&audio.quiescent()&&!enabled&&!writes);
        CHECK(audio.lastError()==(fault==2?ESP_ERR_INVALID_SIZE:ESP_FAIL));
        CHECK(preloaded<=3&&enables<=1); // No retry loop after a failed startup.
    }
    for(unsigned volume:{0,5,15,30,50,75,100}) {
        fresh();Audio audio;CHECK(audio.begin()==ESP_OK);CHECK(audio.setVolume(volume)==ESP_OK);
        CHECK(!audio.musicEnabled());CHECK(audio.play(AudioCue::Navigate)==(volume!=0));
        run(400);CHECK(audio.quiescent());CHECK((nonzeroWrites!=0)==(volume!=0));
        CHECK(peak<23200&&audio.ready());
    }
    std::printf("Actual audio worker: %u checks passed; Audio=%zu B; fixed256-frame stereo buffer1024 B +3 DMA descriptors3072 B +4096 B task stack; RTOS/NVS/I2S doubles only\n",checks,sizeof(Audio));
}

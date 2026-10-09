#include "device_imu.hpp"
#include "board_hal.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}} while(0)
struct Stop {};
std::uint64_t now=0,finish=0;
unsigned critical=0,busDepth=0,reads=0,writes=0;
bool busy=false,ioFail=false,stopFail=false,createFail=false;
TaskFunction_t worker=nullptr;void* context=nullptr;
std::function<void()> onDelay,onBurst;
std::array<std::uint8_t,256> registers{};
digivice::board::BoardProfile fakeBoard{digivice::board::ProfileId::Waveshare146};
void fresh() {
    now=finish=0;critical=busDepth=reads=writes=0;
    busy=ioFail=stopFail=createFail=false;worker=nullptr;context=nullptr;
    onDelay={};onBurst={};registers.fill(0);registers[0]=5;registers[1]=0x7c;
}
void run(unsigned duration) {
    finish=now+duration;
    try {worker(context);} catch(const Stop&) {}
    CHECK(now>=finish);CHECK(critical==0);CHECK(busDepth==0);
}
}
void fakeEnter(portMUX_TYPE*) {CHECK(critical==0);++critical;}
void fakeExit(portMUX_TYPE*) {CHECK(critical==1);--critical;}
std::int64_t esp_timer_get_time(){return static_cast<std::int64_t>(now*1000);}
BaseType_t xTaskCreate(TaskFunction_t entry,const char*,std::uint32_t stack,void* arg,
                       UBaseType_t priority,TaskHandle_t* task) {
    CHECK(stack==4096);CHECK(priority==3);
    if(createFail)return 0;
    worker=entry;context=arg;*task=reinterpret_cast<void*>(1);return pdPASS;
}
void vTaskDelay(TickType_t ticks) {
    CHECK(ticks==2);CHECK(critical==0);CHECK(busDepth==0);
    now+=ticks*10;
    if(onDelay)onDelay();
    if(now>=finish)throw Stop{};
}
namespace digivice::board {
const BoardProfile& selectedProfile(){return fakeBoard;}
i2c_master_bus_handle_t sharedI2cBus(){return reinterpret_cast<void*>(1);}
esp_err_t lockSharedI2c(std::uint32_t timeout){
    CHECK(timeout<=20);CHECK(critical==0);
    if(busy)return ESP_ERR_TIMEOUT;
    CHECK(busDepth==0);++busDepth;return ESP_OK;
}
void unlockSharedI2c(){CHECK(busDepth==1);--busDepth;}
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t* c,i2c_master_dev_handle_t* d){
    CHECK(busDepth==1);CHECK(c->device_address==0x6b);CHECK(c->scl_speed_hz==400000);
    *d=reinterpret_cast<void*>(1);return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const std::uint8_t* bytes,std::size_t length,int timeout){
    CHECK(busDepth==1);CHECK(critical==0);CHECK(length==2);CHECK(timeout==20);++writes;
    if(stopFail&&bytes[0]==8&&bytes[1]==0)return ESP_FAIL;
    registers[bytes[0]]=bytes[1];return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,const std::uint8_t* reg,std::size_t addressLength,
                                    std::uint8_t* out,std::size_t length,int timeout){
    CHECK(busDepth==1);CHECK(critical==0);CHECK(addressLength==1);CHECK(timeout==20);++reads;
    if(*reg!=0x35){CHECK(length==1);*out=registers[*reg];return ESP_OK;}
    if(onBurst)onBurst();
    if(ioFail)return ESP_FAIL;
    CHECK(length==12);
    for(std::size_t i=0;i<length;++i)out[i]=0;
    const float phase=static_cast<float>(now>1200?now-1200:0)*6.28318530718F/600.0F;
    const auto raw=static_cast<std::int16_t>((1.0F+(now>1200?.18F*std::sin(phase):0))*8192);
    out[4]=static_cast<std::uint8_t>(raw);out[5]=static_cast<std::uint8_t>(static_cast<unsigned>(raw)>>8);
    return ESP_OK;
}
int main(){
    using digivice::device::Imu;using digivice::motion::StepStatus;
    {
        fresh();Imu imu;CHECK(imu.begin()==ESP_OK);
        unsigned confirmed=0;bool checked=false,paused=false;
        onDelay=[&]{
            const auto s=imu.stepReading();
            if(s.acceptedSteps&&!checked){CHECK(s.acceptedSteps==3);checked=true;}
            if(now==12000){
                CHECK(s.acceptedSteps>=16); // UI has never called poll during sampling.
                confirmed=s.acceptedSteps;CHECK(imu.poll(now).valid);
                CHECK(imu.pause(true)==ESP_OK);CHECK(!imu.quiescent());paused=true;
            }
            if(now==12020){CHECK(imu.quiescent());CHECK(registers[8]==0);CHECK(imu.stepReading().acceptedSteps==confirmed);}
            if(now==13000){CHECK(imu.quiescent());CHECK(imu.stepReading().acceptedSteps==confirmed);imu.recenter();}
            if(now==14000){CHECK(imu.pause(false)==ESP_OK);CHECK(!imu.quiescent());}
            if(now==17000)CHECK(imu.stepReading().acceptedSteps>confirmed);
        };
        run(18000);CHECK(checked);CHECK(paused);CHECK(imu.ready());
    }
    {
        fresh();Imu imu;CHECK(imu.begin()==ESP_OK);bool requested=false;
        onBurst=[&]{if(now>=6000&&!requested){requested=true;imu.pause(true);CHECK(!imu.quiescent());}};
        onDelay=[&]{if(now==6040){CHECK(imu.quiescent());CHECK(imu.stepReading().status==StepStatus::Paused);}};
        run(6500);CHECK(requested);CHECK(imu.quiescent());
    }
    {
        fresh();Imu imu;CHECK(imu.begin()==ESP_OK);
        onDelay=[&]{
            if(now==1000)busy=true;
            if(now==1300){CHECK(imu.stepReading().status==StepStatus::Gap);CHECK(imu.stepReading().observedAtMs<1000);}
            if(now==3000)busy=false;
            if(now==8000){CHECK(imu.ready());CHECK(imu.stepReading().acceptedSteps>=5);}
        };
        run(8500); // Mutex contention never permanently disables sampling.
    }
    {
        fresh();Imu imu;CHECK(imu.begin()==ESP_OK);
        onDelay=[&]{
            if(now==2000){stopFail=true;imu.pause(true);}
            if(now==2100){CHECK(!imu.quiescent());CHECK(registers[8]==0x43);}
            if(now==2200)stopFail=false;
            if(now==2240){CHECK(imu.quiescent());CHECK(registers[8]==0);imu.pause(false);}
            if(now==2300){imu.pause(true);imu.pause(false);CHECK(!imu.quiescent());}
            if(now==4000)CHECK(imu.ready());
        };
        run(4200);
    }
    {
        fresh();Imu imu;CHECK(imu.begin()==ESP_OK);
        onDelay=[&]{
            if(now==3000)ioFail=true;
            if(now==3200){CHECK(!imu.ready());CHECK(imu.stepReading().status==StepStatus::InvalidSample);imu.pause(true);}
            if(now==3400){CHECK(imu.quiescent());ioFail=false;imu.pause(false);}
            if(now==6000){CHECK(imu.ready());CHECK(imu.stepReading().acceptedSteps>0);}
        };
        run(6200);
    }
    {
        fresh();createFail=true;Imu imu;
        CHECK(imu.begin()==ESP_ERR_NO_MEM);CHECK(!imu.ready());CHECK(imu.quiescent());CHECK(registers[8]==0);
    }
    std::printf("actual IMU worker: %u checks, %u failures; deterministic RTOS/I2C doubles, no hardware\n",checks,failures);
    return failures?1:0;
}

#include "device_imu.hpp"
#include "board_hal.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}} while(0)
struct Stop {};
std::uint64_t now=0,finish=0;
unsigned critical=0,busDepth=0,reads=0,writes=0,created=0,rawReads=0;
bool busy=false,ioFail=false,stopFail=false,createFail=false,configFail=false,readbackMismatch=false,walking=true;
TaskFunction_t worker=nullptr;void* context=nullptr;
std::function<void()> onDelay,onBurst;
std::function<void(std::uint8_t)> onRegister;
std::vector<std::uint64_t> identityReads;
std::array<std::uint8_t,256> registers{};
digivice::board::BoardProfile fakeBoard{digivice::board::ProfileId::Waveshare146};
void fresh() {
    now=finish=0;critical=busDepth=reads=writes=created=rawReads=0;
    busy=ioFail=stopFail=createFail=configFail=readbackMismatch=false;walking=true;worker=nullptr;context=nullptr;
    onDelay={};onBurst={};onRegister={};identityReads.clear();
    registers.fill(0);registers[0]=5;registers[1]=0x7c;
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
    ++created;
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
    if(*reg!=0x35){
        CHECK(length==1);
        if(*reg==0)identityReads.push_back(now);
        if(onRegister)onRegister(*reg);
        if(configFail)return ESP_FAIL;
        *out=registers[*reg];
        if(readbackMismatch&&*reg==3)*out^=1;
        return ESP_OK;
    }
    ++rawReads;
    if(onBurst)onBurst();
    if(ioFail)return ESP_FAIL;
    CHECK(length==12);
    for(std::size_t i=0;i<length;++i)out[i]=0;
    const float phase=static_cast<float>(now>1200?now-1200:0)*6.28318530718F/600.0F;
    const auto raw=static_cast<std::int16_t>((1.0F+(walking&&now>1200?.18F*std::sin(phase):0))*8192);
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
            if(s.acceptedSteps&&!checked){CHECK(s.acceptedSteps==2);checked=true;}
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
            if(now==3200){CHECK(!imu.ready());CHECK(imu.recovering());CHECK(imu.stepReading().status==StepStatus::Recovering);imu.pause(true);}
            if(now==3400){CHECK(imu.quiescent());ioFail=false;imu.pause(false);}
            if(now==6000){CHECK(imu.ready());CHECK(imu.stepReading().acceptedSteps>0);}
        };
        run(6200);
    }
    {
        // One uninterrupted worker run: a cleared bus fault heals without a UI
        // poll, recenter, pause, restart, or a replacement pedometer instance.
        fresh();Imu imu;CHECK(imu.begin()==ESP_OK);CHECK(!imu.ready());
        std::uint32_t confirmed=0;unsigned readsAtFault=0;
        onDelay=[&]{
            if(now==6000){confirmed=imu.stepReading().acceptedSteps;CHECK(confirmed>0);ioFail=true;}
            if(now==6080){
                CHECK(imu.recovering());CHECK(!imu.ready());
                CHECK(imu.stepReading().status==StepStatus::Recovering);
                CHECK(imu.stepReading().acceptedSteps==confirmed);readsAtFault=rawReads;
                ioFail=false;walking=false;
            }
            if(now==7000){CHECK(rawReads==readsAtFault);CHECK(imu.recoveryAttempts()==0);}
            if(now==7060){ // Reconfigured at6040+1000, but no fresh sample yet.
                CHECK(imu.recoveryAttempts()==1);CHECK(imu.recovering());CHECK(!imu.ready());
                CHECK(imu.stepReading().acceptedSteps==confirmed);
            }
            if(now==7080){CHECK(imu.ready());CHECK(!imu.recovering());CHECK(imu.stepReading().status==StepStatus::Priming);}
            if(now==9000){CHECK(imu.stepReading().acceptedSteps==confirmed);walking=true;}
            if(now==12000){
                CHECK(imu.stepReading().acceptedSteps>confirmed);confirmed=imu.stepReading().acceptedSteps;
                ioFail=true;
            }
            if(now==12080){CHECK(imu.recovering());ioFail=false;walking=false;}
            if(now==13080){CHECK(imu.ready());CHECK(imu.recoveryAttempts()==2);CHECK(imu.stepReading().acceptedSteps==confirmed);}
            if(now==15000){CHECK(imu.stepReading().acceptedSteps==confirmed);CHECK(created==1);}
        };
        run(15000);
        CHECK(identityReads==std::vector<std::uint64_t>({0,7040,13040}));
    }
    {
        // Failed configuration retries are 1/2/4/8s, then capped at8s, rather
        // than attempting I2C at the50Hz sampling rate. Clearing the fault
        // waits for the existing deadline and starts a fresh detector window.
        fresh();configFail=true;walking=false;Imu imu;
        CHECK(imu.begin()==ESP_FAIL);CHECK(imu.recovering());CHECK(!imu.ready());CHECK(created==1);
        CHECK(imu.begin()==ESP_FAIL);CHECK(created==1);
        onDelay=[&]{
            if(now==16000){CHECK(imu.recoveryAttempts()==4);CHECK(rawReads==0);CHECK(writes==0);configFail=false;}
            if(now==22000){CHECK(!imu.ready());CHECK(imu.recovering());CHECK(imu.recoveryAttempts()==4);}
            if(now==23020){CHECK(imu.recoveryAttempts()==5);CHECK(imu.recovering());CHECK(!imu.ready());CHECK(rawReads==0);}
            if(now==23040){CHECK(imu.ready());CHECK(!imu.recovering());CHECK(imu.stepReading().status==StepStatus::Priming);}
        };
        run(24000);
        CHECK(identityReads==std::vector<std::uint64_t>({0,1000,3000,7000,15000,23000}));
        CHECK(imu.stepReading().acceptedSteps==0);CHECK(created==1);
    }
    {
        // A wrong identity never receives configuration writes, including
        // when pause is requested during a boot-failure recovery interval.
        fresh();registers[0]=0x99;walking=false;Imu imu;
        CHECK(imu.begin()==ESP_ERR_NOT_FOUND);CHECK(imu.recovering());
        onDelay=[&]{
            if(now==1200){CHECK(writes==0);imu.pause(true);}
            if(now==1240){CHECK(imu.quiescent());CHECK(writes==0);CHECK(imu.stepReading().status==StepStatus::Paused);}
            if(now==4000){CHECK(imu.recoveryAttempts()==1);registers[0]=5;imu.pause(false);}
            if(now==4040){CHECK(imu.ready());CHECK(imu.stepReading().acceptedSteps==0);}
        };
        run(4500);CHECK(identityReads==std::vector<std::uint64_t>({0,1000,4000}));
    }
    {
        // Pause arriving in the final configuration readback may follow a
        // landed enable write. Do not sample or acknowledge quiescence until
        // the disable write actually succeeds. Retry that write only100ms.
        fresh();walking=false;Imu imu;CHECK(imu.begin()==ESP_OK);
        bool interrupted=false;unsigned writesWhenPaused=0;
        onRegister=[&](std::uint8_t reg){
            if(now>=2040&&reg==8&&registers[8]==0x43&&!interrupted){
                interrupted=true;stopFail=true;imu.pause(true);CHECK(!imu.quiescent());
            }
        };
        onDelay=[&]{
            if(now==1000)ioFail=true;
            if(now==1100)ioFail=false;
            if(now==2080){CHECK(interrupted);CHECK(!imu.quiescent());CHECK(!imu.ready());writesWhenPaused=writes;}
            if(now==2140){CHECK(writes==writesWhenPaused);CHECK(!imu.quiescent());stopFail=false;}
            if(now==2200){CHECK(imu.quiescent());CHECK(registers[8]==0);CHECK(imu.stepReading().acceptedSteps==0);}
            if(now==3000){CHECK(imu.recoveryAttempts()==1);imu.pause(false);}
            if(now==4080){CHECK(imu.ready());CHECK(imu.recoveryAttempts()==2);CHECK(imu.stepReading().acceptedSteps==0);}
        };
        run(4500);CHECK(interrupted);
    }
    {
        // Boot readback mismatch is a recoverable configuration fault, with
        // channels stopped until a complete verified configuration succeeds.
        fresh();readbackMismatch=true;walking=false;Imu imu;
        CHECK(imu.begin()==ESP_ERR_INVALID_RESPONSE);CHECK(imu.recovering());
        CHECK(registers[8]==0);CHECK(created==1);CHECK(!imu.ready());
        onDelay=[&]{
            if(now==500){readbackMismatch=false;CHECK(rawReads==0);}
            if(now==1020){CHECK(imu.recovering());CHECK(!imu.ready());CHECK(registers[3]==0x17);CHECK(registers[8]==0x43);}
            if(now==1040){CHECK(imu.ready());CHECK(imu.stepReading().status==StepStatus::Priming);}
        };
        run(1500);CHECK(imu.stepReading().acceptedSteps==0);CHECK(created==1);
    }
    {
        fresh();createFail=true;Imu imu;
        CHECK(imu.begin()==ESP_ERR_NO_MEM);CHECK(!imu.ready());CHECK(imu.quiescent());CHECK(registers[8]==0);
    }
    {
        // No worker exists to finish a failed cleanup. Never claim a stopped
        // sensor merely because task allocation failed after the enable write.
        fresh();createFail=true;Imu imu;
        onRegister=[&](std::uint8_t reg){if(reg==8&&registers[8]==0x43)stopFail=true;};
        CHECK(imu.begin()==ESP_ERR_NO_MEM);CHECK(!imu.ready());CHECK(!imu.quiescent());
        CHECK(registers[8]==0x43);CHECK(created==0);
    }
    std::printf("actual IMU worker: %u checks, %u failures; deterministic RTOS/I2C doubles, no hardware\n",checks,failures);
    return failures?1:0;
}

#include "board_hal.hpp"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstdlib>
using namespace digivice::board;
unsigned checks=0, calls=0, failAt=0; bool locked=false;
std::uint8_t regs[]{0,0x77,0,0xff};
#define CHECK(x) do {++checks; if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(false)
esp_err_t gpio_set_level(gpio_num_t,std::uint32_t){CHECK(false);return ESP_FAIL;}
esp_err_t gpio_config(const gpio_config_t*){CHECK(false);return ESP_FAIL;}
int gpio_get_level(gpio_num_t){CHECK(false);return 0;}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* p){return p;}
int xSemaphoreTake(SemaphoreHandle_t,TickType_t){CHECK(!locked);locked=true;return pdTRUE;}
int xSemaphoreGive(SemaphoreHandle_t){CHECK(locked);locked=false;return pdTRUE;}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* c,i2c_master_bus_handle_t* out){CHECK(c->scl_io_num==7&&c->sda_io_num==15);*out=reinterpret_cast<i2c_master_bus_handle_t>(1);return ESP_OK;}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t* c,i2c_master_dev_handle_t* out){CHECK(locked&&c->device_address==0x20);*out=reinterpret_cast<i2c_master_dev_handle_t>(2);return ESP_OK;}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,const std::uint8_t* reg,std::size_t n,std::uint8_t* data,std::size_t size,int){CHECK(locked&&n==1&&size==1);if(++calls==failAt)return ESP_FAIL;*data=regs[*reg];return ESP_OK;}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const std::uint8_t* data,std::size_t n,int){CHECK(locked&&n==2);if(++calls==failAt)return ESP_FAIL;regs[data[0]]=data[1];return ESP_OK;}
int main(int argc,char** argv){
 CHECK(argc==2);failAt=std::strtoul(argv[1],nullptr,10);
 CHECK(selectedProfile().id==ProfileId::Waveshare21);
 initialize();CHECK(!powerHoldReady()&&!powerControlAvailable());
 bool pressed=true;CHECK(readPowerPressed(pressed)==ESP_ERR_NOT_SUPPORTED);CHECK(setPowerHold(true)==ESP_ERR_NOT_SUPPORTED);
 CHECK(!sdPinsAvailable());CHECK(prepareSdCardSelect()==ESP_ERR_INVALID_STATE&&calls==0);
 const auto prepared=prepareDisplayCommandPins();
 if(failAt){CHECK(prepared!=ESP_OK);CHECK(!sdPinsAvailable());CHECK(prepareSdCardSelect()==ESP_ERR_INVALID_STATE);CHECK(!locked);return 0;}
 CHECK(prepared==ESP_OK);CHECK((regs[1]&0x8c)==0x0c);CHECK((regs[3]&0x8c)==0);CHECK((regs[1]&0x70)==0x70&&(regs[3]&0x70)==0x70);
 CHECK(setPeripheralReset(PeripheralReset::Display,true)==ESP_OK);CHECK(!(regs[1]&1));
 CHECK(setPeripheralReset(PeripheralReset::Display,false)==ESP_OK);CHECK(regs[1]&1);
 CHECK(setPeripheralReset(PeripheralReset::Touch,true)==ESP_OK);CHECK(!(regs[1]&2));
 CHECK(setDisplaySelected(true)==ESP_OK);CHECK(!(regs[1]&4));CHECK(!sdPinsAvailable());
 CHECK(releaseDisplayCommandPins()==ESP_OK);CHECK(sdPinsAvailable());CHECK(regs[1]&4);
 CHECK(prepareSdCardSelect()==ESP_OK);CHECK(regs[1]&8);
 const auto saved=calls;CHECK(setDisplaySelected(true)==ESP_ERR_INVALID_STATE);CHECK(prepareDisplayCommandPins()==ESP_ERR_INVALID_STATE);CHECK(setPeripheralReset(PeripheralReset::Display,true)==ESP_ERR_INVALID_STATE);CHECK(saved==calls);
 CHECK(setPeripheralReset(PeripheralReset::Touch,false)==ESP_OK);CHECK(regs[1]&2);CHECK((regs[1]&0x70)==0x70&&(regs[3]&0x70)==0x70);CHECK(!locked);
 std::printf("2.1 board ownership: %u checks, no power GPIO calls, SD ordering, owned EXIO only\n",checks);
}

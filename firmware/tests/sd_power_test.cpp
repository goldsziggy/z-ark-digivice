// Replace only fixed-path open and fsync. All other storage operations use a
// disposable real host file. This cannot measure FatFS/card persistence.
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include <string>
int testOpen(const char*,int,...);
int testSync(int);
#define open testOpen
#define fsync testSync
#include "../main/sd_asset_storage.cpp"
#undef open
#undef fsync

namespace {
int checks=0;unsigned syncs=0,unmounts=0;
bool failSync=false,failMount=false;
std::string path;
void check(bool yes,const char* name){++checks;if(!yes)throw std::runtime_error(name);}
}
int testOpen(const char* requested,int flags,...){
    check(std::strcmp(requested,"/sdcard/DVASSET1.CCH")==0,"only fixed cache path opened");
    if(flags&O_CREAT){va_list args;va_start(args,flags);const auto mode=va_arg(args,int);va_end(args);return ::open(path.c_str(),flags,mode);}
    return ::open(path.c_str(),flags);
}
int testSync(int descriptor){++syncs;if(failSync){errno=EIO;return -1;}return ::fsync(descriptor);}
namespace digivice::board {
const BoardProfile& selectedProfile(){return kWaveshare146;}
bool powerHoldReady(){return true;}
esp_err_t prepareSdCardSelect(){return ESP_OK;}
}
esp_err_t esp_vfs_fat_sdmmc_mount(const char*,const sdmmc_host_t*,const sdmmc_slot_config_t*,const esp_vfs_fat_sdmmc_mount_config_t* config,sdmmc_card_t** card){
    check(!config->format_if_mount_failed,"mount never formats");
    static sdmmc_card_t value;if(failMount)return ESP_FAIL;*card=&value;return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_unmount(const char*,sdmmc_card_t*){++unmounts;return ESP_OK;}
int main(int argc,char** argv){try{
    using digivice::assets::SdAssetStorage;
    if(argc!=2)throw std::runtime_error("temporary cache path required");path=argv[1];
    {
        SdAssetStorage storage;
        check(storage.preparePowerOff()==ESP_OK&&syncs==0,"never-mounted storage needs no flush");
        failMount=true;check(storage.begin()!=ESP_OK,"absent card remains optional");failMount=false;
        check(storage.preparePowerOff()==ESP_OK&&syncs==0,"failed mount does not invent pending writes");
    }
    {
        SdAssetStorage storage;check(storage.begin()==ESP_OK&&storage.ready(),"real temporary file initialized");
        const auto before=syncs;check(storage.preparePowerOff()==ESP_OK&&syncs==before+1,"shutdown explicitly fsyncs mounted cache");
        check(storage.ready()&&unmounts==0,"quiet standby keeps cache available for resume");
        unsigned char byte=0;check(storage.program(0,&byte,1),"resumed cache write remains available");
        failSync=true;const auto beforeFailure=syncs;
        check(storage.preparePowerOff()!=ESP_OK&&syncs==beforeFailure+1,"fsync failure refuses shutdown");
        failSync=false;
        check(!storage.ready()&&storage.preparePowerOff()!=ESP_OK&&syncs==beforeFailure+1,"failed media stays unavailable and cannot pass retry");
        check(storage.lastError()!=ESP_OK,"flush error is exposed");
        check(std::strstr(storage.diagnostic(),"power retained")!=nullptr,"flush error has a truthful user diagnostic");
    }
    check(unmounts==1,"normal lifetime close unmounts once");
    {
        SdAssetStorage storage;check(storage.begin()==ESP_OK,"new boot revalidates existing cache file");
        check(storage.preparePowerOff()==ESP_OK,"restarted storage can acknowledge flush");
    }
    std::cout<<"PASS "<<checks<<" SD power checks: real POSIX file, injected fsync faults; no card durability claim.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

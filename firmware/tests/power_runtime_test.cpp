// Integration: actual HandheldRuntime/app command code, power FSM, game,
// SaveStore, practice session, motion delivery and cache. Only SDK/hardware and
// concurrent adapter I/O are doubled. This cannot validate GPIOs, USB or radio.
#include "../main/handheld_runtime.hpp"
#include "park_save_backend.hpp"
#include "legacy_v15_snapshot.hpp"
#include "../main/app_main.cpp"
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fake {
std::uint64_t now=0;
bool pressed=false, held=true, powerEnabled=true;
bool assetBusy=false, networkBusy=false, networkFailure=false, syncFailure=false;
bool motionEnabled=false, practiceFailure=false;
bool assetPaused=false, assetsInitiallyPaused=false, inspectionBarrier=true, refuseInspectionBarrier=false;
bool sdReady=true;
std::function<void(bool)> sdMounted;
unsigned inventoryCalls=0, probeCalls=0;
bool transferPauseFailure=false, transferBarrier=true;
unsigned transferHandles=0, transferPauses=0;
std::size_t transferLineBytes=0;
std::function<void(bool,bool)> transferState;
std::uint32_t motionCount=0;
unsigned cuts=0, assertions=0, syncs=0, requests=0, practiceWrites=0, restarts=0;
unsigned networkTicks=0;
std::uint32_t randomValue=7;
unsigned randomCalls=0;
bool diagnosticAllocationFailure=false;
unsigned diagnosticAllocations=0,diagnosticFrees=0,diagnosticOutstanding=0;
std::size_t diagnosticBytes=0;
std::uint32_t diagnosticCaps=0;
bool networkPaused=false, senseFailure=false, cutFailure=false, assertFailure=false, recheckPressed=false;
std::uint64_t readAt=~std::uint64_t{0}; unsigned readsAtTime=0;
std::vector<std::uint8_t> sd(digivice::assets::kStorageBytes,255);
digivice::devicepractice::Slot practiceSlots[2]{};
bool practicePresent[2]{};
std::function<void()> delayHook;
struct Restart {};
void reset() {
    now=0;pressed=false;held=true;powerEnabled=true;networkPaused=false;
    assetBusy=networkBusy=networkFailure=syncFailure=motionEnabled=practiceFailure=false;
    senseFailure=cutFailure=assertFailure=recheckPressed=false;readAt=~std::uint64_t{0};readsAtTime=0;
    motionCount=cuts=assertions=syncs=requests=practiceWrites=restarts=0;
    networkTicks=0;
    randomValue=7;randomCalls=0;
    diagnosticAllocationFailure=false;diagnosticAllocations=diagnosticFrees=diagnosticOutstanding=0;
    diagnosticBytes=0;diagnosticCaps=0;
    sd.assign(sd.size(),255);practicePresent[0]=practicePresent[1]=false;delayHook={};
    assetPaused=assetsInitiallyPaused=refuseInspectionBarrier=false;inspectionBarrier=true;inventoryCalls=probeCalls=0;
    sdReady=true;sdMounted={};
    transferPauseFailure=false;transferBarrier=true;transferHandles=transferPauses=0;transferLineBytes=0;transferState={};
}
}
digivice::entropy::Seeds testSeeds() { return {17u,0x2468aceu,0x12345678u,7u}; }
namespace digivice::device { entropy::Seeds collectStartupEntropy() { return testSeeds(); } }
std::uint32_t esp_random() { ++fake::randomCalls; return fake::randomValue; }
std::int64_t esp_timer_get_time() { return static_cast<std::int64_t>(fake::now*1000); }
void esp_restart() { ++fake::restarts; throw fake::Restart{}; }
void vTaskDelay(TickType_t ticks) { fake::now+=ticks;if(fake::delayHook)fake::delayHook(); }
void* heap_caps_malloc(std::size_t bytes,std::uint32_t capabilities) {
    ++fake::diagnosticAllocations;fake::diagnosticBytes=bytes;fake::diagnosticCaps=capabilities;
    if(fake::diagnosticAllocationFailure||bytes>digivice::kJsonCapacity)return nullptr;
    auto* memory=std::malloc(bytes);if(memory)++fake::diagnosticOutstanding;return memory;
}
void heap_caps_free(void* allocation) {
    if(allocation){++fake::diagnosticFrees;--fake::diagnosticOutstanding;std::free(allocation);}
}

namespace digivice::board {
Capabilities initialize(){return {};}
const char* boardName(){return "mock Waveshare146";}
const BoardProfile& selectedProfile(){return kWaveshare146;}
bool powerHoldReady(){return fake::powerEnabled;}
esp_err_t powerHoldStatus(){return fake::powerEnabled?ESP_OK:ESP_FAIL;}
bool powerControlAvailable(){return fake::powerEnabled;}
esp_err_t readPowerPressed(bool& value){if(fake::readAt!=fake::now){fake::readAt=fake::now;fake::readsAtTime=0;}++fake::readsAtTime;value=fake::pressed||(fake::recheckPressed&&fake::readsAtTime>1);return fake::senseFailure?ESP_FAIL:ESP_OK;}
esp_err_t setPowerHold(bool value){if((value&&fake::assertFailure)||(!value&&fake::cutFailure))return ESP_FAIL;fake::held=value;if(value)++fake::assertions;else ++fake::cuts;return ESP_OK;}
bool powerHoldAsserted(){return fake::held;}
i2c_master_bus_handle_t sharedI2cBus(){return reinterpret_cast<void*>(1);}
esp_err_t sharedI2cStatus(){return ESP_OK;}
esp_err_t lockSharedI2c(std::uint32_t){return ESP_OK;}
void unlockSharedI2c(){}
esp_err_t prepareSdCardSelect(){return ESP_OK;}
}
namespace digivice::storage {
NvsBackend::~NvsBackend()=default;
esp_err_t NvsBackend::initialize(){handle_=1;ready_=true;return ESP_OK;}
ReadStatus NvsBackend::readSlot(unsigned,Slot&){return ReadStatus::Missing;}
bool NvsBackend::writeSlot(unsigned,const Snapshot&){return true;}
}
namespace digivice::devicetrade {
bool freshCareStorageAllowed(){return true;}
NvsBackend::~NvsBackend()=default;
esp_err_t NvsBackend::initialize(){handle_=1;ready_=true;return ESP_OK;}
Read NvsBackend::read(unsigned,Slot&){return Read::Missing;}
bool NvsBackend::write(unsigned,const Bytes&){return false;}
}
namespace digivice::devicepractice {
NvsBackend::~NvsBackend()=default;
esp_err_t NvsBackend::initialize(){handle_=1;ready_=true;return ESP_OK;}
Read NvsBackend::read(unsigned i,Slot& out){if(!fake::practicePresent[i])return Read::Missing;out=fake::practiceSlots[i];return Read::Present;}
bool NvsBackend::write(unsigned i,const Bytes& value){++fake::practiceWrites;if(fake::practiceFailure)return false;fake::practiceSlots[i].bytes=value;fake::practiceSlots[i].length=kRecordBytes;fake::practicePresent[i]=true;return true;}
}
namespace digivice::net {
bool NetworkAdapter::privateHttpBuildEnabled(){return false;}
esp_err_t NetworkAdapter::begin(){ready_=true;controller_.configure(false,fake::now);return ESP_OK;}
esp_err_t NetworkAdapter::configure(const Config& value){config_=value;controller_.configure(true,fake::now);return ESP_OK;}
esp_err_t NetworkAdapter::forget(){controller_.configure(false,fake::now);return ESP_OK;}
void NetworkAdapter::tick(){++fake::networkTicks;controller_.tick(fake::now);}
void NetworkAdapter::retry(){controller_.retry(fake::now);}
void NetworkAdapter::pause(bool value){paused_=value;fake::networkPaused=value;controller_.pause(value,fake::now);}
esp_err_t NetworkAdapter::quiescence()const{return fake::networkFailure?ESP_FAIL:(fake::networkBusy||!controller_.status().paused?ESP_ERR_NOT_FINISHED:ESP_OK);}
}
namespace digivice::assets {
// Only concurrent-I/O ownership is doubled. The separately tested transfer
// engine owns syntax, path confinement, hashes, resume and actual file durability.
UsbSdTransfer::UsbSdTransfer(const char*,Cooperate cooperate,void* context)
    :cooperate_(cooperate),context_(context){
    fake::transferState=[this](bool active,bool busy){active_=active;busy_=busy;};
}
bool UsbSdTransfer::pause(){
    ++fake::transferPauses;
    if(busy_||fake::transferPauseFailure)return false;
    active_=false;return true;
}
bool UsbSdTransfer::handle(const char* line,char* response,std::size_t capacity){
    ++fake::transferHandles;fake::transferLineBytes=std::strlen(line);
    fake::transferBarrier&=fake::assetPaused&&!fake::assetBusy;
    char verb[16]{};std::sscanf(line,"sdput %15s",verb);
    const char* reply="SDPUT ERROR code=SYNTAX";
    if(busy_)reply="SDPUT ERROR code=BUSY";
    else if(!std::strcmp(verb,"status"))reply=active_?"SDPUT PENDING":"SDPUT IDLE";
    else if(!std::strcmp(verb,"begin")){active_=true;reply="SDPUT READY";}
    else if(!std::strcmp(verb,"chunk"))reply="SDPUT ACK offset=512";
    else if(!std::strcmp(verb,"abort")){active_=false;reply="SDPUT PAUSED";}
    std::snprintf(response,capacity,"%s",reply);return true;
}
bool DeviceAssetsClient::developmentAssetsEnabled(){return true;}
esp_err_t DeviceAssetsClient::begin(Cache*){paused_=fake::assetsInitiallyPaused;fake::assetPaused=paused_;return ESP_OK;}
bool DeviceAssetsClient::request(const char*,bool,const char*,const char*const*,std::size_t){++fake::requests;return !paused_;}
void DeviceAssetsClient::cancel(){}
void DeviceAssetsClient::pause(bool value){paused_=value;fake::assetPaused=value;}
bool DeviceAssetsClient::quiescent()const{return paused_&&!fake::assetBusy&&!fake::refuseInspectionBarrier;}
void DeviceAssetsClient::tick(){}
DownloadStatus DeviceAssetsClient::status()const{auto value=status_;value.busy=fake::assetBusy;return value;}
bool DeviceAssetsClient::cachedSpec(const char*,Spec&){return false;}
bool DeviceAssetsClient::tryRead(const Spec&,std::size_t,void*,std::size_t,Result&){return false;}
const char* downloadPhaseName(DownloadPhase){return "mock";}
const char* downloadErrorName(DownloadError){return "mock";}
void printSdInventory(const SdAssetStorage&,std::uint32_t){++fake::inventoryCalls;fake::inspectionBarrier&=fake::assetPaused&&!fake::assetBusy;}
void printSdBootProbe(const SdAssetStorage&){++fake::probeCalls;fake::inspectionBarrier&=fake::assetPaused&&!fake::assetBusy;}
bool SdAssetStorage::buildEnabled(){return true;}
SdAssetStorage::~SdAssetStorage()=default;
esp_err_t SdAssetStorage::begin(){mounted_=true;fake::sdMounted=[this](bool value){mounted_=value;};return ESP_OK;}
void SdAssetStorage::end(){mounted_=false;}
esp_err_t SdAssetStorage::preparePowerOff(){++fake::syncs;return fake::syncFailure?ESP_FAIL:ESP_OK;}
bool SdAssetStorage::ready()const{return mounted_&&fake::sdReady;}
esp_err_t SdAssetStorage::lastError()const{return ESP_OK;}
const char* SdAssetStorage::diagnostic()const{return "mock SD";}
bool SdAssetStorage::read(std::size_t offset,void* bytes,std::size_t length){if(offset>fake::sd.size()||length>fake::sd.size()-offset)return false;std::memcpy(bytes,fake::sd.data()+offset,length);return true;}
bool SdAssetStorage::erase(std::size_t offset,std::size_t length){if(offset>fake::sd.size()||length>fake::sd.size()-offset)return false;std::fill_n(fake::sd.data()+offset,length,255);return true;}
bool SdAssetStorage::program(std::size_t offset,const void* bytes,std::size_t length){if(offset>fake::sd.size()||length>fake::sd.size()-offset)return false;std::memcpy(fake::sd.data()+offset,bytes,length);return true;}
bool SdAssetStorage::PosixFile::size(std::size_t&){return false;}
bool SdAssetStorage::PosixFile::read(std::size_t,void*,std::size_t){return false;}
bool SdAssetStorage::PosixFile::write(std::size_t,const void*,std::size_t){return false;}
bool SdAssetStorage::PosixFile::sync(){return false;}
}
namespace digivice::motion {
bool MotionAdapter::buildEnabled(){return fake::motionEnabled;}
MotionAdapter::~MotionAdapter()=default;
AdapterStatus MotionAdapter::begin(i2c_master_bus_handle_t,bool){reading_.status=fake::motionEnabled?AdapterStatus::Ready:AdapterStatus::Disabled;return reading_.status;}
CounterReading MotionAdapter::poll(std::uint64_t now){reading_.status=AdapterStatus::Ready;reading_.valid=true;reading_.counter24=fake::motionCount;reading_.observedAtMs=now;return reading_;}
void MotionAdapter::end(){}
const char* adapterStatusText(AdapterStatus){return "mock motion";}
}

namespace {
unsigned checks=0;
void require(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
struct Fixture {
    digivice::State state=[] {auto s=digivice::newDevice();(void)digivice::apply(s,digivice::Action::Hatch,1);(void)digivice::apply(s,digivice::Action::WorldSeed,testSeeds().world);return s;}();
    ParkSaveBackend backend;
    digivice::storage::SaveStore saves{backend};
    digivice::HandheldRuntime runtime{state,saves,testSeeds()};
    Fixture(){require(saves.restore(state)==digivice::storage::BootStatus::Empty,"empty fixture");require(saves.checkpoint(state),"initial save");runtime.begin();}
    void tick(unsigned milliseconds=20){fake::now+=milliseconds;runtime.poll();}
    void wait(unsigned milliseconds){for(unsigned elapsed=0;elapsed<milliseconds;elapsed+=20)tick();}
    void press(unsigned milliseconds){fake::pressed=true;wait(milliseconds);}
    void release(unsigned milliseconds=100){fake::pressed=false;wait(milliseconds);}
    bool send(const char* text){char line[1536]{};std::strncpy(line,text,sizeof(line)-1);return runtime.command(line);}
    void dispatch(const char* text){char line[1536]{};std::strncpy(line,text,sizeof(line)-1);if(!runtime.command(line))command(line,state,saves,{},runtime);}
    void shutdown(){wait(100);press(3200);release();}
    void wake(){release(100);press(100);release(100);}

};
struct ConsolePipe {
    int original=-1,descriptors[2]{-1,-1};
    ConsolePipe(){
        require(::pipe(descriptors)==0,"console pipe");
        original=::dup(STDIN_FILENO);require(original>=0,"save stdin");
        require(::fcntl(descriptors[0],F_SETFL,O_NONBLOCK)==0&&::dup2(descriptors[0],STDIN_FILENO)>=0,"nonblocking test console");
        std::clearerr(stdin);
    }
    ~ConsolePipe(){::dup2(original,STDIN_FILENO);::close(original);::close(descriptors[0]);::close(descriptors[1]);std::clearerr(stdin);fake::delayHook={};}
    void write(const std::string& text){require(::write(descriptors[1],text.data(),text.size())==static_cast<ssize_t>(text.size()),"console bytes queued");}
};
struct ConsoleOutput {
    std::FILE* file=std::tmpfile();
    int original=-1;
    ConsoleOutput(){
        require(file!=nullptr,"diagnostic output file");std::fflush(stdout);
        original=::dup(STDOUT_FILENO);
        require(original>=0&&::dup2(::fileno(file),STDOUT_FILENO)>=0,"capture diagnostic output");
    }
    ~ConsoleOutput(){std::fflush(stdout);if(original>=0){::dup2(original,STDOUT_FILENO);::close(original);}if(file)std::fclose(file);}
    std::string read(){
        std::fflush(stdout);std::rewind(file);std::string result;char chunk[4096];
        for(std::size_t n;(n=std::fread(chunk,1,sizeof(chunk),file))!=0;)result.append(chunk,n);
        return result;
    }
};
}
namespace {
void boundedDiagnosticMemory() {
    using namespace digivice;
    fake::reset();Fixture f;
    f.state.sequence=f.state.foregroundSequence=100;f.state.collectionCount=kCollectionCapacity;
    f.state.captures=f.state.encounters=kCollectionCapacity-1;f.state.steps=100*(kCollectionCapacity-1);
    f.state.nextMemberId=kCollectionCapacity+1;
    for(unsigned i=1;i<kCollectionCapacity;++i){f.state.collection[i]=f.state.collection[0];f.state.collection[i].id=i+1;f.state.collection[i].capturedAtSequence=i+1;}
    require(isValid(f.state)&&f.saves.checkpoint(f.state),"full60 diagnostic fixture is durably valid");
    const auto before=f.state;const auto writes=f.backend.writes;
    const auto readStatus=[&]{ConsoleOutput capture;printState(f.state);return capture.read();};
    const auto json=readStatus();
    require(json.find("\"collectionCapacity\":60")!=std::string::npos&&json.find("\"id\":60")!=std::string::npos,"full roster status includes final member and capacity");
    require(json.size()<kJsonCapacity&&fake::diagnosticBytes==kJsonCapacity,"diagnostic allocation is bounded by the native JSON contract");
    require(fake::diagnosticCaps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),"display target diagnostics request PSRAM");
    require(fake::diagnosticAllocations==1&&fake::diagnosticFrees==1&&!fake::diagnosticOutstanding,"successful diagnostic releases its temporary buffer");
    fake::diagnosticAllocationFailure=true;const auto failure=readStatus();
    require(failure.find("insufficient diagnostic memory")!=std::string::npos,"OOM is visible to console user");
    require(fake::diagnosticAllocations==2&&fake::diagnosticFrees==1&&!fake::diagnosticOutstanding,"OOM makes one bounded request without internal fallback or leaked buffer");
    require(sameSavedState(f.state,before)&&f.backend.writes==writes,"success and OOM leave all60 members and durable saves unchanged");
    fake::diagnosticAllocationFailure=false;require(readStatus()==json,"diagnostic retry returns the exact same full state");
    require(fake::diagnosticAllocations==3&&fake::diagnosticFrees==2&&!fake::diagnosticOutstanding,"retry releases memory");
}
void consoleAutoWaitsForFlick() {
    using namespace digivice;
    fake::reset();Fixture f;f.wait(100);
    State before,expected;bool found=false;
    for(unsigned seed=1;seed<=64&&!found;++seed) {
        before=newDevice(seed);require(apply(before,Action::Hatch,2)==Error::None,"auto fixture hatch");
        require(apply(before,Action::WorldSeed,testSeeds().world)==Error::None,"auto fixture independent world");
        require(apply(before,Action::Explore,1000)==Error::None,"auto fixture encounter");before.battleMode=BattleMode::Auto;
        expected=before;require(applyAutoFight(expected)==Error::None,"native AutoFight fixture");
        found=expected.autoCapture==AutoCapture::Awaiting;
    }
    require(found,"fixture reaches capture opportunity");f.state=before;require(f.saves.checkpoint(f.state),"auto initial state saved");
    const auto writes=f.backend.writes;f.dispatch("auto");
    require(sameSavedState(f.state,expected)&&f.backend.writes==writes+1,"serial auto saves partial attack chunk, not historical autothrow");
    require(f.state.captureAttempts==0&&f.state.lastCapture.result==CaptureResult::None,"no throw while awaiting user");
    f.dispatch("auto");require(f.backend.writes==writes+1,"duplicate auto cannot continue or throw");
    const auto paused=expected;
    for(const auto* invalid : {"ring-capture", "ring-capture -1", "ring-capture 2400", "ring-capture 12x"}) {
        f.dispatch(invalid);
        require(sameSavedState(f.state,paused)&&f.backend.writes==writes+1,"invalid or missing serial ring phase cannot become an implicit throw");
    }
    require(applyAutoResume(expected)==Error::None,"expected skip resumes attacks");f.dispatch("auto-resume");
    require(sameSavedState(f.state,expected)&&f.backend.writes==writes+2&&f.state.phase==Phase::Home,"explicit resume completes without capture prompt");
    Fixture ring;ring.wait(100);ring.state=paused;
    require(ring.saves.checkpoint(ring.state),"save legal paused state in a fresh fixture without rolling back a checkpoint");
    expected=paused;require(apply(expected,Action::RingCapture,2300)==Error::None,"expected red timing throw");
    const auto beforeThrow=ring.backend.writes;ring.dispatch("ring-capture 2300");
    require(sameSavedState(ring.state,expected)&&ring.backend.writes==beforeThrow+1,"explicit serial phase produces exactly the native saved throw");
}
void startupOffersAreDurable() {
    using namespace digivice;
    fake::reset();
    State state=newDevice();ParkSaveBackend backend;storage::SaveStore saves{backend};
    require(saves.restore(state)==storage::BootStatus::Empty,"fresh egg storage");
    require(saves.checkpoint(state),"original egg checkpoint");
    HandheldRuntime runtime{state,saves,testSeeds()};runtime.begin();
    require(state.phase==Phase::Egg&&!state.onboardingComplete&&state.collectionCount==0,
            "offer initialization never hatches or grants a partner");
    require(state.starterOfferSeed==7&&state.sequence==1&&backend.writes==2,
            "one offer event is durably saved before onboarding");
    require(state.starterOffers[0]!=state.starterOffers[1]&&state.starterOffers[1]!=state.starterOffers[2]&&
            state.starterOffers[0]!=state.starterOffers[2],"three distinct offered forms");
    const State offered=state;
    storage::SaveStore restored{backend};State after;
    require(restored.restore(after)==storage::BootStatus::Loaded&&sameSavedState(offered,after),
            "restart restores exact offer and seed");
    fake::randomValue=99;const auto calls=fake::randomCalls,writes=backend.writes;
    HandheldRuntime rebooted{after,restored,testSeeds()};rebooted.begin();
    require(sameSavedState(offered,after)&&backend.writes==writes&&fake::randomCalls==calls+1,
            "restart only draws the motion session, never rerolls saved offers");
    char open[]{"starter confirm"},next[]{"starter next"},back[]{"starter back"};
    require(rebooted.command(open)&&rebooted.command(next)&&rebooted.command(back),"browse and cancel starter choices");
    require(sameSavedState(offered,after)&&backend.writes==writes,
            "starter browsing and cancel cannot mutate the saved offer");

    for(bool landed:{false,true}) {
        fake::reset();State egg=newDevice();ParkSaveBackend failing;storage::SaveStore store{failing};
        require(store.restore(egg)==storage::BootStatus::Empty&&store.checkpoint(egg),"fault fixture original egg");
        failing.failBefore=!landed;failing.failAfter=landed;
        HandheldRuntime failed{egg,store,testSeeds()};failed.begin();
        require(egg.starterOfferSeed==0&&egg.phase==Phase::Egg&&!store.writable()&&!failed.allowsCareAction(Action::Hatch),
                "uncertain offer write stays unpublished and blocks hatch");
        failing.failBefore=failing.failAfter=false;storage::SaveStore recovery{failing};State recovered;
        require(recovery.restore(recovered)==storage::BootStatus::Loaded,"own save survives failed offer checkpoint");
        require((recovered.starterOfferSeed!=0)==landed,"restore honors whether failed write reached storage");
        HandheldRuntime again{recovered,recovery,testSeeds()};again.begin();
        require(recovered.starterOfferSeed==7&&recovered.phase==Phase::Egg&&recovered.collectionCount==0,
                "recovery establishes exactly one fixed offer without hatching");
    }
}
digivice::State installedTestEncounter() {
    using namespace digivice;
    std::vector<std::uint8_t> bytes((sizeof(kLegacyV15EncounterHex)-1)/2);
    auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(std::size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<std::uint8_t>(digit(kLegacyV15EncounterHex[2*i])*16+digit(kLegacyV15EncounterHex[2*i+1]));
    State state;
    require(decodeSnapshot(bytes.data(),bytes.size(),state)==SnapshotStatus::Migrated,"installed synthetic rules12 fixture migrates without edits");
    require(needsTestEncounterResolution(state)&&state.wildFormId==4,"installed fixture is active Flicker test encounter");
    return state;
}
void startupTestEncounterResolution() {
    using namespace digivice;
    for(unsigned scenario=0;scenario<4;++scenario) {
        fake::reset();State state=installedTestEncounter();
        if(scenario) {
            require(apply(state,Action::ResolveTestEncounter,0)==Error::None,"prepare real encounter fixture");
            require(apply(state,Action::AccrueSteps,1000)==Error::None&&apply(state,Action::PresentEncounter,0)==Error::None,"real current-rule encounter");
        }
        if(scenario==1||scenario==2) {
            require(apply(state,Action::AccrueSteps,1000)==Error::None,"earn one pending fixture");
            state.pendingEncounter={4,1,12};
            if(scenario==2) { // Synthetic active and pending, resolved in one event.
                state=installedTestEncounter();
                require(apply(state,Action::ResolveTestEncounter,0)==Error::None&&apply(state,Action::AccrueSteps,1000)==Error::None,"valid threshold metadata");
                const auto queued=state;
                state=installedTestEncounter();
                state.encounterRng=queued.encounterRng;state.encounterTarget=queued.encounterTarget;state.encounterProgress=queued.encounterProgress;
                state.pendingEncounter={4,1,12};
            }
        }
        require(isValid(state),"valid synthetic boot case");
        ParkSaveBackend backend;storage::SaveStore saves{backend};
        require(saves.restore(state)==storage::BootStatus::Empty&&saves.checkpoint(state),"original own checkpoint");
        const auto before=state;State expected=state;
        if(scenario!=3)require(apply(expected,Action::ResolveTestEncounter,0)==Error::None,"expected explicit resolution event");
        require(apply(expected,Action::WorldSeed,testSeeds().world)==Error::None,"expected independent world event after resolution");
        const auto writes=backend.writes;
        HandheldRuntime runtime{state,saves,testSeeds()};runtime.begin();
        require(sameSavedState(state,expected)&&!needsTestEncounterResolution(state),"publish only exact resolution or unchanged real encounter");
        require(backend.writes==writes+(scenario!=3)+1,"each resolution/world event verified once");
        require(state.rngState==before.rngState&&state.encounterRng==before.encounterRng&&state.explorationSteps==before.explorationSteps&&state.steps==before.steps,"boot resolution draws no game RNG or walking progress");
        require(!std::memcmp(state.collection,before.collection,sizeof(state.collection)),"all collection and care bytes preserved");
        storage::SaveStore restored{backend};State rebooted;
        require(restored.restore(rebooted)==storage::BootStatus::Loaded&&sameSavedState(rebooted,state),"verified result survives restart");
        const auto committed=backend.writes;
        HandheldRuntime again{rebooted,restored,testSeeds()};again.begin();
        require(backend.writes==committed&&sameSavedState(rebooted,state),"restart never duplicates the resolution event");
    }
    // A reported failed write may have landed. RAM remains exact; only the next
    // restore decides which durable event exists. No same-boot automatic retry.
    for(unsigned failure=0;failure<3;++failure) {
        const bool landed=failure!=0;
        fake::reset();State state=installedTestEncounter();ParkSaveBackend backend;storage::SaveStore saves{backend};
        require(saves.restore(state)==storage::BootStatus::Empty&&saves.checkpoint(state),"fault original checkpoint");
        const auto before=state;backend.failBefore=failure==0;backend.failAfter=failure==1;backend.failReadback=failure==2;
        HandheldRuntime runtime{state,saves,testSeeds()};runtime.begin();
        require(sameSavedState(before,state)&&!saves.writable()&&!runtime.allowsCareAction(Action::Attack),"uncertain cleanup stays unpublished and blocks gameplay");
        const auto writes=backend.writes;const auto requests=fake::requests;
        for(unsigned i=0;i<4;++i){fake::now+=20;runtime.poll();}
        for(const char* text:{"feed","walk 100","checkpoint","practice start 1 0 tactical","assets warm","sdput begin","resolve-test-encounter"}) {
            char line[80]{};std::strcpy(line,text);require(runtime.command(line),"recovery consumes every mutation command");
        }
        require(backend.writes==writes&&fake::requests==requests&&sameSavedState(before,state),"recovery polling and commands never retry or mutate");
        char status[]{"status"};require(!runtime.command(status),"read-only status remains available through app dispatcher");
        char reboot[]{"reboot"};bool restarted=false;try{runtime.command(reboot);}catch(const fake::Restart&){restarted=true;}
        require(restarted&&backend.writes==writes,"explicit recovery reboot performs no checkpoint");
        backend.failBefore=backend.failAfter=backend.failReadback=false;storage::SaveStore restored{backend};State after;
        require(restored.restore(after)==storage::BootStatus::Loaded,"own slots restore after uncertainty");
        require(needsTestEncounterResolution(after)!=landed,"restore honors whether uncertain event reached storage");
        HandheldRuntime again{after,restored,testSeeds()};again.begin();
        require(!needsTestEncounterResolution(after)&&backend.writes==writes+(!landed)+1,"reboot resolves missing event and establishes independent world once");
    }
    // A logical event-limit failure still leaves healthy storage and the
    // physical PWR path usable. The recovery latch itself must not block shutdown.
    fake::reset();State exhausted=installedTestEncounter();
    exhausted.sequence=exhausted.foregroundSequence=UINT32_MAX;
    ParkSaveBackend backend;storage::SaveStore saves{backend};
    require(isValid(exhausted)&&saves.restore(exhausted)==storage::BootStatus::Empty&&saves.checkpoint(exhausted),"valid exhausted recovery fixture");
    const auto before=exhausted;const auto writes=backend.writes;
    HandheldRuntime runtime{exhausted,saves,testSeeds()};runtime.begin();
    require(!runtime.allowsCareAction(Action::Attack)&&saves.writable()&&backend.writes==writes,"logical resolution failure preserves healthy storage without speculative write");
    auto tick=[&](unsigned count){for(unsigned i=0;i<count;++i){fake::now+=20;runtime.poll();}};
    tick(5);fake::pressed=true;tick(160);fake::pressed=false;tick(5);
    require(fake::cuts==1&&sameSavedState(before,exhausted),"recovery diagnostic retains normal physical power shutdown without game mutation");
}
void independentWorldStartup() {
    using namespace digivice;
    for(unsigned stage=0;stage<4;++stage) {
        fake::reset();auto state=newDevice();require(apply(state,Action::Hatch,1)==Error::None,"world fixture hatch");
        if(stage) {
            require(apply(state,Action::EncounterSeed,91)==Error::None,"existing gap seed");
            require(apply(state,Action::AccrueSteps,1000)==Error::None,"existing queued foe");
            if(stage>=2)require(apply(state,Action::PresentEncounter)==Error::None,"existing active foe");
            if(stage==3)require(apply(state,Action::AccrueSteps,1000)==Error::None,"second queued foe during match");
        }
        ParkSaveBackend backend;storage::SaveStore saves{backend};require(saves.restore(state)==storage::BootStatus::Empty&&saves.checkpoint(state),"own pre-upgrade save");
        auto expected=state;require(apply(expected,Action::WorldSeed,testSeeds().world)==Error::None,"expected only world initialization");
        const auto writes=backend.writes;HandheldRuntime runtime{state,saves,testSeeds()};runtime.begin();
        require(sameSavedState(state,expected)&&backend.writes==writes+1,"startup changes only explicit world event");
        for(unsigned boot=0;boot<3;++boot) {
            storage::SaveStore reader{backend};State restored;require(reader.restore(restored)==storage::BootStatus::Loaded,"world saved across reboot");
            HandheldRuntime restarted{restored,reader,{91u,92u+boot,93u,94u}};restarted.begin();
            require(sameSavedState(restored,expected)&&backend.writes==writes+1,"new boot entropy never reseeds persistent world");
        }
    }
    for(unsigned failure=0;failure<4;++failure) {
        fake::reset();auto state=newDevice();require(apply(state,Action::Hatch,1)==Error::None,"failure fixture hatch");
        ParkSaveBackend backend;storage::SaveStore saves{backend};require(saves.restore(state)==storage::BootStatus::Empty&&saves.checkpoint(state),"failure original save");
        const auto before=state;
        backend.failBefore=failure==0;backend.failAfter=failure==1;backend.failReadback=failure==2;
        HandheldRuntime runtime{state,saves,failure==3?entropy::Seeds{}:testSeeds()};runtime.begin();
        require(sameSavedState(state,before)&&!runtime.allowsCareAction(Action::Walk),"uncertain or absent entropy blocks gameplay without RAM publication");
        const auto writes=backend.writes;
        for(unsigned tick=0;tick<5;++tick){fake::now+=20;runtime.poll();}
        for(const char* input:{"walk 100","auto","feed","assets warm"}){char line[80]{};std::strcpy(line,input);require(runtime.command(line),"runtime consumes mutation while entropy recovery active");}
        require(sameSavedState(state,before)&&backend.writes==writes,"no same-boot seed retry or progression after failure");
    }
    // A just-hatched native identity cannot bypass initialization through the
    // console dispatcher before the next owner poll.
    fake::reset();auto state=newDevice();ParkSaveBackend backend;storage::SaveStore saves{backend};
    require(saves.restore(state)==storage::BootStatus::Empty&&saves.checkpoint(state),"egg saved");HandheldRuntime runtime{state,saves,testSeeds()};runtime.begin();
    require(apply(state,Action::Hatch,1)==Error::None&&saves.checkpoint(state),"hatch boundary saved");
    const auto before=state;char walk[]{"walk 100"};command(walk,state,saves,runtime.capabilities({}),runtime);
    require(sameSavedState(state,before)&&!runtime.allowsCareAction(Action::Walk),"immediate console walk cannot select shared default sequence");
    runtime.poll();require(state.worldSeed==testSeeds().world&&runtime.allowsCareAction(Action::Walk),"owner poll establishes world before accepting walks");
}
void heldBootAndCancel(){
    fake::reset();fake::pressed=true;Fixture f;f.wait(5000);
    require(fake::cuts==0&&!f.runtime.powerFrozen()&&f.backend.writes==1,"boot-held PWR cannot shut down");
    f.release();f.press(1000);f.release();
    require(fake::cuts==0&&!f.runtime.powerFrozen()&&f.backend.writes==1,"early release cancels without saving");
    f.press(3000);f.release();
    require(fake::cuts==0&&f.backend.writes==1,"hold excludes debounce; releasing at 3s wall time cancels");
}
void shutdownAndResume(){
    fake::reset();Fixture f;f.wait(100);const auto epoch=f.runtime.inputEpoch();
    f.press(3200);
    require(f.runtime.powerFrozen()&&fake::held&&fake::cuts==0,"saved shutdown waits for physical release");
    require(f.backend.writes==2&&fake::syncs==1,"one final checkpoint and one SD sync");
    require(f.runtime.inputEpoch()!=epoch,"shutdown invalidates partial input epoch");
    const auto saved=f.state;const auto writes=f.backend.writes;
    for(const char* text:{"feed","checkpoint","reboot","practice start 1 0 tactical","mode auto","mode confirm","assets warm","net resume"})f.dispatch(text);
    require(f.backend.writes==writes&&fake::practiceWrites==0&&sameSavedState(saved,f.state)&&fake::restarts==0,"shutdown gates every care/practice/reboot mutation path");
    f.release();
    require(!fake::held&&fake::cuts==1&&f.runtime.powerFrozen(),"surviving latch drop enters quiet standby");
    const auto requests=fake::requests;f.wait(3000);
    require(fake::cuts==1&&f.backend.writes==writes&&fake::requests==requests,"standby emits no saves/downloads/repeated cuts");
    require(f.send("power status"),"power status remains reachable");
    const auto frozenEpoch=f.runtime.inputEpoch();f.wake();
    require(fake::held&&!f.runtime.powerFrozen()&&fake::assertions>0,"fresh PWR press/release reasserts latch then resumes");
    require(f.runtime.inputEpoch()!=frozenEpoch&&!fake::networkPaused,"resume clears stale input and restores prior unpaused network");
    f.dispatch("feed");require(f.backend.writes==writes+1,"gameplay returns after safe resume");
    digivice::storage::SaveStore restarted(f.backend);auto restored=digivice::newDevice();
    require(restarted.restore(restored)==digivice::storage::BootStatus::Loaded&&sameSavedState(restored,f.state),"restart restores current verified save");
}
void blockedWorkerAndFailure(){
    fake::reset();Fixture f;fake::assetBusy=fake::networkBusy=true;f.wait(100);f.press(3200);f.release();
    require(fake::cuts==0&&f.backend.writes==1&&fake::syncs==0,"in-flight workers prevent save and cut");
    fake::assetBusy=false;f.wait(200);require(fake::syncs==0&&fake::cuts==0,"HTTP/radio drain still blocks after asset completion");
    fake::networkBusy=false;f.wait(200);require(fake::cuts==1&&f.backend.writes==2&&fake::syncs==1,"cut occurs only after both worker barriers and save");
    fake::reset();Fixture stalled;fake::assetBusy=true;stalled.wait(100);stalled.press(3200);stalled.release();stalled.wait(10500);
    require(fake::held&&fake::cuts==0&&stalled.backend.writes==1,"worker timeout fails without power cut");
    stalled.wake();require(stalled.runtime.powerFrozen(),"wake cannot resume while a timed-out worker still owns I/O");
    fake::assetBusy=false;stalled.wake();require(!stalled.runtime.powerFrozen()&&fake::held,"fresh wake after late worker completion can resume");
}
void barriersRecheckedAtRelease(){
    for(unsigned changed=0;changed<2;++changed){
        fake::reset();Fixture f;f.wait(100);f.press(3200);
        require(fake::held&&f.backend.writes==2&&fake::cuts==0,"saved shutdown still waiting for release");
        if(changed==0)fake::assetBusy=true;else fake::networkFailure=true;
        f.release();
        require(fake::held&&fake::cuts==0&&f.runtime.powerFrozen(),"late I/O barrier change prevents latch cut after save");
    }
}
void saveAndSyncFailures(){
    for(unsigned fault=0;fault<4;++fault){
        fake::reset();Fixture f;
        if(fault==0)f.backend.failBefore=true;
        if(fault==1)f.backend.failAfter=true;
        if(fault==2)fake::syncFailure=true;
        if(fault==3)fake::networkFailure=true;
        f.shutdown();f.wait(200);
        require(fake::held&&fake::cuts==0&&f.runtime.powerFrozen(),"save, sync or radio failure retains latch and frozen work");
        const auto count=f.backend.writes;f.wait(1000);require(f.backend.writes==count,"failed shutdown does not retry writes automatically");
        if(fault<2){require(!f.saves.writable(),"uncertain NVS commit remains recovery gated");f.wake();f.shutdown();require(fake::held&&fake::cuts==0,"retry never bypasses uncertain save gate");}
    }
}
void practiceFailureAndPausePreserved(){
    fake::reset();Fixture f;fake::practiceFailure=true;f.dispatch("practice start 1 0 tactical");
    require(fake::practiceWrites==1,"practice fault exercised through actual session");f.shutdown();
    require(fake::held&&fake::cuts==0,"uncertain practice write blocks shutdown despite healthy care save");
    fake::reset();Fixture paused;paused.dispatch("net pause");paused.shutdown();paused.wake();
    require(fake::held&&!paused.runtime.powerFrozen()&&fake::networkPaused,"standby resume preserves preexisting network pause");
}
void hardwareFailuresAndReleaseRace(){
    fake::reset();Fixture f;fake::cutFailure=true;f.shutdown();
    require(fake::held&&fake::cuts==0&&f.runtime.powerFrozen(),"failed latch write retains work freeze");
    fake::reset();Fixture sense;fake::senseFailure=true;sense.wait(100);
    require(fake::held&&fake::cuts==0&&sense.runtime.powerFrozen(),"sense failure freezes input without dropping latch");
    fake::senseFailure=false;sense.wake();require(!sense.runtime.powerFrozen(),"recovered sense requires fresh wake gesture");
    fake::reset();Fixture race;fake::recheckPressed=true;race.shutdown();
    require(fake::held&&fake::cuts==0,"raw re-press during final latch recheck prevents cut");
    fake::recheckPressed=false;race.release();require(fake::cuts==1,"stable later release may cut");
    fake::assertFailure=true;race.wake();
    require(race.runtime.powerFrozen(),"failed hold reassertion cannot resume gameplay");
    const auto writes=race.backend.writes;race.dispatch("feed");require(race.backend.writes==writes,"failed resume keeps command gate closed");
    fake::assertFailure=false;race.wake();require(fake::held&&!race.runtime.powerFrozen(),"recovered reassertion resumes only with fresh gesture");
}
void partialConsoleInput(){
    fake::reset();Fixture f;f.wait(100);
    int descriptors[2];require(::pipe(descriptors)==0,"console pipe");
    const int original=::dup(STDIN_FILENO);require(original>=0,"save stdin");
    require(::fcntl(descriptors[0],F_SETFL,O_NONBLOCK)==0&&::dup2(descriptors[0],STDIN_FILENO)>=0,"nonblocking test console");
    std::clearerr(stdin);require(::write(descriptors[1],"walk ",5)==5,"partial command queued");
    fake::pressed=true;const auto start=fake::now;
    fake::delayHook=[&]{
        const auto elapsed=fake::now-start;
        if(elapsed>=3200)fake::pressed=false;
        if(elapsed>=3500&&elapsed<3700)fake::pressed=true;
        if(elapsed>=4000){require(::write(descriptors[1],"100\nstatus\n",11)==11,"command remainder and fresh line queued");fake::delayHook={};}
    };
    char line[384]{};const bool complete=readLine(line,sizeof(line),f.runtime);
    ::dup2(original,STDIN_FILENO);::close(original);::close(descriptors[0]);::close(descriptors[1]);std::clearerr(stdin);
    require(complete&&std::strcmp(line,"status")==0,"actual readLine discards partial pre-shutdown command and its suffix through newline");
    require(fake::held&&!f.runtime.powerFrozen()&&f.state.steps==0,"console scenario actually shut down/resumed without executing stale walk");
}
void pendingStepsAndResumeAnchor(){
    fake::reset();fake::motionEnabled=true;Fixture f;f.wait(1000);fake::motionCount=2;f.wait(1000);
    require(f.state.steps==0,"motion batch remains pending before usual interval");f.shutdown();
    require(f.state.steps==2&&fake::cuts==1,"shutdown forces lawful confirmed-step checkpoint before cut");
    fake::motionCount=500;f.wake();f.wait(1000);require(f.state.steps==2,"resume anchors hardware count and discards standby gap");
    fake::motionCount=502;f.wait(1000);f.shutdown();require(f.state.steps==4,"post-resume real delta credited exactly once");
    fake::reset();fake::motionEnabled=true;Fixture blocked;blocked.dispatch("practice start 1 0 tactical");blocked.wait(1000);fake::motionCount=2;blocked.wait(1000);blocked.shutdown();
    require(fake::held&&fake::cuts==0&&blocked.state.steps==0,"practice-blocked confirmed steps refuse cut without applying illegal Walk");
}
void readOnlySdDiagnostics(){
    fake::reset();Fixture f;
    const auto before=f.state;const auto writes=f.backend.writes;
    require(f.send("art inventory")&&f.send("art probe"),"idle serial SD diagnostics are recognized without a display dependency");
    require(fake::inventoryCalls==1&&fake::probeCalls==1&&fake::inspectionBarrier,"diagnostics obtain paused asset I/O barrier before reading");
    require(!fake::assetPaused&&!fake::networkPaused&&!f.runtime.powerFrozen(),"idle diagnostics restore prior unpaused states");
    require(f.backend.writes==writes&&std::memcmp(&before,&f.state,sizeof(before))==0,"diagnostics preserve complete game state and do not checkpoint");
    fake::assetBusy=true;f.send("art inventory");f.send("art probe");
    require(fake::inventoryCalls==1&&fake::probeCalls==1&&!fake::assetPaused,"active worker refuses inspection without changing its pause state");
    fake::assetBusy=false;fake::refuseInspectionBarrier=true;f.send("art probe");
    require(fake::probeCalls==1&&!fake::assetPaused,"failed barrier refuses probe and restores previous pause state");
    fake::refuseInspectionBarrier=false;f.dispatch("net pause");f.send("art probe");
    require(fake::probeCalls==2&&fake::networkPaused&&!fake::assetPaused,"diagnostics preserve explicit network pause independently");
    f.shutdown();f.send("art inventory");f.send("art probe");
    require(fake::inventoryCalls==1&&fake::probeCalls==2,"power transition still blocks all SD diagnostics");
    fake::reset();fake::assetsInitiallyPaused=true;Fixture paused;
    paused.send("art inventory");paused.send("art probe");
    require(fake::inventoryCalls==1&&fake::probeCalls==1&&fake::inspectionBarrier&&fake::assetPaused,"preexisting asset pause remains set after both diagnostics");
}
void usbTransferLease(){
    fake::reset();Fixture f;f.wait(100);
    const auto saved=f.state;const auto writes=f.backend.writes;const auto requests=fake::requests;
    require(f.send("sdput status")&&fake::transferHandles==1,"idle status acquires USB transfer lease");
    require(fake::assetPaused&&fake::transferBarrier&&!f.runtime.powerFrozen(),"idle lease pauses competing assets without a power transition");
    for(const char* text:{"feed","checkpoint","reboot","practice start 1 0 tactical","mode auto","mode confirm","assets warm","assets fetch mote","art inventory","art probe","net resume"})f.dispatch(text);
    f.wait(1000);
    require(sameSavedState(saved,f.state)&&f.backend.writes==writes&&fake::practiceWrites==0&&fake::restarts==0,"lease blocks gameplay, checkpoint, practice and reboot fallback");
    require(fake::inventoryCalls==0&&fake::probeCalls==0&&fake::requests==requests,"lease blocks SD inventory, probe and asset work");
    require(f.send("device identity")&&f.send("power status"),"read-only identity and power status remain available");
    const auto epoch=f.runtime.inputEpoch();
    require(f.send("sdput \tabort   ")&&!fake::assetPaused,"accepted whitespace abort releases the USB transfer lease");
    require(f.runtime.inputEpoch()!=epoch,"transfer exit invalidates stale console input");
    f.dispatch("feed");require(f.backend.writes==writes+1,"fresh gameplay resumes after explicit abort");
    fake::assetBusy=true;const auto handles=fake::transferHandles;f.send("sdput status");
    require(fake::transferHandles==handles&&!fake::assetPaused,"busy asset worker refuses lease without entering engine or changing pause");
    fake::assetBusy=false;fake::refuseInspectionBarrier=true;f.send("sdput status");
    require(fake::transferHandles==handles&&!fake::assetPaused,"failed I/O barrier restores pause without entering engine");
}
void usbTransferTimeoutAndPauseRestoration(){
    for(bool initiallyPaused:{false,true}){
        fake::reset();fake::assetsInitiallyPaused=initiallyPaused;Fixture f;f.wait(100);
        f.send("sdput begin");f.send("sdput abort");
        require(fake::assetPaused==initiallyPaused,"abort restores preexisting asset pause");
        f.send("sdput status");const auto epoch=f.runtime.inputEpoch();const auto writes=f.backend.writes;
        f.tick(59999);require(fake::assetPaused,"lease stays held before 60-second idle deadline");
        f.tick(1);
        require(fake::assetPaused==initiallyPaused&&f.runtime.inputEpoch()!=epoch,"idle timeout releases lease and restores prior pause");
        f.dispatch("feed");require(f.backend.writes==writes+1,"timeout leaves fresh gameplay usable");
        f.send("sdput status");f.tick(59000);f.send("sdput status");f.tick(2000);
        const auto refreshedWrites=f.backend.writes;f.dispatch("feed");
        require(f.backend.writes==refreshedWrites&&fake::assetPaused,"transfer command refreshes the idle lease deadline");
        f.send("sdput abort");
    }
}
void usbTransferDelayedAcquisition(){
    // The display-off harness models the common acquisition predicate through
    // the asset barrier. Physical audio/display acknowledgment is separate.
    for(bool initiallyPaused:{false,true})for(unsigned readyAfter:{30u,250u,260u}){
        fake::reset();fake::assetsInitiallyPaused=initiallyPaused;Fixture f;f.wait(100);
        fake::refuseInspectionBarrier=true;
        const auto start=fake::now;const auto ticks=fake::networkTicks;
        const auto writes=f.backend.writes;const auto saved=f.state;
        unsigned waits=0;bool onlyWaiting=true;
        fake::delayHook=[&]{
            ++waits;
            onlyWaiting&=fake::assetPaused&&fake::transferHandles==0&&fake::networkTicks==ticks&&
                fake::syncs==0&&fake::inventoryCalls==0&&fake::probeCalls==0&&f.backend.writes==writes;
            if(fake::now-start>=readyAfter)fake::refuseInspectionBarrier=false;
        };
        require(f.send("sdput status"),"delayed-barrier transfer command remains recognized");
        fake::delayHook={};
        const auto elapsed=fake::now-start;
        require(onlyWaiting&&sameSavedState(saved,f.state),"acquisition wait retains pause without engine entry, recursive runtime polling, SD inspection or save writes");
        if(readyAfter<=250){
            require(elapsed==readyAfter&&waits==readyAfter/10,"acquisition waits in 10 ms steps only until barrier acknowledgment");
            require(fake::transferHandles==1&&fake::transferBarrier&&fake::assetPaused,"barrier ready within bound permits exactly one transfer dispatch");
            f.dispatch("feed");require(f.backend.writes==writes,"successful delayed acquisition keeps the lease exclusive");
            f.send("sdput abort");
            require(fake::assetPaused==initiallyPaused,"abort after delayed acknowledgment restores prior asset pause");
        }else{
            require(elapsed==250&&waits==25&&fake::transferHandles==0,"unacknowledged barrier times out at 250 ms without transfer access");
            require(fake::assetPaused==initiallyPaused&&!f.runtime.powerFrozen(),"barrier timeout restores previous pause state without claiming a lease");
            f.dispatch("feed");require(f.backend.writes==writes+1,"timeout does not leave gameplay locked");
            fake::refuseInspectionBarrier=false;const auto retryAt=fake::now;
            f.send("sdput status");
            require(fake::transferHandles==1&&fake::now==retryAt,"a later ready barrier permits a fresh immediate acquisition");
            f.send("sdput abort");
        }
    }
}
void usbTransferAbortWithoutSd(){
    for(bool loseMount:{false,true})for(bool initiallyPaused:{false,true}){
        fake::reset();fake::assetsInitiallyPaused=initiallyPaused;Fixture f;f.wait(100);
        const auto saved=f.state;const auto writes=f.backend.writes;
        require(f.send("sdput begin")&&fake::assetPaused,"transfer starts before simulated SD loss");
        if(loseMount)fake::sdMounted(false);else fake::sdReady=false;
        const auto handles=fake::transferHandles;const auto epoch=f.runtime.inputEpoch();
        f.send("sdput chunk 0 aa");
        require(fake::transferHandles==handles&&fake::assetPaused,"SD loss blocks further file commands while retaining lease");
        f.dispatch("feed");require(f.backend.writes==writes,"SD loss does not silently release gameplay exclusion");
        require(f.send("sdput \tabort   "),"explicit abort remains recognized after mount or readiness loss");
        require(fake::assetPaused==initiallyPaused&&f.runtime.inputEpoch()!=epoch,"abort without SD releases lease and restores original asset pause");
        require(sameSavedState(saved,f.state)&&f.backend.writes==writes,"abort without SD preserves save and game state");
        f.dispatch("feed");require(f.backend.writes==writes+1,"local gameplay resumes without waiting for SD recovery or timeout");
    }
}
void usbTransferPowerBarriers(){
    for(bool initiallyPaused:{false,true}){
        fake::reset();fake::assetsInitiallyPaused=initiallyPaused;Fixture f;f.send("sdput begin");
        const auto pauses=fake::transferPauses;f.shutdown();
        require(fake::transferPauses>pauses&&fake::cuts==1&&f.backend.writes==2,"power request closes transfer before final save and cut");
        const auto handles=fake::transferHandles;f.send("sdput begin");
        require(fake::transferHandles==handles,"power transition rejects new transfer work");
        f.wake();require(!f.runtime.powerFrozen()&&fake::assetPaused==initiallyPaused,"power resume restores asset pause from before transfer lease");
        f.send("sdput status");require(fake::transferHandles==handles+1,"transfer may start fresh after power resume");f.send("sdput abort");
        f.shutdown();f.wake();
        require(fake::assetPaused==initiallyPaused,"power resume without a lease preserves existing asset pause too");
    }
    for(bool pauseFailure:{false,true}){
        fake::reset();Fixture f;f.send("sdput begin");
        if(pauseFailure)fake::transferPauseFailure=true;else fake::transferState(true,true);
        f.shutdown();f.wait(10500);
        require(fake::cuts==0&&fake::held&&f.backend.writes==1&&fake::syncs==0,"busy engine or failed pause blocks SD sync, final save and cut");
        f.wake();require(f.runtime.powerFrozen(),"unreleased transfer ownership cannot resume after shutdown timeout");
    }
    fake::reset();Fixture late;late.wait(100);late.press(3200);
    require(late.backend.writes==2&&fake::cuts==0,"shutdown saved and waits for release");
    fake::transferState(false,true);late.release();
    require(fake::cuts==0&&fake::held,"late transfer busy state is rechecked before latch cut");
}
void usbTransferConsoleFrames(){
    fake::reset();Fixture f;f.wait(100);ConsolePipe pipe;
    const std::string frame="sdput chunk 0 "+std::string(1024,'a');
    pipe.write(frame.substr(0,300));
    fake::delayHook=[&]{pipe.write(frame.substr(300)+"\n");fake::delayHook={};};
    char line[1536]{};
    require(readLine(line,sizeof(line),f.runtime)&&line==frame,"fragmented 512-byte hex payload fits the actual enlarged line reader");
    require(f.runtime.command(line)&&fake::transferLineBytes==frame.size(),"full transfer frame reaches runtime without truncation");
    pipe.write(std::string(sizeof(line),'x')+"\nstatus\n");
    require(!readLine(line,sizeof(line),f.runtime),"overlong transfer-era line is rejected in full");
    require(readLine(line,sizeof(line),f.runtime)&&!std::strcmp(line,"status"),"next line survives overflow without executing a suffix");
    f.send("sdput abort");
}
void usbTransferStaleConsoleInput(){
    fake::reset();Fixture f;f.wait(100);f.send("sdput status");ConsolePipe pipe;
    const auto epoch=f.runtime.inputEpoch();const auto start=fake::now;
    pipe.write("walk ");
    fake::delayHook=[&]{
        // A large host-clock step keeps the timeout test bounded while the real
        // reader still observes the incomplete line and pumps runtime.poll().
        if(fake::now-start<60000)fake::now=start+60000;
        else {pipe.write("100\nfeed\n");fake::delayHook={};}
    };
    char line[1536]{};
    require(readLine(line,sizeof(line),f.runtime)&&!std::strcmp(line,"feed"),"lease timeout discards a partial command and its suffix through newline");
    require(f.runtime.inputEpoch()!=epoch&&!fake::assetPaused&&f.state.steps==0,"timeout restores I/O without executing stale walk input");
    const auto writes=f.backend.writes;f.dispatch(line);
    require(f.backend.writes==writes+1&&f.state.steps==0,"fresh post-timeout command executes exactly once");
}
void usbTransferHandshakeClearsPartialGameplay(){
    fake::reset();Fixture f;f.wait(100);ConsolePipe pipe;
    const auto saved=f.state;const auto writes=f.backend.writes;
    // A newline here would submit this complete but unterminated action. The
    // installer's first frame must overflow it before sending any delimiter.
    pipe.write("walk 100");
    fake::delayHook=[&]{pipe.write(std::string(1536,'~')+"\nstatus\n");fake::delayHook={};};
    char line[1536]{};
    const bool complete=readLine(line,sizeof(line),f.runtime);
    if(complete)f.dispatch(line); // Same dispatch condition as app_main.
    require(!complete,"1536 printable handshake bytes overflow buffered gameplay without submitting it");
    require(sameSavedState(saved,f.state)&&f.backend.writes==writes&&fake::transferHandles==0,"handshake performs no game action, checkpoint or transfer command");
    require(readLine(line,sizeof(line),f.runtime)&&!std::strcmp(line,"status"),"clean command follows the rejected handshake line");
    f.dispatch(line);
    require(sameSavedState(saved,f.state)&&f.backend.writes==writes,"post-handshake status does not execute the stale walk");
}
}
int main(){try{
    boundedDiagnosticMemory();
    consoleAutoWaitsForFlick();
    startupOffersAreDurable();
    startupTestEncounterResolution();
    independentWorldStartup();
    heldBootAndCancel();shutdownAndResume();blockedWorkerAndFailure();barriersRecheckedAtRelease();saveAndSyncFailures();practiceFailureAndPausePreserved();pendingStepsAndResumeAnchor();hardwareFailuresAndReleaseRace();partialConsoleInput();readOnlySdDiagnostics();
    usbTransferLease();usbTransferTimeoutAndPauseRestoration();usbTransferDelayedAcquisition();usbTransferAbortWithoutSd();usbTransferPowerBarriers();usbTransferConsoleFrames();usbTransferStaleConsoleInput();usbTransferHandshakeClearsPartialGameplay();
    std::printf("PASS %u power/USB lease integration checks (actual runtime/core, SDK/storage/radio/transfer I/O doubles; no board claim).\n",checks);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL power integration: %s\n",error.what());return 1;}}

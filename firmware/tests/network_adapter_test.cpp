// Execute the actual adapter. SDK callbacks/queues/transport are controlled
// doubles; these tests establish ownership barriers, not physical radio timing.
#include "../main/network_adapter.cpp"
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
int checks=0;
void check(bool yes,const char* name){++checks;if(!yes)throw std::runtime_error(name);}
struct Queue {unsigned capacity,size;std::deque<std::vector<std::uint8_t>> items;};
std::map<void*,Queue> queues;
void(*workerFunction)(void*)=nullptr;void* workerContext=nullptr;
struct WorkerDone {};
esp_event_handler_t eventFunction=nullptr;void* eventContext=nullptr;
std::int64_t clockUs=0;
unsigned starts=0,stops=0,connects=0,httpOpens=0,httpCleanups=0,nvsWrites=0;
bool stopFails=false;
unsigned scanStarts=0,scanStops=0,scanClears=0;
std::vector<wifi_ap_record_t> scanRecords;
std::time_t wallClock=1735689600;
unsigned clockStarts=0,clockStops=0;
wifi_config_t lastConfig{};
std::function<void()> httpHook;
void event(esp_event_base_t base,int id){eventFunction(eventContext,base,id,nullptr);}
void runWorker(){try{workerFunction(workerContext);}catch(const WorkerDone&) {}}
digivice::net::Config config(){digivice::net::Config value{};std::strcpy(value.ssid,"test-ap");std::strcpy(value.password,"development-only");std::strcpy(value.endpoint,"https://example.test");return value;}
void connect(digivice::net::NetworkAdapter& adapter){adapter.tick();event(WIFI_EVENT,WIFI_EVENT_STA_START);adapter.tick();event(IP_EVENT,IP_EVENT_STA_GOT_IP);adapter.tick();}
}
QueueHandle_t xQueueCreateStatic(UBaseType_t count,UBaseType_t n,std::uint8_t*,StaticQueue_t*q){queues[q]={count,n,{}};return q;}
BaseType_t xQueueSend(QueueHandle_t q,const void*p,TickType_t){auto& x=queues.at(q);if(x.items.size()==x.capacity)return 0;auto* b=static_cast<const std::uint8_t*>(p);x.items.emplace_back(b,b+x.size);return pdTRUE;}
BaseType_t xQueueReceive(QueueHandle_t q,void*p,TickType_t wait){auto& x=queues.at(q);if(x.items.empty()){if(wait==portMAX_DELAY)throw WorkerDone{};return 0;}std::memcpy(p,x.items.front().data(),x.size);x.items.pop_front();return pdTRUE;}
BaseType_t xQueueOverwrite(QueueHandle_t q,const void*p){auto& x=queues.at(q);x.items.clear();return xQueueSend(q,p,0);}
BaseType_t xTaskCreate(void(*f)(void*),const char*,std::uint32_t,void*p,UBaseType_t,TaskHandle_t*t){workerFunction=f;workerContext=p;*t=p;return pdPASS;}
esp_err_t nvs_flash_init(){return ESP_OK;}
esp_err_t nvs_open(const char*,int,nvs_handle_t*p){*p=1;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,std::size_t*){return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,std::size_t){++nvsWrites;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t){return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t,const char*){++nvsWrites;return ESP_OK;}
esp_err_t esp_netif_init(){return ESP_OK;}
esp_netif_t* esp_netif_create_default_wifi_sta(){static esp_netif_t interface;return &interface;}
esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t*p){p->ip.addr=1;return ESP_OK;}
esp_err_t esp_event_loop_create_default(){return ESP_OK;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t,int,esp_event_handler_t f,void*p,esp_event_handler_instance_t*h){eventFunction=f;eventContext=p;*h=p;return ESP_OK;}
esp_err_t esp_wifi_init(const wifi_init_config_t*){return ESP_OK;}
esp_err_t esp_wifi_set_storage(int){return ESP_OK;}
esp_err_t esp_wifi_set_mode(int){return ESP_OK;}
esp_err_t esp_wifi_set_config(int,const wifi_config_t*p){lastConfig=*p;return ESP_OK;}
esp_err_t esp_wifi_start(){++starts;return ESP_OK;}
esp_err_t esp_wifi_stop(){++stops;return stopFails?ESP_FAIL:ESP_OK;}
esp_err_t esp_wifi_connect(){++connects;return ESP_OK;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*p){std::memset(p,0,sizeof(*p));std::memcpy(p->ssid,lastConfig.sta.ssid,32);return ESP_OK;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*,bool blocking){check(!blocking,"scan never blocks owner task");++scanStarts;return ESP_OK;}
esp_err_t esp_wifi_scan_stop(){++scanStops;return ESP_OK;}
esp_err_t esp_wifi_clear_ap_list(){++scanClears;return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_records(std::uint16_t*n,wifi_ap_record_t*p){*n=std::min<std::size_t>(*n,scanRecords.size());for(unsigned i=0;i<*n;++i)p[i]=scanRecords[i];return ESP_OK;}
extern "C" std::time_t time(std::time_t* result){if(result)*result=wallClock;return wallClock;}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t*p){check(p->start&&p->wait_for_sync&&p->num_of_servers==1,"one asynchronous time client");++clockStarts;return ESP_OK;}
void esp_netif_sntp_deinit(){++clockStops;}
esp_err_t esp_netif_sntp_sync_wait(TickType_t wait){check(wait==0,"time status poll never blocks");return ESP_ERR_TIMEOUT;}
std::int64_t esp_timer_get_time(){return clockUs;}
std::uint32_t esp_random(){return 1;}
esp_err_t esp_crt_bundle_attach(void*){return ESP_OK;}
struct FakeHttp {};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*){static FakeHttp handle;return &handle;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int){return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t,int){++httpOpens;if(httpHook){auto hook=std::move(httpHook);httpHook={};hook();}return ESP_FAIL;}
std::int64_t esp_http_client_fetch_headers(esp_http_client_handle_t){return -1;}
int esp_http_client_get_status_code(esp_http_client_handle_t){return 500;}
int esp_http_client_read(esp_http_client_handle_t,char*,int){return -1;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t){return false;}
esp_err_t esp_http_client_close(esp_http_client_handle_t){return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t){++httpCleanups;return ESP_OK;}

int main(){try{
    using digivice::net::NetworkAdapter;
    NetworkAdapter adapter;
    check(adapter.begin()==ESP_OK,"unconfigured adapter starts");
    check(adapter.quiescence()==ESP_ERR_INVALID_STATE,"idle is not a frozen barrier");
    adapter.pause(true);adapter.tick();
    check(adapter.quiescence()==ESP_OK&&starts==0,"unconfigured pause has no radio work");
    check(adapter.configure(config())==ESP_ERR_INVALID_STATE&&adapter.forget()==ESP_ERR_INVALID_STATE&&nvsWrites==0,"pause freezes NVS writes");
    adapter.pause(false);check(adapter.configure(config())==ESP_OK,"resume allows setup");
    adapter.tick();check(starts==1,"radio start pending");
    adapter.pause(true);check(stops==1&&adapter.quiescence()==ESP_ERR_NOT_FINISHED,"pause interrupts in-progress start");
    event(WIFI_EVENT,WIFI_EVENT_STA_START);adapter.tick();
    check(connects==0&&adapter.quiescence()==ESP_ERR_NOT_FINISHED,"late START cannot join while paused");
    event(WIFI_EVENT,WIFI_EVENT_STA_STOP);adapter.tick();check(adapter.quiescence()==ESP_OK,"STOP acknowledgement grants idle");
    adapter.pause(false);connect(adapter);check(connects==1,"resume rejoins");
    adapter.pause(true);event(WIFI_EVENT,WIFI_EVENT_STA_STOP);adapter.tick();
    check(adapter.quiescence()==ESP_ERR_NOT_FINISHED,"queued HTTP reservation survives pause");
    runWorker();adapter.tick();check(adapter.quiescence()==ESP_OK&&httpOpens==0,"cancelled queued job drains without HTTP");
    adapter.pause(false);connect(adapter);
    httpHook=[&]{adapter.pause(true);event(WIFI_EVENT,WIFI_EVENT_STA_STOP);adapter.tick();check(adapter.quiescence()==ESP_ERR_NOT_FINISHED&&httpCleanups==0,"active HTTP owns reservation even after radio STOP");};
    runWorker();adapter.tick();check(adapter.quiescence()==ESP_OK&&httpCleanups==1,"HTTP cleanup releases final reservation");
    const auto before=starts;adapter.tick();adapter.retry();adapter.tick();check(starts==before,"paused retry never restarts radio");
    adapter.pause(false);connect(adapter);runWorker();adapter.tick();
    check(adapter.forget()==ESP_OK,"forget can leave an asynchronous radio stop");
    adapter.pause(true);check(adapter.quiescence()==ESP_ERR_NOT_FINISHED,"unconfigured controller still waits for physical radio stop");
    event(WIFI_EVENT,WIFI_EVENT_STA_STOP);adapter.tick();check(adapter.quiescence()==ESP_OK,"forgotten network drains normally");
    adapter.pause(false);check(adapter.configure(config())==ESP_OK,"configure after resume");adapter.tick();
    stopFails=true;adapter.pause(true);check(adapter.quiescence()==ESP_FAIL,"failed radio stop never claims quiet");
    stopFails=false;
    NetworkAdapter timeout;check(timeout.begin()==ESP_OK&&timeout.configure(config())==ESP_OK,"timeout fixture starts");timeout.tick();timeout.pause(true);clockUs+=5000000;timeout.tick();
    check(timeout.quiescence()==ESP_FAIL,"lost STOP event fails closed at deadline");
    NetworkAdapter activeFailure;check(activeFailure.begin()==ESP_OK&&activeFailure.configure(config())==ESP_OK,"active failure fixture starts");connect(activeFailure);
    httpHook=[&]{stopFails=true;activeFailure.pause(true);check(activeFailure.quiescence()==ESP_ERR_NOT_FINISHED,"failed radio still preserves active HTTP ownership");};
    runWorker();check(activeFailure.quiescence()==ESP_FAIL,"terminal radio error only reported after HTTP cleanup");stopFails=false;
    NetworkAdapter scanning;check(scanning.begin()==ESP_OK,"scan fixture starts offline");
    const auto writesBeforeScan=nvsWrites,connectionsBeforeScan=connects;
    check(scanning.startScan()==ESP_OK&&scanning.scanStatus().busy,"scan request is asynchronous");
    scanning.tick();event(WIFI_EVENT,WIFI_EVENT_STA_START);scanning.tick();
    check(scanStarts==1&&connects==connectionsBeforeScan,"scan starts radio without joining");
    wifi_ap_record_t ap{};std::strcpy(reinterpret_cast<char*>(ap.ssid),"synthetic-hotspot");ap.rssi=-35;ap.authmode=WIFI_AUTH_WPA2_PSK;
    scanRecords={ap,ap};ap.ssid[0]=0;scanRecords.push_back(ap);
    event(WIFI_EVENT,WIFI_EVENT_SCAN_DONE);scanning.tick();
    check(scanning.scanStatus().busy,"scan completion still waits physical STOP");
    event(WIFI_EVENT,WIFI_EVENT_STA_STOP);scanning.tick();
    check(!scanning.scanStatus().busy&&scanning.scanStatus().count==1&&scanning.scanStatus().accessPoints[0].supported,"bounded scan removes duplicates and blank SSIDs");
    check(nvsWrites==writesBeforeScan&&scanClears>0,"scan frees results without persisting network identifiers");
    check(scanning.startScan()==ESP_OK,"next scan allowed after stop barrier");scanning.tick();
    const bool previousExplicitPause=scanning.requestedPaused();
    check(!previousExplicitPause&&scanning.status().paused,"scan pause is distinct from owner pause");
    scanning.cancelScan();scanning.pause(true);
    event(WIFI_EVENT,WIFI_EVENT_STA_START);scanning.tick();
    check(scanning.quiescence()==ESP_ERR_NOT_FINISHED&&scanStarts==1,"paused late START cannot scan");
    event(WIFI_EVENT,WIFI_EVENT_STA_STOP);scanning.tick();check(scanning.quiescence()==ESP_OK,"cancelled scan shutdown waits STOP");
    scanning.pause(previousExplicitPause);scanning.tick();
    check(!scanning.requestedPaused()&&!scanning.status().paused,"power resume restores explicit unpaused state after interrupted scan");
    check(scanning.startScan()==ESP_OK,"setup remains usable after scan power cancellation and resume");
    scanning.cancelScan();scanning.tick();
    NetworkAdapter wifiOnly;check(wifiOnly.begin()==ESP_OK,"Wi-Fi-only fixture begins");
    check(wifiOnly.configureWifi("synthetic-hotspot","synthetic-password")==ESP_OK,"Wi-Fi-only credentials accepted");
    const auto oldHttp=httpOpens;connect(wifiOnly);runWorker();wifiOnly.tick();
    check(wifiOnly.status().hasIp&&!wifiOnly.status().serviceReachable&&wifiOnly.endpoint()[0]==0&&httpOpens==oldHttp,"Wi-Fi-only never fabricates or probes service origin");
    const auto savedWrites=nvsWrites;
    check(wifiOnly.configureEndpoint("https://example.test/secret/path",false)==ESP_ERR_INVALID_ARG&&nvsWrites==savedWrites,"invalid origin does not overwrite credentials");
    check(wifiOnly.configureEndpoint("https://example.test",false)==ESP_OK,"separate origin update preserves stored Wi-Fi credentials");
    NetworkAdapter clocked;wallClock=0;
    check(clocked.begin()==ESP_OK&&clocked.configure(config())==ESP_OK,"unset-clock fixture begins");
    const auto noClockHttp=httpOpens;connect(clocked);runWorker();
    check(clocked.clockState()==digivice::net::ClockState::Waiting&&clockStarts==1&&httpOpens==noClockHttp,"HTTPS waits while clock bootstrap runs");
    clockUs+=15000000;clocked.tick();
    check(clocked.clockState()==digivice::net::ClockState::Failed&&clockStops==1,"clock attempt ends at bounded deadline");
    for(unsigned attempt=0;attempt<2;++attempt){clockUs+=60000000;clocked.tick();clockUs+=15000000;clocked.tick();}
    const auto finite=clockStarts;clockUs+=600000000;clocked.tick();
    check(finite==3&&clockStarts==finite,"clock retries are finite");
    clocked.retry();clocked.tick();check(clockStarts==4,"explicit retry can restart clock bootstrap");
    wallClock=1735689600;clocked.tick();
    check(clocked.clockState()==digivice::net::ClockState::Ready,"valid clock enables separate service probe");
    clocked.pause(true);event(WIFI_EVENT,WIFI_EVENT_STA_STOP);clocked.tick();runWorker();clocked.tick();
    check(clocked.quiescence()==ESP_OK,"time client joins radio and HTTP shutdown barrier");
    // Exclusive Nearby lease preserves configuration and owns Wi-Fi transitions.
    NetworkAdapter leased;check(leased.begin()==ESP_OK&&leased.configure(config())==ESP_OK,"lease fixture configured");
    check(leased.beginRadioLease()==ESP_ERR_INVALID_STATE,"lease requires explicit pause");
    leased.pause(true);leased.tick();check(leased.beginRadioLease()==ESP_OK&&leased.radioLeased(),"paused quiescent driver grants exclusive lease");
    const auto leaseStops=stops,leaseConnects=connects,leaseWrites=nvsWrites;
    check(leased.quiescence()==ESP_ERR_NOT_FINISHED,"lease prevents power-off quiescence before borrower cleanup");
    event(WIFI_EVENT,WIFI_EVENT_STA_START);leased.tick();
    check(stops==leaseStops&&connects==leaseConnects,"borrowed START neither stops nor joins hotspot");
    event(IP_EVENT,IP_EVENT_STA_GOT_IP);event(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED);leased.tick();
    check(!leased.status().hasIp&&stops==leaseStops,"borrowed old IP/disconnect cannot mutate network FSM");
    leased.pause(false);leased.retry();leased.pause(true);leased.tick();
    check(leased.requestedPaused()&&stops==leaseStops&&connects==leaseConnects,"pause/resume/retry cannot steal leased radio including power pause");
    check(leased.configure(config())==ESP_ERR_INVALID_STATE&&leased.forget()==ESP_ERR_INVALID_STATE&&leased.startScan()==ESP_ERR_INVALID_STATE&&nvsWrites==leaseWrites,"all network controls reject while leased");
    check(leased.releaseRadioLease(true)==ESP_ERR_NOT_FINISHED,"borrower end still waits STOP acknowledgement");
    event(WIFI_EVENT,WIFI_EVENT_STA_STOP);check(leased.releaseRadioLease(true)==ESP_OK&&!leased.radioLeased(),"ordered borrowed STOP releases lease");
    check(leased.quiescence()==ESP_OK&&leased.requestedPaused(),"release retains previous explicit pause until caller restores it");
    leased.pause(false);connect(leased);runWorker();leased.tick();
    check(connects==leaseConnects+1&&std::strcmp(reinterpret_cast<char*>(lastConfig.sta.ssid),"test-ap")==0,"hotspot configuration rejoins after explicit restore");
    // Setup can fail before Wi-Fi starts, or after start+cleanup before events arrive.
    NetworkAdapter early;check(early.begin()==ESP_OK,"partial lease fixture");early.pause(true);
    check(early.beginRadioLease()==ESP_OK&&early.releaseRadioLease(false)==ESP_OK,"pre-start borrower failure needs no fabricated STOP");
    check(early.beginRadioLease()==ESP_OK,"partial after-start lease");event(WIFI_EVENT,WIFI_EVENT_STA_START);event(WIFI_EVENT,WIFI_EVENT_STA_STOP);
    check(early.releaseRadioLease(true)==ESP_OK,"queued START/STOP drain together after partial setup cleanup");
    check(early.beginRadioLease()==ESP_OK&&early.releaseRadioLease(true)==ESP_ERR_NOT_FINISHED,"release before delayed START remains pending");
    event(WIFI_EVENT,WIFI_EVENT_STA_START);early.tick();check(early.releaseRadioLease(true)==ESP_ERR_NOT_FINISHED,"delayed START alone cannot clear STOP barrier");
    event(WIFI_EVENT,WIFI_EVENT_STA_STOP);check(early.releaseRadioLease(true)==ESP_OK,"late ordered STOP finishes barrier");
    // Ordinary stale START queued before borrowing must be stopped before lease.
    event(WIFI_EVENT,WIFI_EVENT_STA_START);const auto oldStopCount=stops;
    check(early.beginRadioLease()==ESP_ERR_NOT_FINISHED&&!early.radioLeased()&&stops==oldStopCount+1,"queued old START cannot leak into new owner");
    event(WIFI_EVENT,WIFI_EVENT_STA_STOP);early.tick();check(early.beginRadioLease()==ESP_OK,"old STOP drains before borrowing");
    check(early.releaseRadioLease(false)==ESP_OK,"unused borrowed driver releases");
    NetworkAdapter missingStart;check(missingStart.begin()==ESP_OK,"missing START fixture");missingStart.pause(true);check(missingStart.beginRadioLease()==ESP_OK,"missing START lease");
    const auto noForcedStop=stops;clockUs+=5000000;missingStart.tick();
    check(missingStart.recoveryRequired()&&missingStart.quiescence()==ESP_FAIL&&stops==noForcedStop,"missing START fails bounded without stopping active ESP-NOW behind borrower");
    check(missingStart.releaseRadioLease(true)==ESP_FAIL&&missingStart.radioLeased(),"ambiguous missing event cannot restore hotspot");
    NetworkAdapter missingStop;check(missingStop.begin()==ESP_OK,"missing STOP fixture");missingStop.pause(true);check(missingStop.beginRadioLease()==ESP_OK,"missing STOP lease");
    event(WIFI_EVENT,WIFI_EVENT_STA_START);missingStop.tick();check(missingStop.releaseRadioLease(true)==ESP_ERR_NOT_FINISHED,"missing STOP initially pending");
    clockUs+=5000000;check(missingStop.releaseRadioLease(true)==ESP_FAIL&&missingStop.recoveryRequired(),"missing STOP reaches finite recovery failure");
    NetworkAdapter overflow;check(overflow.begin()==ESP_OK,"overflow lease fixture");overflow.pause(true);check(overflow.beginRadioLease()==ESP_OK,"overflow lease");event(WIFI_EVENT,WIFI_EVENT_STA_START);overflow.tick();
    const auto overflowStops=stops;for(unsigned i=0;i<9;++i)event(IP_EVENT,IP_EVENT_STA_GOT_IP);overflow.tick();
    check(overflow.recoveryRequired()&&overflow.radioLeased()&&stops==overflowStops,"lost leased event ordering fails closed without wrong-owner stop");
    NetworkAdapter inconsistent;check(inconsistent.begin()==ESP_OK,"inconsistent lease fixture");inconsistent.pause(true);check(inconsistent.beginRadioLease()==ESP_OK,"inconsistent lease");
    event(WIFI_EVENT,WIFI_EVENT_STA_START);check(inconsistent.releaseRadioLease(false)==ESP_FAIL,"false no-start claim cannot suppress required STOP barrier");
    NetworkAdapter reversed;check(reversed.begin()==ESP_OK,"reordered lease fixture");reversed.pause(true);check(reversed.beginRadioLease()==ESP_OK,"reordered lease");
    event(WIFI_EVENT,WIFI_EVENT_STA_STOP);event(WIFI_EVENT,WIFI_EVENT_STA_START);reversed.tick();check(reversed.recoveryRequired(),"STOP before START is ambiguous rather than an idle grant");
    std::cout<<"PASS "<<checks<<" actual network adapter checks: SDK/RTOS doubles, no physical radio claim.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

// Actual client implementation + real mbedTLS/cJSON. SDK/RTOS doubles below
// test our acceptance/ownership logic; this is NOT an ESP-IDF or radio build.
#include "../main/device_assets_client.cpp"
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void check(bool yes, const char* name) { ++checks; if (!yes) throw std::runtime_error(name); }
std::string readFile(const std::string& path) { std::ifstream f(path,std::ios::binary); if (!f) throw std::runtime_error("fixture missing"); return {std::istreambuf_iterator<char>(f),{}}; }
struct Queue { std::size_t size; std::uint8_t* storage; bool filled=false; };
std::map<void*,Queue> queues;
void (*workerFunction)(void*) = nullptr; void* workerContext = nullptr;
struct WorkerDone {};
std::function<void()> lockHook, readHook;
std::int64_t clockUs = 0;
struct Response { int status=200; std::vector<std::pair<std::string,std::string>> headers; std::string body; bool chunked=false; std::int64_t delay=0; };
std::deque<Response> replies;
std::vector<std::map<std::string,std::string>> requests;
unsigned reads=0, cleanups=0;
void runWorker() { try { workerFunction(workerContext); } catch (const WorkerDone&) {} }
Response jsonResponse(const std::string& body) { return {200,{{"Content-Type","application/json; charset=utf-8"},{"Content-Length",std::to_string(body.size())}},body}; }
std::string hashText(const digivice::assets::Spec& spec) { const char*hex="0123456789abcdef"; std::string s="\""; for (auto v:spec.sha256) {s+=hex[v>>4];s+=hex[v&15];}return s+'"'; }
Response rangeResponse(const digivice::assets::Spec& spec,const std::string& body,std::size_t offset) {
    auto part=body.substr(offset,std::min<std::size_t>(4096,body.size()-offset));
    return {206,{{"Content-Type",spec.kind==digivice::assets::Kind::Sprite?"application/octet-stream":"image/jpeg"},
        {"Content-Length",std::to_string(part.size())},{"ETag",hashText(spec)},
        {"Content-Range","bytes "+std::to_string(offset)+"-"+std::to_string(offset+part.size()-1)+"/"+std::to_string(body.size())}},part};
}
struct Flash : digivice::assets::Storage {
    std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(digivice::assets::kStorageBytes,255);
    unsigned mutations=0;
    bool read(std::size_t o,void*p,std::size_t n) override { if(o>bytes.size()||n>bytes.size()-o)return false;std::memcpy(p,bytes.data()+o,n);return true; }
    bool erase(std::size_t o,std::size_t n) override { if(o%4096||n%4096||o>bytes.size()||n>bytes.size()-o)return false;std::fill_n(bytes.data()+o,n,255);++mutations;return true; }
    bool program(std::size_t o,const void*p,std::size_t n) override { if(o>bytes.size()||n>bytes.size()-o)return false;const auto*b=static_cast<const std::uint8_t*>(p);for(std::size_t i=0;i<n;++i){if((bytes[o+i]&b[i])!=b[i])return false;bytes[o+i]&=b[i];}++mutations;return true; }
};
}
struct FakeHttp { esp_http_client_config_t options; Response response; std::map<std::string,std::string> headers; std::size_t offset=0; };
QueueHandle_t xQueueCreateStatic(UBaseType_t,UBaseType_t n,std::uint8_t*p,StaticQueue_t*q){queues[q]={n,p,false};return q;}
BaseType_t xQueueSend(QueueHandle_t q,const void*p,TickType_t){auto&x=queues.at(q);if(x.filled)return 0;std::memcpy(x.storage,p,x.size);x.filled=true;return pdTRUE;}
BaseType_t xQueueReceive(QueueHandle_t q,void*p,TickType_t wait){auto&x=queues.at(q);if(!x.filled){if(wait==portMAX_DELAY)throw WorkerDone{};return 0;}std::memcpy(p,x.storage,x.size);x.filled=false;return pdTRUE;}
BaseType_t xQueueOverwrite(QueueHandle_t q,const void*p){auto&x=queues.at(q);std::memcpy(x.storage,p,x.size);x.filled=true;return pdTRUE;}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t*p){return p;}
BaseType_t xSemaphoreTake(SemaphoreHandle_t,TickType_t){if(lockHook){auto f=std::move(lockHook);lockHook={};f();}return pdTRUE;}
BaseType_t xSemaphoreGive(SemaphoreHandle_t){return pdTRUE;}
BaseType_t xTaskCreate(void(*f)(void*),const char*,std::uint32_t,void*p,UBaseType_t,TaskHandle_t*t){workerFunction=f;workerContext=p;*t=p;return pdPASS;}
std::int64_t esp_timer_get_time(){return clockUs;}
esp_err_t esp_crt_bundle_attach(void*){return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*o){
    check(o->disable_auto_redirect&&!o->skip_cert_common_name_check&&o->crt_bundle_attach,"TLS/redirect policy");
    if(replies.empty())return nullptr;
    auto*p=new FakeHttp{*o,std::move(replies.front()),{},0};replies.pop_front();return p;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int n){return n>0&&n<=8000?ESP_OK:ESP_FAIL;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char*k,const char*v){c->headers[k]=v;return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int){requests.push_back(c->headers);return ESP_OK;}
std::int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c){
    clockUs+=c->response.delay;std::int64_t length=-1;
    for(auto&[key,value]:c->response.headers){esp_http_client_event_t e{HTTP_EVENT_ON_HEADER,c->options.user_data,key.data(),value.data()};(void)c->options.event_handler(&e);if(key=="Content-Length")length=std::stoll(value);}
    return length; // Deliberately ignore callback return, matching IDF 5.3.2.
}
int esp_http_client_get_status_code(esp_http_client_handle_t c){return c->response.status;}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t c){return c->response.chunked;}
int esp_http_client_read(esp_http_client_handle_t c,char*p,int n){++reads;if(readHook){auto f=std::move(readHook);readHook={};f();}const auto size=std::min<std::size_t>({static_cast<std::size_t>(n),c->response.body.size()-c->offset,701});std::memcpy(p,c->response.body.data()+c->offset,size);c->offset+=size;return static_cast<int>(size);}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c){return c->offset==c->response.body.size();}
esp_err_t esp_http_client_close(esp_http_client_handle_t){return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c){++cleanups;delete c;return ESP_OK;}

int main(int argc,char**argv){try{
    using namespace digivice::assets;
    if(argc!=3)throw std::runtime_error("fixture dir and repo required");
    const std::string directory=argv[1],repo=argv[2];
    const auto envelope=readFile(directory+"/valid.json");char payload[6145]{};Spec specs[24]{};std::size_t count=0;std::uint32_t release=0;
    check(catalog(envelope.c_str(),envelope.size(),payload,specs,count,release)==DownloadError::None&&count==18&&release==1,"actual catalog signature + 18 specs");
    Spec spec{};for(std::size_t i=0;i<count;++i)if(std::strcmp(specs[i].id,"sprite-mote-v1")==0)spec=specs[i];
    const auto blob=readFile(repo+"/assets/device/"+spec.id+".dva");
    for(const auto& subset:std::vector<std::pair<const char*,std::size_t>>{{"scenes-only",8},{"single-scene",1}}) {
        const auto source=readFile(directory+"/"+subset.first+".json");
        check(catalog(source.c_str(),source.size(),payload,specs,count,release)==DownloadError::None&&count==subset.second,"signed scene-only bounded catalog subset accepted");
        for(std::size_t i=0;i<count;++i)check(specs[i].kind==Kind::Background&&!std::strncmp(specs[i].id,"scene-",6),"scene subset contains no test sprite");
    }
    for(const char*name:{"signature","payload","duplicate-key","extra-key","wrong-key","wrong-profile","duplicate-id","wrong-path","oversize","wrong-dimensions","unknown-id","deep","nul","bad-padding","empty"}){
        const auto s=readFile(directory+"/"+name+".json");check(catalog(s.c_str(),s.size(),payload,specs,count,release)!=DownloadError::None,name);
    }
    std::uint8_t output[8193]{};std::size_t received=0;
    auto get=[&](Response response){replies.push_back(std::move(response));return httpRead("https://example.test","/blob",&spec,0,output,4096,received);};
    check(get(rangeResponse(spec,blob,0))&&received==4096&&std::memcmp(output,blob.data(),4096)==0,"exact range streams bounded reads");
    check(requests.back().at("Range")=="bytes=0-4095"&&requests.back().at("If-Range")==hashText(spec),"request range and digest pin");
    for(unsigned fault=0;fault<9;++fault){auto r=rangeResponse(spec,blob,0);if(fault==0)r.status=200;if(fault==1)r.status=302;if(fault==2)r.headers[0].second="text/html";if(fault==3)r.headers[1].second="4097";if(fault==4)r.headers[2].second="\"wrong\"";if(fault==5)r.headers[3].second="bytes 1-4096/8816";if(fault==6)r.headers.push_back({"Content-Length","4096"});if(fault==7)r.headers.push_back({"Content-Encoding","gzip"});if(fault==8)r.chunked=true;const auto before=reads;check(!get(std::move(r))&&reads==before,"bad headers rejected before body");}
    auto r=rangeResponse(spec,blob,0);r.body.resize(20);check(!get(r),"truncated body rejected");
    r=rangeResponse(spec,blob,0);r.delay=8000001;check(!get(r),"late header result rejected");
    r=jsonResponse(envelope);replies.push_back(r);check(httpRead("https://example.test","/catalog",nullptr,0,output,8192,received)&&received==envelope.size(),"bounded real catalog HTTP body");
    r=jsonResponse(std::string(8193,'x'));replies.push_back(r);check(!httpRead("https://example.test","/catalog",nullptr,0,output,8192,received),"oversized catalog body refused");
    // Real cache + real worker body; only scheduling/HTTP transport is doubled.
    Flash flash;Cache cache(flash);check(cache.boot()==Result::Ok,"cache boot");DeviceAssetsClient client;check(client.begin(&cache)==ESP_OK,"client begin");
    check(!client.request("http://203.0.113.1",true,spec.id)&&!client.request("http://192.168.1.8",false,spec.id),"private HTTP requires actual opt-in and private address");
    check(client.request("https://example.test",false,spec.id),"enqueue");check(!client.request("https://example.test",false,spec.id),"single pending job");client.cancel();runWorker();client.tick();check(client.status().phase==DownloadPhase::Cancelled&&!client.status().busy&&flash.mutations==0,"cancel queued prevents mutation");
    replies.push_back(jsonResponse(envelope));check(client.request("https://example.test",false,spec.id),"enqueue cancellation race");lockHook=[&]{client.cancel();};runWorker();client.tick();check(flash.mutations==0&&!client.status().busy,"cancel while acquiring cache lock prevents begin");
    replies.push_back(jsonResponse(envelope));replies.push_back(rangeResponse(spec,blob,0)); // Second range absent => bounded network failure.
    check(client.request("https://example.test",false,spec.id),"enqueue partial");runWorker();client.tick();check(client.status().phase==DownloadPhase::Failed&&client.status().received==4096&&!client.status().busy,"interrupted range leaves durable prefix");
    replies.push_back(jsonResponse(envelope));replies.push_back(rangeResponse(spec,blob,4096));replies.push_back(rangeResponse(spec,blob,8192));
    check(client.request("https://example.test",false,spec.id),"enqueue resume");runWorker();client.tick();check(client.status().phase==DownloadPhase::Complete&&client.status().received==blob.size(),"resumed asset hash verified and committed");
    Spec cached{};check(client.cachedSpec(spec.id,cached)&&sameSpec(spec,cached),"nonblocking verified spec");Result result{};check(client.tryRead(cached,0,output,4096,result)&&result==Result::Ok&&std::memcmp(output,blob.data(),4096)==0,"bounded cached read");
    check(!client.tryRead(cached,0,output,4097,result),"read bound");
    check(catalog(envelope.c_str(),envelope.size(),payload,specs,count,release)==DownloadError::None,"reload verified specs");
    Spec second{};for(std::size_t i=0;i<count;++i)if(std::strcmp(specs[i].id,"sprite-glint-v1")==0)second=specs[i];
    const auto secondBlob=readFile(repo+"/assets/device/"+second.id+".dva");const char* protectedIds[]{spec.id};
    replies.push_back(jsonResponse(envelope));replies.push_back(rangeResponse(second,secondBlob,0));
    check(client.request("https://example.test",false,second.id,protectedIds,1),"enqueue append cancellation");
    lockHook=[&]{lockHook=[&]{client.cancel();};};runWorker();client.tick();
    check(client.status().phase==DownloadPhase::Cancelled&&client.status().received==0,"cancel on append lock retains zero committed bytes");
    replies.push_back(jsonResponse(envelope));
    for(std::size_t offset=0;offset<secondBlob.size();offset+=4096){auto response=rangeResponse(second,secondBlob,offset);if(!offset)response.body[0]^=1;replies.push_back(response);}
    check(client.request("https://example.test",false,second.id,protectedIds,1),"enqueue corrupt replacement");runWorker();client.tick();
    check(client.status().phase==DownloadPhase::Failed&&client.status().cacheResult==Result::Integrity,"corrupt final digest cannot activate");
    check(client.cachedSpec(spec.id,cached)&&sameSpec(spec,cached)&&!client.cachedSpec(second.id,cached),"protected prior art survives corrupt new install");
    check(!client.quiescent(),"idle without request freeze is not a shutdown barrier");
    check(client.request("https://example.test",false,spec.id),"enqueue before shutdown");
    client.pause(true);client.pause(true);
    check(!client.quiescent()&&client.status().busy,"cancelled queued reservation remains pending");
    check(!client.request("https://example.test",false,spec.id),"shutdown refuses new work");
    check(!client.cachedSpec(spec.id,cached)&&!client.tryRead(spec,0,output,4096,result),"shutdown freezes owner cache reads");
    const auto beforeShutdown=flash.mutations;runWorker();client.tick();
    check(client.quiescent()&&flash.mutations==beforeShutdown,"queued cancel drains before granting storage barrier");
    client.pause(false);
    check(!client.quiescent()&&client.cachedSpec(spec.id,cached),"standby resume restores verified reads");
    replies.push_back(jsonResponse(envelope));
    check(client.request("https://example.test",false,spec.id),"request after standby resume");
    const auto beforeHttpCleanup=cleanups;
    readHook=[&]{client.pause(true);check(!client.quiescent()&&cleanups==beforeHttpCleanup,"in-flight HTTP remains pending through cancellation");};
    runWorker();client.tick();
    check(client.quiescent()&&cleanups==beforeHttpCleanup+1&&flash.mutations==beforeShutdown,"HTTP cleanup precedes quiescence and late catalog cannot write");
    client.pause(false);replies.push_back(jsonResponse(envelope));
    check(client.request("https://example.test",false,spec.id),"resume a second time");
    lockHook=[&]{client.pause(true);check(!client.quiescent(),"cache lock boundary is not completion");};
    runWorker();client.tick();
    check(client.quiescent()&&flash.mutations==beforeShutdown,"cache ownership returns before shutdown barrier");
    check(replies.empty(),"all scripted replies consumed");
    std::cout<<"PASS "<<checks<<" checks: actual client + real mbedTLS/cJSON; SDK/RTOS doubles, no ESP-IDF or board build.\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

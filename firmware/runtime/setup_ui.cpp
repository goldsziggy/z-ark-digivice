#include "setup_ui.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace digivice::setupui {
namespace {
constexpr int size = 412;
constexpr std::size_t maxButtons = 40;
void wipe(void* memory, std::size_t length) {
    auto* bytes = static_cast<volatile unsigned char*>(memory);
    while (length--) *bytes++ = 0;
}
std::size_t length(const char* text, std::size_t limit) {
    std::size_t n = 0;
    if (text) while (n < limit && text[n]) ++n;
    return n;
}
void copy(char* destination, std::size_t capacity, const char* source) {
    const auto n = length(source, capacity - 1);
    if (n) std::memcpy(destination, source, n);
    destination[n] = 0;
}
bool inside(int x, int y) {
    if (x < 0 || y < 0 || x >= size || y >= size) return false;
    const int dx=x-206, dy=y-206;
    return dx*dx+dy*dy <= 204*204;
}
constexpr std::uint16_t rgb(int r, int g, int b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
constexpr auto background=rgb(9,20,29), panel=rgb(18,42,51), ink=rgb(236,246,217);
constexpr auto muted=rgb(119,154,151), mint=rgb(144,231,174), amber=rgb(255,193,96);
// Original CC0 5x7 capitals from device_ui.cpp, with original lowercase and
// punctuation added here. Distinct lowercase glyphs make keyboard case visible.
constexpr const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+:/%.!?<>";
constexpr std::uint8_t capitals[][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{0,0,0,31,0,0,0},{0,4,4,31,4,4,0},
 {0,4,4,0,4,4,0},{1,2,2,4,8,8,16},{25,25,2,4,8,19,19},
 {0,0,0,0,0,6,6},{4,4,4,4,4,0,4},{14,17,1,2,4,0,4},
 {2,4,8,16,8,4,2},{8,4,2,1,2,4,8}
};
constexpr std::uint8_t lowercase[][7] = {
 {0,0,14,1,15,17,15},{16,16,30,17,17,17,30},{0,0,14,16,16,17,14},
 {1,1,15,17,17,17,15},{0,0,14,17,31,16,14},{6,9,8,28,8,8,8},
 {0,0,15,17,15,1,14},{16,16,30,17,17,17,17},{4,0,12,4,4,4,14},
 {2,0,6,2,2,18,12},{16,16,18,20,24,20,18},{12,4,4,4,4,4,14},
 {0,0,26,21,21,21,21},{0,0,30,17,17,17,17},{0,0,14,17,17,17,14},
 {0,0,30,17,30,16,16},{0,0,15,17,15,1,1},{0,0,22,25,16,16,16},
 {0,0,15,16,14,1,30},{8,8,28,8,8,9,6},{0,0,17,17,17,19,13},
 {0,0,17,17,17,10,4},{0,0,17,17,21,21,10},{0,0,17,10,4,10,17},
 {0,0,17,17,15,1,14},{0,0,31,2,4,8,31}
};
std::array<std::uint8_t,7> glyph(unsigned char ch) {
    std::array<std::uint8_t,7> result{};
    if (ch >= 'a' && ch <= 'z') {
        std::copy_n(lowercase[ch-'a'], 7, result.begin()); return result;
    }
    const char* found = ch ? std::strchr(alphabet, ch) : nullptr;
    if (found) { std::copy_n(capitals[found-alphabet], 7, result.begin()); return result; }
    switch (ch) {
    case ' ': return {};
    case '"': return {10,10,10,0,0,0,0};
    case '#': return {10,31,10,10,31,10,0};
    case '$': return {4,15,20,14,5,30,4};
    case '&': return {12,18,20,8,21,18,13};
    case '\'': return {4,4,8,0,0,0,0};
    case '(': return {2,4,8,8,8,4,2};
    case ')': return {8,4,2,2,2,4,8};
    case '*': return {0,21,14,31,14,21,0};
    case ',': return {0,0,0,0,6,4,8};
    case ';': return {0,4,4,0,4,4,8};
    case '=': return {0,0,31,0,31,0,0};
    case '@': return {14,17,23,21,23,16,14};
    case '[': return {14,8,8,8,8,8,14};
    case '\\': return {16,8,8,4,2,2,1};
    case ']': return {14,2,2,2,2,2,14};
    case '^': return {4,10,17,0,0,0,0};
    case '_': return {0,0,0,0,0,0,31};
    case 96: return {8,4,2,0,0,0,0};
    case '{': return {3,4,4,8,4,4,3};
    case '|': return {4,4,4,4,4,4,4};
    case '}': return {24,4,4,2,4,4,24};
    case '~': return {0,0,9,22,0,0,0};
    default: return {14,17,1,2,4,0,4};
    }
}
struct Span { std::int16_t begin=0, end=0; };
constexpr auto makeSpans() {
    std::array<Span,size> rows{};
    for (int y=0; y<size; ++y) {
        const int dy=y-206, square=204*204-dy*dy;
        if (square<0) continue;
        int reach=0;
        while ((reach+1)*(reach+1)<=square) ++reach;
        rows[y]={static_cast<std::int16_t>(206-reach),static_cast<std::int16_t>(207+reach)};
    }
    return rows;
}
constexpr auto spans=makeSpans();
struct Canvas {
    std::uint16_t* pixels;
    void rect(int x, int y, int w, int h, std::uint16_t color) {
        for (int row=std::max(0,y); row<std::min(size,y+h); ++row) {
            const int left=std::max<int>(x,spans[row].begin), right=std::min<int>(x+w,spans[row].end);
            if (left<right) std::fill(pixels+row*size+left,pixels+row*size+right,color);
        }
    }
    void text(int x,int y,const char* value,int scale,std::uint16_t color,std::size_t limit=32) {
        for (std::size_t n=0; value && n<limit && value[n]; ++n) {
            const auto rows=glyph(static_cast<unsigned char>(value[n]));
            for (int row=0; row<7; ++row) for (int col=0; col<5; ++col)
                if (rows[row] & (1u<<(4-col))) rect(x+col*scale,y+row*scale,scale,scale,color);
            x+=6*scale;
        }
    }
    void center(int y,const char* value,int scale=2,std::uint16_t color=ink,std::size_t limit=26) {
        const auto n=length(value,limit);
        text((size-static_cast<int>(n)*6*scale+scale)/2,y,value,scale,color,limit);
    }
};
enum Id { Scan=1, Manual, Service, Retry, Forget, Close, Back, Previous, Next,
          Page, Space, Delete, Done, PrivateHttp, ConfirmForget, Key=100, AccessPoint=1000 };
constexpr const char* pages[]={"abcdefghijklmnopqrstuvwxyz","ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789!\"#$%&'()*+,-./:;", "<=>?@[\\]^_\x60{|}~"};
static_assert(sizeof("0123456789!\"#$%&'()*+,-./:;")-1 <= 27);
bool writable(const Model& model) {
    // Scanning pauses the link controller internally. A power pause without a
    // scan is different: drafts are discarded and editing stays disabled.
    return model.ready && !model.recovery && (!model.network.paused || model.scanning);
}
std::size_t count(const Model& model) {
    return model.accessPoints ? std::min(model.count,kMaxAccessPoints) : 0;
}
std::uint32_t fingerprint(const Model& model) {
    std::uint32_t hash=2166136261u;
    auto add=[&](std::uint8_t byte) { hash=(hash^byte)*16777619u; };
    add(model.ready); add(model.recovery); add(model.scanning); add(model.network.paused);
    add(model.network.configured); add(model.privateHttpAllowed);
    for (unsigned i=0;i<4;++i) add(static_cast<std::uint8_t>(model.network.generation>>(8*i)));
    add(static_cast<std::uint8_t>(count(model)));
    for (std::size_t i=0;i<count(model);++i) {
        const auto& ap=model.accessPoints[i]; add(ap.supported);
        for (std::size_t n=0;n<sizeof(ap.ssid);++n) { add(ap.ssid[n]); if (!ap.ssid[n]) break; }
    }
    return hash;
}
} // namespace

Controller::~Controller() { clearDrafts(); }
void Controller::clearDrafts() {
    wipe(ssid_,sizeof(ssid_)); wipe(password_,sizeof(password_)); wipe(endpoint_,sizeof(endpoint_));
    privateHttp_=false; pending_=Intent::None; error_[0]=0;
}
void Controller::open() {
    clearDrafts(); active_=true; initialized_=false; keyboardPage_=networkPage_=0;
    changeScreen(Screen::Status);
}
void Controller::close() {
    clearDrafts(); active_=false; initialized_=false; changeScreen(Screen::Status);
}
void Controller::cancelTouch() { down_=false; downButton_=0; }
void Controller::changeScreen(Screen screen) {
    cancelTouch(); screen_=screen; keyboardPage_=0; error_[0]=0;
}
void Controller::update(const Model& model) {
    const auto next=fingerprint(model);
    if (!initialized_ || context_!=next) cancelTouch();
    if (!writable(model) && (screen_!=Screen::Status || pending_!=Intent::None)) {
        clearDrafts(); changeScreen(Screen::Status);
    }
    if (!model.privateHttpAllowed) privateHttp_=false;
    const auto total=count(model);
    if (networkPage_*3>=total) networkPage_=0;
    context_=next; initialized_=true;
}
std::size_t Controller::buttons(const Model& model,Button* output) const {
    std::size_t n=0;
    auto add=[&](int x,int y,int w,int h,const char* label,int id,bool enabled=true) {
        if (n>=maxButtons) return;
        auto& button=output[n++];
        button.x=x; button.y=y; button.w=w; button.h=h; button.id=id;
        copy(button.label,sizeof(button.label),label);
        button.enabled=enabled && pending_==Intent::None;
    };
    auto pair=[&](int row,const char* left,int leftId,const char* right,int rightId,bool l=true,bool r=true) {
        add(62,176+row*52,139,42,left,leftId,l); add(211,176+row*52,139,42,right,rightId,r);
    };
    const bool can=writable(model) && !model.scanning;
    switch (screen_) {
    case Screen::Status:
        pair(0,"SCAN",Scan,"HIDDEN SSID",Manual,can&&!model.scanning,can);
        pair(1,"SERVICE",Service,"RECONNECT",Retry,can&&model.network.configured,can&&model.network.configured);
        pair(2,"FORGET",Forget,"CLOSE",Close,can&&model.network.configured,true);
        break;
    case Screen::Networks:
        for (std::size_t i=networkPage_*3;i<std::min(count(model),std::size_t(networkPage_*3+3));++i) {
            const auto& ap=model.accessPoints[i];
            add(68,140+static_cast<int>(i%3)*52,276,44,ap.ssid,AccessPoint+static_cast<int>(i),
                can&&!model.scanning&&ap.supported&&length(ap.ssid,33)>0&&length(ap.ssid,33)<33);
        }
        add(110,305,92,34,"< PREV",Previous,networkPage_>0&&!model.scanning);
        add(210,305,92,34,"NEXT >",Next,(networkPage_+1)*3<count(model)&&!model.scanning);
        add(144,350,124,32,"BACK",Back);
        break;
    case Screen::Ssid: case Screen::Password: case Screen::Endpoint: {
        const char* keys=pages[keyboardPage_];
        for (std::size_t i=0; keys[i]; ++i) {
            const char label[]={keys[i],0};
            add(62+static_cast<int>(i%9)*32,168+static_cast<int>(i/9)*40,28,34,label,Key+keys[i],can);
        }
        const char* pageNames[]={"ABC","123/#","MORE #","abc"};
        add(62,290,92,36,pageNames[keyboardPage_],Page,can);
        add(160,290,92,36,"SPACE",Space,can);
        add(258,290,92,36,"DELETE",Delete,can);
        add(110,348,92,32,"BACK",Back);
        add(210,348,92,32,screen_==Screen::Ssid?"NEXT":"SAVE",Done,can);
        if (screen_==Screen::Endpoint && model.privateHttpAllowed)
            add(74,136,264,26,privateHttp_?"[X] PRIVATE HTTP":"[ ] PRIVATE HTTP",PrivateHttp,can);
        break;
    }
    case Screen::ForgetReview:
        add(106,240,200,44,"FORGET WI-FI",ConfirmForget,can&&model.network.configured);
        add(144,350,124,32,"CANCEL",Back);
        break;
    }
    return n;
}
int Controller::hit(const Model& model,int x,int y) const {
    if (!inside(x,y)) return 0;
    Button candidates[maxButtons]{};
    const auto n=buttons(model,candidates);
    for (std::size_t i=0;i<n;++i) {
        const auto& b=candidates[i];
        if (b.enabled && x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h) return b.id;
    }
    return 0;
}
Intent Controller::touch(const Model& model,deviceui::Touch event) {
    if (!active_) return Intent::None;
    update(model);
    using deviceui::TouchKind;
    if (event.kind==TouchKind::Cancel || !inside(event.x,event.y) ||
        (down_ && event.atMs<lastAt_)) { cancelTouch(); return Intent::None; }
    lastAt_=event.atMs;
    if (event.kind==TouchKind::Down) {
        if (down_) { cancelTouch(); return Intent::None; }
        downButton_=hit(model,event.x,event.y);
        down_=downButton_!=0; downAt_=event.atMs; downX_=event.x; downY_=event.y;
        return Intent::None;
    }
    if (!down_) return Intent::None;
    const int dx=event.x-downX_, dy=event.y-downY_;
    if (dx*dx+dy*dy>100 || event.atMs-downAt_>2000 || hit(model,event.x,event.y)!=downButton_) {
        cancelTouch(); return Intent::None;
    }
    if (event.kind==TouchKind::Move) return Intent::None;
    const int id=downButton_; cancelTouch();
    return event.kind==TouchKind::Up ? activate(model,id) : Intent::None;
}
void Controller::append(char character) {
    char* draft=screen_==Screen::Ssid ? ssid_ : screen_==Screen::Password ? password_ : endpoint_;
    const std::size_t limit=screen_==Screen::Ssid ? 32 : screen_==Screen::Password ? 64 : 192;
    const auto n=length(draft,limit);
    if (n>=limit) { copy(error_,sizeof(error_),"FIELD IS FULL"); return; }
    draft[n]=character; draft[n+1]=0; error_[0]=0;
}
void Controller::eraseCharacter() {
    char* draft=screen_==Screen::Ssid ? ssid_ : screen_==Screen::Password ? password_ : endpoint_;
    const auto n=length(draft,screen_==Screen::Ssid ? sizeof(ssid_) :
        screen_==Screen::Password ? sizeof(password_) : sizeof(endpoint_));
    if (n) {
        std::size_t begin=n-1;
        while (begin && (static_cast<unsigned char>(draft[begin])&0xc0)==0x80) --begin;
        wipe(draft+begin,n-begin);
    }
    error_[0]=0;
}
bool Controller::validWifi() {
    net::Config config{};
    std::memcpy(config.ssid,ssid_,sizeof(ssid_));
    std::memcpy(config.password,password_,sizeof(password_));
    const auto result=net::validateConfig(config);
    wipe(&config,sizeof(config));
    if (result==net::ConfigError::None) return true;
    copy(error_,sizeof(error_),result==net::ConfigError::Ssid ? "SSID: 1 TO 32 BYTES" :
        "USE 8-63 ASCII OR 64 HEX");
    return false;
}
Intent Controller::activate(const Model& model,int id) {
    error_[0]=0;
    if (id>=Key+32 && id<=Key+126) { append(static_cast<char>(id-Key)); return Intent::None; }
    if (id>=AccessPoint && id<AccessPoint+static_cast<int>(count(model))) {
        const auto& ap=model.accessPoints[id-AccessPoint];
        wipe(password_,sizeof(password_)); copy(ssid_,sizeof(ssid_),ap.ssid);
        changeScreen(Screen::Password); return Intent::None;
    }
    switch (id) {
    case Scan: clearDrafts(); networkPage_=0; changeScreen(Screen::Networks); return Intent::Scan;
    case Manual: clearDrafts(); changeScreen(Screen::Ssid); break;
    case Service:
        clearDrafts(); copy(endpoint_,sizeof(endpoint_),model.endpoint&&model.endpoint[0]?model.endpoint:"https://");
        privateHttp_=model.privateHttpAllowed&&model.allowPrivateHttp; changeScreen(Screen::Endpoint); break;
    case Retry: return Intent::Retry;
    case Close: close(); return Intent::Close;
    case Forget: clearDrafts(); changeScreen(Screen::ForgetReview); break;
    case ConfirmForget: clearDrafts(); changeScreen(Screen::Status); return Intent::Forget;
    case Back: clearDrafts(); changeScreen(Screen::Status); break;
    case Previous: if (networkPage_) --networkPage_; cancelTouch(); break;
    case Next: if ((networkPage_+1)*3<count(model)) ++networkPage_; cancelTouch(); break;
    case Page: keyboardPage_=(keyboardPage_+1)%4; cancelTouch(); break;
    case Space: append(' '); break;
    case Delete: eraseCharacter(); break;
    case PrivateHttp: privateHttp_=model.privateHttpAllowed&&!privateHttp_; break;
    case Done:
        if (screen_==Screen::Ssid) {
            if (!ssid_[0]) copy(error_,sizeof(error_),"ENTER A NETWORK NAME");
            else changeScreen(Screen::Password);
        } else if (screen_==Screen::Password) {
            if (validWifi()) return pending_=Intent::SaveWifi;
        } else if (screen_==Screen::Endpoint) {
            const auto result=net::validateEndpoint(endpoint_,privateHttp_&&model.privateHttpAllowed);
            if (result==net::ConfigError::None) return pending_=Intent::SaveEndpoint;
            copy(error_,sizeof(error_),result==net::ConfigError::PrivateHttpDisabled ?
                "PRIVATE HTTP NEEDS OPT-IN" : "USE AN HTTPS ORIGIN");
        }
        break;
    default: break;
    }
    return Intent::None;
}
bool Controller::takeWifi(char (&ssid)[33],char (&password)[65]) {
    wipe(ssid,sizeof(ssid)); wipe(password,sizeof(password));
    if (pending_!=Intent::SaveWifi) return false;
    std::memcpy(ssid,ssid_,sizeof(ssid_)); std::memcpy(password,password_,sizeof(password_));
    clearDrafts(); changeScreen(Screen::Status); return true;
}
bool Controller::takeEndpoint(char (&endpoint)[193],bool& allowPrivateHttp) {
    wipe(endpoint,sizeof(endpoint)); allowPrivateHttp=false;
    if (pending_!=Intent::SaveEndpoint) return false;
    std::memcpy(endpoint,endpoint_,sizeof(endpoint_));
    allowPrivateHttp=privateHttp_; clearDrafts(); changeScreen(Screen::Status); return true;
}
bool Controller::render(const Model& model,std::uint16_t* pixels,std::size_t capacity) const {
    if (!active_ || !pixels || capacity<deviceui::kPixels) return false;
    std::fill_n(pixels,deviceui::kPixels,0);
    Canvas canvas{pixels}; canvas.rect(0,0,size,size,background);
    const char* title=screen_==Screen::Networks ? "CHOOSE WI-FI" : screen_==Screen::Ssid ? "NETWORK NAME" :
        screen_==Screen::Password ? "WI-FI PASSWORD" : screen_==Screen::Endpoint ? "SERVICE ORIGIN" :
        screen_==Screen::ForgetReview ? "FORGET WI-FI?" : "WI-FI SETUP";
    canvas.center(46,title);
    if (screen_==Screen::Status) {
        const char* wifi=model.network.hasIp ? "WI-FI: CONNECTED" :
            model.network.state==net::State::Joining ? "WI-FI: CONNECTING" :
            model.network.configured ? "WI-FI: OFFLINE" : "WI-FI: NOT SET";
        const char* service=!model.endpoint||!model.endpoint[0] ? "SERVER: NOT SET" :
            model.network.hasIp&&model.network.serviceReachable ? "SERVER: READY" : "SERVER: UNREACHABLE";
        const char* clock=model.clockReady ? "CLOCK: READY" : model.clockWaiting ? "CLOCK: WAITING" :
            model.clockFailed ? "CLOCK: FAILED" : "CLOCK: NOT SET";
        canvas.center(84,wifi,2,model.network.hasIp?mint:muted);
        canvas.center(108,clock,2,model.clockReady?mint:muted);
        canvas.center(132,service,2,model.network.hasIp&&model.network.serviceReachable?mint:muted);
        canvas.center(156,model.recovery?"SETUP LOCKED FOR RECOVERY":!model.ready?"SETUP UNAVAILABLE":
            model.network.paused&&!model.scanning?"SETUP PAUSED":model.notice,
            1,amber,44);
        canvas.center(340,"GAME WORKS OFFLINE",1,muted);
    } else if (screen_==Screen::Networks) {
        canvas.center(84,model.scanning?"SCANNING...":"WPA2 PERSONAL NETWORKS",1,muted);
        canvas.center(108,model.notice,1,amber,40);
        if (!count(model)) canvas.center(208,model.scanning?"PLEASE WAIT":"NO NETWORKS FOUND",2,muted);
        for (std::size_t i=networkPage_*3;i<std::min(count(model),std::size_t(networkPage_*3+3));++i)
            if (!model.accessPoints[i].supported)
                canvas.center(185+static_cast<int>(i%3)*52,"UNSUPPORTED SECURITY",1,amber);
    } else if (screen_==Screen::ForgetReview) {
        canvas.center(108,"REMOVE SAVED WI-FI",2,amber);
        canvas.center(140,"AND SERVICE ADDRESS?",2,amber);
        canvas.center(190,"YOUR GAME SAVE IS KEPT",1,muted);
    } else {
        char display[33]{}, counter[24]{};
        if (screen_==Screen::Password) {
            const auto n=length(password_,64);
            std::fill_n(display,std::min<std::size_t>(n,24),'*');
            std::snprintf(counter,sizeof(counter),"%u / 64 BYTES",static_cast<unsigned>(n));
            canvas.center(76,ssid_,1,muted,32);
        } else {
            const char* draft=screen_==Screen::Ssid ? ssid_ : endpoint_;
            const auto n=length(draft,192);
            copy(display,sizeof(display),draft+(n>25?n-25:0));
            std::snprintf(counter,sizeof(counter),"%u / %u BYTES",static_cast<unsigned>(n),
                screen_==Screen::Ssid?32:192);
        }
        canvas.rect(54,96,304,26,panel); canvas.center(102,display,2,ink,25);
        canvas.center(126,counter,1,muted);
        if (screen_!=Screen::Endpoint || !model.privateHttpAllowed)
            canvas.center(143,error_[0]?error_:screen_==Screen::Password?"PASSWORD STAYS MASKED":"ASCII KEYBOARD",1,amber,44);
        else if (error_[0]) canvas.center(330,error_,1,amber,40);
        canvas.center(332,screen_==Screen::Endpoint&&error_[0]?"":keyboardPage_==0?"lowercase":
            keyboardPage_==1?"UPPERCASE":keyboardPage_==2?"numbers / symbols":"more symbols",1,muted);
    }
    Button controls[maxButtons]{};
    const auto n=buttons(model,controls);
    for (std::size_t i=0;i<n;++i) {
        const auto& b=controls[i];
        canvas.rect(b.x,b.y,b.w,b.h,panel);
        const int scale=length(b.label,32)*12<=static_cast<std::size_t>(b.w-8)?2:1;
        const auto visible=std::min<std::size_t>(length(b.label,32),(b.w-12)/(6*scale));
        canvas.text(b.x+(b.w-static_cast<int>(visible)*6*scale+scale)/2,b.y+(b.h-7*scale)/2,
            b.label,scale,b.enabled?ink:muted,visible);
    }
    return true;
}
} // namespace digivice::setupui

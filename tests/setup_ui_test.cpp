#include "setup_ui.hpp"
#include "../firmware/main/display_orientation.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

using namespace digivice;
using namespace digivice::setupui;
unsigned checks=0;
#define CHECK(value) do { ++checks; if (!(value)) { std::fprintf(stderr,"FAIL line %d\n",__LINE__); std::exit(1); } } while (false)
constexpr const char* keyPages[]={"abcdefghijklmnopqrstuvwxyz","ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789!\"#$%&'()*+,-./:;", "<=>?@[\\]^_\x60{|}~"};
bool contains(const Controller& ui,const std::string& text) {
    const auto* begin=reinterpret_cast<const unsigned char*>(&ui);
    return std::search(begin,begin+sizeof(ui),text.begin(),text.end())!=begin+sizeof(ui);
}
struct Harness {
    Controller ui;
    Model model{};
    AP aps[kMaxAccessPoints]{};
    std::uint64_t now=100;
    unsigned page=0;
    Harness() {
        model.ready=true; model.network.configured=true;
        ui.open(); ui.update(model);
    }
    Intent event(deviceui::TouchKind kind,int x,int y,std::uint64_t delta=30) {
        now+=delta;
        return ui.touch(model,{kind,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now});
    }
    Intent tap(int x,int y) {
        CHECK(event(deviceui::TouchKind::Down,x,y)==Intent::None);
        return event(deviceui::TouchKind::Up,x,y,60);
    }
    void setPage(unsigned wanted) {
        while (page!=wanted) { CHECK(tap(108,308)==Intent::None); page=(page+1)%4; }
    }
    void type(const std::string& text) {
        for (char ch:text) {
            if (ch==' ') { CHECK(tap(206,308)==Intent::None); continue; }
            bool found=false;
            for (unsigned p=0;p<4;++p) {
                const auto* at=std::strchr(keyPages[p],ch);
                if (!at) continue;
                setPage(p);
                const auto index=static_cast<unsigned>(at-keyPages[p]);
                CHECK(tap(76+(index%9)*32,185+(index/9)*40)==Intent::None);
                found=true; break;
            }
            CHECK(found);
        }
    }
    void manual(const std::string& ssid="fixture") {
        CHECK(tap(280,197)==Intent::None); CHECK(ui.screen()==Screen::Ssid); page=0;
        type(ssid); CHECK(tap(256,364)==Intent::None);
        CHECK(ui.screen()==Screen::Password); page=0;
    }
    void service() {
        CHECK(tap(131,249)==Intent::None); CHECK(ui.screen()==Screen::Endpoint); page=0;
    }
    void erase(unsigned times) {
        for (unsigned i=0;i<times;++i) CHECK(tap(304,308)==Intent::None);
    }
    void back() { CHECK(tap(156,364)==Intent::None); CHECK(ui.screen()==Screen::Status); page=0; }
};

int main() {
    CHECK(sizeof(Controller)<512);
    // Native-coordinate goldens exercise both quarter turns through the real
    // setup controller, independently of the forward display transform.
    struct NativeControls {
        display::Orientation orientation;
        display::Point manual,keyA,next,back,forget,confirm,cancelReview;
    };
    const NativeControls nativeControls[]{
        {display::Orientation::Cw90,{214,280},{226,76},{47,256},{47,156},{110,131},{149,206},{45,206}},
        {display::Orientation::Ccw90,{197,131},{185,335},{364,155},{364,255},{301,280},{262,205},{366,205}},
    };
    for(const auto& controls:nativeControls) {
        Harness h;
        auto event=[&](deviceui::TouchKind kind,display::Point native,std::uint64_t delta=30) {
            const auto logical=display::panelToLogical(controls.orientation,native);
            return h.event(kind,logical.x,logical.y,delta);
        };
        auto tap=[&](display::Point native) {
            CHECK(event(deviceui::TouchKind::Down,native)==Intent::None);
            return event(deviceui::TouchKind::Up,native,60);
        };
        CHECK(tap(controls.manual)==Intent::None && h.ui.screen()==Screen::Ssid);
        CHECK(tap(controls.keyA)==Intent::None);
        CHECK(tap(controls.next)==Intent::None && h.ui.screen()==Screen::Password);
        CHECK(event(deviceui::TouchKind::Up,controls.next)==Intent::None);
        for(unsigned i=0;i<8;++i) CHECK(tap(controls.keyA)==Intent::None);
        CHECK(tap(controls.next)==Intent::SaveWifi);
        char ssid[33]{},password[65]{};
        CHECK(h.ui.takeWifi(ssid,password));
        CHECK(std::string(ssid)=="a" && std::string(password)=="aaaaaaaa");
        CHECK(h.ui.screen()==Screen::Status);
        CHECK(event(deviceui::TouchKind::Up,controls.next)==Intent::None);
        CHECK(tap(controls.manual)==Intent::None && h.ui.screen()==Screen::Ssid);
        CHECK(tap(controls.back)==Intent::None && h.ui.screen()==Screen::Status);
        CHECK(tap(controls.forget)==Intent::None && h.ui.screen()==Screen::ForgetReview);
        CHECK(event(deviceui::TouchKind::Up,controls.confirm)==Intent::None);
        CHECK(tap(controls.cancelReview)==Intent::None && h.ui.screen()==Screen::Status);
        CHECK(tap(controls.forget)==Intent::None && h.ui.screen()==Screen::ForgetReview);
        CHECK(tap(controls.confirm)==Intent::Forget && h.ui.screen()==Screen::Status);
        CHECK(event(deviceui::TouchKind::Up,controls.confirm)==Intent::None);
    }
    // Every printable ASCII character must survive the real tap keyboard.
    for (int begin=32;begin<127;begin+=48) {
        Harness h; h.manual();
        std::string expected;
        for (int c=begin;c<std::min(begin+48,127);++c) expected+=static_cast<char>(c);
        h.type(expected);
        CHECK(h.tap(256,364)==Intent::SaveWifi);
        CHECK(h.tap(256,364)==Intent::None); // One outstanding save only.
        char ssid[33]{}, password[65]{};
        CHECK(h.ui.takeWifi(ssid,password));
        CHECK(std::string(ssid)=="fixture" && std::string(password)==expected);
        CHECK(!contains(h.ui,expected));
        CHECK(h.ui.screen()==Screen::Status);
        CHECK(!h.ui.takeWifi(ssid,password) && !ssid[0] && !password[0]);
    }
    // Passwords of equal length yield exactly equal pixels, including no last
    // character reveal or pressed-key highlight. Fixtures are synthetic only.
    {
        Harness a,b; a.manual(); b.manual();
        const std::string secretA="silvertrail", secretB="cobaltcloud";
        a.type(secretA); b.type(secretB); a.setPage(0); b.setPage(0);
        static std::array<std::uint16_t,deviceui::kPixels> left{},right{};
        CHECK(a.ui.render(a.model,left.data(),left.size()));
        CHECK(b.ui.render(b.model,right.data(),right.size()));
        CHECK(left==right);
        a.back(); CHECK(!contains(a.ui,secretA));
        b.ui.close(); CHECK(!contains(b.ui,secretB) && !b.ui.active());
    }
    // Character limits, WPA2 length/64-hex distinction, and destructive copies.
    {
        Harness h; h.manual(std::string(33,'n')); h.type(std::string(7,'a'));
        CHECK(h.tap(256,364)==Intent::None);
        h.type(std::string(58,'g')); CHECK(h.tap(256,364)==Intent::None);
        h.erase(1); CHECK(h.tap(256,364)==Intent::SaveWifi);
        char ssid[33]{}, password[65]{};
        CHECK(h.ui.takeWifi(ssid,password));
        CHECK(std::strlen(ssid)==32 && std::strlen(password)==63);
        CHECK(password[62]=='g');
        h.manual(); h.type(std::string(65,'a'));
        CHECK(h.tap(256,364)==Intent::SaveWifi); CHECK(h.ui.takeWifi(ssid,password));
        CHECK(std::strlen(password)==64);
    }
    // An Up, drift, changed key, cancel, stale timestamp, and overlong hold do
    // not type a character or reuse a touch on a new screen.
    {
        Harness h;
        CHECK(h.event(deviceui::TouchKind::Up,280,197)==Intent::None);
        CHECK(h.ui.screen()==Screen::Status);
        h.manual();
        CHECK(h.event(deviceui::TouchKind::Down,76,185)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Move,87,185)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Up,76,185)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Down,76,185)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Up,108,185)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Down,76,185)==Intent::None);
        h.ui.cancelTouch(); CHECK(h.event(deviceui::TouchKind::Up,76,185)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Down,76,185)==Intent::None);
        CHECK(h.ui.touch(h.model,{deviceui::TouchKind::Up,76,185,h.now-1})==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Down,76,185)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Up,76,185,2001)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Down,0,0)==Intent::None);
        CHECK(h.event(deviceui::TouchKind::Up,76,185)==Intent::None);
        const std::string expected="boundedpass";
        h.type(expected); CHECK(h.tap(256,364)==Intent::SaveWifi);
        char ssid[33]{}, password[65]{};
        CHECK(h.ui.takeWifi(ssid,password) && std::string(password)==expected);
    }
    // Scan result selection is bound to the viewed list; preserve UTF-8 bytes
    // even when the limited font falls back, and disable unsupported security.
    {
        Harness h;
        std::strcpy(h.aps[0].ssid,"unsupported"); h.aps[0].supported=false;
        std::strcpy(h.aps[1].ssid,"caf\xc3\xa9"); h.aps[1].supported=true;
        h.model.accessPoints=h.aps; h.model.count=2;
        CHECK(h.tap(131,197)==Intent::Scan);
        CHECK(h.tap(206,162)==Intent::None && h.ui.screen()==Screen::Networks);
        CHECK(h.event(deviceui::TouchKind::Down,206,214)==Intent::None);
        std::strcpy(h.aps[1].ssid,"replacement");
        CHECK(h.event(deviceui::TouchKind::Up,206,214)==Intent::None);
        CHECK(h.ui.screen()==Screen::Networks);
        std::strcpy(h.aps[1].ssid,"caf\xc3\xa9");
        h.model.scanning=true;
        CHECK(h.tap(206,214)==Intent::None && h.ui.screen()==Screen::Networks);
        h.model.scanning=false;
        CHECK(h.tap(206,214)==Intent::None && h.ui.screen()==Screen::Password);
        h.type("synthetic-pass"); CHECK(h.tap(256,364)==Intent::SaveWifi);
        char ssid[33]{}, password[65]{};
        CHECK(h.ui.takeWifi(ssid,password));
        CHECK(std::string(ssid)=="caf\xc3\xa9");
    }
    // Pagination is bounded, including an oversized public count.
    {
        Harness h;
        for (unsigned i=0;i<kMaxAccessPoints;++i) {
            std::snprintf(h.aps[i].ssid,sizeof(h.aps[i].ssid),"network-%u",i);
            h.aps[i].supported=true;
        }
        h.model.accessPoints=h.aps; h.model.count=1000;
        CHECK(h.tap(131,197)==Intent::Scan);
        for (unsigned i=0;i<8;++i) CHECK(h.tap(256,322)==Intent::None);
        CHECK(h.tap(206,162)==Intent::None && h.ui.screen()==Screen::Password);
        h.type("synthetic-pass"); CHECK(h.tap(256,364)==Intent::SaveWifi);
        char ssid[33]{}, password[65]{};
        CHECK(h.ui.takeWifi(ssid,password) && std::string(ssid)=="network-15");
    }
    // Scan-induced link pauses retain the results screen; external power pauses
    // discard a draft, disarm touch, and still permit closing setup.
    {
        Harness h; CHECK(h.tap(131,197)==Intent::Scan);
        h.model.scanning=true; h.model.network.paused=true; h.ui.update(h.model);
        CHECK(h.ui.screen()==Screen::Networks);
        h.model.scanning=false; h.model.network.paused=false; h.ui.update(h.model);
        CHECK(h.tap(206,366)==Intent::None);
        h.manual(); const std::string secret="transient-fixture"; h.type(secret);
        h.model.network.paused=true; h.ui.update(h.model);
        CHECK(h.ui.screen()==Screen::Status && !contains(h.ui,secret));
        CHECK(h.tap(280,197)==Intent::None && h.ui.screen()==Screen::Status);
        CHECK(h.tap(280,301)==Intent::Close);
    }
    // Recovery closes and wipes an editing draft. Closing remains possible even
    // when setup is unavailable; forgetting requires a separate confirmation.
    {
        Harness h; h.manual(); const std::string secret="synthetic-secret"; h.type(secret);
        h.model.recovery=true; h.ui.update(h.model);
        CHECK(h.ui.screen()==Screen::Status && !contains(h.ui,secret));
        CHECK(h.tap(280,197)==Intent::None && h.ui.screen()==Screen::Status);
        CHECK(h.tap(280,301)==Intent::Close && !h.ui.active());
        h.model.recovery=false; h.ui.open(); h.ui.update(h.model);
        CHECK(h.tap(131,301)==Intent::None && h.ui.screen()==Screen::ForgetReview);
        CHECK(h.event(deviceui::TouchKind::Up,206,262)==Intent::None);
        CHECK(h.tap(206,366)==Intent::None && h.ui.screen()==Screen::Status);
        CHECK(h.tap(131,301)==Intent::None);
        CHECK(h.tap(206,262)==Intent::Forget);
        CHECK(h.ui.screen()==Screen::Status);
        CHECK(h.tap(280,249)==Intent::Retry);
        h.model.ready=false; h.ui.update(h.model);
        CHECK(h.tap(280,301)==Intent::Close);
    }
    // Origin validation and length use the shared validator. HTTPS is default;
    // private HTTP needs a build gate and an explicit UI checkbox.
    {
        Harness h; h.service(); h.type("example.test/path");
        CHECK(h.tap(256,364)==Intent::None);
        h.erase(5); CHECK(h.tap(256,364)==Intent::SaveEndpoint);
        char endpoint[193]{}; bool allow=true;
        CHECK(h.ui.takeEndpoint(endpoint,allow));
        CHECK(std::string(endpoint)=="https://example.test" && !allow);
        CHECK(!h.ui.takeEndpoint(endpoint,allow) && !endpoint[0] && !allow);
        h.service(); h.erase(8); h.type("http://192.168.1.2:8787");
        CHECK(h.tap(206,149)==Intent::None); CHECK(h.tap(256,364)==Intent::None);
        h.back(); h.model.privateHttpAllowed=true; h.service(); h.erase(8);
        h.type("http://192.168.1.2:8787"); CHECK(h.tap(256,364)==Intent::None);
        CHECK(h.tap(206,149)==Intent::None); CHECK(h.tap(256,364)==Intent::SaveEndpoint);
        CHECK(h.ui.takeEndpoint(endpoint,allow) && allow);
        CHECK(std::string(endpoint)=="http://192.168.1.2:8787");
        h.service(); h.erase(8);
        const std::string full="https://"+std::string(63,'a')+"."+std::string(63,'b')+"."+std::string(56,'c');
        CHECK(full.size()==192); h.type(full); h.type("x");
        CHECK(h.tap(256,364)==Intent::SaveEndpoint);
        CHECK(h.ui.takeEndpoint(endpoint,allow) && std::string(endpoint)==full && !allow);
    }
    // The renderer respects the caller's exact capacity, separates Wi-Fi from
    // server reachability, and shows independent clock states.
    {
        Harness h;
        static std::array<std::uint16_t,deviceui::kPixels+2> pixels{};
        pixels.front()=0x1234; pixels.back()=0xabcd;
        CHECK(!h.ui.render(h.model,pixels.data()+1,deviceui::kPixels-1));
        CHECK(h.ui.render(h.model,pixels.data()+1,deviceui::kPixels));
        CHECK(pixels.front()==0x1234 && pixels.back()==0xabcd);
        static std::array<std::uint16_t,deviceui::kPixels+2> before{};
        before=pixels;
        h.model.network.hasIp=true;
        CHECK(h.ui.render(h.model,pixels.data()+1,deviceui::kPixels) && pixels!=before);
        before=pixels; h.model.clockWaiting=true;
        CHECK(h.ui.render(h.model,pixels.data()+1,deviceui::kPixels) && pixels!=before);
        before=pixels; h.model.clockReady=true; h.model.clockWaiting=false;
        CHECK(h.ui.render(h.model,pixels.data()+1,deviceui::kPixels) && pixels!=before);
        before=pixels; h.model.endpoint="https://example.test";
        CHECK(h.ui.render(h.model,pixels.data()+1,deviceui::kPixels) && pixels!=before);
        before=pixels; h.model.network.serviceReachable=true;
        CHECK(h.ui.render(h.model,pixels.data()+1,deviceui::kPixels) && pixels!=before);
        CHECK(pixels.front()==0x1234 && pixels.back()==0xabcd);
        h.ui.close(); CHECK(!h.ui.render(h.model,pixels.data()+1,deviceui::kPixels));
    }
    std::printf("setup UI: %u checks passed; controller %zu bytes\n",checks,sizeof(Controller));
}

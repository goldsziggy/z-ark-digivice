#include "pedometer.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <vector>

#ifndef PEDOMETER_BASELINE_METRICS
namespace digivice::motion {
struct PedometerTestAccess {
    static void count(Pedometer& p, std::uint32_t n) { p.reading_.acceptedSteps=n; }
};
}
#endif
namespace {
unsigned checks=0, failures=0;
#define CHECK(x) do {++checks; if(!(x)) {++failures; std::printf("FAIL line %u: %s\n",__LINE__,#x);}} while(0)
constexpr float pi=3.14159265358979323846F;
using namespace digivice::motion;
struct Sample { Vector3 a; std::uint64_t time; };
struct Trace {
    const char* name;
    unsigned cycles;
    std::vector<Sample> samples;
    std::uint64_t now=0;
    Trace(const char* n,unsigned c):name(n),cycles(c) {}
    void add(Vector3 a,unsigned dt=20) { samples.push_back({a,now}); now+=dt; }
    void still(unsigned ms=1200) {
        const auto end=now+ms;
        while(now<end) add({0,0,1});
    }
};
Vector3 rotate(Vector3 a,float angle) {
    return {a.x*std::cos(angle)+a.z*std::sin(angle),a.y,
            a.z*std::cos(angle)-a.x*std::sin(angle)};
}
// Generated acceleration fixtures, not measurements or ground-truth walking.
void wave(Trace& t,unsigned period,unsigned cycles,float amplitude,
          bool rotation=false,bool jitter=false) {
    const auto start=t.now,end=start+static_cast<std::uint64_t>(period)*cycles;
    unsigned sample=0;
    while(t.now<end) {
        const float phase=2*pi*static_cast<float>(t.now-start)/period;
        const float magnitude=1+amplitude*std::sin(phase);
        t.add(rotate({0,0,magnitude},rotation?static_cast<float>(t.now)/1300:0),
              jitter?(sample++%3==0?40:20):20);
    }
}
Trace cadence(const char* name,unsigned period,unsigned cycles,float amplitude=.18F,
              bool rotation=false,bool jitter=false) {
    Trace t(name,cycles);t.still();wave(t,period,cycles,amplitude,rotation,jitter);t.still(600);
    return t;
}
// A 30cm pendulum with gravity, tangential/centripetal acceleration and an
// independent vertical step component, rotated into swinging sensor axes.
// One full swing spans two input cycles. Parameters are illustrative only.
Trace lanyard(const char* name,unsigned period,unsigned cycles,float angle,float vertical) {
    Trace t(name,cycles);t.still();
    constexpr float length=.30F,gravity=9.80665F;
    const float omega=pi/(period*.001F);
    const auto start=t.now,end=start+static_cast<std::uint64_t>(period)*cycles;
    while(t.now<end) {
        const float phase=omega*static_cast<float>(t.now-start)*.001F;
        const float theta=angle*std::sin(phase),speed=angle*omega*std::cos(phase);
        const float acceleration=-angle*omega*omega*std::sin(phase);
        const float ax=length*(std::cos(theta)*acceleration-std::sin(theta)*speed*speed)/gravity;
        const float az=length*(std::sin(theta)*acceleration+std::cos(theta)*speed*speed)/gravity;
        const Vector3 world={ax,.012F*std::sin(.7F*phase),1+az+vertical*std::sin(2*phase)};
        t.add(rotate(world,theta+.3F*std::sin(.6F*phase)));
    }
    t.still(1800);return t;
}
Trace bursts(unsigned cycles) {
    Trace t(cycles==1?"one-cycle stop/start":"two-cycle stop/start",cycles*6);t.still();
    for(unsigned i=0;i<6;++i) {wave(t,600,cycles,.10F);t.still(2400);}
    return t;
}
Trace irregular() {
    Trace t("irregular cadence",24);t.still();
    constexpr unsigned periods[]={420,1000,540,1600,700,1250};
    for(unsigned i=0;i<24;++i) wave(t,periods[i%6],1,.10F);
    t.still(600);return t;
}
Trace alternating(const char* name,unsigned period,float strong,float weak) {
    Trace t(name,40);t.still();
    for(unsigned i=0;i<40;++i) wave(t,period,1,i%2?weak:strong);
    t.still(600);return t;
}
Trace handling(bool ringing) {
    Trace t(ringing?"damped capture-like ringing":"single capture-like impulse",0);t.still();
    const auto start=t.now;
    while(t.now-start<2400) {
        const float ms=static_cast<float>(t.now-start);
        float offset=0;
        if(ringing) offset=.45F*std::exp(-ms/900)*std::sin(2*pi*ms/420);
        else if(ms<160) offset=.7F*std::sin(pi*ms/160);
        else if(ms<380) offset=-.20F*std::sin(pi*(ms-160)/220);
        const Vector3 world={.08F*std::sin(2*pi*ms/700)*std::exp(-ms/250),0,1+offset};
        t.add(rotate(world,.8F*std::exp(-ms/500)));
    }
    t.still(1800);return t;
}
std::uint32_t feed(Pedometer& p,const Trace& trace,std::uint64_t start=0) {
    for(const auto& sample:trace.samples) {
        const auto before=p.reading().acceptedSteps;
        const auto r=p.observe(sample.a,start+sample.time);
        CHECK(r.acceptedSteps>=before);
        CHECK(r.status!=StepStatus::InvalidSample);
    }
    return p.reading().acceptedSteps;
}
std::uint32_t run(const Trace& t) {Pedometer p;return feed(p,t);}
void metrics() {
    std::vector<Trace> traces;
    traces.push_back(cadence("regular cadence",600,24));
    traces.push_back(cadence("slow 1600ms",1600,24,.10F));
    traces.push_back(cadence("slow 1800ms",1800,24,.10F));
    traces.push_back(irregular());
    traces.push_back(bursts(1));traces.push_back(bursts(2));
    traces.push_back(cadence("low-amplitude hand",600,24,.065F));
    traces.push_back(cadence("pocket rotation/sample jitter",600,24,.14F,true,true));
    traces.push_back(lanyard("lanyard gravity/swing/steps",700,24,.28F,.075F));
    traces.push_back(lanyard("lanyard dominant swing/weak steps",700,24,.40F,.020F));
    traces.push_back(lanyard("swing-only handling, no steps",700,24,.40F,0));
    traces.push_back(alternating("alternating strong/weak peaks",600,.18F,.055F));
    traces.push_back(alternating("alternating subthreshold peaks",600,.18F,.025F));
    traces.push_back(handling(false));traces.push_back(handling(true));
    traces.push_back(cadence("rapid shake 180ms",180,40,.24F));
    traces.push_back(alternating("rapid shake alternating peaks",180,.24F,.025F));
    auto bumps=cadence("two walking-like handling bumps",600,2,.10F);bumps.cycles=0;
    traces.push_back(bumps);
    traces.push_back(cadence("rapid 260ms boundary",260,24));
    traces.push_back(cadence("below rise threshold 0.040g",600,24,.040F));
    traces.push_back(cadence("minimum 280ms",280,24));
    traces.push_back(cadence("too slow 2000ms",2000,24,.10F));
    std::vector<std::uint32_t> counts;
    for(const auto& trace:traces) {
        counts.push_back(run(trace));
        std::printf("METRIC %-38s cycles=%2u accepted=%2u\n",trace.name,trace.cycles,counts.back());
    }
#ifndef PEDOMETER_BASELINE_METRICS
    CHECK(counts[0]>=23 && counts[0]<=24);
    CHECK(counts[1]==24);CHECK(counts[2]==24);
    CHECK(counts[3]>=22 && counts[3]<=24);
    CHECK(counts[4]==0);CHECK(counts[5]==12);
    CHECK(counts[6]==24);CHECK(counts[7]==24);CHECK(counts[8]==24);
    // Swing dominates the weak vertical signal: even a no-step pendulum gets
    // credit, including one settling lobe. This is an explicit known limit.
    CHECK(counts[9]==25);CHECK(counts[10]==25);
    CHECK(counts[11]==40);CHECK(counts[12]==20); // Subthreshold steps remain lost.
    CHECK(counts[13]==0);CHECK(counts[15]==0);
    CHECK(counts[14]==4); // Ringing is rhythmic enough to resemble four steps.
    CHECK(counts[16]==20); // Unequal shake peaks alias to a plausible 360ms cadence.
    CHECK(counts[17]==2); // Deliberately admitted false positives: indistinguishable input.
    // Filter startup shifts the first two sampled 260ms peaks to 280ms apart;
    // this pair passes the minimum gate, subsequent rapid peaks do not count.
    CHECK(counts[18]==2);CHECK(counts[19]==0);
    CHECK(counts[20]>=23 && counts[20]<=24);CHECK(counts[21]==0);
#endif
}
void safety() {
    {
        Trace t("stationary noise/orientation",0);t.still();
        for(unsigned i=0;i<6000;++i) {
            const float noise=static_cast<float>(static_cast<int>(i%7)-3)*.005F;
            t.add(rotate({0,0,1+noise},static_cast<float>(i)*.08F));
        }
        CHECK(run(t)==0);
    }
    for(unsigned period:{300u,400u,600u,1000u,1200u,1400u,1500u}) {
        const auto count=run(cadence("regular sweep",period,24));CHECK(count>=22 && count<=24);
    }
    for(float amplitude:{.10F,.14F,.24F}) {
        const auto count=run(cadence("amplitude sweep",600,24,amplitude));CHECK(count>=22 && count<=24);
    }
    CHECK(run(cadence("fixed",600,30))==run(cadence("rotated",600,30,.18F,true)));
    const auto jitter=run(cadence("jitter",600,30,.18F,true,true));CHECK(jitter>=28 && jitter<=30);
    {
        Pedometer p;const auto trace=cadence("recovery walk",600,12);
        std::uint64_t t=0;feed(p,trace,t);t+=trace.now;
        auto count=p.reading().acceptedSteps;CHECK(count>=10);
        const auto last=p.reading().observedAtMs;
        for(unsigned i=0;i<20;++i) p.observe({0,0,1.25F},last);
        CHECK(p.reading().acceptedSteps==count);CHECK(p.reading().observedAtMs==last);
        CHECK(p.poll(last+201).status==StepStatus::Gap);CHECK(p.reading().acceptedSteps==count);
        t+=3000;feed(p,trace,t);t+=trace.now;
        CHECK(p.reading().acceptedSteps>count);count=p.reading().acceptedSteps;
        CHECK(p.observe({0,0,1},t-100).status==StepStatus::TimeReversed);
        CHECK(p.reading().acceptedSteps==count);
        for(const auto a:{Vector3{0,0,4},Vector3{0,0,0},
                          Vector3{0,0,std::numeric_limits<float>::quiet_NaN()},
                          Vector3{0,0,std::numeric_limits<float>::infinity()}}) {
            CHECK(p.observe(a,t).status==StepStatus::InvalidSample);t+=20;
            CHECK(p.reading().acceptedSteps==count);
        }
        feed(p,trace,t);t+=trace.now;CHECK(p.reading().acceptedSteps>count);count=p.reading().acceptedSteps;
        CHECK(p.pause(true,t).status==StepStatus::Paused);
        feed(p,trace,t);t+=trace.now;CHECK(p.reading().acceptedSteps==count);
        CHECK(p.pause(false,t).status==StepStatus::Priming);CHECK(p.reading().acceptedSteps==count);
        feed(p,trace,t);t+=trace.now;CHECK(p.reading().acceptedSteps>count);count=p.reading().acceptedSteps;
#ifndef PEDOMETER_BASELINE_METRICS
        CHECK(p.invalidate(StepStatus::Recovering).status==StepStatus::Recovering);
        CHECK(p.reading().acceptedSteps==count);CHECK(p.poll(t+10000).acceptedSteps==count);
        t+=12000;CHECK(p.observe({0,0,1},t).status==StepStatus::Priming);t+=20;
        CHECK(p.reading().acceptedSteps==count);
        feed(p,trace,t);CHECK(p.reading().acceptedSteps>count);
#endif
    }
    // An unconfirmed candidate cannot bridge a pause, sample gap or recovery.
    for(unsigned reset=0;reset<3;++reset) {
        Pedometer p;const auto one=cadence("isolated cycle",600,1,.10F);
        feed(p,one);CHECK(p.reading().acceptedSteps==0);
        auto t=one.now;
        if(reset==0) {p.pause(true,t);p.pause(false,t);}
        else if(reset==1) p.poll(t+201);
        else {
#ifndef PEDOMETER_BASELINE_METRICS
            p.invalidate(StepStatus::Recovering);
#else
            p.invalidate();
#endif
        }
        t+=3000;feed(p,one,t);CHECK(p.reading().acceptedSteps==0);
    }
#ifndef PEDOMETER_BASELINE_METRICS
    static_assert(static_cast<unsigned>(StepStatus::CounterExhausted)==7,"old values stable");
    static_assert(static_cast<unsigned>(StepStatus::Recovering)==8,"append recovery status");
    CHECK(std::strcmp(stepStatusText(StepStatus::Recovering),"sensor recovering")==0);
    for(unsigned remaining:{0u,1u,2u,3u}) {
        Pedometer p;PedometerTestAccess::count(p,UINT32_MAX-remaining);
        const auto trace=cadence("saturation",600,12);feed(p,trace);
        CHECK(p.reading().acceptedSteps==UINT32_MAX);
        CHECK(p.reading().status==StepStatus::CounterExhausted);
        p.invalidate(StepStatus::Recovering);p.pause(true,trace.now);p.pause(false,trace.now);
        feed(p,trace,trace.now+10000);
        CHECK(p.reading().acceptedSteps==UINT32_MAX);
        CHECK(p.reading().status==StepStatus::CounterExhausted);
    }
#endif
}
}
int main() {
    metrics();safety();
    std::printf("pedometer: %u checks, %u failures; detector=%zu B reading=%zu B; synthetic traces only\n",
                checks,failures,sizeof(digivice::motion::Pedometer),sizeof(digivice::motion::StepReading));
    return failures?1:0;
}

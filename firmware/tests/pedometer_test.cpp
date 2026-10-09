#include "pedometer.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>

namespace {
unsigned checks=0, failures=0;
#define CHECK(x) do {++checks; if(!(x)) {++failures; std::printf("FAIL line %u: %s\n",__LINE__,#x);}} while(0)
constexpr float pi=3.14159265358979323846F;
using namespace digivice::motion;
void stationary(Pedometer& p, std::uint64_t& t, unsigned duration=1200) {
    const auto end=t+duration;
    for(;t<end;t+=20) (void)p.observe({0,0,1},t);
}
// These are generated acceleration fixtures, not recordings of actual walking.
// Simulate the same magnitudes on fixed/rotating axes with deterministic jitter.
std::uint32_t walk(Pedometer& p,std::uint64_t& t,unsigned period,unsigned cycles,
                   float amplitude=.18F,bool rotate=false,bool jitter=false) {
    const auto begin=t, end=t+static_cast<std::uint64_t>(period)*cycles;
    unsigned sample=0;
    for(;t<end;) {
        const float phase=2*pi*static_cast<float>(t-begin)/period;
        const float magnitude=1+amplitude*std::sin(phase);
        const float angle=rotate ? static_cast<float>(t)/1300 : 0;
        auto before=p.reading().acceptedSteps;
        const auto r=p.observe({magnitude*std::sin(angle),0,magnitude*std::cos(angle)},t);
        CHECK(r.acceptedSteps>=before); CHECK(r.status!=StepStatus::InvalidSample);
        t+=jitter ? (sample++%3==0 ? 40 : 20) : 20;
    }
    return p.reading().acceptedSteps;
}
}
int main() {
    using namespace digivice::motion;
    {
        Pedometer p; std::uint64_t t=0;
        stationary(p,t);
        CHECK(p.reading().acceptedSteps==0); CHECK(p.reading().status==StepStatus::Tracking);
        for(unsigned i=0;i<6000;++i,t+=20) {
            const float n=static_cast<float>(static_cast<int>(i%7)-3)*.005F;
            const float angle=static_cast<float>(i)*.08F;
            p.observe({(1+n)*std::sin(angle),0,(1+n)*std::cos(angle)},t);
        }
        CHECK(p.reading().acceptedSteps==0); // Stillness/noise/orientation only.
    }
    for(unsigned period:{300u,400u,600u,1000u,1200u,1400u,1500u}) {
        Pedometer p; std::uint64_t t=0; stationary(p,t);
        const auto count=walk(p,t,period,24);
        CHECK(count>=22 && count<=24);
        std::printf("synthetic cadence %ums: %u/24 cycles\n",period,count);
    }
    for(float amplitude:{.10F,.14F,.24F}) {
        Pedometer p; std::uint64_t t=0; stationary(p,t);
        const auto count=walk(p,t,600,24,amplitude);
        CHECK(count>=22 && count<=24); // Gentle, not vigorous-only threshold.
    }
    {
        Pedometer a,b; std::uint64_t ta=0,tb=0; stationary(a,ta);stationary(b,tb);
        CHECK(walk(a,ta,600,30,.18F)==walk(b,tb,600,30,.18F,true));
    }
    {
        Pedometer p; std::uint64_t t=0; stationary(p,t);
        const auto count=walk(p,t,600,30,.18F,true,true);
        CHECK(count>=28 && count<=30); // One missed20ms sample per three.
    }
    {
        Pedometer p; std::uint64_t t=0; stationary(p,t);
        walk(p,t,600,2); CHECK(p.reading().acceptedSteps==0);
        stationary(p,t,3000); walk(p,t,600,2); CHECK(p.reading().acceptedSteps==0);
        // Large shocks/drop are rejected rather than joined into a later walk.
        CHECK(p.observe({0,0,4},t).status==StepStatus::InvalidSample);t+=20;
        CHECK(p.observe({0,0,0},t).status==StepStatus::InvalidSample);t+=20;
        CHECK(p.observe({0,0,std::numeric_limits<float>::quiet_NaN()},t).status==StepStatus::InvalidSample);t+=20;
        CHECK(p.reading().acceptedSteps==0);
    }
    {
        Pedometer p; std::uint64_t t=0; stationary(p,t);
        walk(p,t,180,40,.24F); CHECK(p.reading().acceptedSteps==0); // Rapid vibration.
    }
    {
        Pedometer p; std::uint64_t t=0; stationary(p,t);walk(p,t,600,12);
        const auto count=p.reading().acceptedSteps;
        CHECK(count>=10);
        const auto last=p.reading().observedAtMs;
        for(unsigned i=0;i<20;++i) p.observe({0,0,1.25F},last);
        CHECK(p.reading().acceptedSteps==count);CHECK(p.reading().observedAtMs==last);
        CHECK(p.poll(last+201).status==StepStatus::Gap);
        CHECK(p.reading().acceptedSteps==count);
        t+=3000; stationary(p,t);walk(p,t,600,2);CHECK(p.reading().acceptedSteps==count);
        CHECK(p.observe({0,0,1},t-100).status==StepStatus::TimeReversed);
        CHECK(p.reading().acceptedSteps==count);
    }
    {
        Pedometer p; std::uint64_t t=0; stationary(p,t);walk(p,t,600,2);
        CHECK(p.pause(true,t).status==StepStatus::Paused);
        walk(p,t,600,20);CHECK(p.reading().acceptedSteps==0);
        CHECK(p.pause(false,t).status==StepStatus::Priming);
        stationary(p,t);walk(p,t,600,2);CHECK(p.reading().acceptedSteps==0);
        walk(p,t,600,12); const auto count=p.reading().acceptedSteps;CHECK(count>0);
        p.pause(true,t);t+=10000;p.pause(false,t);stationary(p,t);
        CHECK(p.reading().acceptedSteps==count); // No paused-time reconstruction.
    }
    std::printf("pedometer: %u checks, %u failures; detector=%zu B reading=%zu B; synthetic traces only\n",
                checks,failures,sizeof(Pedometer),sizeof(StepReading));
    return failures?1:0;
}

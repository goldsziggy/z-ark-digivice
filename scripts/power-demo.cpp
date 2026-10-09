#include "power.hpp"
#include <cstdio>

namespace {
using namespace digivice::power;
class Demo {
public:
    explicit Demo(const char* title) { std::printf("\n%s\n", title); tick(); advance(30); }
    void press() { input.pressed = true; tick(10); advance(30); }
    void release() { input.pressed = false; tick(10); advance(30); }
    void advance(unsigned ms) { while (ms) { const unsigned step = ms > 10 ? 10 : ms; tick(step); ms -= step; } }
    void result(Work work) { input.work = work; tick(10); input.work = Work::Pending; }
    void held() { press(); advance(3000); }
    void surviveCut() { input.latchDropped = true; tick(10); input.latchDropped = false; advance(30); }
private:
    Controller controller;
    Input input{};
    Phase previous = Phase::Failed;
    unsigned countdown = 99;
    void tick(unsigned ms = 0) {
        input.nowMs += ms;
        const auto actions = controller.tick(input);
        const auto& state = controller.status();
        if (state.phase != previous || countdown != state.countdownSeconds) {
            std::printf("%5llu ms  %s", static_cast<unsigned long long>(input.nowMs), phaseName(state.phase));
            if (state.countdownSeconds) std::printf(" (%u)", state.countdownSeconds);
            if (state.failure != Failure::None) std::printf(": %s", failureText(state.failure));
            std::putchar('\n'); previous = state.phase; countdown = state.countdownSeconds;
        }
        if (actions.beginShutdown) std::puts("          Freeze gameplay; drain confirmed steps and active I/O.");
        if (actions.save) std::puts("          Request final durable checkpoint.");
        if (actions.cutLatch) std::puts("          Request SYS_EN LOW after rechecking physical release (SIMULATED).");
        if (actions.resume) std::puts("          Request SYS_EN HIGH first, then resume work (SIMULATED).");
    }
};
} // namespace

int main() {
    std::puts("Same portable power controller as ESP firmware. No GPIO, files, saves or hardware touched.");
    {
        Demo demo("1. Release early: cancel shutdown");
        demo.press(); demo.advance(1300); demo.release();
    }
    {
        Demo demo("2. Battery path: hold, drain, save, release, request power cut");
        demo.held(); demo.result(Work::Ready); demo.result(Work::Ready); demo.release();
        std::puts("          Simulation ends at cut request; real battery rail behavior needs bench validation.");
    }
    {
        Demo demo("3. CPU survives cut: quiet standby; fresh press/release resumes");
        demo.held(); demo.result(Work::Ready); demo.result(Work::Ready); demo.release();
        demo.surviveCut(); demo.press(); demo.release();
    }
    {
        Demo demo("4. Save failure: retain power; deliberate press/release resumes for recovery");
        demo.held(); demo.result(Work::Ready); demo.result(Work::Failed);
        demo.release(); demo.press(); demo.release();
    }
    return 0;
}

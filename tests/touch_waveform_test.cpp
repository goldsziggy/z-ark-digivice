// Tap waveform replay for the Waveshare ESP32-S3-Touch-LCD-1.46 (SPD2010).
//
// Synthetic finger waveforms run through a model of the handheld main loop,
// the firmware's real touch pipeline (touchstream::Stream) and the real
// deviceui::Controller. Timing comes from recorded device evidence:
//   - touch polled every 20 ms outside capture (DIRECT_CAPTURE.md)
//   - full frames due 160 ms after the last one, 80 ms when dirty (handheld_ui.cpp)
//   - frame cost: unit 1 max 29.956 ms render + 45.691 ms flush = 75.6 ms;
//     unit 2 max 140.989 ms (TOUCH_READINESS.md / DEVICE_READINESS.md)
//   - 412 px across a 37.1 mm active area = 11.1 px/mm
// SPD2010 report semantics under polling are not benchmarked, so three are modelled:
//   latched - one report waits until the host reads and clears it (vendor
//             handshake), so a short press survives a stall
//   level   - a read shows the current contact only; a press that starts and
//             ends inside a stall is never seen
//   chatter - latched, but a read between scans decodes "running, no data" as
//             a fresh release while the finger is still down
// The numbers are a model, not physical acceptance. Run with --report for tables.
#include "device_ui.hpp"
#include "touch_stream.hpp"
#include "../firmware/tests/catalog_fixture.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <tuple>
#include <vector>

using namespace digivice;
using namespace digivice::deviceui;
namespace {
unsigned checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (false)
constexpr double kPxPerMm = 412.0 / 37.1;

// ---- Minimal UI harness (same pattern as device_ui_test) --------------------
struct Harness {
    State state = newDevice(12345);
    Model model{};
    onboarding::StarterController starter;
    Controller ui;
    std::uint64_t now = 100;
    Harness() { model.writable = true; model.motionAvailable = true; sync(); }
    void sync() {
        if (state.starterOfferSeed) { model.starterCount = 11; starter.configureChoices(11); model.starterFormId = starterForm(state, starter.selectedId()); }
        model.starterStage = starter.stage(); model.selectedId = starter.selectedId(); ui.update(state, model);
    }
    Intent event(TouchKind kind, int x, int y, std::uint64_t delta = 30) {
        now += delta; return ui.touch(state, model, {kind, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), now});
    }
    Intent tap(int x, int y) { const auto d = event(TouchKind::Down, x, y); const auto u = event(TouchKind::Up, x, y, 60); return d ? d : u; }
    void dispatch(Intent intent) {
        CHECK(intent);
        onboarding::Request request;
        switch (intent.kind) {
        case IntentKind::GameAction: CHECK(apply(state, intent.action, intent.value) == Error::None); break;
        case IntentKind::StarterConfirm: request = starter.input(onboarding::Input::Confirm); break;
        case IntentKind::StarterNext: starter.input(onboarding::Input::Next); break;
        case IntentKind::StarterPrevious: starter.input(onboarding::Input::Previous); break;
        default: break;
        }
        if (request.hatchId) { CHECK(apply(state, Action::Hatch, request.hatchId) == Error::None); starter.resolve(true); }
        ui.resolve(nullptr); sync();
    }
    void home() {
        dispatch(tap(206, 285)); dispatch(tap(206, 312)); dispatch(tap(206, 278));
        CHECK(ui.screen() == Screen::Home);
    }
    void open(HomePanel panel) {
        for (unsigned i = 0; i < 4 && ui.homePanel() != panel; ++i) dispatch(tap(355, 190));
        CHECK(ui.homePanel() == panel); dispatch(tap(206, 323));
    }
};

// ---- Finger and SPD2010 models ------------------------------------------------
struct Finger {
    std::uint64_t downMs = 0, upMs = 0;
    double x = 0, y = 0;          // aimed contact centroid
    double jitterPx = 2;          // per-report centroid noise (sigma)
    double driftX = 0, driftY = 0; // roll-off drift reached at lift, ramped over the last 40 ms
    double moveX = 0, moveY = 0;   // deliberate swipe travel, linear over the whole contact
    bool touching(std::uint64_t t) const { return t >= downMs && t < upMs; }
};
struct Report { bool pressed = false, fresh = false; std::int16_t x = 0, y = 0; };

enum class Semantics { Latched, Level, Chatter };
struct Spd2010 {
    Semantics semantics; Finger finger; std::mt19937* rng;
    bool pending = false; Report slot{}; bool lastGeneratedPressed = false, lastDeliveredPressed = false;
    std::uint64_t generatedThrough = 0;
    Report sense(std::uint64_t t) {
        Report r; r.pressed = finger.touching(t); r.fresh = true;
        if (r.pressed) {
            std::normal_distribution<double> n(0, finger.jitterPx);
            double ramp = 0;
            if (finger.upMs - t < 40) ramp = 1.0 - static_cast<double>(finger.upMs - t) / 40.0;
            const double along = finger.upMs > finger.downMs ? static_cast<double>(t - finger.downMs) / static_cast<double>(finger.upMs - finger.downMs) : 0;
            r.x = static_cast<std::int16_t>(std::lround(finger.x + n(*rng) + ramp * finger.driftX + along * finger.moveX));
            r.y = static_cast<std::int16_t>(std::lround(finger.y + n(*rng) + ramp * finger.driftY + along * finger.moveY));
        }
        return r;
    }
    void generate(std::uint64_t t) { // 100 Hz scan; one outstanding report under the handshake
        for (std::uint64_t g = generatedThrough + 10; g <= t; g += 10) {
            generatedThrough = g;
            if (pending) continue;
            const auto r = sense(g);
            if (r.pressed || lastGeneratedPressed) { slot = r; pending = true; lastGeneratedPressed = r.pressed; }
        }
    }
    Report read(std::uint64_t t) {
        if (semantics != Semantics::Level) {
            generate(t);
            if (!pending) {
                if (semantics == Semantics::Chatter) return {false, true, 0, 0}; // "running, no data"
                return {lastDeliveredPressed, false, slot.x, slot.y};
            }
            pending = false; lastDeliveredPressed = slot.pressed; return slot;
        }
        const auto r = sense(t);
        if (r.pressed) { lastDeliveredPressed = true; return r; }
        if (lastDeliveredPressed) { lastDeliveredPressed = false; return {false, true, 0, 0}; }
        return {false, false, 0, 0};
    }
};

// ---- Main-loop model --------------------------------------------------------
struct LoopConfig {
    double renderMs = 29.956, flushMs = 45.691; // unit 1 recorded maxima
    bool redrawEverySample = true;              // legacy: every held sample marks the frame dirty
    bool flushSampling = false;                 // poll touch between DMA stripes
    touchstream::Options stream{0, 0, 0};          // legacy: immediate Up, latch clears only on a release report
};
struct Outcome { bool any = false; unsigned intents = 0; Screen screen = Screen::Egg; IntentKind kind = IntentKind::None; HomePanel panel = HomePanel::Care; };

struct Sim {
    Harness& h; LoopConfig cfg; Spd2010 touch; touchstream::Stream stream;
    std::uint64_t lastPoll = 0; double lastFrameEnd = 0; bool dirty = false; bool blanked = false;
    bool errorNext = false; Outcome out;
    Sim(Harness& harness, LoopConfig c, Semantics s, Finger f, std::mt19937& rng)
        : h(harness), cfg(c), touch{s, f, &rng}, stream(c.stream) {}
    void sample(std::uint64_t t) {
        lastPoll = t;
        touchstream::Sample s;
        if (errorNext) { errorNext = false; s.ok = false; s.atMs = t; }
        else { const auto r = touch.read(t); s = {true, r.pressed, r.fresh, r.x, r.y, t, blanked}; }
        const auto step = stream.feed(s);
        static const bool debug = std::getenv("WAVE_DEBUG") != nullptr;
        if (debug && t > 1000) std::fprintf(stderr, "  t=%llu read p=%d f=%d (%d,%d) -> ev=%d kind=%d\n", (unsigned long long)t, s.pressed, s.fresh, s.x, s.y, step.hasEvent, (int)step.event.kind);
        if (step.cancel) h.ui.cancelTouch();
        if (step.released) h.ui.acknowledgeContactReleased();
        if (step.hasEvent) {
            const bool visual = step.event.kind != TouchKind::Move;
            const auto intent = h.ui.touch(h.state, h.model, step.event);
            if (cfg.redrawEverySample || visual) dirty = true;
            if (intent) {
                ++out.intents;
                if (!out.any) { out.any = true; out.kind = intent.kind; }
                dirty = true;
                if (intent.kind == IntentKind::Navigation) h.ui.resolve(nullptr); // home carousel swipes are local
            }
        }
    }
    void frame(double& t) {
        t += cfg.renderMs;
        const int stripes = 26; // ceil(412 / 16 rows)
        for (int i = 0; i < stripes; ++i) {
            t += cfg.flushMs / stripes;
            // Same 20 ms cadence as the main loop: the UI rejects taps shorter than 20 ms.
            if (cfg.flushSampling && t - static_cast<double>(lastPoll) >= 20.0) sample(static_cast<std::uint64_t>(t));
        }
        lastFrameEnd = t; dirty = false;
    }
    void run(double from, double until) {
        double t = from;
        while (t < until) {
            const auto now = static_cast<std::uint64_t>(t);
            if (now - lastPoll >= 20) sample(now);
            const double since = t - lastFrameEnd;
            if (!blanked && ((dirty && since >= 80) || since >= 160)) frame(t);
            t += 10; // one RTOS tick between loop passes
        }
        out.screen = h.ui.screen(); out.panel = h.ui.homePanel();
    }
};

struct Target { const char* name; HomePanel panel; int x, y; Screen expectScreen; IntentKind expectKind; double moveX = 0; };
const Target kBack{"BACK", HomePanel::Settings, 206, 356, Screen::Home, IntentKind::None};
const Target kMode{"MODE (row button)", HomePanel::Settings, 131, 304, Screen::ModeReview, IntentKind::None};
const Target kGyro{"GYRO (row button)", HomePanel::Settings, 280, 252, Screen::Settings, IntentKind::ToggleGyro};
const Target kTimeout{"SCREEN TIMEOUT", HomePanel::Settings, 206, 139, Screen::Settings, IntentKind::SleepTimeout};
// Home carousel swipe: exactly one panel step, never an open.
const Target kSwipe{"HOME SWIPE", HomePanel::Care, 260, 190, Screen::Home, IntentKind::Navigation, -110};

bool success(const Target& target, const Outcome& o, HomePanel startPanel) {
    if (target.moveX != 0) return o.screen == Screen::Home && o.intents == 1 && o.panel != startPanel;
    if (target.expectKind != IntentKind::None) return o.any && o.kind == target.expectKind;
    return o.screen == target.expectScreen;
}

struct TrialSpec {
    Semantics semantics = Semantics::Latched; LoopConfig loop{}; Target target = kBack;
    double durationMs = 100, aimSigmaPx = 0, jitterPx = 2, driftPx = 0;
    double blankBeforeMs = 0, tapAfterWakeMs = 0; bool errorBefore = false;
    bool warm = true;   // an earlier tap on empty glass: steady state, latch already cleared
};
bool trial(const TrialSpec& spec, std::mt19937& rng) {
    Harness h; h.home();
    if (spec.target.moveX == 0) { h.open(spec.target.panel); CHECK(h.ui.screen() == Screen::Settings); }
    const auto startPanel = h.ui.homePanel();
    std::uniform_real_distribution<double> phase(0, 200);
    std::normal_distribution<double> aim(0, spec.aimSigmaPx > 0 ? spec.aimSigmaPx : 1e-9);
    std::uniform_real_distribution<double> angle(0, 6.283185307);
    const double base = static_cast<double>(h.now) + 100;
    const double start = base + (spec.warm ? 600 : 0);
    Finger f;
    f.downMs = static_cast<std::uint64_t>(start + 400 + spec.blankBeforeMs + spec.tapAfterWakeMs + phase(rng));
    f.upMs = f.downMs + static_cast<std::uint64_t>(spec.durationMs);
    f.x = spec.target.x + (spec.aimSigmaPx > 0 ? aim(rng) : 0);
    f.y = spec.target.y + (spec.aimSigmaPx > 0 ? aim(rng) : 0);
    f.jitterPx = spec.jitterPx; f.moveX = spec.target.moveX;
    const double a = angle(rng); f.driftX = spec.driftPx * std::cos(a); f.driftY = spec.driftPx * std::sin(a);
    Sim sim(h, spec.loop, spec.semantics, f, rng);
    sim.lastPoll = static_cast<std::uint64_t>(base); sim.lastFrameEnd = base - phase(rng) * 0.8;
    sim.touch.generatedThrough = static_cast<std::uint64_t>(base);
    sim.stream.requireRelease(static_cast<std::uint64_t>(base)); // boot/resume state, as in the firmware
    double t = base;
    if (spec.warm) {   // first touch after boot clears the latch on an empty spot, then the real tap
        Finger w; w.downMs = static_cast<std::uint64_t>(t + 20); w.upMs = w.downMs + 300; w.x = 60; w.y = 120; w.jitterPx = 1;
        Spd2010 real = sim.touch; sim.touch.finger = w; sim.run(t, t + 600);
        const auto gen = sim.touch.generatedThrough; sim.touch = real; sim.touch.generatedThrough = gen;
        sim.touch.pending = false; sim.touch.lastGeneratedPressed = sim.touch.lastDeliveredPressed = false;
        CHECK(!sim.out.any && !sim.stream.awaitingRelease()); t += 600;
    }
    if (spec.errorBefore) { sim.run(t, t + 200); sim.errorNext = true; sim.run(t + 200, t + 260); t += 260; }
    if (spec.blankBeforeMs > 0) {
        sim.run(t, t + 300); sim.blanked = true; sim.run(t + 300, t + 300 + spec.blankBeforeMs);
        sim.blanked = false; t += 300 + spec.blankBeforeMs; // motion wake, no finger on the glass
    }
    sim.run(t, static_cast<double>(f.upMs) + 2500);
    return success(spec.target, sim.out, startPanel);
}
double rate(const TrialSpec& spec, unsigned n, unsigned seed) {
    std::mt19937 rng(seed); unsigned ok = 0;
    for (unsigned i = 0; i < n; ++i) ok += trial(spec, rng);
    return 100.0 * ok / n;
}

LoopConfig legacyLoop(double render = 29.956, double flush = 45.691) { LoopConfig c; c.renderMs = render; c.flushMs = flush; return c; }
LoopConfig fixedLoop(double render = 29.956, double flush = 45.691) {
    LoopConfig c = legacyLoop(render, flush); c.redrawEverySample = false; c.flushSampling = true; c.stream = touchstream::Options{}; return c;
}
const char* name(Semantics s) { return s == Semantics::Latched ? "latched" : s == Semantics::Level ? "level" : "chatter"; }
constexpr Semantics kAll[]{Semantics::Latched, Semantics::Level, Semantics::Chatter};
const double kHeavyR = 140.989 * 29.956 / 75.647, kHeavyF = 140.989 - kHeavyR;

void report(unsigned n) {
    std::printf("\n## Steady-state taps and swipes by SPD2010 model (%u trials/cell, 76 ms frames)\n\n", n);
    std::printf("| Gesture | SPD2010 | Before %% | After %% |\n| --- | --- | --- | --- |\n");
    for (const auto& [label, target, ms] : {std::tuple<const char*, Target, double>{"BACK tap 100 ms", kBack, 100.0},
            {"BACK tap 60 ms", kBack, 60.0}, {"MODE tap 100 ms", kMode, 100.0}, {"Home swipe 250 ms", kSwipe, 250.0}})
        for (Semantics s : kAll) {
            TrialSpec a; a.semantics = s; a.target = target; a.durationMs = ms; a.loop = legacyLoop();
            TrialSpec b = a; b.loop = fixedLoop();
            std::printf("| %s | %s | %.1f | %.1f |\n", label, name(s), rate(a, n, 11), rate(b, n, 11));
        }
    std::printf("\n## Quick-tap survival, BACK centre, 141 ms frames (unit 2 worst case)\n\n| Tap ms | SPD2010 | Before %% | After %% |\n| --- | --- | --- | --- |\n");
    for (Semantics s : kAll) for (double d : {40.0, 60.0, 80.0, 120.0}) {
        TrialSpec a; a.semantics = s; a.durationMs = d; a.loop = legacyLoop(kHeavyR, kHeavyF);
        TrialSpec b = a; b.loop = fixedLoop(kHeavyR, kHeavyF);
        std::printf("| %.0f | %s | %.1f | %.1f |\n", d, name(s), rate(a, n, 13), rate(b, n, 13));
    }
    std::printf("\n## First tap after boot / motion wake / bus error (100 ms tap)\n\n| Case | SPD2010 | Before %% | After %% |\n| --- | --- | --- | --- |\n");
    for (Semantics s : kAll) {
        { TrialSpec a; a.semantics = s; a.warm = false; a.loop = legacyLoop(); TrialSpec b = a; b.loop = fixedLoop();
          std::printf("| boot, first tap | %s | %.1f | %.1f |\n", name(s), rate(a, n, 23), rate(b, n, 23)); }
        for (double wake : {200.0, 1000.0}) {
            TrialSpec a; a.semantics = s; a.blankBeforeMs = 2000; a.tapAfterWakeMs = wake; a.loop = legacyLoop();
            TrialSpec b = a; b.loop = fixedLoop();
            std::printf("| motion wake, tap %.0f ms later | %s | %.1f | %.1f |\n", wake, name(s), rate(a, n, 21), rate(b, n, 21));
        }
        TrialSpec a; a.semantics = s; a.errorBefore = true; a.loop = legacyLoop(); TrialSpec b = a; b.loop = fixedLoop();
        std::printf("| one I2C error, tap ~400 ms later | %s | %.1f | %.1f |\n", name(s), rate(a, n, 22), rate(b, n, 22));
    }
    std::printf("\n## Lift drift (roll-off) at release, 100 ms tap, latched\n\n| Drift mm | BACK before %% | BACK after %% | MODE before %% | MODE after %% |\n| --- | --- | --- | --- | --- |\n");
    for (double mm : {0.0, 1.0, 2.0, 3.0, 4.0}) {
        TrialSpec a; a.driftPx = mm * kPxPerMm; a.loop = legacyLoop(); TrialSpec b = a; b.loop = fixedLoop();
        TrialSpec c = a; c.target = kMode; TrialSpec d = b; d.target = kMode;
        std::printf("| %.0f | %.1f | %.1f | %.1f | %.1f |\n", mm, rate(a, n, 31), rate(b, n, 31), rate(c, n, 31), rate(d, n, 31));
    }
    std::printf("\n## Aim scatter (sigma) vs target, 100 ms tap, fixed loop\n\n| Sigma mm | BACK 180x48+pad %% | MODE 139x44 %% | GYRO 139x44 %% | SCREEN TIMEOUT 212x44 %% |\n| --- | --- | --- | --- | --- |\n");
    for (double mm : {1.0, 1.5, 2.0, 2.5}) {
        TrialSpec a; a.aimSigmaPx = mm * kPxPerMm; a.loop = fixedLoop();
        TrialSpec b = a; b.target = kMode; TrialSpec c = a; c.target = kGyro; TrialSpec d = a; d.target = kTimeout;
        std::printf("| %.1f | %.1f | %.1f | %.1f | %.1f |\n", mm, rate(a, n, 41), rate(b, n, 41), rate(c, n, 41), rate(d, n, 41));
    }
}

void streamUnits() {
    using touchstream::Stream; using touchstream::Options;
    {   // Legacy parity: options {0,0} emit exactly the old handheld events.
        Stream s(Options{0, 0, 0}); s.requireRelease(0);
        CHECK(!s.feed({true, true, true, 10, 10, 100}).hasEvent);          // starts latched
        CHECK(s.feed({true, false, true, 0, 0, 120}).released);           // observed release rearms
        auto d = s.feed({true, true, true, 50, 60, 140}); CHECK(d.hasEvent && d.event.kind == TouchKind::Down && d.activity);
        auto m = s.feed({true, true, false, 50, 60, 160}); CHECK(m.hasEvent && m.event.kind == TouchKind::Move && !m.activity);
        auto u = s.feed({true, false, true, 0, 0, 180}); CHECK(u.hasEvent && u.event.kind == TouchKind::Up && u.event.x == 50 && u.event.y == 60);
        CHECK(!s.feed({true, false, false, 0, 0, 200}).hasEvent);
        auto e = s.feed({false, false, false, 0, 0, 220}); CHECK(e.cancel && s.awaitingRelease());
        for (std::uint64_t t = 240; t < 2000; t += 20) CHECK(!s.feed({true, false, false, 0, 0, t}).released); // legacy never rearms
    }
    {   // Confirmed release: a between-scan "release" under a held finger is not Up.
        Stream s; s.requireRelease(0); CHECK(s.feed({true, false, true, 0, 0, 10}).released);
        CHECK(s.feed({true, true, true, 40, 40, 20}).event.kind == TouchKind::Down);
        CHECK(!s.feed({true, false, true, 0, 0, 25}).hasEvent);          // 5 ms later: chatter read
        auto m = s.feed({true, true, true, 55, 40, 30}); CHECK(!m.hasEvent && s.chatter() == 1); // held back one sample
        CHECK(!s.feed({true, false, true, 0, 0, 40}).hasEvent);          // real lift seen at 40
        auto u = s.feed({true, false, false, 0, 0, 60});                 // 30 ms sample is roll-off: dropped
        CHECK(u.hasEvent && u.event.kind == TouchKind::Up && u.event.atMs == 40 && u.event.x == 40 && s.rolledOff() == 1);
        CHECK(s.releases() == 1 && s.presses() == 1);
    }
    {   // Held-back Moves keep their own timestamps; an old last sample is a real lift position.
        Stream s; s.requireRelease(0); s.feed({true, false, true, 0, 0, 5});
        CHECK(s.feed({true, true, true, 100, 100, 20}).event.kind == TouchKind::Down);
        CHECK(!s.feed({true, true, true, 110, 100, 40}).hasEvent);
        auto m1 = s.feed({true, true, true, 120, 100, 60}); CHECK(m1.event.kind == TouchKind::Move && m1.event.x == 110 && m1.event.atMs == 40);
        auto u = s.feed({true, false, true, 0, 0, 100});                 CHECK(u.event.kind == TouchKind::Up && u.event.x == 120 && s.rolledOff() == 0); // lift 40 ms after the last sample: Up at once, real position
    }
    {   // Quiet rearm: only when no fresh contact was seen while latched.
        Stream s; s.requireRelease(0);
        CHECK(!s.feed({true, false, false, 0, 0, 100}).released);
        auto r = s.feed({true, false, false, 0, 0, 160}); CHECK(r.released && r.inferred && !s.awaitingRelease() && s.rearms() == 1);
        s.requireRelease(200);
        for (std::uint64_t t = 220; t < 1500; t += 20) CHECK(!s.feed({true, true, true, 9, 9, t}).released && s.awaitingRelease()); // finger held at wake
        auto o = s.feed({true, false, true, 0, 0, 1500}); CHECK(o.released && !o.inferred);  // observed lift
        s.requireRelease(1600);
        for (std::uint64_t t = 1620; t < 1800; t += 20) { const auto b = s.feed({true, false, false, 0, 0, t, true}); CHECK(b.cancel && !b.released); }
        CHECK(s.feed({true, false, false, 0, 0, 1960}).released);       // motion wake: rearms 150 ms after the last blanked poll
    }
    {   // A stale cached "pressed" level neither holds the latch nor starts a phantom Down.
        Stream s; s.requireRelease(0);
        CHECK(!s.feed({true, true, true, 70, 70, 10}).released);         // finger on glass at wake
        for (std::uint64_t t = 30; t < 160; t += 20) CHECK(!s.feed({true, true, false, 70, 70, t}).released);
        CHECK(s.feed({true, true, false, 70, 70, 170}).released);        // stale level only: quiet since 10
        for (std::uint64_t t = 190; t < 400; t += 20) CHECK(!s.feed({true, true, false, 70, 70, t}).hasEvent);
        auto d = s.feed({true, true, true, 80, 90, 420}); CHECK(d.hasEvent && d.event.kind == TouchKind::Down && d.event.x == 80);
    }
}
} // namespace

int main(int argc, char** argv) {
    const bool full = argc > 1 && !std::strcmp(argv[1], "--report");
    if (argc > 1 && !std::strcmp(argv[1], "--trace")) {   // first failing quick latched tap on slow frames, traced
        TrialSpec b; b.durationMs = 40; b.loop = fixedLoop(kHeavyR, kHeavyF);
        std::mt19937 rng(13);
        for (unsigned i = 0; i < 400; ++i) { const auto save = rng; if (!trial(b, rng)) { std::printf("fail at %u\n", i); setenv("WAVE_DEBUG", "1", 1); auto r2 = save; trial(b, r2); return 0; } }
        return 0;
    }
    streamUnits();
    const unsigned n = full ? 400 : 60;
    // Regression gates on the model (deterministic seeds); each reproduces a legacy failure.
    for (Semantics s : kAll) {
        TrialSpec held; held.semantics = s; held.durationMs = 120; held.loop = fixedLoop();
        CHECK(rate(held, n, 12) > 99.0);                                   // ordinary taps always land
        TrialSpec swipe; swipe.semantics = s; swipe.target = kSwipe; swipe.durationMs = 250; swipe.loop = fixedLoop();
        CHECK(rate(swipe, n, 14) > 99.0);                                  // one swipe = one panel step
        TrialSpec wake; wake.semantics = s; wake.blankBeforeMs = 2000; wake.tapAfterWakeMs = 400; wake.loop = fixedLoop();
        CHECK(rate(wake, n, 21) > 99.0);                                   // first tap after a wake lands
        TrialSpec err; err.semantics = s; err.errorBefore = true; err.loop = fixedLoop();
        CHECK(rate(err, n, 22) > 99.0);                                    // and after an I2C error
    }
    TrialSpec bootOld; bootOld.warm = false; bootOld.loop = legacyLoop();
    CHECK(rate(bootOld, n, 12) < 5.0);  // latched: the legacy loop eats the first tap after boot
    TrialSpec wakeOld; wakeOld.blankBeforeMs = 2000; wakeOld.tapAfterWakeMs = 400; wakeOld.loop = legacyLoop();
    CHECK(rate(wakeOld, n, 21) < 5.0);   // latched: the legacy loop eats the first tap after a wake
    TrialSpec quick; quick.semantics = Semantics::Level; quick.durationMs = 60;
    TrialSpec quickOld = quick; quickOld.loop = legacyLoop(); quick.loop = fixedLoop();
    CHECK(rate(quick, n, 11) >= rate(quickOld, n, 11) + 10.0); // stripe sampling shrinks the blind window
    TrialSpec drift; drift.driftPx = 4 * kPxPerMm; drift.target = kMode;
    TrialSpec driftOld = drift; driftOld.loop = legacyLoop(); drift.loop = fixedLoop();
    CHECK(rate(drift, n, 31) >= rate(driftOld, n, 31) + 10.0 && rate(drift, n, 31) > 99.0); // roll-off filter keeps the tap
    if (full) report(n);
    std::printf("touch waveform: %u checks passed (%s)\n", checks, full ? "report" : "gates");
    return 0;
}

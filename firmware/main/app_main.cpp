#include "board_hal.hpp"
#include "device_entropy.hpp"
#include "game.hpp"
#include "handheld_runtime.hpp"
#include "nvs_backend.hpp"
#include "save_store.hpp"
#include "trade_nvs.hpp"

#include "sdkconfig.h"
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <unistd.h>
#include <utility>

namespace {
using namespace digivice;

// The 64 KiB main stack is reserved from internal RAM before app_main. The
// 250-member runtime no longer leaves a contiguous internal block that large,
// so the long-lived game objects are created in PSRAM after the scheduler starts.
template <typename T, typename... Args>
T& permanentPsram(Args&&... args) {
    void* memory = heap_caps_aligned_alloc(alignof(T), sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        std::printf("PSRAM allocation failed (%zu bytes). Existing saves were not erased.\n", sizeof(T));
        for (;;) vTaskDelay(portMAX_DELAY);
    }
    return *new (memory) T(std::forward<Args>(args)...);
}

// Keep the return-by-value initialization temporary out of app_main's frame,
// which remains live beneath every save/trade call for the lifetime of the task.
__attribute__((noinline)) void initializeBootState(State& state, bool fresh, std::uint32_t seed) {
    state = fresh ? newDevice(seed) : newGame();
}

esp_err_t initializeConsoleInput() {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    // IDF 5.3.6 nonblocking VFS reads inspect the driver's RX ring, not the
    // raw FIFO. Install it before enabling O_NONBLOCK or input stays empty.
    usb_serial_jtag_driver_config_t config{};
    // One 512-byte hex transfer frame plus command fields fits without RX loss.
    config.rx_buffer_size = 2048;
    config.tx_buffer_size = 1024;
    const auto error = usb_serial_jtag_driver_install(&config);
    if (error != ESP_OK) return error;
    usb_serial_jtag_vfs_use_driver();
#endif
    std::setvbuf(stdin, nullptr, _IONBF, 0);
    return ESP_OK;
}

void printState(const State& state) {
    // Cold diagnostics use one bounded temporary buffer. Prefer external RAM
    // on the display board; generic serial builds use the ordinary heap.
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    auto* json = static_cast<char*>(heap_caps_malloc(kJsonCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    auto* json = static_cast<char*>(heap_caps_malloc(kJsonCapacity, MALLOC_CAP_8BIT));
#endif
    if (!json) { std::puts("State JSON unavailable: insufficient diagnostic memory; saved game unchanged."); return; }
    if (writeJson(state, json, kJsonCapacity)) std::puts(json);
    else std::puts("State JSON exceeded diagnostic buffer; saved game unchanged.");
    heap_caps_free(json);
}

void help() {
    std::puts("Commands: status | feed | play | rest | walk 100 | card 1 | card 2 | attack | heavy | magic | capture | select 1");
    std::puts("Full recovery (Home): rest full | rest full confirm | rest full cancel/status. Review first; one checkpoint.");
    std::puts("attack/physical: normal hit; heavy: 6 energy required; magic: magic vs resistance.");
    std::puts("Wild modes (Home): mode tactical | mode auto | mode confirm | mode cancel | mode status");
    std::puts("After confirming Auto: auto fights to the capture opportunity, then tap the screen or auto-resume to continue without capture.");
    std::puts("Capture timing harness: ring-capture <phaseMs 0..2399>. Explicit simulated timing; missing or invalid phase is rejected.");
    std::puts("Digivolution: evolve status | evolve <formId> | evolve confirm | evolve cancel. Preview first; confirmation saves once.");
    std::puts("Collection: companions | journal | select <stable ID> | release <stable ID> confirm (nonactive; Home or new encounters).");
    std::puts("Power: power status; Waveshare onboard PWR hold 3 seconds, early release cancels. Quiet standby resumes with short PWR press/release.");
    std::puts("Tools: snapshot | checkpoint | capabilities | reboot | help; device status/sound/mute/unmute/recenter");
    std::puts("First run: starter status/confirm/next/back | hatch 1..11 (saved offer slot). Back simulates a held button.");
    std::puts("Slots 1..8 are fixed starters; 9..11 are this device's three saved Rookie offers. Choosing grants one partner.");
    std::puts("Starters: 1 Impmon, 2 Agumon, 3 Gabumon, 4 Patamon, 5 Tentomon, 6 Palmon, 7 Gomamon, 8 Renamon.");
    std::puts("Walk/card are SIMULATED inputs. card 1=attack boost; card 2=shield.");
    std::puts("Network: net status/set/set-lan/retry/pause/resume/forget; see docs/FIRMWARE.md for private setup.");
    std::puts("Assets: assets status/warm/fetch <id>; motion status/recover (explicit reanchor after confirmed queue drains). NFC/GPS/battery telemetry remain disabled.");
    std::puts("Exact-form private SD artwork: art status | art preview (inspection only; optional build gate, resident fallback).");
    std::puts("USB asset installer: device identity | sdput status/begin/chunk/finish/verify/abort. Installer pauses play; abort resumes it.");
    std::puts("Practice: practice help | practice status. Separate Tactical/Auto serial session, independent NVS records.");
}

void capabilities(const board::Capabilities& caps) {
    std::printf("display=%d touch=%d motion=%d nfc=%d gps=%d network=%d battery=%d charging=%d\n",
                caps.display, caps.touch, caps.motionSteps, caps.nfc, caps.gps,
                caps.network, caps.batteryTelemetry, caps.chargingControl);
}

// Bounded line input; a too-long line is discarded in full, never executed as a
// truncated command. Nonblocking USB console may report EOF when no byte exists.
bool readLine(char* line, std::size_t capacity, HandheldRuntime& runtime) {
    std::size_t length = 0;
    bool overflow = false, discardLine = false;
    auto inputEpoch = runtime.inputEpoch();
    for (;;) {
        runtime.poll();
        if (inputEpoch != runtime.inputEpoch()) {
            inputEpoch = runtime.inputEpoch();
            auto* secret = reinterpret_cast<volatile unsigned char*>(line);
            for (std::size_t i=0;i<capacity;++i) secret[i]=0;
            length=0; overflow=false; discardLine=true;
            std::puts("Power transition: partial console input discarded. Press Enter before a fresh command.");
        }
        const int ch = std::getchar();
        if (ch == EOF) {
            std::clearerr(stdin);
            vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(runtime.recommendedPollDelayMs())));
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            if (discardLine) { discardLine=false; continue; }
            if (length == 0 && !overflow) continue;
            line[length] = '\0';
            if (overflow) std::printf("Input too long; discarded (%zu bytes maximum).\n", capacity - 1);
            return !overflow;
        }
        if (discardLine) continue;
        if (ch == '\b' || ch == 127) {
            if (!overflow && length) --length;
            continue;
        }
        // Preserve SSID bytes; configuration validation rejects control bytes.
        if (ch < 32 || ch > 255) continue;
        if (length + 1 < capacity && !overflow) line[length++] = static_cast<char>(ch);
        else overflow = true;
    }
}

bool parseUnsigned(const char* value, std::uint32_t& result) {
    if (!value || *value < '0' || *value > '9') return false;
    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtoul(value, &end, 10);
    if (errno || *end || parsed > UINT32_MAX) return false;
    result = static_cast<std::uint32_t>(parsed);
    return true;
}

void command(char* line, State& state, storage::SaveStore& saves,
             const board::Capabilities& caps, const HandheldRuntime& runtime) {
    if (runtime.powerFrozen()) { std::puts("Power transition blocks commands."); return; }
    char* context = nullptr;
    const char* name = strtok_r(line, " \t", &context);
    const char* value = strtok_r(nullptr, " \t", &context);
    if (!name) return;
    if (strtok_r(nullptr, " \t", &context)) {
        std::puts("Too many arguments.");
        return;
    }
    if (!value) {
        if (std::strcmp(name, "help") == 0) { help(); return; }
        if (std::strcmp(name, "capabilities") == 0) { capabilities(caps); return; }
        if (std::strcmp(name, "status") == 0) {
            std::printf("Storage: %s\n", saves.diagnostic());
            printState(state);
            return;
        }
        if (std::strcmp(name, "snapshot") == 0) {
            Snapshot snapshot;
            if (encodeSnapshot(state, snapshot)) {
                std::puts("RAM snapshot hex (recovery mode may show a fallback; preserve raw NVS separately):");
                for (const auto byte : snapshot.bytes) std::printf("%02x", byte);
                std::putchar('\n');
            }
            return;
        }
        if (std::strcmp(name, "checkpoint") == 0) {
            const bool saved = saves.checkpoint(state);
            std::printf("Checkpoint %s: %s\n", saved ? "OK" : "FAILED", saves.diagnostic());
            return;
        }
        if (std::strcmp(name, "reboot") == 0) {
            if (saves.writable() && !saves.checkpoint(state)) {
                std::printf("Reboot postponed: %s\n", saves.diagnostic());
                return;
            }
            std::puts("Restarting; recovery mode never erases saves.");
            std::fflush(stdout);
            esp_restart();
            return;
        }
    }
    Action action;
    if (!parseAction(name, action)) { std::puts("Unknown command; type help."); return; }
    if (action == Action::Auto) action = Action::AutoFight; // Historical Auto remains replay-only.
    if (action == Action::Mode) {
        std::puts("Choose mode tactical or mode auto, then mode confirm; direct numeric mode changes are disabled.");
        return;
    }
    if (action == Action::Evolve || action == Action::Release) {
        std::puts("Use the explicit confirmed evolve/release command; an unconfirmed change is disabled.");
        return;
    }
    std::uint32_t argument = 0;
    const bool takesValue = action == Action::Walk || action == Action::Card || action == Action::Select || action == Action::Hatch || action == Action::RingCapture;
    if ((takesValue && !parseUnsigned(value, argument)) || (!takesValue && value)) {
        std::puts("walk/card/select/hatch/ring-capture require an unsigned integer; other actions take no argument.");
        return;
    }
    if (!saves.writable()) {
        std::printf("Play paused to protect existing saves: %s\n", saves.diagnostic());
        return;
    }
    if (!runtime.allowsCareAction(action)) {
        std::puts("Partner changes and new walks are paused during active practice or practice recovery; care remains available.");
        return;
    }
    State next = state;
    const auto error = apply(next, action, argument);
    if (error != Error::None) { std::printf("Rejected: %s\n", errorText(error)); return; }
    // Every command, including an Auto attack chunk, is acknowledged only after
    // its complete state is durable. Duplicate Auto while awaiting a flick is rejected by
    // the core's phase guard; uncertain writes freeze commands until recovery. No
    // physical step interrupt will write flash per step; future HAL batches it.
    if (!saves.checkpoint(next)) {
        std::printf("Command durability uncertain; play paused: %s\n", saves.diagnostic());
        return;
    }
    state = next;
    std::printf("%s: %s (saved sequence %" PRIu32 ")\n",
                creatureName(state), messageText(state.message), state.sequence);
    printState(state);
}
} // namespace

extern "C" void app_main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const auto consoleInitialization = initializeConsoleInput();
    const auto caps = digivice::board::initialize();
    // Power hold is asserted; no ADC, RF or audio owner has started yet.
    const auto startupSeeds = digivice::device::collectStartupEntropy();
    std::printf("\nDigivice v0.1 / %s\n", digivice::board::boardName());
    capabilities(caps);
    static digivice::storage::NvsBackend backend;
    const auto initialization = backend.initialize();
    if (initialization != ESP_OK) {
        std::printf("NVS initialization failed (%s). Recovery mode; partition was NOT erased.\n",
                    esp_err_to_name(initialization));
    }
    static auto& saves = permanentPsram<digivice::storage::SaveStore>(backend);
    static auto& state = permanentPsram<digivice::State>();
    initializeBootState(state, false, 0); // Inspection fallback; never enroll or save this default.
    const auto boot = saves.restore(state);
    std::printf("Boot storage: %s\n", saves.diagnostic());
    if (boot == digivice::storage::BootStatus::Empty) {
        // Empty means BOTH slots are explicitly missing. Corruption, future
        // schema, unreadable NVS and restored older saves never restart hatch.
        if (!digivice::devicetrade::freshCareStorageAllowed()) {
            saves.requireRecovery("care slots missing with trade journal evidence or unreadable trade storage; no new game created");
            std::printf("RECOVERY: %s\n", saves.diagnostic());
        } else if (!startupSeeds.ready()) {
            saves.requireRecovery("startup entropy/identity unavailable; empty storage retained, no fixed-seed profile created");
            std::printf("RECOVERY: %s\n", saves.diagnostic());
        } else {
            initializeBootState(state, true, startupSeeds.profile);
            if (!saves.checkpoint(state)) std::printf("Initial egg checkpoint failed: %s\n", saves.diagnostic());
        }
    }
    if (boot == digivice::storage::BootStatus::RecoveryRequired) {
        std::puts("RECOVERY: snapshot/status are inspection only; do not erase or downgrade storage.");
        std::puts("Shown state is the newest valid fallback, or a temporary default if none exists.");
    }
    static auto& runtime = permanentPsram<digivice::HandheldRuntime>(state, saves, startupSeeds);
    runtime.begin();
    printState(state);
    help();
    if (consoleInitialization != ESP_OK) {
        std::printf("USB console input unavailable (%s); runtime remains active.\n",
                    esp_err_to_name(consoleInitialization));
        for (;;) { runtime.poll(); vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(runtime.recommendedPollDelayMs()))); }
    }
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::puts("Nonblocking console unavailable; input disabled to keep network/game tasks responsive.");
        for (;;) { runtime.poll(); vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(runtime.recommendedPollDelayMs()))); }
    }
    for (;;) {
        static char line[1536]{}; // Bounded USB frames; keep off the task stack.
        std::printf("digivice> ");
        if (readLine(line, sizeof(line), runtime) && !runtime.command(line)) command(line, state, saves, runtime.capabilities(caps), runtime);
        auto* secret = reinterpret_cast<volatile unsigned char*>(line);
        for (std::size_t i = 0; i < sizeof(line); ++i) secret[i] = 0;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

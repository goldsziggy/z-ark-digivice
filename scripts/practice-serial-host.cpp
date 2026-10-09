#include "practice_session.hpp"
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Only the launcher's new private temporary directory is used. This adapter
// models two durable blobs; it does not claim physical NVS/power-loss behavior.
namespace {
using namespace digivice;
namespace dp = devicepractice;
class FileBackend final : public dp::Backend {
public:
    explicit FileBackend(const char* path) : directory_(open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) {}
    ~FileBackend() override { if (directory_ >= 0) close(directory_); }
    bool ready() const { return directory_ >= 0; }
    dp::Read read(unsigned slot, dp::Slot& out) override {
        if (!ready() || slot > 1) return dp::Read::Unreadable;
        const int fd = openat(directory_, names_[slot], O_RDONLY | O_NOFOLLOW);
        if (fd < 0) return errno == ENOENT ? dp::Read::Missing : dp::Read::Unreadable;
        struct stat info{};
        bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && (info.st_size == static_cast<off_t>(dp::kRecordBytes) || info.st_size == static_cast<off_t>(dp::kLegacyRecordBytes));
        const auto size = ok ? static_cast<std::size_t>(info.st_size) : 0;
        std::size_t offset = 0;
        while (ok && offset < size) {
            const auto got = ::read(fd, out.bytes.data + offset, size - offset);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) { ok = false; break; }
            offset += static_cast<std::size_t>(got);
        }
        if (close(fd) != 0) ok = false;
        out.length = offset;
        return ok ? dp::Read::Present : dp::Read::Unreadable;
    }
    bool write(unsigned slot, const dp::Bytes& bytes) override {
        if (!ready() || slot > 1) return false;
        const char* temporary = slot ? "state_b.pending" : "state_a.pending";
        const int fd = openat(directory_, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0) return false;
        bool ok = true;
        std::size_t offset = 0;
        while (offset < dp::kRecordBytes) {
            const auto written = ::write(fd, bytes.data + offset, dp::kRecordBytes - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { ok = false; break; }
            offset += static_cast<std::size_t>(written);
        }
        if (ok && fsync(fd) != 0) ok = false;
        if (close(fd) != 0) ok = false;
        if (ok && renameat(directory_, temporary, directory_, names_[slot]) != 0) ok = false;
        if (ok && fsync(directory_) != 0) ok = false;
        if (!ok) unlinkat(directory_, temporary, 0); // Only this write's exclusively created temp file.
        return ok;
    }
private:
    int directory_ = -1;
    static constexpr const char* names_[2]{"state_a.bin", "state_b.bin"};
};

void show(const dp::PracticeSession& session) {
    std::printf("PRACTICE revision=%" PRIu32 " command=%" PRIu32 " mode=%s writable=%d\n",
        session.revision(), session.lastCommandId(), dp::modeName(session.mode()), session.writable());
    if (const auto* state = session.battle()) {
        char json[practice::kJsonCapacity];
        if (practice::writePublicJson(*state, json, sizeof(json))) std::printf("BATTLE %s\n", json);
    }
}
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fputs("launcher-owned temporary directory required\n", stderr); return 2; }
    FileBackend backend(argv[1]);
    if (!backend.ready()) return 2;
    dp::PracticeSession session(backend);
    std::printf("BOOT %s\n", dp::resultText(session.restore()));
    auto care = newDevice();
    if (apply(care, Action::Hatch, 1) != Error::None) return 2; // Explicit Impmon host fixture only.
    Snapshot initialCare;
    if (!encodeSnapshot(care, initialCare)) return 2;
    char line[384];
    while (std::fgets(line, sizeof(line), stdin)) {
        const auto length = std::strlen(line);
        if (!length || line[length - 1] != '\n') { std::fputs("bounded complete line required\n", stderr); return 2; }
        line[length - 1] = 0;
        if (length > 1 && line[length - 2] == '\r') line[length - 2] = 0;
        const auto reply = session.command(line, care, true, 12345); // Fixed reproducible seed, no radio/AI.
        std::printf("REPLY %s\n", dp::resultText(reply.result));
        if (reply.result == dp::Result::Help) {
            std::puts("practice status | practice replay");
            std::puts("practice start <new-id> <expected-revision> tactical|auto (explicit confirmation)");
            std::puts("practice act <new-id> <expected-revision> physical|heavy|magic|brace|counter|ward|retreat");
            std::puts("practice act <new-id> <expected-revision> card 1|2");
        }
        if (reply.result == dp::Result::CoreRejected) std::printf("ERROR %s\n", practice::errorText(reply.coreError));
        if (reply.result == dp::Result::Replay) {
            autobattle::Trace trace;
            if (!session.replay(trace)) std::puts("REPLAY unavailable");
            else {
                std::printf("REPLAY outcome=%s turns=%zu\n", autobattle::outcomeName(trace.outcome), trace.count);
                for (std::uint32_t i = 0; i < trace.count; ++i) {
                    const auto& step = trace.steps[i];
                    std::printf("TURN %" PRIu32 " action=%s opponent=%s player=%" PRIu32 "->%" PRIu32 " enemy=%" PRIu32 "->%" PRIu32 "\n",
                        i + 1, autobattle::moveName(step.action), autobattle::moveName(step.opponentAction),
                        step.playerHpBefore, step.playerHpAfter, step.enemyHpBefore, step.enemyHpAfter);
                }
            }
        }
        show(session);
        Snapshot after;
        if (!encodeSnapshot(care, after) || std::memcmp(initialCare.bytes, after.bytes, kSnapshotSize)) return 3;
    }
    std::puts("CARE_UNCHANGED=true; fixture=Impmon; physical NVS and controls untested");
    return std::ferror(stdin) ? 2 : 0;
}

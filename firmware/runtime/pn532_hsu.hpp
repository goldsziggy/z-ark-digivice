#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Bounded PN532 HSU protocol (NXP UM0701-02). Shared by diagnostics and the
// allowlisted gameplay adapter; this transport has no game/save dependency.
namespace digivice::nfcdiag {
struct Link {
    virtual ~Link() = default;
    // Nonblocking: 0 = no progress, negative = error, positive = bytes moved.
    virtual int write(const std::uint8_t*, std::size_t) = 0;
    virtual int read(std::uint8_t*, std::size_t) = 0;
    virtual bool flush() = 0;
};
enum class Frame { Pending, Ack, Response, Invalid };
class Parser {
public:
    void reset() { size_ = 0; needed_ = 0; zeros_ = 0; kind_ = Frame::Pending; }
    Frame push(std::uint8_t byte) {
        if (kind_ != Frame::Pending) return Frame::Invalid;
        if (size_ < 3) {
            // Bounded caller/deadline allows boot noise and arbitrarily split preamble.
            if (!byte) { if (zeros_ < 2) ++zeros_; return kind_; }
            if (byte != 0xff || zeros_ != 2) { zeros_ = 0; return kind_; }
            bytes_[0] = bytes_[1] = 0; bytes_[2] = 0xff; size_ = 3; return kind_;
        }
        if (size_ >= sizeof(bytes_)) return kind_ = Frame::Invalid;
        bytes_[size_++] = byte;
        if (size_ == 5) {
            if (!bytes_[3] && bytes_[4] == 0xff) needed_ = 6;
            else if (bytes_[3] < 2 || bytes_[3] > 66 ||
                     static_cast<std::uint8_t>(bytes_[3] + bytes_[4]))
                return kind_ = Frame::Invalid; // NACK/error/extended frames fail closed.
            else needed_ = bytes_[3] + 7;
        }
        if (needed_ && size_ == needed_) {
            if (byte) return kind_ = Frame::Invalid;
            if (needed_ == 6) return kind_ = Frame::Ack;
            std::uint8_t sum = 0;
            for (std::size_t i = 5; i + 1 < needed_; ++i) sum += bytes_[i];
            kind_ = !sum && bytes_[5] == 0xd5 ? Frame::Response : Frame::Invalid;
        }
        return kind_;
    }
    std::uint8_t command() const { return bytes_[6]; }
    const std::uint8_t* data() const { return bytes_ + 7; }
    std::size_t size() const { return bytes_[3] - 2U; }
private:
    std::uint8_t bytes_[73]{};
    std::size_t size_ = 0, needed_ = 0;
    unsigned zeros_ = 0;
    Frame kind_ = Frame::Pending;
};
inline std::size_t frame(const std::uint8_t* command, std::size_t size, std::uint8_t* out) {
    if (!command || !out || !size || size > 8) return 0;
    out[0] = out[1] = 0; out[2] = 0xff; out[3] = static_cast<std::uint8_t>(size + 1);
    out[4] = static_cast<std::uint8_t>(-out[3]); out[5] = 0xd4;
    std::uint8_t sum = 0xd4;
    for (std::size_t i = 0; i < size; ++i) { out[6+i] = command[i]; sum += command[i]; }
    out[6+size] = static_cast<std::uint8_t>(-sum); out[7+size] = 0;
    return size + 8;
}
struct Result {
    bool scan = false, samReady = false, firmwareValid = false, uidValid = false, noCard = false, rfOff = false;
    std::uint8_t firmware[4]{}, uid[10]{}, uidSize = 0, atqa[2]{}, sak = 0;
    unsigned ackCount = 0, rxBytes = 0;
    const char* error = "not_run";
    std::uint8_t failedCommand=0;
    const char* failedPhase="none";
    const char* cleanupError="none";
};
inline bool target(const std::uint8_t* data, std::size_t size, Result& result) {
    if (!data || !size) return false;
    if (size == 1 && !data[0]) { result.noCard = true; return true; }
    if (size < 6 || data[0] != 1 || data[1] != 1) return false;
    const auto n = data[5];
    if ((n != 4 && n != 7 && n != 10) || size < 6U + n) return false;
    const std::size_t end = 6U + n;
    // For ISO14443-4 the ATS length includes its length byte.
    if ((data[4] & 0x20) ? (size <= end || !data[end] || size != end + data[end]) : size != end)
        return false;
    std::memcpy(result.uid, data + 6, n); result.uidSize = n; result.uidValid = true;
    result.atqa[0]=data[2]; result.atqa[1]=data[3]; result.sak=data[4];
    return true;
}

class Session {
public:
    explicit Session(Link& link) : link_(link) {}
    static constexpr std::uint64_t kExchangeMs = 1000, kScanMs = 1200, kSessionMs = 6000;
    bool busy() const { return phase_ != Phase::Idle; }
    const Result& result() const { return result_; }
    // Repeat a completed, clean scan without redoing firmware/SAM setup. RF
    // was confirmed off; no cached target/no-card observation is replayed.
    bool rescan(std::uint64_t now) {
        if(busy()||!result_.scan||!result_.samReady||!result_.firmwareValid||!result_.rfOff||std::strcmp(result_.error,"none"))return false;
        const auto old=result_;result_={};result_.scan=true;result_.samReady=true;result_.firmwareValid=true;
        std::memcpy(result_.firmware,old.firmware,4);result_.error="pending";
        started_=last_=since_=now;cleanup_=false;
        if(!link_.flush()){phase_=Phase::Idle;result_.error="flush";return false;}
        select(Command::Scan,now);return true;
    }
    bool start(bool scan, std::uint64_t now) {
        if (busy()) return false;
        result_ = {}; result_.scan = scan; result_.error = "pending";
        started_ = last_ = since_ = now; cleanup_ = false;
        if (!link_.flush()) { result_.error = "flush"; return false; }
        // Match the verified 2026-10-16 diagnostic startup: the documented
        // LowVbat -> SAM normal-mode transition must precede firmware queries.
        // Retain the 50ms oscillator wait and the full 16-byte HSU preamble.
        const std::uint8_t wake[] = {0x55, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        raw(wake, sizeof(wake)); phase_ = Phase::Wake; return true;
    }
    void cancel(std::uint64_t now) { if (busy() && !cleanup_) fail("cancelled", now); }
    // At most one <=64-byte nonblocking read/write per tick; no sleeps/allocations.
    void poll(std::uint64_t now) {
        if (!busy()) return;
        if (now < last_) { result_.error = "clock"; phase_ = Phase::Idle; return; }
        last_ = now;
        if (!cleanup_ && now - started_ >= kSessionMs) { fail("session_timeout", now); return; }
        if (phase_ == Phase::WakeWait || phase_ == Phase::AbortWait) {
            if (now - since_ < 50) return;
            if (!link_.flush()) { fail("flush", now); return; }
            select(cleanup_ ? Command::Off : Command::Sam, now); return;
        }
        const auto timeout = command_ == Command::Scan && phase_ == Phase::Response ? kScanMs : kExchangeMs;
        if (now - since_ >= timeout) { fail(phase_ == Phase::Ack ? "ack_timeout" : "response_timeout", now); return; }
        if (phase_ == Phase::Wake || phase_ == Phase::Abort || phase_ == Phase::Send) {
            const int count = link_.write(tx_ + sent_, txSize_ - sent_);
            if (count < 0 || static_cast<std::size_t>(count) > txSize_ - sent_) { fail("write", now); return; }
            sent_ += static_cast<std::size_t>(count);
            if (sent_ == txSize_) {
                phase_ = phase_ == Phase::Wake ? Phase::WakeWait : phase_ == Phase::Abort ? Phase::AbortWait : Phase::Ack;
                since_ = now;
            }
            return;
        }
        std::uint8_t bytes[64]{};
        const int count = link_.read(bytes, sizeof(bytes));
        if (count < 0 || count > static_cast<int>(sizeof(bytes))) { fail("read", now); return; }
        result_.rxBytes += static_cast<unsigned>(count);
        for (int i = 0; i < count; ++i) {
            const auto decoded = parser_.push(bytes[i]);
            if (decoded == Frame::Pending) continue;
            if (phase_ == Phase::Ack && decoded == Frame::Ack) {
                ++result_.ackCount; phase_ = Phase::Response; since_ = now; parser_.reset(); continue;
            }
            if (phase_ != Phase::Response || decoded != Frame::Response ||
                parser_.command() != static_cast<std::uint8_t>(code(command_) + 1) || i + 1 != count) {
                fail("frame", now); return;
            }
            response(now); return;
        }
    }
private:
    enum class Phase { Idle, Wake, WakeWait, Send, Ack, Response, Abort, AbortWait };
    enum class Command { Firmware, Sam, Retries, Scan, Off };
    static std::uint8_t code(Command cmd) {
        return cmd == Command::Firmware ? 0x02 : cmd == Command::Sam ? 0x14 : cmd == Command::Scan ? 0x4a : 0x32;
    }
    void raw(const std::uint8_t* bytes, std::size_t size) {
        std::memcpy(tx_, bytes, size); txSize_ = size; sent_ = 0; parser_.reset();
    }
    void select(Command cmd, std::uint64_t now) {
        command_ = cmd; since_ = now; sent_ = 0; parser_.reset();
        const std::uint8_t firmware[] = {0x02}, sam[] = {0x14, 1, 0x14, 0};
        const std::uint8_t retries[] = {0x32, 5, 0, 0, 0}, scan[] = {0x4a, 1, 0}, off[] = {0x32, 1, 0};
        switch (cmd) {
        case Command::Firmware: txSize_ = frame(firmware, sizeof(firmware), tx_); break;
        case Command::Sam: txSize_ = frame(sam, sizeof(sam), tx_); break;
        case Command::Retries: txSize_ = frame(retries, sizeof(retries), tx_); break;
        case Command::Scan: txSize_ = frame(scan, sizeof(scan), tx_); result_.rfOff = false; break;
        case Command::Off: txSize_ = frame(off, sizeof(off), tx_); break;
        }
        phase_ = Phase::Send;
    }
    void fail(const char* error, std::uint64_t now) {
        if (cleanup_) { result_.cleanupError=error;phase_ = Phase::Idle; return; } // One cleanup attempt, never a retry loop.
        result_.failedCommand=code(command_);
        result_.failedPhase=phase_==Phase::Ack?"ack":phase_==Phase::Response?"response":
            phase_==Phase::Wake || phase_==Phase::WakeWait?"wake":"send";
        result_.error = error; result_.rfOff = false; cleanup_ = true; since_ = now;
        const std::uint8_t abort[] = {0, 0, 0xff, 0, 0xff, 0}; // Host ACK aborts pending command.
        raw(abort, sizeof(abort)); phase_ = Phase::Abort;
    }
    void response(std::uint64_t now) {
        const auto* data = parser_.data(); const auto size = parser_.size();
        switch (command_) {
        case Command::Firmware:
            if (size != 4 || data[0] != 0x32 || !(data[3] & 1)) { fail("firmware", now); return; }
            std::memcpy(result_.firmware, data, 4); result_.firmwareValid = true;
            select(result_.scan ? Command::Retries : Command::Off, now); break;
        case Command::Sam:
            if (size) { fail("sam", now); return; }
            result_.samReady = true;
            select(Command::Firmware, now); break;
        case Command::Retries:
            if (size) { fail("retries", now); return; }
            select(Command::Scan, now); break;
        case Command::Scan:
            if (!target(data, size, result_)) { fail("target", now); return; }
            select(Command::Off, now); break;
        case Command::Off:
            if (size) { fail("rf_off", now); return; }
            result_.rfOff = true;
            if (!cleanup_) result_.error = "none";
            phase_ = Phase::Idle; break;
        }
    }
    Link& link_;
    Parser parser_{};
    Result result_{};
    std::uint8_t tx_[16]{};
    std::size_t sent_ = 0, txSize_ = 0;
    std::uint64_t started_ = 0, last_ = 0, since_ = 0;
    Phase phase_ = Phase::Idle;
    Command command_ = Command::Firmware;
    bool cleanup_ = false;
};
} // namespace digivice::nfcdiag

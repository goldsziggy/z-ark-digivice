#include "practice_session.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include <cstring>

namespace digivice::devicepractice {
namespace {
void put32(std::uint8_t* p, std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i)); }
std::uint32_t get32(const std::uint8_t* p) { std::uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i); return v; }
std::uint32_t crc(const std::uint8_t* p, std::size_t n) {
    std::uint32_t c = 0xffffffffu;
    for (std::size_t i = 0; i < n; ++i) { c ^= p[i]; for (unsigned b = 0; b < 8; ++b) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u))); }
    return ~c;
}
bool equal(const Request& a, const Request& b) {
    return a.id == b.id && a.expectedRevision == b.expectedRevision && a.kind == b.kind &&
        a.mode == b.mode && a.action == b.action && a.value == b.value;
}
bool valid(const Request& r) {
    if (!r.id || static_cast<unsigned>(r.mode) > 1) return false;
    if (r.kind == Kind::Start) return r.action == Action::None && r.value == 0;
    if (r.kind != Kind::Act || r.mode != Mode::Tactical || r.action < Action::Physical || r.action > Action::Retreat) return false;
    return r.action == Action::Card ? (r.value == 1 || r.value == 2) : r.value == 0;
}
const char* actionName(Action a) {
    if (a == Action::Card) return "card";
    if (a == Action::Retreat) return "retreat";
    return practice::moveName(static_cast<practice::Move>(a));
}
bool sameBattle(const practice::State& a, const practice::State& b) {
    practice::Snapshot left, right;
    return practice::encodeSnapshot(a, left) && practice::encodeSnapshot(b, right) &&
        std::memcmp(left.bytes, right.bytes, practice::kSnapshotSize) == 0;
}
bool number(const char* s, std::uint32_t& value) {
    if (!s || !*s) return false;
    value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        const auto digit = static_cast<unsigned>(*s - '0');
        if (value > (UINT32_MAX - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    return true;
}
}
const char* modeName(Mode mode) { return mode == Mode::Auto ? "auto" : "tactical"; }
bool PracticeSession::encode(const Record& r, Bytes& output) {
    if (!r.revision || !r.companionId || !valid(r.last) ||
        r.last.expectedRevision != r.revision - 1 || static_cast<unsigned>(r.mode) > 1) return false;
    practice::Snapshot current;
    if (!practice::encodeSnapshot(r.battle, current)) return false;
    Bytes bytes;
    std::memcpy(bytes.data, "DVPR", 4); put32(bytes.data + 4, 2);
    const std::uint32_t fields[]{r.revision, static_cast<unsigned>(r.mode), r.companionId, r.last.id,
        r.last.expectedRevision, static_cast<unsigned>(r.last.kind), static_cast<unsigned>(r.last.mode),
        static_cast<unsigned>(r.last.action), r.last.value};
    for (unsigned i = 0; i < 9; ++i) put32(bytes.data + 8 + i * 4, fields[i]);
    std::memcpy(bytes.data + 44, r.initial.bytes, practice::kSnapshotSize);
    std::memcpy(bytes.data + 164, current.bytes, practice::kSnapshotSize);
    put32(bytes.data + 300, crc(bytes.data, 300)); output = bytes; return true;
}
bool PracticeSession::decode(const Slot& slot, Record& output) {
    const auto* p = slot.bytes.data;
    const bool old = slot.length == kLegacyRecordBytes && get32(p + 4) == 1;
    if ((!old && (slot.length != kRecordBytes || get32(p + 4) != 2)) || std::memcmp(p, "DVPR", 4) ||
        get32(p + slot.length - 4) != crc(p, slot.length - 4)) return false;
    const std::size_t embedded = old ? practice::kV2SnapshotSize : practice::kSnapshotSize;
    const std::size_t currentOffset = 44 + embedded, reservedOffset = currentOffset + embedded;
    for (std::size_t i = reservedOffset; i < slot.length - 4; ++i) if (p[i]) return false;
    const auto initialLength = practice::snapshotSize(p + 44, embedded);
    const auto currentLength = practice::snapshotSize(p + currentOffset, embedded);
    if (!initialLength || initialLength != currentLength) return false;
    for (std::size_t i = initialLength; i < embedded; ++i) if (p[44 + i] || p[currentOffset + i]) return false;
    if (get32(p + 12) > 1 || get32(p + 28) < 1 || get32(p + 28) > 2 || get32(p + 32) > 1 || get32(p + 36) > 8) return false;
    Record r;
    r.revision = get32(p + 8); r.mode = static_cast<Mode>(get32(p + 12)); r.companionId = get32(p + 16);
    r.last = {get32(p + 20), get32(p + 24), static_cast<Kind>(get32(p + 28)), static_cast<Mode>(get32(p + 32)), static_cast<Action>(get32(p + 36)), get32(p + 40)};
    std::memcpy(r.initial.bytes, p + 44, initialLength);
    practice::State initial;
    if (!r.revision || !r.companionId || !valid(r.last) || r.last.expectedRevision != r.revision - 1 ||
        !practice::decodeSnapshot(r.initial.bytes, initialLength, initial) ||
        !practice::decodeSnapshot(p + currentOffset, currentLength, r.battle)) return false;
    if (initial.sequence || initial.status != practice::Status::Active || initial.cardUsed ||
        initial.playerHp != practice::maxHp(initial) || initial.enemyHp != practice::maxHp(initial, true) ||
        initial.rulesVersion != r.battle.rulesVersion || initial.playerFormId != r.battle.playerFormId || initial.enemyFormId != r.battle.enemyFormId ||
        initial.playerSpecies != r.battle.playerSpecies || initial.playerLevel != r.battle.playerLevel ||
        initial.enemySpecies != r.battle.enemySpecies || initial.enemyLevel != r.battle.enemyLevel) return false;
    if (r.mode == Mode::Auto) {
        autobattle::Trace trace;
        if (r.last.kind != Kind::Start || r.last.mode != Mode::Auto || practice::runAuto(initial, trace) != practice::Error::None || !sameBattle(initial, r.battle)) return false;
    } else if (r.last.kind == Kind::Start) {
        if (r.last.mode != Mode::Tactical || !sameBattle(initial, r.battle)) return false;
    } else if (!r.battle.sequence) return false;
    output = r; return true;
}
Result PracticeSession::restore() {
    writable_ = false; hasBattle_ = false; activeSlot_ = -1; record_ = {};
    Slot slots[2]; Record records[2]; bool good[2]{}; bool damaged = false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto read = backend_.read(i, slots[i]);
        if (read == Read::Missing) continue;
        good[i] = read == Read::Present && decode(slots[i], records[i]); damaged |= !good[i];
    }
    if (good[0] && good[1] && records[0].revision == records[1].revision)
        {
            Bytes left, right;
            damaged |= !encode(records[0], left) || !encode(records[1], right) ||
                std::memcmp(left.data, right.data, kRecordBytes) != 0;
        }
    if (good[0] || good[1]) {
        activeSlot_ = good[1] && (!good[0] || records[1].revision > records[0].revision) ? 1 : 0;
        record_ = records[activeSlot_]; hasBattle_ = true;
    }
    if (damaged) { diagnostic_ = "practice records unreadable/conflicting/unsupported; preserved; practice writes disabled"; return Result::RecoveryRequired; }
    writable_ = true;
    diagnostic_ = hasBattle_ ? "verified practice record restored" : "empty practice storage";
    return hasBattle_ ? Result::Ready : Result::Empty;
}
Reply PracticeSession::execute(const Request& r, const State& care, bool careWritable, std::uint32_t seed) {
    if (!writable_) return {Result::RecoveryRequired};
    if (!valid(r)) return {Result::InvalidCommand};
    if (hasBattle_ && r.id == record_.last.id) return {equal(r, record_.last) ? Result::Duplicate : Result::Conflict};
    if (r.id <= record_.last.id || r.expectedRevision != record_.revision) return {Result::Stale};
    if (record_.revision == UINT32_MAX) return {Result::Limit};
    Record candidate = record_;
    if (r.kind == Kind::Start) {
        if (hasBattle_ && record_.battle.status == practice::Status::Active) return {Result::ActiveBattle};
        if (!careWritable || !isValid(care) || !care.onboardingComplete || care.phase != Phase::Home) return {Result::InvalidStart};
        const auto* selected = activeMember(care);
        if (!selected) return {Result::InvalidStart};
        const auto& member = *selected;
        candidate.companionId = member.id; candidate.mode = r.mode;
        // Resolve a bounded rival pool from shared native rules, once per new
        // command. Care RNG and profile are never changed during practice.
        const auto rivalForm = practice::selectProductionRivalForm(seed, member.formId, member.level);
        const auto* rival = forms::find(rivalForm);
        if (!rival) return {Result::InvalidStart};
        candidate.battle = practice::newBattleWithForms(seed, static_cast<unsigned>(member.species), member.level, member.formId,
            rival->lineage, member.level, rivalForm);
        if (!practice::encodeSnapshot(candidate.battle, candidate.initial)) return {Result::InvalidStart};
        if (candidate.mode == Mode::Auto) {
            autobattle::Trace trace;
            const auto error = practice::runAuto(candidate.battle, trace);
            if (error != practice::Error::None) return {Result::CoreRejected, error};
        }
    } else {
        if (!hasBattle_) return {Result::InvalidStart};
        if (record_.mode == Mode::Auto) return {Result::AutoComplete};
        const auto error = practice::apply(candidate.battle, actionName(r.action), r.value);
        if (error != practice::Error::None) return {Result::CoreRejected, error};
    }
    candidate.revision = record_.revision + 1; candidate.last = r;
    Bytes bytes;
    if (!encode(candidate, bytes)) return {Result::CoreRejected, practice::Error::InvalidState};
    const unsigned next = activeSlot_ == 0 ? 1 : 0;
    if (!backend_.write(next, bytes)) {
        writable_ = false; diagnostic_ = "practice commit uncertain; reboot/recover before more practice commands";
        return {Result::RecoveryRequired};
    }
    Slot readback;
    if (backend_.read(next, readback) != Read::Present || readback.length != kRecordBytes || std::memcmp(readback.bytes.data, bytes.data, kRecordBytes)) {
        writable_ = false; diagnostic_ = "practice readback failed; old record preserved; recovery required";
        return {Result::RecoveryRequired};
    }
    record_ = candidate; activeSlot_ = static_cast<int>(next); hasBattle_ = true;
    diagnostic_ = "practice checkpoint committed and verified";
    return {Result::Applied};
}
bool PracticeSession::blocksPartner() const { return !writable_ || (hasBattle_ && record_.battle.status == practice::Status::Active); }
bool PracticeSession::allowsCareAction(digivice::Action action) const {
    return !blocksPartner() || (action != digivice::Action::Select && action != digivice::Action::Walk &&
        action != digivice::Action::Explore && action != digivice::Action::PresentEncounter &&
        action != digivice::Action::Evolve && action != digivice::Action::Release);
}
bool PracticeSession::replay(autobattle::Trace& trace) const {
    if (!writable_ || !hasBattle_ || record_.mode != Mode::Auto) return false;
    practice::State state;
    return practice::decodeSnapshot(record_.initial.bytes, practice::snapshotSize(record_.initial.bytes, practice::kSnapshotSize), state) &&
        practice::runAuto(state, trace) == practice::Error::None && sameBattle(state, record_.battle);
}
Reply PracticeSession::command(const char* line, const State& care, bool careWritable, std::uint32_t seed) {
    if (!line) return {Result::InvalidCommand};
    char buffer[160]; const auto length = std::strlen(line);
    if (!length || length >= sizeof(buffer)) return {Result::InvalidCommand};
    std::memcpy(buffer, line, length + 1);
    char* tokens[8]{}; unsigned count = 0; char* p = buffer;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (count == 8) return {Result::InvalidCommand};
        tokens[count++] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) *p++ = 0;
    }
    if (count < 2 || std::strcmp(tokens[0], "practice")) return {Result::InvalidCommand};
    if (count == 2) {
        if (!std::strcmp(tokens[1], "status")) return {Result::Status};
        if (!std::strcmp(tokens[1], "help")) return {Result::Help};
        if (!std::strcmp(tokens[1], "replay")) return {Result::Replay};
    }
    if (count < 5 || count > 6) return {Result::InvalidCommand};
    Request r;
    if (!number(tokens[2], r.id) || !number(tokens[3], r.expectedRevision)) return {Result::InvalidCommand};
    if (!std::strcmp(tokens[1], "start") && count == 5) {
        if (!std::strcmp(tokens[4], "auto")) r.mode = Mode::Auto;
        else if (std::strcmp(tokens[4], "tactical")) return {Result::InvalidCommand};
    } else if (!std::strcmp(tokens[1], "act")) {
        r.kind = Kind::Act;
        for (unsigned i = 1; i <= 8; ++i) if (!std::strcmp(tokens[4], actionName(static_cast<Action>(i)))) r.action = static_cast<Action>(i);
        if (count == 6 && !number(tokens[5], r.value)) return {Result::InvalidCommand};
        if (r.action == Action::Card && count != 6) return {Result::InvalidCommand};
        if (r.action != Action::Card && count != 5) return {Result::InvalidCommand};
    } else return {Result::InvalidCommand};
    return execute(r, care, careWritable, seed);
}
const char* resultText(Result r) {
    switch (r) {
    case Result::Ready: return "restored"; case Result::Empty: return "empty";
    case Result::Applied: return "saved"; case Result::Duplicate: return "exact retry; saved result unchanged";
    case Result::Status: return "status"; case Result::Help: return "help"; case Result::Replay: return "replay";
    case Result::InvalidCommand: return "invalid practice command";
    case Result::RecoveryRequired: return "practice recovery required; records preserved";
    case Result::Conflict: return "command ID already used with another request";
    case Result::Stale: return "older command ID or stale expected revision";
    case Result::InvalidStart: return "start requires a saved, hatched companion at Home";
    case Result::ActiveBattle: return "finish or retreat from current practice first";
    case Result::AutoComplete: return "Auto result is complete; manual actions are disabled";
    case Result::CoreRejected: return "practice core rejected action";
    case Result::Limit: return "practice revision limit reached";
    }
    return "unknown";
}
} // namespace digivice::devicepractice

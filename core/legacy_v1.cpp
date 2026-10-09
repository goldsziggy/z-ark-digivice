// Frozen rules-v1 engine for trusted legacy history replay. CLI only; never linked into firmware.
#include "legacy_v1.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

namespace digivice_v1 {
namespace {
constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
std::uint32_t cappedAdd(std::uint32_t current, std::uint32_t amount, std::uint32_t cap) {
    return amount >= cap - current ? cap : current + amount;
}
std::uint32_t levelFor(std::uint32_t bond) { return bond >= 100 ? 3 : bond >= 40 ? 2 : 1; }
std::uint32_t random(State& state) {
    std::uint32_t x = state.rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return state.rngState = x;
}
void addBond(State& state, std::uint32_t amount) {
    state.bond = cappedAdd(state.bond, amount, 200);
    const auto level = levelFor(state.bond);
    if (level > state.level) state.message = Message::Evolved;
    state.level = level;
}
void home(State& state) {
    state.phase = Phase::Home;
    state.wildHp = state.wildMaxHp = state.captureAttempts = 0;
    state.attackBoost = state.shield = 0;
    state.cardUsed = false;
}
void wildResponse(State& state) {
    const auto damage = 3 + random(state) % 4;
    const auto blocked = state.shield < damage ? state.shield : damage;
    state.shield -= blocked;
    const auto received = damage - blocked;
    if (received >= state.hp) {
        home(state);
        state.hp = 10;
        state.message = Message::Retreated;
    } else {
        state.hp -= received;
    }
}
void put32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint32_t get32(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (i * 8);
    return value;
}
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t length) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
} // namespace

State newGame(std::uint32_t seed) {
    State state;
    state.seed = seed;
    state.rngState = seed ? seed : 0x6d2b79f5u;
    return state;
}

bool isValid(const State& state) {
    if (!state.rngState || state.hp < 1 || state.hp > 100 || state.energy > 100 ||
        state.fullness > 100 || state.mood > 100 || state.bond > 200 ||
        state.level != levelFor(state.bond) || state.steps < state.stepCredit ||
        (state.steps - state.stepCredit) % 100 != 0 ||
        state.encounters != (state.steps - state.stepCredit) / 100 ||
        state.captures > state.encounters || state.encounters > state.sequence ||
        static_cast<unsigned>(state.message) > static_cast<unsigned>(Message::Evolved)) return false;
    if (state.phase == Phase::Home) {
        return state.wildHp == 0 && state.wildMaxHp == 0 && state.captureAttempts == 0 &&
               !state.cardUsed && state.attackBoost == 0 && state.shield == 0;
    }
    if (state.phase != Phase::Encounter || !state.encounters || state.wildMaxHp < 24 ||
        state.wildMaxHp > 30 || state.wildHp < 1 || state.wildHp > state.wildMaxHp ||
        state.captureAttempts > 3 || (state.attackBoost != 0 && state.attackBoost != 5) ||
        state.shield > 12 || (state.attackBoost && state.shield) ||
        (!state.cardUsed && (state.attackBoost || state.shield))) return false;
    return true;
}

Error apply(State& state, Action action, std::uint32_t value) {
    if (!isValid(state)) return Error::InvalidState;
    if (state.sequence == kMax) return Error::CounterOverflow;
    if (action == Action::Walk) {
        if (value < 1 || value > 1000) return Error::InvalidValue;
        if (state.steps > kMax - value || state.stepCredit > kMax - value)
            return Error::CounterOverflow;
    } else if (action == Action::Card) {
        if (value != 1 && value != 2) return Error::InvalidValue;
    } else if (value != 0) {
        return Error::InvalidValue;
    }
    State next = state;
    switch (action) {
    case Action::Feed:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        next.fullness = cappedAdd(next.fullness, 15, 100);
        next.energy = cappedAdd(next.energy, 3, 100);
        next.mood = cappedAdd(next.mood, 2, 100);
        next.message = Message::Fed;
        addBond(next, 2);
        break;
    case Action::Play:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        if (next.energy < 5) return Error::LowEnergy;
        next.energy -= 5;
        next.mood = cappedAdd(next.mood, 12, 100);
        next.message = Message::Played;
        addBond(next, 5);
        break;
    case Action::Rest:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        next.hp = cappedAdd(next.hp, 25, 100);
        next.energy = cappedAdd(next.energy, 25, 100);
        next.message = Message::Rested;
        addBond(next, 1);
        break;
    case Action::Walk:
        next.steps += value;
        next.stepCredit += value;
        next.message = Message::Walked;
        if (next.phase == Phase::Home && next.stepCredit >= 100) {
            next.stepCredit -= 100;
            ++next.encounters;
            next.phase = Phase::Encounter;
            next.wildMaxHp = next.wildHp = 24 + random(next) % 7;
            next.message = Message::Encounter;
        }
        break;
    case Action::Card:
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        if (next.cardUsed) return Error::CardAlreadyUsed;
        next.cardUsed = true;
        if (value == 1) {
            next.attackBoost = 5;
            next.message = Message::AttackCard;
        } else {
            next.shield = 12;
            next.message = Message::ShieldCard;
        }
        break;
    case Action::Attack: {
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        const auto damage = 6 + random(next) % 4 + next.level * 2 + next.attackBoost;
        next.attackBoost = 0;
        next.energy = next.energy > 2 ? next.energy - 2 : 0;
        if (damage >= next.wildHp) {
            home(next);
            next.message = Message::Won;
            addBond(next, 8);
        } else {
            next.wildHp -= damage;
            next.message = Message::Attacked;
            wildResponse(next);
        }
        break;
    }
    case Action::Capture:
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        if (next.wildHp > next.wildMaxHp / 2) return Error::WildTooStrong;
        if (next.captureAttempts >= 3) return Error::CaptureLimit;
        ++next.captureAttempts;
        if (random(next) % 100 < 70 + next.level * 5) {
            ++next.captures;
            home(next);
            next.message = Message::Captured;
            addBond(next, 12);
        } else {
            next.message = Message::CaptureMissed;
            wildResponse(next);
        }
        break;
    default:
        return Error::InvalidAction;
    }
    ++next.sequence;
    if (!isValid(next)) return Error::InvalidState;
    state = next;
    return Error::None;
}

const char* errorText(Error error) {
    switch (error) {
    case Error::None: return "ok";
    case Error::InvalidState: return "invalid state";
    case Error::InvalidAction: return "unknown action";
    case Error::InvalidValue: return "invalid action value";
    case Error::WrongPhase: return "action unavailable in current phase";
    case Error::LowEnergy: return "rest before playing again";
    case Error::CardAlreadyUsed: return "only one card per encounter";
    case Error::WildTooStrong: return "weaken the wild creature to half health before capture";
    case Error::CaptureLimit: return "three capture attempts already used this encounter";
    case Error::CounterOverflow: return "state counter limit reached";
    }
    return "unknown error";
}
const char* messageText(Message message) {
    switch (message) {
    case Message::Welcome: return "Your adventure begins.";
    case Message::Fed: return "A happy snack.";
    case Message::Played: return "Time together builds your bond.";
    case Message::Rested: return "Rested and ready.";
    case Message::Walked: return "Every step counts.";
    case Message::Encounter: return "A wild Flicker appeared!";
    case Message::AttackCard: return "Spark card read. Your next attack is stronger.";
    case Message::ShieldCard: return "Shelter card read. A gentle shield surrounds you.";
    case Message::Attacked: return "Your creature used Spark.";
    case Message::Won: return "A friendly battle won.";
    case Message::Captured: return "Flicker joined your collection!";
    case Message::CaptureMissed: return "Flicker slipped away from the capture beam.";
    case Message::Retreated: return "A gentle retreat. Rest whenever you are ready.";
    case Message::Evolved: return "Your bond helped your creature evolve!";
    }
    return "Unknown message.";
}
const char* creatureName(const State& state) {
    return state.level >= 3 ? "Lumen" : state.level == 2 ? "Glint" : "Mote";
}
bool parseAction(const char* name, Action& action) {
    if (!name) return false;
    const char* names[] = {"feed", "play", "rest", "walk", "card", "attack", "capture"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (std::strcmp(name, names[i]) == 0) {
            action = static_cast<Action>(i);
            return true;
        }
    }
    return false;
}
std::size_t writeJson(const State& s, char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0';
    if (!isValid(s)) return 0;
    const int length = std::snprintf(output, capacity,
        "{\"schemaVersion\":%u,\"rulesVersion\":%u,\"sequence\":%u,\"seed\":%u,"
        "\"rngState\":%u,\"steps\":%u,\"stepCredit\":%u,\"hp\":%u,\"energy\":%u,"
        "\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"captures\":%u,"
        "\"encounters\":%u,\"phase\":\"%s\",\"wildHp\":%u,\"wildMaxHp\":%u,"
        "\"captureAttempts\":%u,\"cardUsed\":%s,\"attackBoost\":%u,\"shield\":%u,"
        "\"creature\":\"%s\",\"message\":\"%s\"}",
        static_cast<unsigned>(kSchemaVersion), static_cast<unsigned>(kRulesVersion),
        static_cast<unsigned>(s.sequence), static_cast<unsigned>(s.seed),
        static_cast<unsigned>(s.rngState), static_cast<unsigned>(s.steps),
        static_cast<unsigned>(s.stepCredit), static_cast<unsigned>(s.hp),
        static_cast<unsigned>(s.energy), static_cast<unsigned>(s.fullness),
        static_cast<unsigned>(s.mood), static_cast<unsigned>(s.bond),
        static_cast<unsigned>(s.level), static_cast<unsigned>(s.captures),
        static_cast<unsigned>(s.encounters), s.phase == Phase::Home ? "home" : "encounter",
        static_cast<unsigned>(s.wildHp), static_cast<unsigned>(s.wildMaxHp),
        static_cast<unsigned>(s.captureAttempts), s.cardUsed ? "true" : "false",
        static_cast<unsigned>(s.attackBoost), static_cast<unsigned>(s.shield),
        creatureName(s), messageText(s.message));
    if (length < 0 || static_cast<std::size_t>(length) >= capacity) {
        output[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(length);
}

bool encodeSnapshot(const State& s, Snapshot& snapshot) {
    if (!isValid(s)) return false;
    Snapshot next;
    auto* bytes = next.bytes;
    std::memcpy(bytes, "DGVS", 4);
    bytes[4] = 2;
    bytes[6] = 88; // payload byte length (rules version plus 21 canonical fields)
    const std::uint32_t fields[] = {
        kRulesVersion, s.sequence, s.seed, s.rngState, s.steps, s.stepCredit,
        s.hp, s.energy, s.fullness, s.mood, s.bond, s.level, s.captures, s.encounters,
        s.wildHp, s.wildMaxHp, s.captureAttempts, static_cast<std::uint32_t>(s.phase),
        s.cardUsed ? 1u : 0u, s.attackBoost, s.shield, static_cast<std::uint32_t>(s.message)
    };
    for (std::size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        put32(bytes + 8 + i * 4, fields[i]);
    put32(bytes + kSnapshotSize - 4, crc32(bytes, kSnapshotSize - 4));
    snapshot = next;
    return true;
}

SnapshotStatus decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state) {
    if (!bytes || length < 8) return SnapshotStatus::InvalidLength;
    if (std::memcmp(bytes, "DGVS", 4) != 0) return SnapshotStatus::BadMagic;
    const auto version = static_cast<unsigned>(bytes[4]) | (static_cast<unsigned>(bytes[5]) << 8);
    if (version != 1 && version != 2) return SnapshotStatus::UnsupportedVersion;
    const auto required = version == 1 ? kLegacySnapshotSize : kSnapshotSize;
    const auto payload = static_cast<unsigned>(bytes[6]) | (static_cast<unsigned>(bytes[7]) << 8);
    if (length != required || payload != length - 12) return SnapshotStatus::InvalidLength;
    if (get32(bytes + length - 4) != crc32(bytes, length - 4)) return SnapshotStatus::BadChecksum;
    if (get32(bytes + 8) != kRulesVersion) return SnapshotStatus::UnsupportedRules;
    std::size_t offset = 12;
    const auto read = [&]() { const auto value = get32(bytes + offset); offset += 4; return value; };
    State next;
    next.sequence = read(); next.seed = read(); next.rngState = read();
    next.steps = read(); next.stepCredit = read(); next.hp = read();
    next.energy = read(); next.fullness = read(); next.mood = read();
    next.bond = read(); next.level = read(); next.captures = read();
    next.encounters = read(); next.wildHp = read(); next.wildMaxHp = read();
    next.captureAttempts = read();
    const auto phase = read();
    const auto card = read();
    next.attackBoost = read();
    next.shield = version == 1 ? 0 : read();
    const auto message = read();
    if (phase > 1 || card > 1 || message > static_cast<unsigned>(Message::Evolved))
        return SnapshotStatus::InvalidState;
    next.phase = static_cast<Phase>(phase);
    next.cardUsed = card != 0;
    next.message = static_cast<Message>(message);
    if (!isValid(next)) return SnapshotStatus::InvalidState;
    state = next;
    return version == 1 ? SnapshotStatus::Migrated : SnapshotStatus::Ok;
}
const char* snapshotStatusText(SnapshotStatus status) {
    switch (status) {
    case SnapshotStatus::Ok: return "ok";
    case SnapshotStatus::Migrated: return "migrated snapshot version 1 to version 2";
    case SnapshotStatus::InvalidLength: return "invalid snapshot length";
    case SnapshotStatus::BadMagic: return "invalid snapshot magic";
    case SnapshotStatus::UnsupportedVersion: return "unsupported snapshot version";
    case SnapshotStatus::UnsupportedRules: return "unsupported rules version";
    case SnapshotStatus::BadChecksum: return "snapshot checksum mismatch";
    case SnapshotStatus::InvalidState: return "snapshot contains invalid game state";
    }
    return "unknown snapshot status";
}
} // namespace digivice_v1

// Frozen schema-3 / rules-2 history interpreter. Do not change game rules.
#include "legacy_v2.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace digivice_v2 {
namespace {
constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
std::uint32_t cappedAdd(std::uint32_t current, std::uint32_t amount, std::uint32_t cap) {
    return amount >= cap - current ? cap : current + amount;
}
std::uint32_t levelFor(Species species, std::uint32_t bond) {
    return species == Species::Flicker ? 1 : bond >= 100 ? 3 : bond >= 40 ? 2 : 1;
}
CreatureMember freshMember(std::uint32_t id, Species species, std::uint32_t sequence) {
    return {id, species, 100, 80, 70, 80, 0, 1, sequence};
}
void storeActive(State& state) {
    auto& member = state.collection[state.activeCreatureId - 1];
    member.hp = state.hp; member.energy = state.energy; member.fullness = state.fullness;
    member.mood = state.mood; member.bond = state.bond; member.level = state.level;
}
void loadActive(State& state) {
    const auto& member = state.collection[state.activeCreatureId - 1];
    state.hp = member.hp; state.energy = member.energy; state.fullness = member.fullness;
    state.mood = member.mood; state.bond = member.bond; state.level = member.level;
}
std::uint32_t random(State& state) {
    std::uint32_t x = state.rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return state.rngState = x;
}
void addBond(State& state, std::uint32_t amount) {
    state.bond = cappedAdd(state.bond, amount, 200);
    const auto level = levelFor(state.collection[state.activeCreatureId - 1].species, state.bond);
    if (level > state.level) state.message = Message::Evolved;
    state.level = level;
}
void home(State& state) {
    state.phase = Phase::Home;
    state.wildHp = state.wildMaxHp = state.captureAttempts = 0;
    state.attackBoost = state.shield = 0;
    state.cardUsed = false;
    state.wildSpecies = Species::None;
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
    state.collection[0] = freshMember(1, Species::Mote, 0);
    return state;
}

bool isValid(const State& state) {
    if (state.collectionCount < 1 || state.collectionCount > kCollectionCapacity ||
        state.activeCreatureId < 1 || state.activeCreatureId > state.collectionCount ||
        state.legacyCaptures > state.captures ||
        state.captures - state.legacyCaptures != state.collectionCount - 1) return false;
    for (std::size_t i = 0; i < kCollectionCapacity; ++i) {
        const auto& member = state.collection[i];
        if (i >= state.collectionCount) {
            if (member.id || member.species != Species::None || member.hp || member.energy ||
                member.fullness || member.mood || member.bond || member.level || member.capturedAtSequence)
                return false;
            continue;
        }
        if (member.id != i + 1 || member.species < Species::Mote || member.species > Species::Cinder ||
            member.hp < 1 || member.hp > 100 || member.energy > 100 || member.fullness > 100 ||
            member.mood > 100 || member.bond > 200 || member.level != levelFor(member.species, member.bond) ||
            member.capturedAtSequence > state.sequence) return false;
        if (i == 0) {
            if (member.species != Species::Mote || member.capturedAtSequence != 0) return false;
        } else if (member.species == Species::Mote ||
                   member.capturedAtSequence <= state.collection[i - 1].capturedAtSequence) return false;
    }
    const auto& active = state.collection[state.activeCreatureId - 1];
    if (state.hp != active.hp || state.energy != active.energy || state.fullness != active.fullness ||
        state.mood != active.mood || state.bond != active.bond || state.level != active.level) return false;
    if (!state.rngState || state.steps < state.stepCredit ||
        (state.steps - state.stepCredit) % 100 != 0 ||
        state.encounters != (state.steps - state.stepCredit) / 100 ||
        state.captures > state.encounters || state.encounters > state.sequence ||
        static_cast<unsigned>(state.message) > static_cast<unsigned>(Message::Selected)) return false;
    if (state.phase == Phase::Home) {
        return state.wildHp == 0 && state.wildMaxHp == 0 && state.captureAttempts == 0 &&
               !state.cardUsed && state.attackBoost == 0 && state.shield == 0 && state.wildSpecies == Species::None;
    }
    if (state.phase != Phase::Encounter || !state.encounters || state.wildMaxHp < 24 ||
        state.wildMaxHp > 30 || state.wildHp < 1 || state.wildHp > state.wildMaxHp ||
        state.captureAttempts > 3 || (state.attackBoost != 0 && state.attackBoost != 5) ||
        state.shield > 12 || (state.attackBoost && state.shield) ||
        (!state.cardUsed && (state.attackBoost || state.shield)) ||
        state.wildSpecies < Species::Flicker || state.wildSpecies > Species::Cinder) return false;
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
    } else if (action == Action::Select) {
        if (value < 1 || value > kCollectionCapacity) return Error::InvalidValue;
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
            next.wildSpecies = static_cast<Species>(static_cast<unsigned>(Species::Flicker) + (next.encounters - 1) % 3);
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
        if (next.collectionCount >= kCollectionCapacity) return Error::CollectionFull;
        if (next.wildHp > next.wildMaxHp / 2) return Error::WildTooStrong;
        if (next.captureAttempts >= 3) return Error::CaptureLimit;
        ++next.captureAttempts;
        if (random(next) % 100 < 70 + next.level * 5) {
            next.collection[next.collectionCount] = freshMember(next.collectionCount + 1, next.wildSpecies, next.sequence + 1);
            ++next.collectionCount;
            ++next.captures;
            home(next);
            next.message = Message::Captured;
            addBond(next, 12);
        } else {
            next.message = Message::CaptureMissed;
            wildResponse(next);
        }
        break;
    case Action::Select:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        if (value > next.collectionCount) return Error::UnknownMember;
        next.activeCreatureId = value;
        loadActive(next);
        next.message = Message::Selected;
        break;
    default:
        return Error::InvalidAction;
    }
    ++next.sequence;
    storeActive(next);
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
    case Error::CollectionFull: return "collection is full (8 creatures); no creature was replaced";
    case Error::UnknownMember: return "that creature is not in your collection";
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
    case Message::Encounter: return "A wild creature appeared!";
    case Message::AttackCard: return "Spark card read. Your next attack is stronger.";
    case Message::ShieldCard: return "Shelter card read. A gentle shield surrounds you.";
    case Message::Attacked: return "Your creature used Spark.";
    case Message::Won: return "A friendly battle won.";
    case Message::Captured: return "A new friend joined your collection!";
    case Message::CaptureMissed: return "The wild creature slipped away from the capture beam.";
    case Message::Retreated: return "A gentle retreat. Rest whenever you are ready.";
    case Message::Evolved: return "Your bond helped your creature evolve!";
    case Message::Selected: return "Your companion is ready.";
    }
    return "Unknown message.";
}
const char* creatureName(const State& state) {
    if (state.activeCreatureId < 1 || state.activeCreatureId > kCollectionCapacity) return "Unknown";
    return memberName(state.collection[state.activeCreatureId - 1]);
}
const char* speciesId(Species species) {
    switch (species) {
    case Species::Mote: return "mote";
    case Species::Flicker: return "flicker";
    case Species::Rill: return "rill";
    case Species::Cinder: return "cinder";
    case Species::None: return "none";
    }
    return "unknown";
}
const char* memberName(const CreatureMember& member) {
    switch (member.species) {
    case Species::Mote: return member.level >= 3 ? "Lumen" : member.level == 2 ? "Glint" : "Mote";
    case Species::Rill: return member.level >= 3 ? "Pelagia" : member.level == 2 ? "Brine" : "Rill";
    case Species::Cinder: return member.level >= 3 ? "Pyrel" : member.level == 2 ? "Scoria" : "Cinder";
    case Species::Flicker: return "Flicker";
    case Species::None: return "Unknown";
    }
    return "Unknown";
}
const char* wildName(const State& state) {
    CreatureMember wild;
    wild.species = state.wildSpecies;
    wild.level = 1;
    return memberName(wild);
}
bool parseAction(const char* name, Action& action) {
    if (!name) return false;
    const char* names[] = {"feed", "play", "rest", "walk", "card", "attack", "capture", "select"};
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
    std::size_t used = 0;
    bool ok = true;
    const auto append = [&](const char* format, ...) {
        if (!ok) return;
        va_list args;
        va_start(args, format);
        const int length = std::vsnprintf(output + used, capacity - used, format, args);
        va_end(args);
        if (length < 0 || static_cast<std::size_t>(length) >= capacity - used) { ok = false; return; }
        used += static_cast<std::size_t>(length);
    };
    char message[128];
    if (s.message == Message::Encounter)
        std::snprintf(message, sizeof(message), "A wild %s appeared!", wildName(s));
    else if (s.message == Message::Captured)
        std::snprintf(message, sizeof(message), "%s joined your collection!",
                      s.collectionCount > 1 ? memberName(s.collection[s.collectionCount - 1]) : "Flicker");
    else if (s.message == Message::CaptureMissed)
        std::snprintf(message, sizeof(message), "%s slipped away from the capture beam.", wildName(s));
    else std::snprintf(message, sizeof(message), "%s", messageText(s.message));
    append("{\"schemaVersion\":%u,\"rulesVersion\":%u,\"sequence\":%u,\"seed\":%u,"
           "\"rngState\":%u,\"steps\":%u,\"stepCredit\":%u,\"hp\":%u,\"energy\":%u,"
           "\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"captures\":%u,"
           "\"encounters\":%u,\"phase\":\"%s\",\"wildHp\":%u,\"wildMaxHp\":%u,"
           "\"captureAttempts\":%u,\"cardUsed\":%s,\"attackBoost\":%u,\"shield\":%u,"
           "\"creature\":\"%s\",\"message\":\"%s\",\"species\":\"%s\","
           "\"activeCreatureId\":%u,\"collectionCapacity\":%u,\"legacyCaptures\":%u",
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
           creatureName(s), message, speciesId(s.collection[s.activeCreatureId - 1].species),
           static_cast<unsigned>(s.activeCreatureId), static_cast<unsigned>(kCollectionCapacity),
           static_cast<unsigned>(s.legacyCaptures));
    if (s.phase == Phase::Encounter)
        append(",\"wildSpecies\":\"%s\",\"wildName\":\"%s\"", speciesId(s.wildSpecies), wildName(s));
    else append(",\"wildSpecies\":null,\"wildName\":null");
    append(",\"collection\":[");
    for (std::size_t i = 0; i < s.collectionCount; ++i) {
        const auto& m = s.collection[i];
        append("%s{\"id\":%u,\"species\":\"%s\",\"name\":\"%s\",\"hp\":%u,\"energy\":%u,"
               "\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"capturedAtSequence\":%u}",
               i ? "," : "", static_cast<unsigned>(m.id), speciesId(m.species), memberName(m),
               static_cast<unsigned>(m.hp), static_cast<unsigned>(m.energy),
               static_cast<unsigned>(m.fullness), static_cast<unsigned>(m.mood),
               static_cast<unsigned>(m.bond), static_cast<unsigned>(m.level),
               static_cast<unsigned>(m.capturedAtSequence));
    }
    append("]}");
    if (!ok) { output[0] = '\0'; return 0; }
    return used;
}

bool encodeSnapshot(const State& s, Snapshot& snapshot) {
    static_assert(kSnapshotSize == 8 + (22 + 4 + kCollectionCapacity * 9) * 4 + 4);
    if (!isValid(s)) return false;
    Snapshot next;
    auto* bytes = next.bytes;
    std::memcpy(bytes, "DGVS", 4);
    bytes[4] = 3;
    constexpr auto payload = kSnapshotSize - 12;
    bytes[6] = static_cast<std::uint8_t>(payload);
    bytes[7] = static_cast<std::uint8_t>(payload >> 8);
    const std::uint32_t fields[] = {
        kRulesVersion, s.sequence, s.seed, s.rngState, s.steps, s.stepCredit,
        s.hp, s.energy, s.fullness, s.mood, s.bond, s.level, s.captures, s.encounters,
        s.wildHp, s.wildMaxHp, s.captureAttempts, static_cast<std::uint32_t>(s.phase),
        s.cardUsed ? 1u : 0u, s.attackBoost, s.shield, static_cast<std::uint32_t>(s.message),
        s.legacyCaptures, s.activeCreatureId, s.collectionCount, static_cast<std::uint32_t>(s.wildSpecies)
    };
    std::size_t offset = 8;
    for (const auto value : fields) { put32(bytes + offset, value); offset += 4; }
    for (const auto& member : s.collection) {
        const std::uint32_t values[] = {member.id, static_cast<std::uint32_t>(member.species),
            member.hp, member.energy, member.fullness, member.mood, member.bond, member.level, member.capturedAtSequence};
        for (const auto value : values) { put32(bytes + offset, value); offset += 4; }
    }
    put32(bytes + kSnapshotSize - 4, crc32(bytes, kSnapshotSize - 4));
    snapshot = next;
    return true;
}

SnapshotStatus decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state) {
    if (!bytes || length < 8) return SnapshotStatus::InvalidLength;
    if (std::memcmp(bytes, "DGVS", 4) != 0) return SnapshotStatus::BadMagic;
    const auto version = static_cast<unsigned>(bytes[4]) | (static_cast<unsigned>(bytes[5]) << 8);
    if (version < 1 || version > 3) return SnapshotStatus::UnsupportedVersion;
    const auto required = version == 1 ? kLegacySnapshotSize : version == 2 ? kV2SnapshotSize : kSnapshotSize;
    const auto payload = static_cast<unsigned>(bytes[6]) | (static_cast<unsigned>(bytes[7]) << 8);
    if (length != required || payload != length - 12) return SnapshotStatus::InvalidLength;
    if (get32(bytes + length - 4) != crc32(bytes, length - 4)) return SnapshotStatus::BadChecksum;
    if (get32(bytes + 8) != (version < 3 ? 1u : kRulesVersion)) return SnapshotStatus::UnsupportedRules;
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
    const auto lastMessage = version < 3 ? Message::Evolved : Message::Selected;
    if (phase > 1 || card > 1 || message > static_cast<unsigned>(lastMessage))
        return SnapshotStatus::InvalidState;
    next.phase = static_cast<Phase>(phase);
    next.cardUsed = card != 0;
    next.message = static_cast<Message>(message);
    if (version < 3) {
        next.legacyCaptures = next.captures;
        next.collection[0] = freshMember(1, Species::Mote, 0);
        next.wildSpecies = next.phase == Phase::Encounter ? Species::Flicker : Species::None;
        storeActive(next);
    } else {
        next.legacyCaptures = read(); next.activeCreatureId = read(); next.collectionCount = read();
        const auto wild = read();
        if (wild > static_cast<unsigned>(Species::Cinder)) return SnapshotStatus::InvalidState;
        next.wildSpecies = static_cast<Species>(wild);
        for (auto& member : next.collection) {
            member.id = read();
            const auto species = read();
            if (species > static_cast<unsigned>(Species::Cinder)) return SnapshotStatus::InvalidState;
            member.species = static_cast<Species>(species);
            member.hp = read(); member.energy = read(); member.fullness = read();
            member.mood = read(); member.bond = read(); member.level = read(); member.capturedAtSequence = read();
        }
    }
    if (!isValid(next)) return SnapshotStatus::InvalidState;
    state = next;
    return version < 3 ? SnapshotStatus::Migrated : SnapshotStatus::Ok;
}
const char* snapshotStatusText(SnapshotStatus status) {
    switch (status) {
    case SnapshotStatus::Ok: return "ok";
    case SnapshotStatus::Migrated: return "migrated legacy snapshot to version 3";
    case SnapshotStatus::InvalidLength: return "invalid snapshot length";
    case SnapshotStatus::BadMagic: return "invalid snapshot magic";
    case SnapshotStatus::UnsupportedVersion: return "unsupported snapshot version";
    case SnapshotStatus::UnsupportedRules: return "unsupported rules version";
    case SnapshotStatus::BadChecksum: return "snapshot checksum mismatch";
    case SnapshotStatus::InvalidState: return "snapshot contains invalid game state";
    }
    return "unknown snapshot status";
}
} // namespace digivice_v2

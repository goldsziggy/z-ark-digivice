#include "usage_store.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace digivice::usage;
namespace {
unsigned checks = 0;
void require(bool value) { ++checks; if (!value) { std::fprintf(stderr, "usage check %u failed\n", checks); std::exit(1); } }
struct Memory final : Backend {
    Record records[2]{}; bool present[2]{}; unsigned writes = 0;
    bool failWrite = false, writeThenFail = false, corruptRead = false;
    Read read(unsigned slot, Record& out) override {
        if (!present[slot]) return Read::Missing;
        out = records[slot]; if (corruptRead) out.bytes[16] ^= 1;
        return Read::Present;
    }
    bool write(unsigned slot, const Record& in) override {
        ++writes; if (failWrite) return false;
        records[slot] = in; present[slot] = true; return !writeThenFail;
    }
};
}
int main() {
    Memory memory; Store store(memory);
    require(store.restore(0)); require(store.total() == 0 && store.session() == 0);
    require(store.observe(0) == 0); require(store.observe(3) == 3); // First confirmation is credited.
    require(store.total() == 3 && store.session() == 3);
    for (unsigned n = 4; n < 64; ++n) { require(store.observe(n) == 1); require(store.checkpoint(n * 20)); }
    require(memory.writes == 0); require(store.observe(64) == 1); require(store.checkpoint(1400));
    require(memory.writes == 1 && store.savedTotal() == 64);
    require(store.observe(64) == 0); require(store.checkpoint(50000)); require(memory.writes == 1);
    require(store.observe(67) == 3); require(store.checkpoint(50001)); require(memory.writes == 2);
    Store reboot(memory); require(reboot.restore(0)); require(reboot.total() == 67 && reboot.session() == 0);
    require(reboot.observe(3) == 3); require(reboot.total() == 70); require(reboot.checkpoint(1, true));
    Store reboot2(memory); require(reboot2.restore(0)); require(reboot2.total() == 70);
    require(reboot2.observe(7) == 7); require(reboot2.checkpoint(2));
    Store suddenLoss(memory); require(suddenLoss.restore(0)); require(suddenLoss.total() == 70); // Only unsaved tail lost.
    require(suddenLoss.observe(3) == 3); memory.writeThenFail = true;
    require(!suddenLoss.checkpoint(5, true)); require(!suddenLoss.writable());
    require(suddenLoss.observe(9) == 0); memory.writeThenFail = false;
    Store uncertainRecovery(memory); require(uncertainRecovery.restore(0)); require(uncertainRecovery.total() == 73);
    require(uncertainRecovery.observe(4) == 4); memory.failWrite = true;
    require(!uncertainRecovery.checkpoint(8, true)); memory.failWrite = false;
    Store failedRecovery(memory); require(failedRecovery.restore(0)); require(failedRecovery.total() == 73);
    require(failedRecovery.observe(5) == 5); require(failedRecovery.observe(4) == 0); require(!failedRecovery.writable());
    Store recoverAgain(memory); require(recoverAgain.restore(0)); require(recoverAgain.observe(UINT32_MAX) == UINT32_MAX);
    require(recoverAgain.total() == 73ull + UINT32_MAX); require(recoverAgain.checkpoint(1, true));
    Store large(memory); require(large.restore(0)); require(large.total() == 73ull + UINT32_MAX);
    require(large.observe(3) == 3); memory.corruptRead = true;
    require(!large.checkpoint(2, true)); require(!large.writable()); memory.corruptRead = false;
    Store readbackRecovery(memory); require(readbackRecovery.restore(0)); require(readbackRecovery.total() == 76ull + UINT32_MAX);
    const auto preserved = memory.records[0]; memory.records[0].bytes[5] = 1;
    Store corrupt(memory); require(!corrupt.restore(0)); require(!corrupt.writable());
    require(!corrupt.checkpoint(30000, true)); require(memory.records[0].bytes[5] == 1); memory.records[0] = preserved;
    Memory missing; Store idle(missing); require(idle.restore(0)); require(idle.checkpoint(999999, true)); require(missing.writes == 0);
    require(idle.observe(1) == 1); require(idle.checkpoint(29999)); require(missing.writes == 0);
    require(idle.checkpoint(30000)); require(missing.writes == 1);
    std::printf("PASS: %u lifetime-step persistence checks\n", checks);
}

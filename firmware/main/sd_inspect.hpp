#pragma once
#include <cstdint>

namespace digivice::assets {
class SdAssetStorage;
// Main-task diagnostic. Caller must exclude asset-worker/local-reader activity
// and power transitions. Reads only the already-mounted card; never mounts,
// formats, repairs, writes, or downloads. Emits paths and header summaries, not
// file contents or card identity. At most 512 entries, 16 directories, depth 3,
// 128-byte paths, 24 sample paths, and 4 KiB inspected per file. Synchronous card
// I/O still has the underlying driver's timeout, not a hard wall-clock bound.
void printSdInventory(const SdAssetStorage& storage, std::uint32_t expectedFormId);
// Explicit diagnostic for an UNMOUNTED card after failed startup. Caller must
// exclude asset/local-reader activity and power transitions. Initializes the
// official SDMMC host temporarily, reads <=24 sectors using existing board pins,
// then deinitializes only the host it owns. No mount, write, erase or format.
void printSdBootProbe(const SdAssetStorage& storage);
}

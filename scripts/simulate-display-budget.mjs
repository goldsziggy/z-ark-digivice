// Host accounting and lifecycle simulation, not an ESP heap/FPS benchmark.
import assert from 'node:assert/strict';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inspectPack } from '../service/asset-signing.ts';
import { ASSET_LIMITS, assetCacheBytes, decodeFrame, verifyManifest } from '../web/asset-cache.js';
import { emptyAssetState } from '../web/asset-store.js';
import { backgroundJpegBytes } from '../web/background-pack.js';
import { BACKGROUND_SCENES, BACKGROUND_FRAME_BYTES, createBackgroundPlayer } from '../web/background-player.js';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const outputPath = join(root, 'docs/evidence/display-memory-simulation.json');
const budget = JSON.parse(readFileSync(join(root, 'assets/backgrounds/budget.json'), 'utf8'));
const sum = values => values.reduce((total, value) => total + value, 0);
const packs = {}, measurements = [];
for (const scene of BACKGROUND_SCENES) {
  const id = `scene-${scene}-v1`, path = `assets/packs/${id}.json`;
  const bytes = readFileSync(join(root, path)), pack = JSON.parse(bytes);
  const validated = inspectPack(pack, id), jpeg = backgroundJpegBytes(pack);
  const declared = budget.packs.find(entry => entry.id === id);
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  assert.equal(validated.decodedBytes, BACKGROUND_FRAME_BYTES);
  assert.equal(bytes.length, declared.bytes); assert.equal(sha256, declared.sha256);
  packs[id] = pack;
  measurements.push({ id, path, bytes: bytes.length, sha256, jpegBytes: jpeg.length,
    base64Characters: pack.background.data.length, width: pack.background.width, height: pack.background.height });
}
const sprites = ['starter-v2', 'tide-v1', 'ember-v1'].map(id => {
  const bytes = readFileSync(join(root, `assets/packs/${id}.json`));
  const pack = JSON.parse(bytes); const validated = inspectPack(pack, id);
  return { id, bytes: bytes.length, decodedAllFramesRgbaBytes: validated.decodedBytes, pack };
});
const starter = sprites[0].pack;
const clips = [
  ['companion', 'sprites', 'mote', 'idle'], ['wild', 'sprites', 'flicker', 'idle'],
  ['capture effect', 'effects', 'capture', 'idle'], ['original art gallery', 'sprites', 'mote', 'idle'],
].map(([role, category, id, animation]) => {
  const asset = starter[category][id], clip = asset.animations[animation];
  const rgbaBytes = sum(clip.frames.map((_, index) => decodeFrame(starter, category, id, animation, index).data.byteLength));
  return { role, packId: starter.packId, category, id, animation, frameCount: clip.frames.length,
    width: asset.width, height: asset.height, indexedPixelBytes: sum(clip.frames.map(frame => Buffer.from(frame, 'base64').length)),
    decodedRgbaBytes: rgbaBytes, derivedRgb565Bytes: rgbaBytes / 2 };
});
const backgroundPackBytes = sum(measurements.map(item => item.bytes));
const spritePackBytes = sum(sprites.map(item => item.bytes));
const runtimeBytes = backgroundPackBytes + spritePackBytes;
const largest = measurements.reduce((a, b) => a.bytes > b.bytes ? a : b);
assert.equal(backgroundPackBytes, budget.backgroundPackBytes);
assert.equal(spritePackBytes, budget.existingSpritePackBytes);
assert.equal(runtimeBytes, budget.allRuntimePackBytes);
assert.equal(runtimeBytes + largest.bytes, budget.allPacksPlusLargestStagingBytes);
assert.ok(runtimeBytes + largest.bytes <= ASSET_LIMITS.cacheBytes);
// Use the actual eviction accountant, including signed envelopes and its
// conservative record/index allowance. Do not equate this with engine disk use.
const envelope = JSON.parse(readFileSync(join(root, 'assets/packs/catalog.json'), 'utf8'));
const manifest = await verifyManifest(envelope);
assert.equal(manifest.packs.length, BACKGROUND_SCENES.length + sprites.length);
const cacheState = emptyAssetState();
cacheState.packs = manifest.packs.map(entry => ({ key: `${entry.id}@${entry.version}`, entry, envelope,
  data: new Uint8Array(readFileSync(join(root, `assets/packs/${entry.id}.json`))), lastUsed: 0 }));
cacheState.highWater = { release: manifest.release, versions: manifest.packs.map(({ id, version, sha256 }) => ({ id, version, sha256 })) };
cacheState.active = 'ember-v1@1'; cacheState.backgroundActive = 'scene-digital-v1@1';
const accountedCompleteBytes = assetCacheBytes(cacheState);
cacheState.stage = structuredClone(cacheState.packs.find(record => record.entry.id === largest.id));
const accountedWithLargestStageBytes = assetCacheBytes(cacheState);
assert.ok(accountedWithLargestStageBytes <= ASSET_LIMITS.cacheBytes);

// Fake bitmap handles instrument the real player's ownership policy. Their
// byte costs derive from its RGBA contract; no JPEG/GPU/heap allocation is timed.
async function simulatePlayer(limit) {
  let resident = 0, peak = 0, decoders = 0, maxDecoders = 0, failScene = null;
  let opened = 0, closed = 0, failureCount = 0;
  const player = createBackgroundPlayer({ decode: async pack => {
    decoders++; maxDecoders = Math.max(maxDecoders, decoders);
    try {
      await Promise.resolve(); backgroundJpegBytes(pack);
      if (pack.background.sceneId === failScene || resident + BACKGROUND_FRAME_BYTES > limit) {
        failureCount++; throw new Error('Injected decoder failure or pixel-budget rejection');
      }
      resident += BACKGROUND_FRAME_BYTES; peak = Math.max(peak, resident); opened++;
      let released = false;
      return { width: 480, height: 480, close() {
        if (!released) { released = true; resident -= BACKGROUND_FRAME_BYTES; closed++; }
      } };
    } finally { decoders--; }
  } });
  const settle = async () => {
    for (let step = 0; step < 50; step++) {
      await Promise.resolve();
      if (!player.getState().inFlight) return;
    }
    throw new Error('Background replacement did not settle within the microtask bound');
  };
  const visible = () => player.draw({ drawImage() {} });
  player.setPacks(packs); await settle();
  assert.equal(player.getState().status, 'ready'); assert.ok(visible());
  if (limit < 2 * BACKGROUND_FRAME_BYTES) {
    player.select('forest'); await settle();
    assert.equal(player.getState().status, 'error'); assert.equal(resident, BACKGROUND_FRAME_BYTES);
    assert.equal(visible(), false, 'failed selection uses normal fallback, not a mislabeled old scene');
    player.select('meadow'); assert.ok(visible(), 'previous retained bitmap can still be selected');
  } else {
    for (const scene of BACKGROUND_SCENES) { player.select(scene); await settle(); assert.equal(player.getState().visibleId, scene); }
    // A burst coalesces to the latest request without parallel decoders.
    for (const scene of ['forest', 'beach', 'ruins', 'snow', 'digital', 'meadow']) player.select(scene);
    await settle(); assert.equal(player.getState().visibleId, 'meadow');
    failScene = 'forest'; player.select('forest'); await settle();
    assert.equal(player.getState().status, 'error'); assert.equal(resident, BACKGROUND_FRAME_BYTES);
    failScene = null; player.retry(); await settle(); assert.equal(player.getState().visibleId, 'forest');
    const damaged = structuredClone(packs['scene-beach-v1']); damaged.background.data = 'not-a-jpeg';
    player.setPacks({ ...packs, 'scene-beach-v1': damaged }); player.select('beach'); await settle();
    assert.equal(player.getState().status, 'error'); assert.equal(resident, BACKGROUND_FRAME_BYTES);
    player.setPacks(packs); player.retry(); await settle(); assert.equal(player.getState().visibleId, 'beach');
    player.setPacks({}); assert.equal(player.getState().status, 'fallback'); assert.equal(visible(), false);
    player.setPacks(packs); player.select('forest');
    player.destroy(); await settle(); // Pending candidate must close without adoption.
  }
  const playerPeak = player.getState().peakDecodedBytes;
  player.destroy(); await settle();
  assert.equal(resident, 0); assert.equal(opened, closed); assert.equal(maxDecoders, 1);
  assert.ok(peak <= limit); assert.equal(playerPeak, peak);
  return { pixelBudgetBytes: limit, peakLogicalResidentRgbaBytes: peak, maxConcurrentDecoders: maxDecoders,
    handlesOpened: opened, handlesClosed: closed, injectedFailureOrBudgetRejections: failureCount,
    allHandlesReleased: true, success: true };
}
const normal = await simulatePlayer(2 * BACKGROUND_FRAME_BYTES);
const constrained = await simulatePlayer(BACKGROUND_FRAME_BYTES);

const rgb565Frame = 480 * 480 * 2;
const spriteRgb = sum(clips.map(clip => clip.derivedRgb565Bytes));
const largestSpriteClip = Math.max(...clips.map(clip => clip.derivedRgb565Bytes));
const psramCapacity = 8 * 1024 * 1024, reserve = 2 * 1024 * 1024;
const residentAssumptions = {
  twoRgb565PanelFrames: 2 * rgb565Frame, selectedRgb565Scene: rgb565Frame,
  selectedSpriteClipsRgb565: spriteRgb, optionalRgb565DrawStrip480x40: 480 * 40 * 2,
  fourRgb565Palettes: 4 * 16 * 2,
};
const replacementAssumptions = {
  candidateRgb565Scene: rgb565Frame, candidateSpriteClipRgb565: largestSpriteClip,
  decoderScratchCap: 512 * 1024, largestJsonPackUtf8Staging: largest.bytes,
  parsedBase64StringCopyUtf8: largest.base64Characters, decodedJpegInput: largest.jpegBytes,
  parserAndAssetObjectsCap: 64 * 1024, fullRgb888DecodeSurface: 480 * 480 * 3,
};
const steady = sum(Object.values(residentAssumptions));
const switching = steady + sum(Object.values(replacementAssumptions));
const rgbaSwitching = switching - replacementAssumptions.fullRgb888DecodeSurface + BACKGROUND_FRAME_BYTES;
const budgetCheck = (required, capacity = psramCapacity) => ({ requiredBytes: required, reserveBytes: reserve,
  capacityBytes: capacity, headroomAfterReserveBytes: capacity - reserve - required, accepted: required + reserve <= capacity });
assert.ok(budgetCheck(rgbaSwitching).accepted);
const eagerRgba = steady - rgb565Frame + 8 * BACKGROUND_FRAME_BYTES;
assert.equal(budgetCheck(eagerRgba).accepted, false, 'all-eight RGBA residency must fail the modeled device cap');
assert.equal(budgetCheck(rgbaSwitching, 4 * 1024 * 1024).accepted, false);
const internalAssumptions = { mainTaskStack: 8192, ioTaskStack: 8192, networkChunk: ASSET_LIMITS.chunkBytes,
  manifestBuffer: ASSET_LIMITS.manifestBytes, dmaBounceAllowance: 32768, coreSaveAndControlAllowance: 8192 };
const internalTarget = 120 * 1024, internalTotal = sum(Object.values(internalAssumptions));
assert.ok(internalTotal < internalTarget);
const partitionLines = readFileSync(join(root, 'firmware/partitions.csv'), 'utf8').split('\n').filter(line => line.trim() && !line.startsWith('#'));
const partitions = partitionLines.map(line => {
  const fields = line.split(',').map(field => field.trim()); return { name: fields[0], offset: Number(fields[3]), bytes: Number(fields[4]) };
});
const assetsPartition = partitions.find(partition => partition.name === 'assets').bytes;
assert.ok(partitions.every(partition => partition.offset + partition.bytes <= 16 * 1024 * 1024));
const rawAlternativeWithStaging = 8 * rgb565Frame + spritePackBytes + rgb565Frame;
assert.ok(rawAlternativeWithStaging <= assetsPartition);

const report = {
  formatVersion: 1, result: 'PASS', scope: 'Host file measurements, real browser-player lifecycle with injected fake decoder, and proposed ESP memory accounting. No hardware allocation or FPS measurement.',
  measuredFiles: { sourceBudget: 'assets/backgrounds/budget.json', serviceValidator: 'service/asset-signing.ts#inspectPack', backgrounds: measurements,
    backgroundPackBytes, jpegBytes: sum(measurements.map(item => item.jpegBytes)), spritePackBytes, totalRuntimePackBytes: runtimeBytes,
    largestPackBytes: largest.bytes, allPayloadsPlusLargestStageBytes: runtimeBytes + largest.bytes },
  measuredSpriteDecodes: { basis: 'Actual decodeFrame output lengths for original companion/wild/capture-effect/gallery clips. Duplicate Mote clips count separately because prepareFrames creates separate canvases.', clips,
    rgbaPixelBytes: sum(clips.map(clip => clip.decodedRgbaBytes)), derivedRgb565PixelBytes: spriteRgb,
    largestClipReplacementExtraRgb565Bytes: largestSpriteClip, excludes: 'Canvas/ImageData/GPU copies, encoded JS objects and private imported artwork.' },
  browserPlayerSimulation: { basis: 'Real createBackgroundPlayer with fake 480x480 closeable bitmap handles; RGBA byte costs derive from its contract, not JS/GPU heap measurements.',
    scenarios: ['all eight scenes sequentially', 'rapid latest-request-wins switching', 'decoder failure and retry', 'malformed JPEG rejection', 'missing pack fallback', 'destroy during decode', 'one-frame budget rejects atomic replacement and retains previous bitmap'], normal, constrained,
    cache: { backend: 'IndexedDB in the browser, not ESP RAM', accountedLimitBytes: ASSET_LIMITS.cacheBytes,
      accountant: 'web/asset-cache.js#assetCacheBytes, using actual catalog envelopes, pack bytes and version high-water records',
      accountedCompleteBytes, accountedWithLargestStageBytes, accountedHeadroomBytes: ASSET_LIMITS.cacheBytes - accountedWithLargestStageBytes,
      payloadAndStagingOnlyBytes: runtimeBytes + largest.bytes,
      basis: 'Current 11-pack state plus one full largest replacement stage; same-version staging models verified repair/replacement capacity. Includes the real accountant\'s signature/metadata/index allowance, not an IndexedDB engine disk measurement.',
      excludes: 'Engine-specific IndexedDB disk overhead, JSON/base64 JS strings in RAM, parsed objects, decoder scratch and browser/GPU overhead.' } },
  proposedEspPsram: { nominalCapacityBytes: psramCapacity, assumedRequiredReserveBytes: reserve,
    residentAssumptions, replacementAssumptions, steadyRgb565: budgetCheck(steady), atomicSwitchRgb888Decoder: budgetCheck(switching),
    atomicSwitchRgbaDecoderAlternative: budgetCheck(rgbaSwitching), rejectedEagerEightRgbaScenes: budgetCheck(eagerRgba),
    rejectedConstrained4MiBDevice: budgetCheck(rgbaSwitching, 4 * 1024 * 1024),
    placement: 'All listed pixels, decoder scratch, pack/base64/JPEG copies and parser objects are assumed PSRAM-capable. Allocation placement and fragmentation must be measured on the selected board.',
    reservePurpose: 'At least 2 MiB held for unmodeled GUI/fonts/driver allocations and fragmentation; this is an assumption, not a measured SDK footprint.' },
  proposedEspInternalSram: { basis: 'Application incremental allowance, not total ESP-IDF/Wi-Fi/TLS/internal-heap usage.',
    assumptions: internalAssumptions, proposedBytes: internalTotal, targetExclusiveBytes: internalTarget, headroomToTargetBytes: internalTarget - internalTotal,
    excludes: 'Existing ESP-IDF/FreeRTOS/radio/TLS heaps, actual DMA requirements, ISR stacks and driver descriptors; no measured less-than-120-KiB claim.' },
  flashPlanning: { nominalCapacityBytes: 16 * 1024 * 1024, configuredPartitions: partitions, reservedAssetsBytes: assetsPartition,
    compressedPayloadsAndStagingBytes: runtimeBytes + largest.bytes, compressedHeadroomBeforeFilesystemMetadataBytes: assetsPartition - runtimeBytes - largest.bytes,
    futureEightRawRgb565ScenesBytes: 8 * rgb565Frame, rawAlternativeWithSpritePacksAndOneRawStageBytes: rawAlternativeWithStaging,
    rawAlternativeHeadroomBeforeFilesystemMetadataBytes: assetsPartition - rawAlternativeWithStaging,
    note: 'Partition reservations only. Raw backgrounds are not in the runtime bundle. ESP asset loader, atomic activation, JPEG decoder, display drivers and firmware image size remain unimplemented or unmeasured.' },
  limitations: ['No physical ESP32 execution or target compilation.', 'No JPEG entropy decode, display bandwidth, FPS, power or runtime measurement.',
    'Model budget rejection is not an implemented firmware allocator.', 'A decoder requiring more than 512 KiB scratch or additional full-frame copies invalidates the assumed peak.',
    'The selected clip model includes original art only; 64x64 private sprites require a separate bound.'],
};
mkdirSync(dirname(outputPath), { recursive: true });
writeFileSync(outputPath, `${JSON.stringify(report, null, 2)}\n`);
console.log(`PASS display budget: ${runtimeBytes} encoded bytes; player peak ${normal.peakLogicalResidentRgbaBytes} logical RGBA bytes; proposed ESP switch ${rgbaSwitching} bytes + ${reserve} reserve.\n${outputPath}`);

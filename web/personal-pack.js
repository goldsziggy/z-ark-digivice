import { ASSET_LIMITS, sha256, validatePersonalPackSchema } from './asset-cache.js';

export const PERSONAL_LIMITS = Object.freeze({ packBytes: 256 * 1024, provenanceBytes: 64 * 1024, frameBytes: 16384 });
const trustedLocally = new WeakSet();
const encoder = new TextEncoder();
const animations = ['idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate'];
const originalSlots = new Set(['mote', 'glint', 'lumen', 'flicker', 'rill', 'brine', 'pelagia', 'cinder', 'scoria', 'pyrel']);
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const integer = (value, min, max) => Number.isSafeInteger(value) && value >= min && value <= max;
function require(value, message) { if (!value) throw new Error(message); }
function boundedText(value, max = 2048) { return typeof value === 'string' && value.trim().length > 0 && value.length <= max; }
function validUrl(value) {
  if (!boundedText(value)) return false;
  try { const url = new URL(value); return ['https:', 'http:'].includes(url.protocol) && !url.username && !url.password; } catch { return false; }
}
export function isPersonalPack(pack) { return object(pack) && trustedLocally.has(pack); }

// Local user selection is the trust decision. This is structural validation and
// hash matching of two local files, never a claim of a licensed or signed pack.
export async function validatePersonalImport(packText, provenanceText, { expectedArtId = null } = {}) {
  require(expectedArtId === null || typeof expectedArtId === 'string' && /^ds-form-[1-9][0-9]{0,2}$/.test(expectedArtId)
    && Number(expectedArtId.slice(8)) <= 512, 'Unsupported exact form artwork identity.');
  require(typeof packText === 'string' && encoder.encode(packText).length <= PERSONAL_LIMITS.packBytes, 'Personal pack exceeds 256 KiB.');
  require(typeof provenanceText === 'string' && encoder.encode(provenanceText).length <= PERSONAL_LIMITS.provenanceBytes, 'Provenance exceeds 64 KiB.');
  let pack, sidecar;
  try { pack = JSON.parse(packText); sidecar = JSON.parse(provenanceText); } catch { throw new Error('Choose the generated pack.json and provenance.json files.'); }
  require(object(pack) && typeof pack.packId === 'string' && /^personal-[a-z0-9][a-z0-9-]{0,38}$/.test(pack.packId) && integer(pack.version, 1, 0x7fffffff), 'Personal art requires its own personal- pack ID and version.');
  require(pack.license === 'LicenseRef-Personal-Use-Restrictions', 'Personal artwork must retain its restricted-use label.');
  require(object(sidecar) && sidecar.formatVersion === 1 && sidecar.packId === pack.packId && sidecar.version === pack.version &&
    sidecar.packBytes === encoder.encode(packText).length && integer(sidecar.decodedBytes, 1, ASSET_LIMITS.decodedPackBytes) &&
    typeof sidecar.packSha256 === 'string' && /^[a-f0-9]{64}$/.test(sidecar.packSha256), 'Pack and provenance identity or resource bounds disagree.');
  require(await sha256(encoder.encode(packText)) === sidecar.packSha256, 'The pack does not match its provenance checksum.');
  const provenance = sidecar.provenance;
  require(object(provenance) && boundedText(provenance.creator, 512) && validUrl(provenance.sourceUrl) &&
    boundedText(provenance.game, 512) && boundedText(provenance.rights) && boundedText(provenance.reuseScope), 'Record the creator, source URL, game, rights and reuse scope.');
  require(object(pack.sprites) && Object.keys(pack.sprites).length > 0 && Object.keys(pack.sprites).length <= 10 &&
    (expectedArtId === null ? Object.keys(pack.sprites).every(id => originalSlots.has(id))
      : pack.packId === `personal-${expectedArtId}` && Object.keys(pack.sprites).length === 1 && Object.hasOwn(pack.sprites, expectedArtId)) && object(pack.effects) && !Object.keys(pack.effects).length &&
    object(pack.icons) && !Object.keys(pack.icons).length, 'Personal art may replace known creature appearances only.');
  validatePersonalPackSchema(pack, { id: pack.packId, version: pack.version, decodedBytes: sidecar.decodedBytes });
  require(object(sidecar.coverage) && Object.keys(sidecar.coverage).length === Object.keys(pack.sprites).length, 'Every imported creature needs animation coverage.');
  for (const [id, sprite] of Object.entries(pack.sprites)) {
    const coverage = sidecar.coverage[id];
    require(object(coverage) && Object.keys(coverage).length === animations.length, `Missing animation coverage for ${id}.`);
    for (const name of animations) {
      const info = coverage[name];
      require(object(info) && ['genuine', 'reused-fallback'].includes(info.kind) &&
        integer(info.originalFrameCount, 0, 8) && info.outputFrameCount === sprite.animations[name].frames.length &&
        Array.isArray(info.sourceRects) && info.sourceRects.length <= 8 &&
        info.sourceRects.every(rect => Array.isArray(rect) && rect.length === 4 && rect.every((v, i) => integer(v, i < 2 ? 0 : 1, 4096))),
      `Invalid ${id}/${name} coverage.`);
      if (info.kind === 'reused-fallback') {
        require(animations.includes(info.sourceAnimation) && info.sourceAnimation !== name &&
          coverage[info.sourceAnimation]?.kind === 'genuine' && info.originalFrameCount === 0 &&
          JSON.stringify(sprite.animations[name].frames) === JSON.stringify(sprite.animations[info.sourceAnimation].frames),
        `The ${id}/${name} fallback must identify its actual source frames.`);
      } else {
        require(info.originalFrameCount > 0 && info.originalFrameCount === info.outputFrameCount && info.sourceRects.length === info.originalFrameCount,
          `Source-frame counts disagree for ${id}/${name}.`);
      }
    }
  }
  require(Array.isArray(sidecar.sources) && sidecar.sources.length >= 1 && sidecar.sources.length <= 32 && sidecar.sources.every(source =>
    object(source) && boundedText(source.path, 512) && /^[a-f0-9]{64}$/.test(source.sha256) && integer(source.bytes, 1, 16 * 1024 * 1024) &&
    integer(source.width, 1, 4096) && integer(source.height, 1, 4096)), 'Source sheet provenance is missing or out of bounds.');
  trustedLocally.add(pack);
  return { pack, provenance: sidecar };
}

import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { validatePersonalImport, isPersonalPack } from '../web/personal-pack.js';
import { decodeFrame, decodePersonalFrame, validatePackedPack } from '../web/asset-cache.js';

export function personalFixture(size = 64) {
  const frame = Buffer.alloc(size * size / 2, 0x11).toString('base64');
  const animations = Object.fromEntries(['idle','attack','hurt','sleep','care','celebrate'].map(name => [name, { frameMs: 200, frames: [frame] }]));
  const pack = { formatVersion: 1, packId: 'personal-original-test', version: 1, license: 'LicenseRef-Personal-Use-Restrictions', paletteEncoding: 'rgb565',
    sprites: { mote: { name: 'Original test square', family: 'synthetic', stage: 1, width: size, height: size, palette: [0, 2016, ...Array(14).fill(0)], transparentIndex: 0, animations } }, effects: {}, icons: {} };
  const coverage = Object.fromEntries(Object.keys(animations).map(name => [name, { kind: name === 'idle' ? 'genuine' : 'reused-fallback', sourceAnimation: 'idle', originalFrameCount: name === 'idle' ? 1 : 0, outputFrameCount: 1, sourceRects: name === 'idle' ? [[0,0,size,size]] : [], transforms: [] }]));
  const provenance = { formatVersion: 1, packId: pack.packId, version: 1, packBytes: 0, packSha256: '', decodedBytes: size * size * 4 * 6,
    provenance: { creator: 'Project test', sourceUrl: 'https://example.com/original-test', game: 'Original synthetic fixture', rights: 'Original test artwork', reuseScope: 'Local test only' },
    coverage: { mote: coverage }, sources: [{ path: 'original-test.png', sha256: 'a'.repeat(64), bytes: 100, width: size, height: size }], conversion: {} };
  return { pack, provenance };
}
export function encodeFixture({ pack, provenance }) {
  const packText = JSON.stringify(pack);
  provenance.packBytes = Buffer.byteLength(packText);
  provenance.packSha256 = createHash('sha256').update(packText).digest('hex');
  return [packText, JSON.stringify(provenance)];
}

test('explicit personal import accepts bounded64px art with honest fallback; public pack decoder stays32px', async () => {
  const fixture = personalFixture(); const [packText, sidecarText] = encodeFixture(fixture);
  const result = await validatePersonalImport(packText, sidecarText);
  assert.ok(isPersonalPack(result.pack)); assert.ok(Object.isFrozen(result.pack));
  assert.equal(decodePersonalFrame(result.pack, 'sprites', 'mote').data.byteLength, 16384);
  assert.throws(() => decodeFrame(result.pack, 'sprites', 'mote'), /Unvalidated/);
  assert.throws(() => validatePackedPack(result.pack, { id: result.pack.packId, version: 1, decodedBytes: fixture.provenance.decodedBytes }), /dimensions/);
  assert.equal(isPersonalPack(fixture.pack), false);
});
test('altered bytes, false fallback, missing provenance and oversized inputs are rejected', async () => {
  const good = encodeFixture(personalFixture());
  await assert.rejects(validatePersonalImport(good[0] + ' ', good[1]), /bounds disagree/);
  const bad = personalFixture(); bad.provenance.coverage.mote.attack.sourceAnimation = 'sleep';
  await assert.rejects(validatePersonalImport(...encodeFixture(bad)), /actual source frames/);
  const noRights = personalFixture(); noRights.provenance.provenance.rights = '';
  await assert.rejects(validatePersonalImport(...encodeFixture(noRights)), /Record the creator/);
  await assert.rejects(validatePersonalImport('x'.repeat(262145), good[1]), /256 KiB/);
  await assert.rejects(validatePersonalImport(good[0], 'x'.repeat(65537)), /64 KiB/);
});
test('personal art cannot replace game logic or import unbounded or unknown appearance slots', async () => {
  const huge = personalFixture(128);
  await assert.rejects(validatePersonalImport(...encodeFixture(huge)), /dimensions/);
  const unknown = personalFixture(); unknown.pack.sprites.unknown = unknown.pack.sprites.mote; delete unknown.pack.sprites.mote;
  await assert.rejects(validatePersonalImport(...encodeFixture(unknown)), /known creature/);
  const url = personalFixture(); url.provenance.provenance.sourceUrl = 'javascript:alert(1)';
  await assert.rejects(validatePersonalImport(...encodeFixture(url)), /Record the creator/);
});

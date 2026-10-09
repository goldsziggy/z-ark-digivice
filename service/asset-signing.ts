import { createPrivateKey, createPublicKey, sign, verify } from 'node:crypto';
import { createRequire } from 'node:module';

// A single pure parser enforces the same JPEG subset at signing and download.
const { backgroundJpegBytes, BACKGROUND_DECODED_BYTES } = createRequire(import.meta.url)('../web/background-pack.js') as {
  backgroundJpegBytes: (pack: unknown) => Uint8Array; BACKGROUND_DECODED_BYTES: number;
};

// PUBLIC, INSECURE TEST FIXTURE. RFC 6979 §A.2.5 publishes this entire key pair.
// https://www.rfc-editor.org/rfc/rfc6979.html#appendix-A.2.5
// Anyone can sign with this fixture. It demonstrates the verification path only.
// Replace through a separate production enrollment/release design, never by reusing it.
export const KEY_ID = 'digivice-dev-v1';
export const PUBLIC_KEY_SPKI = 'MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEYP7UuiVanTHJYet0xjVtaMBJuJI7Yfps5mliLmDyn7Z5A/4QCLi8maQa6elWKLxk8vGyDC1+n1F3o8KU1EYimQ==';
export const MAX_PACK_BYTES = 256 * 1024;
export const MAX_CATALOG_BYTES = 16 * 1024;
export const MAX_DECODED_BYTES = 2 * 1024 * 1024;
export const PACK_IDS = ['starter-v2', 'tide-v1', 'ember-v1'] as const;
// These legacy original creatures are test fixtures, never release game species.
// Keep their IDs for explicit fixture tools and the separate Garage allowlist.
export const BACKGROUND_PACK_IDS = ['scene-meadow-v1', 'scene-forest-v1', 'scene-beach-v1', 'scene-ruins-v1', 'scene-cavern-v1', 'scene-snow-v1', 'scene-volcanic-v1', 'scene-digital-v1'] as const;
export const ALL_PACK_IDS = [...PACK_IDS, ...BACKGROUND_PACK_IDS] as const;
export type PackId = typeof ALL_PACK_IDS[number];
export type PackEntry = { id: PackId; version: number; bytes: number; sha256: string; url: string; required: boolean; decodedBytes: number };
export type AssetManifest = { formatVersion: 1; release: number; rulesVersion: 1; packs: PackEntry[] };
export type SignedCatalog = { keyId: typeof KEY_ID; payloadBase64: string; signature: string };

export function requireDevelopment(): void {
  if (process.env.NODE_ENV === 'production') throw new Error('Public asset signing fixture and local asset service are disabled in production.');
}
function object(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}
function exactKeys(value: Record<string, unknown>, expected: string[]): boolean {
  const keys = Object.keys(value).sort();
  const sorted = [...expected].sort();
  return keys.length === sorted.length && keys.every((key, index) => key === sorted[index]);
}
function boundedInteger(value: unknown, maximum: number): value is number {
  return Number.isSafeInteger(value) && Number(value) > 0 && Number(value) <= maximum;
}
export function validateManifest(value: unknown): asserts value is AssetManifest {
  if (!object(value) || !exactKeys(value, ['formatVersion', 'release', 'rulesVersion', 'packs']) || value.formatVersion !== 1 || value.rulesVersion !== 1 || !boundedInteger(value.release, 0x7fffffff) || !Array.isArray(value.packs) || value.packs.length > ALL_PACK_IDS.length) throw new Error('Unsupported asset manifest or resource bounds.');
  const ids = new Set<string>();
  for (const pack of value.packs) {
    if (!object(pack) || !exactKeys(pack, ['id', 'version', 'bytes', 'sha256', 'url', 'required', 'decodedBytes']) || typeof pack.id !== 'string' || !ALL_PACK_IDS.includes(pack.id as PackId) || ids.has(pack.id) || !boundedInteger(pack.version, 0x7fffffff) || !boundedInteger(pack.bytes, MAX_PACK_BYTES) || !boundedInteger(pack.decodedBytes, MAX_DECODED_BYTES) || typeof pack.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(pack.sha256) || pack.url !== `/api/assets/packs/${pack.id}/${pack.version}` || pack.required !== (pack.id === 'starter-v2')) throw new Error('Invalid or out-of-bounds asset pack entry.');
    ids.add(pack.id);
  }
}
function decodeBase64(value: unknown, maximum: number): Buffer {
  if (typeof value !== 'string' || value.length > Math.ceil(maximum / 3) * 4 || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value)) throw new Error('Invalid asset signature envelope encoding.');
  const bytes = Buffer.from(value, 'base64');
  if (!bytes.length || bytes.length > maximum || bytes.toString('base64') !== value) throw new Error('Invalid asset signature envelope size.');
  return bytes;
}
/** Shared exact-byte signature envelope for bounded development asset catalogs. */
export function verifyDevelopmentEnvelope(envelope: unknown, maximum = 10 * 1024): Buffer {
  requireDevelopment();
  if (!Number.isSafeInteger(maximum) || maximum < 1 || maximum > 10 * 1024) throw new Error('Invalid development payload limit.');
  if (!object(envelope) || !exactKeys(envelope, ['keyId', 'payloadBase64', 'signature']) || envelope.keyId !== KEY_ID) throw new Error('Unknown asset signing key or envelope.');
  const payload = decodeBase64(envelope.payloadBase64, maximum);
  const signature = decodeBase64(envelope.signature, 64);
  if (signature.length !== 64 || !verify('sha256', payload, { key: createPublicKey({ key: Buffer.from(PUBLIC_KEY_SPKI, 'base64'), format: 'der', type: 'spki' }), dsaEncoding: 'ieee-p1363' }, signature)) throw new Error('Asset catalog signature verification failed.');
  return payload;
}
export function verifyCatalog(envelope: unknown): AssetManifest {
  const manifest: unknown = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(verifyDevelopmentEnvelope(envelope)));
  validateManifest(manifest);
  return manifest;
}
export function signDevelopmentPayload(payload: Uint8Array): SignedCatalog {
  requireDevelopment();
  if (!(payload instanceof Uint8Array) || payload.byteLength < 1 || payload.byteLength > 10 * 1024) throw new Error('Development signing payload exceeds bounds.');
  const key = createPrivateKey({
    format: 'jwk', key: {
      kty: 'EC', crv: 'P-256',
      x: Buffer.from('60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6', 'hex').toString('base64url'),
      y: Buffer.from('7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299', 'hex').toString('base64url'),
      // Published RFC example scalar; deliberately not a private credential.
      d: Buffer.from('C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721', 'hex').toString('base64url'),
    },
  });
  const envelope: SignedCatalog = { keyId: KEY_ID, payloadBase64: Buffer.from(payload).toString('base64'), signature: sign('sha256', payload, { key, dsaEncoding: 'ieee-p1363' }).toString('base64') };
  if (Buffer.byteLength(JSON.stringify(envelope)) > MAX_CATALOG_BYTES) throw new Error('Asset catalog exceeds size limit.');
  verifyDevelopmentEnvelope(envelope);
  return envelope;
}
export function signManifest(manifest: AssetManifest): SignedCatalog {
  validateManifest(manifest);
  const envelope = signDevelopmentPayload(Buffer.from(JSON.stringify(manifest)));
  verifyCatalog(envelope);
  return envelope;
}

/** The browser decoder contract: bounded 16/32px RGB565-indexed animations. */
export function inspectPack(value: unknown, expectedId: PackId): { version: number; decodedBytes: number } {
  if (object(value) && value.formatVersion === 3) {
    if (!BACKGROUND_PACK_IDS.includes(expectedId as typeof BACKGROUND_PACK_IDS[number]) || value.packId !== expectedId) throw new Error('Unsupported background identity.');
    backgroundJpegBytes(value);
    return { version: Number(value.version), decodedBytes: BACKGROUND_DECODED_BYTES };
  }
  if (!PACK_IDS.includes(expectedId as typeof PACK_IDS[number])) throw new Error('Background identity requires format 3.');
  if (!object(value) || !exactKeys(value, ['formatVersion', 'packId', 'version', 'license', 'paletteEncoding', 'sprites', 'effects', 'icons']) || value.formatVersion !== 1 || value.packId !== expectedId || !boundedInteger(value.version, 0x7fffffff) || value.license !== 'CC0-1.0' || value.paletteEncoding !== 'rgb565') throw new Error('Unsupported asset pack format, identity, or license.');
  const animationsAllowed = ['idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate'];
  const idPattern = /^(?!constructor$|prototype$)[a-z][a-z0-9-]{0,47}$/;
  let decodedBytes = 0;
  for (const group of ['sprites', 'effects', 'icons']) {
    const members = value[group];
    if (!object(members) || Object.keys(members).length > 32 || (group === 'sprites' && Object.keys(members).length === 0)) throw new Error('Invalid asset sprite group.');
    for (const [id, sprite] of Object.entries(members)) {
      if (!idPattern.test(id) || !object(sprite) || !exactKeys(sprite, ['name', 'family', 'stage', 'width', 'height', 'palette', 'transparentIndex', 'animations']) || (sprite.width !== 16 && sprite.width !== 32) || sprite.height !== sprite.width || typeof sprite.name !== 'string' || !sprite.name.length || sprite.name.length > 64 || typeof sprite.family !== 'string' || !sprite.family.length || sprite.family.length > 48 || !Number.isInteger(sprite.stage) || Number(sprite.stage) < 0 || Number(sprite.stage) > 16 || !Array.isArray(sprite.palette) || sprite.palette.length !== 16 || !sprite.palette.every((color) => Number.isInteger(color) && color >= 0 && color <= 65535) || sprite.transparentIndex !== 0 || !object(sprite.animations) || !Object.hasOwn(sprite.animations, 'idle') || Object.keys(sprite.animations).length > 6 || (group === 'sprites' && !animationsAllowed.every((name) => Object.hasOwn(sprite.animations as Record<string, unknown>, name)))) throw new Error('Invalid asset sprite dimensions, fields, palette, or required animations.');
      const width = Number(sprite.width);
      const frameBytes = width * width / 2;
      for (const [animationId, animation] of Object.entries(sprite.animations)) {
        if (!animationsAllowed.includes(animationId) || !object(animation) || !exactKeys(animation, ['frameMs', 'frames']) || !boundedInteger(animation.frameMs, 2000) || animation.frameMs < 40 || !Array.isArray(animation.frames) || !animation.frames.length || animation.frames.length > 8) throw new Error('Invalid animation resource bounds.');
        for (const frame of animation.frames) if (decodeBase64(frame, frameBytes).length !== frameBytes) throw new Error('Invalid indexed frame byte length.');
        decodedBytes += animation.frames.length * width * width * 4;
        if (decodedBytes > MAX_DECODED_BYTES) throw new Error('Decoded asset pack exceeds resource bounds.');
      }
    }
  }
  return { version: value.version, decodedBytes };
}

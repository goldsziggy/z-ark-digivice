// Shared by the release signer, cache and renderer. This parses headers and
// bounded JPEG structure before a browser/native decoder allocates pixels.
// Entropy decoding still belongs to that decoder; it must also confirm 480².
export const BACKGROUND_SCENES = Object.freeze(['meadow', 'forest', 'beach', 'ruins', 'cavern', 'snow', 'volcanic', 'digital']);
export const BACKGROUND_DECODED_BYTES = 480 * 480 * 4;
export const BACKGROUND_JPEG_BYTES = 150 * 1024;
const LICENSE = 'User-approved generated art; source license unspecified';
function requireValue(value, message) { if (!value) throw new Error(`Invalid background: ${message}`); }
function exact(value, keys) {
  return value !== null && typeof value === 'object' && !Array.isArray(value) &&
    Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
}

/** Only the single-scan, 8-bit baseline JPEG subset emitted by our art build. */
export function validateBackgroundJpeg(bytes, dimension = 480) {
  requireValue(dimension === 480 || dimension === 412, 'unsupported display dimension');
  requireValue(bytes instanceof Uint8Array && bytes.length >= 64 && bytes.length <= BACKGROUND_JPEG_BYTES, 'JPEG byte budget');
  requireValue(bytes[0] === 0xff && bytes[1] === 0xd8, 'JPEG SOI required');
  const word = offset => (bytes[offset] << 8) | bytes[offset + 1];
  let offset = 2, frame = null, scan = false, restartInterval = 0;
  const quantization = new Set(), huffman = new Set();
  while (offset < bytes.length) {
    requireValue(bytes[offset++] === 0xff, 'JPEG marker boundary');
    while (bytes[offset] === 0xff) ++offset;
    const marker = bytes[offset++];
    requireValue(marker !== undefined && marker !== 0 && marker !== 0xd8 && marker !== 0xd9, 'unexpected JPEG marker');
    requireValue(offset + 2 <= bytes.length, 'truncated JPEG segment');
    const length = word(offset), end = offset + length;
    requireValue(length >= 2 && end <= bytes.length, 'JPEG segment length');
    let cursor = offset + 2;
    if (marker === 0xe0) {
      // JFIF only; no arbitrary EXIF, ICC, comment or application payloads.
      requireValue(length === 16 && bytes[cursor] === 0x4a && bytes[cursor + 1] === 0x46 &&
        bytes[cursor + 2] === 0x49 && bytes[cursor + 3] === 0x46 && bytes[cursor + 4] === 0 &&
        bytes[end - 2] === 0 && bytes[end - 1] === 0, 'unsupported JPEG application data');
    } else if (marker === 0xdb) {
      requireValue(cursor < end, 'empty JPEG quantization table');
      while (cursor < end) {
        const table = bytes[cursor++];
        requireValue(table <= 3 && cursor + 64 <= end, '8-bit JPEG quantization table');
        for (let index = 0; index < 64; ++index) requireValue(bytes[cursor + index] > 0, 'zero JPEG quantization value');
        quantization.add(table); cursor += 64;
      }
    } else if (marker === 0xc4) {
      requireValue(cursor < end, 'empty JPEG Huffman table');
      while (cursor < end) {
        const table = bytes[cursor++];
        requireValue((table & 0xec) === 0 && cursor + 16 <= end, 'JPEG Huffman table selector');
        let count = 0, available = 1;
        for (let index = 0; index < 16; ++index) {
          available = available * 2 - bytes[cursor + index]; count += bytes[cursor + index];
          requireValue(available >= 0, 'oversubscribed JPEG Huffman table');
        }
        cursor += 16;
        requireValue(count > 0 && count <= 256 && cursor + count <= end, 'JPEG Huffman table length');
        huffman.add(table); cursor += count;
      }
    } else if (marker === 0xc0) {
      requireValue(!frame && length === 17 && bytes[cursor] === 8 && word(cursor + 1) === dimension &&
        word(cursor + 3) === dimension && bytes[cursor + 5] === 3, `baseline JPEG must be ${dimension} by ${dimension} RGB`);
      frame = new Map(); cursor += 6;
      for (let index = 0; index < 3; ++index) {
        const id = bytes[cursor++], sampling = bytes[cursor++], table = bytes[cursor++];
        requireValue(!frame.has(id) && id >= 1 && id <= 3 && (sampling >> 4) >= 1 && (sampling >> 4) <= 2 &&
          (sampling & 15) >= 1 && (sampling & 15) <= 2 && table <= 3, 'JPEG component dimensions');
        frame.set(id, table);
      }
    } else if (marker === 0xdd) {
      requireValue(length === 4, 'JPEG restart interval length'); restartInterval = word(cursor);
    } else if (marker === 0xda) {
      requireValue(frame && !scan && length === 12 && bytes[cursor++] === 3, 'single interleaved JPEG scan required');
      const components = new Set();
      for (let index = 0; index < 3; ++index) {
        const id = bytes[cursor++], tables = bytes[cursor++];
        requireValue(frame.has(id) && !components.has(id) && quantization.has(frame.get(id)) &&
          huffman.has(tables >> 4) && huffman.has(0x10 | (tables & 15)), 'JPEG scan references missing tables');
        components.add(id);
      }
      requireValue(bytes[cursor] === 0 && bytes[cursor + 1] === 63 && bytes[cursor + 2] === 0, 'baseline JPEG scan parameters');
      scan = true; cursor = end; const entropyStart = cursor;
      while (cursor < bytes.length) {
        if (bytes[cursor++] !== 0xff) continue;
        while (bytes[cursor] === 0xff) ++cursor;
        const next = bytes[cursor++];
        if (next === 0) continue;
        if (next >= 0xd0 && next <= 0xd7 && restartInterval > 0) continue;
        requireValue(next === 0xd9 && cursor === bytes.length && cursor - entropyStart > 2, 'JPEG must end after one complete scan');
        return bytes;
      }
      requireValue(false, 'JPEG EOI required');
    } else requireValue(false, 'unsupported JPEG marker or progressive format');
    offset = end;
  }
  requireValue(false, 'JPEG scan required');
}

export function backgroundJpegBytes(pack) {
  requireValue(exact(pack, ['formatVersion', 'packId', 'version', 'rulesVersion', 'license', 'background', 'provenance']) &&
    pack.formatVersion === 3 && Number.isSafeInteger(pack.version) && pack.version >= 1 && pack.version <= 0x7fffffff &&
    pack.rulesVersion === 1 && pack.license === LICENSE, 'pack format or license');
  const image = pack.background;
  requireValue(exact(image, ['sceneId', 'name', 'encoding', 'width', 'height', 'data']) && BACKGROUND_SCENES.includes(image.sceneId) &&
    pack.packId === `scene-${image.sceneId}-v1` && typeof image.name === 'string' && image.name.length >= 1 && image.name.length <= 64 &&
    image.encoding === 'jpeg-base64' && image.width === 480 && image.height === 480, 'scene identity or dimensions');
  requireValue(exact(pack.provenance, ['source', 'sourceSha256']) && pack.provenance.source === 'approved-generated-background' &&
    typeof pack.provenance.sourceSha256 === 'string' && /^[a-f0-9]{64}$/.test(pack.provenance.sourceSha256), 'source provenance');
  requireValue(typeof image.data === 'string' && image.data.length <= Math.ceil(BACKGROUND_JPEG_BYTES / 3) * 4 &&
    /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(image.data), 'bounded JPEG base64');
  const binary = atob(image.data);
  requireValue(btoa(binary) === image.data, 'canonical JPEG base64');
  return validateBackgroundJpeg(Uint8Array.from(binary, character => character.charCodeAt(0)));
}

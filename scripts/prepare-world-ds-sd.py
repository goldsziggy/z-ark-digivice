#!/usr/bin/env python3
"""Stage audited private 32px form packs for manual microSD copying.

Writes only inside ignored .personal-assets/world-ds/sd-card. It never mounts,
formats, writes to a physical card, downloads artwork, or flashes a device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = ROOT / '.personal-assets' / 'world-ds'
MAX_BYTES = 24688


def checked_blob(descriptor, maximum):
    path = PRIVATE / descriptor['file']
    if path.resolve() != path.absolute() or not path.resolve().is_relative_to(PRIVATE.resolve()):
        raise ValueError('Private pack path cannot escape its directory or use a symlink')
    if not path.is_file() or not 0 < path.stat().st_size <= maximum:
        raise ValueError('Private pack is missing or exceeds its byte budget')
    data = path.read_bytes()
    if len(data) != descriptor['bytes'] or hashlib.sha256(data).hexdigest() != descriptor['sha256']:
        raise ValueError('Private pack differs from its audited index')
    return data


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    index_path = PRIVATE / 'index.json'
    if PRIVATE.resolve() != PRIVATE.absolute() or index_path.stat().st_size > 1024 * 1024:
        raise ValueError('Private index path or size is invalid')
    index = json.loads(index_path.read_text())
    if index['formatVersion'] != 1 or index['collection'] != 'digimon-world-ds' or index['privateOnly'] is not True:
        raise ValueError('Expected the audited private World DS index')
    if not 1 <= len(index['entries']) <= 512:
        raise ValueError('Invalid form count')
    prepared, attribution = [], []
    for entry in index['entries']:
        ident = entry['artId']
        if not ident.startswith('ds-form-') or not ident[8:].isdigit():
            raise ValueError('Expected an exact numeric form identity')
        form = int(ident[8:])
        if not 1 <= form <= 512 or ident != f'ds-form-{form}':
            raise ValueError('Invalid form identity')
        if any(row[0]['formId'] == form for row in prepared):
            raise ValueError('Duplicate form identity')
        device = entry['device']
        data = checked_blob(device, MAX_BYTES)
        if (device['width'], device['height']) != (32, 32) or len(data) < 112 or data[:4] != b'DVA1':
            raise ValueError('Expected compact32px DVA1')
        version, header, width, height = struct.unpack_from('<HHxxxxHH', data, 4)
        payload, crc = struct.unpack_from('<II', data, 24)
        if (version, header, width, height) != (1, 32, 32, 32) or payload != len(data) - 32 or zlib.crc32(data[32:]) != crc:
            raise ValueError('DVA1 header or CRC failed')
        provenance = json.loads(checked_blob(entry['provenance'], 64 * 1024))
        filename = f'DSF{form:05}.DVA'
        record = {'formId': form, 'artId': ident, 'file': filename, 'bytes': len(data), 'sha256': device['sha256'],
                  'width': 32, 'height': 32, 'frames': device['frames']}
        prepared.append((record, data))
        attribution.append({'formId': form, 'name': entry['name'], 'provenance': provenance})
    output = PRIVATE / 'sd-card'
    if output.is_symlink():
        raise ValueError('Output must be the private staging directory')
    output.mkdir(exist_ok=True)
    for record, data in prepared:
        temporary = output / (record['file'] + '.tmp')
        temporary.write_bytes(data)
        temporary.replace(output / record['file'])
    manifest = {'formatVersion': 1, 'privateOnly': True,
                'installation': 'Manually copy DSF*.DVA to the card root. No device or physical SD was modified by this preparation script.',
                'entries': [record for record, _ in prepared]}
    (output / 'INDEX.JSON').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'ATTRIB.TXT').write_text('Private unpublished, noncommercial artwork. No underlying franchise license is granted.\n'
                                     + json.dumps(attribution, ensure_ascii=False, indent=2) + '\n')
    print(f'Staged {len(prepared)} private32px packs ({sum(len(data) for _, data in prepared)} bytes) at {output}')
    print('No physical SD card, device, remote service, or public artifact was modified.')


if __name__ == '__main__':
    main()

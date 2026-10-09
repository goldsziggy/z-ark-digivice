#!/usr/bin/env python3
"""Test pinned FatFs on new disposable regular-file images; never opens devices.

The actual VFS stat/fstat functions are extracted from the vendored source so the
host's 64-bit off_t cannot conceal a missing ESP signed-32-bit overflow check.
Only OS locks/time/allocation and the block-device boundary are test doubles.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FATFS = ROOT / 'components/fatfs'


def function(source, name):
    import re
    for match in re.finditer(r'^static [^\n]+\b' + name + r'\(', source, re.M):
        opening = source.index('{', match.start())
        if ';' in source[match.start():opening]:
            continue
        level = 1
        end = opening + 1
        while level:
            level += (source[end] == '{') - (source[end] == '}')
            end += 1
        return source[match.start():end] + '\n'
    raise ValueError(f'Missing actual VFS function: {name}')


def main():
    with tempfile.TemporaryDirectory(prefix='digivice-fatfs-images-') as temporary:
        build = Path(temporary)
        (build / 'freertos').mkdir()
        (build / 'freertos/FreeRTOS.h').write_text('#define portTICK_PERIOD_MS 1\n')
        (build / 'freertos/semphr.h').write_text('/* Single-threaded host test. */\n')
        (build / 'sdkconfig.h').write_text('''
#define CONFIG_FATFS_EXFAT 1
#define CONFIG_FATFS_LFN_HEAP 1
#define CONFIG_FATFS_MAX_LFN 128
#define CONFIG_FATFS_API_ENCODING_UTF_8 1
#define CONFIG_FATFS_CODEPAGE 437
#define CONFIG_FATFS_VOLUME_COUNT 2
#define CONFIG_FATFS_FS_LOCK 0
#define CONFIG_FATFS_TIMEOUT_MS 10000
#define CONFIG_FATFS_PER_FILE_CACHE 1
#define CONFIG_FATFS_USE_DYN_BUFFERS 0
#define CONFIG_WL_SECTOR_SIZE 4096
#define CONFIG_FATFS_VFS_FSTAT_BLKSIZE 0
''')
        source = (FATFS / 'vfs/vfs_fat.c').read_text()
        # Pin imported files against the recorded SDK revision; only the three
        # reviewed local patches and separately added documentation are allowed.
        upstream = json.loads((FATFS / 'UPSTREAM.json').read_text())
        changed = []
        for name, expected in upstream['filesBeforeProjectChanges'].items():
            actual = hashlib.sha256((FATFS / name).read_bytes()).hexdigest()
            if actual != expected:
                changed.append(name)
        if sorted(changed) != ['Kconfig', 'src/ffconf.h', 'vfs/vfs_fat.c']:
            raise ValueError(f'Unexpected changes to pinned FatFs: {changed}')
        dates = source[source.index('/* Date and time storage formats'):source.index('} fat_time_t;') + len('} fat_time_t;')]
        names = ('fresult_to_errno', 'prepend_drive_to_path', 'vfs_fat_fstat', 'get_stat_mode', 'vfs_fat_stat')
        (build / 'actual_vfs_stat.inc').write_text(dates + '\n' + '\n'.join(function(source, n) for n in names))
        subprocess.run([
            'clang', '-std=c17', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-missing-field-initializers',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(build), '-I' + str(FATFS / 'src'),
            str(ROOT / 'tests/fatfs_exfat_test.c'), str(FATFS / 'src/ff.c'),
            str(FATFS / 'src/ffunicode.c'), '-o', str(build / 'test'),
        ], check=True)
        images = [build / name for name in ('fat32.img', 'exfat.img')]
        subprocess.run([str(build / 'test'), *(str(p) for p in images)], check=True)
        evidence = []
        for path in images:
            digest = hashlib.sha256()
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(block)
            evidence.append({'file': path.name, 'bytes': path.stat().st_size, 'sha256': digest.hexdigest()})
        print(json.dumps({'result': 'PASS', 'images': evidence,
                          'sourceRevision': '79e3454c68248bd7d881721c9b2e94561378a8ce',
                          'actualVfsFunctions': list(names), 'deviceAccess': False,
                          'imagesRemovedAfterTest': True}, sort_keys=True))


if __name__ == '__main__':
    main()

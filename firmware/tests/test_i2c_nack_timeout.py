#!/usr/bin/env python3
"""Compile the actual backported SDK function with host RTOS/HAL fault doubles."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / 'components/esp_driver_i2c'


def function(source):
    start = source.index('static void s_i2c_send_commands(')
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def main():
    provenance = json.loads((COMPONENT / 'UPSTREAM.json').read_text())
    for name, digest in provenance['filesAfterProjectChanges'].items():
        assert hashlib.sha256((COMPONENT / name).read_bytes()).hexdigest() == digest, name
    source = (COMPONENT / 'i2c_master.c').read_text()
    # Reverse the two recorded hunks, then require the exact pinned upstream
    # hash. The old failure demonstration thus cannot use an invented baseline.
    with tempfile.TemporaryDirectory(prefix='digivice-i2c-nack-') as temporary:
        build = Path(temporary)
        baseline = build / 'baseline.c'
        baseline.write_text(source)
        subprocess.run(['patch', '--silent', '--reverse', str(baseline),
                        str(COMPONENT / 'bounded-nack.patch')], check=True, timeout=10)
        old_source = baseline.read_text()
        assert hashlib.sha256(baseline.read_bytes()).hexdigest() == provenance['filesBeforeProjectChanges']['i2c_master.c']
        outputs = []
        for name, text in [('baseline', old_source), ('patched', source)]:
            (build / 'actual_i2c_send_commands.inc').write_text(function(text))
            binary = build / name
            subprocess.run(['clang', '-std=c17', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', '-I' + str(build),
                            str(ROOT / 'tests/i2c_nack_timeout_test.c'), '-o', str(binary)],
                           check=True, timeout=30)
            args = [str(binary)] + (['baseline-stuck'] if name == 'baseline' else [])
            result = subprocess.run(args, capture_output=True, text=True, timeout=10)
            if name == 'baseline':
                assert result.returncode == 86 and result.stderr == 'UNBOUNDED_NACK_GUARD\n', result
                outputs.append('Baseline reproduced: exact pinned SDK exceeded 100000 busy polls without returning.')
            else:
                assert result.returncode == 0, result.stderr
                outputs.append(result.stdout.strip())
        for line in outputs:
            print(line)
        print(json.dumps({'result': 'PASS', 'sdkRevision': provenance['revision'],
                          'backportCommit': provenance['backport']['commit'],
                          'actualFunctionSha256': hashlib.sha256(function(source).encode()).hexdigest(),
                          'sanitizers': ['address', 'undefined'], 'deviceAccess': False}, sort_keys=True))


if __name__ == '__main__':
    main()

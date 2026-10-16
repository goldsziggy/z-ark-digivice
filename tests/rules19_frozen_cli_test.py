"""Goldens captured from the pre-rules20 checkout, using synthetic game histories."""
import base64
import json
import subprocess
import sys
from pathlib import Path

core = sys.argv[1]
cases = json.loads((Path(__file__).parent / 'fixtures/rules19-care-evolution-golden.json').read_text())
def run(args, events):
    reply = subprocess.run([core, *args], input=events, text=True, capture_output=True)
    assert reply.returncode == 0, reply.stderr
    return json.loads(reply.stdout)
for case in cases:
    seed, events, expected = str(case['seed']), case['events'], case['expected']
    frozen = run(['--replay-v19-onboarding-trace', seed], events)
    assert frozen == expected, 'Frozen rules19 must match the old executor, including JSON and Auto trace'
    migrated = run(['--migrate-v19-onboarding', seed], events)
    assert (migrated['state']['rulesVersion'], migrated['state']['schemaVersion']) == (22, 30)
    for key in expected['state']:
        if key not in ('rulesVersion', 'schemaVersion', 'evolution', 'expeditions'):
            assert migrated['state'][key] == expected['state'][key], key
    encoded = base64.b64decode(migrated['snapshotBase64'])
    assert len(encoded) == 6876 and encoded[4:6] == bytes((30, 0)) and encoded[8:12] == bytes((22, 0, 0, 0))
    assert run(['--replay-snapshot', migrated['snapshotBase64']], '') == migrated['state']
    fold = run(['--fold-v19-onboarding', seed], events)
    fold_trace = run(['--replay-fold-v19-onboarding-trace', seed], events)
    for key in fold_trace['state']:
        if key not in ('rulesVersion', 'schemaVersion', 'evolution', 'expeditions'):
            assert fold['state'][key] == fold_trace['state'][key], key
for payload in ('AA==', 'AAAA', 'AAAAAA==', 'AAAAAAA='):
    rejected = subprocess.run([core, '--migrate-v19-snapshot', payload], capture_output=True, text=True)
    assert rejected.returncode != 0 and not rejected.stdout
print(f'PASS frozen rules19 CLI: {len(cases)} complete state/trace goldens; migration, round trip, historical60 fold')

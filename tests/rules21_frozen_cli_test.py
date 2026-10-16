"""Exact replay of genuine pre-fix rules21 outputs plus lossless current migration."""
import base64
import json
from pathlib import Path
import subprocess
import sys

core=Path(sys.argv[1]).resolve()
fixture=Path(__file__).resolve().parent/'fixtures/rules21-service-migration.json'
golden=json.loads(fixture.read_text())
checks=0
def call(args,events=()):
    value=subprocess.run([str(core),*args],input=''.join(f"{e['type']} {e['value']}\n" for e in events),text=True,capture_output=True)
    assert value.returncode==0,(args[0],value.stderr)
    return json.loads(value.stdout)
for case in golden['cases']:
    events=case['events'];original=case['expected']
    replay=call(['--replay-v21-snapshot-trace',golden['snapshotBase64']],events)
    assert replay==original,('frozen replay drift',case['label']);checks+=1
    current=call(['--migrate-v21-snapshot',golden['snapshotBase64']],events)
    normalized={**current['state'],'schemaVersion':29,'rulesVersion':21}
    assert normalized==original['state'],('migration field drift',case['label']);checks+=1
    restored=call(['--replay-snapshot',current['snapshotBase64']])
    assert restored==current['state'],('current roundtrip drift',case['label']);checks+=1
    raw=base64.b64decode(current['snapshotBase64'],validate=True)
    assert len(raw)==6876 and int.from_bytes(raw[4:6],'little')==30 and int.from_bytes(raw[8:12],'little')==22
    checks+=1
for args in [['--roster'],['--starters'],['--evolutions','impmon'],['--evolution-graph','18','0'],['--catalog-page','0','1']]:
    result=call(args);assert result['rulesVersion']==22,args;checks+=1
print(f'{checks} checks; {len(golden["cases"])} exact frozen rules21 replay and migration cases; current catalog contract22')

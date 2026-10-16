"""Golden comparison against the unchanged pre-tier rules20 executor."""
import base64, json, subprocess, sys
from pathlib import Path
core=sys.argv[1]
fixture=json.loads((Path(__file__).parent/'fixtures/rules20-dungeon-golden.json').read_text())
def run(args, events=''):
    result=subprocess.run([core,*args],input=events,text=True,capture_output=True)
    assert result.returncode==0,result.stderr
    return json.loads(result.stdout)
def equal_old_fields(current,old):
    for key,value in old.items():
        if key in ('rulesVersion','schemaVersion'): continue
        if key=='expeditions':
            for nested,expected in value.items(): assert current[key][nested]==expected,(key,nested)
        else: assert current[key]==value,key
    expedition=current['expeditions']
    assert (expedition['active'],expedition['scenarioIndex'],expedition['tier'],expedition['tiers'],expedition['awaitingNext'])==(False,None,0,0,False)
for case in fixture['replays']:
    assert run(['--replay-v20-onboarding-trace',str(case['seed'])],case['events'])==case['expected']
    migrated=run(['--migrate-v20-onboarding',str(case['seed'])],case['events'])
    assert (migrated['state']['rulesVersion'],migrated['state']['schemaVersion'])==(22,30)
    equal_old_fields(migrated['state'],case['expected']['state'])
    assert run(['--replay-snapshot',migrated['snapshotBase64']])==migrated['state']
for case in fixture['saves']:
    frozen=run(['--replay-v20-snapshot-trace',case['snapshotBase64']])
    assert frozen=={'state':case['state'],'trace':None}
    migrated=run(['--migrate-v20-snapshot',case['snapshotBase64']])
    equal_old_fields(migrated['state'],case['state'])
    original=base64.b64decode(case['snapshotBase64']); encoded=base64.b64decode(migrated['snapshotBase64'])
    assert len(original)==6860 and len(encoded)==6876
    assert original[12:-4]==encoded[12:6856], 'all old stored progress must remain byte-identical'
    assert encoded[6856:-4]==bytes(16), 'old untagged encounters must not become a new dungeon run'
    if case['state']['phase']=='encounter':
        old=run(['--replay-v20-snapshot-trace',case['snapshotBase64']],'retreat\n')['state']
        new=run(['--replay-snapshot',migrated['snapshotBase64']],'retreat\n')
        equal_old_fields(new,old)
for payload in ('AA==','AAAA','AAAAAA==','AAAAAAA='):
    result=subprocess.run([core,'--migrate-v20-snapshot',payload],capture_output=True)
    assert result.returncode and not result.stdout
print('PASS frozen rules20: 4 exact replay/trace goldens, 2 genuine snapshots, zero new progress, retained old encounter, short-input rejection')

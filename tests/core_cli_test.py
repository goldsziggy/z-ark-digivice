"""Real native protocol/migration checks; no duplicated combat implementation."""
import base64
import json
import struct
import subprocess
import sys
import zlib
binary=sys.argv[1]
def accepted(events=b'',args=None):
 p=subprocess.run([binary,*(args or ['--replay','12345'])],input=events,capture_output=True)
 assert p.returncode==0,p.stderr
 assert p.stderr==b'' and len(p.stdout.splitlines())==1
 return json.loads(p.stdout)
def rejected(events=b'',args=None):
 p=subprocess.run([binary,*(args or ['--replay','12345'])],input=events,capture_output=True)
 assert p.returncode and p.stdout==b'' and p.stderr.strip(),(args,events,p.stdout)
def encoded(b):return base64.b64encode(b).decode()
def seal(b):struct.pack_into('<I',b,len(b)-4,zlib.crc32(b[:-4]))
def rules15_image(blob):
 # Schema23 adds a care word on every member and three care-time words before the CRC.
 assert len(blob)==3216
 image=bytearray(2964)
 image[:112]=blob[:112]
 for i in range(60):
  image[112+i*44:112+(i+1)*44]=blob[112+i*48:112+i*48+44]
 image[2752:2960]=blob[2992:3200]
 return image
def old_payload(blob):
 # Project schema21's inserted52 slots out when comparing source-era bytes.
 if len(blob)==3216: blob=rules15_image(blob)
 assert len(blob)==2964
 return blob[:464]+blob[2752:2948]+blob[-4:]
def current_offset(old_offset):return old_offset if old_offset<464 else old_offset+2288
def current_presentation(state):
 # User-facing wording is current; immutable historical fixture files stay exact.
 # The live level cap is 50. Frozen receipts keep the cap they were recorded with.
 # Live routes recompute level, bond, care and preview combat. Stored fixture gates stay historical.
 state.pop('maxLevel', None)
 assert state.pop('focus', None) is None # Rules 18 focus pauses never exist in historical fixtures.
 evolution=state.get('evolution')
 if isinstance(evolution, dict):
  for option in evolution.get('options') or []:
   for key in ('requiredLevel','requiredBond','requiredCare','previewLevel','combat','maxCareMistakes','careRouteOpen'): option.pop(key, None)
 # Rules 17 care-quality fields are zero on every migrated historical member.
 for member in state.get('collection') or []:
  assert member.pop('careMistakes', 0)==0 and member.pop('injury', 0)==0
 state['message']=state['message'].replace('Your friend is free to roam;', 'Your Digimon is free to roam;').replace('A new friend joined', 'A new Digimon joined')
def remove_new_pacing(state):
 assert state.pop('partyCapacity',3)==3 and state.pop('partyMemberIds',[])==[]
 assert state['collectionCapacity'] in (8,60)
 state['collectionCapacity']=8
 assert state.pop('receivedTrades',0)==0
 assert state.pop('autoCapture',0)==0
 assert state.pop('worldSeed',0)==0
 state.pop('foregroundSequence');state.pop('care');state.pop('lastCapture')
 state['onboarding'].pop('offerSeed');state['onboarding'].pop('offers')
 for member in state['collection']:member.pop('care')
 for key in ('careMinute','critical','captureDeferred','carePoints','toilet','careMissed'): state.pop(key,None)
 for member in state.get('collection') or []:
  for key in ('carePoints','toilet','careMissed'): member.pop(key,None)
 assert state.pop('walking')=={'rate':2,'name':'Normal','eligibleSteps':0,'encounters':0,'rngState':0,'target':0,'progress':0,'remainingSteps':0,'pendingEncounter':None}

initial=accepted()
assert (initial['schemaVersion'],initial['rulesVersion'],initial['creature'],initial['formId'],initial['level'],initial['xp'])==(26,19,'Mote',1,1,0)
assert initial['partyCapacity']==3 and initial['partyMemberIds']==[]
assert initial['maxLevel']==50 and initial['xpToNext']==40 and initial['collectionCapacity']==60
assert initial['worldSeed']==0
# World setup is explicit, one-time and background-only; neither the CLI nor an
# old replay epoch silently invents entropy or changes the historical game seed.
world_args=['--replay-onboarding','12345']
world_before=accepted(b'hatch 1\nencounter-seed 777\naccrue-steps 1000\n',world_args)
world_after=accepted(b'hatch 1\nencounter-seed 777\naccrue-steps 1000\nworld-seed 4294967295\n',world_args)
assert world_after['worldSeed']==4294967295 and world_after['sequence']==world_before['sequence']+1
world_after['worldSeed']=0;world_after['sequence']-=1
assert world_after==world_before
for bad in [b'world-seed\n',b'world-seed 0\n',b'world-seed -1\n',b'world-seed 4294967296\n',b'world-seed 1\nworld-seed 1\n']:
 rejected(b'hatch 1\n'+bad,world_args)
rejected(b'world-seed 1\n',world_args)
rejected(b'hatch 1\nworld-seed 1\n',['--replay-v12-onboarding-trace','12345'])
assert accepted(b'rest\n'*10000)['sequence']==10000
for events in [b'rest\n'*10001,b'\n',b'unknown\n',b'feed 1\n',b'walk\n',b'walk 0\n',b'walk 1001\n',b'card 3\n',b'walk -1\n',b'walk +1\n',b'walk 1.0\n',b'walk 4294967296\n',b'rest 0 extra\n',b'rest\0\n',b'x'*64,b'attack\n',b'walk 100\nfeed\n',b'walk 100\ncapture\n',b'walk 100\ncard 1\ncard 2\n',b'select\n',b'select 0\n',b'select 2\n',b'select 9\n',b'evolve\n',b'evolve 0\n',b'evolve 67\n',b'evolve 2\n']:
 rejected(events)
for seed in ['-1','+1','1.0','4294967296','']:rejected(args=['--replay',seed])
# Flick is an additive, explicit-valued input. Old parsers/history epochs cannot
# reinterpret it, and the native trajectory is inspectable without any save.
for value, x, y, hit in [(0,46,300,False),(82175,366,45,False),(41140,206,120,True),
                         (28852,158,120,True),(28596,157,120,False),
                         (41092,206,168,True),(41091,206,169,False)]:
 assert accepted(args=['--flick-trajectory',str(value)])=={'inputVersion':1,'landingX':x,'landingY':y,'hit':hit}
for value in ['-1','+1','1.0','82176','4294967295','4294967296','']:
 rejected(args=['--flick-trajectory',value])
for event in [b'flick\n',b'flick -1\n',b'flick 1.0\n',b'flick 82176\n',b'flick 41140\n',b'walk 100\nflick 41140\n']:
 rejected(event)
for version in range(1,10):
 rejected(b'flick 41140\n',args=[f'--migrate-v{version}','12345'])
starters=accepted(args=['--starters']);assert starters['rulesVersion']==19 and len(starters['starters'])==8
roster=accepted(args=['--roster']);assert roster['rulesVersion']==19 and len(roster['profiles'])==8
all_forms=set();egg_args=['--replay-onboarding','12345'];egg=accepted(args=egg_args)
assert egg['phase']=='egg' and egg['collection']==[] and egg['formId']==0 and egg['evolution']=={'options':[]}
for entry in starters['starters']:
 sid=entry['id'];hatch=f'hatch {sid}\n'.encode();s=accepted(hatch,egg_args)
 assert s['stage']=='Rookie' and s['formId']==11+7*(sid-1) and s['creature']==entry['name'] and s['combat']==entry['combat']
 assert s['collection'][0]['xp']==0 and s['sequence']==1 and s['rngState']==12345
 repeated_care=accepted(hatch+b'feed\nrest\n'*100,egg_args)
 assert repeated_care['xp']==4 and repeated_care['level']==1 and repeated_care['bond']<20
 rejected(hatch+hatch,args=egg_args);rejected(hatch)
 tree=accepted(args=['--evolutions',entry['species']])
 assert set(tree)=={'formatVersion','rulesVersion','species','forms'} and tree['rulesVersion']==19 and len(tree['forms'])==7
 nodes={n['formId']:n for n in tree['forms']};all_forms.update(nodes)
 root=nodes[s['formId']];assert root['parentId']==0 and root['children']==[s['formId']+1,s['formId']+4]
 for n in nodes.values():
  assert n['artId'] is None and n['previewLevel']==n['requiredLevel']
  for child in n['children']:assert nodes[child]['parentId']==n['formId']
 options=s['evolution']['options'];assert len(options)==2 and all(not o['eligible'] and o['requiredLevel']==18 and o['requiredBond']==64 and o['requiredCare']==40 for o in options)
 rejected(hatch+f'evolve {root["children"][0]}\n'.encode(),args=egg_args)
assert len(all_forms)==56
rejected(args=['--evolutions','unknown'])
for events in [b'hatch\n',b'hatch 0\n',b'hatch 9\n',b'feed\n',b'walk 100\n',b'evolve 12\n']:rejected(events,args=egg_args)
# Original frozen interpreters retain their shipped histories before conversion.
old_history=b'feed\nplay\nwalk 100\ncard 1\nattack\ncapture\n'
for mode in ['--migrate-v1','--migrate-v2']:
 m=accepted(old_history,[mode,'12345']);s=m['state']
 assert (s['hp'],s['sequence'],s['rngState'],s['bond'])==(97,6,1955480042,19)
 assert s['legacyCaptures']==(1 if mode.endswith('v1') else 0)
 assert accepted(args=['--replay-snapshot',m['snapshotBase64']])==s
 for bad in [b'hatch 1\n',b'heavy\n',b'magic\n',b'mode 1\n',b'evolve 2\n']:rejected(bad,args=[mode,'12345'])
history3=b'feed\nplay\nwalk 100\ncard 1\nattack\nattack\nattack\ncapture\n'
m=accepted(history3,['--migrate-v3','12345']);s=m['state'];blob=base64.b64decode(m['snapshotBase64'],validate=True)
assert (s['hp'],s['sequence'],s['rngState'],s['bond'])==(58,8,3336926330,19)
assert len(blob)==3216 and struct.unpack_from('<HHI',blob,4)==(26,3204,19)
assert struct.unpack_from('<I',blob,3212)[0]==zlib.crc32(blob[:3212])
assert accepted(args=['--replay-snapshot',m['snapshotBase64']])==s
# Build exact old layout from a known level-1 migrated state, stripping new fields.
image=rules15_image(blob)
old=bytearray(image[:112])
for i in range(8):old.extend(image[112+44*i:148+44*i])
old.extend(old_payload(blob)[464:488]);old.extend(bytes(4));struct.pack_into('<HHI',old,4,6,416,3);seal(old)
for version,length in [(3,404),(4,404),(5,412),(6,428)]:
 snap=bytearray(old[:length-4]+bytes(4));struct.pack_into('<HHI',snap,4,version,length-12,2 if version==3 else 3)
 if version==3:struct.pack_into('<I',snap,156,100)
 seal(snap)
 restored=accepted(args=['--replay-snapshot',encoded(snap)]);assert restored==s
 mode='--migrate-v2-snapshot' if version==3 else '--migrate-v3-snapshot'
 continued=accepted(b'select 2\nfeed\n',[mode,encoded(snap)])['state'];assert continued['activeCreatureId']==2 and continued['sequence']==10
rejected(args=['--migrate-v3-snapshot',m['snapshotBase64']])
# Conversion credit retains old trained stats; it is not a fresh battle reward.
for tier,feeds,level,xp in [(1,0,1,0),(2,20,5,400),(3,50,10,1800)]:
 trained=accepted(b'hatch 1\n'+b'feed\n'*feeds,['--migrate-v3-onboarding','12345'])['state']
 assert (trained['level'],trained['xp'],trained['creature'],trained['stage'])==(level,xp,'Impmon','Rookie')
 assert trained['hp']==[92,108,128][tier-1]
 for field in ('hp','energy','fullness','mood'):assert trained[field]==trained['collection'][0][field]
for bad in ['', '!',m['snapshotBase64'][:-1],m['snapshotBase64']+'!',encoded(blob[:-1]),encoded(blob+b'\0')]:rejected(args=['--replay-snapshot',bad])
for offset in [0,120,148,152,464,492,496]:
 bad=bytearray(blob);bad[offset]^=0x40;rejected(args=['--replay-snapshot',encoded(bad)])
for offset,value in [(4,27),(8,99),(104,61),(148,7601),(152,67),(464,2),(468,1),(472,2),(476,6),(488,1),(492,1)]:
 bad=bytearray(blob);struct.pack_into('<H' if offset==4 else '<I',bad,current_offset(offset),value);seal(bad);rejected(args=['--replay-snapshot',encoded(bad)])
# Full Auto is one event; replay returns the identical persisted result/trace.
auto=b'hatch 1\nmode 1\nwalk 100\nauto\n';args=['--replay-onboarding-trace','12345'];result=accepted(auto,args)
assert result==accepted(auto,args);state,trace=result['state'],result['trace']
# Seed 12345 walks one level above the partner. Capture reward stays 20 + 6 * wild level.
assert state['sequence']==4 and state['xp']==32 and state['collection'][1]['capturedAtSequence']==4
assert trace['enemy']['level']==2
assert 1<=len(trace['steps'])<=48 and trace['startSequence']==3 and trace['endSequence']==4 and trace['outcome']=='captured'
assert trace['player']['name']=='Impmon' and trace['player']['formId']==11 and trace['enemy']['formId']>=11
assert all(not step['reflected'] and step['guard'] in {'brace','ward','counter'} for step in trace['steps'])
assert 'rngState' not in json.dumps(trace) and 'enemyChoice' not in json.dumps(trace)
assert accepted(auto+b'rest\nmode 0\n',args)['trace']==trace
for bad in [b'mode\n',b'mode 2\n',b'auto\n',b'walk 100\nauto\n',b'walk 100\nmode 1\n',b'mode 1\nwalk 100\nattack\n',b'mode 1\nwalk 100\ncard 1\n',b'mode 1\nwalk 100\ncapture\n',b'mode 1\nwalk 100\nauto 1\n',b'mode 1\nwalk 100\nauto\nauto\n']:rejected(bad)
budget=accepted(args=['--budget']);assert budget['stateBytes']==3188 and budget['snapshotBytes']==3216 and budget['jsonBufferBytes']==65536 and budget['coreHeapAllocations']==0
print('RPG CLI: all8 trees/hatches, native previews, bounded XP/care, exact rules1–3 migration, snapshots, trace replay and hostile input passed')

# Exact prior rules4 Auto history remains frozen before migration.
old4=accepted(auto,['--migrate-v4-onboarding','12345'])
assert old4['state']['hp']==52 and old4['state']['sequence']==4 and old4['state']['xp']==26
assert old4['state']['nextMemberId']==3 and old4['state']['journal']['obtainedFormIds']==[4,11]
assert accepted(args=['--replay-snapshot',old4['snapshotBase64']])==old4['state']
rejected(b'release 2\n',args=['--migrate-v4','12345'])
for bad in [b'release\n',b'release 0\n',b'release 1\n',b'release 4294967295\n']:rejected(bad)
# Catalog completeness and direct high-ID lookup; no full-table response.
ids=[];offset=0
while True:
 page=accepted(args=['--catalog-page',str(offset),'16'])
 ids.extend(row['formId'] for row in page['forms'])
 if page['nextOffset'] is None:break
 offset=page['nextOffset']
RETIRED={279,286,287,288} # retired duplicates keep their IDs but are never published
assert ids==[i for i in range(11,466) if i not in RETIRED]
assert accepted(args=['--form','276'])['formId']==276
for argv in [['--catalog-page','0','17'],['--catalog-page','-1'],['--form','513'],*[['--form',str(i)] for i in sorted(RETIRED)]]:rejected(args=argv)
print('Full roster CLI: frozen rules4 migration, stable IDs, catalog paging and new guard traces passed')
# Exact rules5 replay and snapshot migration from committed, independent goldens.
from pathlib import Path
frozen=json.loads((Path(__file__).parent/'fixtures/rules5-324b774.json').read_text())
for fixture in frozen['fixtures']:
 events=''.join(f"{e['type']} {e['value']}\n" for e in fixture['events']).encode()
 mode='--migrate-v5-onboarding' if fixture['initialMode']=='onboarding' else '--migrate-v5'
 migrated=accepted(events,[mode,str(fixture['seed'])]);decoded=accepted(args=['--migrate-v5-snapshot',fixture['snapshotBase64']])
 assert migrated==decoded,fixture['name']
 expected=json.loads(json.dumps(fixture['state']));actual=json.loads(json.dumps(migrated['state']))
 for key in ['schemaVersion','rulesVersion','evolution','maxLevel']:expected.pop(key);actual.pop(key)
 actual.pop('wildCaptureChance');remove_new_pacing(actual)
 for key in ('wildRarity','recoveryRestCount','queuedEncounters','stepsToNextEncounter'):actual.pop(key)
 if actual['phase']=='home':
  for state in (actual,expected):
   if state['combat']:state['combat'].pop('skills')
   for member in state['collection']:member['combat'].pop('skills')
 current_presentation(actual);current_presentation(expected)
 assert actual==expected,fixture['name']
 oldbytes=base64.b64decode(fixture['snapshotBase64']);newbytes=base64.b64decode(migrated['snapshotBase64'])
 assert len(newbytes)==3216 and struct.unpack_from('<HHI',newbytes,4)==(26,3204,19)
 assert oldbytes[12:-4]==old_payload(newbytes)[12:572],fixture['name']
 assert accepted(args=['--replay-snapshot',fixture['snapshotBase64']])==migrated['state']
 assert accepted(args=['--replay-snapshot',migrated['snapshotBase64']])==migrated['state']
 rejected(args=['--migrate-v5-snapshot',migrated['snapshotBase64']])
# Graph pages are native, bounded, complete per component and independent of lineage.
graph_ids=[];offset=0
while True:
 page=accepted(args=['--evolution-graph','18',str(offset),'16'])
 assert page['formatVersion']==2 and page['rulesVersion']==19 and page['focusFormId']==18
 assert 1<=len(page['forms'])<=16 and page['total']<=512
 for node in page['forms']:
  assert len(node['children'])<=2 and len(node['edges'])==len(node['children'])
  assert [e['toFormId'] for e in node['edges']]==node['children']
 graph_ids.extend(n['formId'] for n in page['forms'])
 if page['nextOffset'] is None:break
 assert page['nextOffset']>offset;offset=page['nextOffset']
assert len(set(graph_ids))==len(graph_ids)==page['total'] and 18 in graph_ids
for argv in [['--evolution-graph','0','0'],['--evolution-graph','18','0','17'],['--evolution-graph','18','999']]:rejected(args=argv)
print('Rules6 CLI: 11 exact frozen5 histories/snapshots, unchanged progress bytes, and bounded paged graph passed')
# Independently captured rules6 source-era snapshots/history must survive graph7.
frozen6=json.loads((Path(__file__).parent/'fixtures/digigame-frozen-rules6.json').read_text())
def event_bytes(events):return ''.join(f"{e['type']} {e['value']}\n" for e in events).encode()
def prior_equal(actual,expected):
 actual=json.loads(json.dumps(actual));expected=json.loads(json.dumps(expected))
 for key in ('schemaVersion','rulesVersion','evolution','maxLevel'):actual.pop(key);expected.pop(key)
 actual.pop('wildCaptureChance');remove_new_pacing(actual)
 for key in ('wildRarity','recoveryRestCount','queuedEncounters','stepsToNextEncounter'):actual.pop(key)
 if actual['phase']=='home':
  for state in (actual,expected):
   if state['combat']:state['combat'].pop('skills',None)
   for member in state['collection']:member['combat'].pop('skills',None)
 current_presentation(actual);current_presentation(expected)
 assert actual==expected
baseline=frozen6['rules6Baseline']
converted=accepted(args=['--migrate-v6-snapshot',baseline['snapshotBase64']])
prior_equal(converted['state'],baseline['state'])
assert old_payload(base64.b64decode(converted['snapshotBase64']))[12:572]==base64.b64decode(baseline['snapshotBase64'])[12:-4]
prior_equal(accepted(event_bytes(frozen6['suffixEvents']),['--migrate-v6-snapshot',baseline['snapshotBase64']])['state'],frozen6['suffixResult']['state'])
for events,key in [(frozen6['encounterEvents'],'encounterResult'),(frozen6['prefixEvents'],'autoResult'),([], 'egg')]:
 restored=accepted(event_bytes(events),['--migrate-v6-onboarding','12345'])
 prior_equal(restored['state'],frozen6[key]['state'])
 assert (restored['state']['schemaVersion'],restored['state']['rulesVersion'])==(26,19)
 assert accepted(args=['--replay-snapshot',restored['snapshotBase64']])==restored['state']
 if key=='encounterResult':
  assert restored['state']['wildRules']==6
  rejected(b'auto\n',['--replay-snapshot-trace',restored['snapshotBase64']])
  old12=bytearray(old_payload(base64.b64decode(restored['snapshotBase64']))[:648])+bytearray(4);struct.pack_into('<HH',old12,4,16,640);struct.pack_into('<I',old12,8,12);seal(old12)
  continued=accepted(b'auto\n',['--replay-v12-snapshot-trace',encoded(old12)])
  prior_equal(continued['state'],frozen6['autoResult']['state'])
  assert continued['trace']==frozen6['autoResult']['trace']
prior_equal(accepted(args=['--migrate-v6','12345'])['state'],frozen6['legacyZeroEvent']['state'])
rejected(args=['--migrate-v6-snapshot',converted['snapshotBase64']])
print('Rules7 CLI: independent frozen6 baseline, event suffix, Egg, active encounter and identical Auto trace passed')

# Source-era rules7 active battles keep their old resolver, capture policy and labels.
frozen7=json.loads((Path(__file__).parent/'fixtures/care-v7.json').read_text())
for fixture in frozen7['fixtures']:
 initial=accepted(args=['--replay-snapshot',fixture['snapshotBase64']])
 prior_equal(initial,fixture['initialState'])
 assert initial['wildRules']==7
 migrated=accepted(args=['--migrate-v7-snapshot',fixture['snapshotBase64']])
 assert migrated['state']==initial
 assert old_payload(base64.b64decode(migrated['snapshotBase64']))[12:572]==base64.b64decode(fixture['snapshotBase64'])[12:-4]
 continued=accepted(fixture['continuation'].encode(),['--replay-v12-snapshot-trace',fixture['snapshotBase64']])
 # A completed encounter rejoins the current home presentation; trace labels stay frozen.
 actual=json.loads(json.dumps(continued['state']));expected=json.loads(json.dumps(fixture['result']['state']))
 if actual['phase']=='home':
  for state in (actual,expected):
   state['combat'].pop('skills')
   for member in state['collection']:member['combat'].pop('skills')
 prior_equal(actual,expected)
 assert continued['trace']==fixture['result']['trace']
 rejected(args=['--migrate-v7-snapshot',migrated['snapshotBase64']])
print('Rules8 CLI:16 independent frozen7 active Tactical/Auto fixtures, frozen trace labels, unchanged payload and old-epoch rejection passed')

# Frozen8 host tracing preserves original receipt displays; current migration
# changes only the two approved HP scales at Home, never in an active encounter.
frozen8=json.loads((Path(__file__).parent/'fixtures/auto-tuning-baseline16-service.json').read_text())
events=[]
for command in frozen8['care']['commands']:
 events.extend(command['body']['events'])
 replayed=accepted(event_bytes(events),['--replay-v8-onboarding-trace','12345'])
 expected=command['response']['body']
 assert replayed['state']==expected['state'] and replayed['trace']==expected['autoTrace']
assert accepted(args=['--replay-v8-trace','12345'])['state']['rulesVersion']==8
assert accepted(args=['--replay-v8-trace','12345'])['trace'] is None
rejected(args=['--replay-v8-snapshot-trace',m['snapshotBase64']])
profiles=json.loads((Path(__file__).parent/'fixtures/care-v8-profile-migration.json').read_text())
for fixture in profiles['fixtures']:
 snapshot=fixture['snapshotBase64'];prior=accepted(args=['--replay-v8-snapshot-trace',snapshot])
 assert prior=={'state':fixture['state'],'trace':None}
 migrated=accepted(args=['--migrate-v8-snapshot',snapshot]);expected=accepted(args=['--replay-snapshot',fixture['migrated']['snapshotBase64']]);assert migrated['state']==expected
 assert accepted(args=['--replay-snapshot',snapshot])==migrated['state']
 assert accepted(args=['--replay-snapshot',migrated['snapshotBase64']])==migrated['state']
 if 'continuation' in fixture:
  old_terminal=accepted(fixture['continuation'].encode(),['--migrate-v8-snapshot',snapshot])
  rejected(fixture['continuation'].encode(),['--replay-snapshot',migrated['snapshotBase64']])
  repaired=accepted(b'resolve-test-encounter 0\n',['--replay-snapshot',migrated['snapshotBase64']])
  assert repaired['phase']=='home' and repaired['sequence']==migrated['state']['sequence']+1
  assert old_terminal['state']==accepted(args=['--replay-snapshot',fixture['terminal']['snapshotBase64']])
 rejected(args=['--migrate-v8-snapshot',migrated['snapshotBase64']])
print('Rules9 CLI: exact frozen8 receipt state/trace, Home HP conversion, active old8 continuation, epoch rejection and idempotent restore passed')

# Park uses stateless authored rarity, exact frozen9 replay, and ordinary Rest batches.
park9=json.loads((Path(__file__).parent/'fixtures/park-baseline-service.json').read_text())
events=[]
for command in park9['care']['commands']:
 events.extend(command['body']['events'])
 replayed=accepted(event_bytes(events),['--replay-v9-onboarding-trace','12345'])
 expected=command['response']['body'];assert replayed['state']==expected['state'] and replayed['trace']==expected['autoTrace']
 migrated=accepted(event_bytes(events),['--migrate-v9-onboarding','12345'])
 restored=accepted(args=['--replay-snapshot',migrated['snapshotBase64']]);assert restored==migrated['state']
 remove_new_pacing(restored)
 for key in ('schemaVersion','rulesVersion','wildRarity','recoveryRestCount','queuedEncounters','stepsToNextEncounter'):restored.pop(key)
 old=dict(expected['state']);old.pop('schemaVersion');old.pop('rulesVersion');current_presentation(restored);current_presentation(old);assert restored==old
 assert accepted(args=['--replay-v9-trace','12345'])['state']['rulesVersion']==9
 rejected(args=['--replay-v9-snapshot-trace',migrated['snapshotBase64']])
park=accepted(b'hatch 1\nmode 1\nwalk 100\nwalk 1000\n',egg_args)
assert park['wildRarity'] in {'common','uncommon','rare'} and park['queuedEncounters']==10 and park['stepsToNextEncounter']==0 and park['recoveryRestCount']==0
park_history=b'hatch 1\nmode 1\nwalk 100\nauto\n';park=accepted(park_history,egg_args);count=park['recoveryRestCount'];assert 1<=count<=40
recovered=accepted(park_history+b'rest\n'*count,egg_args)
assert recovered['recoveryRestCount']==0 and recovered['hp']==recovered['combat']['maxHp'] and recovered['energy']==100
assert recovered['sequence']==park['sequence']+count and recovered['rngState']==park['rngState'] and recovered['xp']==park['xp']+2
for form in (11,71,86,276):
 detail=accepted(args=['--form',str(form)]);assert detail['encounterRarity'] in {'common','uncommon','rare'}
print('Park CLI: frozen9 original receipts/traces, identity migration, native queue/rarity/recovery and bounded ordinary Rest batch passed')

# Deferred walking action names and values are explicit CLI contracts.
for event in (b'accrue-steps\n', b'accrue-steps 0\n', b'accrue-steps 1001\n', b'present-encounter 1\n', b'present-encounter\n'):
 rejected(b'hatch 1\n'+event,args=['--replay-onboarding','12345'])
waiting=accepted(b'hatch 1\naccrue-steps 1000\n',args=['--replay-onboarding','12345'])
assert waiting['phase']=='home' and waiting['walking']['pendingEncounter'] and waiting['foregroundSequence']==1 and waiting['sequence']==2
shown=accepted(b'hatch 1\naccrue-steps 1000\npresent-encounter\n',args=['--replay-onboarding','12345'])
assert shown['phase']=='encounter' and shown['wildFormId']==waiting['walking']['pendingEncounter']['formId'] and shown['foregroundSequence']==3
print('Deferred CLI: bounded explicit step input, durable single pending result and separate Home presentation passed')

# XP companions are additive current inputs; frozen14 receipts remain their own
# exact epoch and migrate with an empty selection rather than invented members.
for event in (b'party-add\n', b'party-add 0\n', b'party-add -1\n', b'party-add 4294967296\n', b'party-remove\n', b'party-remove 0\n'):
 rejected(b'hatch 1\n'+event,args=['--replay-onboarding','31'])
for event in (b'party-add 2\n', b'party-remove 2\n'):
 rejected(b'hatch 1\n'+event,args=['--replay-v14-onboarding-trace','31'])
prior_history=b'hatch 1\nfeed\nplay\nrest\nworld-seed 991\n'
prior=accepted(prior_history,['--replay-v14-onboarding-trace','31'])
assert prior['state']['schemaVersion']==21 and prior['state']['rulesVersion']==14
assert 'partyMemberIds' not in prior['state'] and 'partyCapacity' not in prior['state']
migrated=accepted(prior_history,['--migrate-v14-onboarding','31'])
current=json.loads(json.dumps(migrated['state']));frozen=json.loads(json.dumps(prior['state']))
assert current.pop('partyMemberIds')==[] and current.pop('partyCapacity')==3
for state in (current,frozen):state.pop('schemaVersion');state.pop('rulesVersion')
for key in ('careMinute','critical','captureDeferred','carePoints','toilet','careMissed'): current.pop(key,None)
for member in current['collection']:
 for key in ('carePoints','toilet','careMissed'): member.pop(key,None)
current_presentation(current);current_presentation(frozen)
assert current==frozen
assert accepted(args=['--replay-snapshot',migrated['snapshotBase64']])==migrated['state']
rejected(args=['--replay-v14-snapshot-trace',migrated['snapshotBase64']])
print('XP companion CLI: explicit inputs, exact frozen14 presentation, empty migration and old-epoch rejection passed')
# Rules 19: rules-18 histories replay in the frozen executor (276-form roster)
# and migrate once into schema 26; later events use the full roster.
events18=b'mode 1\nwalk 100\nauto-fight 0\n'
frozen18=accepted(events18,['--replay-v18-trace','4242'])
assert (frozen18['state']['schemaVersion'],frozen18['state']['rulesVersion'])==(25,18)
assert frozen18['state']['wildFormId']==0 or frozen18['state']['wildFormId']<=276
migrated18=accepted(events18,['--migrate-v18','4242'])
assert (migrated18['state']['schemaVersion'],migrated18['state']['rulesVersion'])==(26,19)
assert migrated18['state']['sequence']==frozen18['state']['sequence']
blob18=base64.b64decode(migrated18['snapshotBase64']);assert len(blob18)==3216 and struct.unpack_from('<HHI',blob18,4)==(26,3204,19)
assert accepted(args=['--replay-snapshot',migrated18['snapshotBase64']])==migrated18['state']
rejected(b'focus 100\n',['--replay-v17-trace','4242'])  # no focus before rules 18
print('Rules19 CLI: frozen rules18 replay, single migration to schema26 and exact restore passed')

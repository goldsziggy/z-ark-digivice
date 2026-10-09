#!/usr/bin/env python3
"""Frozen native park-session measurement, never a physical walking claim."""
import argparse,collections,hashlib,json,math,subprocess,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/park-session'
PIN='e6f352300ee6c049b7e21598009db4cf52cca0a6'
SOURCES=['combat.cpp','forms.cpp','game.cpp','practice_battle.cpp','battle_trace.cpp','legacy_combat_v3.cpp','legacy_forms_v5.cpp','legacy_combat_v5.cpp','legacy_forms_v6.cpp','legacy_combat_v6.cpp','legacy_forms_v7.cpp','legacy_combat_v7.cpp','legacy_forms_v8.cpp','legacy_combat_v8.cpp']
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def distribution(values):
 values=sorted(values)
 return {'min':values[0],'p50':values[math.ceil(len(values)*.5)-1],'p95':values[math.ceil(len(values)*.95)-1],'max':values[-1],'mean':sum(values)/len(values)}
def rarity_probe(directory,source,sources):
 harness=directory/'park_rarity_probe.cpp';harness.write_bytes((ROOT/'tests/park_rarity_probe.cpp').read_bytes())
 command=['c++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(source),str(harness),*[str(source/n) for n in sources],'-o',str(directory/'rarity-probe')]
 subprocess.run(command,cwd=ROOT,check=True)
 raw=directory/'rarity-probe.jsonl';raw.write_text(subprocess.check_output([directory/'rarity-probe'],text=True))
 rows=[json.loads(line) for line in raw.read_text().splitlines()]
 report={'formatVersion':1,'status':'PASS','rulesVersion':10,'catalogVersion':6,'draws':65536,'seedsPerGate':64,'encountersPerSeed':256,'seedFormula':'uint32(0x9e3779b9*(index+1)) XOR 0x5041524b','firstFlickerChecks':256,'sameInputRepeatChecks':65536,'eligibilityChecks':65536,'bucketTarget':[70,25,5],'rows':rows,'note':'Deterministic bounded design sample, not cryptographic randomness or physical encounter forecasting. All forced first Flickers are excluded from frequency denominators.','evidence':{'command':command,'harnessSha256':sha(harness),'rawSha256':sha(raw),'rarityMetadataSha256':sha(ROOT/'data/encounter-rarity.json'),'sourceHashes':{n:sha(source/n) for n in sources+['encounters.hpp','encounter_rarity_generated.inc']}}}
 (ROOT/'docs/evidence/park-rarity-probe.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS native rarity probe: 65,536 selections')
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--reuse',action='store_true');parser.add_argument('--rarity-probe',action='store_true');parser.add_argument('--variant',choices=('baseline','manual-full','recover-full','weighted-recover'),default='baseline');args=parser.parse_args()
 directory=BUILD/args.variant;directory.mkdir(parents=True,exist_ok=True)
 recovery={'baseline':0,'manual-full':1,'recover-full':2,'weighted-recover':2}[args.variant]
 source=directory/'core'
 if not args.reuse:
  if source.exists():shutil.rmtree(source)
  source.mkdir(parents=True)
  if args.variant=='weighted-recover':
   for path in (ROOT/'core').iterdir():
    if path.is_file():shutil.copyfile(path,source/path.name)
  else:
   for name in subprocess.check_output(['git','ls-tree','--name-only',PIN+':core'],cwd=ROOT,text=True).splitlines():
    (source/name).write_bytes(subprocess.check_output(['git','show',PIN+':core/'+name],cwd=ROOT))
 cases=[]
 for starter in range(1,9):
  for seedIndex in range(4):
   seed=int.from_bytes(hashlib.sha256(f'park-session-v1:{starter}:{seedIndex}'.encode()).digest()[:4],'big') or 1
   for minutes in (20,25,30):
    for rate in (80,100,120):
     for automatic in (0,1):
      for release in (0,1):
       for pace in ((50,100,200) if minutes==25 and rate==100 else (100,)):
        cases.append([len(cases)+1,starter,seed,seedIndex,minutes,rate,automatic,release,pace])
 casesPath=BUILD/'cases.txt';caseText=''.join(' '.join(map(str,row))+'\n' for row in cases)
 if casesPath.exists():assert casesPath.read_text()==caseText,'case strata changed'
 else:casesPath.write_text(caseText)
 harness=directory/'park_session_audit.cpp'
 if not args.reuse:harness.write_bytes((ROOT/'tests/park_session_audit.cpp').read_bytes())
 sources=SOURCES+(['encounters.cpp','legacy_forms_v9.cpp','legacy_combat_v9.cpp'] if args.variant=='weighted-recover' else [])
 command=['c++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(source),'-DPARK_RECOVERY_MODE='+str(recovery),*(['-DPARK_CURRENT_RARITY'] if args.variant=='weighted-recover' else []),str(harness),*[str(source/p) for p in sources],'-o',str(directory/'audit')]
 raw=directory/'sessions.jsonl'
 if not args.reuse:
  subprocess.run(command,cwd=ROOT,check=True)
  with casesPath.open() as inp,raw.open('w') as out:subprocess.run([directory/'audit'],stdin=inp,stdout=out,cwd=ROOT,check=True)
 rows=[json.loads(line) for line in raw.open()];assert len(rows)==len(cases)
 for expected,actual in zip(cases,rows):
  assert expected[:6]==[actual[k] for k in ('case','starter','seed','seedIndex','minutes','stepsPerMinute')]
 groups=collections.defaultdict(list)
 for row in rows:
  groups[row['minutes'],row['stepsPerMinute'],row['mode'],row['rosterPolicy'],row['interactionPacePercent']].append(row)
 metrics=('level','xp','encounters','captures','wins','retreats','failedCaptureAttempts','distinctSeen','distinctCaptured','collectionCount','fullEncounters','releases','evolutions','rests','commandActions','battleTurns','steps','walkMs','interactionMs','autoplayMs','unspentMs','recoveryConfirmations','recoveryVisits','nativeGameEvents')
 report={'formatVersion':1,'baselineCommit':PIN,'variant':args.variant,'recoveryMode':recovery,'careRules':int(__import__('re').search(r'kRulesVersion = (\d+)',(source/'game.hpp').read_text()).group(1)),'schema':int(__import__('re').search(r'kSchemaVersion = (\d+)',(source/'game.hpp').read_text()).group(1)),'practiceRules':7,'sessions':len(rows),'rarityPresent':args.variant=='weighted-recover',
  'scope':'Deterministic host simulation with authored time assumptions, not measured human input, walking accuracy, FPS or hardware behavior.',
  'policies':'Fresh hatched starter, choose mode before timed session, actual native Walk and battles, capture first legal, keep active founder, first eligible evolution. Explicit alternative release-oldest-nonactive retains journal.',
  'recoveryGoal':{'hp':'full','minimumEnergy':80 if recovery==0 else 100,'oneConfirmation':recovery==2},
  'timeAssumptionsMs':{'notice':3000,'tacticalCommand':4000,'captureAdditionalWindup':1200,'autoConfirm':4000,'autoTraceRow':650,'result':2000,'careNavigation':4000,'rest':2000,'release':10000,'evolve':10000},
  'timingNotes':['Walking stops during all interactions and animations. Onboarding/mode choice occur before timer.','Pace50/100/200 percent scales assumed input/decision time; capture windup and native Auto trace animation stayfixed.','Command actions exclude individual Next taps, represented in assumed per-command time.','Auto commits atomically on confirmation; if trace exceeds session end, durable outcome counts and presentationPending is true. Tactical commands onlycommit if theirtimefits.','Unused final milliseconds remain idle. No network/phone/GPS or device sleep model is invented.'],
  'summaries':[dict(zip(('minutes','stepsPerMinute','mode','rosterPolicy','interactionPacePercent'),key))|{'n':len(group),'metrics':{k:distribution([r[k] for r in group]) for k in metrics},'fullRosterSessions':sum(r['fullAtMs']>=0 for r in group),'endsInEncounter':sum(r['endsInEncounter'] for r in group),'presentationPending':sum(r['presentationPending'] for r in group)} for key,group in sorted(groups.items())],
  'rows':[{k:v for k,v in row.items() if k!='events'} for row in rows],'eventRecords':'Exact per-encounter records remain in hash-bound raw JSONL under ignored build/park-session/<variant>/sessions.jsonl.','evidence':{'rawSha256':sha(raw),'caseSha256':sha(casesPath),'harnessSha256':sha(harness),'runnerSha256':sha(Path(__file__)),'buildCommand':command,'sourceHashes':{p.name:sha(p) for p in sorted(source.iterdir()) if p.is_file()}}}
 out=ROOT/'docs/evidence'/('park-session-'+args.variant+'.json');out.write_text(json.dumps(report,indent=2)+'\n')
 if args.rarity_probe:
  assert args.variant=='weighted-recover'
  rarity_probe(directory,source,sources)
 print('PASS',len(rows),'native sessions',out)
 for s in report['summaries']:
  if s['stepsPerMinute']==100 and s['interactionPacePercent']==100:
   print({k:v for k,v in s.items() if k!='metrics'}, {k:s['metrics'][k] for k in ('level','encounters','captures','distinctCaptured','rests','walkMs','interactionMs')})
if __name__=='__main__':main()

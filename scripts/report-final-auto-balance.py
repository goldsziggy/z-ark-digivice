#!/usr/bin/env python3
"""Summarize the frozen single-pass candidates and verify selected production."""
from collections import Counter, defaultdict
import hashlib
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/final-balance-pass'
OUT=ROOT/'docs/evidence/final-auto-balance.json'
NAMES=('baseline','stats-only','auto-only','combined','current')

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def raw(name):return BUILD/'baseline-battles.jsonl' if name=='baseline' else BUILD/name/'results.jsonl'
def nearest(values,q):return sorted(values)[max(0,__import__('math').ceil(q*len(values))-1)]

def run():
 forms={r['formId']:r for r in json.loads((BUILD/'baseline/data/world-ds-runtime.json').read_text())['forms']}
 summaries={n:json.loads((BUILD/n/'summary.json').read_text()) for n in NAMES}
 assert raw('current').read_bytes()==raw('combined').read_bytes()
 assert (BUILD/'current/profiles.jsonl').read_bytes()==(BUILD/'combined/profiles.jsonl').read_bytes()
 assert (BUILD/'current/stress.jsonl').read_bytes()==(BUILD/'combined/stress.jsonl').read_bytes()
 current_profiles={}
 for line in (BUILD/'current/profiles.jsonl').open():
  r=json.loads(line);current_profiles[r['form'],r['level']]=r['stats']
 live={r['formId']:r for r in json.loads((ROOT/'data/world-ds-runtime.json').read_text())['forms']}
 old={}
 keys=('maxHp','attack','defense','magic','resistance')
 for f in forms.values():
  for row in f['statsByLevel']:old[f['formId'],row['level']]=[row[k] for k in keys]
 expected={(f['formId'],row['level']):[row[k] for k in keys] for f in live.values() for row in f['statsByLevel']}
 assert expected==current_profiles
 changed=[{'formId':k[0],'level':k[1],'before':v,'after':current_profiles[k]} for k,v in old.items() if v!=current_profiles[k]]
 assert len(changed)==22 and {r['formId'] for r in changed}=={3,7}
 result={'formatVersion':1,'status':'PASS','scope':'Deterministic native host simulation; no hardware, human enjoyment, live encounter-rate or complete-balance claim.',
  'baselineCommit':'16feeddf58f7637b51564c83dd24e1e6e9a2ba23','selectedCareRules':9,'selectedPracticeRules':7,
  'decision':'Accept one family-consistent two-anchor correction and one public-only expected immediate HP-swing Auto policy. No additional parameter iteration.',
  'sampling':{'casesPerPolicy':23392,'matchups':5848,'seedsPerMatchup':4,'actors':276,'opponentForms':152,'actorLevelPoints':576,
   'nativeHarnessSha256':sha(BUILD/'current/audit.cpp'),
   'caseSha256':sha(BUILD/'baseline-cases.txt'),'seedDerivation':'first4 bytes big-endian SHA256(world-ds-balance-v1:playerForm:playerLevel:enemyForm:enemyLevel:seedIndex), zero becomes1',
   'design':'Entry and20 for every form; original eight Rookie starters also5/10/15; same-tier five-role representatives, rotating types, legal -2/0/+2 offsets. Same cases and seeds for every candidate/policy.',
   'wildFightsPerVariant':187136,'practiceFightsPerVariant':46784,'damageRowsPerVariant':5848},
  'profileChecks':{'nativeRuntimeExact':True,'legalLevelRows':3641,'changedRows':changed,'unchangedForms':274,
   'currentRuntimeSha256':sha(ROOT/'data/world-ds-runtime.json'),
   'outlierEvidence':'docs/evidence/original-outlier-profile-audit.json','growthUnchanged':[4,1,1,2,2],
   'weightedBudget':'HP/4 + Attack + Defense + Magic + Resistance; diagnostic only, not a combat formula.',
   'candidateBudgets':{'Lumen':[209,244,279],'Pelagia':[214,249,284]},'levels':[10,15,20]},
  'productionMatchesAcceptedCandidate':{'all239768RowsByteIdentical':True,'all3641ProfileRowsByteIdentical':True,'stressRowsByteIdentical':True,
   'rawSha256':sha(raw('current')),'profilesSha256':sha(BUILD/'current/profiles.jsonl'),'stressSha256':sha(BUILD/'current/stress.jsonl')},
  'policy':{'candidateHelperSha256':sha(ROOT/'tests/fixtures/auto-policy-candidate.hpp'),
   'productionHelperSha256':sha(ROOT/'core/practice_auto_policy.hpp'),
   'inputs':['phase','rulesVersion','public player/enemy form and level','public current HP'],
   'excluded':['committed enemy choice','opponent RNG','two-option hint','future events','cards'],
   'objective':'Sum clipped immediate enemy HP loss minus own HP loss across all three opponent moves; choose maximum; own deterministic RNG only for tied maxima.',
   'maxResolverCallsPerTurn':9,'limitations':'Shallow scorer; uniform weighting over three moves. It favors Counter heavily and never selects Brace in the main sample. Optional public hints/cards remain Tactical advantages.'},
  'reproduce':['python3 scripts/investigate-final-auto-balance.py baseline --stress','python3 scripts/investigate-final-auto-balance.py stats-only --stress','python3 scripts/investigate-final-auto-balance.py auto-only --stress','python3 scripts/investigate-final-auto-balance.py combined --stress','python3 scripts/investigate-final-auto-balance.py current --stress','python3 scripts/report-final-auto-balance.py'],
  'variants':{}}
 assert result['policy']['candidateHelperSha256']==result['policy']['productionHelperSha256']
 for name,s in summaries.items():
  axes=[r for r in s['groups'] if r['axis'] in ('all','equalLevel','equalTierRole','type','enemyType','offset') or r['axis']=='formLevel' and r['stratum'] in ('3:10','3:20','7:10','7:20')]
  v={'prospectiveMeasurementOnly':name not in ('baseline','current'),'rawSha256':sha(raw(name)),
    'rows':s['rows'],'buildCommand':s['buildCommand'],'sourceHashes':s['sourceHashes'],
    'groups':axes,'pairedBaseline':s.get('pairedBaseline',[]),'perFormDistribution':{},'typeRelationship':[], 'stress':{}}
  for policy in ('auto','public-hint'):
   rows=[r for r in s['groups'] if r['kind']=='practice' and r['policy']==policy and r['axis']=='formLevel']
   wins=[r['rates'].get('won',0) for r in rows]
   v['perFormDistribution'][policy]={'points':len(rows),'winRateP10':nearest(wins,.1),'winRateMedian':nearest(wins,.5),'winRateP90':nearest(wins,.9),'zeroWinPoints':sum(x==0 for x in wins),
    'worst':[{'formId':int(r['stratum'].split(':')[0]),'name':forms[int(r['stratum'].split(':')[0])]['name'],'level':int(r['stratum'].split(':')[1]),'n':r['n'],'outcomes':r['outcomes']} for r in sorted(rows,key=lambda r:(r['rates'].get('won',0),-r['rates'].get('draw',0),r['stratum']))[:8]]}
  relation=defaultdict(Counter); damagePrefs=Counter();counter=Counter()
  for line in raw(name).open():
   r=json.loads(line)
   if r['kind']=='damage':
    p,h,m=r['hits'][0];damagePrefs['physical' if p>m else 'magic' if m>p else 'tie']+=1
    continue
   p,e=forms[r['player']]['type'],forms[r['enemy']]['type']
   # Group labels from the unchanged published native type chart; no damage calculation.
   rel='neutral' if p==e or 'neutral' in (p,e) else 'advantage' if (p,e) in (('grove','tide'),('tide','ember'),('ember','grove')) else 'disadvantage'
   relation[r['kind'],r.get('objective','no-rewards'),r['policy'],rel][r['outcome']]+=1
  v['unguardedWildPhysicalMagicPreference']=damagePrefs
  for k,c in sorted(relation.items()):v['typeRelationship'].append(dict(zip(('kind','objective','policy','relationship'),k))|{'n':sum(c.values()),'outcomes':dict(c)})
  stressGroups=defaultdict(Counter); examples=[];draws=[]
  for line in (BUILD/name/'stress.jsonl').open():
   r=json.loads(line)
   if r['kind']=='damage':continue
   group='mirror' if r['case']<=552 else 'reciprocal-outlier'
   stressGroups[group,r['kind'],r.get('objective','no-rewards'),r['policy']][r['outcome']]+=1
   if r['kind']=='practice' and r['player']==223 and r['level']==15 and group=='mirror':examples.append(r)
   if r['kind']=='practice' and r['policy']=='auto' and group=='mirror' and r['outcome']=='draw':draws.append({'formId':r['player'],'name':forms[r['player']]['name'],'level':r['level']})
  v['stress']={'mirrorCases':552,'mirrorSeed':222,'reciprocalOutlierCases':160,'caseSha256':sha(BUILD/'stress-cases.txt'),
   'rawSha256':sha(BUILD/name/'stress.jsonl'),'note':'Separate targeted checks: one shared seed at entry/20 mirrors, plus reversed roles for the160 original-outlier sampled cases. These are not mixed into main rates.',
   'groups':[dict(zip(('group','kind','objective','policy'),k))|{'n':sum(c.values()),'outcomes':dict(c)} for k,c in sorted(stressGroups.items())],
   'cannondramonSeed222':examples,'autoMirrorDraws':draws}
  result['variants'][name]=v
 result['reportScriptSha256']=sha(Path(__file__))
 result['runnerScriptSha256']=sha(ROOT/'scripts/investigate-final-auto-balance.py')
 OUT.write_text(json.dumps(result,indent=2)+'\n')
 def mainrow(name,kind,policy,objective='no-rewards'):
  return next(r for r in result['variants'][name]['groups'] if r['kind']==kind and r['policy']==policy and r['objective']==objective and r['axis']=='all')
 def percent(row,outcome):return 100*row['rates'].get(outcome,0)
 lines=['# Final bounded balance pass: care9 / Practice7','',
  'Actual native host simulation, compared with immutable `16feeddf58f7637b51564c83dd24e1e6e9a2ba23`. This is a deliberately stratified regression sample, not player telemetry or physical hardware performance. No 50% win target was used.','',
  'The accepted change combines two justified original-form stat anchors with one public-information Auto policy. Heavy16, energy6, capture50→90, the Practice floor20/cap40, all evolution routes and every other profile stay unchanged.','',
  '## Paired sample and production proof','',
  'Each variant uses the same23,392 cases:276 actor forms,152 role/type representatives,5,848 matchups and four SHA-derived seeds per matchup. Actors run at entry and20; the eight starter Rookies also5/10/15. Legal enemy offsets are−2/0/+2. Each variant produces187,136 wild fights,46,784 Practice fights and5,848 damage probes.','',
  '**Production exactly matches the accepted combined candidate:**239,768 output rows,3,641 native profile rows and all additional712 stress cases are byte-identical. Native profiles also exactly match the generated current runtime catalog. Only22 form/level rows differ from baseline: Lumen3 and Pelagia7 at10–20;274 other forms remain unchanged.','',
  'The candidates were isolated copies of pinned baseline source. The final comparison compiled current production with the same pinned native harness. These measurements do not replace separate save-migration and retry tests.','',
  '| Variant | Auto wins / draws | Hint wins / draws | Auto exchanges mean / p95 |','| --- | ---: | ---: | ---: |']
 for name in NAMES:
  a=mainrow(name,'practice','auto');h=mainrow(name,'practice','public-hint')
  lines.append(f"| {name} | {percent(a,'won'):.2f}% / {percent(a,'draw'):.2f}% | {percent(h,'won'):.2f}% / {percent(h,'draw'):.2f}% | {a['turns']['mean']:.2f} / {a['turns']['p95']} |")
 lines += ['', 'Current exact Practice counts: Auto20,198 wins /2,398 losses /796 draws; public-hint22,873 /439 /80. Tactical retains an11.44 percentage-point win advantage. Both paths terminate within40 exchanges. The comparison is not monotonic on every seed:222 previous Auto wins become losses and99 become draws;8,656 previous losses and1,024 previous draws become wins.','',
  '## Original form correction','',
  'Both original Ultimates entered substantially below their peers. Weighted diagnostic budget is HP/4+Attack+Defense+Magic+Resistance; it is not the damage formula. Lumen entered at154 and Pelagia170.5 versus preserved Ultimate205–211 and new-role210. Existing growth already adds7 budget per level, so only entry anchors change.','',
  '| Form | Base HP / Atk / Def / Magic / Res | Level20 stats | Budget at10 /15 /20 |','| --- | --- | --- | --- |',
  '| Lumen3 | 136/32/26/32/30 →164/42/36/46/44 | 204/52/46/66/64 | 209 /244 /279 |',
  '| Pelagia7 | 142/28/31/40/36 →168/38/38/50/46 | 208/48/48/70/66 | 214 /249 /284 |','',
  'The anchors follow the existing Ultimate bonus and Mystic bias applied to Mote/Rill family bases. Growth remains4/1/1/2/2. Neither revised form dominates or is dominated by another Ultimate at10/15/20; each stat lies inside the role-appropriate peer envelope. Pelagia retains an explicit+4 weighted premium over new-role profiles. [Complete peer proof](original-outlier-profile-audit.json).','',
  '| Form / level | Full-collection wild wins before→after /40 | Captures before→after /40 | Hint wins before→after /40 | Auto wins before→after /40 |','| --- | ---: | ---: | ---: | ---: |']
 for point in ('3:10','3:20','7:10','7:20'):
  def pointrow(name,kind,policy,objective):return next(r for r in result['variants'][name]['groups'] if r['axis']=='formLevel' and r['stratum']==point and r['kind']==kind and r['policy']==policy and r['objective']==objective)
  cells=[]
  for kind,policy,obj,outcome in [('wild','auto','defeat-full-collection','won'),('wild','auto','capture-first','captured'),('practice','public-hint','no-rewards','won'),('practice','auto','no-rewards','won')]:
   cells.append(str(pointrow('baseline',kind,policy,obj)['outcomes'].get(outcome,0))+'→'+str(pointrow('current',kind,policy,obj)['outcomes'].get(outcome,0)))
  lines.append('| '+('Lumen' if point.startswith('3:') else 'Pelagia')+' /'+point.split(':')[1]+' | '+' | '.join(cells)+' |')
 lines += ['', 'Stat-only changes fix the outlier rather than granting universal victories:131/160 full-collection wins versus12 before;140 captures versus42. One previous capture becomes a defeat win because of stronger damage. In160 reversed-role cases these forms are now tougher opponents: hint wins fall160→157, and phase-only Auto154→87. Combined improved Auto wins144 of those reversed cases.','',
  '## Auto policy, types and remaining limits','',
  'Auto scores immediate clipped HP swing across all three possible opponent moves using the real resolver and public HP/profiles. Only exact-score ties consume its separate seeded RNG. The input type contains no hidden committed move, enemy RNG, optional two-choice hint, future events or cards. At most nine resolver calls are made per turn. Tactical sees its existing two-choice hint and may use cards.','',
  '| Player type relationship | Cases | Auto wins before→after | Hint wins after |','| --- | ---: | ---: | ---: |']
 for rel in ('advantage','neutral','disadvantage'):
  def typerow(name,policy):return next(r for r in result['variants'][name]['typeRelationship'] if r['kind']=='practice' and r['policy']==policy and r['relationship']==rel)
  a,b,h=typerow('baseline','auto'),typerow('current','auto'),typerow('current','public-hint')
  lines.append(f"| {rel} | {b['n']:,} | {100*a['outcomes'].get('won',0)/a['n']:.2f}% →{100*b['outcomes'].get('won',0)/b['n']:.2f}% | {100*h['outcomes'].get('won',0)/h['n']:.2f}% |")
 a=mainrow('current','practice','auto');c=a['choices'];att=sum(c[k] for k in ('physical','heavy','magic'));defend=sum(c[k] for k in ('brace','counter','ward'))
 lines += ['', f"Current Auto attacks: Physical{100*c['physical']/att:.2f}%, Heavy{100*c['heavy']/att:.2f}%, Magic{100*c['magic']/att:.2f}%. Defense is strongly skewed: Counter{100*c['counter']/defend:.2f}%, Ward{100*c['ward']/defend:.2f}%, Brace0%. This is an explicit limitation of a shallow expected-swing policy; the pass does not claim equally useful automatic defenses or complete balance.", '',
  'Equal-level Mega Bulwark Auto improves113/600→340/600 wins, but still draws116/600 (19.33%). Mega Warden improves118/480→374/480, with44/480 draws (9.17%). Hint draws stay8/600 and3/480. Across576 equally weighted form/level points, Auto win-rate p10/median/p90 improves20%/42.5%/75%→65%/90%/100%, with zero zero-win points. The worst remaining Auto point is Pyrel10:12 wins /28 losses in40 cases.','',
  'Separate fixed-seed222 mirror checks cover all276 forms at entry/20: Auto47 wins /419 losses /86 draws becomes549 /0 /3. These one-seed results are not population odds. Remaining draws are Kabuterimon20, MegaKabuterimon(Red)10 and Ikkakumon20. Cannondramon15 now wins in39 exchanges with79HP; the old policy drew at40 with10HP. Hint wins all552 mirrors, unchanged.','',
  '## Wild behavior and recovery','',
  'Auto-only changes every wild/damage row zero times: its policy, capture odds and rewards are untouched. Combined wild results change only through the two revised forms. Full-sample capture rises87.51%→87.93%; full-collection wins81.65%→82.16%. Mean capture-first energy changes23.609→23.583 and recovery Rests3.958→3.944; full-collection energy39.278→39.337 and Rests5.076→5.068. No additional Rest, waiting or recovery system was introduced.','',
  'Wild unguarded Physical/Magic preferences remain diverse:2,767 Physical /2,562 Magic /519 ties before,2,769 /2,569 /510 after across5,848 probes. Heavy power/cost and type multipliers are unchanged. Existing level/evolution and free-care pacing reports remain historical evidence; this pass does not remeasure player time or claim to fix repetitive Rest input.','',
  '## Reproduce and evidence','', '```sh', *result['reproduce'],'```','',
  'Candidate and raw files stay in ignored `build/final-balance-pass/`. The runner reconstructs pinned source/cases from Git; current compiles the present production source into its own snapshot. No networking, dependency installation or hardware access is needed. [Full exact counts, hashes, commands and strata](final-auto-balance.json). Earlier [Practice6 measurements](world-ds-balance.md) remain unchanged and historical.','']
 # Keep code, links, commit IDs and percentile notation literal.
 parts=re.split(r'(```[\s\S]*?```|`[^`]*`|\[[^\]]*\]\([^)]*\)|\bp(?:10|50|90|95)\b)', '\n'.join(lines))
 for index in range(0,len(parts),2):
  text=parts[index]
  text=re.sub(r'(?<=[A-Za-z:;)])(?=\d)', ' ', text)
  text=re.sub(r'(?<=\d)(?=[A-Za-z])', ' ', text)
  text=re.sub(r'(?<!\d),(?=\d)', ', ', text)
  text=re.sub(r'(?<=[A-Za-z])(?=[+−]\d)', ' ', text)
  text=re.sub(r' /(?=\d)', ' / ', text)
  text=re.sub(r' *→ *',' → ',text)
  text=text.replace('**239','** 239')
  parts[index]=text
 (ROOT/'docs/evidence/final-auto-balance.md').write_text(''.join(parts))
 print('PASS',OUT,OUT.stat().st_size,'bytes')
 print('actual production rows, profiles, stress = accepted combined candidate, byte-for-byte')
 for name in NAMES:
  for r in result['variants'][name]['groups']:
   if r['kind']=='practice' and r['axis']=='all':print(name,r['policy'],r['outcomes'],r['turns'])

if __name__=='__main__':run()

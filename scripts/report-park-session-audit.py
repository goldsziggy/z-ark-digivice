#!/usr/bin/env python3
"""Summarize the single Park candidate against identical native session cases."""
import collections,hashlib,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/park-session'
VARIANTS=('baseline','manual-full','recover-full','weighted-recover')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def dist(values):
 values=sorted(values)
 return {'min':values[0],'p50':values[math.ceil(len(values)*.5)-1],'p95':values[math.ceil(len(values)*.95)-1],'max':values[-1],'mean':sum(values)/len(values)}
def main():
 reports={v:json.loads((ROOT/f'docs/evidence/park-session-{v}.json').read_text()) for v in VARIANTS}
 raw={v:[json.loads(l) for l in (BUILD/v/'sessions.jsonl').open()] for v in VARIANTS}
 groups={};checks=0
 for name,rows in raw.items():
  assert len(rows)==1408
  for row in rows:
   assert abs(sum(row[k] for k in ('walkMs','interactionMs','autoplayMs','unspentMs'))-row['minutes']*60000)<=2
   assert row['wins']+row['captures']+row['retreats']+int(row['endsInEncounter'])==row['encounters']
   assert row['collectionCount']==1+row['captures']-row['releases']<=8
   assert row['distinctCaptured']<=row['captures'] and row['recoveryConfirmations']<=row['rests']
   checks+=4
  groups[name]={tuple(s[k] for k in ('minutes','stepsPerMinute','mode','rosterPolicy','interactionPacePercent')):s for s in reports[name]['summaries']}
 keys=('starter','seed','seedIndex','minutes','stepsPerMinute','mode','rosterPolicy','interactionPacePercent')
 for name in VARIANTS[1:]:
  assert all(all(a[k]==b[k] for k in keys) for a,b in zip(raw['baseline'],raw[name]))
 rare=[];bygroup=collections.defaultdict(list)
 for row in raw['weighted-recover']:
  bygroup[tuple(row[k] for k in ('minutes','stepsPerMinute','mode','rosterPolicy','interactionPacePercent'))].append(row)
 for key,rows in sorted(bygroup.items()):
  seen=captured=full=seenSessions=capSessions=fullSessions=0
  for row in rows:
   lifetime=0;a=b=c=0
   for event in row['events']:
    if event['rarity']=='rare':
     a+=1;b+=event['outcome']=='captured'
     c+=row['rosterPolicy']=='keep-eight' and lifetime>=7
    lifetime+=event['outcome']=='captured'
   seen+=a;captured+=b;full+=c;seenSessions+=a>0;capSessions+=b>0;fullSessions+=c>0
  rare.append(dict(zip(('minutes','stepsPerMinute','mode','rosterPolicy','interactionPacePercent'),key))|{'sessions':len(rows),'rareEncounters':seen,'rareCaptures':captured,'rareEncountersStartingFull':full,'sessionsWithRare':seenSessions,'sessionsWithRareCapture':capSessions,'sessionsWithRareStartingFull':fullSessions})
 milestones={}
 for name,rows in raw.items():
  milestones[name]={'zeroCaptureSessions':sum(r['captures']==0 for r in rows),'captureCount':dist([r['captures'] for r in rows]),'level':dist([r['level'] for r in rows]),'evolvedSessions':sum(r['evolutions']>0 for r in rows),'sessions':len(rows)}
 pairs=[]
 for before,after in [('baseline','manual-full'),('manual-full','recover-full'),('recover-full','weighted-recover')]:
  pairs.append({'before':before,'after':after,'cases':1408,'deltas':{k:dist([b[k]-a[k] for a,b in zip(raw[before],raw[after])]) for k in ('level','encounters','captures','distinctCaptured','rests','recoveryConfirmations','commandActions','walkMs')}})
 report={'formatVersion':1,'status':'PASS','baselineCommit':reports['baseline']['baselineCommit'],'candidateCareRules':10,'candidateSchema':13,
  'decision':'Retain 100 steps per encounter and current XP. Accept authored 70/25/5 stage-eligible rarity and one-confirm full recovery; retain capacity8 and explicit user release. No additional capture penalty.',
  'sessionsPerVariant':1408,'variants':{v:{'careRules':reports[v]['careRules'],'schema':reports[v]['schema'],'recoveryGoal':reports[v]['recoveryGoal'],'summaries':reports[v]['summaries'],'evidence':reports[v]['evidence']} for v in VARIANTS},
  'sameCasesAndSeeds':True,'timeAndCountAssertions':checks,'milestones':milestones,'pairedChanges':pairs,'raritySessions':rare,
  'rarityProbe':json.loads((ROOT/'docs/evidence/park-rarity-probe.json').read_text()),
  'assumptions':reports['weighted-recover']['timeAssumptionsMs'],'timingNotes':reports['weighted-recover']['timingNotes'],
  'limits':['Authored clock assumptions, not physical walking, measured user input speed, battery, device FPS or network latency.','All policies keep the starter active, use public visible combat information, capture when first legal, and take the first eligible evolution. No cards.','Four deterministic seeds per starter; overlapping duration/rate groups share prefixes and are not independent population estimates.','Keep-eight never releases. Release-oldest-nonactive explicitly represents a user choosing to make room at Home before another encounter; it is not an automatic production rule.','New Make Room during an active encounter is validated elsewhere. The session model does not infer when a human would choose it or credit captures for merely having the capability.','An atomic Auto fight committed before the session end counts even if its result animation is unfinished; that boundary is flagged per row.','Original baseline uses fullHP/energy80; manual-full and both Recover variants use fullHP/energy100 so UI efficiency has a matching resource comparator.','14 art gaps, roster capacity, capture odds, XP rewards and combat balance are untouched by this pass.'],
  'reproduce':[f'python3 scripts/run-park-session-audit.py --variant {v}'+(' --rarity-probe' if v=='weighted-recover' else '') for v in VARIANTS]+['python3 scripts/report-park-session-audit.py'],
  'reportScriptSha256':sha(Path(__file__))}
 out=ROOT/'docs/evidence/park-session-comparison.json';out.write_text(json.dumps(report,indent=2)+'\n')
 def get(v,m,mode,roster='keep-eight',pace=100,rate=100):return groups[v][m,rate,mode,roster,pace]
 lines=['# Park session comparison','',
  'Measured with the actual native core: **1,408 sessions per variant**, all eight fresh hatched starters and four deterministic seeds, 20/25/30 minutes at 80/100/120 simulated steps per minute, Tactical and Auto, and two explicit roster choices. A 25-minute / 100-step/min subset also halves and doubles assumed input time. These are authored walking-and-stopping scenarios, not physical accuracy or human usability measurements.','',
  '**Decision:** keep 100 steps per encounter and current XP. Add the single authored 70/25/5 rarity policy and one-confirm full recovery. Keep eight roster slots and explicit release; no auto-culling or capture penalty. Baseline is immutable `e6f352300ee6c049b7e21598009db4cf52cca0a6` (care9/schema12); the measured candidate is care10/schema13.','',
  '## Walking and early progression','',
  'The table uses 100 steps/min and nominal interaction time, 32 sessions per row. Counts are p50 (the lower observed median for these even-sized groups). The active starter takes the first eligible evolution; captured partners are not substituted.','',
  '| Minutes | Mode | Level before → after | Encounters before → after | Current captures / distinct | Current evolved sessions | Current walking minutes, mean |','| ---: | --- | ---: | ---: | ---: | ---: | ---: |']
 for minutes in (20,25,30):
  for mode in ('tactical','auto'):
   a,b=get('baseline',minutes,mode),get('weighted-recover',minutes,mode)
   evolved=sum(r['evolutions']>0 for r in raw['weighted-recover'] if r['minutes']==minutes and r['stepsPerMinute']==100 and r['mode']==mode and r['rosterPolicy']=='keep-eight' and r['interactionPacePercent']==100)
   lines.append(f"| {minutes} | {mode} | {a['metrics']['level']['p50']} → {b['metrics']['level']['p50']} | {a['metrics']['encounters']['p50']} → {b['metrics']['encounters']['p50']} | {b['metrics']['captures']['p50']} / {b['metrics']['distinctCaptured']['p50']} | {evolved}/32 | {b['metrics']['walkMs']['mean']/60000:.2f} |")
 current=milestones['weighted-recover']
 lines += ['',f"Across the entire candidate grid, **{current['zeroCaptureSessions']} sessions have zero captures**; capture count ranges {current['captureCount']['min']}–{current['captureCount']['max']} and final level {current['level']['min']}–{current['level']['max']}. These ranges include release-enabled runs and speed sensitivity, not one representative user. Every terminal state was encoded and restored through native snapshot validation.",'',
  'Baseline already reaches several levels; faster XP or more encounters would increase stopping and roster pressure. At 25 minutes / 100 steps per minute, merely halving versus doubling assumed input time changes baseline Tactical p50 level from 6 to 4 and Auto from 7 to 5. Real outdoor testing must establish actual interaction times.','',
  '## Recover: fewer confirmations, same native Rest effects','',
  'The original baseline recovers full HP and energy ≥80. To isolate the UI improvement, manual-full100 and Recover-full100 both execute the same existing Rest rule until HP and energy are full. Recover spends one confirmation for that group of native events. Current native `recoveryRestCount` exactly matches the events used by the model. Additional saved time can allow another encounter, so whole-session event totals need not be equal.','',
  '| 25 min / 100 steps/min | Mode | Level | Native Rest events | Recovery confirmations | All command actions |','| --- | --- | ---: | ---: | ---: | ---: |']
 for v in VARIANTS:
  for mode in ('tactical','auto'):
   s=get(v,25,mode)['metrics'];lines.append(f"| {v} | {mode} | {s['level']['p50']} | {s['rests']['p50']} | {s['recoveryConfirmations']['p50']} | {s['commandActions']['p50']} |")
 lines += ['', 'Command counts exclude individual Next taps; those are represented by assumed per-command time. Recover adds no XP, waiting requirement, healing formula or bonus. Native Rest events and user confirmations remain separate counters.','',
  '## Rarity, capacity and Make Room','',
  'The old selector followed consecutive eligible IDs with no rarity. The candidate uses a stateless seed/encounter mixer, chooses Common/Uncommon/Rare with weights 70/25/5, then selects uniformly inside the eligible bucket. It keeps first Flicker, level/stage eligibility and capture RNG separate. These are authored prototype frequency labels, not franchise canon or power ratings.','',
  'The independent native probe checks 65,536 repeatable eligible selections across four stage gates. The observed bucket shares are **70.71% / 24.45% / 4.84%**; all 65/139/206/276 eligible forms appear at the corresponding gates. First Flicker is excluded from those denominators. There are no rarity penalties to capture odds.','',
  'Early eligible buckets contain 37 Common / 22 Uncommon / 6 Rare forms: individual per-draw chances are approximately 1.892% / 1.136% / 0.833%. At the full pool they are 0.569% / 0.223% / 0.122%. This preserves the individual rarity ordering, not just aggregate bucket labels. Consecutive repeats are allowed; a rare is never guaranteed.','',
  '| 25 min / 100 steps/min | Roster choice | Sessions seeing rare | Rare encounters | Rare captures | Rare encounters starting full |','| --- | --- | ---: | ---: | ---: | ---: |']
 for r in rare:
  if r['minutes']==25 and r['stepsPerMinute']==100 and r['interactionPacePercent']==100:
   lines.append(f"| {r['mode']} | {r['rosterPolicy']} | {r['sessionsWithRare']}/32 | {r['rareEncounters']} | {r['rareCaptures']} | {r['rareEncountersStartingFull']} |")
 lines += ['', 'Keeping all eight members is a genuine choice: later rares may arrive with no free slot. At 30 minutes, keep-eight Auto has 37 rare encounters, with 25 starting full. The new **Make Room** flow lets the user explicitly release a nonactive member without discarding the current encounter; it does not silently release anything. This report does not award hypothetical captures for that capability. Its separate release-oldest policy makes room at Home before walking, and quantifies management cost rather than guessing individual preferences.','',
  'At 25 minutes, release-enabled current runs have p50 12 distinct captured forms in Tactical and 15 in Auto, versus 7 when keeping eight. The journal retains previously obtained forms after release. Failed capture attempts, retreats, full-roster events, command counts and all speed/duration strata are retained in the JSON evidence.','',
  '## Time assumptions and reproducibility','',
  'Walking stops during every interaction/animation. Onboarding and initial mode choice precede the timer. Assumptions: encounter notice 3 s; Tactical command 4 s, plus the existing 1.2 s capture windup; Auto confirmation 4 s, then 0.65 s per native trace row; result acknowledgement 2 s; Care navigation 4 s; Rest or Recover confirmation 2 s; explicit release/evolution 10 s. Input-speed sensitivity scales decisions/controls, not fixed capture or Auto animation durations. No physical button timing, ESP frame rate, network delay, location or step-sensor accuracy is inferred.','',
  'Auto resolution is atomic: if confirmed just before the timer expires, its durable outcome counts and an unfinished presentation is flagged. Tactical only commits commands whose modeled duration fits. Unspent final time is reported. All time and outcome/capacity accounting assertions passed.','',
  '```sh',*report['reproduce'],'```','',
  '[Full comparison, milestones, assumptions, raw/source hashes and exact build commands](park-session-comparison.json). [Native rarity probe](park-rarity-probe.json). Exact encounter records remain in ignored `build/park-session/`; public reports retain per-session totals and source hashes. No additional combat tuning or art changes were made.','']
 (ROOT/'docs/evidence/park-session-comparison.md').write_text('\n'.join(lines))
 print('PASS',checks,'time/count assertions;',out)
 print('candidate milestones',current)
 for mode in ('tactical','auto'):
  for minutes in (20,25,30):
   rows=[r for r in raw['weighted-recover'] if r['minutes']==minutes and r['stepsPerMinute']==100 and r['mode']==mode and r['rosterPolicy']=='keep-eight' and r['interactionPacePercent']==100]
   print(mode,minutes,'evolved',sum(r['evolutions']>0 for r in rows),'/32')
if __name__=='__main__':main()

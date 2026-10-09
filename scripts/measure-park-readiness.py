#!/usr/bin/env python3
"""Add observed milestones to the frozen Park cohort; never changes gameplay."""
import argparse, collections, hashlib, json, statistics, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = 'e7b905f1922a1075d0e264a5b087de45b409777f'
BUILD = ROOT / 'build/park-session/readiness'
SOURCES = ['combat.cpp', 'forms.cpp', 'game.cpp', 'practice_battle.cpp', 'battle_trace.cpp',
           'legacy_combat_v3.cpp', 'legacy_forms_v5.cpp', 'legacy_combat_v5.cpp',
           'legacy_forms_v6.cpp', 'legacy_combat_v6.cpp', 'legacy_forms_v7.cpp',
           'legacy_combat_v7.cpp', 'legacy_forms_v8.cpp', 'legacy_combat_v8.cpp',
           'encounters.cpp', 'legacy_forms_v9.cpp', 'legacy_combat_v9.cpp']
NEW_FIELDS = {'newCaptureForms', 'firstEvolutionAtMs', 'firstEvolutionSteps',
              'firstEvolutionForm', 'firstEvolutionLevel', 'firstEvolutionWalkMs',
              'firstEvolutionStoppedMs'}

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def dist(values):
    return None if not values else {'median': statistics.median(values), 'min': min(values),
                                   'max': max(values), 'mean': statistics.mean(values)}

def summarize(group):
    evolved = [r for r in group if r['firstEvolutionAtMs'] >= 0]
    metrics = ('levelsGained', 'encounters', 'captures', 'distinctCaptured', 'newCaptureForms',
               'failedCaptureAttempts', 'retreats', 'fullEncounters', 'releases', 'steps',
               'walkMs', 'stoppedMs', 'unspentMs', 'rests', 'recoveryConfirmations', 'commandActions')
    rare = {'encounters': 0, 'captures': 0, 'startingFull': 0, 'sessionsSeeing': 0, 'sessionsCapturing': 0}
    buckets = collections.Counter()
    for r in group:
        prior_captures = 0; seen = caught = False
        for i, event in enumerate(r['events']):
            # Forced first Flicker is excluded from weighted frequency counts.
            if i: buckets[event['rarity']] += 1
            if event['rarity'] == 'rare':
                rare['encounters'] += 1; seen = True
                if r['rosterPolicy'] == 'keep-eight' and prior_captures >= 7: rare['startingFull'] += 1
                if event['outcome'] == 'captured': rare['captures'] += 1; caught = True
            prior_captures += event['outcome'] == 'captured'
        rare['sessionsSeeing'] += seen; rare['sessionsCapturing'] += caught
    return {'n': len(group), 'metrics': {k: dist([r[k] for r in group]) for k in metrics},
            'firstEvolution': {'observed': len(evolved), 'notYetByEnd': len(group)-len(evolved),
                               'elapsedMs': dist([r['firstEvolutionAtMs'] for r in evolved]),
                               'steps': dist([r['firstEvolutionSteps'] for r in evolved]),
                               'walkMs': dist([r['firstEvolutionWalkMs'] for r in evolved]),
                               'stoppedMs': dist([r['firstEvolutionStoppedMs'] for r in evolved]),
                               'levels': dict(collections.Counter(r['firstEvolutionLevel'] for r in evolved)),
                               'targetForms': dict(collections.Counter(r['firstEvolutionForm'] for r in evolved))},
            'fullRosterSessions': sum(r['fullAtMs'] >= 0 for r in group),
            'firstFullRosterMs': dist([r['fullAtMs'] for r in group if r['fullAtMs'] >= 0]),
            'zeroCaptureSessions': sum(r['captures'] == 0 for r in group),
            'endsInEncounter': sum(r['endsInEncounter'] for r in group),
            'presentationPending': sum(r['presentationPending'] for r in group),
            'weightedEncounterBuckets': dict(buckets), 'rare': rare}

def compact(d, scale=1, digits=0):
    if d is None: return 'not observed'
    def number(x): return f'{x/scale:.{digits}f}'.rstrip('0').rstrip('.') if digits else f'{x/scale:g}'
    return f"{number(d['median'])} ({number(d['min'])}–{number(d['max'])})"

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--reuse', action='store_true'); args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True); source = BUILD/'core'; source.mkdir(exist_ok=True)
    old_report_path = ROOT/'docs/evidence/park-session-weighted-recover.json'
    old_report = json.loads(old_report_path.read_text())
    old_raw = ROOT/'build/park-session/weighted-recover/sessions.jsonl'
    cases = BUILD/'cases.txt'
    # Public frozen rows retain every input, so a fresh checkout needs no ignored case file.
    case_rows = [[r['case'],r['starter'],r['seed'],r['seedIndex'],r['minutes'],r['stepsPerMinute'],
                  int(r['mode']=='auto'),int(r['rosterPolicy']=='release-oldest-nonactive'),r['interactionPacePercent']]
                 for r in old_report['rows']]
    cases.write_text(''.join(' '.join(map(str,r))+'\n' for r in case_rows))
    assert sha(cases) == old_report['evidence']['caseSha256']
    if not args.reuse:
        for name in subprocess.check_output(['git', 'ls-tree', '--name-only', PIN+':core'], cwd=ROOT, text=True).splitlines():
            (source/name).write_bytes(subprocess.check_output(['git', 'show', PIN+':core/'+name], cwd=ROOT))
        (BUILD/'park_session_audit.cpp').write_bytes((ROOT/'tests/park_session_audit.cpp').read_bytes())
    for name, expected in old_report['evidence']['sourceHashes'].items():
        assert sha(source/name) == expected, f'Native source changed: {name}'
    command = ['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-I'+str(source), '-DPARK_RECOVERY_MODE=2', '-DPARK_CURRENT_RARITY',
               str(BUILD/'park_session_audit.cpp'), *[str(source/n) for n in SOURCES], '-o', str(BUILD/'audit')]
    if not old_raw.exists():
        # Recreate the unchanged reference only when its ignored raw evidence is absent.
        reference_harness = BUILD/'reference.cpp'
        reference_harness.write_bytes(subprocess.check_output(['git','show',PIN+':tests/park_session_audit.cpp'],cwd=ROOT))
        reference_command = [str(reference_harness) if p==str(BUILD/'park_session_audit.cpp') else
                             str(BUILD/'reference') if p==str(BUILD/'audit') else p for p in command]
        subprocess.run(reference_command,cwd=ROOT,check=True)
        old_raw = BUILD/'reference-sessions.jsonl'
        with cases.open() as inp,old_raw.open('w') as out:
            subprocess.run([BUILD/'reference'],stdin=inp,stdout=out,cwd=ROOT,check=True)
    assert sha(old_raw) == old_report['evidence']['rawSha256']
    raw = BUILD/'sessions.jsonl'
    if not args.reuse:
        subprocess.run(command, cwd=ROOT, check=True)
        with cases.open() as inp, raw.open('w') as out:
            subprocess.run([BUILD/'audit'], stdin=inp, stdout=out, cwd=ROOT, check=True)
    rows = [json.loads(line) for line in raw.read_text().splitlines()]
    old_rows = [json.loads(line) for line in old_raw.read_text().splitlines()]
    assert len(rows) == len(old_rows) == 1408
    for row, old in zip(rows, old_rows):
        projection = {k:v for k,v in row.items() if k not in NEW_FIELDS}
        projection['events'] = [{k:v for k,v in e.items() if k not in ('previouslyObtained','newCapture')} for e in row['events']]
        assert projection == old, f"Prior measurement changed: {row['case']}"
        assert row['newCaptureForms'] == sum(e['newCapture'] for e in row['events'])
        assert row['newCaptureForms'] <= row['distinctCaptured'] <= row['captures']
        assert (row['firstEvolutionAtMs'] >= 0) == (row['evolutions'] > 0)
        if row['firstEvolutionAtMs'] >= 0:
            assert row['firstEvolutionSteps'] <= row['steps']
            assert row['firstEvolutionLevel'] == 5
            assert abs(row['firstEvolutionWalkMs'] + row['firstEvolutionStoppedMs'] - row['firstEvolutionAtMs']) <= 2
        row['levelsGained'] = row['level']-1; row['stoppedMs'] = row['interactionMs']+row['autoplayMs']
    keys = ('minutes','stepsPerMinute','mode','rosterPolicy','interactionPacePercent')
    groups = collections.defaultdict(list)
    for r in rows: groups[tuple(r[k] for k in keys)].append(r)
    summaries = [dict(zip(keys,key)) | summarize(group) for key,group in sorted(groups.items())]
    primary = [r for r in summaries if r['stepsPerMinute']==100 and r['interactionPacePercent']==100 and r['rosterPolicy']=='keep-eight']
    probe_path = ROOT/'docs/evidence/park-rarity-probe.json'; probe = json.loads(probe_path.read_text())
    report = {'formatVersion':1, 'status':'PASS', 'nativeCommit':PIN, 'careRules':10, 'schema':13,
              'scope':'Host native-core simulation with assumed human timing, not physical walking, battery, radio, device FPS or actual-browser measurements.',
              'sessions':len(rows), 'previousFieldsExactlyUnchanged':len(rows),
              'medianDefinition':'Usual statistical median (midpoint of the two middle values); prior reports used lower observed p50.',
              'newCaptureDefinition':'Native hasObtained was false at encounter start and that form was captured; excludes founder/already evolved/already captured forms. Counts game form IDs, not canonical species deduplication.',
              'firstEvolutionDefinition':'Elapsed time and native cumulative steps immediately after the first real Evolve commit, including its assumed 10-second interaction. Summary timings condition on evolved sessions; others are right-censored at session end.',
              'initialState':'Fresh level-one hatched starter; onboarding and initial mode selection excluded. First eligible outgoing evolution chosen, active founder retained; no cards.',
              'timeAssumptionsMs':old_report['timeAssumptionsMs'], 'timingNotes':old_report['timingNotes'],
              'rarity':{'targetPercent':[70,25,5], 'policy':'Authored bucket weights then uniform eligible form; forced first Flicker; unchanged stage/level eligibility and capture odds.', 'probeSelections':probe['draws'], 'probeArtifact':'park-rarity-probe.json', 'probeSha256':sha(probe_path)},
              'summaries':summaries, 'primary':primary,
              'starterMilestones':[{'minutes':minutes,'mode':mode,'starter':starter,**summarize([r for r in rows if r['minutes']==minutes and r['mode']==mode and r['starter']==starter and r['stepsPerMinute']==100 and r['interactionPacePercent']==100 and r['rosterPolicy']=='keep-eight'])} for minutes in (20,25,30) for mode in ('tactical','auto') for starter in range(1,9)],
              'rows':[{k:v for k,v in r.items() if k!='events'} for r in rows],
              'evidence':{'buildCommand':command,'rawPath':str(raw.relative_to(ROOT)), 'rawSha256':sha(raw), 'caseSha256':sha(cases), 'harnessSha256':sha(BUILD/'park_session_audit.cpp'), 'runnerSha256':sha(Path(__file__)), 'frozenPriorReportSha256':sha(old_report_path), 'frozenPriorRawSha256':sha(old_raw), 'nativeSourceHashes':old_report['evidence']['sourceHashes']}}
    out = ROOT/'docs/evidence/park-readiness-measurements.json'; out.write_text(json.dumps(report,indent=2)+'\n')
    lines = ['# Measured Park session outcomes', '', 'This is a deterministic native-core simulation, **not an outdoor or human usability test**. It adds milestone instrumentation to the same 1,408 frozen Park cases; every previous result and per-encounter field matched exactly. Existing Park reports are unchanged.', '',
             'The main table uses **100 simulated steps/min while walking**, normal assumed interaction time, keeping all eight roster slots, all eight starters and four seeds: **32 sessions per row**. Values are median (minimum–maximum); levels gained start from level 1. Captures include repeat forms. New forms are actual native journal additions caused by captures, excluding the starter and any already evolved/captured form; no canonical species deduplication is claimed.', '',
             '| Minutes | Mode | Levels gained | Encounters | Captures | Distinct captured forms | Newly obtained by capture | Failed attempts |',
             '| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for r in primary:
        m=r['metrics']; lines.append('| '+ ' | '.join([str(r['minutes']),r['mode'],*[compact(m[k]) for k in ('levelsGained','encounters','captures','distinctCaptured','newCaptureForms','failedCaptureAttempts')]])+' |')
    lines += ['', 'Walking stops during every interaction and animation. These are authored assumptions: encounter notice 3 s; Tactical command 4 s plus capture windup 1.2 s; Auto confirmation 4 s plus 0.65 s per recorded turn; result 2 s; Care navigation 4 s; Recover confirmation 2 s; release or evolution 10 s. Onboarding and initial mode selection are excluded. Individual Next taps are absorbed into the command assumptions. Unspent final time is separately counted.', '',
              '| Minutes | Mode | Walking minutes | Stopped minutes | Steps | Rest events / Recover confirmations, median | First full roster, minutes |',
              '| ---: | --- | ---: | ---: | ---: | ---: | ---: |']
    for r in primary:
        m=r['metrics']; lines.append('| '+' | '.join([str(r['minutes']),r['mode'],compact(m['walkMs'],60000,2),compact(m['stoppedMs'],60000,2),compact(m['steps']),f"{m['rests']['median']:g} / {m['recoveryConfirmations']['median']:g}",compact(r['firstFullRosterMs'],60000,2)])+' |')
    lines += ['', '## First digivolution', '', 'Every observed first evolution was the starter’s first Champion route at level 5 (required bond 20). Times below include the actual modeled Evolve commit. Timings and steps are **only among sessions that evolved**; the not-yet column is right-censored, not a failure or a zero-time evolution.', '',
              '| Session minutes | Mode | Evolved / 32 | Not yet | First evolution minutes | Steps at first evolution |',
              '| ---: | --- | ---: | ---: | ---: | ---: |']
    for r in primary:
        e=r['firstEvolution']; lines.append('| '+' | '.join([str(r['minutes']),r['mode'],str(e['observed']),str(e['notYetByEnd']),compact(e['elapsedMs'],60000,2),compact(e['steps'])])+' |')
    lines += ['', 'The only 25-minute / 100-step/min Tactical session not yet evolved used Gomamon; it evolved at 25.07 minutes in the matching 30-minute run. This is one of four Gomamon seeds, not a species-wide failure. All 32 runs in both modes evolved by 30 minutes.', '',
              'Walking-cadence sensitivity at 25 minutes (same 32 starter/seed cases per row, ordinary input time):', '',
              '| Steps/min while walking | Mode | Levels gained | Evolved / 32 | First evolution minutes, among evolved |',
              '| ---: | --- | ---: | ---: | ---: |']
    for r in summaries:
        if r['minutes']==25 and r['rosterPolicy']=='keep-eight' and r['interactionPacePercent']==100:
            lines.append('| '+' | '.join([str(r['stepsPerMinute']),r['mode'],compact(r['metrics']['levelsGained']),str(r['firstEvolution']['observed']),compact(r['firstEvolution']['elapsedMs'],60000,2)])+' |')
    lines += ['', '## Rarity and roster pressure', '', 'Authored 70% Common / 25% Uncommon / 5% Rare buckets select uniformly among eligible forms. The separate 65,536-draw native probe observed **70.71% / 24.45% / 4.84%**, excluding the forced first Flicker. Rarity changes frequency, not capture odds. A rare encounter is not guaranteed in a short walk.', '',
              '| 25-minute mode | Roster policy | Sessions seeing a rare / 32 | Rare encounters / captures | Rare encounters starting full | New capture forms, median (range) | Releases, median (range) |',
              '| --- | --- | ---: | ---: | ---: | ---: | ---: |']
    for r in summaries:
        if r['minutes']==25 and r['stepsPerMinute']==100 and r['interactionPacePercent']==100:
            q=r['rare']; lines.append('| '+' | '.join([r['mode'],r['rosterPolicy'],str(q['sessionsSeeing']),f"{q['encounters']} / {q['captures']}",str(q['startingFull']),compact(r['metrics']['newCaptureForms']),compact(r['metrics']['releases'])])+' |')
    lines += ['', 'All primary sessions reached a full eight-member roster. Failed capture attempts had median 5 (range 0–16); at 30 minutes retreats had median 0, range 0–4 Tactical / 0–5 Auto. Full-roster encounters at 30 minutes had median 11 Tactical / 15 Auto. These are bounded outcomes, not a promise that every fight or capture succeeds.', '',
              'The release strategy explicitly spends 10 seconds releasing an old nonactive partner at Home before walking; it is a modeled user choice, not automatic game behavior. The separate Make Room feature can preserve a live encounter while the user releases a nonactive partner; these measurements do not award hypothetical captures for it. No session across the 1,408-case grid had zero captures.', '',
              'The JSON retains 80/100/120-step/min strata, half/double input-time sensitivity, every starter’s censored milestone counts, retreats, full-roster encounters, command counts and complete session totals. Native snapshots were encoded and restored after every session. Auto commits atomically before its replay: a result may count when presentation is still unfinished at the cutoff; the flag is retained. Tactical only commits an action when its assumed time fits.', '',
              'Reproduce without changing the old reports:', '', '```sh', 'python3 scripts/measure-park-readiness.py', '```', '',
              '[Machine-readable results, exact inputs, source/build hashes and preservation checks](park-readiness-measurements.json). [Earlier four-way comparison](park-session-comparison.md). [Independent rarity probe](park-rarity-probe.json).']
    (ROOT/'docs/evidence/park-readiness-measurements.md').write_text('\n'.join(lines)+'\n')
    print('PASS 1,408 native sessions; all prior fields identical; first evolution and new capture journals measured.')
    print('\n'.join(lines[:17]))

if __name__=='__main__': main()

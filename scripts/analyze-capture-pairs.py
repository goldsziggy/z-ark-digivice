#!/usr/bin/env python3
"""Paired capture-policy analysis of isolated native candidate raw data."""
import json,pathlib,collections,subprocess
r=pathlib.Path(__file__).resolve().parents[1]
PIN='aea1a4188430feba807ad444a4d85ed55e0cd93a'
forms={f['formId']:f for f in json.loads(subprocess.check_output(['git','show',PIN+':data/world-ds-runtime.json'],cwd=r))['forms']};out={}
for name in ['baseline','capture65','capture50','heavy12-capture50']:
 d={};groups=collections.defaultdict(collections.Counter);examples={}
 for line in (r/'build/balance-investigation'/name/'results.jsonl').open():
  x=json.loads(line)
  if x['kind']!='wild' or x['objective']!='capture-first' or x['policy'] not in ['public-greedy','public-quarter']:continue
  key=x['case'];d.setdefault(key,{})[x['policy']]=x
 for pair in d.values():
  a,b=pair['public-quarter'],pair['public-greedy'];f=forms[a['player']]
  outcome=('bothCapture' if a['outcome']==b['outcome']=='captured' else 'quarterOnlyCapture' if a['outcome']=='captured' else 'firstOnlyCapture' if b['outcome']=='captured' else 'neitherCapture')
  for axis,val in [('all','all'),('tier',f['combatTier']),('role',f['role']),('tierRole',f['combatTier']+'/'+f['role'])]:
   c=groups[axis,val];c['n']+=1;c[outcome]+=1;c['turnDelta']+=a['turns']-b['turns'];c['restDelta']+=a['recoveryRests']-b['recoveryRests']
  if outcome not in examples:examples[outcome]={'quarter':a,'first':b,'playerName':f['name'],'enemyName':forms[a['enemy']]['name']}
 out[name]={'groups':[{'axis':k[0],'value':k[1],**v} for k,v in groups.items()],'examples':examples}
p=r/'docs/evidence/gameplay-capture-paired.json';p.write_text(json.dumps(out,indent=2)+'\n')
for name,v in out.items():
 print(name,v['groups'][0]);print('quarterimproves',[(g['value'],g.get('quarterOnlyCapture',0)-g.get('firstOnlyCapture',0),g['n']) for g in v['groups'] if g['axis']=='tierRole' and g.get('quarterOnlyCapture',0)>g.get('firstOnlyCapture',0)])

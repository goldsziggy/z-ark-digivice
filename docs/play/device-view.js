// Browser presentation of the installed 171cda7 touch screens. Game actions
// still go through the unchanged native core; gesture geometry is kept in
// device-touch-input.js. Physical sensors and radios are never simulated here.
import { orderedMembers } from './shared/party.js';

export function createDeviceView(api) {
  let page = 'starter', home = 0, memberIndex = 0, evolutionIndex = 0, move = 0, statsPage = 0;
  let previousPhase, previousSequence, previousStarter, reviewStarter, revision = 0, reviewState = null, proposedMode = null, memberId = null;
  const homes = ['CARE', 'PARTNERS', 'SETTINGS', 'NEARBY'];
  const moves = ['attack', 'heavy', 'magic'];
  const state = () => api.getState();
  const members = () => orderedMembers(state());
  const member = () => members()[memberIndex % Math.max(1,members().length)];
  const options = () => state()?.evolution?.options || [];
  const option = () => options()[evolutionIndex % Math.max(1,options().length)];
  function go(next) { page = next; reviewState = null; revision++; api.changed(); }
  function sync() {
    const s=state(); if(!s)return;
    if (previousStarter !== api.getStarter() && page==='starter-review') {page='starter';reviewState=null;revision++;}
    previousStarter=api.getStarter();
    if(previousPhase !== s.phase) {
      page=s.phase==='egg'?'starter':s.phase==='encounter'?'encounter':previousPhase==='encounter'?'result':'home';
      memberIndex=evolutionIndex=move=0; memberId=s.activeCreatureId; revision++;
    }
    if(previousSequence !== s.sequence) {
      revision++;
      move=0;
      const position=members().findIndex(m=>m.id===memberId);memberIndex=position<0?0:position;memberId=member()?.id;
      if(['starter-review','evolution-review','release-review','mode-review'].includes(page) && reviewState && reviewState!==s) {
        page=page==='starter-review'?'starter':page==='mode-review'?'settings':page==='release-review'?'stats':'evolution';
        reviewState=null;proposedMode=null;
      }
    }
    previousPhase=s.phase;previousSequence=s.sequence;
  }
  const current = () => api.isCapture() ? 'capture' : page;
  function targets() {
    const s=state(), out=[]; if(!s)return out;
    const add=(id,label,x,y,w,h,enabled=true)=>out.push({id,label,x,y,w,h,enabled:enabled&&api.canPlay()});
    const back=()=>add('back','BACK',144,348,124,38);
    const p=current(), m=member(), o=option();
    if(p==='egg') add('meet','MEET PARTNER',116,260,180,54);
    if(p==='starter') {add('choose','CHOOSE',116,292,180,44);back();}
    if(p==='starter-review') {add('hatch','HATCH',116,252,180,52);back();}
    if(p==='home') {
      add('previous','‹',30,134,54,112);add('next','›',328,134,54,112);
      add('home-open',homes[home],116,300,180,46,s.phase==='home');
    }
    if(p==='care') {
      add('feed','FEED',62,232,139,46);add('play','PLAY',211,232,139,46,s.energy>=5||s.mood>=100);
      add('rest','REST +25',116,292,180,44);back();
    }
    if(p==='collection') {
      const active=m?.id===s.activeCreatureId, companion=s.partyMemberIds.includes(m?.id);
      add('party',active?'ACTIVE PARTNER':companion?'REMOVE XP COMPANION':s.partyMemberIds.length>=s.partyCapacity?'XP COMPANIONS FULL':'ADD XP COMPANION',100,248,212,36,!!m&&!active&&(companion||s.partyMemberIds.length<s.partyCapacity)&&s.phase==='home');
      add('stats','STATS + EVOLVE',62,286,139,46,!!m);add('select','MAKE PARTNER',211,286,139,46,!!m&&!active&&s.phase==='home');back();
    }
    if(p==='stats') {add(m?.id===s.activeCreatureId?'evolution':'release-review',m?.id===s.activeCreatureId?'DIGIVOLVE':'RELEASE DIGIMON',108,292,196,44,!!m&&s.phase==='home');back();}
    if(p==='release-review') {add('release','RELEASE',108,280,196,50,!!m&&m.id!==s.activeCreatureId&&s.phase==='home');back();}
    if(p==='evolution') {add('evolution-review',o?.eligible?'SELECT':'REQUIREMENTS',174,286,176,46,!!o);back();}
    if(p==='evolution-review') {add('evolve','DIGIVOLVE',108,280,196,50,!!o?.eligible);back();}
    if(p==='settings') {add('mode',s.battleMode==='auto'?'MODE: AUTO':'MODE: MANUAL',62,286,139,46,s.phase==='home');back();}
    if(p==='mode-review') {add('mode-confirm','CONFIRM',116,262,180,50,s.phase==='home');back();}
    if(p==='nearby') back();
    if(p==='encounter') add(s.autoCapture===1?'capture':'battle',s.autoCapture===1?'AIM CAPTURE':s.battleMode==='auto'?'AUTO BATTLE':'BATTLE',116,248,180,52);
    if(p==='battle') {
      if(s.battleMode==='auto') add(s.autoCapture===1?'capture':'auto-fight',s.autoCapture===1?'AIM CAPTURE':'RUN AUTO',116,250,180,52);
      else add('capture','CATCH',274,287,76,44,api.canCapture());
      back();
    }
    if(p==='capture') {
      if(s.autoCapture===1) add('auto-resume','SKIP / RESUME FIGHT',104,348,204,38);
      else back();
    }
    if(p==='result') add('home','HOME',116,274,180,50);
    return out;
  }
  function back() {
    if(current()==='capture'){api.closeCapture();go('battle');return;}
    const parents={'starter':'egg','starter-review':'starter','battle':'encounter','stats':'collection','release-review':'stats','evolution':'stats','evolution-review':'evolution','mode-review':'settings'};
    go(parents[page]||'home');
  }
  function horizontal(next) {
    if(!api.canPlay())return;
    const step=next?1:-1, p=current();
    if(p==='starter') {
      const choices=api.getStarters(), i=choices.findIndex(s=>s.id===api.getStarter());
      api.setStarter(choices[(i+step+choices.length)%choices.length].id);
    } else if(p==='home') home=(home+step+4)%4;
    else if(p==='collection') {memberIndex=(memberIndex+step+members().length)%Math.max(1,members().length);memberId=member()?.id;}
    else if(p==='stats') statsPage=(statsPage+step+4)%4;
    else if(p==='evolution') evolutionIndex=(evolutionIndex+step+options().length)%Math.max(1,options().length);
    else if(p==='battle'&&state().battleMode==='tactical') move=(move+step+3)%3;
    revision++;api.changed();
  }
  function activate(id) {
    if(!targets().some(t=>t.id===id&&t.enabled))return;
    if(id==='back'){back();return;}
    if(id==='previous'||id==='next'){horizontal(id==='next');return;}
    if(id==='meet'){go('starter');return;}
    if(id==='choose'){go('starter-review');reviewState=state();reviewStarter=api.getStarter();return;}
    if(id==='hatch'){if(reviewState===state()&&reviewStarter===api.getStarter())void api.command('hatch',reviewStarter);else go('starter');return;}
    if(id==='home-open'){if(home===1){memberIndex=0;memberId=state().activeCreatureId;}go(['care','collection','settings','nearby'][home]);return;}
    if(['feed','play','rest','auto-fight','auto-resume'].includes(id)){void api.command(id);return;}
    if(id==='party'){void api.command(state().partyMemberIds.includes(member().id)?'party-remove':'party-add',member().id);return;}
    if(id==='select'){void api.command('select',member().id);return;}
    if(id==='stats'){statsPage=0;go('stats');return;}
    if(id==='release-review'){go('release-review');reviewState=state();return;}
    if(id==='release'){if(reviewState===state()){const id=member().id;go('collection');void api.command('release',id);}return;}
    if(id==='evolution'){evolutionIndex=0;go('evolution');return;}
    if(id==='evolution-review') {
      if(!option().eligible){api.notify(`Requires level ${option().requiredLevel} and ${option().requiredBond} bond. Keep growing together.`);return;}
      go('evolution-review');reviewState=state();return;
    }
    if(id==='evolve'){if(reviewState===state())void api.command('evolve',option().formId);return;}
    if(id==='mode'){proposedMode=state().battleMode==='auto'?0:1;go('mode-review');reviewState=state();return;}
    if(id==='mode-confirm'){if(reviewState===state()&&proposedMode!==null){const mode=proposedMode;go('settings');void api.command('mode',mode);}return;}
    if(id==='battle'){go('battle');return;}
    if(id==='capture'){api.openCapture();return;}
    if(id==='home')go('home');
  }
  function battleCommit() {
    if(current()!=='battle'||state().battleMode!=='tactical'||!api.canPlay())return;
    if(move===1&&state().energy<6){api.notify('Heavy needs 6 energy.');return;}
    void api.command(moves[move]);
  }
  function mode() {const p=current();return p==='capture'?'capture':p==='battle'&&state().battleMode==='tactical'?'battle':['starter','home','collection','stats','evolution'].includes(p)?'browse':'buttons';}
  function description() {
    const p=current(), s=state();if(!s)return 'Loading the touch demo.';
    const selected=p==='starter'?api.getStarters().find(v=>v.id===api.getStarter())?.name:p==='home'?homes[home]:p==='collection'?member()?.name:p==='evolution'?option()?.name:p==='battle'&&s.battleMode==='tactical'?['Physical','Heavy','Magic'][move]:'';
    return `${p.replaceAll('-',' ')}${selected?`: ${selected}`:''}. ${p==='capture'?'Tap the main area to commit the current timing.':mode()==='battle'?'Swipe or tap left/right to choose a move. Swipe up to use it.':mode()==='browse'?'Swipe or tap left/right to browse. Use the labeled screen buttons to choose.':'Tap a labeled screen button.'}`;
  }
  function paint(ctx,{art,eggs,time,reducedMotion}) {
    const s=state(),p=current();if(!s)return;
    const box=(x,y,w,h,fill='#102b2bec',r=6)=>{ctx.fillStyle=fill;ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fill();};
    const text=(value,x,y,size=12,tint='#f5f3df')=>{ctx.fillStyle=tint;ctx.font=`600 ${size}px Arial`;ctx.textAlign='center';ctx.fillText(String(value),x,y);};
    const label=(value,y,size=12)=>{box(58,y-size-8,296,size+20);text(value,206,y,size);};
    const actor=(id,x=206,y=176,maxSide=144,facing)=>{
      if(!art?.drawForm(ctx,id,{x,y,maxSide,time:reducedMotion?0:time,animation:'idle',facing})){
        box(x-maxSide/2,y-13,maxSide,26);text(art?.formStatus(id)==='missing'?'EXACT ART UNAVAILABLE':art?.formStatus(id)==='error'?'ART COULD NOT LOAD':'LOADING ART…',x,y+3,8);
      }
    };
    label(p==='home'?homes[home]:p==='battle'?`${s.battleMode==='auto'?'AUTO':'MANUAL'} BATTLE`:p.replaceAll('-',' ').toUpperCase(),64,11);
    if(['egg','starter','starter-review'].includes(p)) {
      const egg=eggs.get(api.getStarter());if(egg)ctx.drawImage(egg,132,90,148,148);
      label(api.getStarters().find(v=>v.id===api.getStarter())?.name||'Choose an egg',p==='starter-review'?232:270,19);
    } else if(p==='home') {
      label(s.creature,98,19);actor(s.formId,206,196,176);label(`LV ${s.level} · HP ${s.hp}/${s.combat.maxHp}`,282,11);
    } else if(p==='care') {
      actor(s.formId,206,132,80);label(`HP ${s.hp}/${s.combat.maxHp} · ENERGY ${s.energy}`,191,11);label(`FULL ${s.fullness} · MOOD ${s.mood}`,216,11);
    } else if(p==='collection'||p==='stats'||p==='release-review') {
      const m=member();if(m){actor(m.formId,206,150,116);label(m.name,222,18);if(p==='stats'){
        const pages=[`LV ${m.level} · ${m.bond} BOND · ${m.xp} XP`,`HP ${m.hp}/${m.combat.maxHp} · ${m.combat.type.toUpperCase()}`,m.combat.skills.physical,m.combat.skills.magic];
        label(`${statsPage+1}/4 · ${pages[statsPage]}`,257,11);
      }if(p==='release-review')label('This companion will leave your box.',253,11);}
      else label('Hatch a partner first',210,16);
    } else if(p==='evolution'||p==='evolution-review') {
      const o=option();if(o){actor(o.formId,206,150,112);label(o.name,226,17);label(`LEVEL ${s.level}/${o.requiredLevel} · BOND ${s.bond}/${o.requiredBond}`,258,10);}
      else label('No further evolution',210,16);
    } else if(p==='settings'||p==='mode-review') {
      actor(s.formId,206,144,110);label(p==='mode-review'?`CHANGE TO ${proposedMode===0?'MANUAL':'AUTO'}?`:'BATTLE PREFERENCE',214,13);
      if(p==='settings')label('Hardware settings are device-only.',248,10);
    } else if(p==='nearby') {
      label('ON THE HANDHELD',145,18);label('Nearby play needs two real devices.',190,12);label('This demo has no radio connection.',223,12);
    } else if(p==='encounter'||p==='battle') {
      text(s.creature,118,97,12);text(s.wildName,294,97,12);
      text(`HP ${s.hp}/${s.combat.maxHp}`,118,118,10);text(`HP ${s.wildHp}/${s.wildMaxHp}`,294,118,10);
      actor(s.formId,118,185,112,'right');actor(s.wildFormId,294,185,112,'left');
      if(p==='battle'&&s.battleMode==='tactical') {
        const labels=['PHYSICAL','HEAVY','MAGIC'];
        box(80,248,96,39);text(`‹ ${labels[(move+2)%3]}`,128,273,10);
        box(238,248,96,39);text(`${labels[(move+1)%3]} ›`,286,273,10);
        box(168,240,76,53,'#4e6e52');text(labels[move],206,263,10);text('↑ USE',206,282,10);
        text('SWIPE UP TO COMMIT',164,320,10,'#c6d7c3');
      }
    } else if(p==='result') {actor(s.formId,206,167,150);label(s.lastCapture?.result==='captured'?'A NEW COMPANION!':'ADVENTURE COMPLETE',248,13);}
    if(['starter','collection','stats','evolution'].includes(p)) {text('‹',57,197,28,'#9ee2a9');text('›',355,197,28,'#9ee2a9');}
    paintTargets(ctx,targets());
  }
  function paintTargets(ctx,items=targets()) {
    for(const t of items){ctx.fillStyle=t.enabled?'#1b3c37f2':'#25342ce8';ctx.beginPath();ctx.roundRect(t.x,t.y,t.w,t.h,6);ctx.fill();ctx.strokeStyle=t.enabled?'#8abca5':'#526459';ctx.lineWidth=1;ctx.stroke();ctx.fillStyle=t.enabled?'#f5f3df':'#8d9a91';ctx.font=`600 ${t.label.length>17?10:t.w<90?12:13}px Arial`;ctx.textAlign='center';ctx.fillText(t.label,t.x+t.w/2,t.y+t.h/2+5);}
  }
  return {sync,targets,mode,activate,horizontal,battleCommit,back,paint,paintTargets,description,
    page:current,context:()=>revision,browseBand:()=>current()==='home'?[112,282]:[100,280],
    artIds:()=>['collection','stats'].includes(current())?[member()?.formId].filter(Boolean):['evolution','evolution-review'].includes(current())?[option()?.formId].filter(Boolean):[],
    selectTab(tab){if(tab==='box'){memberIndex=0;memberId=state().activeCreatureId;go('collection');}else if(tab==='evolve'){memberIndex=0;memberId=state().activeCreatureId;evolutionIndex=0;go('evolution');}else go(state()?.phase==='egg'?'starter':state()?.phase==='encounter'?'encounter':'home');}
  };
}

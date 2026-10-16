// Browser presentation of the current handheld touch screens. Game actions
// go through the shared native core; gesture geometry is kept in
// device-touch-input.js. Physical sensors and radios are never simulated here.
import { orderedMembers } from './shared/party.js';
import { expeditionView, paintBossAura } from './shared/expedition-presentation.js';

export function createDeviceView(api) {
  let page = 'starter', home = 0, memberIndex = 0, evolutionIndex = 0, move = 0, statsPage = 0;
  let previousPhase, previousSequence, previousStarter, reviewStarter, revision = 0, reviewState = null, proposedMode = null, memberId = null;
  const homes = ['CARE', 'PARTNERS', 'SETTINGS', 'NEARBY', 'DUNGEONS'];
  let expeditionIndex = 0;
  const moves = ['attack', 'heavy', 'magic'];
  const state = () => api.getState();
  const members = () => orderedMembers(state());
  const member = () => members()[memberIndex % Math.max(1,members().length)];
  const options = () => state()?.evolution?.options || [];
  const option = () => options()[evolutionIndex % Math.max(1,options().length)];
  const active = () => state()?.collection?.find(m => m.id === state().activeCreatureId);
  const scenarios = () => state()?.expeditions?.scenarios || [];
  const scenario = () => scenarios()[expeditionIndex % Math.max(1,scenarios().length)];
  const held = () => Number(scenario()?.kind === 'boss' ? state()?.expeditions?.bossSigils : state()?.expeditions?.dungeonKeys) || 0;
  const entryReady = () => state()?.phase === 'home' && !!scenario() && held() >= 1;
  const scene = () => ['dungeons','dungeon-lobby'].includes(current()) ? ({grove:'forest',tide:'beach',ember:'volcanic'}[scenario()?.theme] || 'forest')
    : current() === 'home' ? 'meadow' : current() === 'nearby' ? 'digital' : current() === 'settings' ? 'ruins' : ['collection','stats','evolution','evolution-review'].includes(current()) ? 'forest' : 'meadow';
  // The cumulative curve is native xpForLevel; remaining XP comes directly from
  // writeJson. This is presentation of earned progress, never an XP award.
  function xpProgress() {
    const s = state(), maximum = s.level >= (s.maxLevel || 99);
    const earned = Math.max(0, s.xp - 20 * (s.level - 1) * s.level);
    return {maximum, fraction:maximum ? 1 : Math.max(0, Math.min(1, earned / Math.max(1, earned + s.xpToNext)))};
  }
  function go(next) { page = next; reviewState = null; revision++; api.changed(); }
  function sync() {
    const s=state(); if(!s)return;
    if (previousStarter !== api.getStarter() && page==='starter-review') {page='starter';reviewState=null;revision++;}
    previousStarter=api.getStarter();
    if(previousPhase !== s.phase) {
      page=s.phase==='egg'?'starter':s.phase==='encounter'?'encounter':s.phase==='expedition'?'expedition-next':previousPhase==='encounter'?'result':'home';
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
    const back=()=>add('back','BACK',132,344,148,44);
    const p=current(), m=member(), o=option();
    if(p==='egg') add('meet','MEET PARTNER',116,260,180,54);
    if(p==='starter') {add('choose','CHOOSE',116,292,180,44);back();}
    if(p==='starter-review') {add('hatch','HATCH',116,252,180,52);back();}
    if(p==='home') {
      add('previous','‹',30,134,54,112);add('next','›',328,134,54,112);
      add('home-open',homes[home],116,300,180,46,s.phase==='home');
    }
    if(p==='care') {
      add('feed','FEED',62,218,139,46,s.phase==='home');add('play','PLAY',211,218,139,46,s.phase==='home'&&(s.energy>=5||s.mood>=100));
      add(active()?.injury?'treat':'rest',active()?.injury?'TREAT':'REST',62,278,139,46,s.phase==='home');
      add('toilet','TOILET',211,278,139,46,s.phase==='home'&&(s.toilet>=25||s.careMissed));back();
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
    if(p==='nearby') {add('nearby-battle','BATTLE',116,192,180,50,false);add('nearby-trade','TRADE',116,254,180,50,false);back();}
    if(p==='dungeons') {add('previous','‹',30,134,54,112);add('next','›',328,134,54,112);add('open-lobby','OPEN LOBBY',116,250,180,50,entryReady());back();}
    if(p==='dungeon-lobby') {add('start-solo','START SOLO',116,224,180,50,entryReady());back();}
    if(p==='encounter'){add(s.focus||s.autoCapture===1?'capture':'battle',s.focus?'FOCUS':s.autoCapture===1?'AIM CAPTURE':s.battleMode==='auto'?'AUTO BATTLE':'BATTLE',116,248,180,52);add('retreat','RETREAT',116,314,180,44);}
    if(p==='battle') {
      if(s.battleMode==='auto') {
        add(s.focus||s.autoCapture===1?'capture':'auto-fight',s.focus?'FOCUS':s.autoCapture===1?'AIM CAPTURE':'RUN AUTO',116,254,180,44);
        add('retreat','RETREAT',70,310,130,44);add('back','BACK',212,310,130,44);
      } else {
        add('capture','CATCH',274,287,76,44,api.canCapture());
        add('retreat','RETREAT',62,287,136,44);back();
      }
    }
    if(p==='capture') {
      if(s.focus)add('skip-focus','SKIP FOCUS',132,344,148,44);
      else if(s.autoCapture===1) add('auto-resume','SKIP / RESUME FIGHT',104,348,204,38);
      else back();
    }
    if(p==='expedition-next'){add('next-expedition','NEXT FIGHT',116,246,180,50,s.expeditions?.awaitingNext);add('retreat','LEAVE DUNGEON',116,308,180,44);}
    if(p==='result') add('home','HOME',116,274,180,50);
    return out;
  }
  function back() {
    if(current()==='capture'){api.closeCapture();go('battle');return;}
    if(state()?.phase==='expedition'){go('expedition-next');return;}
    const parents={'starter':'egg','starter-review':'starter','battle':'encounter','stats':'collection','release-review':'stats','evolution':'stats','evolution-review':'evolution','mode-review':'settings','dungeon-lobby':'dungeons'};
    go(parents[page]||'home');
  }
  function horizontal(next) {
    if(!api.canPlay())return;
    const step=next?1:-1, p=current();
    if(p==='starter') {
      const choices=api.getStarters(), i=choices.findIndex(s=>s.id===api.getStarter());
      api.setStarter(choices[(i+step+choices.length)%choices.length].id);
    } else if(p==='home') {home=(home+step+homes.length)%homes.length;}
    else if(p==='dungeons') expeditionIndex=(expeditionIndex+step+scenarios().length)%Math.max(1,scenarios().length);
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
    if(id==='home-open'){if(home===1){memberIndex=0;memberId=state().activeCreatureId;}go(['care','collection','settings','nearby','dungeons'][home]);return;}
    if(id==='skip-focus'){void api.command('focus',2400);return;}
    if(id==='open-lobby'){go('dungeon-lobby');return;}
    if(id==='start-solo'){void api.command('start-expedition',expeditionIndex);return;}
    if(['feed','play','rest','toilet','treat','retreat','auto-fight','auto-resume','next-expedition'].includes(id)){void api.command(id);return;}
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
  function mode() {const p=current();return p==='capture'?'capture':p==='battle'&&state().battleMode==='tactical'?'battle':['starter','home','dungeons','collection','stats','evolution'].includes(p)?'browse':'buttons';}
  function description() {
    const p=current(), s=state();if(!s)return 'Loading the touch demo.';
    const selected=p==='starter'?api.getStarters().find(v=>v.id===api.getStarter())?.name:p==='home'?homes[home]:p==='dungeons'?`${scenario()?.name || 'Unavailable'}, ${held()} ${scenario()?.kind==='boss'?'sigils':'keys'} held`:p==='collection'?member()?.name:p==='evolution'?option()?.name:p==='battle'&&s.battleMode==='tactical'?['Physical','Heavy','Magic'][move]:'';
    return `${p.replaceAll('-',' ')}${selected?`: ${selected}`:''}.${p==='home'?` ${xpProgress().maximum?'Maximum level.':`XP ${Math.round(xpProgress().fraction*100)} percent to next level.`} Health ${s.hp} of ${s.combat.maxHp} HP.`:p==='nearby'?' Battle and Trade need two connected handhelds.':p==='care'?` Health ${s.hp} of ${s.combat.maxHp} HP. Food ${s.fullness} percent, mood ${s.mood} percent, energy ${s.energy} percent.${active()?.injury?' Injured: treat before resting to full health.':''}`:''} ${p==='capture'?'Tap the main area to commit the current timing.':mode()==='battle'?'Swipe or tap left/right to choose a move. Swipe up to use it.':mode()==='browse'?'Swipe or tap left/right to browse. Use the labeled screen buttons to choose.':'Tap a labeled screen button.'}`;
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
    if(p==='care'){ctx.save();ctx.globalAlpha=.56;actor(s.formId,206,206,280);ctx.restore();ctx.fillStyle='#071b204d';ctx.fillRect(0,0,412,412);}
    label(p==='home'?homes[home]:p==='dungeon-lobby'?'LOBBY':p==='nearby'?'FIND NEARBY':p==='dungeons'?(scenario()?.kind==='boss'?'BOSS':'DUNGEON'):p==='battle'||p==='encounter'?`${expeditionView(s)?.label || (s.battleMode==='auto'?'AUTO':'MANUAL') } BATTLE`:p==='expedition-next'?'TIER CLEARED':p.replaceAll('-',' ').toUpperCase(),p==='home'?44:64,11);
    if(['egg','starter','starter-review'].includes(p)) {
      const egg=eggs.get(api.getStarter());if(egg)ctx.drawImage(egg,132,90,148,148);
      label(api.getStarters().find(v=>v.id===api.getStarter())?.name||'Choose an egg',p==='starter-review'?232:270,19);
    } else if(p==='home') {
      label(s.creature,78,19);
      const xp=xpProgress();box(128,89,156,24,'#102b2bef',12);text(`LEVEL ${s.level}${xp.maximum?' · MAX':''}`,206,105,11);
      box(78,116,256,42);
      const hp=s.combat.maxHp>0?Math.max(0,Math.min(1,s.hp/s.combat.maxHp)):0;
      [['XP',123,xp.fraction,'#69b9ff'],['HP',143,hp,'#90e7ae']].forEach(([name,y,fraction,tint])=>{
        text(name,97,y+8,12);box(116,y,210,8,'#265357',3);
        if(fraction>0)box(116,y,210*fraction,8,tint,3);
      });
      actor(s.formId,206,220,116);
      for(let i=0;i<homes.length;i++){ctx.fillStyle=i===home?'#d1ffd9':'#537c72';ctx.beginPath();ctx.arc(174+i*16,282,3,0,Math.PI*2);ctx.fill();}
      box(126,351,160,36);text('LIFETIME STEPS',206,366,10);text('ON HANDHELD',206,379,9,'#c5d4ce');
    } else if(p==='care') {
      box(62,85,288,99,'#102b2be8',12);
      text('HEALTH',110,105,12);text(`${s.hp}/${s.combat.maxHp} HP`,278,105,11);
      box(78,116,256,10,'#071d22',5);if(s.hp>0)box(78,116,256*Math.max(0,Math.min(1,s.hp/s.combat.maxHp)),10,active()?.injury?'#ef766c':'#b5d69c',5);
      [['FOOD',s.fullness,'#efc682'],['MOOD',s.mood,'#b5d69c'],['ENERGY',s.energy,'#8dcfdf']].forEach(([name,value,tint],i)=>{
        const x=78+i*88;text(name,x+40,145,11);
        box(x,154,80,9,'#071d22',4);if(value>0)box(x,154,80*Math.max(0,Math.min(100,value))/100,9,value<25?'#ef766c':tint,4);
      });
      if(active()?.injury)text('INJURED · TREAT BEFORE REST',206,176,9,'#ffd0a0');
    } else if(p==='collection'||p==='stats'||p==='release-review') {
      const m=member();if(m){actor(m.formId,206,150,116);label(m.name,222,18);if(p==='stats'){
        const pages=[`LV ${m.level} · ${m.bond} BOND · ${m.xp} XP`,`HP ${m.hp}/${m.combat.maxHp} · ${m.combat.type.toUpperCase()}`,m.combat.skills.physical,m.combat.skills.magic];
        label(`${statsPage+1}/4 · ${pages[statsPage]}`,257,11);
      }if(p==='release-review')label('This companion will leave your box.',253,11);}
      else label('Hatch a partner first',210,16);
    } else if(p==='evolution'||p==='evolution-review') {
      const o=option();if(o){actor(o.formId,206,150,112);label(o.name,218,17);if(o.formId===30||o.formId===357)text(o.formId===30?'GUARDIAN FORM':'MYSTIC FORM',206,237,10,'#efc682');label(`LEVEL ${s.level}/${o.requiredLevel} · BOND ${s.bond}/${o.requiredBond}`,258,10);}
      else label('No further evolution',210,16);
    } else if(p==='settings'||p==='mode-review') {
      actor(s.formId,206,144,110);label(p==='mode-review'?`CHANGE TO ${proposedMode===0?'MANUAL':'AUTO'}?`:'BATTLE PREFERENCE',214,13);
      if(p==='settings')label('Hardware settings are device-only.',248,10);
    } else if(p==='nearby') {
      actor(s.formId,206,121,66);label('Two connected handhelds required',167,11);
    } else if(p==='dungeons'||p==='dungeon-lobby') {
      const entry=scenario(),boss=entry?.kind==='boss',currency=boss?'Sigils':'Keys';
      label(entry?.name || 'No scenarios available',106,19);
      if(p==='dungeons') {
        actor(s.formId,206,171,84);label(`ENTRY · 1 ${boss?'SIGIL':'KEY'}`,232,11);
        text(`${held()} ${currency} Held`,206,323,12,entryReady()?'#e3f3df':'#d1b8a8');
      } else {
        label(`Spends 1 ${boss?'sigil':'key'} now`,155,13);label(entry?.kind==='dungeon'?'Three fights · health carries over':'Loss or retreat still uses it',182,11);
        text(`${held()} ${currency} Held`,206,300,12);text('Shared play needs two handhelds',206,323,10,'#c6d7c3');
      }
    } else if(p==='encounter'||p==='battle') {
      text(s.creature,118,97,12);text(s.wildName,294,97,12);
      text(`HP ${s.hp}/${s.combat.maxHp}`,118,118,10);text(`HP ${s.wildHp}/${s.wildMaxHp}`,294,118,10);
      const side = 128, y = p==='battle'&&s.battleMode==='tactical'?210:187;
      if(expeditionView(s)?.boss)paintBossAura(ctx,294,y,side,time,reducedMotion);
      actor(s.formId,118,y,side,'right');actor(s.wildFormId,294,y,side,'left');
      if(p==='battle'&&s.battleMode==='tactical') {
        const labels=['PHYSICAL','HEAVY','MAGIC'];
        box(120,122,172,21,'#4e6e52');text(`‹ ${labels[move]} · ↑ USE ›`,206,137,10);

      }
    } else if(p==='expedition-next') {
      const run=expeditionView(s);label(run?.name || 'Dungeon',105,18);
      actor(s.formId,206,173,100);label(`TIER ${run?.tier} / ${run?.tiers} CLEARED`,232,12);
      text(`${s.hp} HP · ${s.energy} ENERGY · NO EXTRA KEY`,206,376,9);
    } else if(p==='result') {actor(s.formId,206,167,150);label(s.lastCapture?.result==='captured'?'A NEW COMPANION!':'ADVENTURE COMPLETE',248,13);}
    if(['starter','collection','stats','evolution'].includes(p)) {text('‹',57,197,28,'#9ee2a9');text('›',355,197,28,'#9ee2a9');}
    paintTargets(ctx,targets());
  }
  function paintTargets(ctx,items=targets()) {
    for(const t of items){ctx.fillStyle=t.enabled?'#1b3c37f2':'#25342ce8';ctx.beginPath();ctx.roundRect(t.x,t.y,t.w,t.h,6);ctx.fill();ctx.strokeStyle=t.enabled?'#8abca5':'#526459';ctx.lineWidth=1;ctx.stroke();ctx.fillStyle=t.enabled?'#f5f3df':'#8d9a91';ctx.font=`600 ${t.label.length>17?10:t.w<90?12:13}px Arial`;ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(t.label,t.x+t.w/2,t.y+t.h/2);ctx.textBaseline='alphabetic';}
  }
  return {sync,targets,mode,activate,horizontal,battleCommit,back,paint,paintTargets,description,
    page:current,context:()=>revision,scene,xpProgress,browseBand:()=>['home','dungeons'].includes(current())?[112,246]:[100,280],
    artIds:()=>['collection','stats'].includes(current())?[member()?.formId].filter(Boolean):['evolution','evolution-review'].includes(current())?[option()?.formId].filter(Boolean):[],
    selectTab(tab){if(tab==='box'){memberIndex=0;memberId=state().activeCreatureId;go('collection');}else if(tab==='evolve'){memberIndex=0;memberId=state().activeCreatureId;evolutionIndex=0;go('evolution');}else go(state()?.phase==='egg'?'starter':state()?.phase==='encounter'?'encounter':state()?.phase==='expedition'?'expedition-next':'home');}
  };
}

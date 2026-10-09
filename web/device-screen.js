// Accessible controls projected onto the 480px round display. No game rules.
import { paintStarterEgg } from './starter-onboarding.js';
export function createDeviceScreen(root, { activate, select, move, paintPortrait = () => {} }) {
  let signature = '';
  let previousSelection = '';
  function memberPortrait(member, size) {
    const canvas = document.createElement('canvas'); canvas.className = 'roster-portrait';
    canvas.width = canvas.height = size; canvas.setAttribute('role', 'img');
    canvas.setAttribute('aria-label', member.artAvailable ? `${member.name} artwork` : `${member.name}: artwork pending; original placeholder`);
    paintPortrait(canvas, member); return canvas;
  }
  function memberHealth(member) {
    const health = document.createElement('span'); health.className = 'roster-health';
    const label = document.createElement('span'); label.textContent = `HP ${member.hp} / ${member.maxHp}`;
    const meter = document.createElement('span'); meter.className = 'roster-health-track'; meter.setAttribute('aria-hidden', 'true');
    const fill = document.createElement('span'); fill.style.width = `${Math.max(0, Math.min(100, member.hp / member.maxHp * 100))}%`; meter.append(fill);
    health.append(label, meter); return health;
  }
  function memberTag(text, className) {
    const tag = document.createElement('span'); tag.className = className; tag.textContent = text; return tag;
  }
  function eggPortrait(starter) {
    if (starter.offered && starter.artId) return memberPortrait(starter, 128);
    const canvas = document.createElement('canvas'); canvas.className = 'starter-egg';
    canvas.width = canvas.height = 128; canvas.setAttribute('role', 'img');
    canvas.setAttribute('aria-label', `Original decorated egg; hatches into ${starter.name}`);
    paintStarterEgg(canvas, starter.id); return canvas;
  }
  root.addEventListener('click', event => {
    const direction = event.target.closest('[data-device-move]');
    if (direction && !direction.disabled) { move(Number(direction.dataset.deviceMove)); return; }
    const button = event.target.closest('[data-device-action]');
    if (!button || button.disabled) return;
    if (button.dataset.deviceIndex !== undefined) select(Number(button.dataset.deviceIndex));
    activate(button.dataset.deviceAction, event);
  });
  function render(view) {
    const next = JSON.stringify({ ...view, index: undefined });
    if (next !== signature) {
      signature = next;
      root.replaceChildren();
      root.dataset.screen = view.screen;
      root.dataset.controls = view.inputMode || 'buttons';
      root.dataset.scene = String(Boolean(view.scene));
      root.dataset.layout = view.layout || 'list';
      root.dataset.hasEyebrow = String(Boolean(view.eyebrow));
      root.dataset.longTitle = String(view.title.length > 18);
      if (view.battleMode) root.dataset.battleMode = view.battleMode; else delete root.dataset.battleMode;
      if (view.autoStep) { root.dataset.autoStep = String(view.autoStep.turn); root.dataset.autoTotal = String(view.autoStep.total); }
      else { delete root.dataset.autoStep; delete root.dataset.autoTotal; }
      if (view.egg) root.dataset.starterId = String(view.egg.id); else delete root.dataset.starterId;
      if (view.creatureType) root.dataset.creatureType = view.creatureType; else delete root.dataset.creatureType;
      if (view.encounterRarity !== undefined) root.dataset.encounterRarity = view.encounterRarity || 'legacy'; else delete root.dataset.encounterRarity;
      if (view.walkProgress) { root.dataset.queuedEncounters = String(view.walkProgress.queued); root.dataset.stepsToNextEncounter = String(view.walkProgress.remaining); }
      else { delete root.dataset.queuedEncounters; delete root.dataset.stepsToNextEncounter; }
      if (view.graph) { root.dataset.graphFocusId = String(view.graph.focusFormId); root.dataset.graphOffset = String(view.graph.offset); root.dataset.graphTotal = String(view.graph.total); if (view.graph.nodeId) root.dataset.graphNodeId = String(view.graph.nodeId); else delete root.dataset.graphNodeId; }
      else { delete root.dataset.graphFocusId; delete root.dataset.graphOffset; delete root.dataset.graphTotal; delete root.dataset.graphNodeId; }
      if (view.catalogFormId) root.dataset.catalogFormId = String(view.catalogFormId); else delete root.dataset.catalogFormId;
      if (view.member) { root.dataset.memberId = String(view.member.id); root.dataset.current = String(view.member.current); }
      else { delete root.dataset.memberId; delete root.dataset.current; }
      if (view.progression) {
        root.dataset.progressionMember = String(view.progression.memberId); root.dataset.formId = String(view.progression.formId);
        root.dataset.xp = String(view.progression.xp); root.dataset.level = String(view.progression.level);
      } else { delete root.dataset.progressionMember; delete root.dataset.formId; delete root.dataset.xp; delete root.dataset.level; }
      const header = document.createElement('header'); header.className = 'screen-heading';
      const eyebrow = document.createElement('span'); eyebrow.textContent = view.eyebrow || 'DIGIVICE';
      const title = document.createElement('h2'); title.id = 'device-screen-title'; title.textContent = view.title;
      header.append(eyebrow, title); root.append(header);
      const content = document.createElement('div'); content.className = 'screen-content';
      if (view.egg) content.append(eggPortrait(view.egg));
      if (view.portrait) { const portrait = memberPortrait(view.portrait, 128); portrait.classList.add('screen-portrait'); content.append(portrait); }
      if (view.member) {
        const member = view.member;
        const hero = document.createElement('div'); hero.className = 'member-hero'; hero.dataset.creatureType = member.type;
        hero.append(memberPortrait(member, 96));
        const summary = document.createElement('div'); summary.className = 'member-identity';
        const tags = document.createElement('div'); tags.className = 'member-tags';
        tags.append(memberTag(member.typeLabel, 'member-type'));
        if (member.current) tags.append(memberTag('PARTNER', 'partner-badge'));
        summary.append(tags, memberTag(`${member.family} · ${member.stageLabel || `Stage ${member.stage}`}`, 'member-family'), memberHealth(member));
        if (!member.artAvailable) summary.append(memberTag(member.pendingForm ? 'Artwork pending' : 'Art not saved', 'roster-art-missing'));
        hero.append(summary); content.append(hero);
      }
      if (view.detail) { const detail = document.createElement('p'); detail.className = 'screen-detail'; detail.dataset.notice = view.notice || ''; detail.textContent = view.detail; content.append(detail); }
      if (view.stats?.length) {
        const stats = document.createElement('dl'); stats.className = 'screen-stats';
        for (const [label, value, statKey] of view.stats) {
          const item = document.createElement('div'); const term = document.createElement('dt'); term.textContent = label;
          const definition = document.createElement('dd'); definition.textContent = value; if (statKey) definition.dataset.stat = statKey; item.append(term, definition); stats.append(item);
        }
        content.append(stats);
      }
      if (view.facts?.length) {
        const facts = document.createElement('dl'); facts.className = 'screen-facts';
        for (const [label, value] of view.facts) {
          const item = document.createElement('div'); const term = document.createElement('dt'); term.textContent = label;
          const definition = document.createElement('dd'); definition.textContent = value; item.append(term, definition); facts.append(item);
        }
        content.append(facts);
      }
      if (view.progress) {
        const progress = document.createElement('progress'); progress.className = 'screen-progress'; progress.max = view.progress.total; progress.value = view.progress.value; progress.setAttribute('aria-label', 'Verified artwork download');
        const bytes = document.createElement('p'); bytes.className = 'screen-progress-label'; bytes.textContent = `${(view.progress.value / 1024).toFixed(1)} / ${(view.progress.total / 1024).toFixed(1)} KiB`;
        content.append(progress, bytes);
      }
      const choices = document.createElement('div'); choices.className = 'screen-choices';
      view.items.forEach((item, index) => {
        const button = document.createElement('button'); button.type = 'button';
        button.dataset.deviceAction = item.id; button.dataset.deviceIndex = String(index); button.disabled = Boolean(item.disabled);
        if (item.formId) button.dataset.formId = String(item.formId);
        if (typeof item.eligible === 'boolean') button.dataset.eligible = String(item.eligible);
        button.className = 'screen-choice';
        if (view.layout?.includes('carousel') || view.focusActions) {
          const counter = document.createElement('span'); counter.className = 'screen-choice-count'; counter.textContent = `${index + 1} / ${view.items.length}`; button.append(counter);
        }
        if (item.egg) {
          button.dataset.starterId = String(item.egg.id);
          button.append(eggPortrait(item.egg));
          const name = document.createElement('strong'); name.dataset.starterName = ''; name.textContent = item.egg.name;
          const result = document.createElement('span'); result.className = 'starter-result'; result.textContent = `Hatches into ${item.egg.name}`;
          const stage = document.createElement('small'); stage.dataset.starterStage = ''; stage.textContent = item.egg.stage;
          button.append(name, result, stage); choices.append(button); return;
        }
        if (item.member) {
          const member = item.member;
          button.classList.add('roster-card'); button.dataset.memberId = String(member.id);
          button.dataset.current = String(member.current); button.dataset.creatureType = member.type;
          button.setAttribute('aria-label', `Companion #${member.id}, ${member.name}, ${member.typeLabel}, ${member.stageLabel || `stage ${member.stage}`}, ${member.hp} of ${member.maxHp} health${member.current ? ', current partner' : ''}`);
          button.append(memberPortrait(member, 64));
          const summary = document.createElement('span'); summary.className = 'roster-summary';
          const title = document.createElement('span'); title.className = 'roster-title';
          title.append(memberTag(`#${String(member.id).padStart(2, '0')}`, 'roster-id'), memberTag(member.name, 'roster-name'));
          if (member.current) title.append(memberTag('PARTNER', 'partner-badge'));
          summary.append(title, memberTag(`${member.typeLabel} · ${member.stageLabel || `Stage ${member.stage}`}${!member.artAvailable ? member.rookie ? ' · Artwork pending' : ' · Art not saved' : ''}`, 'roster-meta'), memberHealth(member));
          button.append(summary); choices.append(button); return;
        }
        if (item.icon) { const icon = document.createElement('span'); icon.className = 'screen-choice-icon'; icon.textContent = item.icon; icon.setAttribute('aria-hidden', 'true'); button.append(icon); }
        const label = document.createElement('span'); label.className = 'screen-choice-label'; label.textContent = item.label; button.append(label);
        if (item.detail) { const detail = document.createElement('small'); detail.textContent = item.detail; button.append(detail); }
        choices.append(button);
      });
      content.append(choices);
      if (view.inputMode === 'touch' && view.footer) {
        const note = document.createElement('p'); note.className = 'screen-touch-note';
        note.textContent = view.footer.replace(/HOLD BACK/g, 'BACK').replace(/HOLD L TO CHANGE/g, 'BACK TO CHANGE').replace(/L NEXT · R (?:USE|CHOOSE|CONFIRM)/gi, 'TAP A CHOICE'); content.append(note);
      }
      root.append(content);
      if (view.meter) { const meter = document.createElement('div'); meter.className = 'screen-meter'; meter.textContent = view.meter; root.append(meter); }
      const footer = document.createElement('div'); footer.className = 'screen-footer';
      if (view.inputMode === 'touch' && view.items.length > 1) {
        for (const [delta, label, id] of [[-1, '‹ Prev', 'device-previous'], [1, 'Next ›', 'device-next']]) {
          const button = document.createElement('button'); button.type = 'button'; button.id = id;
          button.dataset.deviceMove = String(delta); button.textContent = label;
          button.setAttribute('aria-label', delta < 0 ? 'Previous choice' : 'Next choice');
          button.disabled = view.items.filter(item => !item.disabled).length < 2;
          footer.append(button);
        }
      }
      if (view.back) {
        const back = document.createElement('button'); back.id = 'device-back'; back.type = 'button'; back.dataset.deviceAction = 'back'; back.textContent = view.backLabel || '‹ Back'; back.disabled = Boolean(view.backDisabled); footer.append(back);
      }
      if (view.footer && view.inputMode !== 'touch') { const text = document.createElement('span'); text.textContent = view.footer; footer.append(text); }
      root.append(footer);
    }
    for (const button of root.querySelectorAll('[data-device-index]')) {
      const selected = Number(button.dataset.deviceIndex) === (view.inputMode === 'touch' && view.index < 0 ? 0 : view.index);
      button.dataset.selected = String(selected);
      if (view.inputMode === 'touch' || view.layout?.includes('carousel') || view.focusActions) button.hidden = !selected;
      if (selected) button.setAttribute('aria-current', 'true'); else button.removeAttribute('aria-current');
    }
    const selection = `${view.screen}:${view.index}:${view.inputMode}`;
    if (view.inputMode === 'touch' && selection !== previousSelection) {
      // A swipe through long details must not hide the next choice offscreen.
      const content = root.querySelector('.screen-content');
      if (content) content.scrollTop = 0;
    }
    previousSelection = selection;
  }
  return { render };
}

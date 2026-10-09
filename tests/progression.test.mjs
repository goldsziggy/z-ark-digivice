import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { validateEvolutionGraph, validateEvolutionOptions, evolutionRequirements } from '../web/progression.js';
import { fetchEvolutionGraph } from '../web/roster-client.js';
const native = (args, input = '') => JSON.parse(execFileSync(process.env.DIGIVICE_TEST_CORE_PATH ?? 'build/digivice-core', args, { input, encoding: 'utf8', maxBuffer: 65536 }));
const graph = (id, offset = 0) => validateEvolutionGraph(native(['--evolution-graph', String(id), String(offset), '8']), id, offset);

test('all eight starters expose actual outgoing choices independently of historical lineage', () => {
  for (const starter of native(['--starters']).starters) {
    const state = native(['--replay-onboarding', '12345'], `hatch ${starter.id}\n`);
    const choices = validateEvolutionOptions(state.evolution.options);
    const pages = []; let offset = 0;
    do { const page = graph(state.formId, offset); pages.push(page); offset = page.nextOffset; } while (offset !== null);
    const nodes = pages.flatMap(page => page.forms);
    const root = nodes.find(form => form.formId === state.formId);
    assert.equal(root.stage, 'Rookie'); assert.equal(root.children.length, 2);
    assert.deepEqual(choices.map(choice => choice.formId), root.children);
    assert.ok(choices.every(choice => choice.eligible === false));
    assert.match(evolutionRequirements(state.collection[0], choices[0])[0][1], /needed/);
    for (const choice of choices) assert.deepEqual(root.edges.find(edge => edge.toFormId === choice.formId), { toFormId: choice.formId, requiredLevel: choice.requiredLevel, requiredBond: choice.requiredBond });
    assert.equal(nodes.length, pages[0].total); assert.equal(new Set(nodes.map(form => form.formId)).size, nodes.length);
  }
});

test('merged components page beyond seven forms and expose multiple incoming edges', () => {
  const pages = []; let offset = 0;
  do { const page = graph(15, offset); pages.push(page); offset = page.nextOffset; } while (offset !== null);
  assert.ok(pages[0].total > 16);
  assert.equal(pages.flatMap(page => page.forms).length, pages[0].total);
  assert.ok(pages.every(page => page.forms.length <= 8));
  assert.deepEqual(pages.flatMap(page => page.forms).find(form => form.formId === 15).parents, [11, 84, 116]);
  const empty = validateEvolutionGraph(native(['--evolution-graph', '15', String(pages[0].total), '8']), 15, pages[0].total);
  assert.deepEqual(empty.forms, []); assert.equal(empty.nextOffset, null);
});

test('bounded graph metadata rejects mismatched versions, pagination and malformed edges', async () => {
  const source = graph(15);
  for (const change of [x => x.rulesVersion = 5, x => x.catalogVersion = 1, x => x.focusFormId = 18, x => x.total = 513,
    x => x.nextOffset = 999, x => x.forms.push(x.forms[0]), x => x.forms[0].parents.push(x.forms[0].formId),
    x => x.forms[0].combat.maxHp = 401, x => x.forms[0].previewLevel = 21, x => x.forms[0].artId = '../private.png',
    x => x.forms[0].edges[0].requiredBond = 201, x => x.forms[0].edges[0].toFormId = 512]) {
    const bad = structuredClone(source); change(bad); assert.throws(() => validateEvolutionGraph(bad, 15));
  }
  let path;
  await fetchEvolutionGraph(15, 0, async value => { path = value; return Response.json(source); });
  assert.equal(path, '/api/evolution-graph?formId=15&offset=0&limit=8');
  await assert.rejects(fetchEvolutionGraph(15, 0, async () => new Response('x'.repeat(65537))), /large/);
  await assert.rejects(fetchEvolutionGraph(513), /position/);
  const options = native(['--replay-onboarding', '12345'], 'hatch 1\n').evolution.options; options[0].eligible = 'true';
  assert.throws(() => validateEvolutionOptions(options));
});

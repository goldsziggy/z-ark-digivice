import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';

// Run the real browser retry function with bounded service/storage adapters.
// A rejected old command must survive byte-for-byte while the current save loads.
const source = await readFile(new URL('../web/app.js', import.meta.url), 'utf8');
const functionSource = source.slice(source.indexOf('async function retryPending() {'), source.indexOf('\nasync function discardConflict() {'));
assert.match(source, /const RULES_VERSION = 22;/);
assert.match(source, /const SCHEMA_VERSION = 30;/);
for (const marker of [undefined, 19, 20, 21]) test(`old pending marker ${marker ?? 'missing'} is preserved during rules22 reconciliation`, async () => {
  const pending = {deviceId: 'dv_fixture', ...(marker === undefined ? {} : {rulesVersion: marker}), baseRevision: 12,
    batchId: 'before-rules22-pending', events: [{type: 'evolve', value: 33}], subjectMemberId: 1};
  const raw = JSON.stringify(pending);
  let notices = [], calls = [], accepted;
  const context = {pending, busy: false, transitionBusy: false, identity: {deviceId: 'dv_fixture', token: 'synthetic'},
    ownsWriter: true, storageAvailable: true, game: {activeCreatureId: 1}, revision: 12, conflict: false,
    captureFlight: null, RULES_VERSION: 22, structuredClone, render() {}, showNotice: (...values) => notices.push(values),
    clearPending() { throw Error('An old request must not be discarded automatically'); },
    acceptSave(save) { accepted = save; },
    async api(path, options) {
      calls.push({path, options});
      if (path === '/api/save-sync') throw Object.assign(Error('Old rules'), {status: 409, code: 'migration_required'});
      assert.equal(path, '/api/save'); return {revision: 13, state: {rulesVersion: 22, schemaVersion: 30}};
    },
  };
  vm.createContext(context); vm.runInContext(functionSource, context);
  await context.retryPending();
  assert.equal(JSON.stringify(context.pending), raw);
  assert.equal(calls[0].options.body.rulesVersion, marker);
  assert.equal(calls.filter(call => call.path === '/api/save-sync').length, 1);
  assert.equal(accepted.state.rulesVersion, 22);
  assert.equal(context.conflict, true); assert.equal(context.busy, false);
  assert.match(notices.at(-1)[0], /earlier-rules request was not replayed/);
});

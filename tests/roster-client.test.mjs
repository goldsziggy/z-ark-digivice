import test from 'node:test';
import assert from 'node:assert/strict';
import { createRosterService } from '../service/roster-service.ts';
import { validateRosterPage, validateRosterDetail, fetchRosterPage, fetchRosterIds, rosterReferences, rosterEncyclopediaMoves } from '../web/roster-client.js';
import { createFormArt } from '../web/form-art.js';
import { validatePersonalImport } from '../web/personal-pack.js';
import { personalFixture, encodeFixture } from './personal-art.test.mjs';

const service = createRosterService({ rootDir: process.cwd() });
test('encyclopedia move references retain official identity and variant qualification without changing combat skills', () => {
  const falcomon = service.detail(89, new URLSearchParams()).form, before = structuredClone(falcomon.combat.skills);
  const references = rosterEncyclopediaMoves(falcomon);
  assert.ok(references.length > 0); assert.ok(references.every(move => move.sourceName === falcomon.official.name && move.variantReviewRequired));
  assert.ok(references.every(move => move.detail.includes('Sheet identity needs review')));
  assert.ok(references.every(move => move.url.startsWith('https://digimon.net/reference_en/')));
  assert.deepEqual(falcomon.combat.skills, before);
  const longest = service.detail(272, new URLSearchParams()).form;
  assert.ok(rosterEncyclopediaMoves(longest).some(move => move.name === 'Blood Dance: Maracas Version'));
});
test('untrusted or malformed move metadata never becomes an encyclopedia reference', () => {
  const original = service.detail(18, new URLSearchParams()).form;
  for (const mutate of [x => x.official = null, x => x.official.name = null, x => x.official.url = 'https://github.com/example/data',
    x => x.official.url = 'javascript:alert(1)', x => x.official.url = 'https://digimon.net@evil.example/reference_en/detail.php?directory_name=x',
    x => x.official.sheetVariantReviewRequired = undefined, x => x.official.specialMoves = 'Pepper Breath',
    x => x.official.specialMoves = Array(9).fill('Pepper Breath'), x => x.official.specialMoves = [null],
    x => x.official.specialMoves = ['x'.repeat(65)], x => x.official.specialMoves = ['<script>'],
    x => x.official.specialMoves = ['Pepper\nBreath']]) {
    const form = structuredClone(original); mutate(form); assert.deepEqual(rosterEncyclopediaMoves(form), []);
  }
});
test('all source entries remain individually paged and high form IDs retain native authored metadata', () => {
  const ids = [];
  let total = 0;
  for (let offset = 0; offset < 512; offset += 8) {
    const page = validateRosterPage(service.list(new URLSearchParams({ offset: String(offset), limit: '8' })));
    if (offset === 0) total = page.total;
    assert.equal(page.total, total); assert.ok(page.entries.length <= 8); ids.push(...page.entries.map(row => row.formId));
    if (ids.length >= total) break;
  }
  assert.equal(total, 444);
  assert.equal(new Set(ids).size, total);
  const raw = service.detail(276, new URLSearchParams());
  const form = validateRosterDetail(raw, 276);
  assert.equal(form.name, 'Calumon'); assert.equal(form.art.status, 'unavailable'); assert.equal(form.canonicalEvolutionClaim, false);
  assert.deepEqual(form.combat, raw.form.combat); assert.deepEqual(form.growth, raw.form.growth);
  assert.ok(rosterReferences(form).every(reference => reference.url.startsWith('https:')));
  const journal = validateRosterPage(service.list(new URLSearchParams({ ids: '11,276', limit: '8' })));
  assert.deepEqual(journal.entries.map(row => row.formId), [11, 276]);
});

test('catalog requests have bounded pages and reject malformed resources before display', async () => {
  let path;
  await fetchRosterPage({ prefix: 'c', stage: 'No Level' }, async value => { path = value; return Response.json(service.list(new URL(value, 'http://localhost').searchParams)); });
  assert.match(path, /limit=8/); assert.match(path, /prefix=c/); assert.match(path, /stage=No\+Level/);
  await assert.rejects(fetchRosterIds(Array.from({ length: 9 }, (_, index) => index + 1)), /journal/);
  await assert.rejects(fetchRosterPage({ q: 'x'.repeat(65) }), /filter/);
  await assert.rejects(fetchRosterPage({}, async () => new Response('x'.repeat(65537))), /large/);
  const bad = service.detail(276, new URLSearchParams()); bad.form.formId = 513;
  assert.throws(() => validateRosterDetail(bad, 276));
});

function formFixture(id) {
  const fixture = personalFixture(32), key = `ds-form-${id}`;
  fixture.pack.packId = fixture.provenance.packId = `personal-${key}`;
  fixture.pack.sprites[key] = fixture.pack.sprites.mote; delete fixture.pack.sprites.mote;
  fixture.provenance.coverage[key] = fixture.provenance.coverage.mote; delete fixture.provenance.coverage.mote;
  const [packText, provenanceText] = encodeFixture(fixture);
  return { formId: id, artId: key, version: 1, sha256: fixture.provenance.packSha256, packText, provenanceText };
}
const settle = async predicate => { for (let n = 0; n < 100 && !predicate(); n++) await new Promise(resolve => setTimeout(resolve, 2)); assert.ok(predicate()); };
test('exact per-form imports cannot rename another appearance and leave ordinary imports strict', async () => {
  const fixture = formFixture(276);
  await assert.rejects(validatePersonalImport(fixture.packText, fixture.provenanceText), /known creature/);
  await assert.rejects(validatePersonalImport(fixture.packText, fixture.provenanceText, { expectedArtId: 'ds-form-18' }), /known creature/);
  const good = await validatePersonalImport(fixture.packText, fixture.provenanceText, { expectedArtId: 'ds-form-276' });
  assert.deepEqual(Object.keys(good.pack.sprites), ['ds-form-276']);
});

test('private artwork loads only requested forms with two-pack memory and no absent-art retry loop', async () => {
  const requests = [];
  const client = createFormArt({ getCredential: () => ({ deviceId: 'test', token: 'local-test-token' }),
    fetcher: async (path, options) => {
      requests.push(path); assert.equal(options.credentials, 'omit'); assert.equal(options.headers.Authorization, 'Bearer local-test-token');
      const id = Number(path.split('/').at(-1)); return id === 276 ? new Response('', { status: 404 }) : Response.json(formFixture(id));
    } });
  assert.deepEqual(requests, []);
  client.select(Array.from({ length: 60 }, (_, index) => index + 18)); await settle(() => client.state().loaded.length === 2);
  assert.deepEqual(requests, ['/api/roster/art/18', '/api/roster/art/19']);
  assert.deepEqual(client.state().requested, [18, 19], 'sixty proposed forms never bypass the two-pack request/cache limit');
  client.select([276]); await settle(() => client.state().unavailable.includes(276));
  for (let n = 0; n < 10; n++) client.select([276]);
  assert.equal(requests.length, 3); assert.deepEqual(client.state().loaded, []);
  client.setEnabled(false); client.select([20]); await new Promise(resolve => setTimeout(resolve, 5)); assert.equal(requests.length, 3);
  client.setEnabled(true); await settle(() => client.state().loaded.includes(20)); assert.ok(client.get(20));
});

test('GrapLeomon artwork uses only its verified same-species alias after exact 404', async () => {
  for (const [requested, alternate] of [[30, 357], [357, 30]]) {
    const requests = [];
    const client = createFormArt({ getCredential: () => ({ deviceId: 'test', token: 'local-test-token' }),
      fetcher: async path => {
        requests.push(path); const id = Number(path.split('/').at(-1));
        return id === requested ? new Response('', { status: 404 }) : Response.json(formFixture(id));
      } });
    client.select([requested]); await settle(() => client.state().loaded.includes(requested));
    assert.equal(client.sourceId(requested), alternate);
    assert.deepEqual(Object.keys(client.get(requested).sprites), [`ds-form-${alternate}`]);
    assert.deepEqual(requests, [`/api/roster/art/${requested}`, `/api/roster/art/${alternate}`]);
    client.select([requested]); assert.equal(requests.length, 2);
    client.setEnabled(false); assert.equal(client.sourceId(requested), null); assert.equal(client.get(requested), null);
  }
});

test('exact GrapLeomon art wins and malformed exact packs never borrow another identity', async () => {
  for (const outcome of ['exact', '503', 'identity', 'corrupt', 'missing-both']) {
    const requests = [];
    const client = createFormArt({ getCredential: () => ({ deviceId: 'test', token: 'local-test-token' }),
      fetcher: async path => {
        requests.push(path); const id = Number(path.split('/').at(-1));
        if (outcome === '503') return new Response('', { status: 503 });
        if (outcome === 'missing-both') return new Response('', { status: 404 });
        const body = formFixture(outcome === 'identity' ? 357 : id);
        if (outcome === 'corrupt') body.sha256 = '0'.repeat(64);
        return Response.json(body);
      } });
    client.select([30]); await settle(() => !client.state().pending);
    assert.equal(requests.length, outcome === 'missing-both' ? 2 : 1);
    if (outcome === 'exact') { assert.equal(client.sourceId(30), 30); assert.ok(client.get(30).sprites['ds-form-30']); }
    else { assert.equal(client.get(30), null); assert.equal(client.sourceId(30), null); assert.deepEqual(client.state().unavailable, [30]); }
  }
  const requests = [];
  const client = createFormArt({ getCredential: () => ({ deviceId: 'test', token: 'local-test-token' }),
    fetcher: async path => { requests.push(path); return new Response('', { status: 404 }); } });
  client.select([31]); await settle(() => client.state().unavailable.includes(31));
  assert.deepEqual(requests, ['/api/roster/art/31']);
});

test('GrapLeomon alias responses keep source identity validation and two visible-pack bounds', async () => {
  const requests = [];
  const client = createFormArt({ getCredential: () => ({ deviceId: 'test', token: 'local-test-token' }),
    fetcher: async path => {
      requests.push(path); const id = Number(path.split('/').at(-1));
      return id === 30 ? new Response('', { status: 404 }) : Response.json(formFixture(id));
    } });
  client.select([30, 18, 19]); await settle(() => client.state().loaded.length === 2);
  assert.deepEqual(client.state().loaded, [30, 18]); assert.equal(client.sourceId(30), 357);
  assert.deepEqual(requests, ['/api/roster/art/30', '/api/roster/art/357', '/api/roster/art/18']);
  client.select([18]); assert.equal(client.get(30), null); assert.equal(client.sourceId(30), null);
  const invalid = createFormArt({ getCredential: () => ({ deviceId: 'test', token: 'local-test-token' }),
    fetcher: async path => path.endsWith('/30') ? new Response('', { status: 404 }) : Response.json(formFixture(31)) });
  invalid.select([30]); await settle(() => invalid.state().unavailable.includes(30));
  assert.equal(invalid.get(30), null); assert.equal(invalid.sourceId(30), null);
});


test('delayed exact 404 does not fetch fallback after target, private-art or account changes', async () => {
  for (const change of ['target', 'disabled', 'account']) {
    let credential = { deviceId: 'original', token: 'original-token' }, releaseExact;
    const exact = new Promise(resolve => { releaseExact = resolve; }), requests = [];
    const client = createFormArt({ getCredential: () => credential,
      fetcher: async (path, options) => {
        requests.push([path, options.headers.Authorization]);
        if (requests.length === 1) return exact;
        return Response.json(formFixture(Number(path.split('/').at(-1))));
      } });
    client.select([30]); assert.equal(requests.length, 1);
    if (change === 'target') client.select([18]);
    else if (change === 'disabled') client.setEnabled(false);
    else { credential = { deviceId: 'replacement', token: 'replacement-token' }; client.select([30]); }
    releaseExact(new Response('', { status: 404 }));
    await settle(() => !client.state().pending);
    assert.equal(requests.some(([path]) => path.endsWith('/357')), false, `${change}: obsolete request never fetches alias`);
    if (change === 'disabled') { assert.equal(requests.length, 1); assert.deepEqual(client.state().loaded, []); }
    else if (change === 'target') { assert.deepEqual(requests[1], ['/api/roster/art/18', 'Bearer original-token']); assert.equal(client.sourceId(18), 18); }
    else { assert.deepEqual(requests[1], ['/api/roster/art/30', 'Bearer replacement-token']); assert.equal(client.sourceId(30), 30); }
  }
});

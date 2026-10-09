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
test('all255 entries remain individually paged and high form IDs retain native authored metadata', () => {
  const ids = [];
  for (let offset = 0; offset < 255; offset += 8) {
    const page = validateRosterPage(service.list(new URLSearchParams({ offset: String(offset), limit: '8' })));
    assert.equal(page.total, 255); assert.ok(page.entries.length <= 8); ids.push(...page.entries.map(row => row.formId));
  }
  assert.equal(new Set(ids).size, 255);
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
  client.select([18, 19, 20]); await settle(() => client.state().loaded.length === 2);
  assert.deepEqual(requests, ['/api/roster/art/18', '/api/roster/art/19']);
  client.select([276]); await settle(() => client.state().unavailable.includes(276));
  for (let n = 0; n < 10; n++) client.select([276]);
  assert.equal(requests.length, 3); assert.deepEqual(client.state().loaded, []);
  client.setEnabled(false); client.select([20]); await new Promise(resolve => setTimeout(resolve, 5)); assert.equal(requests.length, 3);
  client.setEnabled(true); await settle(() => client.state().loaded.includes(20)); assert.ok(client.get(20));
});

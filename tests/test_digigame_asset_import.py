"""Original generated pixels only; no upstream code, network, or artwork needed."""
from contextlib import contextmanager, ExitStack
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('digigame_import', ROOT / 'scripts/import-digigame-assets.py')
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


class DigiGameImportTests(unittest.TestCase):
    def setUp(self):
        image = Image.new('RGBA', (20 * 12, 28))
        for frame in range(12):
            for y in range(6, 25):
                for x in range(5, 15):
                    image.putpixel((frame * 20 + x, y), (0, 0, 0, 255) if x == 5 else (48, 160, 80, 255))
        encoded = io.BytesIO(); image.save(encoded, format='PNG'); image.close()
        self.png = encoded.getvalue()
        self.field = dict(source_kind='official_ds', cell_width=20, cell_height=28,
                          frame_count=12, frames_per_direction=3,
                          canonical_runtime_order=importer.ORDER,
                          canonical_runtime_phases=importer.PHASES,
                          field_path='res://assets/characters/synthetic/field.png')
        self.entry = dict(formId=200, name='Synthetic', sourceSlug='synthetic',
                          pngSha256=importer.digest(self.png), fieldSha256='', direction='down_left',
                          frameMs=180, browserResize='none', creator='Original test author',
                          game='Original test fixture', inspection=dict(reviewer='Test fixture', notes='Original test pixels.'))
        self.commit = 'a' * 40
        self.baseline = dict(formatVersion=1, collection='digimon-world-ds', version=10,
                             privateOnly=True, entries=[dict(artId='ds-form-18')])
        self.catalog = dict(entries=[dict(formId=200, displayName='Synthetic'), dict(formId=18, displayName='Existing')])

    def mapping(self, field=None, entry=None, png=None):
        data = importer.canonical(field or self.field)
        row = copy.deepcopy(entry or self.entry); row['fieldSha256'] = importer.digest(data)
        return importer.prepare_mapping(row, self.commit, png or self.png, data)

    def plan(self):
        row = copy.deepcopy(self.entry); row['fieldSha256'] = importer.digest(importer.canonical(self.field))
        return dict(formatVersion=1, sourceRepository=importer.REPOSITORY, sourceCommit=self.commit,
                    cohort='original-test', baselineIndexSha256=importer.digest(importer.canonical(self.baseline)),
                    entries=[row])

    def test_exact_direction_triplet_and_honest_fallback(self):
        entry = copy.deepcopy(self.entry); entry['direction'] = 'down_right'
        mapping, audit = self.mapping(entry=entry)
        sprite = mapping['sprites']['ds-form-200']
        self.assertEqual(sprite['animations']['idle']['rects'], [[60, 0, 20, 28], [80, 0, 20, 28], [100, 0, 20, 28]])
        self.assertTrue(all(sprite['animations'][name] == {'fallback': 'idle'} for name in importer.pipeline.importer.ANIMATIONS[1:]))
        self.assertIn(self.commit, mapping['provenance']['sourceUrl'])
        self.assertFalse(audit['originalSourceBytesIndependentlyVerified'])

    def test_both_known_metadata_spellings_but_no_conflicting_order(self):
        field = copy.deepcopy(self.field); field['directions'] = field.pop('canonical_runtime_order')
        self.mapping(field)
        field['canonical_runtime_order'] = list(reversed(importer.ORDER))
        with self.assertRaises(importer.Failure): self.mapping(field)

    def test_reject_mixed_origin_and_missing_phase_contract(self):
        for replacement in [dict(source_kind='community'), dict(canonical_runtime_phases=[]), dict(frame_count=9)]:
            field = {**self.field, **replacement}
            with self.assertRaises(importer.Failure): self.mapping(field)

    def test_hash_identity_and_geometry_must_agree(self):
        with self.assertRaises(importer.Failure): self.mapping(png=self.png + b'changed')
        with self.assertRaises(importer.Failure): self.mapping({**self.field, 'cell_width': 19})
        with self.assertRaises(importer.Failure): self.mapping({**self.field, 'field_path': 'res://assets/characters/other/field.png'})

    def test_frozen_unknown_and_duplicate_forms_are_rejected(self):
        good = self.plan(); importer.validate_plan(good, self.catalog, self.baseline)
        for ident in [18, 512]:
            plan = copy.deepcopy(good); plan['entries'][0]['formId'] = ident
            with self.assertRaises(importer.Failure): importer.validate_plan(plan, self.catalog, self.baseline)
        plan = copy.deepcopy(good); plan['entries'] *= 2
        with self.assertRaises(importer.Failure): importer.validate_plan(plan, self.catalog, self.baseline)
        plan = copy.deepcopy(good); plan['entries'][0]['sourceSlug'] = '../tools'
        with self.assertRaises(importer.Failure): importer.validate_plan(plan, self.catalog, self.baseline)

    def test_git_reads_only_pinned_data_and_refuses_large_blob(self):
        with patch.object(importer.subprocess, 'run') as run:
            run.return_value.stdout = b'99999999'
            with self.assertRaises(importer.Failure):
                importer.git_blob(Path('/local'), self.commit, 'assets/characters/synthetic/field.png', 100)
            self.assertEqual(run.call_count, 1)
        with self.assertRaises(importer.Failure):
            importer.git_blob(Path('/local'), self.commit, 'tools/run.py', 100)

    @contextmanager
    def workspace(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); private = root / '.personal-assets/digigame'; private.mkdir(parents=True)
            world = root / '.personal-assets/world-ds'; (world / 'input').mkdir(parents=True)
            (root / 'data').mkdir(); (root / 'data/world-ds-catalog.json').write_bytes(importer.canonical(self.catalog))
            (root / 'data/world-ds-ids.json').write_bytes(importer.canonical(self.catalog))
            entry = {**self.entry, 'formId': 18, 'name': 'Existing'}
            mapping, _ = self.mapping(entry=entry)
            source = mapping['sprites']['ds-form-18']['source']
            target = world / 'input' / source; target.parent.mkdir(parents=True); target.write_bytes(self.png)
            (world / 'input/ds-form-18.mapping.json').write_bytes(importer.canonical(mapping))
            batch = dict(formatVersion=1, collection='digimon-world-ds', version=10, entries=[dict(
                artId='ds-form-18', mapping='ds-form-18.mapping.json', deviceResize='nearest',
                inspection=dict(reviewer='Original fixture author', notes='Original generated pixels.',
                                sources=[dict(file=source, sha256=importer.digest(self.png))]))])
            (world / 'input/batch.json').write_bytes(importer.canonical(batch))
            self.baseline = importer.pipeline.build_batch(world / 'input/batch.json', world, private_root=root / '.personal-assets')
            baseline = (world / 'index.json').read_bytes()
            (private / 'baseline-index-v10.json').write_bytes(baseline)
            plan = private / 'plan.json'; plan.write_bytes(importer.canonical(self.plan()))
            def read_blob(repository, commit, path, maximum):
                return self.png if path.endswith('.png') else importer.canonical(self.field)
            with ExitStack() as patches:
                for key, value in dict(ROOT=root, PRIVATE=private, BASELINE_SHA256=importer.digest(baseline),
                                       BASELINE_FORMS=1, git_blob=read_blob).items():
                    patches.enter_context(patch.object(importer, key, value))
                yield root, private, world, plan, baseline

    def approve(self, output, report):
        review = dict(formatVersion=1, status='approved', reviewer='Independent original-fixture test',
                      **{key: report[key] for key in ('sourceCommit', 'planSha256', 'stagedIndexSha256')},
                      entries=[dict(status='approved', **{key: row[key] for key in
                               ('formId', 'pngSha256', 'fieldSha256', 'mappingSha256')}) for row in report['entries']])
        path = output / 'review.json'; path.write_bytes(importer.canonical(review)); return path

    def test_stage_is_deterministic_and_never_activates(self):
        with self.workspace() as (_, _, world, plan, baseline):
            output, report = importer.prepare(plan)
            first = (output / 'staged/index.json').read_bytes()
            second_output, second = importer.prepare(plan)
            self.assertEqual(output, second_output); self.assertEqual(report, second)
            self.assertEqual(first, (output / 'staged/index.json').read_bytes())
            self.assertEqual((world / 'index.json').read_bytes(), baseline)
            self.assertFalse(report['activeIndexChanged']); self.assertFalse(report['upstreamCodeExecuted'])
            self.assertEqual(json.loads(first)['entries'][0]['device']['frames'], 18)

    def test_activation_recovers_interrupted_index_batch_pair_exactly_once(self):
        with self.workspace() as (_, private, world, plan, baseline):
            output, report = importer.prepare(plan); review = self.approve(output, report)
            atomic = importer.pipeline.importer.atomic_write
            def failing_write(path, raw):
                if path == world / 'input/batch.json': raise OSError('simulated process interruption')
                return atomic(path, raw)
            with patch.object(importer.pipeline.importer, 'atomic_write', failing_write):
                with self.assertRaises(OSError): importer.activate(output, review)
            self.assertEqual(json.loads((world / 'index.json').read_bytes())['version'], 11)
            self.assertEqual(json.loads((world / 'input/batch.json').read_bytes())['version'], 10)
            result = importer.activate(output, review)
            self.assertEqual(result, importer.activate(output, review))
            self.assertEqual(result['forms'], 2); self.assertEqual(result['addedForms'], [200])
            self.assertEqual(json.loads((world / 'input/batch.json').read_bytes())['version'], 11)
            self.assertEqual(json.loads((world / 'index.json').read_bytes())['entries'][0], self.baseline['entries'][0])

    def test_activation_rejects_unreviewed_and_changed_baseline_blob(self):
        with self.workspace() as (_, _, world, plan, baseline):
            output, report = importer.prepare(plan); review = self.approve(output, report)
            changed = json.loads(review.read_text()); changed['entries'][0]['mappingSha256'] = 'f' * 64
            review.write_bytes(importer.canonical(changed))
            with self.assertRaises(importer.Failure): importer.activate(output, review)
            review = self.approve(output, report)
            original = self.baseline['entries'][0]['device']; (world / original['file']).write_bytes(b'changed')
            with self.assertRaises(importer.Failure): importer.activate(output, review)
            self.assertEqual((world / 'index.json').read_bytes(), baseline)

    def test_source_aware_audit_requires_unchanged_approved_receipt(self):
        audit_spec = importlib.util.spec_from_file_location('mixed_art_audit', ROOT / 'scripts/audit-digigame-assets.py')
        audit = importlib.util.module_from_spec(audit_spec); audit_spec.loader.exec_module(audit)
        with self.workspace() as (_, private, _, plan, _), patch.object(audit, 'm', importer):
            output, report = importer.prepare(plan); review = self.approve(output, report)
            importer.activate(output, review)
            result = audit.audit()
            self.assertEqual(result['integratedPacks'], 2); self.assertEqual(result['addedDigiGameForms'], 1)
            self.assertTrue(result['original87RowsAndArtifactsUnchanged'])
            receipt = next((private / 'activations').glob('*/activation.json'))
            changed = json.loads(receipt.read_text()); changed['entries'][0]['sourceSlug'] = 'wrong'
            receipt.write_bytes(importer.canonical(changed))
            with self.assertRaises(importer.Failure): audit.audit()

    def test_recovery_refuses_unrelated_changed_batch(self):
        with self.workspace() as (_, _, world, plan, _):
            output, report = importer.prepare(plan); review = self.approve(output, report)
            atomic = importer.pipeline.importer.atomic_write
            def failing_write(path, raw):
                if path == world / 'input/batch.json': raise OSError('interrupted')
                return atomic(path, raw)
            with patch.object(importer.pipeline.importer, 'atomic_write', failing_write):
                with self.assertRaises(OSError): importer.activate(output, review)
            (world / 'input/batch.json').write_bytes(b'{}')
            with self.assertRaises(importer.Failure): importer.activate(output, review)


if __name__ == '__main__':
    unittest.main()

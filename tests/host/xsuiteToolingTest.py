# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 MKME Media
"""Local, non-network checks for portable maintenance paths and refusal guards."""
import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/xsuite' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


update = load('update_upstream')
validation = load('validate')


class ToolingTests(unittest.TestCase):
    def setUp(self):
        self.area = OUTPUT / self._testMethodName
        self.area.mkdir()
        self.repo = self.area / 'standalone'
        self.repo.mkdir()
        self.calls = []

    def git(self, branch='codex/xsuite-radio', dirty='', sparse='.git/info/sparse-checkout', upstream=None):
        def run(*args):
            self.calls.append(args)
            if args == ('branch', '--show-current'):
                return branch
            if args == ('remote', 'get-url', 'upstream'):
                return upstream or 'https://github.com/RadioFoundry/RadioFoundry.git'
            if args == ('rev-parse', '--git-path', 'info/sparse-checkout'):
                return str(sparse)
            if args == ('status', '--porcelain'):
                return dirty
            if args == ('diff', '--name-only', 'a' * 40, 'HEAD'):
                return 'core/xsuite/xsuite.c'
            return 'a' * 40
        return run

    def test_normal_clone_needs_no_sparse_file(self):
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git()):
            result = update.refresh()
        self.assertFalse(result['merged'])
        self.assertIn(('fetch', '--no-tags', 'upstream', 'main'), self.calls)

    def test_sparse_path_resolved_by_git_accepts_git_file(self):
        (self.repo / '.git').write_text('gitdir: ../shared/worktree\n', encoding='utf-8')
        sparse = self.area / 'shared/worktree/info/sparse-checkout'
        sparse.parent.mkdir(parents=True)
        sparse.write_text('/*\n!/.github/\n', encoding='utf-8')
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git(sparse=sparse)):
            update.refresh()
        self.assertIn(('fetch', '--no-tags', 'upstream', 'main'), self.calls)

    def test_sparse_checkout_keeps_workflows_excluded(self):
        sparse = self.repo / '.git/info/sparse-checkout'
        sparse.parent.mkdir(parents=True)
        sparse.write_text('/*\n', encoding='utf-8')
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git()):
            with self.assertRaisesRegex(RuntimeError, 'Keep .github excluded'):
                update.refresh()
        self.assertFalse(any(call[0] == 'fetch' for call in self.calls))

    def test_dirty_merge_refuses_before_fetch(self):
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git(dirty=' M source.c')):
            with self.assertRaisesRegex(RuntimeError, 'Local changes preserved'):
                update.refresh(merge=True)
        self.assertFalse(any(call[0] == 'fetch' for call in self.calls))

    def test_wrong_branch_refuses_fetch(self):
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git(branch='main')):
            with self.assertRaisesRegex(RuntimeError, 'existing codex/xsuite-radio'):
                update.refresh()
        self.assertFalse(any(call[0] == 'fetch' for call in self.calls))

    def test_wrong_upstream_refuses_fetch(self):
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git(upstream='https://example.invalid/other')):
            with self.assertRaisesRegex(RuntimeError, 'approved RadioFoundry'):
                update.refresh()
        self.assertFalse(any(call[0] == 'fetch' for call in self.calls))

    def test_clean_merge_preserves_normal_history(self):
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git()), \
             mock.patch.object(update.subprocess, 'run') as run:
            self.assertTrue(update.refresh(merge=True)['merged'])
        self.assertEqual(run.call_args.args[0], ['git', '-C', str(self.repo), 'merge', '--no-edit', 'upstream/main'])

    def test_standalone_export_needs_explicit_destination(self):
        with mock.patch.object(update, 'ROOT', self.repo):
            with self.assertRaisesRegex(RuntimeError, '--export-dir'):
                update.export_source()
        self.assertEqual(list(self.area.iterdir()), [self.repo])

    def test_existing_parent_layout_keeps_export_default(self):
        repo = self.area / 'XTOC/radio-firmware/RadioFoundry'
        repo.mkdir(parents=True)
        app = self.area / 'XTOC/xtoc-web'
        app.mkdir()
        (app / 'package.json').write_text('{}', encoding='utf-8')
        with mock.patch.object(update, 'ROOT', repo):
            self.assertEqual(update.export_destination(), repo.parent)

    def test_explicit_export_matches_exact_diff(self):
        destination = self.area / 'exports'
        delta = b'diff --git a/feature.c b/feature.c\n'
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git()), \
             mock.patch.object(update.subprocess, 'check_output', return_value=delta):
            result = update.export_source(destination)
        self.assertEqual((destination / 'patches/0001-xsuite-radio.patch').read_bytes(), delta)
        self.assertEqual(result['patches'][0]['sha256'], hashlib.sha256(delta).hexdigest())
        self.assertEqual(result['derivativeUrl'], 'https://github.com/MKme/XSuite-Radio.git')
        self.assertEqual(result['upstreamUrl'], 'https://github.com/RadioFoundry/RadioFoundry.git')
        self.assertEqual(json.loads((destination / 'upstream.json').read_text())['upstreamCommit'], 'a' * 40)

    def test_credential_bearing_upstream_is_not_exported(self):
        destination = self.area / 'exports'
        remote = 'https://synthetic-test-credential@github.com/RadioFoundry/RadioFoundry.git'
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git(upstream=remote)):
            with self.assertRaisesRegex(RuntimeError, 'approved RadioFoundry') as failure:
                update.export_source(destination)
        self.assertNotIn('synthetic-test-credential', str(failure.exception))
        self.assertFalse(destination.exists())

    def test_dirty_source_export_preserves_destination(self):
        destination = self.area / 'exports'
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git(dirty=' M source.c')):
            with self.assertRaisesRegex(RuntimeError, 'Commit the reviewed derivative'):
                update.export_source(destination)
        self.assertFalse(destination.exists())

    def test_existing_export_is_preserved_as_a_pair(self):
        destination = self.area / 'exports'
        destination.mkdir()
        metadata = destination / 'upstream.json'
        metadata.write_text('existing unrelated local work', encoding='utf-8')
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git()), \
             mock.patch.object(update.subprocess, 'check_output', return_value=b'diff\n'):
            with self.assertRaisesRegex(RuntimeError, '--replace-export'):
                update.export_source(destination)
        self.assertEqual(metadata.read_text(), 'existing unrelated local work')
        self.assertFalse((destination / 'patches').exists())

    def test_explicit_replace_and_identical_reuse(self):
        destination = self.area / 'exports'
        destination.mkdir()
        (destination / 'upstream.json').write_text('old reviewed export', encoding='utf-8')
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git()), \
             mock.patch.object(update.subprocess, 'check_output', return_value=b'diff\n'):
            update.export_source(destination, replace=True)
            snapshot = {path: path.stat().st_mtime_ns for path in destination.rglob('*') if path.is_file()}
            update.export_source(destination)
        self.assertEqual(snapshot, {path: path.stat().st_mtime_ns for path in snapshot})
        self.assertEqual((destination / 'patches/0001-xsuite-radio.patch').read_bytes(), b'diff\n')

    def test_nonregular_export_target_is_preserved(self):
        destination = self.area / 'exports'
        (destination / 'upstream.json').mkdir(parents=True)
        with mock.patch.object(update, 'ROOT', self.repo), mock.patch.object(update, 'git', self.git()), \
             mock.patch.object(update.subprocess, 'check_output', return_value=b'diff\n'):
            with self.assertRaisesRegex(RuntimeError, 'not a regular file'):
                update.export_source(destination, replace=True)
        self.assertTrue((destination / 'upstream.json').is_dir())
        self.assertFalse((destination / 'patches').exists())

    def test_missing_app_prevents_validation_output(self):
        output = self.area / 'validation'
        with self.assertRaisesRegex(RuntimeError, 'Canonical XTOC source is required'):
            validation.validate(output, xtoc_web=self.area / 'missing')
        self.assertFalse(output.exists())

    def test_existing_output_is_preserved(self):
        output = self.area / 'validation'
        output.mkdir()
        sentinel = output / 'keep.txt'
        sentinel.write_text('existing local work', encoding='utf-8')
        with self.assertRaisesRegex(RuntimeError, 'existing output is preserved'):
            validation.validation_output(output)
        self.assertEqual(sentinel.read_text(), 'existing local work')
        with self.assertRaises(RuntimeError):
            validation.validation_output(sentinel)

    def test_explicit_and_environment_app_paths(self):
        app = self.area / 'XTOC Web'
        (app / 'src/core').mkdir(parents=True)
        (app / 'package.json').write_text('{}', encoding='utf-8')
        (app / 'src/core/packet.ts').write_text('// canonical fixture path only\n', encoding='utf-8')
        self.assertEqual(validation.canonical_app(app), app)
        with mock.patch.dict(os.environ, {'XTOC_WEB_ROOT': str(app)}):
            self.assertEqual(validation.canonical_app(), app)
            with self.assertRaises(RuntimeError):
                validation.canonical_app(self.area / 'explicit-missing')

    def test_invalid_cli_arguments_refuse_before_fetch(self):
        script = ROOT / 'tools/xsuite/update_upstream.py'
        for args in (['--merge', '--validate'], ['--merge', '--export-source'], ['--export-dir', str(self.area)], ['--replace-export']):
            result = subprocess.run([sys.executable, '-B', str(script), *args], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn('error:', result.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, required=True, help='New empty test output directory; retained for inspection')
    args = parser.parse_args()
    OUTPUT = args.output.resolve()
    if OUTPUT.exists() and any(OUTPUT.iterdir()):
        parser.error('Use a new empty output directory; existing files are preserved')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[sys.argv[0]], verbosity=2)

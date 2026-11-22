# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 MKME Media
"""Refresh the maintained derivative without discarding local work or checking out Actions."""
import argparse
import json
import pathlib
import subprocess
import sys
import hashlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def export_source(parent=None):
    """Portable source delta against the accepted upstream; no inaccessible private commit is a fetch dependency."""
    parent = pathlib.Path(parent or ROOT.parent)
    upstream = git('rev-parse', 'upstream/main')
    git('merge-base', '--is-ancestor', upstream, 'HEAD')
    if git('status', '--porcelain'):
        raise RuntimeError('Commit the reviewed derivative before exporting its exact portable source')
    changed = git('diff', '--name-only', upstream, 'HEAD').splitlines()
    if any(p.startswith('.github/') for p in changed):
        raise RuntimeError('The derivative patch must not contain GitHub Actions files')
    patches = parent / 'patches'
    patches.mkdir(parents=True, exist_ok=True)
    data = subprocess.check_output(['git', '-C', str(ROOT), 'diff', '--binary', upstream, 'HEAD'])
    target = patches / '0001-xsuite-radio.patch'
    target.write_bytes(data)
    record = dict(format='xsuite-radio-source/1', upstreamUrl=git('remote', 'get-url', 'upstream'), upstreamCommit=upstream,
                  derivativeBranch=git('branch', '--show-current'), derivativeCommit=git('rev-parse', 'HEAD'),
                  patches=[dict(path='patches/' + target.name, sha256=hashlib.sha256(data).hexdigest())],
                  submodules={'third_party/FreeRTOS-Kernel': git('rev-parse', 'HEAD:third_party/FreeRTOS-Kernel')},
                  compiler={'gcc': '14.2.0', 'binutils': '2.43.1', 'newlib': '4.5.0.20241231'})
    (parent / 'upstream.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def refresh(merge=False):
    branch = git('branch', '--show-current')
    if branch != 'codex/xsuite-radio':
        raise RuntimeError('Use the existing codex/xsuite-radio derivative branch')
    if git('remote', 'get-url', 'upstream').lower().removesuffix('.git') != 'https://github.com/radiofoundry/radiofoundry':
        raise RuntimeError('The upstream remote is not the approved RadioFoundry repository')
    patterns = (ROOT / '.git/info/sparse-checkout').read_text()
    if '!/.github/' not in patterns:
        raise RuntimeError('Keep .github excluded from this local-only derivative checkout')
    before = git('rev-parse', 'HEAD')
    git('fetch', '--no-tags', 'upstream', 'main')
    latest = git('rev-parse', 'upstream/main')
    dirty = git('status', '--porcelain')
    if merge:
        if dirty:
            raise RuntimeError('Local changes preserved. Commit the reviewed derivative changes before an upstream merge.')
        # Ordinary merge keeps both histories. A conflict remains visible for repair; no reset/clean/force follows.
        subprocess.run(['git', '-C', str(ROOT), 'merge', '--no-edit', 'upstream/main'], check=True)
    return {'branch': branch, 'before': before, 'upstream': latest, 'head': git('rev-parse', 'HEAD'),
            'localChangesPreserved': bool(dirty), 'merged': bool(merge),
            'requiredChecks': ['canonical packet templates', 'X Suite native engine and host protocol tests',
                               'upstream host tests', 'DM-32UV xsuite build', 'XSUITE off build',
                               'XTOC/XCOM transport and packaged workflow tests']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--merge', action='store_true', help='Merge latest upstream into a clean, reviewed derivative')
    parser.add_argument('--validate', action='store_true', help='Run the local native and crosscompiler gate')
    parser.add_argument('--output', type=pathlib.Path, help='A new empty task-owned validation directory')
    parser.add_argument('--export-source', action='store_true', help='Save the reviewed portable delta and pinned provenance in the parent product')
    args = parser.parse_args()
    result = refresh(args.merge)
    if args.validate:
        if args.output is None:
            parser.error('--validate requires --output NEW_EMPTY_DIRECTORY')
        from validate import validate
        result['validation'] = validate(args.output)
    if args.export_source:
        if args.merge and not args.validate:
            parser.error('After a merge, validate before accepting/exporting the new source')
        result['source'] = export_source()
    print(json.dumps(result, indent=2))

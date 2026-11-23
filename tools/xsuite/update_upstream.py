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
UPSTREAM_URL = 'https://github.com/RadioFoundry/RadioFoundry.git'
DERIVATIVE_URL = 'https://github.com/MKme/XSuite-Radio.git'


def approved_upstream():
    if git('remote', 'get-url', 'upstream').lower().removesuffix('.git') != UPSTREAM_URL.lower().removesuffix('.git'):
        raise RuntimeError('The upstream remote is not the approved RadioFoundry repository')
    # Persist only the known public URL, never a credential-bearing remote value.
    return UPSTREAM_URL


def export_destination(parent=None):
    if parent is not None:
        return pathlib.Path(parent).resolve()
    if ROOT.parent.name == 'radio-firmware' and (ROOT.parent.parent / 'xtoc-web/package.json').is_file():
        return ROOT.parent
    raise RuntimeError('A standalone fork requires --export-dir PATH with --export-source; '
                       'no files will be written beside the clone implicitly.')


def export_source(parent=None, replace=False):
    """Portable source delta against the accepted upstream; no inaccessible private commit is a fetch dependency."""
    parent = export_destination(parent)
    upstream_url = approved_upstream()
    upstream = git('rev-parse', 'upstream/main')
    git('merge-base', '--is-ancestor', upstream, 'HEAD')
    if git('status', '--porcelain'):
        raise RuntimeError('Commit the reviewed derivative before exporting its exact portable source')
    changed = git('diff', '--name-only', upstream, 'HEAD').splitlines()
    if any(p.startswith('.github/') for p in changed):
        raise RuntimeError('The derivative patch must not contain GitHub Actions files')
    patches = parent / 'patches'
    data = subprocess.check_output(['git', '-C', str(ROOT), 'diff', '--binary', upstream, 'HEAD'])
    target = patches / '0001-xsuite-radio.patch'
    record = dict(format='xsuite-radio-source/1', upstreamUrl=upstream_url, upstreamCommit=upstream,
                  derivativeUrl=DERIVATIVE_URL,
                  derivativeBranch=git('branch', '--show-current'), derivativeCommit=git('rev-parse', 'HEAD'),
                  patches=[dict(path='patches/' + target.name, sha256=hashlib.sha256(data).hexdigest())],
                  submodules={'third_party/FreeRTOS-Kernel': git('rev-parse', 'HEAD:third_party/FreeRTOS-Kernel')},
                  compiler={'gcc': '14.2.0', 'binutils': '2.43.1', 'newlib': '4.5.0.20241231'})
    outputs = {target: data, parent / 'upstream.json': (json.dumps(record, indent=2) + '\n').encode('utf-8')}
    # Inspect the whole pair before creating directories or replacing either file.
    for path, content in outputs.items():
        if path.is_symlink() or (path.exists() and not path.is_file()):
            raise RuntimeError('Export target is not a regular file; existing contents preserved: ' + str(path))
        if path.exists() and path.read_bytes() != content and not replace:
            raise RuntimeError('Existing export differs; choose a new --export-dir or explicitly pass '
                               '--replace-export after reviewing the destination: ' + str(path))
    patches.mkdir(parents=True, exist_ok=True)
    for path, content in outputs.items():
        if path.exists() and path.read_bytes() == content:
            continue
        with path.open('wb' if replace else 'xb') as stream:
            stream.write(content)
    return record


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def refresh(merge=False):
    branch = git('branch', '--show-current')
    if branch != 'codex/xsuite-radio':
        raise RuntimeError('Use the existing codex/xsuite-radio derivative branch')
    approved_upstream()
    # git resolves both a normal .git directory and a linked worktree's .git file.
    sparse = pathlib.Path(git('rev-parse', '--git-path', 'info/sparse-checkout'))
    if not sparse.is_absolute():
        sparse = ROOT / sparse
    if sparse.is_file() and '!/.github/' not in sparse.read_text(encoding='utf-8'):
        raise RuntimeError('Keep .github excluded when using a sparse checkout; normal full clones are supported')
    dirty = git('status', '--porcelain')
    if merge and dirty:
        raise RuntimeError('Local changes preserved. Commit the reviewed derivative changes before an upstream merge.')
    before = git('rev-parse', 'HEAD')
    git('fetch', '--no-tags', 'upstream', 'main')
    latest = git('rev-parse', 'upstream/main')
    if merge:
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
    parser.add_argument('--xtoc-web', type=pathlib.Path, help='Existing XTOC xtoc-web source for canonical release checks')
    parser.add_argument('--cross', default='/opt/xsuite/csky-elf/bin/csky-elf-', help='C-SKY compiler prefix for --validate')
    parser.add_argument('--export-source', action='store_true', help='Save the reviewed portable delta and pinned provenance')
    parser.add_argument('--export-dir', type=pathlib.Path, help='Explicit export destination; required for standalone forks')
    parser.add_argument('--replace-export', action='store_true', help='Explicitly replace differing patch/provenance files in the reviewed export destination')
    args = parser.parse_args()
    if args.validate and args.output is None:
        parser.error('--validate requires --output NEW_EMPTY_DIRECTORY')
    if args.export_dir is not None and not args.export_source:
        parser.error('--export-dir requires --export-source')
    if args.replace_export and not args.export_source:
        parser.error('--replace-export requires --export-source')
    if args.export_source and args.merge and not args.validate:
        parser.error('After a merge, validate before accepting/exporting the new source')
    destination = export_destination(args.export_dir) if args.export_source else None
    if args.validate:
        from validate import canonical_app, validation_output
        canonical_app(args.xtoc_web)
        validation_output(args.output)
    result = refresh(args.merge)
    if args.validate:
        from validate import validate
        result['validation'] = validate(args.output, args.cross, xtoc_web=args.xtoc_web)
    if args.export_source:
        result['source'] = export_source(destination, replace=args.replace_export)
    print(json.dumps(result, indent=2))

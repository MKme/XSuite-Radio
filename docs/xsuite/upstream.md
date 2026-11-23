<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 MKME Media -->
# Keep X Suite Radio current with RadioFoundry

The fork at [MKme/XSuite-Radio](https://github.com/MKme/XSuite-Radio) keeps RadioFoundry's Git history and our X Suite integration together. Its default branch is **`codex/xsuite-radio`**. Local `origin` points to this fork; `upstream` points to [RadioFoundry/RadioFoundry](https://github.com/RadioFoundry/RadioFoundry).

The fork's `main` is the upstream mirror. Work on `codex/xsuite-radio` for X Suite changes. GitHub's **Sync fork** action updates the selected branch: select the upstream mirror when maintaining that mirror, and use the reviewed merge procedure below for the X Suite branch.

## Clone the maintained branch

This setup preserves LF line endings for Linux tools and omits inherited GitHub workflow files from the working tree:

```sh
git clone --no-checkout --branch codex/xsuite-radio https://github.com/MKme/XSuite-Radio.git
cd XSuite-Radio
git config core.autocrlf input
git sparse-checkout set --no-cone '/*' '!/.github/'
git checkout codex/xsuite-radio
git remote add upstream https://github.com/RadioFoundry/RadioFoundry.git
git submodule update --init third_party/FreeRTOS-Kernel
git remote -v
```

The submodule command checks out the FreeRTOS commit pinned by this firmware tree. The update helper also supports an ordinary full clone. Validation for this fork runs locally; inherited GitHub Actions remain disabled in the hosted repository.

## Inspect an update

Begin on `codex/xsuite-radio` and inspect existing work:

```sh
git status --short
git branch --show-current
python3 tools/xsuite/update_upstream.py
git log --oneline HEAD..upstream/main
git diff --stat HEAD...upstream/main
```

The helper fetches the current upstream `main` and reports the branch, commits and local-change state. Fetching leaves the working files intact. Commit reviewed local changes before merging; the merge helper preserves dirty work and asks for a clean tree.

## Merge, review and qualify

```sh
python3 tools/xsuite/update_upstream.py --merge
git status --short
```

This is an ordinary Git merge that retains both histories. Resolve any conflicts in the affected files, stage the reviewed resolutions and complete the merge with `git commit`. Review upstream changes around the thin APRS, DMR, settings, UI and host-link hooks described in [XSUITE.md](../../XSUITE.md).

Follow [the full release checks](building.md#canonical-application-and-full-release-checks) using the current canonical XTOC encoders and matching compiler. A combined clean-tree update and validation command is:

```sh
python3 tools/xsuite/update_upstream.py --merge --validate --xtoc-web /path/to/XTOC/xtoc-web --cross /path/to/csky-elf/bin/csky-elf- --output build/xsuite-update-YYYYMMDD
```

Use that combined form at the start of the update, or run `validate.py` after a separately reviewed merge. The [build guide](building.md) explains the required tools and output-directory choice. Carry the BETA label and current operator instructions through firmware and application updates.

Keep our X Suite README as the fork's landing page. When upstream changes its README, reconcile useful upstream context into [README.upstream.md](../../README.upstream.md), retaining the original authorship and links, while updating our integration instructions where behavior changed.

After the review and relevant firmware/application checks, publish the maintained branch with a normal push:

```sh
git push origin codex/xsuite-radio
```

Ordinary merges and pushes retain the project's history. Resolve divergent commits by reviewing and integrating them; preserve local work throughout the update.

## Optional portable source export

The public fork is the direct source download and Git clone location. Maintainers can also export an exact patch and provenance record to an explicitly chosen directory after committing reviewed changes:

```sh
python3 tools/xsuite/update_upstream.py --export-source --export-dir /path/to/source-export
```

The export contains `upstream.json` and `patches/0001-xsuite-radio.patch`. Use a task-owned destination and preserve any required earlier export. Identical existing exports are reusable; replacing different export files requires the explicit `--replace-export` option after reviewing those exact targets. The source exporter accepts the maintained branch only when its recorded `upstream/main` is an ancestor of the clean derivative. Use `--merge --validate` with the canonical app path and compiler when accepting a newly integrated upstream revision.

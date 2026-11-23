<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 MKME Media -->
# Build X Suite Radio from source

This page is for firmware developers. Operators use the normal XTOC/XCOM [backup, installation and recovery workflow](operator-guide.md#install-update-and-recover-the-radio).

**Firmware flasher: BETA — Use at your own risk.** Keep the radio's original backup before installing a build.

## Prepare a source checkout

Follow [the clone instructions](upstream.md#clone-the-maintained-branch) to select `codex/xsuite-radio`, add the upstream remote and initialize the pinned FreeRTOS submodule. A Linux environment supplies GNU Make, Git, Python 3, a host C compiler and zlib development headers for the native tests. The firmware target uses `csky-elf-gcc`.

The pinned C-SKY compiler version is recorded in [`platform/hrc7000/platform.toml`](../../platform/hrc7000/platform.toml): GCC 14.2.0. The existing [RadioFoundry build instructions](../dev/building.md#toolchains) describe its toolchain and the included build script. Use an installed matching compiler and set its prefix explicitly in the commands below. The commands on this page use existing tools and committed repository resources.

## Standalone firmware build

The generated packet templates and canonical test vectors are committed in this repository. A normal firmware build uses them directly.

From the repository root, replace the compiler prefix with your installed location:

```sh
CSKY_PREFIX="$HOME/toolchains/csky-elf/bin/csky-elf-"
make BOARD=dm32 VARIANT=xsuite CROSS="$CSKY_PREFIX" RFOUNDRY_PYTHON=python3
make BOARD=dm32 VARIANT=xsuite CROSS="$CSKY_PREFIX" RFOUNDRY_PYTHON=python3 stack-check
```

The normal outputs are `build/dm32-xsuite/rfoundry_dm32.bin`, `.elf` and `.map`. The `xsuite` variant enables the guarded X Suite integration while retaining upstream voice, position and radio services selected by that variant. [`config/variants/xsuite.mk`](../../config/variants/xsuite.mk) lists its feature choices.

## Local source checks

Run the inherited host suite and the actual X Suite menu test:

```sh
make -C tests/host -j4 PYTHON=python3
make -C tests/host -j4 xsuiteUiTest PYTHON=python3
./tests/host/xsuiteUiTest
```

Check the C packet routine against the committed canonical vectors using a new empty output directory:

```sh
python3 tests/host/xsuite_packet_test.py --output build/xsuite-packet-check-YYYYMMDD
python3 tests/host/xsuiteInstallerTest.py --output build/xsuite-installer-check-YYYYMMDD
python3 tests/host/xsuiteToolingTest.py --output build/xsuite-tooling-check-YYYYMMDD
```

The installer test uses deterministic stand-ins. Local source checks and firmware compilation support development; use the intended radio and receive system for field setup as described in [Keep a working field setup](operator-guide.md#keep-a-working-field-setup).

## Canonical application and full release checks

Maintainers who regenerate packet templates use the existing XTOC application source and its established Node/esbuild dependencies. Point at the **`xtoc-web` directory**, containing `package.json` and `src/core/packet.ts`:

```sh
node tools/xsuite/generate-templates.mjs --check --xtoc-web /path/to/XTOC/xtoc-web
```

`XTOC_WEB_ROOT=/path/to/XTOC/xtoc-web` supplies the same choice. To regenerate after an intentional change to the canonical application encoders, run the generator with the same path and omit `--check`; review the generated header and fixture changes together.

The complete firmware release gate includes the canonical encoder comparison, packet/runtime/installer/menu checks, upstream host tests, the C-SKY firmware, stack limits and an X Suite-disabled build:

```sh
python3 tools/xsuite/validate.py --xtoc-web /path/to/XTOC/xtoc-web --cross "$CSKY_PREFIX" --output build/xsuite-validation-YYYYMMDD
```

Choose a new empty output directory for each accepted run. The full gate requires the canonical application source and its existing build dependencies; it retains that comparison as part of release qualification. Reconcile the application-side receive, roster, Compose and packaged workflows with the matching firmware before a distribution update.

## Install and recover

Use the installed application's bundled firmware tools to back up the radio, select its original backup, install the matching firmware package and follow the displayed restart instructions. Keep the codec and calibration associated with that radio. The [operator guide](operator-guide.md#install-update-and-recover-the-radio) supplies current screenshots and the recovery actions.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 MKME Media -->
# Contribute to X Suite Radio

Use [this fork's issues](https://github.com/MKme/XSuite-Radio/issues) for X Suite radio reports, host integration and documentation. Include the firmware revision, radio model, APRS/DMR path, the action taken and the expected and observed result. Use demonstration callsigns, IDs and positions in public examples.

Keep X Suite changes concentrated in `core/xsuite`, `core/ui/ui_xsuite.c`, `tools/xsuite` and the `xsuite` variant, with small guarded hooks into upstream services. Preserve the existing XTOC/XCOM composers and canonical packet encoders. Regenerate templates from those encoders using the explicit app path in the [build guide](building.md#canonical-application-and-full-release-checks).

Run the relevant local source checks, describe the hardware/receive path used for a radio reproduction, and update the matching operator or protocol instructions. Retain **BETA — Use at your own risk** beside firmware installation and recovery controls and instructions. Keep RadioFoundry and third-party copyright and license notices.

See [the upstream update procedure](upstream.md) before changing integration hooks. Work on the maintained X Suite branch and submit a focused pull request against `codex/xsuite-radio`. The fork's checks run locally; GitHub Actions remain disabled.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 MKME Media -->
# X Suite Radio technical integration

**Firmware flasher: BETA — Use at your own risk.** Retain the radio's original backup before installing or updating firmware.

Use the [operator guide](docs/xsuite/operator-guide.md) for setup and field operation, the [build guide](docs/xsuite/building.md) for source builds, and the [upstream guide](docs/xsuite/upstream.md) for maintained merges.

X Suite Radio is based on [RadioFoundry](https://github.com/RadioFoundry/RadioFoundry), MIT licensed.
It sends native XTOC reports through the existing APRS/AX.25 and DMR data services. XTOC and XCOM match the radio's
normal callsign/SSID or DMR source ID to the saved Team roster. The firmware stores radio settings, not a second roster.

The maintained default branch in [MKme/XSuite-Radio](https://github.com/MKme/XSuite-Radio) is `codex/xsuite-radio`.
The `origin` remote identifies this fork and `upstream` identifies RadioFoundry. Both histories remain available; validation runs locally. Keep the upstream copyright, LICENSE, AUTHORS,
CONTRIBUTORS and third-party notices with source and firmware distributions. The radio's proprietary voice codec
comes from its own verified local backup and is retained locally.

## Keeping current

The integration is concentrated in `core/xsuite`, `core/ui/ui_xsuite.c`, `tools/xsuite` and the `xsuite` build variant.
Small guarded hooks connect the upstream APRS, DMR, settings, radio poll, UI and debug-link services. An upstream build
with `CONFIG_XSUITE=n` keeps the integration inactive. The appended settings extension preserves the existing layout.

1. Preserve and locally commit the reviewed derivative changes.
2. Run `python tools/xsuite/update_upstream.py` to fetch and inspect the newest upstream commit. The command reports
   local changes and never deletes, resets or hides them.
3. Run `python tools/xsuite/update_upstream.py --merge` from a clean derivative branch. It performs an ordinary merge.
   Resolve actual conflicts against the thin integration hooks; retain both histories.
4. Run `node tools/xsuite/generate-templates.mjs --check` against the current XTOC source. If canonical application
   encoders changed, regenerate and review the templates, then run the C/TypeScript parity test.
5. Run upstream host tests, X Suite tests, the focused `xsuite` firmware build and a build with XSUITE off. Run both
   apps' radio and packaged workflow checks. A source merge alone is not an accepted radio release.
6. Record the exact accepted upstream commit, derivative commit, compiler and final image hash in the release manifest.

If a merge conflicts or checks fail, preserve the work and repair it before packaging. Never force-push or reset the
derivative as an update mechanism. See the [upstream update procedure](docs/xsuite/upstream.md) and [canonical application checks](docs/xsuite/building.md#canonical-application-and-full-release-checks) for application and guide delivery.

## Building

A standalone build uses the committed generated templates and the C-SKY compiler pinned in `platform/hrc7000/platform.toml`:

```sh
make BOARD=dm32 VARIANT=xsuite CROSS=/path/to/csky-elf/bin/csky-elf- RFOUNDRY_PYTHON=python3
```

Maintainers check or regenerate templates with the existing canonical XTOC application source:

```sh
node tools/xsuite/generate-templates.mjs --check --xtoc-web /path/to/XTOC/xtoc-web
```

The [build guide](docs/xsuite/building.md) separates standalone source checks from the complete canonical application release gate.

Generated menu payloads come from the application's existing `encode*Clear` functions. Firmware fills their generated
time, GPS, printable note/text, quantity/count and priority slots and applies the native X1 wrapper. Presets send with
one OK. Hold OK on a report to edit its note (up to 64 printable ASCII characters), quantity/count and priority through
the existing radio text editor; Write message opens the native MESSAGE form. `tests/host/xsuite_packet_test.py --output NEW_EMPTY_DIRECTORY`
compiles the C routine and checks every payload against the canonical app-generated vectors, including bounds,
coordinate rounding and SOS status.

## Cable protocol

The existing debug-link framing carries command `0x27`. Requests are at most 64 bytes; replies start with the standard
status byte. The CRC remains CRC-16/CCITT-FALSE over command, sequence, length and payload.

| Op | Request after op | Reply after status |
|---|---|---|
| 0 | none | `xsuite.*` status keys |
| 1 | none, or 22-byte versioned profile | 22-byte profile |
| 2 | bearer u8, flags u8, destination DMR u32, destination callsign[10], text length u16 | accepted/busy |
| 3 | offset u16, up to 61 printable text bytes | accepted/invalid |
| 4 | none | queue the complete staged native packet |
| 5 | last received serial u32 | serial u32, bearer u8, flags u8, source DMR u32, destination DMR u32, source callsign[10], destination callsign[10], text length u16, text |
| 6 | menu action u8 | accepted/busy |
| 7 | Unix UTC seconds u32 | accepted |
| 8 | nonzero random per-install epoch u32 | accepted once, refused if already initialized |

Integers are little endian. Bearers are APRS 0, DMR 1, both 2. Profile bytes are version 1, automatic enabled,
bearer, flags (bit0 persisted SOS, bit1 group), interval u16, movement meters u16, DMR destination u32 and callsign[10].
TX_BEGIN and receive flags use `0x02` (bit1) for a group call. Profile bit0 (`0x01`) is the persisted SOS flag;
it is never the group marker on the host wire.
SOS changes through explicit actions. The two-record receive ring preserves original native identities and all
printable APRS payloads; serial and dropped counters expose polling gaps. Polling does not delete the owner's inbox.
BEGIN reserves the bounded host staging buffer against automatic reports and menu generation. Valid COMMIT releases
the reservation into the send queue; ten seconds without a successful chunk releases an abandoned reservation.
An urgent manual report stays queued while the host finishes, and a queued menu action makes a new BEGIN busy.

Each native packet ID is 16 uppercase hex digits: a persisted random 32-bit installation epoch followed by a 32-bit
sequence. Setup generates the epoch locally using the host OS random source. The radio commits a block of 256 IDs
before transmitting any ID in that block; a reboot starts beyond the previous block. Firmware updates preserve both
fields. A fresh stock install gets a fresh epoch, so the saved callsign/DMR roster association continues working after
reinstallation. Status `epoch_ready=0` is resolved automatically by the application with op8. Until setup completes,
the radio holds reports and shows Finish setup in XTOC/XCOM. The profile and sequence occupy appended settings bytes
408..439, with existing settings remaining readable.

Automatic reports default Off, 300 seconds, movement Off and APRS. Source-bound reports can carry time 0 before GPS
or the host has supplied UTC; the existing app decoder substitutes the receive time and retains original provenance.
SOS is a native EVENT (type255,labelSOS,ACTIVE1); All clear is the same EVENT COMPLETE2,noteALL CLEAR. While SOS is
latched, automatic CHECKIN remains HELP. APRS Transmitted follows the actual RF completion callback; Received at
XTOC/XCOM follows an existing native MESSAGE with subject ACK and the original packet ID, from the configured base.
With DMR destination 0, a menu report or host Channel send uses the selected channel's contact and group/private
type; an unset contact falls back to talkgroup 9. Explicit person, TOC and talkgroup destinations retain their saved
address and call type. The radio holds its DMR transmission while another operating mode is active.

The radio view assembles one source-scoped ordered native transfer, up to 128 parts and a 2,820-character assembled
frame including its wrapper, within 15 minutes. It displays a 319-character preview of native MESSAGE text or other report summaries.
The complete per-frame payloads continue to the app for its existing multipart decoder and full record storage. The
native runtime test checks a large command generated by the real app encoder, split over roughly 67 frames, including
a frame from another source. The radio's fast host ring retains two frames; serial/drop counters support diagnostics.
The completed preview retains its native source and bearer independently of that raw ring, so later ordinary APRS
beacons do not change the displayed command's attribution.
Native automatic ACK receipts update delivery status while preserving that readable command and source.
The field-radio preview accepts its own APRS addressee and software broadcasts, and excludes own RF echoes.
Other addressees remain in the unchanged raw host receive stream for the application's routing and provenance.

The Chinese catalog uses English for the new report labels outside the bundled CJK font. Other existing Chinese UI
and translated X Suite controls use the existing bundled glyphs.

Run the complete Linux gate with `python3 tools/xsuite/validate.py --xtoc-web /path/to/XTOC/xtoc-web --cross /path/to/csky-elf/bin/csky-elf- --output NEW_EMPTY_DIRECTORY`.
The [upstream update helper](docs/xsuite/upstream.md#merge-review-and-qualify) can perform the same gate after an ordinary merge.
An optional `--export-source --export-dir PATH` writes an exact portable delta and pinned provenance to a chosen maintenance directory after a clean reviewed commit.
The public fork itself supplies the directly cloneable source and history. Use LF line endings for Linux scripts and retain the pinned submodule commits, as shown in the [clone instructions](docs/xsuite/upstream.md#clone-the-maintained-branch).

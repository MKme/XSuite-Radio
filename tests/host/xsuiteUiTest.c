// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 MKME Media
// Actual radio screens and text editor, deterministic services; the pictures are UI simulations.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sim/ui_sim.h"
#include "ui/ui_app.h"
#include "ui/ui_nav.h"
#include "xsuite/xsuite.h"
static xsuite_config_t cfg = { .interval_s = 300 };
static xsuite_view_t view = { .own_dmr = 1234567, .gps_valid = true, .call = "N1TEST-7" };
static unsigned reports;
void xsuite_get_config(xsuite_config_t *out) { *out = cfg; }
bool xsuite_set_config(const xsuite_config_t *in, uint32_t ms) { (void)ms; cfg = *in; return true; }
void xsuite_get_view(xsuite_view_t *out) { *out = view; }
bool xsuite_action(uint8_t action) { reports++; if (action == XS_SOS) cfg.flags |= XSUITE_SOS; if (action == XS_ALL_CLEAR) cfg.flags &= ~XSUITE_SOS; view.tx_state = XS_TX_QUEUED; return true; }
bool xsuite_action_edit(uint8_t a, const char *note, uint16_t q, uint8_t pri) { assert(note[0] && q == 2 && pri == 0); return xsuite_action(a); }
bool xsuite_receive_next(uint32_t n, xsuite_rx_t *out) { (void)n; (void)out; return false; }
void xsuite_inbox_summary(char *out, size_t cap) { snprintf(out, cap, "Proceed to checkpoint BRAVO. Reply when arrived."); }
static void run(const char *script) { char err[256]; if (!sim_run(script, err, sizeof err)) { fprintf(stderr, "%s\n", err); assert(0); } }
static void shot(const char *directory, const char *name)
{
	if (!directory) return;
	char path[1024]; snprintf(path, sizeof path, "%s/%s.png", directory, name); assert(sim_save_png(path, 2));
}
int main(int argc, char **argv)
{
	const char *shots = argc > 1 ? argv[1] : NULL;
	sim_reset(sim_geom_find("240x320i10x5"), SIM_MAP_K);
	ui_push(SCR_XSUITE, 0); run("wait:100"); shot(shots, "dm32-menu");
	run("down ok"); assert(reports == 1 && cfg.flags & XSUITE_SOS); shot(shots, "dm32-sos");
	ui_frame()->focus = XS_ACTIONS; ui_focus_changed(); run("ok"); assert(cfg.enabled); shot(shots, "dm32-automatic");
	ui_frame()->focus = XS_SITREP; ui_focus_changed(); run("wait:3000 long:ok"); assert(ui_frame()->scr == SCR_XSUITE_EDIT);
	run("down ok down ok"); shot(shots, "dm32-edit-report");
	run("up up ok"); assert(ui_frame()->scr == SCR_MESSAGE_TEXT); shot(shots, "dm32-text-editor");
	run("ok"); assert(ui_frame()->scr == SCR_XSUITE_EDIT);
	ui_frame()->focus = 3; ui_focus_changed(); run("ok"); assert(reports == 2 && ui_frame()->scr == SCR_XSUITE);
	view.tx_state = XS_TX_IMPORTED; ui_dirty(); run("wait:3000"); shot(shots, "dm32-confirmed");
	printf("PASS X Suite radio UI: one-choice SOS, auto check-in setting, hold-to-edit quantity/priority, native text editor, send and confirmed state\n");
	return 0;
}

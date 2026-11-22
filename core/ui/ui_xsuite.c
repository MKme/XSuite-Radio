// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 MKME Media
#include "config.h"
#if CONFIG_XSUITE
#include <string.h>
#include "ui_nav.h"
#include "fmt.h"
#include "hal_time.h"
#include "xsuite/xsuite.h"

static uint8_t edit_action, edit_priority;
static uint16_t edit_quantity;
static char edit_note[65];
static void edit_open(uint8_t action)
{
	edit_action = action; edit_priority = 3; edit_quantity = 1;
	str_copy(edit_note, sizeof edit_note, action == XS_MESSAGE ? "" : xsuite_action_label(action));
	ui_push(SCR_XSUITE_EDIT, 0);
}
static uint16_t count(void) { return XS_ACTIONS + 7u; }

static void item(uint16_t row, ui_item_t *it)
{
	xsuite_config_t c;
	xsuite_get_config(&c);
	if (row < XS_ACTIONS) {
		static const tr_id_t actions[] = {
			TRN(XSUITE_ACTION_CHECKIN, "Check in / OK"), TRN(XSUITE_ACTION_SOS, "SOS - send"),
			TRN(XSUITE_ACTION_MEDICAL, "Need medical help"), TRN(XSUITE_ACTION_HELP, "Need assistance"),
			TRN(XSUITE_ACTION_RTB, "Returning to base"), TRN(XSUITE_ACTION_ARRIVED, "Arrived"),
			TRN(XSUITE_ACTION_CLEAR, "All clear - send"), TRN(XSUITE_ACTION_SITREP, "Status report"),
			TRN(XSUITE_ACTION_CONTACT, "Report observation"), TRN(XSUITE_ACTION_WATER, "Request water"),
			TRN(XSUITE_ACTION_FOOD, "Request food"), TRN(XSUITE_ACTION_FUEL, "Request fuel"),
			TRN(XSUITE_ACTION_SUPPLIES, "Request medical supplies"), TRN(XSUITE_ACTION_TRANSPORT, "Request transport"),
			TRN(XSUITE_ACTION_EQUIPMENT, "Request equipment"), TRN(XSUITE_ACTION_TASK_ACK, "Task acknowledged"),
			TRN(XSUITE_ACTION_TASK_DONE, "Task completed"), TRN(XSUITE_ACTION_MESSAGE, "Reply: received"),
			TRN(XSUITE_ACTION_HAZARD, "Report hazard") };
		str_copy(it->label, sizeof it->label, tr(actions[row]));
		if (row == XS_SOS) it->flags |= UI_ITEM_WARN;
		return;
	}
	static const tr_id_t labels[] = { TRN(XSUITE_AUTO, "Automatic check-ins"), TRN(XSUITE_INTERVAL, "Report interval"),
		TRN(XSUITE_PATH, "Send via"), TRN(XSUITE_MOVEMENT, "Movement reporting"), TRN(XSUITE_INBOX, "Received reports"),
		TRN(XSUITE_APRS_OPTIONS, "APRS options"), TRN(XSUITE_WRITE_MESSAGE, "Write message") };
	str_copy(it->label, sizeof it->label, tr(labels[row - XS_ACTIONS]));
	sbuf_t b;
	sb_init(&b, it->value, sizeof it->value);
	switch (row - XS_ACTIONS) {
	case 0: sb_str(&b, c.enabled ? TR(VALUE_ON, "On") : TR(VALUE_OFF, "Off")); break;
	case 1: sb_u32(&b, c.interval_s, 1); sb_str(&b, "s"); break;
	case 2: sb_str(&b, c.via == XSUITE_DMR ? "DMR" : c.via == XSUITE_BOTH ? "APRS + DMR" : "APRS"); break;
	case 3: if (!c.distance_m) sb_str(&b, TR(VALUE_OFF, "Off")); else { sb_u32(&b, c.distance_m, 1); sb_str(&b, "m"); } break;
	default: break;
	}
}

static void select(uint16_t row, bool hold)
{
	if (row < XS_ACTIONS) {
		if (hold && row != XS_CHECKIN && row != XS_RTB && row != XS_SOS && row != XS_ALL_CLEAR) { edit_open((uint8_t)row); return; }
		ui_toast(xsuite_action((uint8_t)row) ? TR(XSUITE_REPORT_QUEUED, "Report queued") : TR(XSUITE_WAIT_SEND, "A report is sending"));
		ui_dirty(); return;
	}
	xsuite_config_t c;
	xsuite_get_config(&c);
	switch (row - XS_ACTIONS) {
	case 0: c.enabled = !c.enabled; break;
	case 1: {
		static const uint16_t periods[] = { 60, 120, 300, 600, 900, 1800 };
		unsigned i = 0; while (i < 6 && periods[i] <= c.interval_s) i++;
		c.interval_s = periods[i % 6]; break;
	}
	case 2: c.via = (uint8_t)((c.via + 1u) % 3u); break;
	case 3: c.distance_m = c.distance_m == 0 ? 100 : c.distance_m == 100 ? 250 : c.distance_m == 250 ? 500 : c.distance_m == 500 ? 1000 : 0; break;
	case 4: ui_push(SCR_XSUITE_INBOX, 0); return;
	case 5: ui_push(SCR_APRS, 0); return;
	case 6: edit_open(XS_MESSAGE); return;
	default: return;
	}
	(void)xsuite_set_config(&c, hal_millis()); ui_dirty();
}

static const char *body(void)
{
	static char text[156];
	xsuite_view_t v; xsuite_config_t c;
	xsuite_get_view(&v); xsuite_get_config(&c);
	sbuf_t b;
	sb_init(&b, text, sizeof text);
	if (v.call[0]) sb_str(&b, v.call);
	if (v.own_dmr) { sb_str(&b, " DMR "); sb_u32(&b, v.own_dmr, 1); }
	if (c.flags & XSUITE_SOS) { sb_chr(&b, '\n'); sb_str(&b, TR(XSUITE_SOS_ACTIVE, "SOS ACTIVE")); }
	sb_chr(&b, '\n'); sb_str(&b, v.gps_valid ? TR(XSUITE_GPS_READY, "GPS ready") : TR(XSUITE_GPS_WAIT, "Waiting for GPS"));
	static const tr_id_t states[] = { TRN(XSUITE_READY, "Ready"), TRN(XSUITE_QUEUED, "Queued"), TRN(XSUITE_TRANSMITTED, "Transmitted"),
		TRN(XSUITE_IMPORTED, "Received at XTOC/XCOM"), TRN(XSUITE_FAILED, "Send failed - retry"),
		TRN(XSUITE_TIME_WAIT, "Waiting for GPS time"), TRN(XSUITE_POSITION_WAIT, "Waiting for position"), TRN(XSUITE_SETUP, "Finish setup in XTOC/XCOM") };
	sb_chr(&b, '\n'); sb_str(&b, tr(states[v.tx_state <= XS_TX_SETUP ? v.tx_state : XS_TX_IDLE]));
	return text;
}

const ui_scr_def_t ui_xsuite_screen = {
	.name = "xsuite", .flags = SF_LIST | SF_FIELD, .prefix = TRX(XSUITE_TITLE, "X Suite"),
	.hint = TRN(XSUITE_SEND_HINT, "OK sends  Hold edits"), .count = count, .item = item, .select = select, .body = body,
};

static uint16_t edit_count(void) { return 4; }
static void edit_item(uint16_t row, ui_item_t *it)
{
	static const tr_id_t names[] = { TRN(XSUITE_EDIT_NOTE, "Text / note"), TRN(XSUITE_EDIT_QUANTITY, "Quantity / count"),
		TRN(XSUITE_EDIT_PRIORITY, "Priority"), TRN(XSUITE_EDIT_SEND, "Send report") };
	str_copy(it->label, sizeof it->label, tr(names[row]));
	sbuf_t b; sb_init(&b, it->value, sizeof it->value);
	if (!row) sb_str(&b, edit_note);
	else if (row == 1) sb_u32(&b, edit_quantity, 1);
	else if (row == 2) { static const tr_id_t priorities[] = { TRN(XSUITE_PRI_FLASH, "FLASH"), TRN(XSUITE_PRI_IMMEDIATE, "Immediate"), TRN(XSUITE_PRI_PRIORITY, "Priority"), TRN(XSUITE_PRI_ROUTINE, "Routine") }; sb_str(&b, tr(priorities[edit_priority])); }
}
static void edit_select(uint16_t row, bool hold)
{
	if (!row) { ui_menus_xsuite_text(edit_note, sizeof edit_note - 1u); return; }
	if (row == 1) { unsigned q = edit_quantity + (hold ? 10u : 1u); edit_quantity = q > 999u ? 1u : (uint16_t)q; }
	if (row == 2) edit_priority = (uint8_t)((edit_priority + 1u) % 4u);
	if (row == 3) {
		if (edit_action == XS_MESSAGE && !edit_note[0]) { ui_menus_xsuite_text(edit_note, sizeof edit_note - 1u); return; }
		bool ok = xsuite_action_edit(edit_action, edit_note, edit_quantity, edit_priority);
		ui_toast(ok ? TR(XSUITE_REPORT_QUEUED, "Report queued") : TR(XSUITE_WAIT_SEND, "A report is sending"));
		if (ok) ui_pop();
	}
	ui_dirty();
}
const ui_scr_def_t ui_xsuite_edit_screen = {
	.name = "xsuite-edit", .flags = SF_LIST | SF_FIELD, .prefix = TRN(XSUITE_EDIT_TITLE, "Edit report"),
	.hint = TRN(XSUITE_EDIT_HINT, "OK changes  Back"), .count = edit_count, .item = edit_item, .select = edit_select,
};

static uint16_t inbox_count(void)
{
	xsuite_view_t v; xsuite_get_view(&v); return v.rx_serial < 2 ? (uint16_t)v.rx_serial : 2;
}
static void inbox_item(uint16_t row, ui_item_t *it)
{
	xsuite_view_t v; xsuite_rx_t r;
	xsuite_get_view(&v);
	if (!xsuite_receive_next(v.rx_serial - (row + 1u), &r)) return;
	sbuf_t b; sb_init(&b, it->label, sizeof it->label);
	if (r.src_call[0]) sb_str(&b, r.src_call); else { sb_str(&b, "DMR "); sb_u32(&b, r.src_dmr, 1); }
	str_copy(it->value, sizeof it->value, r.bearer == XSUITE_DMR ? "DMR" : "APRS");
}
static const char *inbox_body(void)
{
	static char text[320]; xsuite_rx_t r; xsuite_view_t v;
	xsuite_get_view(&v);
	if (!v.rx_serial || !xsuite_receive_next(v.rx_serial - 1u, &r)) return TR(XSUITE_NO_REPORTS, "No reports received");
	xsuite_inbox_summary(text, sizeof text);
	if (!text[0]) xsuite_packet_summary(r.text, text, sizeof text);
	return text;
}
const ui_scr_def_t ui_xsuite_inbox_screen = {
	.name = "xsuite-inbox", .flags = SF_LIST | SF_FIELD, .prefix = TRN(XSUITE_INBOX, "Received reports"),
	.hint = TRN(HINT_0_9_PICK_BACK, "0-9 pick  Back"), .count = inbox_count, .item = inbox_item, .body = inbox_body,
};
#endif

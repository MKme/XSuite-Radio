// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 MKME Media
// Actual engine/host handlers with deterministic board services; nothing opens or keys a radio.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "xsuite/xsuite.h"
#include "settings/settings.h"
#include "aprs/aprs.h"
#include "dmr/dmr_data_api.h"
#include "board.h"
#include "xsuite-multipart.h"

const board_t board = { 0 };
static settings_t saved;
static uint32_t now;
static bool fix = true;
static unsigned sends;
static char sent[257];
uint32_t hal_millis(void) { return now; }
void storage_quick_lock(void) { }
void storage_quick_unlock(void) { }
void settings_read(settings_t *s) { *s = saved; }
settings_t *settings_begin(void) { return &saved; }
void settings_end(uint32_t ms) { (void)ms; }
bool settings_flush(void) { return true; }
uint32_t codeplug_dmr_id(void) { return 123; }
uint8_t position_fix(position_fix_t *p, uint32_t ms)
{
	memset(p, 0, sizeof *p); p->lat = 12345600; p->lon = 23456700; p->ms = ms;
	return fix ? POSITION_SRC_GPS : POSITION_SRC_NONE;
}
bool position_utc_fields(uint32_t *date, uint32_t *hms, uint32_t ms)
{
	(void)ms; *date = 221126; *hms = 120000; return fix;
}
void aprs_get_view(aprs_view_t *v) { memset(v, 0, sizeof *v); strcpy(v->call, "N1TEST-7"); }
bool aprs_send_packet(const char *to, const char *text) { (void)to; strcpy(sent, text); sends++; char info[258]; info[0] = '>'; strcpy(info + 1, text); xsuite_aprs_complete((const uint8_t *)info, strlen(info), true); return true; }
bool sms_send(uint32_t dst, bool group, const char *text, bool ack, dmr_sms_format_t f)
{ (void)dst; (void)group; (void)ack; (void)f; strcpy(sent, text); sends++; return true; }
dmr_req_state_t sms_send_state(void) { return DMR_REQ_OK; }

static void tick(uint32_t ms) { now = ms; xsuite_tick(ms, 123, true); }
int main(void)
{
	xsuite_config_t c; xsuite_view_t v;
	xsuite_init(0); assert(!xsuite_set_epoch(0)); assert(xsuite_set_epoch(0x12345678)); assert(!xsuite_set_epoch(77)); tick(1000); assert(sends == 0);
	xsuite_get_config(&c); assert(!c.enabled && c.interval_s == 300);
	c.enabled = 1; c.interval_s = 60; assert(xsuite_set_config(&c, now));
	tick(31000); assert(sends == 1 && !strncmp(sent, "X1.4.C.", 7));
	tick(32000); assert(sends == 1);
	assert(xsuite_action(XS_SOS)); tick(33000);
	assert(sends == 2 && !strncmp(sent, "X1.9.C.", 7));
	xsuite_get_view(&v); char first_id[17]; strcpy(first_id, v.last_id); assert(!strncmp(first_id, "12345678", 8));
	xsuite_get_config(&c); assert(c.flags & XSUITE_SOS);
	xsuite_init(34000); xsuite_get_config(&c); assert(c.flags & XSUITE_SOS);
	tick(65000); assert(sends == 3);
	xsuite_get_view(&v); assert(!strncmp(v.last_id, first_id, 8) && strcmp(v.last_id, first_id) > 0 && v.epoch_ready);
	char summary[80]; xsuite_packet_summary(sent, summary, sizeof summary); assert(strstr(summary, "HELP"));
	assert(xsuite_action(XS_ALL_CLEAR)); tick(66000); xsuite_get_config(&c); assert(!(c.flags & XSUITE_SOS));
	assert(!xsuite_action(255));
	fix = false; assert(xsuite_action(XS_SOS)); tick(67000); assert(sends == 5);
	xsuite_packet_summary(sent, summary, sizeof summary); assert(!strcmp(summary, "SOS"));
	assert(xsuite_tx_begin(XSUITE_APRS, 0, 0, "", 8));
	assert(!xsuite_tx_chunk(1, (const uint8_t *)"X1.4.C.x", 8));
	assert(!xsuite_tx_commit()); assert(xsuite_tx_chunk(0, (const uint8_t *)"X1.4.C.x", 8));
	assert(!xsuite_tx_commit());
	const char *valid = "X1.13.C.TEST.1/1.Af__AABSAAECAwQAABBIRUxMTw";
	assert(xsuite_tx_begin(XSUITE_APRS, 0, 0, "", strlen(valid)));
	assert(xsuite_tx_chunk(0, (const uint8_t *)valid, strlen(valid)));
	assert(xsuite_tx_commit()); tick(68000); assert(sends == 6);
	assert(!xsuite_tx_begin(XSUITE_APRS, 0, 0, "", 257));
	assert(!xsuite_tx_begin(XSUITE_APRS, 0, 0, "BAD!", 8));
	assert(xsuite_tx_begin(XSUITE_APRS, 0, 0, "", 7));
	assert(xsuite_tx_chunk(0, (const uint8_t *)"garbage", 7)); assert(!xsuite_tx_commit());
	const uint8_t a[] = ">position";
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N2TEST-7", "APXTOC", a, sizeof a - 1);
	xsuite_receive(XSUITE_DMR, 456, 123, false, NULL, NULL, a, sizeof a - 1);
	xsuite_rx_t rx;
	assert(xsuite_receive_next(0, &rx) && rx.serial == 1 && !strcmp(rx.src_call, "N2TEST-7"));
	xsuite_receive(XSUITE_DMR, 789, 123, true, NULL, NULL, a, sizeof a - 1);
	xsuite_get_view(&v); assert(v.rx_dropped == 1 && v.rx_serial == 3);
	assert(xsuite_receive_next(0, &rx) && rx.serial == 2 && rx.src_dmr == 456);
	uint8_t out[1024], poll[] = { 5,0,0,0,0 }, config[] = { 1 }, status[] = { 0 };
	assert(xsuite_debug_command(poll, sizeof poll, out, sizeof out) == 37 + sizeof a - 1);
	assert(out[0] == 0 && out[5] == XSUITE_DMR && out[7] == (456 & 255));
	assert(xsuite_debug_command(config, 1, out, sizeof out) == 23 && out[1] == 1);
	assert(xsuite_debug_command(status, 1, out, sizeof out) > 100);
	assert(!memcmp(out + 1, "xsuite.proto=1", 14));
	memset(&saved, 0, sizeof saved); xsuite_init(0); xsuite_get_view(&v); assert(!v.epoch_ready);
	assert(xsuite_set_epoch(0x87654321)); assert(xsuite_action(XS_SOS)); tick(1000); xsuite_get_view(&v); assert(strncmp(v.last_id, first_id, 8));
	// One large command composed by the real application's encoder; 67 RF fragments, source-scoped and bounded.
	const char *payload = strrchr(xsuite_command, '.') + 1;
	size_t payload_len = strlen(payload), width = (payload_len + 66u) / 67u;
	unsigned total = (unsigned)((payload_len + width - 1u) / width);
	for (unsigned i = 0; i < total; i++) {
		char fragment[257]; size_t left = payload_len - i * width, n = left < width ? left : width;
		snprintf(fragment, sizeof fragment, "X1.13.C.BASECOMMAND.%u/%u.%.*s", i + 1u, total, (int)n, payload + i * width);
		if (i == 1) xsuite_receive(XSUITE_DMR, 999, 123, false, NULL, NULL, (const uint8_t *)fragment, strlen(fragment));
		xsuite_receive(XSUITE_DMR, 456, 123, false, NULL, NULL, (const uint8_t *)fragment, strlen(fragment));
	}
	char command_preview[320]; xsuite_inbox_summary(command_preview, sizeof command_preview);
	assert(strlen(command_preview) == sizeof command_preview - 1 && !strncmp(command_preview, xsuite_command_text, sizeof command_preview - 1));
	puts("PASS X Suite runtime: saved identity-free profile, automatic reports, persistent SOS, no-GPS SOS, chunk bounds and source-aware polling");
	return 0;
}

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
static uint32_t last_dmr_dst;
static bool last_dmr_group;
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
{ (void)ack; (void)f; last_dmr_dst = dst; last_dmr_group = group; strcpy(sent, text); sends++; return true; }
dmr_req_state_t sms_send_state(void) { return DMR_REQ_OK; }

static void tick_mode(uint32_t ms, bool dmr, uint32_t target, bool private_call)
{ now = ms; xsuite_tick(ms, 123, dmr, target, private_call); }
static void tick(uint32_t ms) { tick_mode(ms, true, 91, false); }
static void host_dmr(uint32_t dst, uint8_t flags)
{
	const char *wire = "X1.13.C.CHANNEL.1/1.Af__AABSAAECAwQAABBIRUxMTw";
	uint8_t begin[19] = { 2, XSUITE_DMR, flags }, chunk[64] = { 3, 0, 0 }, commit[] = { 4 }, out[1024];
	for (unsigned i = 0; i < 4; i++) begin[3 + i] = (uint8_t)(dst >> (i * 8));
	begin[17] = (uint8_t)strlen(wire);
	assert(xsuite_debug_command(begin, sizeof begin, out, sizeof out) == 1 && out[0] == 0);
	memcpy(chunk + 3, wire, strlen(wire));
	assert(xsuite_debug_command(chunk, 3 + strlen(wire), out, sizeof out) == 1 && out[0] == 0);
	assert(xsuite_debug_command(commit, sizeof commit, out, sizeof out) == 1 && out[0] == 0);
}
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
	tick(78000); // An invalid, abandoned stage expires without sending any partial bytes.
	const char *valid = "X1.13.C.TEST.1/1.Af__AABSAAECAwQAABBIRUxMTw";
	assert(xsuite_tx_begin(XSUITE_APRS, 0, 0, "", strlen(valid)));
	assert(xsuite_tx_chunk(0, (const uint8_t *)valid, strlen(valid)));
	assert(xsuite_tx_commit()); tick(79000); assert(sends == 6);
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
	poll[1] = 2;
	assert(xsuite_debug_command(poll, sizeof poll, out, sizeof out) == 37 + sizeof a - 1);
	assert(out[0] == 0 && out[5] == XSUITE_DMR && out[6] == 0x02); // RX group uses bit 1, never SOS bit 0.
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
		if (i == total / 2) xsuite_receive(XSUITE_APRS, 0, 0, false, "N3TEST-7", "APXTOC", a, sizeof a - 1);
		xsuite_receive(XSUITE_DMR, 456, 123, false, NULL, NULL, (const uint8_t *)fragment, strlen(fragment));
	}
	char command_preview[352], after_beacon[352]; xsuite_inbox_summary(command_preview, sizeof command_preview);
	assert(!strncmp(command_preview, "DMR 456\n", 8));
	assert(strlen(command_preview) == 8 + 319 && !strncmp(command_preview + 8, xsuite_command_text, 319));
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N4TEST-7", "APXTOC", a, sizeof a - 1);
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N5TEST-7", "APXTOC", a, sizeof a - 1);
	xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, command_preview));
	xsuite_get_view(&v); assert(xsuite_receive_next(v.rx_serial - 1, &rx) && !strcmp(rx.src_call, "N5TEST-7"));
	char native_message[257];
	assert(xsuite_packet_edit(XS_MESSAGE, false, 0, 0, 12345, 1, 1, false, "Proceed to BRAVO", 1, 3, native_message, sizeof native_message));
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N2TEST-7", "APXTOC", (const uint8_t *)native_message, strlen(native_message));
	xsuite_inbox_summary(command_preview, sizeof command_preview); assert(!strcmp(command_preview, "N2TEST-7 APRS\nProceed to BRAVO"));
	xsuite_receive(XSUITE_DMR, 999, 123, false, NULL, NULL, a, sizeof a - 1);
	xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, command_preview));
	// Automatic native receipts update delivery while leaving the operator's readable command and source intact.
	fix = true; xsuite_get_config(&c); strcpy(c.dst_call, "N2TEST-7"); assert(xsuite_set_config(&c, now));
	assert(xsuite_action(XS_CHECKIN)); tick(2000); xsuite_get_view(&v);
	assert(!strcmp(v.last_id, xsuite_ack_id) && v.tx_state == XS_TX_SENT);
	assert(xsuite_packet_ack(xsuite_ack, NULL) && !xsuite_packet_ack(xsuite_readable_ack, NULL));
	const char *receipts[] = { xsuite_old_ack, xsuite_ack, xsuite_ack, xsuite_old_ack };
	const char *receipt_sources[] = { "N2TEST-7", "N3TEST-7", "N2TEST-7", "N2TEST-7" };
	for (unsigned i = 0; i < 4; i++) {
		xsuite_receive(XSUITE_APRS, 0, 0, false, receipt_sources[i], "N1TEST-7", (const uint8_t *)receipts[i], strlen(receipts[i]));
		xsuite_get_view(&v); assert(v.tx_state == (i < 2 ? XS_TX_SENT : XS_TX_IMPORTED));
		xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, command_preview));
		assert(xsuite_receive_next(v.rx_serial - 1, &rx) && !strcmp(rx.text, receipts[i]));
	}
	// UI recipient/echo checks do not remove frames from the unchanged host receive stream.
	char addressed[257]; addressed[0] = ':'; memset(addressed + 1, ' ', 9); memcpy(addressed + 1, "OTHER-7", 7);
	addressed[10] = ':'; strcpy(addressed + 11, native_message);
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N3TEST-7", "N1TEST-7", (const uint8_t *)addressed, strlen(addressed));
	xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, command_preview));
	xsuite_get_view(&v); assert(xsuite_receive_next(v.rx_serial - 1, &rx) && !strcmp(rx.text, addressed));
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N4TEST-7", "OTHER-7", (const uint8_t *)native_message, strlen(native_message));
	xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, command_preview));
	xsuite_get_view(&v); assert(xsuite_receive_next(v.rx_serial - 1, &rx) && !strcmp(rx.dst_call, "OTHER-7"));
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N1TEST-7", "APXTOC", (const uint8_t *)native_message, strlen(native_message));
	xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, command_preview));
	memset(addressed + 1, ' ', 9); memcpy(addressed + 1, "N1TEST-7", 8);
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N6TEST-7", APRS_TOCALL, (const uint8_t *)addressed, strlen(addressed));
	xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, "N6TEST-7 APRS\nProceed to BRAVO"));
	xsuite_receive(XSUITE_APRS, 0, 0, false, "N7TEST-7", "N1TEST-7", (const uint8_t *)native_message, strlen(native_message));
	xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, "N7TEST-7 APRS\nProceed to BRAVO"));
	const char *broadcasts[] = { "APXTOC", "APRS", APRS_TOCALL };
	for (unsigned i = 0; i < 3; i++) {
		char source[10], expect[80]; snprintf(source, sizeof source, "N%uTEST-7", i + 2);
		snprintf(expect, sizeof expect, "%s APRS\nProceed to BRAVO", source);
		xsuite_receive(XSUITE_APRS, 0, 0, false, source, broadcasts[i], (const uint8_t *)native_message, strlen(native_message));
		xsuite_inbox_summary(after_beacon, sizeof after_beacon); assert(!strcmp(after_beacon, expect));
	}
	// DMR defaults use the selected channel/contact. Explicit destinations are independent of that contact.
	memset(&saved, 0, sizeof saved); fix = true; xsuite_init(0); assert(xsuite_set_epoch(1));
	xsuite_get_config(&c); c.via = XSUITE_DMR; assert(xsuite_set_config(&c, 0));
	unsigned before = sends;
	assert(xsuite_action(XS_CHECKIN)); tick_mode(1000, true, 91, false);
	assert(sends == before + 1 && last_dmr_dst == 91 && last_dmr_group);
	assert(xsuite_action(XS_MESSAGE)); tick_mode(2000, true, 456, true);
	assert(sends == before + 2 && last_dmr_dst == 456 && !last_dmr_group);
	host_dmr(0, 0); tick_mode(3000, true, 92, false);
	assert(sends == before + 3 && last_dmr_dst == 92 && last_dmr_group);
	host_dmr(0, XSUITE_GROUP); tick_mode(4000, true, 789, true);
	assert(sends == before + 4 && last_dmr_dst == 789 && !last_dmr_group);
	host_dmr(7654321, 0); tick_mode(5000, false, 91, false); assert(sends == before + 4);
	xsuite_get_view(&v); assert(v.tx_state == XS_TX_QUEUED);
	tick_mode(6000, true, 91, false);
	assert(sends == before + 5 && last_dmr_dst == 7654321 && !last_dmr_group);
	host_dmr(12345, XSUITE_GROUP); tick_mode(7000, true, 456, true);
	assert(sends == before + 6 && last_dmr_dst == 12345 && last_dmr_group);
	uint8_t bad_group[19] = { 2, XSUITE_DMR, 0x01 }; bad_group[17] = 20;
	assert(xsuite_debug_command(bad_group, sizeof bad_group, out, sizeof out) == 1 && out[0] != 0);
	assert(xsuite_action(XS_CHECKIN)); tick_mode(8000, false, 0, true); assert(sends == before + 6);
	tick_mode(9000, true, 0, true);
	assert(sends == before + 7 && last_dmr_dst == 9 && last_dmr_group);
	// A due auto report and menu activity cannot overwrite a host's incomplete native packet.
	memset(&saved, 0, sizeof saved); now = 0; xsuite_init(0); assert(xsuite_set_epoch(2));
	xsuite_get_config(&c); c.enabled = 1; c.interval_s = 30; assert(xsuite_set_config(&c, 0));
	now = 29000; before = sends;
	assert(xsuite_tx_begin(XSUITE_APRS, 0, 0, "", strlen(valid)));
	assert(xsuite_tx_chunk(0, (const uint8_t *)valid, 10));
	assert(!xsuite_tx_commit());
	assert(!xsuite_tx_begin(XSUITE_APRS, 0, 0, "", strlen(valid)));
	tick(31000); assert(sends == before);
	assert(!xsuite_action(XS_SITREP)); tick(32000); assert(sends == before);
	assert(xsuite_tx_chunk(10, (const uint8_t *)valid + 10, strlen(valid) - 10));
	assert(xsuite_tx_commit()); tick(33000); assert(sends == before + 1 && !strcmp(sent, valid));
	tick(34000);
	assert(xsuite_tx_begin(XSUITE_APRS, 0, 0, "", strlen(valid)));
	assert(xsuite_tx_chunk(0, (const uint8_t *)valid, 10));
	tick(45001); assert(sends == before + 1);
	assert(!xsuite_tx_chunk(10, (const uint8_t *)valid + 10, strlen(valid) - 10) && !xsuite_tx_commit());
	assert(xsuite_action(XS_MESSAGE));
	assert(!xsuite_tx_begin(XSUITE_APRS, 0, 0, "", strlen(valid))); // The queued menu action wins first.
	tick(46000); assert(sends == before + 2);
	// An urgent menu action remains queued while a valid staged host transaction finishes intact.
	now = 47000;
	assert(xsuite_tx_begin(XSUITE_APRS, 0, 0, "", strlen(valid)));
	assert(xsuite_tx_chunk(0, (const uint8_t *)valid, 10));
	assert(xsuite_action(XS_SOS)); tick(48000); assert(sends == before + 2);
	assert(xsuite_tx_chunk(10, (const uint8_t *)valid + 10, strlen(valid) - 10));
	assert(xsuite_tx_commit()); tick(49000); assert(sends == before + 3 && !strcmp(sent, valid));
	tick(50000); assert(sends == before + 4 && !strncmp(sent, "X1.9.C.", 7));
	now = 51000;
	assert(xsuite_tx_begin(XSUITE_APRS, 0, 0, "", strlen(valid)));
	assert(xsuite_tx_chunk(0, (const uint8_t *)valid, 10));
	assert(xsuite_action(XS_SOS)); tick(62000); // The host disappears; queued SOS still runs after stage expiry.
	xsuite_get_view(&v); assert(sends == before + 5 && v.tx_state == XS_TX_SENT && !strncmp(sent, "X1.9.C.", 7));
	puts("PASS X Suite runtime: own/broadcast APRS preview filtering, control receipts preserving readable inbox/source, raw forwarding, multipart, DMR addressing and wire group bit2, staging versus auto/menu, timeout recovery and queued SOS");
	return 0;
}

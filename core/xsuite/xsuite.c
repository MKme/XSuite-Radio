// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 MKME Media
// Native XTOC packets over existing APRS/DMR services. The roster belongs to the receiving application.
#include "config.h"
#if CONFIG_XSUITE
#include <string.h>
#include "xsuite/xsuite.h"
#include "aprs/aprs.h"
#include "dmr/dmr_data_api.h"
#include "codeplug/codeplug.h"
#include "position/position.h"
#include "storage/storage.h"
#include "settings/settings.h"
#include "board.h"
#include "hal_time.h"

#define LOCK() storage_quick_lock()
#define UNLOCK() storage_quick_unlock()
#define RX_COUNT 2u
#define NO_ACTION 255u
static struct {
	xsuite_config_t cfg;
	xsuite_view_t view;
	xsuite_rx_t rx[RX_COUNT];
	uint8_t rx_count, action, tx_bearer, tx_flags, tx_paths;
	uint32_t tx_dst, sequence, reserved, tag, next_ms, last_ms, clock_seconds, clock_ms;
	int32_t last_lat, last_lon;
	bool last_fix, tx_ready, dmr_wait, aprs_wait, ids_ready;
	uint16_t tx_length, tx_have;
	char tx_call[10], tx_text[XSUITE_TEXT_MAX + 1];
	char note[65], inbox[320];
	uint16_t quantity;
	uint8_t priority;
	bool edited;
	struct { uint32_t src; uint8_t bearer; char call[10], id[33]; unsigned tpl, next, total; size_t have;
		uint32_t ms; char data[2820]; } assembly;
} xs;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void copy_call(char *dst, const char *src) { size_t n = src == NULL ? 0 : strnlen(src, 9); if (n) memcpy(dst, src, n); dst[n] = '\0'; }

static void config_bytes(uint8_t out[XSUITE_CONFIG_BYTES])
{
	memset(out, 0, XSUITE_CONFIG_BYTES);
	out[0] = 1; out[1] = xs.cfg.enabled; out[2] = xs.cfg.via; out[3] = xs.cfg.flags;
	wr16(out + 4, xs.cfg.interval_s); wr16(out + 6, xs.cfg.distance_m); wr32(out + 8, xs.cfg.dst_dmr);
	memcpy(out + 12, xs.cfg.dst_call, 10);
}

static void save(uint32_t now)
{
	uint8_t bytes[XSUITE_CONFIG_BYTES];
	LOCK(); config_bytes(bytes); uint32_t sequence = xs.reserved; UNLOCK();
	settings_t *s = settings_begin();
	memcpy(s->xsuite, bytes, sizeof bytes);
	wr32(s->xsuite + 24, sequence);
	wr32(s->xsuite + 28, xs.tag);
	settings_end(now);
}

void xsuite_init(uint32_t now)
{
	memset(&xs, 0, sizeof xs);
	xs.action = NO_ACTION;
	xs.cfg.interval_s = 300; xs.cfg.via = XSUITE_APRS;
	settings_t s;
	settings_read(&s);
	if (s.xsuite[0] == 1) {
		xs.cfg.enabled = s.xsuite[1] == 1; xs.cfg.via = s.xsuite[2] <= XSUITE_BOTH ? s.xsuite[2] : XSUITE_APRS;
		xs.cfg.flags = s.xsuite[3] & (XSUITE_SOS | XSUITE_GROUP);
		xs.cfg.interval_s = rd16(s.xsuite + 4);
		if (xs.cfg.interval_s < 30 || xs.cfg.interval_s > 3600) xs.cfg.interval_s = 300;
		xs.cfg.distance_m = rd16(s.xsuite + 6); xs.cfg.dst_dmr = rd32(s.xsuite + 8) & 0xFFFFFFu;
		memcpy(xs.cfg.dst_call, s.xsuite + 12, 10); xs.cfg.dst_call[9] = '\0';
	}
	xs.sequence = s.xsuite[0] == 1 ? rd32(s.xsuite + 24) : 0;
	xs.reserved = xs.sequence + 256u;
	xs.tag = s.xsuite[0] == 1 ? rd32(s.xsuite + 28) : 0;
	xs.view.epoch_ready = xs.tag != 0;
	xs.next_ms = now + 30000u;
	save(now);
	xs.ids_ready = settings_flush(); // Commit the entire block before its first on-air ID is used.
}

void xsuite_get_config(xsuite_config_t *out) { LOCK(); *out = xs.cfg; UNLOCK(); }
void xsuite_get_view(xsuite_view_t *out) { LOCK(); *out = xs.view; UNLOCK(); }

bool xsuite_set_config(const xsuite_config_t *c, uint32_t now)
{
	if (c->enabled > 1 || c->via > XSUITE_BOTH || c->interval_s < 30 || c->interval_s > 3600 || c->dst_dmr > 0xFFFFFFu ||
	    (c->flags & ~(XSUITE_SOS | XSUITE_GROUP)) || c->dst_call[9] != '\0') return false;
	if (c->dst_call[0] != '\0') { ax25_addr_t a; if (!ax25_addr_parse(c->dst_call, strlen(c->dst_call), &a)) return false; }
	LOCK(); uint8_t sos = xs.cfg.flags & XSUITE_SOS; xs.cfg = *c; xs.cfg.flags = (xs.cfg.flags & ~XSUITE_SOS) | sos; UNLOCK();
	save(now);
	return true;
}

bool xsuite_action(uint8_t action)
{
	if (action >= XS_ACTIONS) return false;
	LOCK(); bool urgent = action == XS_SOS || action == XS_ALL_CLEAR;
	bool ok = urgent || (xs.action == NO_ACTION && !xs.tx_ready && xs.tx_have == 0);
	if (ok) { xs.action = action; xs.view.tx_state = XS_TX_QUEUED;
		xs.edited = false;
		if (action == XS_SOS) xs.cfg.flags |= XSUITE_SOS;
		if (action == XS_ALL_CLEAR) xs.cfg.flags &= ~XSUITE_SOS; }
	UNLOCK(); if (ok && urgent) { save(hal_millis()); (void)settings_flush(); } return ok;
}

bool xsuite_action_edit(uint8_t action, const char *note, uint16_t quantity, uint8_t priority)
{
	if (note == NULL || !note[0] || strlen(note) > 64 || priority > 3 || action == XS_CHECKIN || action == XS_RTB ||
	    action == XS_SOS || action == XS_ALL_CLEAR) return false;
	for (const char *p = note; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) return false;
	if (!xsuite_action(action)) return false;
	LOCK(); strcpy(xs.note, note); xs.quantity = quantity; xs.priority = priority; xs.edited = true; UNLOCK();
	return true;
}

void xsuite_inbox_summary(char *out, size_t cap)
{
	if (!cap) return;
	LOCK(); size_t n = strnlen(xs.inbox, cap - 1); memcpy(out, xs.inbox, n); out[n] = '\0'; UNLOCK();
}

void xsuite_aprs_complete(const uint8_t *info, size_t length, bool sent)
{
	size_t prefix = length && info[0] == '>' ? 1u : length > 11 && info[0] == ':' && info[10] == ':' ? 11u : 0u;
	if (!prefix) return;
	LOCK();
	if (xs.tx_ready && length - prefix == xs.tx_length && !memcmp(info + prefix, xs.tx_text, xs.tx_length)) {
		xs.tx_paths &= ~1u; xs.aprs_wait = false;
		if (!sent) xs.view.tx_state = XS_TX_FAILED;
	}
	UNLOCK();
}

static void receive_summary(const xsuite_rx_t *r)
{
	xsuite_packet_part_t p;
	if (!xsuite_packet_part(r->text, &p)) return;
	if (p.total == 1) { xsuite_packet_summary(r->text, xs.inbox, sizeof xs.inbox); return; }
	if (p.part == 1) {
		xs.assembly.src = r->src_dmr; xs.assembly.bearer = r->bearer; copy_call(xs.assembly.call, r->src_call);
		strcpy(xs.assembly.id, p.id); xs.assembly.tpl = p.tpl; xs.assembly.total = p.total; xs.assembly.next = 1;
		// Reuse the exact received wrapper, replacing only its part count with the canonical whole-packet marker.
		size_t prefix = (size_t)(p.payload - r->text);
		const char *end = p.payload - 1, *start = end;
		while (start > r->text && start[-1] != '.') start--;
		size_t head = (size_t)(start - r->text);
		(void)prefix;
		memcpy(xs.assembly.data, r->text, head); memcpy(xs.assembly.data + head, "1/1.", 4);
		xs.assembly.have = head + 4; xs.assembly.ms = hal_millis();
	}
	if (xs.assembly.bearer != r->bearer || xs.assembly.src != r->src_dmr || strcmp(xs.assembly.call, r->src_call) ||
	    strcmp(xs.assembly.id, p.id) || xs.assembly.tpl != p.tpl || xs.assembly.total != p.total ||
	    hal_millis() - xs.assembly.ms > 900000u || p.part != xs.assembly.next) return;
	if (p.payload_len >= sizeof xs.assembly.data - xs.assembly.have) { xs.assembly.next = 0; return; }
	memcpy(xs.assembly.data + xs.assembly.have, p.payload, p.payload_len); xs.assembly.have += p.payload_len;
	xs.assembly.data[xs.assembly.have] = '\0'; xs.assembly.next++;
	if (p.part == p.total) { xsuite_packet_summary(xs.assembly.data, xs.inbox, sizeof xs.inbox); xs.assembly.next = 0; }
}

void xsuite_set_time(uint32_t seconds, uint32_t now)
{
	if (seconds < 1577836800u) return;
	LOCK(); xs.clock_seconds = seconds; xs.clock_ms = now; UNLOCK();
}

bool xsuite_set_epoch(uint32_t epoch)
{
	if (!epoch) return false;
	LOCK(); bool ok = !xs.tag;
	if (ok) { xs.tag = epoch; xs.view.epoch_ready = true; }
	UNLOCK();
	if (ok) { save(hal_millis()); xs.ids_ready = settings_flush(); }
	return ok && xs.ids_ready;
}

void xsuite_receive(uint8_t bearer, uint32_t src, uint32_t dst, bool group, const char *call,
		    const char *to, const uint8_t *text, size_t length)
{
	if (!length || length > XSUITE_TEXT_MAX) return;
	for (size_t i = 0; i < length; i++) if (text[i] < 32 || text[i] > 126) return;
	LOCK();
	if (xs.rx_count == RX_COUNT) xs.view.rx_dropped++;
	else xs.rx_count++;
	memmove(xs.rx + 1, xs.rx, (RX_COUNT - 1u) * sizeof xs.rx[0]);
	xsuite_rx_t *r = xs.rx;
	memset(r, 0, sizeof *r); r->serial = ++xs.view.rx_serial;
	r->bearer = bearer; r->flags = group ? XSUITE_GROUP : 0; r->src_dmr = src; r->dst_dmr = dst;
	copy_call(r->src_call, call); copy_call(r->dst_call, to);
	r->length = (uint16_t)length; memcpy(r->text, text, length);
	receive_summary(r);
	bool base = bearer == XSUITE_DMR ? xs.cfg.dst_dmr && src == xs.cfg.dst_dmr && !group :
		xs.cfg.dst_call[0] && call != NULL && !strcmp(call, xs.cfg.dst_call);
	if (base && xsuite_packet_ack(r->text, xs.view.last_id)) xs.view.tx_state = XS_TX_IMPORTED;
	UNLOCK();
}

bool xsuite_receive_next(uint32_t after, xsuite_rx_t *out)
{
	bool found = false;
	LOCK();
	for (int i = (int)xs.rx_count - 1; i >= 0; i--) {
		if ((int32_t)(xs.rx[i].serial - after) > 0) { *out = xs.rx[i]; found = true; break; }
	}
	UNLOCK(); return found;
}

bool xsuite_tx_begin(uint8_t bearer, uint8_t flags, uint32_t dst, const char *call, uint16_t length)
{
	if (bearer > XSUITE_BOTH || flags & ~XSUITE_GROUP || dst > 0xFFFFFFu || !length || length > XSUITE_TEXT_MAX) return false;
	if (bearer != XSUITE_DMR && call != NULL && call[0]) { ax25_addr_t a; if (!ax25_addr_parse(call, strnlen(call, 10), &a)) return false; }
	LOCK(); bool ok = !xs.tx_ready && xs.action == NO_ACTION;
	if (ok) { xs.tx_bearer = bearer; xs.tx_flags = flags; xs.tx_dst = dst; copy_call(xs.tx_call, call);
		xs.tx_length = length; xs.tx_have = 0; memset(xs.tx_text, 0, sizeof xs.tx_text); }
	UNLOCK(); return ok;
}

bool xsuite_tx_chunk(uint16_t offset, const uint8_t *data, size_t n)
{
	if (!n || n > 61) return false;
	for (size_t i = 0; i < n; i++) if (data[i] < 32 || data[i] > 126) return false;
	LOCK(); bool ok = !xs.tx_ready && offset == xs.tx_have && offset + n <= xs.tx_length;
	if (ok) { memcpy(xs.tx_text + offset, data, n); xs.tx_have = (uint16_t)(offset + n); }
	UNLOCK(); return ok;
}

bool xsuite_tx_commit(void)
{
	xsuite_packet_part_t part;
	LOCK(); bool ok = !xs.tx_ready && xs.tx_length && xs.tx_have == xs.tx_length && xsuite_packet_part(xs.tx_text, &part);
	if (ok) { xs.tx_ready = true; xs.tx_paths = xs.tx_bearer == XSUITE_BOTH ? 3u : (uint8_t)(1u << xs.tx_bearer);
		xs.view.tx_state = XS_TX_QUEUED; xs.last_ms = 0; }
	UNLOCK(); return ok;
}

static bool moved(const position_fix_t *f)
{
	if (!xs.cfg.distance_m || !xs.last_fix) return false;
	// Conservative Manhattan distance; longitude is scaled by a cosine-free lower bound, then capped at high latitudes.
	int64_t lat = (int64_t)f->lat - xs.last_lat, lon = (int64_t)f->lon - xs.last_lon;
	if (lat < 0) lat = -lat;
	if (lon < 0) lon = -lon;
	return (lat + lon / 2) / 90 >= xs.cfg.distance_m;
}

void xsuite_tick(uint32_t now, uint32_t own, bool on_dmr)
{
	position_fix_t f;
	uint32_t date, hms, minutes = 0;
	bool fix = position_fix(&f, now) != POSITION_SRC_NONE;
	if (position_utc_fields(&date, &hms, now)) minutes = xsuite_unix_minutes(date, hms);
	if (!minutes && xs.clock_seconds) minutes = (xs.clock_seconds + (now - xs.clock_ms) / 1000u) / 60u;
	aprs_view_t av; aprs_get_view(&av);
	LOCK(); xs.view.own_dmr = own; xs.view.gps_valid = fix; xs.view.gps_age_s = fix ? (now - f.ms) / 1000u : UINT32_MAX;
	copy_call(xs.view.call, av.call);
	if (xs.cfg.enabled && fix && xs.action == NO_ACTION && !xs.tx_ready && now - xs.last_ms >= 30000u &&
	    ((int32_t)(now - xs.next_ms) >= 0 || moved(&f))) xs.action = XS_CHECKIN;
	uint8_t action = xs.action;
	UNLOCK();
	if (action != NO_ACTION && !xs.tx_ready) {
		if (!xs.tag) { xs.view.tx_state = XS_TX_SETUP; return; }
		if (!fix && (action == XS_CHECKIN || action == XS_RTB)) { xs.view.tx_state = XS_TX_NO_POSITION; return; }
		if (xs.sequence == xs.reserved) { xs.reserved += 256u; xs.ids_ready = false; save(now); }
		if (!xs.ids_ready) { xs.ids_ready = settings_flush(); if (!xs.ids_ready) { xs.view.tx_state = XS_TX_FAILED; return; } }
		uint32_t sequence = ++xs.sequence;
		size_t n = xsuite_packet_edit(action, fix, fix ? f.lat : 0, fix ? f.lon : 0, minutes, xs.tag, sequence,
			(xs.cfg.flags & XSUITE_SOS) != 0, xs.edited ? xs.note : NULL, xs.quantity, xs.priority, xs.tx_text, sizeof xs.tx_text);
		if (!n) { xs.view.tx_state = XS_TX_FAILED; xs.action = NO_ACTION; return; }
		LOCK();
		xs.tx_length = xs.tx_have = (uint16_t)n; xs.tx_bearer = xs.cfg.via; xs.tx_flags = xs.cfg.flags & XSUITE_GROUP;
		xs.tx_dst = xs.cfg.dst_dmr; copy_call(xs.tx_call, xs.cfg.dst_call);
		xs.tx_paths = xs.cfg.via == XSUITE_BOTH ? 3u : (uint8_t)(1u << xs.cfg.via);
		xs.tx_ready = true; xs.action = NO_ACTION; xs.last_ms = now;
		const char *id = strchr(xs.tx_text + 3, '.'); id = id ? strchr(id + 1, '.') : NULL;
		if (id) { memcpy(xs.view.last_id, id + 1, 16); xs.view.last_id[16] = '\0'; }
		UNLOCK();
		if (fix) { xs.last_lat = f.lat; xs.last_lon = f.lon; xs.last_fix = true; }
		xs.next_ms = now + (uint32_t)xs.cfg.interval_s * 1000u;
		save(now);
	}
	if (!xs.tx_ready) return;
	if (!xs.last_ms) xs.last_ms = now;
	if (now - xs.last_ms > 90000u) { xs.tx_ready = false; xs.tx_have = 0; xs.aprs_wait = false; xs.dmr_wait = false; xs.view.tx_state = XS_TX_FAILED; return; }
	if ((xs.tx_paths & 1u) && !xs.aprs_wait) {
		if (aprs_send_packet(xs.tx_call, xs.tx_text)) xs.aprs_wait = true;
	}
	if ((xs.tx_paths & 2u) && on_dmr && !xs.dmr_wait) {
		uint32_t dst = xs.tx_dst;
		if (!dst) { xs.view.tx_state = XS_TX_FAILED; xs.tx_ready = false; xs.tx_have = 0; return; }
		if (sms_send(dst, (xs.tx_flags & XSUITE_GROUP) != 0, xs.tx_text, true, DMR_SMS_FORMAT_ETSI_IP)) xs.dmr_wait = true;
	}
	if (xs.dmr_wait) {
		dmr_req_state_t state = sms_send_state();
		if (state == DMR_REQ_OK || state == DMR_REQ_FAILED) {
			xs.dmr_wait = false; xs.tx_paths &= ~2u;
			if (state == DMR_REQ_FAILED) xs.view.tx_state = XS_TX_FAILED;
		}
	}
	if (!xs.tx_paths) { xs.tx_ready = false; xs.aprs_wait = false; xs.tx_have = xs.tx_length = 0;
		if (xs.view.tx_state != XS_TX_FAILED && xs.view.tx_state != XS_TX_IMPORTED) xs.view.tx_state = XS_TX_SENT; }
}
#endif

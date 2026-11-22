// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 MKME Media
#include "config.h"
#if CONFIG_XSUITE
#include <string.h>
#include "xsuite/xsuite.h"
#include "debuglink/dl_frame.h"
#include "debuglink/dl_fmt.h"
#include "hal_time.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

size_t xsuite_debug_command(const uint8_t *req, uint16_t length, uint8_t *out, size_t cap)
{
	if (cap < 2) return 0;
	out[0] = DL_ST_BAD_ARG;
	if (!length) return 1;
	switch (req[0]) {
	case 0: {
		if (length != 1) break;
		xsuite_view_t v; xsuite_config_t c;
		xsuite_get_view(&v); xsuite_get_config(&c);
		dl_text_t t;
		dl_text_init(&t, (char *)out + 1, cap - 1);
		dl_text(&t, "xsuite.proto=1\nxsuite.call=%s\nxsuite.dmr_id=%u\nxsuite.aprs=1\nxsuite.dmr=1\n", v.call, (unsigned)v.own_dmr);
		dl_text(&t, "xsuite.gps_valid=%u\nxsuite.gps_age_s=%u\nxsuite.rx_serial=%u\nxsuite.rx_dropped=%u\n", v.gps_valid, (unsigned)v.gps_age_s, (unsigned)v.rx_serial, (unsigned)v.rx_dropped);
		dl_text(&t, "xsuite.tx_state=%u\nxsuite.sos=%u\nxsuite.last_id=%s\nxsuite.max_text=256\nxsuite.actions=%u\n", v.tx_state, (c.flags & XSUITE_SOS) != 0, v.last_id, XS_ACTIONS);
		dl_text(&t, "xsuite.epoch_ready=%u\n", v.epoch_ready);
		out[0] = DL_ST_OK; return 1u + t.len;
	}
	case 1: {
		xsuite_config_t c; xsuite_get_config(&c);
		if (length == 1u + XSUITE_CONFIG_BYTES) {
			const uint8_t *p = req + 1;
			if (p[0] != 1 || p[21] != '\0') break;
			c.enabled = p[1]; c.via = p[2]; c.flags = p[3]; c.interval_s = rd16(p + 4); c.distance_m = rd16(p + 6);
			c.dst_dmr = rd32(p + 8); memcpy(c.dst_call, p + 12, 10);
			if (!xsuite_set_config(&c, hal_millis())) break;
		} else if (length != 1) break;
		if (cap < 1u + XSUITE_CONFIG_BYTES) break;
		xsuite_get_config(&c);
		memset(out, 0, 1u + XSUITE_CONFIG_BYTES); out[1] = 1; out[2] = c.enabled; out[3] = c.via; out[4] = c.flags;
		wr16(out + 5, c.interval_s); wr16(out + 7, c.distance_m); wr32(out + 9, c.dst_dmr); memcpy(out + 13, c.dst_call, 10);
		return 1u + XSUITE_CONFIG_BYTES;
	}
	case 2:
		if (length != 19 || req[16] != '\0') break;
		out[0] = xsuite_tx_begin(req[1], req[2], rd32(req + 3), (const char *)req + 7, rd16(req + 17)) ? DL_ST_OK : DL_ST_BUSY;
		return 1;
	case 3:
		if (length < 4) break;
		out[0] = xsuite_tx_chunk(rd16(req + 1), req + 3, length - 3u) ? DL_ST_OK : DL_ST_BAD_ARG; return 1;
	case 4:
		if (length != 1) break;
		out[0] = xsuite_tx_commit() ? DL_ST_OK : DL_ST_REFUSED; return 1;
	case 5: {
		if (length != 5) break;
		xsuite_rx_t r;
		if (!xsuite_receive_next(rd32(req + 1), &r)) { out[0] = DL_ST_UNAVAILABLE; return 1; }
		if (cap < 37u + r.length) break;
		out[0] = DL_ST_OK; wr32(out + 1, r.serial); out[5] = r.bearer; out[6] = r.flags;
		wr32(out + 7, r.src_dmr); wr32(out + 11, r.dst_dmr);
		memcpy(out + 15, r.src_call, 10); memcpy(out + 25, r.dst_call, 10); wr16(out + 35, r.length);
		memcpy(out + 37, r.text, r.length); return 37u + r.length;
	}
	case 6:
		if (length != 2) break;
		out[0] = xsuite_action(req[1]) ? DL_ST_OK : DL_ST_BUSY; return 1;
	case 7:
		if (length != 5) break;
		xsuite_set_time(rd32(req + 1), hal_millis()); out[0] = DL_ST_OK; return 1;
	case 8:
		if (length != 5) break;
		out[0] = xsuite_set_epoch(rd32(req + 1)) ? DL_ST_OK : DL_ST_REFUSED; return 1;
	default: break;
	}
	return 1;
}
#endif

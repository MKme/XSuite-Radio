// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 MKME Media
#include <string.h>
#include "xsuite/xsuite_packet.h"
#include "xsuite/xsuite_templates.h"

static void be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int32_t e5(int32_t value)
{
	// JavaScript Math.round uses +infinity for a negative half; match canonical coordinate quantization.
	int32_t q = value / 100, r = value % 100;
	return q + (r >= 50 ? 1 : r < -50 ? -1 : 0);
}

const char *xsuite_action_label(uint8_t action)
{
	return action < XSUITE_ACTION_COUNT ? xsuite_templates[action].label : "";
}

size_t xsuite_packet_edit(uint8_t action, bool location, int32_t lat, int32_t lon, uint32_t minutes,
		     uint32_t source_tag, uint32_t sequence, bool sos, const char *note, uint16_t quantity,
		     uint8_t priority, char *out, size_t cap)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	static const char hex[] = "0123456789ABCDEF";
	uint8_t bytes[180];
	if (action >= XSUITE_ACTION_COUNT || cap == 0) return 0;
	const xsuite_template_t *t = &xsuite_templates[action];
	uint8_t slot = (location ? 1u : 0u) + (note != NULL ? 2u : 0u);
	if (!t->len[slot] || t->len[slot] > sizeof bytes) return 0;
	size_t n = t->len[slot];
	if (location && (lat < -900000000 || lat > 900000000 || lon < -1800000000 || lon > 1800000000)) return 0;
	memcpy(bytes, t->data[slot], n);
	be32(bytes + t->time_at[slot], minutes);
	if (t->lat_at[slot] != 255) be32(bytes + t->lat_at[slot], (uint32_t)e5(lat));
	if (t->lon_at[slot] != 255) be32(bytes + t->lon_at[slot], (uint32_t)e5(lon));
	if (action == XS_CHECKIN && sos) bytes[15] = 1;
	if (note != NULL) {
		if (priority > 3) return 0;
		if (t->qty_at[slot] != 255) { unsigned a = t->qty_at[slot]; bytes[a] = (uint8_t)(quantity >> 8); bytes[a + 1] = (uint8_t)quantity; }
		if (t->pri_at[slot] != 255) bytes[t->pri_at[slot]] = t->tpl == 13 ? (uint8_t)"ZOPR"[priority] : priority;
		if (t->text_at[slot] != 255) {
			while (*note == ' ') note++;
			size_t len = strlen(note), a = t->text_at[slot], old = 16;
			while (len && note[len - 1] == ' ') len--;
			if (!len) return 0;
			if (len > 64 || n - old + len > sizeof bytes) return 0;
			for (size_t i = 0; i < len; i++) if ((unsigned char)note[i] < 32 || (unsigned char)note[i] > 126) return 0;
			memmove(bytes + a + len, bytes + a + old, n - a - old); memcpy(bytes + a, note, len);
			bytes[a - 1] = (uint8_t)len; if (t->text_wide == 2) bytes[a - 2] = 0;
			n = n - old + len;
		}
	}
	size_t need = 28u + (t->tpl >= 10 ? 1u : 0u) + (n * 4u + 2u) / 3u;
	if (need >= cap) return 0;
	size_t k = 0;
	memcpy(out, "X1.", 3); k = 3;
	if (t->tpl >= 10) out[k++] = (char)('0' + t->tpl / 10);
	out[k++] = (char)('0' + t->tpl % 10);
	memcpy(out + k, ".C.", 3); k += 3;
	for (int i = 7; i >= 0; i--) out[k++] = hex[(source_tag >> (i * 4)) & 15u];
	for (int i = 7; i >= 0; i--) out[k++] = hex[(sequence >> (i * 4)) & 15u];
	memcpy(out + k, ".1/1.", 5); k += 5;
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = (uint32_t)bytes[i] << 16;
		if (i + 1 < n) v |= (uint32_t)bytes[i + 1] << 8;
		if (i + 2 < n) v |= bytes[i + 2];
		out[k++] = b64[(v >> 18) & 63]; out[k++] = b64[(v >> 12) & 63];
		if (i + 1 < n) out[k++] = b64[(v >> 6) & 63];
		if (i + 2 < n) out[k++] = b64[v & 63];
	}
	out[k] = '\0';
	return k;
}

size_t xsuite_packet(uint8_t action, bool location, int32_t lat, int32_t lon, uint32_t minutes,
		     uint32_t source_tag, uint32_t sequence, bool sos, char *out, size_t cap)
{
	return xsuite_packet_edit(action, location, lat, lon, minutes, source_tag, sequence, sos, NULL, 1, 3, out, cap);
}

uint32_t xsuite_unix_minutes(uint32_t date, uint32_t hms)
{
	unsigned day = date / 10000u, month = date / 100u % 100u, year = 2000u + date % 100u;
	unsigned hour = hms / 10000u, minute = hms / 100u % 100u;
	if (!day || day > 31 || !month || month > 12 || hour > 23 || minute > 59) return 0;
	static const uint16_t before[] = { 0,31,59,90,120,151,181,212,243,273,304,334 };
	uint32_t days = 10957u;
	for (unsigned y = 2000; y < year; y++) days += 365u + (y % 4 == 0);
	days += before[month - 1u] + day - 1u + (month > 2 && year % 4 == 0);
	return days * 1440u + hour * 60u + minute;
}

static int b64value(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	return c == '-' ? 62 : c == '_' ? 63 : -1;
}

bool xsuite_packet_part(const char *text, xsuite_packet_part_t *part)
{
	memset(part, 0, sizeof *part);
	if (text[0] == '>') text++;
	else if (text[0] == ':' && strlen(text) > 11 && text[10] == ':') text += 11;
	if (strncmp(text, "X1.", 3)) return false;
	const char *p = text + 3;
	while (*p >= '0' && *p <= '9') { part->tpl = part->tpl * 10u + (unsigned)(*p++ - '0'); if (part->tpl > 99) return false; }
	if (!part->tpl || strncmp(p, ".C.", 3)) return false;
	p += 3; unsigned n = 0;
	for (; *p && *p != '.'; p++) { if (n == sizeof part->id - 1 || !((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '-' || *p == '_')) return false; part->id[n++] = *p; }
	if (!n || *p++ != '.') return false;
	while (*p >= '0' && *p <= '9') { part->part = part->part * 10u + (unsigned)(*p++ - '0'); if (part->part > 128) return false; }
	if (*p++ != '/') return false;
	while (*p >= '0' && *p <= '9') { part->total = part->total * 10u + (unsigned)(*p++ - '0'); if (part->total > 128) return false; }
	if (!part->part || part->total < part->part || *p++ != '.') return false;
	part->payload = p;
	for (; *p && *p != '{'; p++) { if (b64value(*p) < 0) return false; part->payload_len++; }
	return part->payload_len != 0;
}

static size_t packet_read(const char *text, uint8_t *bytes, size_t cap, unsigned *tpl, bool prefix_only)
{
	xsuite_packet_part_t part;
	if (!xsuite_packet_part(text, &part) || part.part != 1 || part.total != 1) return 0;
	*tpl = part.tpl;
	const char *p = part.payload;
	uint32_t v = 0; unsigned bits = 0; size_t n = 0;
	for (; *p && *p != '{'; p++) {
		int x = b64value(*p); if (x < 0) return 0;
		v = v << 6 | (unsigned)x; bits += 6;
		if (bits >= 8) { bits -= 8; if (n == cap) return prefix_only ? n : 0; bytes[n++] = (uint8_t)(v >> bits); }
	}
	return n;
}

static size_t text_field(const uint8_t *data, size_t n, size_t offset, bool wide, char *out, size_t cap, bool preview)
{
	if (offset + (wide ? 2u : 1u) > n || !cap) return 0;
	size_t len = wide ? (size_t)data[offset] * 256u + data[offset + 1] : data[offset];
	offset += wide ? 2u : 1u;
	if (len > n - offset) { if (!preview) return 0; len = n - offset; }
	if (len >= cap) len = cap - 1;
	memcpy(out, data + offset, len); out[len] = '\0'; return len;
}

void xsuite_packet_summary(const char *text, char *out, size_t cap)
{
	// Decode only the beginning needed for a readable preview; large packets stay in the bounded RX assembly.
	uint8_t b[448]; unsigned tpl;
	if (!cap) return;
	out[0] = '\0';
	size_t n = packet_read(text, b, sizeof b, &tpl, true), offset = 0;
	if (n >= 12 && tpl == 13) {
		offset = 12;
		if (b[11] & 1u) { if (offset >= n || b[offset] > n - offset - 1u) return; offset += 1u + b[offset]; }
		(void)text_field(b, n, offset, true, out, cap, true); return;
	}
	if (n && b[0] == 1) {
		if (tpl == 1 && n >= 12) offset = (b[11] & 2u) ? 12u + ((b[11] & 1u) ? 8u : 0u) : 0;
		if (tpl == 2 && n >= 13) offset = (b[12] & 2u) ? 13u + ((b[12] & 1u) ? 8u : 0u) : 0;
		if (tpl == 3 && n >= 14) offset = (b[13] & 2u) ? 14u + ((b[13] & 1u) ? 8u : 0u) : 0;
		if (tpl == 5 && n >= 12) offset = (b[11] & 2u) ? 12u + ((b[11] & 1u) ? 8u : 0u) : 0;
		if (tpl == 9 && n >= 13 && b[12] & 2u) offset = 13u + ((b[12] & 16u) ? 4u : 0u) + ((b[12] & 32u) ? 4u : 0u) + ((b[12] & 1u) ? 8u : 0u);
		if (offset && text_field(b, n, offset, false, out, cap, false)) return;
		if (tpl == 4 && n >= 16) {
			const char *status = b[15] == 1 ? "HELP / position" : b[15] == 2 ? "Returning to base" : "Position / OK";
			size_t len = strlen(status); if (len >= cap) len = cap - 1; memcpy(out, status, len); out[len] = '\0'; return;
		}
	}
	const char *label = n ? "XTOC report received" : text;
	size_t len = strlen(label); if (len >= cap) len = cap - 1; memcpy(out, label, len); out[len] = '\0';
}

bool xsuite_packet_ack(const char *text, const char *expected)
{
	uint8_t b[192]; unsigned tpl;
	size_t n = packet_read(text, b, sizeof b, &tpl, false);
	if (!*expected || n < 18 || tpl != 13 || b[0] != 1 || !(b[11] & 1u) || b[12] != 3 || memcmp(b + 13, "ACK", 3)) return false;
	char message[33];
	return text_field(b, n, 16, true, message, sizeof message, false) != 0 && strcmp(message, expected) == 0;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 MKME Media
#ifndef XSUITE_H
#define XSUITE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "xsuite/xsuite_packet.h"

#define XSUITE_TEXT_MAX 256u
#define XSUITE_CONFIG_BYTES 22u
#define XSUITE_SOS 1u
#define XSUITE_GROUP 2u
enum { XSUITE_APRS, XSUITE_DMR, XSUITE_BOTH };
enum { XS_TX_IDLE, XS_TX_QUEUED, XS_TX_SENT, XS_TX_IMPORTED, XS_TX_FAILED, XS_TX_NO_TIME, XS_TX_NO_POSITION, XS_TX_SETUP };
typedef struct {
	uint8_t enabled, via, flags;
	uint16_t interval_s, distance_m;
	uint32_t dst_dmr;
	char dst_call[10];
} xsuite_config_t;
typedef struct {
	uint32_t serial, src_dmr, dst_dmr;
	uint8_t bearer, flags;
	char src_call[10], dst_call[10];
	uint16_t length;
	char text[XSUITE_TEXT_MAX + 1];
} xsuite_rx_t;
typedef struct {
	uint32_t rx_serial, rx_dropped, own_dmr, gps_age_s;
	uint8_t tx_state;
	bool gps_valid, epoch_ready;
	char call[10], last_id[17];
} xsuite_view_t;

void xsuite_init(uint32_t now_ms);
void xsuite_tick(uint32_t now_ms, uint32_t own_dmr, bool on_dmr, uint32_t channel_target, bool channel_private);
void xsuite_get_config(xsuite_config_t *out);
bool xsuite_set_config(const xsuite_config_t *cfg, uint32_t now_ms);
void xsuite_get_view(xsuite_view_t *out);
bool xsuite_action(uint8_t action);
bool xsuite_action_edit(uint8_t action, const char *note, uint16_t quantity, uint8_t priority);
// One atomic snapshot of the last complete native preview and its retained native source/transport label.
void xsuite_inbox_summary(char *out, size_t cap);
void xsuite_aprs_complete(const uint8_t *info, size_t length, bool sent);
void xsuite_receive(uint8_t bearer, uint32_t src_dmr, uint32_t dst_dmr, bool group,
		    const char *src_call, const char *dst_call, const uint8_t *text, size_t length);
bool xsuite_receive_next(uint32_t after, xsuite_rx_t *out);
// One native line staged in bounded chunks, reserved against automatic/menu writes until valid commit.
// Ten seconds without a successful chunk abandons a disconnected host's reservation.
bool xsuite_tx_begin(uint8_t bearer, uint8_t flags, uint32_t dst, const char *call, uint16_t length);
bool xsuite_tx_chunk(uint16_t offset, const uint8_t *data, size_t length);
bool xsuite_tx_commit(void);
// A cable-connected application supplies time once; fresh GPS UTC takes precedence while available.
void xsuite_set_time(uint32_t unix_seconds, uint32_t now_ms);
bool xsuite_set_epoch(uint32_t epoch);
size_t xsuite_debug_command(const uint8_t *req, uint16_t length, uint8_t *out, size_t cap);
#endif

/**
 * victronble — Zephyr BLE observer API.
 *
 * Passive-scans for Victron Instant Readout advertisements, decodes them
 * off the BT RX thread (frames are queued to a dedicated decode thread)
 * and delivers records to registered listeners.
 *
 * The application owns the Bluetooth stack: call bt_enable() before
 * victronble_start(). Enable with CONFIG_VICTRONBLE=y (needs
 * CONFIG_BT_OBSERVER=y).
 *
 * Copyright (c) 2026 Scott Penrose
 * License: MIT
 */

#ifndef VICTRONBLE_ZEPHYR_H
#define VICTRONBLE_ZEPHYR_H

#include <zephyr/bluetooth/addr.h>
#include <zephyr/sys/slist.h>

#include "victronble.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Listener; register with victronble_cb_register(). Callbacks run on the
 *  module's decode thread. */
struct victronble_cb {
	/** A monitored device published a new record. */
	void (*record)(const bt_addr_le_t *addr, int8_t rssi,
		       const victronble_record_t *rec);
	/** Optional: a monitored device's advertisement failed to decode. */
	void (*decode_error)(const bt_addr_le_t *addr, victronble_err_t err);
	sys_snode_t node;
};

struct victronble_stats {
	uint32_t adverts;      /* Victron product adverts seen (any device) */
	uint32_t queued;       /* frames queued for a monitored device */
	uint32_t dropped;      /* frames lost to a full queue */
	uint32_t decoded;      /* records decoded OK */
	uint32_t duplicates;   /* suppressed by nonce dedup */
	uint32_t errors;       /* decode failures */
};

/** Register a listener. Returns -EALREADY if already registered. */
int victronble_cb_register(struct victronble_cb *cb);

/** Monitor a device. @p key is its 16-byte advertisement key (VictronConnect
 *  → Product Info). Returns -ENOMEM when full, -EALREADY if present. */
int victronble_device_add(const bt_addr_le_t *addr,
			  const uint8_t key[VICTRONBLE_KEY_LEN]);

/** Stop monitoring a device. Returns -ENOENT if unknown. */
int victronble_device_remove(const bt_addr_le_t *addr);

/** Start the passive scan (bt_enable() must have succeeded first). */
int victronble_start(void);

/** Stop the scan. Queued frames still drain to callbacks. */
int victronble_stop(void);

void victronble_get_stats(struct victronble_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* VICTRONBLE_ZEPHYR_H */

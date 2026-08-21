/**
 * victronble — Zephyr BLE observer backend.
 *
 * Scan callback (BT RX context) does the cheap work only: AD walk, product
 * pre-filter, registry match, copy into a message queue. A dedicated thread
 * decrypts, decodes, dedups and fans out to registered listeners.
 *
 * Copyright (c) 2026 Scott Penrose
 * License: MIT
 */

/* Arduino/PlatformIO builds compile every file under src/ — this backend
 * only exists under Zephyr (the Zephyr CMake build lists sources
 * explicitly, so the reverse problem doesn't arise). */
#ifdef __ZEPHYR__

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/logging/log.h>

#include "victronble_zephyr.h"

LOG_MODULE_REGISTER(victronble, CONFIG_VICTRONBLE_LOG_LEVEL);

#define MAX_MFG_LEN (VICTRONBLE_MIN_MFG_LEN + VICTRONBLE_MAX_CIPHER_LEN)

struct vb_frame {
	bt_addr_le_t addr;
	int8_t rssi;
	uint8_t len;
	uint8_t data[MAX_MFG_LEN];
};

struct vb_device {
	bt_addr_le_t addr;
	uint8_t key[VICTRONBLE_KEY_LEN];
	uint16_t last_nonce;
	bool have_nonce;
	bool used;
};

K_MSGQ_DEFINE(vb_msgq, sizeof(struct vb_frame),
	      CONFIG_VICTRONBLE_QUEUE_DEPTH, 4);

static struct vb_device devices[CONFIG_VICTRONBLE_MAX_DEVICES];
static struct k_mutex dev_mtx;
static sys_slist_t callbacks = SYS_SLIST_STATIC_INIT(&callbacks);
static struct victronble_stats stats;
static bool scanning;

static struct vb_device *find_device(const bt_addr_le_t *addr)
{
	for (int i = 0; i < CONFIG_VICTRONBLE_MAX_DEVICES; i++) {
		if (devices[i].used &&
		    bt_addr_le_cmp(&devices[i].addr, addr) == 0) {
			return &devices[i];
		}
	}
	return NULL;
}

/* --- Scan path (BT RX context) --------------------------------------- */

struct ad_ctx {
	const bt_addr_le_t *addr;
	int8_t rssi;
};

static bool ad_cb(struct bt_data *data, void *user_data)
{
	struct ad_ctx *ctx = user_data;

	if (data->type != BT_DATA_MANUFACTURER_DATA) {
		return true; /* keep walking the AD structures */
	}
	if (!victronble_is_product_adv(data->data, data->data_len)) {
		return true;
	}
	stats.adverts++;

	/* Registry check is a handful of compares — cheap enough here, and
	 * it keeps other people's Victrons out of the queue. */
	if (find_device(ctx->addr) == NULL) {
		return false;
	}

	struct vb_frame frame;

	bt_addr_le_copy(&frame.addr, ctx->addr);
	frame.rssi = ctx->rssi;
	frame.len = MIN(data->data_len, sizeof(frame.data));
	memcpy(frame.data, data->data, frame.len);

	if (k_msgq_put(&vb_msgq, &frame, K_NO_WAIT) == 0) {
		stats.queued++;
	} else {
		stats.dropped++;
	}
	return false; /* found the record — stop walking */
}

static void scan_recv(const bt_addr_le_t *addr, int8_t rssi,
		      uint8_t adv_type, struct net_buf_simple *ad)
{
	ARG_UNUSED(adv_type);

	struct ad_ctx ctx = { .addr = addr, .rssi = rssi };

	bt_data_parse(ad, ad_cb, &ctx);
}

/* --- Decode thread ----------------------------------------------------- */

static void decode_frame(const struct vb_frame *frame)
{
	uint8_t key[VICTRONBLE_KEY_LEN];
	uint16_t last_nonce;
	bool have_nonce;

	k_mutex_lock(&dev_mtx, K_FOREVER);
	struct vb_device *dev = find_device(&frame->addr);

	if (dev == NULL) { /* removed while queued */
		k_mutex_unlock(&dev_mtx);
		return;
	}
	memcpy(key, dev->key, sizeof(key));
	last_nonce = dev->last_nonce;
	have_nonce = dev->have_nonce;
	k_mutex_unlock(&dev_mtx);

	victronble_record_t rec;
	victronble_err_t err = victronble_decode(frame->data, frame->len,
						 key, &rec);
	struct victronble_cb *cb;

	if (err != VICTRONBLE_OK) {
		stats.errors++;
		LOG_DBG("decode failed: %s", victronble_strerror(err));
		SYS_SLIST_FOR_EACH_CONTAINER(&callbacks, cb, node) {
			if (cb->decode_error != NULL) {
				cb->decode_error(&frame->addr, err);
			}
		}
		return;
	}

	if (IS_ENABLED(CONFIG_VICTRONBLE_DEDUP) &&
	    have_nonce && rec.nonce == last_nonce) {
		stats.duplicates++;
		return;
	}

	k_mutex_lock(&dev_mtx, K_FOREVER);
	dev = find_device(&frame->addr);
	if (dev != NULL) {
		dev->last_nonce = rec.nonce;
		dev->have_nonce = true;
	}
	k_mutex_unlock(&dev_mtx);

	stats.decoded++;
	LOG_DBG("%s record, nonce 0x%04x, rssi %d",
		victronble_device_type_str(rec.type), rec.nonce, frame->rssi);

	SYS_SLIST_FOR_EACH_CONTAINER(&callbacks, cb, node) {
		if (cb->record != NULL) {
			cb->record(&frame->addr, frame->rssi, &rec);
		}
	}
}

static void vb_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	struct vb_frame frame;

	while (true) {
		k_msgq_get(&vb_msgq, &frame, K_FOREVER);
		decode_frame(&frame);
	}
}

K_THREAD_DEFINE(vb_thread, CONFIG_VICTRONBLE_THREAD_STACK_SIZE,
		vb_thread_fn, NULL, NULL, NULL,
		CONFIG_VICTRONBLE_THREAD_PRIORITY, 0, 0);

/* --- Public API -------------------------------------------------------- */

int victronble_cb_register(struct victronble_cb *cb)
{
	struct victronble_cb *it;

	SYS_SLIST_FOR_EACH_CONTAINER(&callbacks, it, node) {
		if (it == cb) {
			return -EALREADY;
		}
	}
	sys_slist_append(&callbacks, &cb->node);
	return 0;
}

int victronble_device_add(const bt_addr_le_t *addr,
			  const uint8_t key[VICTRONBLE_KEY_LEN])
{
	int ret = -ENOMEM;

	k_mutex_lock(&dev_mtx, K_FOREVER);
	if (find_device(addr) != NULL) {
		ret = -EALREADY;
	} else {
		for (int i = 0; i < CONFIG_VICTRONBLE_MAX_DEVICES; i++) {
			if (!devices[i].used) {
				bt_addr_le_copy(&devices[i].addr, addr);
				memcpy(devices[i].key, key,
				       VICTRONBLE_KEY_LEN);
				devices[i].have_nonce = false;
				devices[i].used = true;
				ret = 0;
				break;
			}
		}
	}
	k_mutex_unlock(&dev_mtx);
	return ret;
}

int victronble_device_remove(const bt_addr_le_t *addr)
{
	int ret = -ENOENT;

	k_mutex_lock(&dev_mtx, K_FOREVER);
	struct vb_device *dev = find_device(addr);

	if (dev != NULL) {
		memset(dev, 0, sizeof(*dev));
		ret = 0;
	}
	k_mutex_unlock(&dev_mtx);
	return ret;
}

int victronble_start(void)
{
	/* Passive scan at a low duty cycle: Victron devices advertise about
	 * once per second, so slow-scan parameters catch every record for a
	 * fraction of the radio-on time. */
	static const struct bt_le_scan_param param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = CONFIG_VICTRONBLE_SCAN_INTERVAL,
		.window = CONFIG_VICTRONBLE_SCAN_WINDOW,
	};
	int err;

	if (scanning) {
		return -EALREADY;
	}
	err = bt_le_scan_start(&param, scan_recv);
	if (err != 0) {
		LOG_ERR("scan start failed (%d)", err);
		return err;
	}
	scanning = true;
	LOG_INF("observing (interval %u window %u)",
		CONFIG_VICTRONBLE_SCAN_INTERVAL, CONFIG_VICTRONBLE_SCAN_WINDOW);
	return 0;
}

int victronble_stop(void)
{
	int err;

	if (!scanning) {
		return -EALREADY;
	}
	err = bt_le_scan_stop();
	if (err == 0) {
		scanning = false;
	}
	return err;
}

void victronble_get_stats(struct victronble_stats *out)
{
	*out = stats;
}

static int vb_init(void)
{
	k_mutex_init(&dev_mtx);
	return 0;
}

SYS_INIT(vb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* __ZEPHYR__ */

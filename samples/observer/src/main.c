/**
 * VictronBLE Zephyr observer sample.
 *
 * Monitors a fixed list of Victron devices, decodes their Instant Readout
 * advertisements and logs every record. Prints a statistics line every 30 s
 * so a silent console can be diagnosed without a sniffer.
 *
 * Don't know your devices' MAC addresses yet? Build samples/scan first — it
 * lists every Victron device in range.
 *
 * Copyright (c) 2026 Scott Penrose
 * License: MIT
 */

#include <math.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "victronble_zephyr.h"

LOG_MODULE_REGISTER(observer, LOG_LEVEL_INF);

#define STATS_INTERVAL K_SECONDS(30)

/*
 * Your devices. The advertisement key is in VictronConnect:
 *   device -> gear icon -> Product info -> "Instant readout via Bluetooth"
 *   -> SHOW / copy the encryption key (32 hex characters).
 *
 * Victron devices use a random static Bluetooth address, so the address type
 * is "random" — not "public". Getting this wrong is the usual reason a device
 * never matches: the adverts arrive (the stats line counts them) but no
 * record is ever decoded, because the registry lookup compares the type too.
 */
static const struct {
	const char *name;
	const char *addr;
	const char *addr_type;
	const char *key;
} known_devices[] = {
	{
		.name = "Rainbow48V",
		.addr = "E4:05:42:34:14:F3",
		.addr_type = "random",
		.key = "0ec3adf7433dd61793ff2f3b8ad32ed8",
	},
	{
		.name = "ScottTrailer",
		.addr = "E6:45:59:78:3C:FB",
		.addr_type = "random",
		.key = "3fa658aded4f309b9bc17a2318cb1f56",
	},
};

/* --- record formatting ------------------------------------------------- */

#define FBUF_LEN 20

/*
 * A float field the device did not send comes back as NAN (see
 * include/victronble_core.h). Render that as "n/a" rather than letting "nan" leak
 * into the log — a missing load-current reading is not a fault. The unit goes
 * in here too, so an absent field reads "n/a" and not "n/a A".
 *
 * Needs CONFIG_CBPRINTF_FP_SUPPORT=y, or every number comes out empty.
 */
static const char *flt(char *buf, float v, int dp, const char *unit)
{
	if (isnan(v)) {
		strcpy(buf, "n/a");
	} else {
		snprintk(buf, FBUF_LEN, "%.*f %s", dp, (double)v, unit);
	}
	return buf;
}

static void print_solar(const victronble_solar_charger_t *s)
{
	char a[FBUF_LEN], b[FBUF_LEN], c[FBUF_LEN], d[FBUF_LEN];

	LOG_INF("  state %s  error %u", victronble_state_str(s->state),
		s->error);
	LOG_INF("  battery %s %s   pv %s   yield today %u Wh",
		flt(a, s->battery_voltage, 2, "V"),
		flt(b, s->battery_current, 1, "A"),
		flt(c, s->pv_power, 0, "W"), s->yield_today_wh);
	LOG_INF("  load %s", flt(d, s->load_current, 1, "A"));
}

static void print_batmon(const victronble_battery_monitor_t *m)
{
	char a[FBUF_LEN], b[FBUF_LEN], c[FBUF_LEN], d[FBUF_LEN];

	LOG_INF("  battery %s %s   soc %s", flt(a, m->voltage, 2, "V"),
		flt(b, m->current, 2, "A"), flt(c, m->soc, 1, "%"));
	LOG_INF("  consumed %s   alarm 0x%04x",
		flt(d, m->consumed_ah, 1, "Ah"), m->alarm);

	/* 0xFFFF is the wire's "not available", not 45 days of runtime. */
	if (m->remaining_minutes == 0xFFFF) {
		LOG_INF("  time to go n/a");
	} else {
		LOG_INF("  time to go %u min", m->remaining_minutes);
	}

	/* The aux channel is one of three things, chosen on the device. */
	switch (m->aux_mode) {
	case 0:
		LOG_INF("  aux voltage %s", flt(a, m->aux_voltage, 2, "V"));
		break;
	case 2:
		LOG_INF("  temperature %s", flt(a, m->temperature, 1, "degC"));
		break;
	default:
		break;
	}
}

static void print_record(const bt_addr_le_t *addr, int8_t rssi,
			 const victronble_record_t *rec)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	char a[FBUF_LEN], b[FBUF_LEN], c[FBUF_LEN];

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	LOG_INF("%s  %s  rssi %d  model 0x%04x  nonce %u", addr_str,
		victronble_device_type_str(rec->type), rssi, rec->model_id,
		rec->nonce);

	switch (rec->type) {
	case VICTRONBLE_DEV_SOLAR_CHARGER:
		print_solar(&rec->u.solar);
		break;
	case VICTRONBLE_DEV_BATTERY_MONITOR:
		print_batmon(&rec->u.batmon);
		break;
	case VICTRONBLE_DEV_INVERTER:
		LOG_INF("  state %s  battery %s %s  ac %s",
			victronble_state_str(rec->u.inverter.state),
			flt(a, rec->u.inverter.battery_voltage, 2, "V"),
			flt(b, rec->u.inverter.battery_current, 2, "A"),
			flt(c, rec->u.inverter.ac_power, 0, "W"));
		break;
	case VICTRONBLE_DEV_DCDC_CONVERTER:
		LOG_INF("  state %s  in %s  out %s %s",
			victronble_state_str(rec->u.dcdc.state),
			flt(a, rec->u.dcdc.input_voltage, 2, "V"),
			flt(b, rec->u.dcdc.output_voltage, 2, "V"),
			flt(c, rec->u.dcdc.output_current, 1, "A"));
		break;
	case VICTRONBLE_DEV_AC_CHARGER:
		LOG_INF("  state %s  out1 %s %s  temp %s",
			victronble_state_str(rec->u.ac.state),
			flt(a, rec->u.ac.voltage1, 2, "V"),
			flt(b, rec->u.ac.current1, 1, "A"),
			flt(c, rec->u.ac.temperature, 1, "degC"));
		break;
	default:
		LOG_INF("  (no decoder for record type 0x%02x)",
			rec->record_type);
		break;
	}
}

/* --- callbacks --------------------------------------------------------- */

/*
 * These run on the victronble decode thread, not the Bluetooth RX thread, so
 * logging here is fine — it cannot stall the controller.
 */
static void on_record(const bt_addr_le_t *addr, int8_t rssi,
		      const victronble_record_t *rec)
{
	print_record(addr, rssi, rec);
}

static void on_decode_error(const bt_addr_le_t *addr, victronble_err_t err)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_WRN("%s decode failed: %s", addr_str, victronble_strerror(err));
}

static struct victronble_cb callbacks = {
	.record = on_record,
	.decode_error = on_decode_error,
};

/* --- setup ------------------------------------------------------------- */

static int register_devices(void)
{
	int registered = 0;

	for (size_t i = 0; i < ARRAY_SIZE(known_devices); i++) {
		bt_addr_le_t addr;
		uint8_t key[VICTRONBLE_KEY_LEN];
		int err;

		err = bt_addr_le_from_str(known_devices[i].addr,
					  known_devices[i].addr_type, &addr);
		if (err != 0) {
			LOG_ERR("%s: bad address '%s' (%d)",
				known_devices[i].name, known_devices[i].addr,
				err);
			continue;
		}

		if (!victronble_parse_key(known_devices[i].key, key)) {
			LOG_ERR("%s: key must be 32 hex characters",
				known_devices[i].name);
			continue;
		}

		err = victronble_device_add(&addr, key);
		if (err != 0) {
			LOG_ERR("%s: victronble_device_add failed (%d)",
				known_devices[i].name, err);
			continue;
		}

		LOG_INF("monitoring %s (%s)", known_devices[i].name,
			known_devices[i].addr);
		registered++;
	}

	return registered;
}

int main(void)
{
	struct victronble_stats stats;
	int err;

	LOG_INF("VictronBLE observer starting");

	/* The application owns the Bluetooth stack: victronble_start() needs
	 * it up already. */
	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("bt_enable failed (%d)", err);
		return 0;
	}

	err = victronble_cb_register(&callbacks);
	if (err != 0) {
		LOG_ERR("victronble_cb_register failed (%d)", err);
		return 0;
	}

	if (register_devices() == 0) {
		LOG_ERR("no devices registered — nothing to observe");
		return 0;
	}

	err = victronble_start();
	if (err != 0) {
		LOG_ERR("victronble_start failed (%d)", err);
		return 0;
	}

	while (1) {
		k_sleep(STATS_INTERVAL);

		victronble_get_stats(&stats);
		LOG_INF("stats: adverts %u queued %u dropped %u decoded %u dup %u err %u",
			stats.adverts, stats.queued, stats.dropped,
			stats.decoded, stats.duplicates, stats.errors);
	}

	return 0;
}

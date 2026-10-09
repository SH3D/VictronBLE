/**
 * VictronBLE native (host) example.
 *
 * Decodes a Victron "Instant Readout" advertisement on your PC — no board, no
 * BLE stack, no toolchain beyond a C compiler. The library's core is plain
 * C99, so the same code that runs on an ESP32, an nRF52 or under Zephyr also
 * runs here.
 *
 * Useful for checking a key, understanding the record layout, or debugging a
 * capture from a BLE sniffer before you flash anything.
 *
 *   make && ./nativedecode                     # built-in sample advert
 *   ./nativedecode <advert-hex> <key-hex>      # your own capture
 *
 * The advert hex is the manufacturer-specific data starting at the company ID
 * (e1 02 ...), exactly as a sniffer or nRF Connect reports it. Separators are
 * ignored, so "e1:02:10" and "e10210" both work.
 *
 * Copyright (c) 2026 Scott Penrose
 * License: MIT
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "victronble_core.h"

/*
 * Sample advertisement from the library's own test vectors
 * (tests/vectors/test_vectors.h): a SmartSolar MPPT in bulk charge.
 */
static const char DEFAULT_ADVERT[] =
	"e1021060a000013412 0d53b0254c65d34a923470 3a6c19737e65f6a442d056";
static const char DEFAULT_KEY[] = "0df4d0395b7d5d4f5a0d0af52e1b4c1e";

/* --- input parsing ----------------------------------------------------- */

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

/*
 * Parse a hex string into bytes, skipping any separator (space, colon, dash).
 * Returns the byte count, or -1 on a stray character or an odd digit count.
 * (Keys are 32 hex characters exactly, so those use the library's own
 * victronble_parse_key() instead of this.)
 */
static int parse_hex(const char *s, uint8_t *out, size_t max)
{
	size_t n = 0;
	int hi = -1;

	for (; *s != '\0'; s++) {
		if (*s == ' ' || *s == ':' || *s == '-' || *s == '\t') {
			continue;
		}

		int v = hex_nibble(*s);

		if (v < 0) {
			fprintf(stderr, "bad hex character '%c'\n", *s);
			return -1;
		}
		if (hi < 0) {
			hi = v;
			continue;
		}
		if (n >= max) {
			fprintf(stderr, "advert too long (max %zu bytes)\n",
				max);
			return -1;
		}
		out[n++] = (uint8_t)((hi << 4) | v);
		hi = -1;
	}

	if (hi >= 0) {
		fprintf(stderr, "hex string has an odd number of digits\n");
		return -1;
	}
	return (int)n;
}

/* --- record printing --------------------------------------------------- */

/* Fields the device did not send come back as NAN — report them as "n/a"
 * rather than printing "nan", which reads like a fault. */
static void pf(const char *label, float v, const char *unit, int dp)
{
	if (isnan(v)) {
		printf("  %-16s n/a\n", label);
	} else {
		printf("  %-16s %.*f %s\n", label, dp, (double)v, unit);
	}
}

static void print_solar(const victronble_solar_charger_t *s)
{
	printf("  %-16s %s (error %u)\n", "state",
	       victronble_state_str(s->state), s->error);
	pf("battery", s->battery_voltage, "V", 2);
	pf("current", s->battery_current, "A", 1);
	pf("pv power", s->pv_power, "W", 0);
	printf("  %-16s %u Wh\n", "yield today", s->yield_today_wh);
	pf("load current", s->load_current, "A", 1);
}

static void print_batmon(const victronble_battery_monitor_t *m)
{
	static const char *const aux_mode[] = { "aux voltage", "midpoint",
						"temperature", "none" };

	pf("battery", m->voltage, "V", 2);
	pf("current", m->current, "A", 2);
	pf("soc", m->soc, "%", 1);
	pf("consumed", m->consumed_ah, "Ah", 1);

	/* 0xFFFF is the wire's "not available", not 45 days of runtime. */
	if (m->remaining_minutes == 0xFFFF) {
		printf("  %-16s n/a\n", "time to go");
	} else {
		printf("  %-16s %u min\n", "time to go", m->remaining_minutes);
	}

	printf("  %-16s %s\n", "aux mode",
	       m->aux_mode < 4 ? aux_mode[m->aux_mode] : "?");
	pf("aux voltage", m->aux_voltage, "V", 2);
	pf("temperature", m->temperature, "degC", 1);
	printf("  %-16s 0x%04x\n", "alarm", m->alarm);
}

static void print_record(const victronble_record_t *rec)
{
	printf("device type      %s (0x%02x)\n",
	       victronble_device_type_str(rec->type), rec->record_type);
	printf("model id         0x%04x\n", rec->model_id);
	printf("nonce            %u (0x%04x)\n", rec->nonce, rec->nonce);
	printf("fields:\n");

	switch (rec->type) {
	case VICTRONBLE_DEV_SOLAR_CHARGER:
		print_solar(&rec->u.solar);
		break;
	case VICTRONBLE_DEV_BATTERY_MONITOR:
		print_batmon(&rec->u.batmon);
		break;
	case VICTRONBLE_DEV_INVERTER:
		printf("  %-16s %s\n", "state",
		       victronble_state_str(rec->u.inverter.state));
		pf("battery", rec->u.inverter.battery_voltage, "V", 2);
		pf("current", rec->u.inverter.battery_current, "A", 2);
		pf("ac power", rec->u.inverter.ac_power, "W", 0);
		printf("  %-16s 0x%02x\n", "alarms", rec->u.inverter.alarms);
		break;
	case VICTRONBLE_DEV_DCDC_CONVERTER:
		printf("  %-16s %s (error %u)\n", "state",
		       victronble_state_str(rec->u.dcdc.state),
		       rec->u.dcdc.error);
		pf("input", rec->u.dcdc.input_voltage, "V", 2);
		pf("output", rec->u.dcdc.output_voltage, "V", 2);
		pf("output current", rec->u.dcdc.output_current, "A", 1);
		break;
	case VICTRONBLE_DEV_AC_CHARGER:
		printf("  %-16s %s (error %u)\n", "state",
		       victronble_state_str(rec->u.ac.state), rec->u.ac.error);
		pf("output 1", rec->u.ac.voltage1, "V", 2);
		pf("current 1", rec->u.ac.current1, "A", 1);
		pf("output 2", rec->u.ac.voltage2, "V", 2);
		pf("current 2", rec->u.ac.current2, "A", 1);
		pf("output 3", rec->u.ac.voltage3, "V", 2);
		pf("current 3", rec->u.ac.current3, "A", 1);
		pf("ac current", rec->u.ac.ac_current, "A", 1);
		pf("temperature", rec->u.ac.temperature, "degC", 1);
		break;
	default:
		printf("  (decoded, but this build has no field printer for "
		       "that device type)\n");
		break;
	}
}

/* --- main -------------------------------------------------------------- */

int main(int argc, char **argv)
{
	const char *advert_hex = argc > 1 ? argv[1] : DEFAULT_ADVERT;
	const char *key_hex = argc > 2 ? argv[2] : DEFAULT_KEY;
	uint8_t advert[VICTRONBLE_MIN_MFG_LEN + VICTRONBLE_MAX_CIPHER_LEN];
	uint8_t key[VICTRONBLE_KEY_LEN];
	victronble_record_t rec;
	victronble_err_t err;
	int len;

	if (argc > 3 || (argc == 2 && strcmp(argv[1], "-h") == 0)) {
		fprintf(stderr, "usage: %s [advert-hex [key-hex]]\n", argv[0]);
		return 2;
	}
	if (argc == 1) {
		printf("(no arguments — decoding the built-in sample advert)\n\n");
	}

	len = parse_hex(advert_hex, advert, sizeof(advert));
	if (len < 0) {
		return 1;
	}

	if (!victronble_parse_key(key_hex, key)) {
		fprintf(stderr, "key must be exactly 32 hex characters\n");
		return 1;
	}

	printf("advert           %d bytes\n", len);

	/* Cheap pre-filter: company ID and record type only, no crypto. On a
	 * real scanner this is what keeps every other BLE beacon out of the
	 * decode path. */
	if (!victronble_is_product_adv(advert, (size_t)len)) {
		printf("not a Victron product advertisement\n");
		return 1;
	}

	/* Key-check byte. Lets a scanner pick the right key out of several
	 * without doing the AES work — and tells you a wrong key apart from a
	 * corrupt payload. */
	if (!victronble_key_matches(advert, (size_t)len, key)) {
		printf("key check failed — this key is not for this device\n");
		return 1;
	}

	err = victronble_decode(advert, (size_t)len, key, &rec);
	if (err != VICTRONBLE_OK) {
		printf("decode failed: %s (%d)\n", victronble_strerror(err),
		       err);
		return 1;
	}

	print_record(&rec);
	return 0;
}

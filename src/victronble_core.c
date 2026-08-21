/**
 * victronble core — decode + decrypt for Victron Instant Readout adverts.
 * Pure C99: no Arduino, no BLE stack, no allocation, no I/O, reentrant.
 *
 * Byte offsets and bit layouts match the proven ESP32/nRF52 implementation
 * in src/VictronBLE.cpp and Victron's "Extra Manufacturer Data" document.
 *
 * Copyright (c) 2025-2026 Scott Penrose
 * License: MIT
 */

#include "victronble.h"

#include <string.h>
#include <math.h>

/* Manufacturer-data layout (offsets from the company ID):
 *   0-1  company ID (LE, 0x02E1)
 *   2    record type, 0x10 = product advertisement
 *   3-4  model ID (LE)
 *   5    read-out type
 *   6    device record type (victronble_device_type_t)
 *   7-8  nonce / data counter (LE)
 *   9    key check byte (== key[0])
 *   10-  AES-128-CTR ciphertext, up to 21 bytes
 */
#define OFF_RECORD      2
#define OFF_MODEL       3
#define OFF_READOUT     5
#define OFF_DEVTYPE     6
#define OFF_NONCE       7
#define OFF_KEYCHECK    9
#define OFF_CIPHER      10
#define PRODUCT_ADV     0x10

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* --- AES backend selection ------------------------------------------- */

static victronble_aes_ctr_fn aes_fn;
static void *aes_user;

void victronble_set_aes_ctr(victronble_aes_ctr_fn fn, void *user)
{
    aes_fn = fn;
    aes_user = user;
}

static int aes_ctr(const uint8_t key[16], const uint8_t iv[16],
                   const uint8_t *in, uint8_t *out, size_t len)
{
    if (aes_fn != NULL) {
        return aes_fn(key, iv, in, out, len, aes_user);
    }
    return victronble_aes_ctr_default(key, iv, in, out, len, NULL);
}

/* --- Pre-filters ------------------------------------------------------ */

bool victronble_is_product_adv(const uint8_t *mfg, size_t len)
{
    return mfg != NULL && len >= VICTRONBLE_MIN_MFG_LEN &&
           get_le16(mfg) == VICTRONBLE_COMPANY_ID &&
           mfg[OFF_RECORD] == PRODUCT_ADV;
}

bool victronble_key_matches(const uint8_t *mfg, size_t len,
                            const uint8_t key[VICTRONBLE_KEY_LEN])
{
    return victronble_is_product_adv(mfg, len) && mfg[OFF_KEYCHECK] == key[0];
}

/* --- Per-type payload decoders ---------------------------------------
 * All operate on the decrypted payload, zero-padded to
 * VICTRONBLE_MAX_CIPHER_LEN bytes, so length checks always pass at the
 * decode() call site; they remain for direct-call safety. */

static bool parse_solar_charger(const uint8_t *d, size_t len,
                                victronble_solar_charger_t *r)
{
    if (len < 12) {
        return false;
    }
    r->state = d[0];
    r->error = d[1];
    r->battery_voltage = (int16_t)get_le16(d + 2) * 0.01f;   /* 0.01 V */
    r->battery_current = (int16_t)get_le16(d + 4) * 0.1f;    /* 0.1 A */
    r->yield_today_wh = (uint32_t)get_le16(d + 6) * 10u;     /* 0.01 kWh */
    r->pv_power = get_le16(d + 8);                           /* 1 W */
    /* Load current is a 9-bit field (0.1 A units); 0x1FF = no load output */
    uint16_t load_raw = get_le16(d + 10) & 0x1FF;
    r->load_current = (load_raw != 0x1FF) ? load_raw * 0.1f : NAN;
    return true;
}

static bool parse_battery_monitor(const uint8_t *d, size_t len,
                                  victronble_battery_monitor_t *r)
{
    /* Bit-packed, not byte-aligned; decoded by bit offset. SOC ends at
     * bit 117 (byte 14). */
    if (len < 15) {
        return false;
    }

    r->remaining_minutes = get_le16(d);                      /* bits 0-15 */
    r->voltage = (int16_t)get_le16(d + 2) * 0.01f;           /* bits 16-31 */
    r->alarm = get_le16(d + 4);                              /* bits 32-47 */

    /* Aux value (bits 48-63) interpreted per aux mode (bits 64-65) */
    uint16_t aux_raw = get_le16(d + 6);
    r->aux_mode = d[8] & 0x03;
    r->aux_voltage = (r->aux_mode == 0) ? aux_raw * 0.01f : NAN;
    r->temperature = (r->aux_mode == 2) ? aux_raw * 0.01f - 273.15f : NAN;

    /* Battery current (bits 66-87), 22-bit signed, 0.001 A units */
    int32_t current = (int32_t)(((uint32_t)(d[8] >> 2) & 0x3F) |
                                ((uint32_t)d[9] << 6) |
                                ((uint32_t)d[10] << 14));
    if (current & 0x200000) {
        current |= (int32_t)0xFFC00000;                      /* sign extend */
    }
    r->current = current * 0.001f;

    /* Consumed Ah (bits 88-107), 20-bit positive count, 0.1 Ah units,
     * reported negative (amp-hours consumed). */
    uint32_t consumed = (uint32_t)d[11] | ((uint32_t)d[12] << 8) |
                        ((uint32_t)(d[13] & 0x0F) << 16);
    r->consumed_ah = -((float)consumed * 0.1f);

    /* SOC (bits 108-117), 10-bit, 0.1 % units */
    uint16_t soc = (uint16_t)(((d[13] >> 4) | ((uint16_t)d[14] << 4)) & 0x3FF);
    r->soc = soc * 0.1f;
    return true;
}

static bool parse_inverter(const uint8_t *d, size_t len,
                           victronble_inverter_t *r)
{
    if (len < 9) {
        return false;
    }
    r->state = d[0];
    /* d[1] is the error code on the wire; kept out of the struct for parity
     * with the proven implementation, which only surfaced the alarm bits. */
    r->battery_voltage = get_le16(d + 2) * 0.01f;            /* 10 mV */
    r->battery_current = (int16_t)get_le16(d + 4) * 0.01f;   /* 10 mA */

    int32_t ac_power = (int32_t)((uint32_t)d[6] | ((uint32_t)d[7] << 8) |
                                 ((uint32_t)d[8] << 16));
    if (ac_power & 0x800000) {
        ac_power |= (int32_t)0xFF000000;                     /* sign extend */
    }
    r->ac_power = (float)ac_power;
    r->alarms = (len > 9) ? d[9] : 0;
    return true;
}

static bool parse_dcdc(const uint8_t *d, size_t len, victronble_dcdc_t *r)
{
    if (len < 8) {
        return false;
    }
    r->state = d[0];
    r->error = d[1];
    r->input_voltage = get_le16(d + 2) * 0.01f;              /* 10 mV */
    r->output_voltage = get_le16(d + 4) * 0.01f;             /* 10 mV */
    r->output_current = get_le16(d + 6) * 0.01f;             /* 10 mA */
    return true;
}

static uint32_t read_bits(const uint8_t *d, size_t *bit, uint8_t width)
{
    uint32_t value = 0;

    for (uint8_t i = 0; i < width; i++) {
        size_t b = *bit + i;

        value |= (uint32_t)((d[b >> 3] >> (b & 7)) & 0x01) << i;
    }
    *bit += width;
    return value;
}

static bool parse_ac_charger(const uint8_t *d, size_t len,
                             victronble_ac_charger_t *r)
{
    /* Bit-packed: 10 fields, 104 bits ending in byte 12, LSB-first. */
    if (len < 13) {
        return false;
    }

    size_t bit = 0;
    r->state = (uint8_t)read_bits(d, &bit, 8);
    r->error = (uint8_t)read_bits(d, &bit, 8);

    uint32_t v1 = read_bits(d, &bit, 13), i1 = read_bits(d, &bit, 11);
    uint32_t v2 = read_bits(d, &bit, 13), i2 = read_bits(d, &bit, 11);
    uint32_t v3 = read_bits(d, &bit, 13), i3 = read_bits(d, &bit, 11);
    uint32_t temp = read_bits(d, &bit, 7);
    uint32_t ac_cur = read_bits(d, &bit, 9);

    r->voltage1 = (v1 != 0x1FFF) ? v1 * 0.01f : NAN;
    r->current1 = (i1 != 0x7FF) ? i1 * 0.1f : NAN;
    r->voltage2 = (v2 != 0x1FFF) ? v2 * 0.01f : NAN;
    r->current2 = (i2 != 0x7FF) ? i2 * 0.1f : NAN;
    r->voltage3 = (v3 != 0x1FFF) ? v3 * 0.01f : NAN;
    r->current3 = (i3 != 0x7FF) ? i3 * 0.1f : NAN;
    r->temperature = (temp != 0x7F) ? (float)temp - 40.0f : NAN;
    r->ac_current = (ac_cur != 0x1FF) ? ac_cur * 0.1f : NAN;
    return true;
}

/* --- Decode entry point ----------------------------------------------- */

victronble_err_t victronble_decode(const uint8_t *mfg, size_t len,
                                   const uint8_t key[VICTRONBLE_KEY_LEN],
                                   victronble_record_t *out)
{
    if (mfg == NULL || len < VICTRONBLE_MIN_MFG_LEN) {
        return VICTRONBLE_ERR_SHORT;
    }
    if (get_le16(mfg) != VICTRONBLE_COMPANY_ID) {
        return VICTRONBLE_ERR_NOT_VICTRON;
    }
    if (mfg[OFF_RECORD] != PRODUCT_ADV) {
        return VICTRONBLE_ERR_NOT_PRODUCT;
    }
    if (mfg[OFF_KEYCHECK] != key[0]) {
        return VICTRONBLE_ERR_KEY_MISMATCH;
    }

    uint16_t nonce = get_le16(mfg + OFF_NONCE);

    /* IV: nonce in the two low bytes (LE), remaining 14 bytes zero. */
    uint8_t iv[16] = {0};
    iv[0] = (uint8_t)(nonce & 0xFF);
    iv[1] = (uint8_t)(nonce >> 8);

    /* Decrypt what's on the wire; zero-pad to the full payload size so the
     * per-type decoders see a fixed-length buffer (matches the proven
     * implementation, which zero-filled the wire struct before copy-in). */
    uint8_t plain[VICTRONBLE_MAX_CIPHER_LEN] = {0};
    size_t cipher_len = len - OFF_CIPHER;

    if (cipher_len > VICTRONBLE_MAX_CIPHER_LEN) {
        cipher_len = VICTRONBLE_MAX_CIPHER_LEN;
    }
    if (aes_ctr(key, iv, mfg + OFF_CIPHER, plain, cipher_len) != 0) {
        return VICTRONBLE_ERR_CRYPTO;
    }

    victronble_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.record_type = mfg[OFF_DEVTYPE];
    rec.model_id = get_le16(mfg + OFF_MODEL);
    rec.readout_type = mfg[OFF_READOUT];
    rec.nonce = nonce;

    bool ok = false;
    switch (mfg[OFF_DEVTYPE]) {
    case VICTRONBLE_DEV_SOLAR_CHARGER:
        rec.type = VICTRONBLE_DEV_SOLAR_CHARGER;
        ok = parse_solar_charger(plain, sizeof(plain), &rec.u.solar);
        break;
    case VICTRONBLE_DEV_BATTERY_MONITOR:
        rec.type = VICTRONBLE_DEV_BATTERY_MONITOR;
        ok = parse_battery_monitor(plain, sizeof(plain), &rec.u.batmon);
        break;
    case VICTRONBLE_DEV_INVERTER:
    case VICTRONBLE_DEV_INVERTER_RS:
    case VICTRONBLE_DEV_MULTI_RS:
    case VICTRONBLE_DEV_VE_BUS:
        rec.type = VICTRONBLE_DEV_INVERTER;
        ok = parse_inverter(plain, sizeof(plain), &rec.u.inverter);
        break;
    case VICTRONBLE_DEV_DCDC_CONVERTER:
        rec.type = VICTRONBLE_DEV_DCDC_CONVERTER;
        ok = parse_dcdc(plain, sizeof(plain), &rec.u.dcdc);
        break;
    case VICTRONBLE_DEV_AC_CHARGER:
        rec.type = VICTRONBLE_DEV_AC_CHARGER;
        ok = parse_ac_charger(plain, sizeof(plain), &rec.u.ac);
        break;
    default:
        return VICTRONBLE_ERR_UNSUPPORTED;
    }

    if (!ok) {
        return VICTRONBLE_ERR_SHORT;
    }
    *out = rec;
    return VICTRONBLE_OK;
}

/* --- Helpers ----------------------------------------------------------- */

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

bool victronble_parse_key(const char *hex, uint8_t key[VICTRONBLE_KEY_LEN])
{
    if (hex == NULL || strlen(hex) != VICTRONBLE_KEY_LEN * 2) {
        return false;
    }
    for (size_t i = 0; i < VICTRONBLE_KEY_LEN; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);

        if (hi < 0 || lo < 0) {
            return false;
        }
        key[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

const char *victronble_strerror(victronble_err_t err)
{
    switch (err) {
    case VICTRONBLE_OK:               return "ok";
    case VICTRONBLE_ERR_NOT_VICTRON:  return "not victron";
    case VICTRONBLE_ERR_SHORT:        return "truncated";
    case VICTRONBLE_ERR_NOT_PRODUCT:  return "not product adv";
    case VICTRONBLE_ERR_KEY_MISMATCH: return "key mismatch";
    case VICTRONBLE_ERR_UNSUPPORTED:  return "unsupported type";
    case VICTRONBLE_ERR_CRYPTO:       return "crypto error";
    default:                          return "unknown error";
    }
}

const char *victronble_device_type_str(victronble_device_type_t type)
{
    switch (type) {
    case VICTRONBLE_DEV_SOLAR_CHARGER:   return "solar charger";
    case VICTRONBLE_DEV_BATTERY_MONITOR: return "battery monitor";
    case VICTRONBLE_DEV_INVERTER:        return "inverter";
    case VICTRONBLE_DEV_DCDC_CONVERTER:  return "dc-dc converter";
    case VICTRONBLE_DEV_SMART_LITHIUM:   return "smart lithium";
    case VICTRONBLE_DEV_INVERTER_RS:     return "inverter rs";
    case VICTRONBLE_DEV_GX_DEVICE:       return "gx device";
    case VICTRONBLE_DEV_AC_CHARGER:      return "ac charger";
    case VICTRONBLE_DEV_BATTERY_PROTECT: return "battery protect";
    case VICTRONBLE_DEV_LYNX_SMART_BMS:  return "lynx smart bms";
    case VICTRONBLE_DEV_MULTI_RS:        return "multi rs";
    case VICTRONBLE_DEV_VE_BUS:          return "ve.bus";
    case VICTRONBLE_DEV_DC_ENERGY_METER: return "dc energy meter";
    case VICTRONBLE_DEV_ORION_XS:        return "orion xs";
    default:                             return "unknown";
    }
}

const char *victronble_state_str(uint8_t state)
{
    switch (state) {
    case VICTRONBLE_STATE_OFF:              return "off";
    case VICTRONBLE_STATE_LOW_POWER:        return "low";
    case VICTRONBLE_STATE_FAULT:            return "fault";
    case VICTRONBLE_STATE_BULK:             return "bulk";
    case VICTRONBLE_STATE_ABSORPTION:       return "abs";
    case VICTRONBLE_STATE_FLOAT:            return "float";
    case VICTRONBLE_STATE_STORAGE:          return "store";
    case VICTRONBLE_STATE_EQUALIZE:         return "eq";
    case VICTRONBLE_STATE_INVERTING:        return "invert";
    case VICTRONBLE_STATE_POWER_SUPPLY:     return "psu";
    case VICTRONBLE_STATE_EXTERNAL_CONTROL: return "ext";
    default:                                return "?";
    }
}

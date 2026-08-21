/**
 * victronble — pure C99 decoder for Victron Energy "Instant Readout" BLE
 * advertisements (manufacturer ID 0x02E1, record type 0x10, AES-128-CTR).
 *
 * This is the portable core of the VictronBLE library: no Arduino, no BLE
 * stack, no allocation, no I/O, reentrant. Feed it one manufacturer-specific
 * data blob (starting at the company ID) plus the device's 16-byte
 * advertisement key; get a decoded record back. Transport (scanning), device
 * registries, rate limiting and logging belong to the platform wrappers
 * (Arduino C++ class, Zephyr module).
 *
 * Copyright (c) 2025-2026 Scott Penrose
 * License: MIT
 */

#ifndef VICTRONBLE_H
#define VICTRONBLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VICTRONBLE_COMPANY_ID     0x02E1u  /* Victron Energy BV */
#define VICTRONBLE_KEY_LEN        16
#define VICTRONBLE_MAX_CIPHER_LEN 21       /* encrypted payload in one advert */
#define VICTRONBLE_MIN_MFG_LEN    10       /* header before the ciphertext */

typedef enum {
    VICTRONBLE_OK               =  0,
    VICTRONBLE_ERR_NOT_VICTRON  = -1,  /* company ID mismatch */
    VICTRONBLE_ERR_SHORT        = -2,  /* truncated advertisement */
    VICTRONBLE_ERR_NOT_PRODUCT  = -3,  /* not a product-advertisement record */
    VICTRONBLE_ERR_KEY_MISMATCH = -4,  /* key check byte failed */
    VICTRONBLE_ERR_UNSUPPORTED  = -5,  /* known record type, no decoder */
    VICTRONBLE_ERR_CRYPTO       = -6,  /* AES backend failed */
} victronble_err_t;

/* Values are the raw Victron record-type IDs. The inverter family
 * (0x03/0x06/0x0B/0x0C) all decode to VICTRONBLE_DEV_INVERTER. */
typedef enum {
    VICTRONBLE_DEV_UNKNOWN          = 0x00,
    VICTRONBLE_DEV_SOLAR_CHARGER    = 0x01,
    VICTRONBLE_DEV_BATTERY_MONITOR  = 0x02,
    VICTRONBLE_DEV_INVERTER         = 0x03,
    VICTRONBLE_DEV_DCDC_CONVERTER   = 0x04,
    VICTRONBLE_DEV_SMART_LITHIUM    = 0x05,
    VICTRONBLE_DEV_INVERTER_RS      = 0x06,
    VICTRONBLE_DEV_GX_DEVICE        = 0x07,
    VICTRONBLE_DEV_AC_CHARGER       = 0x08,
    VICTRONBLE_DEV_BATTERY_PROTECT  = 0x09,
    VICTRONBLE_DEV_LYNX_SMART_BMS   = 0x0A,
    VICTRONBLE_DEV_MULTI_RS         = 0x0B,
    VICTRONBLE_DEV_VE_BUS           = 0x0C,
    VICTRONBLE_DEV_DC_ENERGY_METER  = 0x0D,
    VICTRONBLE_DEV_ORION_XS         = 0x0F,
} victronble_device_type_t;

/* Charger states shared by solar / AC chargers (VE.Direct "CS"). */
enum {
    VICTRONBLE_STATE_OFF              = 0,
    VICTRONBLE_STATE_LOW_POWER        = 1,
    VICTRONBLE_STATE_FAULT            = 2,
    VICTRONBLE_STATE_BULK             = 3,
    VICTRONBLE_STATE_ABSORPTION       = 4,
    VICTRONBLE_STATE_FLOAT            = 5,
    VICTRONBLE_STATE_STORAGE          = 6,
    VICTRONBLE_STATE_EQUALIZE         = 7,
    VICTRONBLE_STATE_INVERTING        = 9,
    VICTRONBLE_STATE_POWER_SUPPLY     = 11,
    VICTRONBLE_STATE_EXTERNAL_CONTROL = 252,
};

/* Fields the wire encodes as "not available" are NAN (test with isnan()).
 * Integer fields keep the raw value; sentinels are documented per field. */

typedef struct {
    uint8_t  state;               /* VICTRONBLE_STATE_* */
    uint8_t  error;
    float    battery_voltage;     /* V */
    float    battery_current;     /* A */
    float    pv_power;            /* W */
    uint32_t yield_today_wh;      /* Wh */
    float    load_current;        /* A, NAN if no load output */
} victronble_solar_charger_t;

typedef struct {
    float    voltage;             /* V */
    float    current;             /* A */
    float    temperature;         /* degC, NAN unless aux mode = temperature */
    float    aux_voltage;         /* V, NAN unless aux mode = aux voltage */
    uint16_t remaining_minutes;   /* time-to-go; 0xFFFF = not available */
    float    consumed_ah;         /* Ah, negative = consumed */
    float    soc;                 /* % */
    uint8_t  aux_mode;            /* 0=aux V, 1=midpoint, 2=temperature, 3=none */
    uint16_t alarm;               /* raw 16-bit alarm bitmask */
} victronble_battery_monitor_t;

typedef struct {
    uint8_t  state;
    uint8_t  alarms;              /* raw alarm bits: 0x01 lowV, 0x02 highV,
                                   * 0x04 highT, 0x08 overload */
    float    battery_voltage;     /* V */
    float    battery_current;     /* A */
    float    ac_power;            /* W (signed) */
} victronble_inverter_t;

typedef struct {
    uint8_t  state;               /* charge state */
    uint8_t  error;
    float    input_voltage;       /* V */
    float    output_voltage;      /* V */
    float    output_current;      /* A */
} victronble_dcdc_t;

typedef struct {
    uint8_t  state;
    uint8_t  error;
    float    voltage1, current1;  /* output 1 (V, A), NAN if absent */
    float    voltage2, current2;  /* output 2 */
    float    voltage3, current3;  /* output 3 */
    float    temperature;         /* degC, NAN if not available */
    float    ac_current;          /* A, NAN if not available */
} victronble_ac_charger_t;

typedef struct {
    victronble_device_type_t type;   /* decoded family (inverter collapsed) */
    uint8_t  record_type;            /* raw record type from the wire */
    uint16_t model_id;
    uint8_t  readout_type;
    uint16_t nonce;                  /* data counter, as received */
    union {
        victronble_solar_charger_t   solar;
        victronble_battery_monitor_t batmon;
        victronble_inverter_t        inverter;
        victronble_dcdc_t            dcdc;
        victronble_ac_charger_t      ac;
    } u;
} victronble_record_t;

/**
 * Decode one Victron manufacturer-data blob.
 *
 * @param mfg  Manufacturer-specific data, starting at the company ID.
 * @param len  Length of @p mfg in bytes.
 * @param key  16-byte per-device advertisement key.
 * @param out  Populated on VICTRONBLE_OK; untouched otherwise.
 *
 * Reentrant, allocation-free, no I/O. Duplicate suppression (nonce
 * tracking) is the caller's job — the nonce is returned in @p out.
 */
victronble_err_t victronble_decode(const uint8_t *mfg, size_t len,
                                   const uint8_t key[VICTRONBLE_KEY_LEN],
                                   victronble_record_t *out);

/** Cheap pre-filter: company ID + product-advertisement record, no crypto. */
bool victronble_is_product_adv(const uint8_t *mfg, size_t len);

/** Key check byte test — pick the right key from several without decrypting. */
bool victronble_key_matches(const uint8_t *mfg, size_t len,
                            const uint8_t key[VICTRONBLE_KEY_LEN]);

/** Parse a 32-hex-char advertisement key. Returns false on bad input. */
bool victronble_parse_key(const char *hex, uint8_t key[VICTRONBLE_KEY_LEN]);

const char *victronble_strerror(victronble_err_t err);
const char *victronble_device_type_str(victronble_device_type_t type);
/** Short lower-case charger-state label ("bulk", "float", ...). */
const char *victronble_state_str(uint8_t state);

/**
 * AES-128-CTR transform hook.
 *
 * @param key  16-byte key.
 * @param iv   16-byte initial counter block (nonce in the low bytes, rest 0).
 * @param in   Ciphertext.
 * @param out  Plaintext. May alias @p in.
 * @param len  Byte count, not necessarily a multiple of 16.
 * @param user Opaque context supplied at registration.
 * @return 0 on success, negative on failure.
 */
typedef int (*victronble_aes_ctr_fn)(const uint8_t key[16],
                                     const uint8_t iv[16],
                                     const uint8_t *in, uint8_t *out,
                                     size_t len, void *user);

/** Override the AES backend at runtime (NULL restores the default). */
void victronble_set_aes_ctr(victronble_aes_ctr_fn fn, void *user);

/**
 * Default AES backend. Weak symbol: the bundled software AES
 * (victronble_aes_sw.c) provides it; an alternative backend (PSA, mbedTLS,
 * hardware) may define it strong and the linker drops the bundled code.
 */
int victronble_aes_ctr_default(const uint8_t key[16], const uint8_t iv[16],
                               const uint8_t *in, uint8_t *out,
                               size_t len, void *user);

#ifdef __cplusplus
}
#endif

#endif /* VICTRONBLE_H */

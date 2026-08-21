# victronble → pure C core + Zephyr module

Staged porting plan. Five stages, each independently shippable. Stages 0–2 leave the
existing PlatformIO library working the whole way through; Zephyr only appears at Stage 3.

**Assumption throughout:** the protocol offsets, model IDs, sentinel values and per-device
bit layouts already exist and are correct in your ESP32/nRF52 implementation. This plan does
not re-derive them — it restructures around them. Where a byte offset appears below it is
illustrative; take the canonical values from your working code and from Victron's
*Extra Manufacturer Data* PDF.

---

## Stage 0 — Throwaway Zephyr spike

**Time:** 2 hours. **Output:** deleted afterwards. **Purpose:** de-risk three unknowns
before you commit to an API shape.

Copy the parse functions verbatim into a single `main.c`. Hardcode the key and MAC.
`printk` one decoded SmartSolar record. Do not abstract anything.

What you are actually finding out:

1. **Does the bundled AES build clean under Zephyr's toolchain** with no Arduino headers
   dragged in behind it. If it doesn't, you learn that now rather than at Stage 3.
2. **Where the decrypt has to live.** The scan callback runs on the BT RX thread. Time
   spent there delays HCI event processing. Confirm you can decrypt inline for a spike,
   then confirm you don't want to.
3. **Whether `bt_data_parse()` gives you what you expect.** In particular that
   `BT_DATA_MANUFACTURER_DATA` arrives with the company ID as the first two bytes of
   `data->data`, and that the payload is intact at the length you expect.

Minimal `prj.conf`:

```
CONFIG_BT=y
CONFIG_BT_OBSERVER=y
CONFIG_BT_DEVICE_NAME="victron-spike"
CONFIG_LOG=y
CONFIG_LOG_MODE_IMMEDIATE=y
```

Minimal scan setup:

```c
static const struct bt_le_scan_param scan_param = {
    .type     = BT_LE_SCAN_TYPE_PASSIVE,
    .options  = BT_LE_SCAN_OPT_NONE,
    .interval = BT_GAP_SCAN_FAST_INTERVAL,
    .window   = BT_GAP_SCAN_FAST_WINDOW,
};

static bool ad_cb(struct bt_data *data, void *user_data)
{
    if (data->type != BT_DATA_MANUFACTURER_DATA) {
        return true;                    /* keep walking the AD structures */
    }
    if (data->data_len < 10 || sys_get_le16(data->data) != 0x02E1) {
        return true;
    }
    /* ... spike decrypt here ... */
    return false;                       /* found it, stop */
}

static void scan_recv(const bt_addr_le_t *addr, int8_t rssi,
                      uint8_t adv_type, struct net_buf_simple *ad)
{
    bt_data_parse(ad, ad_cb, (void *)addr);
}
```

Two traps worth knowing before you hit them:

- **`bt_data_parse()` consumes the buffer.** It pulls from the `net_buf_simple` as it
  walks. If you need the raw advertisement afterwards, clone the state or copy the bytes
  out first.
- **Callback registration is version-sensitive.** The `bt_le_scan_start(&param, cb)` form
  and the newer `bt_le_scan_cb_register()` / `struct bt_le_scan_cb` form have coexisted
  across releases with the former deprecated at various points. Check which one your
  Zephyr/NCS version wants rather than trusting any example you find online, including
  this one.

**Exit criterion:** one real record from one real SmartSolar, decrypted and printed
correctly on hardware. Then delete the spike.

---

## Stage 1 — Extract the pure C99 core

This is the bulk of the work and the part with value independent of Zephyr. When it's
done you can unit-test the parser on your workstation for the first time.

### Rules for the core

- C99. No C++, no `String`, no Arduino headers, no `Serial`.
- No allocation. Ever. Caller owns all storage.
- No I/O. No logging. Return codes only — the caller decides what to say about them.
- Freestanding-safe: `<stdint.h>`, `<stddef.h>`, `<string.h>`, `<math.h>` only.
- Reentrant. No file-scope mutable state in the parse path.

### `include/victronble.h`

```c
#ifndef VICTRONBLE_H
#define VICTRONBLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VICTRONBLE_COMPANY_ID     0x02E1u   /* Victron Energy BV */
#define VICTRONBLE_KEY_LEN        16
#define VICTRONBLE_MAX_MFG_LEN    31

typedef enum {
    VICTRONBLE_OK               =  0,
    VICTRONBLE_ERR_NOT_VICTRON  = -1,  /* company ID mismatch */
    VICTRONBLE_ERR_SHORT        = -2,  /* truncated advertisement */
    VICTRONBLE_ERR_NOT_PRODUCT  = -3,  /* not a product-advertisement record */
    VICTRONBLE_ERR_KEY_MISMATCH = -4,  /* key check byte failed */
    VICTRONBLE_ERR_UNSUPPORTED  = -5,  /* known record type, no decoder */
    VICTRONBLE_ERR_CRYPTO       = -6,  /* AES backend failed */
    VICTRONBLE_ERR_DUPLICATE    = -7,  /* counter already seen (dedup enabled) */
} victronble_err_t;

typedef enum {
    VICTRONBLE_DEV_UNKNOWN = 0,
    VICTRONBLE_DEV_SOLAR_CHARGER,
    VICTRONBLE_DEV_BATTERY_MONITOR,
    VICTRONBLE_DEV_INVERTER,
    VICTRONBLE_DEV_DCDC_CONVERTER,
    VICTRONBLE_DEV_SMART_LITHIUM,
    VICTRONBLE_DEV_AC_CHARGER,
    /* extend from your existing enum */
} victronble_device_type_t;

typedef struct {
    float    battery_voltage;      /* V,  NAN if not present */
    float    battery_current;      /* A,  NAN if not present */
    float    yield_today;          /* kWh */
    float    pv_power;             /* W */
    float    load_current;         /* A,  NAN if load output absent */
    uint8_t  state;
    uint8_t  error;
} victronble_solar_charger_t;

/* ... battery monitor, inverter, dcdc, etc. ... */

typedef struct {
    victronble_device_type_t type;
    uint16_t model_id;
    uint16_t counter;              /* nonce / data counter, as received */
    union {
        victronble_solar_charger_t   solar;
        victronble_battery_monitor_t batmon;
        /* ... */
    } u;
} victronble_record_t;

/**
 * Decode one Victron manufacturer-data blob.
 *
 * @param mfg  Manufacturer-specific data, starting at the company ID.
 * @param len  Length of @p mfg.
 * @param key  16-byte per-device advertisement key.
 * @param out  Populated on VICTRONBLE_OK. Untouched otherwise.
 *
 * Reentrant, allocation-free, no I/O.
 */
victronble_err_t victronble_decode(const uint8_t *mfg, size_t len,
                                   const uint8_t key[VICTRONBLE_KEY_LEN],
                                   victronble_record_t *out);

/** Cheap pre-filter: company ID + record type only, no crypto. */
bool victronble_is_product_adv(const uint8_t *mfg, size_t len);

/** Key check byte test, so callers with several keys can pick one without decrypting. */
bool victronble_key_matches(const uint8_t *mfg, size_t len,
                            const uint8_t key[VICTRONBLE_KEY_LEN]);

const char *victronble_strerror(victronble_err_t err);

#ifdef __cplusplus
}
#endif
#endif /* VICTRONBLE_H */
```

`victronble_key_matches()` is worth having separately. With several monitored devices you
otherwise burn an AES operation per device per advertisement just to find out which key
applies. The key check byte answers it for free.

### Sentinels

Victron encodes "not available" as per-field sentinel values, and they differ by field
width and signedness. Getting this wrong is the most likely source of a plausible-looking
but wrong reading, so decide the convention once and apply it everywhere.

Recommendation: **`NAN` for every float field that has a sentinel.** It propagates
correctly through arithmetic, tests cleanly with `isnan()`, and can't be confused with a
real zero the way a magic float can. For integer fields (state, error codes) keep the raw
value and document the sentinel.

If you'd rather avoid `<math.h>` on the smallest targets, the alternative is a
`uint32_t valid` bitmask per record — more code at every call site, but no FP dependency.
I'd only do this if flash is genuinely tight.

### Header layout

Encode the frame header as one internal struct with a single parse function, rather than
scattered offset arithmetic. Fields: record type, model ID (LE16), device/read-out type,
nonce counter (LE16), key check byte, then ciphertext offset and length. Use explicit
`sys_get_le16()`-style accessors rather than casting to packed structs — you'll want this
core to build on anything, and unaligned struct punning is exactly the kind of thing that
works on Cortex-M4 and bites you elsewhere.

---

## Stage 2 — AES abstraction

Split out because it's the one design decision that's hard to reverse later.

### Make the hook CTR-shaped, not ECB-shaped

Tempting to expose a single AES-128-ECB block encrypt, since for a ≤16-byte payload
CTR reduces to *ECB(counter block) XOR ciphertext* and you'd never need more. Don't.
Some record types (VE.Bus, Lynx BMS) exceed one block, and — more importantly — PSA and
every hardware accelerator expose CTR natively. An ECB-shaped hook forces those backends
to reimplement the counter loop that PSA would have done for them.

```c
/**
 * AES-128-CTR transform hook.
 *
 * @param key  16-byte key.
 * @param iv   16-byte initial counter block (nonce in the low bytes, rest zero).
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

void victronble_set_aes_ctr(victronble_aes_ctr_fn fn, void *user);
```

### Selection mechanism

Use a **weak symbol default plus a runtime setter**:

```c
__attribute__((weak))
int victronble_aes_ctr_default(const uint8_t key[16], const uint8_t iv[16],
                               const uint8_t *in, uint8_t *out,
                               size_t len, void *user);
```

The weak symbol lets the linker drop the bundled software AES entirely when a backend
overrides it — which matters on a flash-constrained solar node. The runtime setter covers
the case where the backend is chosen at runtime or in a test harness. Both, not one.

### Backends to ship

| Backend | File | Notes |
|---|---|---|
| Bundled software | `victronble_aes_sw.c` | Current implementation, unchanged. Default. Zero dependencies — keep this property, it's the reason your library ports easily. |
| PSA Crypto | `victronble_aes_psa.c` | `psa_crypto_init()` once, then `psa_cipher_encrypt()` with `PSA_ALG_CTR`. On nRF52840 this routes to CryptoCell (CC310). |
| mbedTLS | `victronble_aes_mbedtls.c` | Optional. `mbedtls_aes_crypt_ctr()`. Mostly for ESP-IDF users who already link it. |

Two PSA notes worth writing down now:

- Key lifetime. Importing a volatile key per advertisement is wasteful. Import once per
  monitored device at registration and cache the `psa_key_id_t`, which means your device
  registry needs somewhere to hold it — plan the struct field now rather than retrofitting.
- `psa_crypto_init()` must have run before any use, and on NCS the relevant Kconfig lives
  under `NRF_SECURITY` rather than plain `MBEDTLS_*`. This diverges between upstream Zephyr
  and NCS and is the single most annoying part of Stage 3.

### Host test harness

This is the payoff for Stages 1–2. Capture advertisement frames from your working ESP32
build as hex, pair them with expected decoded values, and run the core under plain `gcc`
on the workstation:

```c
static const struct {
    const char              *hex;
    const char              *key_hex;
    victronble_err_t         expect_err;
    victronble_device_type_t expect_type;
    float                    expect_batt_v;
} vectors[] = {
    { "e10210...", "0df4d0...", VICTRONBLE_OK, VICTRONBLE_DEV_SOLAR_CHARGER, 13.24f },
    /* one per device type, plus: truncated frame, wrong key, unknown record type */
};
```

No test framework needed — a `main()` and a non-zero exit is enough, and it drops straight
into CI. Same shape as the LoRaScope parser vectors. Include the negative cases; the
error paths are where a parser rewrite actually breaks.

---

## Stage 3 — Arduino wrapper over the C core

Before touching Zephyr, prove the extraction by making the existing library a consumer
of it. `VictronBLE` becomes a thin C++ class that owns the device table and calls
`victronble_decode()`. The BLE backends (NimBLE / Bluefruit) keep their current structure
and feed raw manufacturer bytes into the core.

If the public C++ API is unchanged, this is a patch release and existing PlatformIO users
notice nothing. That's the goal. Any pressure to change the C++ API here is a signal that
the C core's shape is wrong — fix the core, not the wrapper.

---

## Stage 4 — Zephyr module

Now the C core exists and is tested, this is mostly plumbing.

### Repo layout

One repo serves both ecosystems. PlatformIO reads `library.json` and ignores CMake;
Zephyr reads `zephyr/module.yml` and ignores `library.json`.

```
victronble/
├── library.json              # PlatformIO
├── CMakeLists.txt            # Zephyr module entry point
├── Kconfig
├── zephyr/
│   └── module.yml
├── include/
│   └── victronble.h          # pure C core
│   └── victronble_zephyr.h   # Zephyr-specific observer API
├── src/
│   ├── victronble_core.c     # pure C99, no dependencies
│   ├── victronble_aes_sw.c
│   ├── victronble_aes_psa.c
│   ├── victronble_zephyr.c   # scan + workqueue + device registry
│   ├── VictronBLE.cpp        # Arduino wrapper
│   └── ble_backend_*.cpp     # NimBLE / Bluefruit
├── samples/
│   └── observer/             # Zephyr sample app
└── tests/
    └── vectors/              # host-runnable, also Ztest under native_sim
```

### `zephyr/module.yml`

```yaml
name: victronble
build:
  cmake: .
  kconfig: Kconfig
```

### `CMakeLists.txt`

```cmake
if(CONFIG_VICTRONBLE)
  zephyr_library()
  zephyr_library_sources(src/victronble_core.c)
  zephyr_library_sources(src/victronble_zephyr.c)
  zephyr_library_sources_ifdef(CONFIG_VICTRONBLE_CRYPTO_SOFTWARE src/victronble_aes_sw.c)
  zephyr_library_sources_ifdef(CONFIG_VICTRONBLE_CRYPTO_PSA      src/victronble_aes_psa.c)
  zephyr_include_directories(include)
endif()
```

### `Kconfig`

```
menuconfig VICTRONBLE
	bool "Victron Instant Readout BLE observer"
	depends on BT_OBSERVER
	help
	  Passive BLE observer for Victron Energy devices broadcasting
	  Instant Readout advertisements. No connection or pairing required.

if VICTRONBLE

config VICTRONBLE_MAX_DEVICES
	int "Maximum monitored devices"
	default 4

config VICTRONBLE_QUEUE_DEPTH
	int "Advertisement queue depth"
	default 8
	help
	  Frames are copied off the BT RX thread into this queue and decoded
	  by a dedicated thread. Overflow drops the oldest frame.

config VICTRONBLE_THREAD_STACK_SIZE
	int "Decode thread stack size"
	default 1024

config VICTRONBLE_THREAD_PRIORITY
	int "Decode thread priority"
	default 10

config VICTRONBLE_DEDUP
	bool "Drop repeated advertisements by nonce counter"
	default y
	help
	  Each advertisement is broadcast on three channels and repeated.
	  Tracking the last counter per device suppresses the duplicates.

choice VICTRONBLE_CRYPTO
	prompt "AES-CTR backend"
	default VICTRONBLE_CRYPTO_SOFTWARE

config VICTRONBLE_CRYPTO_SOFTWARE
	bool "Bundled software AES-128"

config VICTRONBLE_CRYPTO_PSA
	bool "PSA Crypto"
	depends on MBEDTLS_PSA_CRYPTO_C || NRF_SECURITY

endchoice

module = VICTRONBLE
module-str = victronble
source "subsys/logging/Kconfig.template.log_config"

endif
```

### Threading model

Do not decode in the scan callback. Copy and hand off:

```c
struct victronble_frame {
    bt_addr_le_t addr;
    int8_t       rssi;
    uint8_t      len;
    uint8_t      data[VICTRONBLE_MAX_MFG_LEN];
};

K_MSGQ_DEFINE(vb_msgq, sizeof(struct victronble_frame),
              CONFIG_VICTRONBLE_QUEUE_DEPTH, 4);
```

The scan callback pre-filters with `victronble_is_product_adv()` — company ID and record
type, no crypto — then `k_msgq_put()` with `K_NO_WAIT`. A dedicated thread pops frames,
matches against the device registry by address, calls `victronble_decode()`, and invokes
the user callback from its own context. Drop on full queue and count the drops; a
saturated queue is a real signal on a busy site and you want it visible.

Use a dedicated thread rather than the system workqueue. Crypto on the system workqueue
will eventually collide with something else that assumed it was free.

### Public Zephyr API

Since you're dropping the C++ callback structure, make this idiomatic Zephyr rather than
a translation of the Arduino API. A registered-listener list in the style of
`bt_conn_cb_register()` will read as native to anyone in this ecosystem:

```c
struct victronble_cb {
    void (*record)(const bt_addr_le_t *addr, int8_t rssi,
                   const victronble_record_t *rec);
    void (*decode_error)(const bt_addr_le_t *addr, victronble_err_t err);
    sys_snode_t node;
};

int victronble_cb_register(struct victronble_cb *cb);
int victronble_device_add(const bt_addr_le_t *addr,
                          const uint8_t key[VICTRONBLE_KEY_LEN]);
int victronble_device_remove(const bt_addr_le_t *addr);
int victronble_start(void);
int victronble_stop(void);
```

Consider a devicetree binding for statically configured devices later — it's the most
Zephyr-native option and would let a node declare its Victron gear in the overlay — but
don't do it in the first release. Get the runtime API right first.

### Scan parameters

`BT_GAP_SCAN_FAST_*` is wrong for a long-running solar node. Victron broadcasts roughly
once per second, so a low duty cycle catches everything at a fraction of the radio-on
time. Start at `BT_GAP_SCAN_SLOW_INTERVAL_1` / `BT_GAP_SCAN_SLOW_WINDOW_1` and measure —
you have the PPK2 set up, and this is exactly the knob worth characterising for the
downstream OGLAS power budget.

### Sample `prj.conf`

```
CONFIG_BT=y
CONFIG_BT_OBSERVER=y
CONFIG_BT_DEVICE_NAME="victron-observer"
CONFIG_VICTRONBLE=y
CONFIG_VICTRONBLE_MAX_DEVICES=4
CONFIG_LOG=y
CONFIG_VICTRONBLE_LOG_LEVEL_INF=y
```

On a busy site you may need to raise `CONFIG_BT_BUF_EVT_DISCARDABLE_COUNT`; advertising
reports are discardable events and the default pool is easy to exhaust with a passive
scan in a dense RF environment.

### Development loop worth setting up

`native_sim` with `CONFIG_BT_USERCHAN=y` binds the Zephyr Bluetooth host to a real HCI
controller on the Linux host. You can run the full observer on the workstation against
your actual SmartSolar, with gdb and no flash cycle. Worth the half hour it takes to
configure — it will pay for itself during the record-type work.

---

## Stage 5 — Publish

1. `samples/observer/` that builds for `nrf52840dk/nrf52840` and `rak4631/nrf52840`.
   A sample that builds for a DK anyone owns is what makes people try it.
2. GitHub Actions: host vector tests, plus `west build` for both boards and `native_sim`.
3. README with the west manifest snippet up front — the first question every Zephyr user
   has is how to add it to their workspace:

```yaml
manifest:
  remotes:
    - name: dd
      url-base: https://github.com/scottp
  projects:
    - name: victronble
      remote: dd
      revision: main
      path: modules/lib/victronble
```

4. Announce, roughly in descending order of return:
   - The Victron Community *Bluetooth advertising protocol* thread.
   - PR to `keshavdv/victron-ble`'s related-projects list — that repo is the ecosystem hub.
   - Nordic DevZone and the Zephyr Discord `#bluetooth` channel.
   - `zephyr-rtos` GitHub topic, awesome-list PR.

---

## Sequencing summary

| Stage | Effort | Ships? | Risk if skipped |
|---|---|---|---|
| 0 — Spike | 2 h | No | Design the C API around assumptions Zephyr won't honour |
| 1 — C core | 1–2 days | Yes (patch) | — |
| 2 — AES hook | half day | Yes | Hard to change once backends exist downstream |
| 3 — Arduino wrapper | half day | Yes (patch) | Core shape never validated against a real consumer |
| 4 — Zephyr module | 1–2 days | Yes (minor) | — |
| 5 — Publish | half day | Yes | Nobody finds it |

The only stage with real unknowns is 0, which is why it's first and disposable.

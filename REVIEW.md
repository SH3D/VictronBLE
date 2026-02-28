# VictronBLE Code Review

## Part 1: Bug Fixes, Efficiency & Simplification ✅ COMPLETE (v0.4.1)

### Bugs

**1. Missing virtual destructor on `VictronDeviceData` (CRITICAL)**
`VictronBLE.h:123` - The base struct has no virtual destructor, but derived objects (`SolarChargerData`, etc.) are deleted through base pointers at `VictronBLE.cpp:287` (`delete data`). This is **undefined behavior** in C++. The derived destructors (which must clean up the `String` members they inherit) may never run.

Fix: Add `virtual ~VictronDeviceData() {}` — or better, eliminate the inheritance (see simplification below).

**2. `nullPad` field in `victronManufacturerData` is wrong**
`VictronBLE.h:68` - Comment says "extra byte because toCharArray() adds a \0 byte" but the code uses `std::string::copy()` which does NOT null-terminate. This makes the struct 1 byte too large, which is harmless but misleading. If the BLE stack ever returns exactly `sizeof(victronManufacturerData)` bytes, the copy would read past the source buffer.

Fix: Remove the `nullPad` field.

**3. `panelVoltage` calculation is unreliable**
`VictronBLE.cpp:371-376` - PV voltage is computed as `panelPower / batteryCurrent`. On an MPPT charger, battery current and PV current are different (that's the whole point of MPPT). This gives a meaningless number. The BLE protocol doesn't transmit PV voltage for solar chargers.

Fix: Remove `panelVoltage` from `SolarChargerData`. It's not in the protocol and the calculation is wrong.

**4. Aux data voltage/temperature heuristic is fragile**
`VictronBLE.cpp:410` - `if (payload->auxData < 3000)` is used to distinguish voltage from temperature. The Victron protocol actually uses a bit flag (bit 15 of the aux field, or the record subtype) to indicate which type of aux input is connected. The magic number 3000 will misclassify edge cases.

Fix: Use the proper protocol flag if available, or document this as a known limitation.

### Efficiency Improvements

**5. `hexStringToBytes` allocates 16 String objects**
`VictronBLE.cpp:610-611` - For each byte, `hex.substring()` creates a new heap-allocated `String`. On ESP32, this fragments the heap unnecessarily.

Fix: Direct char-to-nibble conversion:
```cpp
bool hexStringToBytes(const char* hex, uint8_t* bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t hi = hex[i*2], lo = hex[i*2+1];
        hi = (hi >= 'a') ? hi - 'a' + 10 : (hi >= 'A') ? hi - 'A' + 10 : hi - '0';
        lo = (lo >= 'a') ? lo - 'a' + 10 : (lo >= 'A') ? lo - 'A' + 10 : lo - '0';
        if (hi > 15 || lo > 15) return false;
        bytes[i] = (hi << 4) | lo;
    }
    return true;
}
```

**6. MAC normalization on every lookup is wasteful**
`normalizeMAC()` is called in `processDevice()` for every BLE advertisement received (could be hundreds per scan), plus in every `getSolarChargerData()` / `getBatteryMonitorData()` call. Each call creates a new String and does 3 replace operations.

Fix: Normalize once at `addDevice()` time and store as a fixed `char[13]` (12 hex chars + null). Use `memcmp` or `strcmp` for comparison.

**7. `std::map<String, DeviceInfo*>` is heavy**
A typical setup monitors 1-4 devices. `std::map` has significant overhead (red-black tree, heap allocations for nodes). A simple fixed-size array with linear search would be faster and use less memory.

Fix: Replace with `DeviceInfo devices[MAX_DEVICES]` (where MAX_DEVICES = 8 or similar) and a `uint8_t deviceCount`.

**8. `loop()` blocks for entire scan duration**
`VictronBLE.cpp:140` - `pBLEScan->start(scanDuration, false)` is blocking. With the default 5-second scan duration, `loop()` blocks for 5 seconds every call.

Fix: Use `pBLEScan->start(0)` for continuous non-blocking scan, or use the async scan API. Data arrives via callbacks anyway.

### Simplification — Things to Remove

**9. Remove `VictronDeviceConfig` struct**
Only used as a parameter to `addDevice`. The convenience overload `addDevice(name, mac, key, type)` is what all examples use. The config struct adds an unnecessary layer.

**10. Remove `lastError` / `getLastError()`**
Uses heap-allocated String. If `debugEnabled` is true, errors already go to Serial. If debug is off, nobody calls `getLastError()` — none of the examples use it. Remove entirely.

**11. Remove `getDevicesByType()`**
No examples use it. Returns `std::vector<String>` which heap-allocates. Users already know their device MACs since they registered them.

**12. Remove `removeDevice()`**
No examples use it. In a typical embedded deployment, devices are configured once at startup and never removed.

**13. Remove the per-type getter methods**
`getSolarChargerData()`, `getBatteryMonitorData()`, etc. are polling-style accessors. All examples use the callback pattern instead. The getters copy the entire data struct (including Strings) which is expensive. If needed, a single `getData(mac, type)` returning a pointer would suffice.

**14. Flatten the inheritance hierarchy**
`VictronDeviceData` → `SolarChargerData` etc. uses inheritance + dynamic allocation + virtual dispatch (needed once we add virtual destructor). Since each device type is always accessed through its specific type, a tagged union or flat struct per type would be simpler:
```cpp
struct VictronDevice {
    char mac[13];
    char name[32];
    uint8_t deviceType;
    int8_t rssi;
    uint32_t lastUpdate;
    bool dataValid;
    union {
        struct { /* solar fields */ } solar;
        struct { /* battery fields */ } battery;
        struct { /* inverter fields */ } inverter;
        struct { /* dcdc fields */ } dcdc;
    };
};
```
This eliminates heap allocation, virtual dispatch, and the `createDeviceData` factory.

**15. Replace virtual callback class with function pointer**
`VictronDeviceCallback` with 4 virtual methods → a single function pointer:
```cpp
typedef void (*VictronCallback)(const VictronDevice* device);
```
The callback receives the device and can switch on `deviceType`. Simpler, no vtable overhead, compatible with C.

**16. Remove `String` usage throughout**
Arduino `String` uses heap allocation and causes fragmentation. MAC addresses are always 12 hex chars. Device names can use fixed `char[]`. This is the single biggest simplification and memory improvement.

### Summary of Simplified API

After removing the above, the public API would be approximately:
```cpp
void victron_init(uint32_t scanDuration);
bool victron_add_device(const char* name, const char* mac, const char* hexKey, uint8_t type);
void victron_set_callback(VictronCallback cb);
void victron_loop();
```
~4 functions instead of ~15 methods.


All items implemented in v0.4.1. See [VERSIONS](VERSIONS) for full changelog.

---

## Part 2: Multi-Platform BLE Support

### Recommended Test Hardware

Two cheap BLE development boards for testing the platform abstraction:

**1. Seeed XIAO nRF52840 (~$10 USD)**
- Nordic nRF52840 SoC, Bluetooth 5.0, onboard antenna
- Arduino-compatible via Adafruit nRF52 board support package
- Ultra-small (21x17.5mm), USB-C, battery charging built in
- 1MB flash, 256KB RAM, 2MB QSPI flash
- Has mbedtls available via the nRF SDK
- https://www.seeedstudio.com/Seeed-XIAO-BLE-nRF52840-p-5201.html

**2. Raspberry Pi Pico W (~$6 USD)**
- RP2040 + Infineon CYW43439 (WiFi + Bluetooth 5.2 with BLE)
- Arduino-compatible via arduino-pico core (earlephilhower)
- BLE Central role supported (needed for passive scanning)
- Very widely available and cheap
- Different architecture (ARM Cortex-M0+) from ESP32 (Xtensa/RISC-V), good for testing portability
- https://www.raspberrypi.com/products/raspberry-pi-pico/

Both boards are under $15, Arduino-compatible, and have BLE Central support needed for passive scanning of Victron advertisements. They use different BLE stacks (nRF SoftDevice vs CYW43 BTstack) which will validate the transport abstraction layer.

### Current BLE Dependencies

All ESP32-specific BLE code is confined to:

1. **Headers** (`VictronBLE.h`):
   - `#include <BLEDevice.h>`, `<BLEAdvertisedDevice.h>`, `<BLEScan.h>`
   - `BLEScan*` member
   - `VictronBLEAdvertisedDeviceCallbacks` class inheriting `BLEAdvertisedDeviceCallbacks`
   - `BLEAddress` type in `macAddressToString()`

2. **Implementation** (`VictronBLE.cpp`):
   - `BLEDevice::init()` — line 42
   - `BLEDevice::getScan()` — line 43
   - `pBLEScan->setAdvertisedDeviceCallbacks()` — line 52
   - `pBLEScan->setActiveScan/setInterval/setWindow` — lines 53-55
   - `pBLEScan->start()` / `pBLEScan->clearResults()` — lines 140-141
   - `BLEAdvertisedDevice` methods in `processDevice()` — lines 152-213

3. **Non-BLE dependencies**:
   - `mbedtls/aes.h` — available on ESP32, STM32 (via Mbed), and many others
   - `Arduino.h` — available on all Arduino-compatible platforms

### What is NOT platform-specific

The bulk of the code — packet structures, enums, decryption, payload parsing — is pure data processing with no BLE dependency. This is ~70% of the code.

### Recommended Approach: BLE Transport Abstraction

Instead of a full HAL with virtual interfaces (which adds complexity), use a **push-based architecture** where the platform-specific code feeds raw manufacturer data into the parser:

```
Platform BLE Code (user provides) → victron_process_advertisement() → Callback
```

#### Step 1: Extract parser into standalone module

Create `victron_parser.h/.c` containing:
- All packed structs (manufacturer data, payloads)
- All enums (device types, charger states)
- `victron_decrypt()` — AES-CTR decryption
- `victron_parse_advertisement()` — takes raw manufacturer bytes, returns parsed data
- Device registry (add device, lookup by MAC)

This module has **zero BLE dependency**. It needs only `<stdint.h>`, `<string.h>`, and an AES-CTR implementation.

#### Step 2: Platform-specific BLE adapter (thin)

For ESP32 Arduino, provide `VictronBLE_ESP32.h` — a thin wrapper that:
- Sets up BLE scanning
- In the scan callback, extracts MAC + manufacturer data bytes
- Calls `victron_process_advertisement(mac, mfg_data, len, rssi)`

For STM32 (using STM32 BLE stack, or a BLE module like HM-10):
- User writes their own scan callback
- Calls the same `victron_process_advertisement()` function

For NRF52 (using Arduino BLE or nRF SDK):
- Same pattern

#### Step 3: AES portability

`mbedtls` is widely available but not universal. Allow the AES implementation to be swapped:
```c
// User can override before including victron_parser.h
#ifndef VICTRON_AES_CTR_DECRYPT
#define VICTRON_AES_CTR_DECRYPT victron_aes_ctr_mbedtls
#endif
```
Or simply provide a function pointer:
```c
typedef bool (*victron_aes_fn)(const uint8_t* key, const uint8_t* iv,
                                const uint8_t* in, uint8_t* out, size_t len);
void victron_set_aes_impl(victron_aes_fn fn);
```

### Result

- **Parser**: Works on any CPU (ESP32, STM32, NRF52, Linux, etc.)
- **BLE adapter**: ~30 lines of platform-specific glue code
- **AES**: Pluggable, defaults to mbedtls

This approach is simpler than a virtual HAL interface and puts the user in control of their BLE stack.


---

## Part 3: C Core with C++ Wrapper

### Rationale

The "knowledge" in this library is:
1. Victron BLE advertisement packet format (struct layouts)
2. Field encoding (scaling factors, bit packing, sign extension)
3. AES-CTR decryption with nonce construction
4. Device type identification

All of this is pure data processing — no C++ features needed. Moving it to C enables:
- Use in ESP-IDF (C-based) without Arduino
- Use on bare-metal STM32, NRF, PIC, etc.
- Use from other languages via FFI (Python ctypes, Rust FFI, etc.)
- Smaller binary, no RTTI/vtable overhead

### Proposed File Structure

```
src/
  victron_ble_parser.h      # C header — all public types and functions
  victron_ble_parser.c      # C implementation — parsing, decryption, device registry
  VictronBLE.h              # C++ wrapper (Arduino/ESP32 convenience class)
  VictronBLE.cpp            # C++ wrapper implementation
```

### `victron_ble_parser.h` — C API

```c
#ifndef VICTRON_BLE_PARSER_H
#define VICTRON_BLE_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ---- Constants ---- */
#define VICTRON_MANUFACTURER_ID   0x02E1
#define VICTRON_MAX_DEVICES       8
#define VICTRON_ENCRYPTION_KEY_LEN 16
#define VICTRON_MAX_ENCRYPTED_LEN 21
#define VICTRON_MAC_STR_LEN       13   /* 12 hex chars + null */
#define VICTRON_NAME_MAX_LEN      32

/* ---- Enums ---- */
typedef enum {
    VICTRON_DEVICE_UNKNOWN          = 0x00,
    VICTRON_DEVICE_SOLAR_CHARGER    = 0x01,
    VICTRON_DEVICE_BATTERY_MONITOR  = 0x02,
    VICTRON_DEVICE_INVERTER         = 0x03,
    VICTRON_DEVICE_DCDC_CONVERTER   = 0x04,
    VICTRON_DEVICE_SMART_LITHIUM    = 0x05,
    VICTRON_DEVICE_INVERTER_RS      = 0x06,
    /* ... etc ... */
} victron_device_type_t;

typedef enum {
    VICTRON_CHARGER_OFF = 0,
    VICTRON_CHARGER_BULK = 3,
    VICTRON_CHARGER_ABSORPTION = 4,
    VICTRON_CHARGER_FLOAT = 5,
    /* ... etc ... */
} victron_charger_state_t;

/* ---- Wire-format structures (packed) ---- */
typedef struct {
    uint16_t vendor_id;
    uint8_t  beacon_type;
    uint8_t  unknown[3];
    uint8_t  record_type;
    uint16_t nonce;
    uint8_t  key_check;
    uint8_t  encrypted_data[VICTRON_MAX_ENCRYPTED_LEN];
} __attribute__((packed)) victron_mfg_data_t;

typedef struct {
    uint8_t  device_state;
    uint8_t  error_code;
    int16_t  battery_voltage_10mv;
    int16_t  battery_current_10ma;
    uint16_t yield_today_10wh;
    uint16_t input_power_w;
    uint16_t load_current_10ma;
    uint8_t  reserved[2];
} __attribute__((packed)) victron_solar_raw_t;

/* ... similar for battery_monitor, inverter, dcdc ... */

/* ---- Parsed result structures ---- */
typedef struct {
    victron_charger_state_t charge_state;
    float battery_voltage;     /* V  */
    float battery_current;     /* A  */
    float panel_power;         /* W  */
    uint16_t yield_today_wh;
    float load_current;        /* A  */
    uint8_t error_code;
} victron_solar_data_t;

typedef struct {
    float voltage;             /* V  */
    float current;             /* A  */
    float temperature;         /* °C */
    float aux_voltage;         /* V  */
    uint16_t remaining_mins;
    float consumed_ah;
    float soc;                 /* %  */
    uint8_t alarms;            /* raw alarm bits */
} victron_battery_data_t;

/* ... similar for inverter, dcdc ... */

/* Tagged union for any device */
typedef struct {
    char    mac[VICTRON_MAC_STR_LEN];
    char    name[VICTRON_NAME_MAX_LEN];
    victron_device_type_t device_type;
    int8_t  rssi;
    uint32_t last_update_ms;
    bool    data_valid;
    union {
        victron_solar_data_t   solar;
        victron_battery_data_t battery;
        /* victron_inverter_data_t inverter; */
        /* victron_dcdc_data_t     dcdc;     */
    };
} victron_device_t;

/* ---- AES function signature (user can provide their own) ---- */
typedef bool (*victron_aes_ctr_fn)(
    const uint8_t key[16], const uint8_t iv[16],
    const uint8_t* input, uint8_t* output, size_t len);

/* ---- Core API ---- */

/* Initialize the parser context. Provide AES implementation (NULL = use default mbedtls). */
void victron_init(victron_aes_ctr_fn aes_fn);

/* Register a device to monitor. hex_key is 32-char hex string. Returns device index or -1. */
int victron_add_device(const char* name, const char* mac_hex,
                       const char* hex_key, victron_device_type_t type);

/* Process a raw BLE manufacturer data buffer. Called from your BLE scan callback.
   Returns pointer to updated device, or NULL if not a monitored device. */
const victron_device_t* victron_process(const char* mac_hex, int8_t rssi,
                                         const uint8_t* mfg_data, size_t mfg_len,
                                         uint32_t timestamp_ms);

/* Get a device by index */
const victron_device_t* victron_get_device(int index);

/* Get device count */
int victron_get_device_count(void);

/* Optional callback — called when a device is updated */
typedef void (*victron_update_callback_t)(const victron_device_t* device);
void victron_set_callback(victron_update_callback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* VICTRON_BLE_PARSER_H */
```

### `victron_ble_parser.c` — Implementation Sketch

```c
#include "victron_ble_parser.h"
#include <string.h>

/* ---- Internal state ---- */
static victron_device_t s_devices[VICTRON_MAX_DEVICES];
static uint8_t s_keys[VICTRON_MAX_DEVICES][16];
static int s_device_count = 0;
static victron_aes_ctr_fn s_aes_fn = NULL;
static victron_update_callback_t s_callback = NULL;

/* ---- Default AES (mbedtls) ---- */
#ifdef VICTRON_USE_MBEDTLS  /* or auto-detect */
#include "mbedtls/aes.h"
static bool default_aes_ctr(const uint8_t key[16], const uint8_t iv[16],
                            const uint8_t* in, uint8_t* out, size_t len) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_enc(&aes, key, 128) != 0) {
        mbedtls_aes_free(&aes);
        return false;
    }
    size_t nc_off = 0;
    uint8_t nonce[16], stream[16];
    memcpy(nonce, iv, 16);
    memset(stream, 0, 16);
    int ret = mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce, stream, in, out);
    mbedtls_aes_free(&aes);
    return ret == 0;
}
#endif

void victron_init(victron_aes_ctr_fn aes_fn) {
    s_device_count = 0;
    memset(s_devices, 0, sizeof(s_devices));
    s_aes_fn = aes_fn;
#ifdef VICTRON_USE_MBEDTLS
    if (!s_aes_fn) s_aes_fn = default_aes_ctr;
#endif
}

/* hex_to_bytes, normalize_mac, parse_solar, parse_battery, etc. — all pure C */

const victron_device_t* victron_process(const char* mac_hex, int8_t rssi,
                                         const uint8_t* mfg_data, size_t mfg_len,
                                         uint32_t timestamp_ms) {
    /* 1. Check vendor ID */
    /* 2. Normalize MAC, find in s_devices[] */
    /* 3. Build IV from nonce, decrypt */
    /* 4. Parse based on record_type */
    /* 5. Update device struct, call callback */
    /* 6. Return pointer to device */
    return NULL; /* placeholder */
}
```

### `VictronBLE.h` — C++ Arduino Wrapper (thin)

```cpp
#ifndef VICTRON_BLE_H
#define VICTRON_BLE_H

#include <Arduino.h>
#include "victron_ble_parser.h"

#if defined(ESP32)
#include <BLEDevice.h>
#include <BLEScan.h>
#endif

class VictronBLE {
public:
    bool begin(uint32_t scanDuration = 5);
    bool addDevice(const char* name, const char* mac,
                   const char* key, victron_device_type_t type);
    void setCallback(victron_update_callback_t cb);
    void loop();
private:
#if defined(ESP32)
    BLEScan* scan = nullptr;
    uint32_t scanDuration = 5;
    static void onScanResult(BLEAdvertisedDevice dev);
#endif
};

#endif
```

### What Goes Where

| Content | File | Language |
|---|---|---|
| Packet structs (wire format) | `victron_ble_parser.h` | C |
| Device type / state enums | `victron_ble_parser.h` | C |
| Parsed data structs | `victron_ble_parser.h` | C |
| AES-CTR decryption | `victron_ble_parser.c` | C |
| Payload parsing (bit twiddling) | `victron_ble_parser.c` | C |
| Device registry | `victron_ble_parser.c` | C |
| Hex string conversion | `victron_ble_parser.c` | C |
| MAC normalization | `victron_ble_parser.c` | C |
| ESP32 BLE scanning | `VictronBLE.cpp` | C++ |
| Arduino convenience class | `VictronBLE.h/.cpp` | C++ |

### Migration Steps

1. Create `victron_ble_parser.h` with all C types and function declarations
2. Create `victron_ble_parser.c` — move parsing functions, convert String→char*, convert class methods→free functions
3. Slim down `VictronBLE.h` to just the ESP32 BLE scanning wrapper that calls the C API
4. Slim down `VictronBLE.cpp` to just `begin()`, `loop()`, and the scan callback glue
5. Update examples (minimal changes — API stays similar)
6. Test on ESP32 first, then try compiling the C core on a different target

### Estimated Code Sizes After Split

- `victron_ble_parser.h`: ~150 lines (types + API)
- `victron_ble_parser.c`: ~300 lines (all the protocol knowledge)
- `VictronBLE.h`: ~30 lines (ESP32 wrapper)
- `VictronBLE.cpp`: ~50 lines (ESP32 BLE glue)

vs. current: `VictronBLE.h` ~330 lines + `VictronBLE.cpp` ~640 lines = 970 lines total
After: ~530 lines total, with better separation of concerns

# VictronBLE

A portable library for reading Victron Energy device data via Bluetooth Low Energy (BLE) advertisements. Use it as an **Arduino** library on ESP32 and nRF52840, as a **Zephyr** module on any Bluetooth-capable board, or drop the **pure C99 core** into anything else.

v0.7 splits the library into a **dependency-free pure C99 core** (decode + decrypt, no BLE stack, no allocation, no I/O) plus thin platform layers: the Arduino C++ wrapper, and a **Zephyr module** with its own observer API. (v0.6 added multi-platform support for ESP32 + nRF52840; v0.5 brought the decoding accuracy fixes and AC charger support.) See [VERSIONS](VERSIONS) for full details. A stable **v1.0** release with a consistent, long-term API is coming soon.

---

Why another library? Most of the Victron BLE examples are built into other frameworks (e.g. ESPHome) or are locked to a single chip. The goal here is one library that works across ESP32 and nRF52 (and is easy to extend to more), usable standalone or inside ESPHome and other frameworks, with a long-term plan to move others onto it and improve the code with many eyes.

Under Arduino it supports **ESP32** (original, S and C series — tested on older ESP32, ESP32-S3 and ESP32-C3) and **nRF52840** (Adafruit/Seeed Bluefruit core, e.g. Seeed XIAO nRF52840). Under **Zephyr** it is board-agnostic — anything with a Bluetooth controller and the observer role (tested on nRF52840DK and RAK4631). All decoding and decryption is shared; only the BLE scanning layer is platform-specific, so other stacks can be added by implementing one more backend.

## Features

- ✅ **Multi-Platform**: ESP32 and nRF52840 under Arduino, any Bluetooth board under Zephyr
- ✅ **No External Dependencies**: Bundled AES-128-CTR — no mbedTLS or crypto library needed
- ✅ **Multiple Device Support**: Monitor multiple Victron devices simultaneously
- ✅ **All Device Types**: Solar chargers, battery monitors, inverters, DC-DC converters, AC chargers
- ✅ **Framework Friendly**: Arduino library, Zephyr module, or the bare C core
- ✅ **Clean API**: Simple, intuitive interface with callback support
- ✅ **No Pairing Required**: Reads BLE advertisement data directly
- ✅ **Low Power**: Uses passive BLE scanning
- ✅ **Full Data Access**: Battery voltage, current, SOC, power, alarms, and more
- ✅ **Production Ready**: Error handling, data validation, debug logging

## Supported Devices

- **Solar Chargers**: SmartSolar MPPT, BlueSolar MPPT (with BLE dongle)
- **Battery Monitors**: SmartShunt, BMV-712 Smart, BMV-700 series
- **Inverters**: MultiPlus, Quattro, Phoenix (with VE.Bus BLE dongle)
- **DC-DC Converters**: Orion Smart, Orion XS
- **AC Chargers**: Blue Smart IP22 / IP65 / IP67 chargers
- **Others**: Smart Battery Protect, Lynx Smart BMS, Smart Lithium batteries

Tested against real hardware: solar chargers (SmartSolar MPPT) and, since
November 2026, a battery monitor, decoded live over Zephyr on an ESP32-S3
(Heltec WiFi LoRa 32 V3) alongside two MPPTs. The shunt voltage agreed
with both chargers to within 0.03 V. That test ran on a full battery in
float, so the sign of the current has not yet been checked under charge or
discharge on real hardware (the host test vectors cover it). A battery monitor
sends "not available" values for fields it cannot report (SOC and consumed
Ah before it has synchronised, time-to-go while idle, aux with nothing
wired). The C core returns those as `NAN`, and time-to-go as
`VICTRONBLE_TTG_NA`; the Arduino wrapper returns 0.

## Hardware Requirements

- An ESP32 (original / S / C series) **or** an nRF52840 board (Adafruit/Seeed
  Bluefruit core — e.g. Seeed XIAO nRF52840) for the Arduino API
- Or any Zephyr-supported board with a Bluetooth controller (tested on
  nRF52840DK, RAK4631 and Heltec WiFi LoRa 32 V3)
- Victron devices with BLE "Instant Readout" enabled

Under Arduino the BLE backend is selected automatically at compile time from
the board's architecture — no code changes are needed to switch platforms.

## Installation

### PlatformIO

1. Add to `platformio.ini` (recommended — installs from the PlatformIO registry):
```ini
lib_deps =
    scottp/victronble
```

   Or install directly from git. Note the **`.git` suffix is required** — the bare
   repository URL is not accepted by PlatformIO:
```ini
lib_deps =
    https://gitea.sh3d.com.au/Sh3d/VictronBLE.git
```

2. Or clone into your project's `lib` folder:
```bash
cd lib
git clone https://gitea.sh3d.com.au/Sh3d/VictronBLE.git
```

#### nRF52840 board note

The nRF52 backend uses the **Bluefruit** library from the Adafruit/Seeed nRF52
core, so pick a board that uses that core. For the Seeed XIAO nRF52840, the
board files live in a community platform fork — use the `*_adafruit` variant
(the plain `xiaoble` variant uses the mbed core, which has no Bluefruit):
```ini
[env:xiao_nrf52840]
platform = https://github.com/maxgerhardt/platform-nordicnrf52
board = xiaoble_adafruit          ; XIAO nRF52840 Sense: xiaoblesense_adafruit
framework = arduino
lib_deps = scottp/victronble
```
The Adafruit Feather nRF52840 (`board = adafruit_feather_nrf52840`) works out of
the box with the stock PlatformIO `nordicnrf52` platform. The `MultiDevice`
example's `platformio.ini` includes ready-made ESP32 and nRF52 environments.

### Arduino IDE

1. Download or clone this repository
2. Move the `VictronBLE` folder to your Arduino libraries directory
3. Restart Arduino IDE

### Zephyr

The repository is a Zephyr module (`zephyr/module.yml`), so Zephyr finds it
automatically once it is in your workspace. Add it to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: sh3d
      url-base: https://gitea.sh3d.com.au/Sh3d
  projects:
    - name: VictronBLE
      remote: sh3d
      revision: main
      path: modules/lib/victronble
```

Then `west update`, and enable it in your `prj.conf`:

```
CONFIG_BT=y
CONFIG_BT_OBSERVER=y
CONFIG_VICTRONBLE=y
CONFIG_CBPRINTF_FP_SUPPORT=y   # only if you print the float fields
```

`CONFIG_VICTRONBLE` depends on `CONFIG_BT_OBSERVER`, and the application must
call `bt_enable()` before `victronble_start()` — the library scans, it does not
own the Bluetooth stack.

To build against a local checkout that is not in the manifest, point Zephyr at
it directly instead:

```sh
west build -b nrf52840dk/nrf52840 -d /tmp/build /path/to/app \
    -- -DZEPHYR_EXTRA_MODULES=/path/to/VictronBLE
```

#### Zephyr API

```c
#include "victronble_zephyr.h"

int  victronble_cb_register(struct victronble_cb *cb);
int  victronble_device_add(const bt_addr_le_t *addr, const uint8_t key[16]);
int  victronble_device_remove(const bt_addr_le_t *addr);
void victronble_watch_set(bool on);      /* log every advert, no keys needed */
int  victronble_start(void);
int  victronble_stop(void);
void victronble_get_stats(struct victronble_stats *out);
```

Records are decoded on a dedicated thread, not the Bluetooth RX thread, so
your `record` callback can log freely without stalling the controller.

#### Kconfig options

| Option | Default | Purpose |
|---|---|---|
| `VICTRONBLE_MAX_DEVICES` | 4 | Size of the monitored-device registry |
| `VICTRONBLE_QUEUE_DEPTH` | 8 | Adverts buffered between the RX and decode threads |
| `VICTRONBLE_THREAD_STACK_SIZE` | 2048 | Decode thread stack |
| `VICTRONBLE_THREAD_PRIORITY` | 10 | Decode thread priority (preemptible) |
| `VICTRONBLE_DEDUP` | y | Suppress repeated adverts by nonce |
| `VICTRONBLE_SCAN_INTERVAL` | 2048 | Scan interval, 0.625 ms units (1.28 s) |
| `VICTRONBLE_SCAN_WINDOW` | 18 | Scan window, 0.625 ms units (11.25 ms) |
| `VICTRONBLE_LOG_LEVEL` | — | Standard Zephyr per-module log level |

Working applications are in [`samples/`](samples/) — start with
[`samples/scan`](samples/scan/) to discover your devices, then
[`samples/observer`](samples/observer/) to read them.

## Quick Start

### 1. Get Your Encryption Keys

Use the VictronConnect app to get your device's encryption key:

1. Open VictronConnect
2. Connect to your device
3. Go to **Settings** → **Product Info**
4. Enable **"Instant readout via Bluetooth"**
5. Click **"Show"** next to **"Instant readout details"**
6. Copy the **encryption key** (32 hexadecimal characters)
7. Note your device's **MAC address**

### 2. Basic Example

```cpp
#include <Arduino.h>
#include "VictronBLE.h"

VictronBLE victron;

// Callback — receives a VictronDevice*, switch on deviceType
void onVictronData(const VictronDevice* dev) {
    if (dev->deviceType == DEVICE_TYPE_SOLAR_CHARGER) {
        Serial.printf("Solar %s: %.2fV %.2fA %dW\n",
            dev->name,
            dev->solar.batteryVoltage,
            dev->solar.batteryCurrent,
            (int)dev->solar.panelPower);
    }
}

void setup() {
    Serial.begin(115200);

    victron.begin(5); // 5 second scan duration
    victron.setCallback(onVictronData);

    // Add your device (replace with your MAC and key)
    victron.addDevice(
        "My MPPT",                          // Name
        "AA:BB:CC:DD:EE:FF",                // MAC address
        "0123456789abcdef0123456789abcdef", // Encryption key
        DEVICE_TYPE_SOLAR_CHARGER           // Device type (optional, auto-detected)
    );
}

void loop() {
    victron.loop(); // Non-blocking, returns immediately
}
```

## API Reference

### VictronBLE Class

#### Initialization

```cpp
bool begin(uint32_t scanDuration = 5);
```
Initialize BLE scanning. Returns `true` on success.

**Parameters:**
- `scanDuration`: BLE scan window in seconds (default: 5)

#### Device Management

```cpp
bool addDevice(const char* name, const char* mac, const char* hexKey,
               VictronDeviceType type = DEVICE_TYPE_UNKNOWN);
```
Add a device to monitor (max 8 devices).

**Parameters:**
- `name`: Friendly name for the device
- `mac`: Device MAC address (format: `"AA:BB:CC:DD:EE:FF"` or `"aabbccddeeff"`)
- `hexKey`: 32-character hex encryption key from VictronConnect
- `type`: Device type (optional, auto-detected from BLE advertisement)

**Returns:** `true` on success

```cpp
size_t getDeviceCount() const;
```
Get the number of configured devices.

#### Callback

```cpp
void setCallback(VictronCallback cb);
```
Set a function pointer callback. Called when new data arrives from a device. The callback receives a `const VictronDevice*` — switch on `deviceType` to access the appropriate data union member.

```cpp
typedef void (*VictronCallback)(const VictronDevice* device);
```

#### Configuration

```cpp
void setMinInterval(uint32_t ms);
```
Set minimum callback interval per device (default: 1000ms). Callbacks are also suppressed when the device nonce hasn't changed (data unchanged).

```cpp
void setDebug(bool enable);
```
Enable/disable debug output to Serial.

#### Main Loop

```cpp
void loop();
```
Call in your main loop. Non-blocking — returns immediately if a scan is already running. Scan restarts automatically when it completes.

### Data Structures

#### VictronDevice (main struct)

All device types share this struct. Access type-specific data via the union member matching `deviceType`.

```cpp
struct VictronDevice {
    char name[32];
    char mac[13];                   // 12 hex chars + null
    VictronDeviceType deviceType;
    int8_t rssi;                    // Signal strength (dBm)
    uint32_t lastUpdate;            // millis() of last update
    bool dataValid;
    union {
        VictronSolarData solar;
        VictronBatteryData battery;
        VictronInverterData inverter;
        VictronDCDCData dcdc;
    };
};
```

#### VictronSolarData

```cpp
struct VictronSolarData {
    uint8_t chargeState;            // SolarChargerState enum
    uint8_t errorCode;
    float batteryVoltage;           // V
    float batteryCurrent;           // A
    float panelPower;               // W
    uint16_t yieldToday;            // Wh
    float loadCurrent;              // A (if load output present)
};
```

**Charge States** (`chargeState` values):
`CHARGER_OFF`, `CHARGER_LOW_POWER`, `CHARGER_FAULT`, `CHARGER_BULK`, `CHARGER_ABSORPTION`, `CHARGER_FLOAT`, `CHARGER_STORAGE`, `CHARGER_EQUALIZE`, `CHARGER_INVERTING`, `CHARGER_POWER_SUPPLY`, `CHARGER_EXTERNAL_CONTROL`

#### VictronBatteryData

```cpp
struct VictronBatteryData {
    float voltage;                  // V
    float current;                  // A (+ charging, - discharging)
    float temperature;              // C (0 if aux is voltage)
    float auxVoltage;               // V (0 if aux is temperature)
    uint16_t remainingMinutes;
    float consumedAh;               // Ah
    float soc;                      // State of charge %
    bool alarmLowVoltage;
    bool alarmHighVoltage;
    bool alarmLowSOC;
    bool alarmLowTemperature;
    bool alarmHighTemperature;
};
```

#### VictronInverterData

```cpp
struct VictronInverterData {
    float batteryVoltage;           // V
    float batteryCurrent;           // A
    float acPower;                  // W (+ inverting, - charging)
    uint8_t state;
    bool alarmLowVoltage;
    bool alarmHighVoltage;
    bool alarmHighTemperature;
    bool alarmOverload;
};
```

#### VictronDCDCData

```cpp
struct VictronDCDCData {
    float inputVoltage;             // V
    float outputVoltage;            // V
    float outputCurrent;            // A
    uint8_t chargeState;
    uint8_t errorCode;
};
```

#### VictronACChargerData

```cpp
struct VictronACChargerData {
    uint8_t chargeState;            // SolarChargerState enum (shared charger states)
    uint8_t errorCode;
    float voltage1;                 // V (output 1)
    float current1;                 // A (output 1)
    float voltage2;                 // V (output 2, 0 if absent)
    float current2;                 // A (output 2, 0 if absent)
    float voltage3;                 // V (output 3, 0 if absent)
    float current3;                 // A (output 3, 0 if absent)
    float temperature;              // C (0 if not available)
    float acCurrent;                // A (0 if not available)
};
```

## Advanced Usage

### Multiple Devices

```cpp
void setup() {
    victron.begin(5);
    victron.setCallback(onVictronData);

    // Add multiple devices (type is auto-detected from BLE advertisements)
    victron.addDevice("MPPT 1", "AA:BB:CC:DD:EE:01", "key1...");
    victron.addDevice("MPPT 2", "AA:BB:CC:DD:EE:02", "key2...");
    victron.addDevice("SmartShunt", "AA:BB:CC:DD:EE:03", "key3...");
    victron.addDevice("Inverter", "AA:BB:CC:DD:EE:04", "key4...");
}
```

### Handling Multiple Device Types

```cpp
void onVictronData(const VictronDevice* dev) {
    switch (dev->deviceType) {
        case DEVICE_TYPE_SOLAR_CHARGER:
            Serial.printf("%s: %.2fV %dW\n", dev->name,
                dev->solar.batteryVoltage, (int)dev->solar.panelPower);
            break;
        case DEVICE_TYPE_BATTERY_MONITOR:
            Serial.printf("%s: %.2fV %.1f%%\n", dev->name,
                dev->battery.voltage, dev->battery.soc);
            break;
        case DEVICE_TYPE_INVERTER:
            Serial.printf("%s: %dW\n", dev->name, (int)dev->inverter.acPower);
            break;
        case DEVICE_TYPE_DCDC_CONVERTER:
            Serial.printf("%s: %.2fV -> %.2fV\n", dev->name,
                dev->dcdc.inputVoltage, dev->dcdc.outputVoltage);
            break;
        case DEVICE_TYPE_AC_CHARGER:
            Serial.printf("%s: %.2fV %.2fA %.0fC\n", dev->name,
                dev->acCharger.voltage1, dev->acCharger.current1,
                dev->acCharger.temperature);
            break;
        default:
            break;
    }
}
```

### Callback Throttling

```cpp
void setup() {
    victron.begin(5);
    victron.setCallback(onVictronData);
    victron.setMinInterval(2000); // Callback at most every 2 seconds per device

    // ...
}
```

## Troubleshooting

### No Data Received

1. **Check encryption key**: Must be exactly 32 hex characters from VictronConnect
2. **Verify MAC address**: Use correct format (AA:BB:CC:DD:EE:FF)
3. **Enable Instant Readout**: Must be enabled in VictronConnect settings
4. **Check range**: BLE range is typically 10-30 meters
5. **Disconnect VictronConnect**: App must be disconnected from device
6. **Enable debug**: `victron.setDebug(true);` to see detailed logs

On **Zephyr**, the stats line from `victronble_get_stats()` narrows this down
fast. If `adverts` climbs but `queued` and `decoded` stay at zero, the device
is being heard but never matched: check the Bluetooth address **type**, which
must be `random` for Victron devices, not `public`. Or run `samples/scan`,
which needs neither addresses nor keys.

### Decryption Failures

- Encryption key must match exactly
- Victron may have multiple keys per device; use the current one
- Keys are case-insensitive hex

### Poor Performance

- Reduce `scanDuration` for faster updates (minimum 1 second)
- Increase `scanDuration` for better reliability (5-10 seconds recommended)
- Ensure good signal strength (RSSI > -80 dBm)

## Protocol Details

This library implements the Victron BLE Advertising protocol:

- **Encryption**: AES-128-CTR
- **Update Rate**: ~1 Hz from Victron devices
- **No Pairing**: Reads broadcast advertisements
- **No Connection**: Extremely low power consumption

Based on official [Victron BLE documentation](https://www.victronenergy.com/live/vedirect_protocol:faq).

## Architecture & Portability

The library keeps everything platform-independent except the BLE radio:

```
include/
├── victronble_core.h       Pure C99 core API — decode one advert, no I/O
└── victronble_zephyr.h     Zephyr observer API
src/
├── victronble_core.c       Decrypt + parse; no BLE, no alloc, reentrant
├── crypto/vble_aes.{h,c}   Bundled AES-128-CTR (no external dependency)
├── VictronBLE.{h,cpp}      Arduino C++ wrapper over the core
├── esp32/                  ESP32 backend  — Bluedroid BLEScan
├── nrf52/                  nRF52 backend  — Bluefruit passive scan
└── victronble_zephyr.c     Zephyr backend — passive scan + decode thread
CMakeLists.txt, Kconfig     Zephyr module glue (ignored by PlatformIO)
```

- **A portable core.** `victronble_core.c` is C99 with no dependencies: no
  Arduino, no BLE stack, no allocation, no I/O, reentrant. Give it a
  manufacturer-data blob and a key, get a record back. Everything else —
  scanning, device registries, rate limiting, logging — belongs to the
  platform layers. `examples/NativeDecode` runs it on a PC.
- **One BLE HAL.** Each backend extracts the manufacturer data, MAC and RSSI
  from a scan result and hands it to the core. Under Arduino the correct
  backend is selected automatically at compile time from the board
  architecture (`ARDUINO_ARCH_ESP32` / `ARDUINO_ARCH_NRF52`) — there is
  nothing platform-specific in your sketch. Under Zephyr the backend is
  `victronble_zephyr.c`, selected by `CONFIG_VICTRONBLE`.
- **No external crypto.** AES-128-CTR is bundled (a trimmed, NIST-verified
  tiny-AES), so the library no longer depends on mbedTLS or any crypto library
  and builds identically on every target.
- **Adding a platform** means implementing one more backend (scan → extract →
  hand to the core); the rest is reused unchanged.

> Under Arduino the data callback runs in the BLE event context (the scan task
> on ESP32, the SoftDevice/Bluefruit handler on nRF52). Keep work in the
> callback light — copy what you need and process it from `loop()`.
> Under Zephyr this does not apply: records are delivered from the library's
> own decode thread, so callbacks may log and block.

## Examples

Arduino / PlatformIO, in [`examples/`](examples/):

- **MultiDevice**: Monitor multiple devices with callbacks. One sketch, multiple
  PlatformIO environments — builds for ESP32 (`esp32dev`, …) and nRF52840
  (`xiao_nrf52840`, `adafruit_feather_nrf52840`).
- **Logger**: Change-detection logging for Solar Charger data
- **Repeater**: Collect BLE data and re-transmit via ESPNow broadcast
- **Receiver**: Receive ESPNow packets from a Repeater and display data
- **FakeRepeater**: Generate test ESPNow packets without real Victron hardware

Zephyr, in [`samples/`](samples/):

- **scan**: List every Victron device advertising nearby. No keys needed —
  run this first to find your MAC addresses.
- **observer**: Monitor known devices and log every decoded field. The
  reference for the Zephyr API.

No hardware at all, in [`examples/`](examples/):

- **NativeDecode**: Decode an advertisement on your PC with plain `make`.
  Good for checking a key or a sniffer capture before you flash anything.

## Contributing

The primary repository is hosted on [Gitea](https://gitea.sh3d.com.au/Sh3d/VictronBLE),
with a mirror on **GitHub at <https://github.com/SH3D/VictronBLE>**. Since the Gitea
instance does not currently allow public sign-ups, please raise **issues and pull
requests on the GitHub mirror**.

Contributions welcome! Please:

1. Fork the [GitHub mirror](https://github.com/SH3D/VictronBLE)
2. Create a feature branch
3. Test thoroughly on real hardware
4. Submit a pull request

## Credits

This library was inspired by and builds upon excellent prior work:

- **[hoberman's Victron BLE Advertising examples](https://github.com/hoberman/Victron_BLE_Advertising_example)**: ESP32 examples demonstrating Victron BLE protocol implementation
- **[keshavdv's victron-ble Python library](https://github.com/keshavdv/victron-ble)**: Comprehensive Python implementation of Victron BLE protocol
- Protocol documentation and specifications by Victron Energy

## License

MIT License - see LICENSE file for details

Copyright (c) 2025 Scott Penrose <scottp@dd.com.au>

* https://www.sh3d.com.au/ - Sh3d
* https://www.dd.com.au/ - Digital Dimensions

## Disclaimer

This library is not officially supported by Victron Energy. Use at your own risk.

## Version History

See [VERSIONS](VERSIONS) file for detailed changelog and release history.

## Support

- 📫 Report issues on the [GitHub mirror](https://github.com/SH3D/VictronBLE/issues)
  (the Gitea instance does not currently allow public sign-ups). Bug reports, device
  decode problems and new device requests are all welcome — debug log output is very
  helpful.
- 📖 Check the examples directory
- 🔧 Enable debug mode for diagnostics
- 📚 See [Victron documentation](https://www.victronenergy.com/live/)

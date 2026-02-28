# VictronBLE

ESP32 library for reading Victron Energy device data via Bluetooth Low Energy (BLE) advertisements.

**⚠️ API CHANGE in v0.4 — not backwards compatible with v0.3.x**

v0.4 is a major rework of the library internals: new callback API, reduced memory usage, non-blocking scanning. See [VERSIONS](VERSIONS) for full details. A stable **v1.0** release with a consistent, long-term API is coming soon.

---

Why another library? Most of the Victron BLE examples are built into other frameworks (e.g. ESPHome) and I want a library that can be used in all ESP32 systems, including ESPHome or other frameworks. With long term plan to try and move others to this library and improve code with many eyes.

Currently supporting ESP32 S and C series (tested on older ESP32, ESP32-S3 and ESP32-C3). Other chipsets can be added with abstraction of Bluetooth code.

## Features

- ✅ **Multiple Device Support**: Monitor multiple Victron devices simultaneously
- ✅ **All Device Types**: Solar chargers, battery monitors, inverters, DC-DC converters
- ✅ **Framework Agnostic**: Works with Arduino and ESP-IDF
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
- **Others**: Smart Battery Protect, Lynx Smart BMS, Smart Lithium batteries

## Hardware Requirements

- ESP32, ESP32-S3, or ESP32-C3 board
- Victron devices with BLE "Instant Readout" enabled

## Installation

### PlatformIO

1. Add to `platformio.ini`:
```ini
lib_deps = 
    https://gitea.sh3d.com.au/Sh3d/VictronBLE
```

2. Or clone into your project's `lib` folder:
```bash
cd lib
git clone https://gitea.sh3d.com.au/Sh3d/VictronBLE
```

### Arduino IDE

1. Download or clone this repository
2. Move the `VictronBLE` folder to your Arduino libraries directory
3. Restart Arduino IDE

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

## Examples

See the `examples/` directory for:

- **MultiDevice**: Monitor multiple devices with callbacks
- **Logger**: Change-detection logging for Solar Charger data
- **Repeater**: Collect BLE data and re-transmit via ESPNow broadcast
- **Receiver**: Receive ESPNow packets from a Repeater and display data
- **FakeRepeater**: Generate test ESPNow packets without real Victron hardware

## Contributing

Contributions welcome! Please:

1. Fork the repository
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

- 📫 Report issues on GitHub
- 📖 Check the examples directory
- 🔧 Enable debug mode for diagnostics
- 📚 See [Victron documentation](https://www.victronenergy.com/live/)

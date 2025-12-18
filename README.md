# VictronBLE

ESP32 library for reading Victron Energy device data via Bluetooth Low Energy (BLE) advertisements.

Why another library? Most of the Victron BLE examples are built into other frameworks (e.g. ESPHome) and I want a library that can be used in all ESP32 systems, including ESPHome or other frameworks. With long term plan to try and move others to this library and improve code with many eyes.

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

// Callback for data updates
class MyCallback : public VictronDeviceCallback {
public:
    void onSolarChargerData(const SolarChargerData& data) override {
        Serial.printf("Solar: %.2fV, %.2fA, %dW\n", 
            data.batteryVoltage, 
            data.batteryCurrent,
            data.panelPower);
    }
};

MyCallback callback;

void setup() {
    Serial.begin(115200);
    
    // Initialize library
    victron.begin(5); // 5 second scan duration
    victron.setCallback(&callback);
    
    // Add your device (replace with your MAC and key)
    victron.addDevice(
        "My MPPT",                          // Name
        "AA:BB:CC:DD:EE:FF",                // MAC address
        "0123456789abcdef0123456789abcdef", // Encryption key
        DEVICE_TYPE_SOLAR_CHARGER           // Device type
    );
}

void loop() {
    victron.loop();
    delay(100);
}
```

## API Reference

### VictronBLE Class

#### Initialization

```cpp
bool begin(uint32_t scanDuration = 5);
```
Initialize BLE and start scanning. Returns `true` on success.

**Parameters:**
- `scanDuration`: BLE scan duration in seconds (default: 5)

#### Device Management

```cpp
bool addDevice(String name, String macAddress, String encryptionKey, 
               VictronDeviceType expectedType = DEVICE_TYPE_UNKNOWN);
```
Add a device to monitor.

**Parameters:**
- `name`: Friendly name for the device
- `macAddress`: Device MAC address (format: "AA:BB:CC:DD:EE:FF")
- `encryptionKey`: 32-character hex encryption key from VictronConnect
- `expectedType`: Device type (optional, for validation)

**Returns:** `true` on success

```cpp
void removeDevice(String macAddress);
```
Remove a device from monitoring.

```cpp
size_t getDeviceCount();
```
Get the number of configured devices.

#### Data Access

```cpp
bool getSolarChargerData(String macAddress, SolarChargerData& data);
bool getBatteryMonitorData(String macAddress, BatteryMonitorData& data);
bool getInverterData(String macAddress, InverterData& data);
bool getDCDCConverterData(String macAddress, DCDCConverterData& data);
```
Get latest data for a specific device. Returns `true` if data is valid.

```cpp
std::vector<String> getDevicesByType(VictronDeviceType type);
```
Get MAC addresses of all devices of a specific type.

#### Callbacks

```cpp
void setCallback(VictronDeviceCallback* callback);
```
Set callback object to receive data updates automatically.

#### Utilities

```cpp
void setDebug(bool enable);
```
Enable/disable debug output to Serial.

```cpp
String getLastError();
```
Get last error message.

```cpp
void loop();
```
Process BLE scanning and data updates. Call this in your main loop.

### Data Structures

#### SolarChargerData

```cpp
struct SolarChargerData {
    String deviceName;
    String macAddress;
    int8_t rssi;                    // Signal strength (dBm)
    uint32_t lastUpdate;            // millis() of last update
    bool dataValid;                 // Data validity flag
    
    SolarChargerState chargeState;  // Charging state
    float batteryVoltage;           // V
    float batteryCurrent;           // A
    float panelVoltage;             // V (calculated)
    float panelPower;               // W
    uint16_t yieldToday;            // Wh
    float loadCurrent;              // A (if load output present)
};
```

**Charge States:**
- `CHARGER_OFF` - Off
- `CHARGER_LOW_POWER` - Low power
- `CHARGER_FAULT` - Fault
- `CHARGER_BULK` - Bulk charging
- `CHARGER_ABSORPTION` - Absorption
- `CHARGER_FLOAT` - Float
- `CHARGER_STORAGE` - Storage mode
- `CHARGER_EQUALIZE` - Equalize
- `CHARGER_INVERTING` - Inverting (HUB-4)
- `CHARGER_POWER_SUPPLY` - Power supply mode
- `CHARGER_EXTERNAL_CONTROL` - External control

#### BatteryMonitorData

```cpp
struct BatteryMonitorData {
    String deviceName;
    String macAddress;
    int8_t rssi;
    uint32_t lastUpdate;
    bool dataValid;
    
    float voltage;                  // V
    float current;                  // A (+ charging, - discharging)
    float temperature;              // °C (if configured)
    float auxVoltage;               // V (starter battery/midpoint)
    uint16_t remainingMinutes;      // Time remaining
    float consumedAh;               // Ah consumed
    float soc;                      // State of charge %
    
    // Alarms
    bool alarmLowVoltage;
    bool alarmHighVoltage;
    bool alarmLowSOC;
    bool alarmLowTemperature;
    bool alarmHighTemperature;
};
```

#### InverterData

```cpp
struct InverterData {
    String deviceName;
    String macAddress;
    int8_t rssi;
    uint32_t lastUpdate;
    bool dataValid;
    
    float batteryVoltage;           // V
    float batteryCurrent;           // A
    float acPower;                  // W (+ inverting, - charging)
    uint8_t state;                  // Inverter state
    
    // Alarms
    bool alarmHighVoltage;
    bool alarmLowVoltage;
    bool alarmHighTemperature;
    bool alarmOverload;
};
```

#### DCDCConverterData

```cpp
struct DCDCConverterData {
    String deviceName;
    String macAddress;
    int8_t rssi;
    uint32_t lastUpdate;
    bool dataValid;
    
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
    victron.setCallback(&callback);
    
    // Add multiple devices
    victron.addDevice("MPPT 1", "AA:BB:CC:DD:EE:01", "key1...", DEVICE_TYPE_SOLAR_CHARGER);
    victron.addDevice("MPPT 2", "AA:BB:CC:DD:EE:02", "key2...", DEVICE_TYPE_SOLAR_CHARGER);
    victron.addDevice("SmartShunt", "AA:BB:CC:DD:EE:03", "key3...", DEVICE_TYPE_BATTERY_MONITOR);
    victron.addDevice("Inverter", "AA:BB:CC:DD:EE:04", "key4...", DEVICE_TYPE_INVERTER);
}
```

### Manual Data Polling

```cpp
void loop() {
    victron.loop();
    
    // Query specific device
    SolarChargerData mpptData;
    if (victron.getSolarChargerData("AA:BB:CC:DD:EE:FF", mpptData)) {
        if (mpptData.dataValid) {
            // Use data
            float power = mpptData.panelPower;
        }
    }
    
    delay(1000);
}
```

### Find All Devices of a Type

```cpp
void loop() {
    victron.loop();
    
    // Get all solar chargers
    std::vector<String> mppts = victron.getDevicesByType(DEVICE_TYPE_SOLAR_CHARGER);
    
    for (const String& mac : mppts) {
        SolarChargerData data;
        if (victron.getSolarChargerData(mac, data)) {
            Serial.println(data.deviceName + ": " + String(data.panelPower) + "W");
        }
    }
    
    delay(5000);
}
```

### Callback Interface

Implement `VictronDeviceCallback` to receive automatic updates:

```cpp
class MyCallback : public VictronDeviceCallback {
public:
    void onSolarChargerData(const SolarChargerData& data) override {
        // Handle solar charger update
    }
    
    void onBatteryMonitorData(const BatteryMonitorData& data) override {
        // Handle battery monitor update
    }
    
    void onInverterData(const InverterData& data) override {
        // Handle inverter update
    }
    
    void onDCDCConverterData(const DCDCConverterData& data) override {
        // Handle DC-DC converter update
    }
};
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
- More examples coming soon!

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

## Support

- 📫 Report issues on GitHub
- 📖 Check the examples directory
- 🔧 Enable debug mode for diagnostics
- 📚 See [Victron documentation](https://www.victronenergy.com/live/)

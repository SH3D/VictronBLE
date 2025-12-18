# VictronBLE Quick Start Guide

## Getting Started in 5 Minutes

### Step 1: Get Your Device Encryption Keys

1. Install **VictronConnect** app on your phone/computer
2. Connect to your Victron device via Bluetooth
3. Navigate to: **Settings** → **Product Info**
4. Enable **"Instant readout via Bluetooth"** (if not already enabled)
5. Click **"Show"** next to **"Instant readout details"**
6. Copy the **Encryption Key** (32 hex characters like: `0df4d0395b7d1a876c0c33ecb9e70dcd`)
7. Note your device's **MAC address** (format: `AA:BB:CC:DD:EE:FF`)

**Important:** Do this for EACH device you want to monitor.

### Step 2: Install the Library

#### Option A: PlatformIO (Recommended)

1. Copy the `VictronBLE` folder to your project's `lib/` directory
2. Your project structure should look like:
```
your-project/
├── lib/
│   └── VictronBLE/
│       ├── src/
│       ├── examples/
│       ├── library.json
│       └── README.md
├── src/
│   └── main.cpp
└── platformio.ini
```

#### Option B: Arduino IDE

1. Copy the `VictronBLE` folder to your Arduino `libraries` directory:
   - Windows: `Documents\Arduino\libraries\`
   - Mac: `~/Documents/Arduino/libraries/`
   - Linux: `~/Arduino/libraries/`
2. Restart Arduino IDE

### Step 3: Update the Example Code

Open `examples/MultiDevice/main.cpp` and update these lines with YOUR device information:

```cpp
// Replace these with YOUR actual device details:

victron.addDevice(
    "MPPT 100/30",                          // Give it a friendly name
    "E7:48:D4:28:B7:9C",                    // YOUR device MAC address
    "0df4d0395b7d1a876c0c33ecb9e70dcd",     // YOUR encryption key
    DEVICE_TYPE_SOLAR_CHARGER                // Device type
);
```

#### For Multiple Devices:

```cpp
// Solar Charger #1
victron.addDevice("MPPT 1", "AA:BB:CC:DD:EE:01", "key1here", DEVICE_TYPE_SOLAR_CHARGER);

// Solar Charger #2  
victron.addDevice("MPPT 2", "AA:BB:CC:DD:EE:02", "key2here", DEVICE_TYPE_SOLAR_CHARGER);

// Battery Monitor
victron.addDevice("SmartShunt", "AA:BB:CC:DD:EE:03", "key3here", DEVICE_TYPE_BATTERY_MONITOR);

// Inverter/Charger
victron.addDevice("MultiPlus", "AA:BB:CC:DD:EE:04", "key4here", DEVICE_TYPE_INVERTER);
```

### Step 4: Build and Upload

#### PlatformIO:
```bash
cd examples/MultiDevice
pio run -t upload && pio device monitor
```

#### Arduino IDE:
1. Open `examples/MultiDevice/main.cpp` as an .ino file
2. Select your ESP32 board from Tools → Board
3. Select your COM port from Tools → Port
4. Click Upload
5. Open Serial Monitor at 115200 baud

### Step 5: Watch the Data!

You should see output like:

```
=== Solar Charger: MPPT 100/30 ===
MAC: e7:48:d4:28:b7:9c
RSSI: -65 dBm
State: Bulk
Battery: 13.45 V
Current: 12.50 A
Panel Voltage: 18.2 V
Panel Power: 168 W
Yield Today: 1250 Wh

=== Battery Monitor: SmartShunt ===
MAC: 11:22:33:44:55:66
RSSI: -58 dBm
Voltage: 13.45 V
Current: 12.50 A
SOC: 87.5 %
Consumed: 2.34 Ah
Time Remaining: 6h 45m
```

## Troubleshooting

### "No data received"

1. **Make sure VictronConnect app is CLOSED/DISCONNECTED**
   - The app prevents BLE advertisements when connected
   - Close the app completely on all devices

2. **Verify encryption key is correct**
   - Must be exactly 32 hex characters
   - Copy/paste from VictronConnect to avoid typos

3. **Check MAC address format**
   - Must use colons: `AA:BB:CC:DD:EE:FF`
   - Case doesn't matter

4. **Enable debug mode to see what's happening:**
   ```cpp
   victron.setDebug(true);
   ```

5. **Check distance**
   - BLE range is typically 10-30 meters
   - Move ESP32 closer to Victron device

### "Decryption failed"

- Encryption key must match EXACTLY
- Get the key again from VictronConnect
- Make sure you're using the CURRENT key (not an old one)

### "Device not found"

- Verify MAC address is correct
- Check that "Instant Readout" is enabled in VictronConnect
- Make sure device has Bluetooth (older models may need BLE dongle)

## Next Steps

### Customize the Output

Edit the callback functions in your code to change what data is displayed:

```cpp
class MyCallback : public VictronDeviceCallback {
public:
    void onSolarChargerData(const SolarChargerData& data) override {
        // Display only what you want:
        Serial.printf("%s: %dW @ %.1fV\n", 
            data.deviceName.c_str(),
            data.panelPower,
            data.batteryVoltage);
    }
};
```

### Add Your Own Logic

```cpp
void onSolarChargerData(const SolarChargerData& data) override {
    // Turn on relay if producing power
    if (data.panelPower > 100) {
        digitalWrite(RELAY_PIN, HIGH);
    }
    
    // Log to SD card
    logToSDCard(data);
    
    // Send to MQTT
    publishToMQTT(data);
    
    // Update display
    updateLCD(data);
}
```

### Integration Ideas

- **Web Dashboard**: Serve data over WiFi
- **MQTT**: Publish to Home Assistant, Node-RED
- **Display**: Show on OLED/TFT screen
- **Data Logging**: Log to SD card or cloud
- **Automation**: Control loads based on solar power
- **Alarms**: Send notifications on low battery

## Device Type Reference

| Device Type | Constant | Examples |
|------------|----------|----------|
| Solar Charger | `DEVICE_TYPE_SOLAR_CHARGER` | SmartSolar MPPT, BlueSolar MPPT |
| Battery Monitor | `DEVICE_TYPE_BATTERY_MONITOR` | SmartShunt, BMV-712, BMV-700 |
| Inverter | `DEVICE_TYPE_INVERTER` | MultiPlus, Quattro, Phoenix |
| DC-DC Converter | `DEVICE_TYPE_DCDC_CONVERTER` | Orion Smart, Orion XS |

## Common Use Cases

### Home Solar Monitoring
```cpp
// Monitor solar production and battery
victron.addDevice("Solar", "MAC1", "KEY1", DEVICE_TYPE_SOLAR_CHARGER);
victron.addDevice("Battery", "MAC2", "KEY2", DEVICE_TYPE_BATTERY_MONITOR);
```

### RV/Van Setup
```cpp
// Monitor multiple solar panels and battery bank
victron.addDevice("Roof MPPT", "MAC1", "KEY1", DEVICE_TYPE_SOLAR_CHARGER);
victron.addDevice("Portable MPPT", "MAC2", "KEY2", DEVICE_TYPE_SOLAR_CHARGER);
victron.addDevice("House Battery", "MAC3", "KEY3", DEVICE_TYPE_BATTERY_MONITOR);
victron.addDevice("Starter Battery", "MAC4", "KEY4", DEVICE_TYPE_DCDC_CONVERTER);
```

### Boat Power System
```cpp
// Complete boat power monitoring
victron.addDevice("Solar", "MAC1", "KEY1", DEVICE_TYPE_SOLAR_CHARGER);
victron.addDevice("Battery Bank", "MAC2", "KEY2", DEVICE_TYPE_BATTERY_MONITOR);
victron.addDevice("Inverter", "MAC3", "KEY3", DEVICE_TYPE_INVERTER);
```

## Performance Tips

### Scan Duration
```cpp
victron.begin(5);  // 5 seconds - balanced (default)
victron.begin(1);  // 1 second - faster updates, may miss devices
victron.begin(10); // 10 seconds - slower but very reliable
```

### Power Consumption
- Passive BLE scanning uses minimal power (~10-20mA)
- No WiFi needed (unless you add it yourself)
- Perfect for battery-powered projects

### Update Rate
- Victron devices broadcast every ~1 second
- Your ESP32 will receive updates based on scan duration
- Data is cached between scans

## Support

- 📖 Read the full README.md
- 🐛 Report issues on GitHub
- 💬 Check existing GitHub issues
- 🔧 Enable debug mode: `victron.setDebug(true)`

## Example Project Layout

```
my-victron-monitor/
├── lib/
│   └── VictronBLE/          # Copy the whole VictronBLE folder here
├── src/
│   └── main.cpp             # Your code (based on example)
└── platformio.ini           # PlatformIO config

# Or in Arduino:
Arduino/libraries/VictronBLE/  # Copy VictronBLE folder here
my-victron-monitor/
└── my-victron-monitor.ino    # Your code
```

Happy monitoring! 🔋⚡

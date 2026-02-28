# Upgrading to VictronBLE v0.4

v0.4 is a breaking API change that simplifies the library significantly.

## Summary of Changes

- **Callback**: Virtual class → function pointer
- **Data access**: Inheritance → tagged union (`VictronDevice` with `solar`, `battery`, `inverter`, `dcdc` members)
- **Strings**: Arduino `String` → fixed `char[]` arrays
- **Memory**: `std::map` + heap allocation → fixed array, zero dynamic allocation
- **Removed**: `getLastError()`, `removeDevice()`, `getDevicesByType()`, per-type getter methods, `VictronDeviceConfig` struct, `VictronDeviceCallback` class
- **Removed field**: `panelVoltage` (was unreliably derived from `panelPower / batteryCurrent`)

## Migration Guide

### 1. Callback: class → function pointer

**Before (v0.3):**
```cpp
class MyCallback : public VictronDeviceCallback {
    void onSolarChargerData(const SolarChargerData& data) override {
        Serial.println(data.deviceName + ": " + String(data.panelPower) + "W");
    }
    void onBatteryMonitorData(const BatteryMonitorData& data) override {
        Serial.println("SOC: " + String(data.soc) + "%");
    }
};
MyCallback callback;
victron.setCallback(&callback);
```

**After (v0.4):**
```cpp
void onVictronData(const VictronDevice* dev) {
    switch (dev->deviceType) {
        case DEVICE_TYPE_SOLAR_CHARGER:
            Serial.printf("%s: %.0fW\n", dev->name, dev->solar.panelPower);
            break;
        case DEVICE_TYPE_BATTERY_MONITOR:
            Serial.printf("SOC: %.1f%%\n", dev->battery.soc);
            break;
    }
}
victron.setCallback(onVictronData);
```

### 2. Data field access

Fields moved from flat `SolarChargerData` etc. into the `VictronDevice` tagged union:

| Old (v0.3) | New (v0.4) |
|---|---|
| `data.deviceName` | `dev->name` (char[32]) |
| `data.macAddress` | `dev->mac` (char[13]) |
| `data.rssi` | `dev->rssi` |
| `data.lastUpdate` | `dev->lastUpdate` |
| `data.batteryVoltage` | `dev->solar.batteryVoltage` |
| `data.batteryCurrent` | `dev->solar.batteryCurrent` |
| `data.panelPower` | `dev->solar.panelPower` |
| `data.yieldToday` | `dev->solar.yieldToday` |
| `data.loadCurrent` | `dev->solar.loadCurrent` |
| `data.chargeState` | `dev->solar.chargeState` (uint8_t, was enum) |
| `data.panelVoltage` | **Removed** - see below |

### 3. panelVoltage removed

`panelVoltage` was a derived value (`panelPower / batteryCurrent`) that was unreliable (division by zero when no current, inaccurate due to MPPT conversion). It has been removed.

If you need an estimate:
```cpp
float panelVoltage = (dev->solar.batteryCurrent > 0.1f)
    ? dev->solar.panelPower / dev->solar.batteryCurrent
    : 0.0f;
```

### 4. getLastError() removed

Debug output now goes directly to Serial when `setDebug(true)` is enabled. Remove any `getLastError()` calls.

**Before:**
```cpp
if (!victron.begin(2)) {
    Serial.println(victron.getLastError());
}
```

**After:**
```cpp
if (!victron.begin(2)) {
    Serial.println("Failed to initialize VictronBLE!");
}
```

### 5. String types

Device name and MAC are now `char[]` instead of Arduino `String`. Use `Serial.printf()` or `String(dev->name)` if you need a String object.

### 6. addDevice() parameters

Parameters changed from `String` to `const char*`. Existing string literals work unchanged. `VictronDeviceConfig` struct is no longer needed.

```cpp
// Both v0.3 and v0.4 - string literals work the same
victron.addDevice("MySolar", "f69dfcce55eb",
    "bf25c098c156afd6a180157b8a3ab1fb", DEVICE_TYPE_SOLAR_CHARGER);
```

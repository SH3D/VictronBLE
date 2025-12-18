/**
 * VictronBLE - ESP32 library for Victron Energy BLE devices
 * 
 * Based on Victron's official BLE Advertising protocol documentation
 * Inspired by hoberman's examples and keshavdv's Python library
 * 
 * Copyright (c) 2025 Scott Penrose
 * License: MIT
 */

#ifndef VICTRON_BLE_H
#define VICTRON_BLE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>
#include <BLEScan.h>
#include <map>
#include <vector>
#include "mbedtls/aes.h"

// Victron manufacturer ID
#define VICTRON_MANUFACTURER_ID 0x02E1

// Device type IDs from Victron protocol
enum VictronDeviceType {
    DEVICE_TYPE_UNKNOWN = 0x00,
    DEVICE_TYPE_SOLAR_CHARGER = 0x01,
    DEVICE_TYPE_BATTERY_MONITOR = 0x02,
    DEVICE_TYPE_INVERTER = 0x03,
    DEVICE_TYPE_DCDC_CONVERTER = 0x04,
    DEVICE_TYPE_SMART_LITHIUM = 0x05,
    DEVICE_TYPE_INVERTER_RS = 0x06,
    DEVICE_TYPE_SMART_BATTERY_PROTECT = 0x07,
    DEVICE_TYPE_LYNX_SMART_BMS = 0x08,
    DEVICE_TYPE_MULTI_RS = 0x09,
    DEVICE_TYPE_VE_BUS = 0x0A,
    DEVICE_TYPE_DC_ENERGY_METER = 0x0B
};

// Device state for Solar Charger
enum SolarChargerState {
    CHARGER_OFF = 0,
    CHARGER_LOW_POWER = 1,
    CHARGER_FAULT = 2,
    CHARGER_BULK = 3,
    CHARGER_ABSORPTION = 4,
    CHARGER_FLOAT = 5,
    CHARGER_STORAGE = 6,
    CHARGER_EQUALIZE = 7,
    CHARGER_INVERTING = 9,
    CHARGER_POWER_SUPPLY = 11,
    CHARGER_EXTERNAL_CONTROL = 252
};

// Base structure for all device data
struct VictronDeviceData {
    String deviceName;
    String macAddress;
    VictronDeviceType deviceType;
    int8_t rssi;
    uint32_t lastUpdate;
    bool dataValid;
    
    VictronDeviceData() : deviceType(DEVICE_TYPE_UNKNOWN), rssi(-100), 
                          lastUpdate(0), dataValid(false) {}
};

// Solar Charger specific data
struct SolarChargerData : public VictronDeviceData {
    SolarChargerState chargeState;
    float batteryVoltage;      // V
    float batteryCurrent;      // A
    float panelVoltage;        // V (PV voltage)
    float panelPower;          // W
    uint16_t yieldToday;       // Wh
    float loadCurrent;         // A
    
    SolarChargerData() : chargeState(CHARGER_OFF), batteryVoltage(0), 
                         batteryCurrent(0), panelVoltage(0), panelPower(0),
                         yieldToday(0), loadCurrent(0) {
        deviceType = DEVICE_TYPE_SOLAR_CHARGER;
    }
};

// Battery Monitor/SmartShunt specific data
struct BatteryMonitorData : public VictronDeviceData {
    float voltage;             // V
    float current;             // A (positive = charging, negative = discharging)
    float temperature;         // °C
    float auxVoltage;          // V (starter battery or midpoint)
    uint16_t remainingMinutes; // Minutes
    float consumedAh;          // Ah
    float soc;                 // State of Charge %
    bool alarmLowVoltage;
    bool alarmHighVoltage;
    bool alarmLowSOC;
    bool alarmLowTemperature;
    bool alarmHighTemperature;
    
    BatteryMonitorData() : voltage(0), current(0), temperature(0), 
                           auxVoltage(0), remainingMinutes(0), consumedAh(0),
                           soc(0), alarmLowVoltage(false), alarmHighVoltage(false),
                           alarmLowSOC(false), alarmLowTemperature(false),
                           alarmHighTemperature(false) {
        deviceType = DEVICE_TYPE_BATTERY_MONITOR;
    }
};

// Inverter specific data
struct InverterData : public VictronDeviceData {
    float batteryVoltage;      // V
    float batteryCurrent;      // A
    float acPower;             // W
    uint8_t state;             // Inverter state
    bool alarmHighVoltage;
    bool alarmLowVoltage;
    bool alarmHighTemperature;
    bool alarmOverload;
    
    InverterData() : batteryVoltage(0), batteryCurrent(0), acPower(0),
                     state(0), alarmHighVoltage(false), alarmLowVoltage(false),
                     alarmHighTemperature(false), alarmOverload(false) {
        deviceType = DEVICE_TYPE_INVERTER;
    }
};

// DC-DC Converter specific data
struct DCDCConverterData : public VictronDeviceData {
    float inputVoltage;        // V
    float outputVoltage;       // V
    float outputCurrent;       // A
    uint8_t chargeState;
    uint8_t errorCode;
    
    DCDCConverterData() : inputVoltage(0), outputVoltage(0), outputCurrent(0),
                          chargeState(0), errorCode(0) {
        deviceType = DEVICE_TYPE_DCDC_CONVERTER;
    }
};

// Forward declaration
class VictronBLE;

// Callback interface for device data updates
class VictronDeviceCallback {
public:
    virtual ~VictronDeviceCallback() {}
    virtual void onSolarChargerData(const SolarChargerData& data) {}
    virtual void onBatteryMonitorData(const BatteryMonitorData& data) {}
    virtual void onInverterData(const InverterData& data) {}
    virtual void onDCDCConverterData(const DCDCConverterData& data) {}
};

// Device configuration structure
struct VictronDeviceConfig {
    String name;
    String macAddress;
    String encryptionKey;  // 32 character hex string
    VictronDeviceType expectedType;
    
    VictronDeviceConfig() : expectedType(DEVICE_TYPE_UNKNOWN) {}
    VictronDeviceConfig(String n, String mac, String key, VictronDeviceType type = DEVICE_TYPE_UNKNOWN)
        : name(n), macAddress(mac), encryptionKey(key), expectedType(type) {}
};

// Main VictronBLE class
class VictronBLE {
public:
    VictronBLE();
    ~VictronBLE();
    
    // Initialize BLE and start scanning
    bool begin(uint32_t scanDuration = 5);
    
    // Add a device to monitor
    bool addDevice(const VictronDeviceConfig& config);
    bool addDevice(String name, String macAddress, String encryptionKey, 
                   VictronDeviceType expectedType = DEVICE_TYPE_UNKNOWN);
    
    // Remove a device
    void removeDevice(String macAddress);
    
    // Get device count
    size_t getDeviceCount() const { return devices.size(); }
    
    // Set callback for data updates
    void setCallback(VictronDeviceCallback* cb) { callback = cb; }
    
    // Process scanning (call in loop())
    void loop();
    
    // Get latest data for a device
    bool getSolarChargerData(String macAddress, SolarChargerData& data);
    bool getBatteryMonitorData(String macAddress, BatteryMonitorData& data);
    bool getInverterData(String macAddress, InverterData& data);
    bool getDCDCConverterData(String macAddress, DCDCConverterData& data);
    
    // Get all devices of a specific type
    std::vector<String> getDevicesByType(VictronDeviceType type);
    
    // Enable/disable debug output
    void setDebug(bool enable) { debugEnabled = enable; }
    
    // Get last error message
    String getLastError() const { return lastError; }
    
private:
    friend class VictronBLEAdvertisedDeviceCallbacks;
    
    struct DeviceInfo {
        VictronDeviceConfig config;
        VictronDeviceData* data;
        uint8_t encryptionKeyBytes[16];
        
        DeviceInfo() : data(nullptr) {
            memset(encryptionKeyBytes, 0, 16);
        }
        ~DeviceInfo() {
            if (data) delete data;
        }
    };
    
    std::map<String, DeviceInfo*> devices;
    BLEScan* pBLEScan;
    VictronDeviceCallback* callback;
    bool debugEnabled;
    String lastError;
    uint32_t scanDuration;
    bool initialized;
    
    // Internal methods
    bool hexStringToBytes(const String& hex, uint8_t* bytes, size_t len);
    bool decryptAdvertisement(const uint8_t* encrypted, size_t encLen, 
                              const uint8_t* key, const uint8_t* iv,
                              uint8_t* decrypted);
    bool parseAdvertisement(const uint8_t* manufacturerData, size_t len,
                            const String& macAddress);
    void processDevice(BLEAdvertisedDevice advertisedDevice);
    
    VictronDeviceData* createDeviceData(VictronDeviceType type);
    bool parseSolarCharger(const uint8_t* data, size_t len, SolarChargerData& result);
    bool parseBatteryMonitor(const uint8_t* data, size_t len, BatteryMonitorData& result);
    bool parseInverter(const uint8_t* data, size_t len, InverterData& result);
    bool parseDCDCConverter(const uint8_t* data, size_t len, DCDCConverterData& result);
    
    void debugPrint(const String& message);
    void debugPrintHex(const char* label, const uint8_t* data, size_t len);
    
    String macAddressToString(BLEAddress address);
    String normalizeMAC(String mac);
};

// BLE scan callback class
class VictronBLEAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
public:
    VictronBLEAdvertisedDeviceCallbacks(VictronBLE* parent) : victronBLE(parent) {}
    void onResult(BLEAdvertisedDevice advertisedDevice) override;
    
private:
    VictronBLE* victronBLE;
};

#endif // VICTRON_BLE_H

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
#include "mbedtls/aes.h"

// --- Constants ---
static constexpr uint16_t VICTRON_MANUFACTURER_ID = 0x02E1;
static constexpr int VICTRON_MAX_DEVICES = 8;
static constexpr int VICTRON_MAC_LEN = 13;     // 12 hex chars + null
static constexpr int VICTRON_NAME_LEN = 32;
static constexpr int VICTRON_ENCRYPTED_LEN = 21;

// --- Device type IDs from Victron protocol ---
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

// --- Device state for Solar Charger ---
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

// ============================================================
// Wire-format packed structures for decoding BLE advertisements
// ============================================================

struct victronManufacturerData {
    uint16_t vendorID;
    uint8_t beaconType;                // 0x10 = Product Advertisement
    uint8_t unknownData1[3];
    uint8_t victronRecordType;         // Device type (see VictronDeviceType)
    uint16_t nonceDataCounter;
    uint8_t encryptKeyMatch;           // Should match encryption key byte 0
    uint8_t victronEncryptedData[VICTRON_ENCRYPTED_LEN];
} __attribute__((packed));

struct victronSolarChargerPayload {
    uint8_t deviceState;
    uint8_t errorCode;
    int16_t batteryVoltage;            // 10mV units
    int16_t batteryCurrent;            // 10mA units (signed)
    uint16_t yieldToday;               // 10Wh units
    uint16_t inputPower;               // 1W units
    uint16_t loadCurrent;              // 10mA units (0xFFFF = no load)
    uint8_t reserved[2];
} __attribute__((packed));

struct victronBatteryMonitorPayload {
    uint16_t remainingMins;
    uint16_t batteryVoltage;           // 10mV units
    uint8_t alarms;
    uint16_t auxData;                  // 10mV (voltage) or 0.01K (temperature)
    uint8_t currentLow;
    uint8_t currentMid;
    uint8_t currentHigh_consumedLow;   // Current bits 16-21 (low 6), consumed bits 0-1 (high 2)
    uint8_t consumedMid;
    uint8_t consumedHigh;
    uint16_t soc;                      // 0.1% units (10-bit)
    uint8_t reserved[2];
} __attribute__((packed));

struct victronInverterPayload {
    uint8_t deviceState;
    uint8_t errorCode;
    uint16_t batteryVoltage;           // 10mV units
    int16_t batteryCurrent;            // 10mA units (signed)
    uint8_t acPowerLow;
    uint8_t acPowerMid;
    uint8_t acPowerHigh;               // Signed 24-bit
    uint8_t alarms;
    uint8_t reserved[4];
} __attribute__((packed));

struct victronDCDCConverterPayload {
    uint8_t chargeState;
    uint8_t errorCode;
    uint16_t inputVoltage;             // 10mV units
    uint16_t outputVoltage;            // 10mV units
    uint16_t outputCurrent;            // 10mA units
    uint8_t reserved[6];
} __attribute__((packed));

// ============================================================
// Parsed data structures (flat, no inheritance)
// ============================================================

struct VictronSolarData {
    uint8_t chargeState;       // SolarChargerState enum
    uint8_t errorCode;
    float batteryVoltage;      // V
    float batteryCurrent;      // A
    float panelPower;          // W
    uint16_t yieldToday;       // Wh
    float loadCurrent;         // A
};

struct VictronBatteryData {
    float voltage;             // V
    float current;             // A
    float temperature;         // C (0 if aux is voltage)
    float auxVoltage;          // V (0 if aux is temperature)
    uint16_t remainingMinutes;
    float consumedAh;          // Ah
    float soc;                 // %
    bool alarmLowVoltage;
    bool alarmHighVoltage;
    bool alarmLowSOC;
    bool alarmLowTemperature;
    bool alarmHighTemperature;
};

struct VictronInverterData {
    float batteryVoltage;      // V
    float batteryCurrent;      // A
    float acPower;             // W
    uint8_t state;
    bool alarmLowVoltage;
    bool alarmHighVoltage;
    bool alarmHighTemperature;
    bool alarmOverload;
};

struct VictronDCDCData {
    float inputVoltage;        // V
    float outputVoltage;       // V
    float outputCurrent;       // A
    uint8_t chargeState;
    uint8_t errorCode;
};

// ============================================================
// Main device struct with tagged union
// ============================================================

struct VictronDevice {
    char name[VICTRON_NAME_LEN];
    char mac[VICTRON_MAC_LEN];
    VictronDeviceType deviceType;
    int8_t rssi;
    uint32_t lastUpdate;
    bool dataValid;
    union {
        VictronSolarData solar;
        VictronBatteryData battery;
        VictronInverterData inverter;
        VictronDCDCData dcdc;
    };
};

// ============================================================
// Callback — simple function pointer
// ============================================================

typedef void (*VictronCallback)(const VictronDevice* device);

// Forward declaration
class VictronBLEAdvertisedDeviceCallbacks;

// ============================================================
// Main VictronBLE class
// ============================================================

class VictronBLE {
public:
    VictronBLE();

    bool begin(uint32_t scanDuration = 5);
    bool addDevice(const char* name, const char* mac, const char* hexKey,
                   VictronDeviceType type = DEVICE_TYPE_UNKNOWN);
    void setCallback(VictronCallback cb) { callback = cb; }
    void setDebug(bool enable) { debugEnabled = enable; }
    void setMinInterval(uint32_t ms) { minIntervalMs = ms; }
    size_t getDeviceCount() const { return deviceCount; }
    void loop();

private:
    friend class VictronBLEAdvertisedDeviceCallbacks;

    struct DeviceEntry {
        VictronDevice device;
        uint8_t key[16];
        uint16_t lastNonce;
        bool active;
    };

    DeviceEntry devices[VICTRON_MAX_DEVICES];
    size_t deviceCount;
    BLEScan* pBLEScan;
    VictronBLEAdvertisedDeviceCallbacks* scanCallbackObj;
    VictronCallback callback;
    bool debugEnabled;
    uint32_t scanDuration;
    uint32_t minIntervalMs;
    bool initialized;

    static bool hexToBytes(const char* hex, uint8_t* out, size_t len);
    static void normalizeMAC(const char* input, char* output);
    DeviceEntry* findDevice(const char* normalizedMAC);
    bool decryptData(const uint8_t* encrypted, size_t len,
                     const uint8_t* key, const uint8_t* iv, uint8_t* decrypted);
    void processDevice(BLEAdvertisedDevice& dev);
    bool parseAdvertisement(DeviceEntry* entry, const victronManufacturerData& mfg);
    bool parseSolarCharger(const uint8_t* data, size_t len, VictronSolarData& result);
    bool parseBatteryMonitor(const uint8_t* data, size_t len, VictronBatteryData& result);
    bool parseInverter(const uint8_t* data, size_t len, VictronInverterData& result);
    bool parseDCDCConverter(const uint8_t* data, size_t len, VictronDCDCData& result);
};

// BLE scan callback (required by ESP32 BLE API)
class VictronBLEAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    VictronBLEAdvertisedDeviceCallbacks(VictronBLE* parent) : victronBLE(parent) {}
    void onResult(BLEAdvertisedDevice advertisedDevice) override;
private:
    VictronBLE* victronBLE;
};

// ============================================================
// Commented-out features — kept for reference / future use
// ============================================================

#if 0

// --- VictronDeviceConfig (use addDevice(name, mac, key, type) directly) ---
struct VictronDeviceConfig {
    String name;
    String macAddress;
    String encryptionKey;
    VictronDeviceType expectedType;
    VictronDeviceConfig() : expectedType(DEVICE_TYPE_UNKNOWN) {}
    VictronDeviceConfig(const String& n, const String& mac, const String& key, VictronDeviceType type = DEVICE_TYPE_UNKNOWN)
        : name(n), macAddress(mac), encryptionKey(key), expectedType(type) {}
};

// --- Virtual callback interface (replaced by function pointer VictronCallback) ---
class VictronDeviceCallback {
public:
    virtual ~VictronDeviceCallback() {}
    virtual void onSolarChargerData(const SolarChargerData& data) {}
    virtual void onBatteryMonitorData(const BatteryMonitorData& data) {}
    virtual void onInverterData(const InverterData& data) {}
    virtual void onDCDCConverterData(const DCDCConverterData& data) {}
};

// --- Per-type getter methods (use callback instead) ---
bool getSolarChargerData(const String& macAddress, SolarChargerData& data);
bool getBatteryMonitorData(const String& macAddress, BatteryMonitorData& data);
bool getInverterData(const String& macAddress, InverterData& data);
bool getDCDCConverterData(const String& macAddress, DCDCConverterData& data);

// --- Other removed methods ---
void removeDevice(const String& macAddress);
std::vector<String> getDevicesByType(VictronDeviceType type);
String getLastError() const;

#endif // commented-out features

#endif // VICTRON_BLE_H

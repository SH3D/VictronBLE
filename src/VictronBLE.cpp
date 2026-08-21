/**
 * VictronBLE - portable library for Victron Energy BLE devices
 *
 * Thin Arduino wrapper over the pure C core (src/victronble_core.c): this
 * file owns the device registry, nonce dedup and rate limiting; decryption
 * and payload decoding live in victronble_decode(). BLE scanning lives in
 * the per-platform backends under src/esp32 and src/nrf52.
 *
 * Copyright (c) 2025 Scott Penrose
 * License: MIT
 */

#include "VictronBLE.h"
#include "victronble.h"
#include <string.h>
#include <math.h>

// The public API keeps the legacy "absent = 0" convention; the core reports
// absent fields as NAN.
static inline float nan_to_zero(float v) { return isnan(v) ? 0.0f : v; }

VictronBLE::VictronBLE()
    : deviceCount(0), callback(nullptr), debugEnabled(false),
      scanDuration(5), minIntervalMs(1000), initialized(false)
#if defined(VICTRON_BACKEND_ESP32)
    , pBLEScan(nullptr), scanCallbackObj(nullptr)
#endif
{
    memset(devices, 0, sizeof(devices));
}

bool VictronBLE::addDevice(const char* name, const char* mac, const char* hexKey,
                           VictronDeviceType type) {
    if (deviceCount >= VICTRON_MAX_DEVICES) return false;
    if (!mac || strlen(mac) == 0) return false;

    char normalizedMAC[VICTRON_MAC_LEN];
    normalizeMAC(mac, normalizedMAC);

    // Check for duplicate
    if (findDevice(normalizedMAC)) return false;

    DeviceEntry* entry = &devices[deviceCount];
    memset(entry, 0, sizeof(DeviceEntry));

    if (!victronble_parse_key(hexKey, entry->key)) return false;
    entry->active = true;

    strncpy(entry->device.name, name ? name : "", VICTRON_NAME_LEN - 1);
    entry->device.name[VICTRON_NAME_LEN - 1] = '\0';
    memcpy(entry->device.mac, normalizedMAC, VICTRON_MAC_LEN);
    entry->device.deviceType = type;
    entry->device.rssi = -100;

    deviceCount++;

    if (debugEnabled) Serial.printf("[VictronBLE] Added: %s (%s)\n", name, normalizedMAC);
    return true;
}

// Platform-independent advertisement handler. Each BLE backend extracts the
// manufacturer-data bytes (vendor ID first), MAC string and RSSI from a scan
// result and feeds them here.
void VictronBLE::onAdvertisement(const uint8_t* mfgData, size_t len,
                                 const char* macStr, int8_t rssi) {
    if (!victronble_is_product_adv(mfgData, len)) return;

    // Normalize MAC and find device
    char normalizedMAC[VICTRON_MAC_LEN];
    normalizeMAC(macStr, normalizedMAC);

    DeviceEntry* entry = findDevice(normalizedMAC);
    if (!entry) {
        if (debugEnabled) Serial.printf("[VictronBLE] Unmonitored Victron: %s\n", normalizedMAC);
        return;
    }

    // Skip if nonce unchanged (data hasn't changed on the device)
    uint16_t nonce = mfgData[7] | ((uint16_t)mfgData[8] << 8);
    if (entry->device.dataValid && nonce == entry->lastNonce) {
        entry->device.rssi = rssi;  // still refresh RSSI
        return;
    }

    // Skip if minimum interval hasn't elapsed
    uint32_t now = millis();
    if (entry->device.dataValid && (now - entry->device.lastUpdate) < minIntervalMs) {
        return;
    }

    victronble_record_t rec;
    victronble_err_t err = victronble_decode(mfgData, len, entry->key, &rec);
    if (err != VICTRONBLE_OK) {
        if (debugEnabled) Serial.printf("[VictronBLE] Decode %s: %s\n",
                                        entry->device.name, victronble_strerror(err));
        return;
    }

    if (debugEnabled) Serial.printf("[VictronBLE] Processing: %s nonce:0x%04X\n",
                                     entry->device.name, rec.nonce);

    storeRecord(entry, rec);
    entry->lastNonce = nonce;
    entry->device.rssi = rssi;
    entry->device.lastUpdate = now;
    entry->device.dataValid = true;
    if (callback) callback(&entry->device);
}

// Map a decoded core record into the legacy public structs (NAN -> 0).
void VictronBLE::storeRecord(DeviceEntry* entry, const victronble_record_t& rec) {
    switch (rec.type) {
    case VICTRONBLE_DEV_SOLAR_CHARGER: {
        entry->device.deviceType = DEVICE_TYPE_SOLAR_CHARGER;
        VictronSolarData& s = entry->device.solar;
        s.chargeState = rec.u.solar.state;
        s.errorCode = rec.u.solar.error;
        s.batteryVoltage = rec.u.solar.battery_voltage;
        s.batteryCurrent = rec.u.solar.battery_current;
        s.panelPower = rec.u.solar.pv_power;
        s.yieldToday = (uint16_t)rec.u.solar.yield_today_wh;
        s.loadCurrent = nan_to_zero(rec.u.solar.load_current);
        if (debugEnabled) {
            Serial.printf("[VictronBLE] Solar: %.2fV %.2fA %dW State:%d\n",
                          s.batteryVoltage, s.batteryCurrent,
                          (int)s.panelPower, s.chargeState);
        }
        break;
    }
    case VICTRONBLE_DEV_BATTERY_MONITOR: {
        entry->device.deviceType = DEVICE_TYPE_BATTERY_MONITOR;
        VictronBatteryData& b = entry->device.battery;
        b.voltage = rec.u.batmon.voltage;
        b.current = rec.u.batmon.current;
        b.temperature = nan_to_zero(rec.u.batmon.temperature);
        b.auxVoltage = nan_to_zero(rec.u.batmon.aux_voltage);
        b.remainingMinutes = rec.u.batmon.remaining_minutes;
        b.consumedAh = rec.u.batmon.consumed_ah;
        b.soc = rec.u.batmon.soc;
        b.alarmLowVoltage = (rec.u.batmon.alarm & 0x0001) != 0;
        b.alarmHighVoltage = (rec.u.batmon.alarm & 0x0002) != 0;
        b.alarmLowSOC = (rec.u.batmon.alarm & 0x0004) != 0;
        b.alarmLowTemperature = (rec.u.batmon.alarm & 0x0010) != 0;
        b.alarmHighTemperature = (rec.u.batmon.alarm & 0x0020) != 0;
        if (debugEnabled) {
            Serial.printf("[VictronBLE] Battery: %.2fV %.2fA SOC:%.1f%%\n",
                          b.voltage, b.current, b.soc);
        }
        break;
    }
    case VICTRONBLE_DEV_INVERTER: {
        entry->device.deviceType = DEVICE_TYPE_INVERTER;
        VictronInverterData& inv = entry->device.inverter;
        inv.batteryVoltage = rec.u.inverter.battery_voltage;
        inv.batteryCurrent = rec.u.inverter.battery_current;
        inv.acPower = rec.u.inverter.ac_power;
        inv.state = rec.u.inverter.state;
        inv.alarmLowVoltage = (rec.u.inverter.alarms & 0x01) != 0;
        inv.alarmHighVoltage = (rec.u.inverter.alarms & 0x02) != 0;
        inv.alarmHighTemperature = (rec.u.inverter.alarms & 0x04) != 0;
        inv.alarmOverload = (rec.u.inverter.alarms & 0x08) != 0;
        if (debugEnabled) {
            Serial.printf("[VictronBLE] Inverter: %.2fV %dW State:%d\n",
                          inv.batteryVoltage, (int)inv.acPower, inv.state);
        }
        break;
    }
    case VICTRONBLE_DEV_DCDC_CONVERTER: {
        entry->device.deviceType = DEVICE_TYPE_DCDC_CONVERTER;
        VictronDCDCData& d = entry->device.dcdc;
        d.chargeState = rec.u.dcdc.state;
        d.errorCode = rec.u.dcdc.error;
        d.inputVoltage = rec.u.dcdc.input_voltage;
        d.outputVoltage = rec.u.dcdc.output_voltage;
        d.outputCurrent = rec.u.dcdc.output_current;
        if (debugEnabled) {
            Serial.printf("[VictronBLE] DC-DC: In=%.2fV Out=%.2fV %.2fA\n",
                          d.inputVoltage, d.outputVoltage, d.outputCurrent);
        }
        break;
    }
    case VICTRONBLE_DEV_AC_CHARGER: {
        entry->device.deviceType = DEVICE_TYPE_AC_CHARGER;
        VictronACChargerData& a = entry->device.acCharger;
        a.chargeState = rec.u.ac.state;
        a.errorCode = rec.u.ac.error;
        a.voltage1 = nan_to_zero(rec.u.ac.voltage1);
        a.current1 = nan_to_zero(rec.u.ac.current1);
        a.voltage2 = nan_to_zero(rec.u.ac.voltage2);
        a.current2 = nan_to_zero(rec.u.ac.current2);
        a.voltage3 = nan_to_zero(rec.u.ac.voltage3);
        a.current3 = nan_to_zero(rec.u.ac.current3);
        a.temperature = nan_to_zero(rec.u.ac.temperature);
        a.acCurrent = nan_to_zero(rec.u.ac.ac_current);
        if (debugEnabled) {
            Serial.printf("[VictronBLE] AC Charger: %.2fV %.2fA Temp:%.0fC State:%d\n",
                          a.voltage1, a.current1, a.temperature, a.chargeState);
        }
        break;
    }
    default:
        break;
    }
}

// --- Helpers ---

void VictronBLE::normalizeMAC(const char* input, char* output) {
    int j = 0;
    for (int i = 0; input[i] && j < VICTRON_MAC_LEN - 1; i++) {
        char c = input[i];
        if (c == ':' || c == '-') continue;
        output[j++] = (c >= 'A' && c <= 'F') ? (c + 32) : c;
    }
    output[j] = '\0';
}

VictronBLE::DeviceEntry* VictronBLE::findDevice(const char* normalizedMAC) {
    for (size_t i = 0; i < deviceCount; i++) {
        if (devices[i].active && strcmp(devices[i].device.mac, normalizedMAC) == 0) {
            return &devices[i];
        }
    }
    return nullptr;
}

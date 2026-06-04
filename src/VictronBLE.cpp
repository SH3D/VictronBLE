/**
 * VictronBLE - ESP32 library for Victron Energy BLE devices
 * Implementation file
 *
 * Copyright (c) 2025 Scott Penrose
 * License: MIT
 */

#include "VictronBLE.h"
#include <string.h>

VictronBLE::VictronBLE()
    : deviceCount(0), pBLEScan(nullptr), scanCallbackObj(nullptr),
      callback(nullptr), debugEnabled(false), scanDuration(5),
      minIntervalMs(1000), initialized(false) {
    memset(devices, 0, sizeof(devices));
}

bool VictronBLE::begin(uint32_t scanDuration) {
    if (initialized) return true;
    this->scanDuration = scanDuration;

    BLEDevice::init("VictronBLE");
    pBLEScan = BLEDevice::getScan();
    if (!pBLEScan) return false;

    scanCallbackObj = new VictronBLEAdvertisedDeviceCallbacks(this);
    pBLEScan->setAdvertisedDeviceCallbacks(scanCallbackObj, true);
    pBLEScan->setActiveScan(false);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    initialized = true;
    if (debugEnabled) Serial.println("[VictronBLE] Initialized");
    return true;
}

bool VictronBLE::addDevice(const char* name, const char* mac, const char* hexKey,
                           VictronDeviceType type) {
    if (deviceCount >= VICTRON_MAX_DEVICES) return false;
    if (!hexKey || strlen(hexKey) != 32) return false;
    if (!mac || strlen(mac) == 0) return false;

    char normalizedMAC[VICTRON_MAC_LEN];
    normalizeMAC(mac, normalizedMAC);

    // Check for duplicate
    if (findDevice(normalizedMAC)) return false;

    DeviceEntry* entry = &devices[deviceCount];
    memset(entry, 0, sizeof(DeviceEntry));
    entry->active = true;

    strncpy(entry->device.name, name ? name : "", VICTRON_NAME_LEN - 1);
    entry->device.name[VICTRON_NAME_LEN - 1] = '\0';
    memcpy(entry->device.mac, normalizedMAC, VICTRON_MAC_LEN);
    entry->device.deviceType = type;
    entry->device.rssi = -100;

    if (!hexToBytes(hexKey, entry->key, 16)) return false;

    deviceCount++;

    if (debugEnabled) Serial.printf("[VictronBLE] Added: %s (%s)\n", name, normalizedMAC);
    return true;
}

// Scan complete callback — sets flag so loop() restarts
static bool s_scanning = false;
static void onScanDone(BLEScanResults results) {
    s_scanning = false;
}

void VictronBLE::loop() {
    if (!initialized) return;
    if (!s_scanning) {
        pBLEScan->clearResults();
        s_scanning = pBLEScan->start(scanDuration, onScanDone, false);
    }
}

// BLE scan callback
void VictronBLEAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
    if (victronBLE) victronBLE->processDevice(advertisedDevice);
}

void VictronBLE::processDevice(BLEAdvertisedDevice& advertisedDevice) {
    if (!advertisedDevice.haveManufacturerData()) return;

    // getManufacturerData() returns std::string on older ESP32 BLE libraries and
    // an Arduino String on newer ones. Both expose c_str()/length(); constructing
    // from (ptr, len) preserves the embedded null bytes present in binary data.
    auto mfg = advertisedDevice.getManufacturerData();
    std::string raw(mfg.c_str(), mfg.length());
    if (raw.length() < 10) return;

    // Quick vendor ID check before any other work
    uint16_t vendorID = (uint8_t)raw[0] | ((uint8_t)raw[1] << 8);
    if (vendorID != VICTRON_MANUFACTURER_ID) return;

    // Parse manufacturer data
    victronManufacturerData mfgData;
    memset(&mfgData, 0, sizeof(mfgData));
    size_t copyLen = raw.length() > sizeof(mfgData) ? sizeof(mfgData) : raw.length();
    raw.copy(reinterpret_cast<char*>(&mfgData), copyLen);

    // Normalize MAC and find device
    char normalizedMAC[VICTRON_MAC_LEN];
    normalizeMAC(advertisedDevice.getAddress().toString().c_str(), normalizedMAC);

    DeviceEntry* entry = findDevice(normalizedMAC);
    if (!entry) {
        if (debugEnabled) Serial.printf("[VictronBLE] Unmonitored Victron: %s\n", normalizedMAC);
        return;
    }

    // Skip if nonce unchanged (data hasn't changed on the device)
    if (entry->device.dataValid && mfgData.nonceDataCounter == entry->lastNonce) {
        // Still update RSSI since we got a packet
        entry->device.rssi = advertisedDevice.getRSSI();
        return;
    }

    // Skip if minimum interval hasn't elapsed
    uint32_t now = millis();
    if (entry->device.dataValid && (now - entry->device.lastUpdate) < minIntervalMs) {
        return;
    }

    if (debugEnabled) Serial.printf("[VictronBLE] Processing: %s nonce:0x%04X\n",
                                     entry->device.name, mfgData.nonceDataCounter);

    if (parseAdvertisement(entry, mfgData)) {
        entry->lastNonce = mfgData.nonceDataCounter;
        entry->device.rssi = advertisedDevice.getRSSI();
        entry->device.lastUpdate = now;
    }
}

bool VictronBLE::parseAdvertisement(DeviceEntry* entry, const victronManufacturerData& mfg) {
    if (debugEnabled) {
        Serial.printf("[VictronBLE] Beacon:0x%02X Record:0x%02X Nonce:0x%04X\n",
                      mfg.beaconType, mfg.victronRecordType, mfg.nonceDataCounter);
    }

    // Quick key check before expensive decryption
    if (mfg.encryptKeyMatch != entry->key[0]) {
        if (debugEnabled) Serial.println("[VictronBLE] Key byte mismatch");
        return false;
    }

    // Build IV from nonce (2 bytes little-endian + 14 zero bytes)
    uint8_t iv[16] = {0};
    iv[0] = mfg.nonceDataCounter & 0xFF;
    iv[1] = (mfg.nonceDataCounter >> 8) & 0xFF;

    // Decrypt
    uint8_t decrypted[VICTRON_ENCRYPTED_LEN];
    if (!decryptData(mfg.victronEncryptedData, VICTRON_ENCRYPTED_LEN,
                     entry->key, iv, decrypted)) {
        if (debugEnabled) Serial.println("[VictronBLE] Decryption failed");
        return false;
    }

    // Parse based on record type (auto-detects device type)
    bool ok = false;
    switch (mfg.victronRecordType) {
        case DEVICE_TYPE_SOLAR_CHARGER:
            entry->device.deviceType = DEVICE_TYPE_SOLAR_CHARGER;
            ok = parseSolarCharger(decrypted, VICTRON_ENCRYPTED_LEN, entry->device.solar);
            break;
        case DEVICE_TYPE_BATTERY_MONITOR:
            entry->device.deviceType = DEVICE_TYPE_BATTERY_MONITOR;
            ok = parseBatteryMonitor(decrypted, VICTRON_ENCRYPTED_LEN, entry->device.battery);
            break;
        case DEVICE_TYPE_INVERTER:
        case DEVICE_TYPE_INVERTER_RS:
        case DEVICE_TYPE_MULTI_RS:
        case DEVICE_TYPE_VE_BUS:
            entry->device.deviceType = DEVICE_TYPE_INVERTER;
            ok = parseInverter(decrypted, VICTRON_ENCRYPTED_LEN, entry->device.inverter);
            break;
        case DEVICE_TYPE_DCDC_CONVERTER:
            entry->device.deviceType = DEVICE_TYPE_DCDC_CONVERTER;
            ok = parseDCDCConverter(decrypted, VICTRON_ENCRYPTED_LEN, entry->device.dcdc);
            break;
        case DEVICE_TYPE_AC_CHARGER:
            entry->device.deviceType = DEVICE_TYPE_AC_CHARGER;
            ok = parseACCharger(decrypted, VICTRON_ENCRYPTED_LEN, entry->device.acCharger);
            break;
        default:
            if (debugEnabled) Serial.printf("[VictronBLE] Unknown type: 0x%02X\n", mfg.victronRecordType);
            return false;
    }

    if (ok) {
        entry->device.dataValid = true;
        if (callback) callback(&entry->device);
    }

    return ok;
}

bool VictronBLE::decryptData(const uint8_t* encrypted, size_t len,
                             const uint8_t* key, const uint8_t* iv,
                             uint8_t* decrypted) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    if (mbedtls_aes_setkey_enc(&aes, key, 128) != 0) {
        mbedtls_aes_free(&aes);
        return false;
    }

    size_t nc_off = 0;
    uint8_t nonce_counter[16];
    uint8_t stream_block[16];
    memcpy(nonce_counter, iv, 16);
    memset(stream_block, 0, 16);

    int ret = mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce_counter,
                                     stream_block, encrypted, decrypted);
    mbedtls_aes_free(&aes);
    return (ret == 0);
}

bool VictronBLE::parseSolarCharger(const uint8_t* data, size_t len, VictronSolarData& result) {
    if (len < sizeof(victronSolarChargerPayload)) return false;
    const auto* p = reinterpret_cast<const victronSolarChargerPayload*>(data);

    result.chargeState = p->deviceState;
    result.errorCode = p->errorCode;
    result.batteryVoltage = p->batteryVoltage * 0.01f;   // 0.01V units
    result.batteryCurrent = p->batteryCurrent * 0.1f;    // 0.1A units
    result.yieldToday = p->yieldToday * 10;
    result.panelPower = p->inputPower;
    // Load current is a 9-bit field (0.1A units); 0x1FF = no load output
    uint16_t loadRaw = p->loadCurrent & 0x1FF;
    result.loadCurrent = (loadRaw != 0x1FF) ? loadRaw * 0.1f : 0;

    if (debugEnabled) {
        Serial.printf("[VictronBLE] Solar: %.2fV %.2fA %dW State:%d\n",
                      result.batteryVoltage, result.batteryCurrent,
                      (int)result.panelPower, result.chargeState);
    }
    return true;
}

bool VictronBLE::parseACCharger(const uint8_t* data, size_t len, VictronACChargerData& result) {
    // Payload is bit-packed (10 fields, 104 bits ending in byte 12). Decode LSB-first.
    if (len < 13) return false;

    size_t bit = 0;
    auto readBits = [&](uint8_t width) -> uint32_t {
        uint32_t value = 0;
        for (uint8_t i = 0; i < width; i++) {
            size_t b = bit + i;
            value |= (uint32_t)((data[b >> 3] >> (b & 7)) & 0x01) << i;
        }
        bit += width;
        return value;
    };

    result.chargeState = (uint8_t)readBits(8);
    result.errorCode = (uint8_t)readBits(8);

    uint32_t v1 = readBits(13), i1 = readBits(11);
    uint32_t v2 = readBits(13), i2 = readBits(11);
    uint32_t v3 = readBits(13), i3 = readBits(11);
    uint32_t temp = readBits(7);
    uint32_t acCur = readBits(9);

    result.voltage1 = (v1 != 0x1FFF) ? v1 * 0.01f : 0;
    result.current1 = (i1 != 0x7FF)  ? i1 * 0.1f  : 0;
    result.voltage2 = (v2 != 0x1FFF) ? v2 * 0.01f : 0;
    result.current2 = (i2 != 0x7FF)  ? i2 * 0.1f  : 0;
    result.voltage3 = (v3 != 0x1FFF) ? v3 * 0.01f : 0;
    result.current3 = (i3 != 0x7FF)  ? i3 * 0.1f  : 0;
    result.temperature = (temp != 0x7F) ? (float)temp - 40.0f : 0;  // C offset by -40
    result.acCurrent = (acCur != 0x1FF) ? acCur * 0.1f : 0;

    if (debugEnabled) {
        Serial.printf("[VictronBLE] AC Charger: %.2fV %.2fA Temp:%.0fC State:%d\n",
                      result.voltage1, result.current1, result.temperature, result.chargeState);
    }
    return true;
}

bool VictronBLE::parseBatteryMonitor(const uint8_t* data, size_t len, VictronBatteryData& result) {
    // The payload is bit-packed and not byte-aligned, so it is decoded by bit
    // offset directly rather than via a struct. SOC ends at bit 117 (byte 14).
    if (len < 15) return false;

    // TTG (bits 0-15), unsigned minutes
    result.remainingMinutes = data[0] | ((uint16_t)data[1] << 8);

    // Voltage (bits 16-31), signed, 0.01V units
    result.voltage = (int16_t)(data[2] | ((uint16_t)data[3] << 8)) * 0.01f;

    // Alarm (bits 32-47), 16-bit bitmask
    uint16_t alarm = data[4] | ((uint16_t)data[5] << 8);
    result.alarmLowVoltage = (alarm & 0x0001) != 0;
    result.alarmHighVoltage = (alarm & 0x0002) != 0;
    result.alarmLowSOC = (alarm & 0x0004) != 0;
    result.alarmLowTemperature = (alarm & 0x0010) != 0;
    result.alarmHighTemperature = (alarm & 0x0020) != 0;

    // Aux value (bits 48-63) interpreted per aux mode (bits 64-65)
    uint16_t auxRaw = data[6] | ((uint16_t)data[7] << 8);
    uint8_t auxMode = data[8] & 0x03;  // 0=aux voltage, 1=midpoint, 2=temperature, 3=none
    if (auxMode == 0) {
        result.auxVoltage = auxRaw * 0.01f;
        result.temperature = 0;
    } else if (auxMode == 2) {
        result.temperature = auxRaw * 0.01f - 273.15f;  // 0.01K -> C
        result.auxVoltage = 0;
    } else {
        result.auxVoltage = 0;
        result.temperature = 0;
    }

    // Battery current (bits 66-87), 22-bit signed, 0.001A units
    int32_t current = ((uint32_t)(data[8] >> 2) & 0x3F)
                    | ((uint32_t)data[9] << 6)
                    | ((uint32_t)data[10] << 14);
    if (current & 0x200000) current |= 0xFFC00000;  // Sign extend 22-bit
    result.current = current * 0.001f;

    // Consumed Ah (bits 88-107), 20-bit, stored as a positive count, 0.1Ah units.
    // Reported as a negative value (amp-hours consumed).
    uint32_t consumed = (uint32_t)data[11]
                      | ((uint32_t)data[12] << 8)
                      | ((uint32_t)(data[13] & 0x0F) << 16);
    result.consumedAh = -((float)consumed * 0.1f);

    // SOC (bits 108-117), 10-bit, 0.1% units
    uint16_t soc = ((uint16_t)(data[13] >> 4) | ((uint16_t)data[14] << 4)) & 0x3FF;
    result.soc = soc * 0.1f;

    if (debugEnabled) {
        Serial.printf("[VictronBLE] Battery: %.2fV %.2fA SOC:%.1f%%\n",
                      result.voltage, result.current, result.soc);
    }
    return true;
}

bool VictronBLE::parseInverter(const uint8_t* data, size_t len, VictronInverterData& result) {
    if (len < sizeof(victronInverterPayload)) return false;
    const auto* p = reinterpret_cast<const victronInverterPayload*>(data);

    result.state = p->deviceState;
    result.batteryVoltage = p->batteryVoltage * 0.01f;
    result.batteryCurrent = p->batteryCurrent * 0.01f;

    // AC Power (signed 24-bit)
    int32_t acPower = p->acPowerLow | (p->acPowerMid << 8) | (p->acPowerHigh << 16);
    if (acPower & 0x800000) acPower |= 0xFF000000;  // Sign extend
    result.acPower = acPower;

    // Alarm bits
    result.alarmLowVoltage = (p->alarms & 0x01) != 0;
    result.alarmHighVoltage = (p->alarms & 0x02) != 0;
    result.alarmHighTemperature = (p->alarms & 0x04) != 0;
    result.alarmOverload = (p->alarms & 0x08) != 0;

    if (debugEnabled) {
        Serial.printf("[VictronBLE] Inverter: %.2fV %dW State:%d\n",
                      result.batteryVoltage, (int)result.acPower, result.state);
    }
    return true;
}

bool VictronBLE::parseDCDCConverter(const uint8_t* data, size_t len, VictronDCDCData& result) {
    if (len < sizeof(victronDCDCConverterPayload)) return false;
    const auto* p = reinterpret_cast<const victronDCDCConverterPayload*>(data);

    result.chargeState = p->chargeState;
    result.errorCode = p->errorCode;
    result.inputVoltage = p->inputVoltage * 0.01f;
    result.outputVoltage = p->outputVoltage * 0.01f;
    result.outputCurrent = p->outputCurrent * 0.01f;

    if (debugEnabled) {
        Serial.printf("[VictronBLE] DC-DC: In=%.2fV Out=%.2fV %.2fA\n",
                      result.inputVoltage, result.outputVoltage, result.outputCurrent);
    }
    return true;
}

// --- Helpers ---

bool VictronBLE::hexToBytes(const char* hex, uint8_t* out, size_t len) {
    if (strlen(hex) != len * 2) return false;
    for (size_t i = 0; i < len; i++) {
        uint8_t hi = hex[i * 2], lo = hex[i * 2 + 1];
        if (hi >= '0' && hi <= '9') hi -= '0';
        else if (hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10;
        else if (hi >= 'A' && hi <= 'F') hi = hi - 'A' + 10;
        else return false;
        if (lo >= '0' && lo <= '9') lo -= '0';
        else if (lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10;
        else if (lo >= 'A' && lo <= 'F') lo = lo - 'A' + 10;
        else return false;
        out[i] = (hi << 4) | lo;
    }
    return true;
}

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

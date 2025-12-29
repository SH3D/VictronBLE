/**
 * VictronBLE - ESP32 library for Victron Energy BLE devices
 * Implementation file
 *
 * Copyright (c) 2025 Scott Penrose
 * License: MIT
 */

#include "VictronBLE.h"

// Constructor
VictronBLE::VictronBLE()
    : pBLEScan(nullptr), callback(nullptr), debugEnabled(false),
      scanDuration(5), initialized(false) {
}

// Destructor
VictronBLE::~VictronBLE() {
    for (auto& pair : devices) {
        delete pair.second;
    }
    devices.clear();

    if (pBLEScan) {
        pBLEScan->stop();
    }
}

// Initialize BLE
bool VictronBLE::begin(uint32_t scanDuration) {
    if (initialized) {
        debugPrint("VictronBLE already initialized");
        return true;
    }

    this->scanDuration = scanDuration;

    debugPrint("Initializing VictronBLE...");

    BLEDevice::init("VictronBLE");
    pBLEScan = BLEDevice::getScan();

    if (!pBLEScan) {
        lastError = "Failed to create BLE scanner";
        debugPrint(lastError);
        return false;
    }

    pBLEScan->setAdvertisedDeviceCallbacks(new VictronBLEAdvertisedDeviceCallbacks(this), true);
    pBLEScan->setActiveScan(false); // Passive scan - lower power
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    initialized = true;
    debugPrint("VictronBLE initialized successfully");

    return true;
}

// Add a device to monitor
bool VictronBLE::addDevice(const VictronDeviceConfig& config) {
    if (config.macAddress.length() == 0) {
        lastError = "MAC address cannot be empty";
        debugPrint(lastError);
        return false;
    }

    if (config.encryptionKey.length() != 32) {
        lastError = "Encryption key must be 32 hex characters";
        debugPrint(lastError);
        return false;
    }

    String normalizedMAC = normalizeMAC(config.macAddress);

    // Check if device already exists
    if (devices.find(normalizedMAC) != devices.end()) {
        debugPrint("Device " + normalizedMAC + " already exists, updating config");
        delete devices[normalizedMAC];
    }

    DeviceInfo* info = new DeviceInfo();
    info->config = config;
    info->config.macAddress = normalizedMAC;

    // Convert encryption key from hex string to bytes
    if (!hexStringToBytes(config.encryptionKey, info->encryptionKeyBytes, 16)) {
        lastError = "Invalid encryption key format";
        debugPrint(lastError);
        delete info;
        return false;
    }

    // Create appropriate data structure based on device type
    info->data = createDeviceData(config.expectedType);
    if (info->data) {
        info->data->macAddress = normalizedMAC;
        info->data->deviceName = config.name;
    }

    devices[normalizedMAC] = info;

    debugPrint("Added device: " + config.name + " (MAC: " + normalizedMAC + ")");
    if (debugEnabled) {
        debugPrint("  Original MAC input: " + config.macAddress);
        debugPrint("  Stored normalized: " + normalizedMAC);
    }

    return true;
}

bool VictronBLE::addDevice(String name, String macAddress, String encryptionKey,
                           VictronDeviceType expectedType) {
    VictronDeviceConfig config(name, macAddress, encryptionKey, expectedType);
    return addDevice(config);
}

// Remove a device
void VictronBLE::removeDevice(String macAddress) {
    String normalizedMAC = normalizeMAC(macAddress);

    auto it = devices.find(normalizedMAC);
    if (it != devices.end()) {
        delete it->second;
        devices.erase(it);
        debugPrint("Removed device: " + normalizedMAC);
    }
}

// Main loop function
void VictronBLE::loop() {
    if (!initialized) {
        return;
    }

    // Start a scan
    BLEScanResults scanResults = pBLEScan->start(scanDuration, false);
    pBLEScan->clearResults();
}

// BLE callback implementation
void VictronBLEAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
    if (victronBLE) {
        victronBLE->processDevice(advertisedDevice);
    }
}

// Process advertised device
void VictronBLE::processDevice(BLEAdvertisedDevice advertisedDevice) {
    // Get MAC address from the advertised device
    String mac = macAddressToString(advertisedDevice.getAddress());
    String normalizedMAC = normalizeMAC(mac);

    if (debugEnabled) {
        debugPrint("Raw MAC: " + mac + " -> Normalized: " + normalizedMAC);
    }

    // TODO: Consider skipping with no manufacturer data?
    memset(&manufacturerData, 0, sizeof(manufacturerData));
    if (advertisedDevice.haveManufacturerData()) {
        std::string  mfgData = advertisedDevice.getManufacturerData();
        // XXX Storing it this way is not thread safe - is that issue on this ESP32?
        debugPrint("Getting manufacturer data: Size=" + String(mfgData.length()));
        mfgData.copy((char*)&manufacturerData, (mfgData.length() > sizeof(manufacturerData) ? sizeof(manufacturerData) : mfgData.length()));
    }

    // Pointer? XXX

    // Debug: Log all discovered BLE devices
    if (debugEnabled) {
        String debugMsg = "";

        debugMsg += "BLE Device: " + mac;
        debugMsg += ", RSSI: " + String(advertisedDevice.getRSSI()) + " dBm";
        if (advertisedDevice.haveName())
            debugMsg += ", Name: " + String(advertisedDevice.getName().c_str());

        debugMsg += ", Mfg ID: 0x" + String(manufacturerData.vendorID, HEX);
        if (manufacturerData.vendorID == VICTRON_MANUFACTURER_ID) {
            debugMsg += " (Victron)";
        }

        debugPrint(debugMsg);
    }

    // Check if this is one of our configured devices
    auto it = devices.find(normalizedMAC);
    if (it == devices.end()) {
        // XXX Check if the device is a Victron device
        //  This needs lots of improvemet and only do in debug
        if (manufacturerData.vendorID == VICTRON_MANUFACTURER_ID) {
            debugPrint("Found unmonitored Victron Device: " + normalizeMAC(mac));
            // DeviceInfo* deviceInfo = new DeviceInfo(mac, advertisedDevice.getName());
            // devices.insert({normalizedMAC, deviceInfo});
            // XXX What type of Victron device is it?
            // Check if it's a Victron Energy device
            /*
            if (advertisedDevice.haveServiceData()) {
                std::string serviceData = advertisedDevice.getServiceData();
                if (serviceData.length() >= 2) {
                    uint16_t serviceId = (uint8_t)serviceData[1] << 8 | (uint8_t)serviceData[0];
                    if (serviceId == VICTRON_ENERGY_SERVICE_ID) {
                        debugPrint("Found Victron Energy Device: " + mac);
                    }
                }
            }
            */
        }
        return; // Not a device we're monitoring
    }

    DeviceInfo* deviceInfo = it->second;

    // Check if it's Victron (manufacturer ID 0x02E1)
    if (manufacturerData.vendorID != VICTRON_MANUFACTURER_ID) {
        debugPrint("Skipping non VICTRON");
        return;
    }

    debugPrint("Processing data from: " + deviceInfo->config.name);

    // Parse the advertisement
    if (parseAdvertisement(normalizedMAC)) {
        // Update RSSI
        if (deviceInfo->data) {
            deviceInfo->data->rssi = advertisedDevice.getRSSI();
            deviceInfo->data->lastUpdate = millis();
        }
    }
}

// Parse advertisement data
bool VictronBLE::parseAdvertisement(const String& macAddress) {
    // XXX We already searched above - try not to again?
    auto it = devices.find(macAddress);
    if (it == devices.end()) {
        debugPrint("parseAdvertisement: Device not found");
        return false;
    }

    DeviceInfo* deviceInfo = it->second;

    if (debugEnabled) {
        debugPrint("Vendor ID: 0x" + String(manufacturerData.vendorID, HEX));
        debugPrint("Beacon Type: 0x" + String(manufacturerData.beaconType, HEX));
        debugPrint("Record Type: 0x" + String(manufacturerData.victronRecordType, HEX));
        debugPrint("Nonce: 0x" + String(manufacturerData.nonceDataCounter, HEX));
    }

    // Build IV (initialization vector) from nonce
    // IV is 16 bytes: nonce (2 bytes little-endian) + zeros (14 bytes)
    uint8_t iv[16] = {0};
    iv[0] = manufacturerData.nonceDataCounter & 0xFF;        // Low byte
    iv[1] = (manufacturerData.nonceDataCounter >> 8) & 0xFF; // High byte
    // Remaining bytes stay zero

    // Decrypt the data
    uint8_t decrypted[32]; // Max expected size
    if (!decryptAdvertisement(manufacturerData.victronEncryptedData,
                              sizeof(manufacturerData.victronEncryptedData),
                              deviceInfo->encryptionKeyBytes, iv, decrypted)) {
        lastError = "Decryption failed";
        debugPrint(lastError);
        return false;
    }

    // Parse based on device type
    bool parseOk = false;

    switch (manufacturerData.victronRecordType) {
        case DEVICE_TYPE_SOLAR_CHARGER:
            if (deviceInfo->data && deviceInfo->data->deviceType == DEVICE_TYPE_SOLAR_CHARGER) {
                parseOk = parseSolarCharger(decrypted, sizeof(decrypted),
                                           *(SolarChargerData*)deviceInfo->data);
            }
            break;

        case DEVICE_TYPE_BATTERY_MONITOR:
            if (deviceInfo->data && deviceInfo->data->deviceType == DEVICE_TYPE_BATTERY_MONITOR) {
                parseOk = parseBatteryMonitor(decrypted, sizeof(decrypted),
                                             *(BatteryMonitorData*)deviceInfo->data);
            }
            break;

        case DEVICE_TYPE_INVERTER:
        case DEVICE_TYPE_INVERTER_RS:
        case DEVICE_TYPE_MULTI_RS:
        case DEVICE_TYPE_VE_BUS:
            if (deviceInfo->data && deviceInfo->data->deviceType == DEVICE_TYPE_INVERTER) {
                parseOk = parseInverter(decrypted, sizeof(decrypted),
                                       *(InverterData*)deviceInfo->data);
            }
            break;

        case DEVICE_TYPE_DCDC_CONVERTER:
            if (deviceInfo->data && deviceInfo->data->deviceType == DEVICE_TYPE_DCDC_CONVERTER) {
                parseOk = parseDCDCConverter(decrypted, sizeof(decrypted),
                                            *(DCDCConverterData*)deviceInfo->data);
            }
            break;

        default:
            debugPrint("Unknown device type: 0x" + String(manufacturerData.victronRecordType, HEX));
            return false;
    }

    if (parseOk && deviceInfo->data) {
        deviceInfo->data->dataValid = true;

        // Call appropriate callback
        if (callback) {
            switch (manufacturerData.victronRecordType) {
                case DEVICE_TYPE_SOLAR_CHARGER:
                    callback->onSolarChargerData(*(SolarChargerData*)deviceInfo->data);
                    break;
                case DEVICE_TYPE_BATTERY_MONITOR:
                    callback->onBatteryMonitorData(*(BatteryMonitorData*)deviceInfo->data);
                    break;
                case DEVICE_TYPE_INVERTER:
                case DEVICE_TYPE_INVERTER_RS:
                case DEVICE_TYPE_MULTI_RS:
                case DEVICE_TYPE_VE_BUS:
                    callback->onInverterData(*(InverterData*)deviceInfo->data);
                    break;
                case DEVICE_TYPE_DCDC_CONVERTER:
                    callback->onDCDCConverterData(*(DCDCConverterData*)deviceInfo->data);
                    break;
            }
        }
    }

    return parseOk;
}

// Decrypt advertisement using AES-128-CTR
bool VictronBLE::decryptAdvertisement(const uint8_t* encrypted, size_t encLen,
                                      const uint8_t* key, const uint8_t* iv,
                                      uint8_t* decrypted) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    // Set encryption key
    int ret = mbedtls_aes_setkey_enc(&aes, key, 128);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return false;
    }

    // AES-CTR decryption
    size_t nc_off = 0;
    uint8_t nonce_counter[16];
    uint8_t stream_block[16];

    memcpy(nonce_counter, iv, 16);
    memset(stream_block, 0, 16);

    ret = mbedtls_aes_crypt_ctr(&aes, encLen, &nc_off, nonce_counter,
                                 stream_block, encrypted, decrypted);

    mbedtls_aes_free(&aes);

    return (ret == 0);
}

// Parse Solar Charger data
bool VictronBLE::parseSolarCharger(const uint8_t* data, size_t len, SolarChargerData& result) {
    if (len < sizeof(victronSolarChargerPayload)) {
        debugPrint("Solar charger data too short: " + String(len) + " bytes");
        return false;
    }

    // Cast decrypted data to struct for easy access
    const victronSolarChargerPayload* payload = (const victronSolarChargerPayload*)data;

    // Parse charge state
    result.chargeState = (SolarChargerState)payload->deviceState;

    // Parse battery voltage (10 mV units -> volts)
    result.batteryVoltage = payload->batteryVoltage * 0.01f;

    // Parse battery current (10 mA units, signed -> amps)
    result.batteryCurrent = payload->batteryCurrent * 0.01f;

    // Parse yield today (10 Wh units -> Wh)
    result.yieldToday = payload->yieldToday * 10;

    // Parse PV power (1 W units)
    result.panelPower = payload->inputPower;

    // Parse load current (10 mA units -> amps, 0xFFFF = no load)
    if (payload->loadCurrent != 0xFFFF) {
        result.loadCurrent = payload->loadCurrent * 0.01f;
    } else {
        result.loadCurrent = 0;
    }

    // Calculate PV voltage from power and current (if current > 0)
    if (result.batteryCurrent > 0.1f) {
        result.panelVoltage = result.panelPower / result.batteryCurrent;
    } else {
        result.panelVoltage = 0;
    }

    debugPrint("Solar Charger: " + String(result.batteryVoltage, 2) + "V, " +
               String(result.batteryCurrent, 2) + "A, " +
               String(result.panelPower) + "W, State: " + String(result.chargeState));

    return true;
}

// Parse Battery Monitor data
bool VictronBLE::parseBatteryMonitor(const uint8_t* data, size_t len, BatteryMonitorData& result) {
    if (len < sizeof(victronBatteryMonitorPayload)) {
        debugPrint("Battery monitor data too short: " + String(len) + " bytes");
        return false;
    }

    // Cast decrypted data to struct for easy access
    const victronBatteryMonitorPayload* payload = (const victronBatteryMonitorPayload*)data;

    // Parse remaining time (1 minute units)
    result.remainingMinutes = payload->remainingMins;

    // Parse battery voltage (10 mV units -> volts)
    result.voltage = payload->batteryVoltage * 0.01f;

    // Parse alarm bits
    result.alarmLowVoltage = (payload->alarms & 0x01) != 0;
    result.alarmHighVoltage = (payload->alarms & 0x02) != 0;
    result.alarmLowSOC = (payload->alarms & 0x04) != 0;
    result.alarmLowTemperature = (payload->alarms & 0x10) != 0;
    result.alarmHighTemperature = (payload->alarms & 0x20) != 0;

    // Parse aux data: voltage (10 mV units) or temperature (0.01K units)
    if (payload->auxData < 3000) { // If < 30V, it's voltage
        result.auxVoltage = payload->auxData * 0.01f;
        result.temperature = 0;
    } else { // Otherwise temperature in 0.01 Kelvin
        result.temperature = (payload->auxData * 0.01f) - 273.15f;
        result.auxVoltage = 0;
    }

    // Parse battery current (22-bit signed, 1 mA units)
    // Bits 0-7: currentLow, Bits 8-15: currentMid, Bits 16-21: low 6 bits of currentHigh_consumedLow
    int32_t current = payload->currentLow |
                     (payload->currentMid << 8) |
                     ((payload->currentHigh_consumedLow & 0x3F) << 16);
    // Sign extend from 22 bits to 32 bits
    if (current & 0x200000) {
        current |= 0xFFC00000;
    }
    result.current = current * 0.001f; // Convert mA to A

    // Parse consumed Ah (18-bit signed, 10 mAh units)
    // Bits 0-1: high 2 bits of currentHigh_consumedLow, Bits 2-9: consumedMid, Bits 10-17: consumedHigh
    int32_t consumedAh = ((payload->currentHigh_consumedLow & 0xC0) >> 6) |
                        (payload->consumedMid << 2) |
                        (payload->consumedHigh << 10);
    // Sign extend from 18 bits to 32 bits
    if (consumedAh & 0x20000) {
        consumedAh |= 0xFFFC0000;
    }
    result.consumedAh = consumedAh * 0.01f; // Convert 10mAh to Ah

    // Parse SOC (10-bit value, 10 = 1.0%)
    result.soc = (payload->soc & 0x3FF) * 0.1f;

    debugPrint("Battery Monitor: " + String(result.voltage, 2) + "V, " +
               String(result.current, 2) + "A, SOC: " + String(result.soc, 1) + "%");

    return true;
}

// Parse Inverter data
bool VictronBLE::parseInverter(const uint8_t* data, size_t len, InverterData& result) {
    if (len < sizeof(victronInverterPayload)) {
        debugPrint("Inverter data too short: " + String(len) + " bytes");
        return false;
    }

    // Cast decrypted data to struct for easy access
    const victronInverterPayload* payload = (const victronInverterPayload*)data;

    // Parse device state
    result.state = payload->deviceState;

    // Parse battery voltage (10 mV units -> volts)
    result.batteryVoltage = payload->batteryVoltage * 0.01f;

    // Parse battery current (10 mA units, signed -> amps)
    result.batteryCurrent = payload->batteryCurrent * 0.01f;

    // Parse AC Power (signed 24-bit, 1 W units)
    int32_t acPower = payload->acPowerLow |
                     (payload->acPowerMid << 8) |
                     (payload->acPowerHigh << 16);
    // Sign extend from 24 bits to 32 bits
    if (acPower & 0x800000) {
        acPower |= 0xFF000000;
    }
    result.acPower = acPower;

    // Parse alarm bits
    result.alarmLowVoltage = (payload->alarms & 0x01) != 0;
    result.alarmHighVoltage = (payload->alarms & 0x02) != 0;
    result.alarmHighTemperature = (payload->alarms & 0x04) != 0;
    result.alarmOverload = (payload->alarms & 0x08) != 0;

    debugPrint("Inverter: " + String(result.batteryVoltage, 2) + "V, " +
               String(result.acPower) + "W, State: " + String(result.state));

    return true;
}

// Parse DC-DC Converter data
bool VictronBLE::parseDCDCConverter(const uint8_t* data, size_t len, DCDCConverterData& result) {
    if (len < sizeof(victronDCDCConverterPayload)) {
        debugPrint("DC-DC converter data too short: " + String(len) + " bytes");
        return false;
    }

    // Cast decrypted data to struct for easy access
    const victronDCDCConverterPayload* payload = (const victronDCDCConverterPayload*)data;

    // Parse charge state
    result.chargeState = payload->chargeState;

    // Parse error code
    result.errorCode = payload->errorCode;

    // Parse input voltage (10 mV units -> volts)
    result.inputVoltage = payload->inputVoltage * 0.01f;

    // Parse output voltage (10 mV units -> volts)
    result.outputVoltage = payload->outputVoltage * 0.01f;

    // Parse output current (10 mA units -> amps)
    result.outputCurrent = payload->outputCurrent * 0.01f;

    debugPrint("DC-DC Converter: In=" + String(result.inputVoltage, 2) + "V, Out=" +
               String(result.outputVoltage, 2) + "V, " + String(result.outputCurrent, 2) + "A");

    return true;
}

// Get data methods
bool VictronBLE::getSolarChargerData(String macAddress, SolarChargerData& data) {
    String normalizedMAC = normalizeMAC(macAddress);
    auto it = devices.find(normalizedMAC);

    if (it != devices.end() && it->second->data &&
        it->second->data->deviceType == DEVICE_TYPE_SOLAR_CHARGER) {
        data = *(SolarChargerData*)it->second->data;
        return data.dataValid;
    }
    return false;
}

bool VictronBLE::getBatteryMonitorData(String macAddress, BatteryMonitorData& data) {
    String normalizedMAC = normalizeMAC(macAddress);
    auto it = devices.find(normalizedMAC);

    if (it != devices.end() && it->second->data &&
        it->second->data->deviceType == DEVICE_TYPE_BATTERY_MONITOR) {
        data = *(BatteryMonitorData*)it->second->data;
        return data.dataValid;
    }
    return false;
}

bool VictronBLE::getInverterData(String macAddress, InverterData& data) {
    String normalizedMAC = normalizeMAC(macAddress);
    auto it = devices.find(normalizedMAC);

    if (it != devices.end() && it->second->data &&
        it->second->data->deviceType == DEVICE_TYPE_INVERTER) {
        data = *(InverterData*)it->second->data;
        return data.dataValid;
    }
    return false;
}

bool VictronBLE::getDCDCConverterData(String macAddress, DCDCConverterData& data) {
    String normalizedMAC = normalizeMAC(macAddress);
    auto it = devices.find(normalizedMAC);

    if (it != devices.end() && it->second->data &&
        it->second->data->deviceType == DEVICE_TYPE_DCDC_CONVERTER) {
        data = *(DCDCConverterData*)it->second->data;
        return data.dataValid;
    }
    return false;
}

// Get devices by type
std::vector<String> VictronBLE::getDevicesByType(VictronDeviceType type) {
    std::vector<String> result;

    for (const auto& pair : devices) {
        if (pair.second->data && pair.second->data->deviceType == type) {
            result.push_back(pair.first);
        }
    }

    return result;
}

// Helper: Create device data structure
VictronDeviceData* VictronBLE::createDeviceData(VictronDeviceType type) {
    switch (type) {
        case DEVICE_TYPE_SOLAR_CHARGER:
            return new SolarChargerData();
        case DEVICE_TYPE_BATTERY_MONITOR:
            return new BatteryMonitorData();
        case DEVICE_TYPE_INVERTER:
        case DEVICE_TYPE_INVERTER_RS:
        case DEVICE_TYPE_MULTI_RS:
        case DEVICE_TYPE_VE_BUS:
            return new InverterData();
        case DEVICE_TYPE_DCDC_CONVERTER:
            return new DCDCConverterData();
        default:
            return new VictronDeviceData();
    }
}

// Helper: Convert hex string to bytes
bool VictronBLE::hexStringToBytes(const String& hex, uint8_t* bytes, size_t len) {
    if (hex.length() != len * 2) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        String byteStr = hex.substring(i * 2, i * 2 + 2);
        char* endPtr;
        bytes[i] = strtoul(byteStr.c_str(), &endPtr, 16);
        if (*endPtr != '\0') {
            return false;
        }
    }

    return true;
}

// Helper: MAC address to string
String VictronBLE::macAddressToString(BLEAddress address) {
    // Use the BLEAddress toString() method which provides consistent formatting
    return String(address.toString().c_str());
}

// Helper: Normalize MAC address format
String VictronBLE::normalizeMAC(String mac) {
    String normalized = mac;
    normalized.toLowerCase();
    // XXX - is this right, was - to : but not consistent location of pairs or not
    normalized.replace("-", "");
    normalized.replace(":", "");
    return normalized;
}

// Debug helpers
void VictronBLE::debugPrint(const String& message) {
    if (debugEnabled)
        Serial.println("[VictronBLE] " + message);
}

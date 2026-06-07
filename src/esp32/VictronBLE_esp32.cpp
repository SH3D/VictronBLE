/**
 * VictronBLE - ESP32 BLE scanning backend
 *
 * Uses the ESP32 Arduino BLE library (Bluedroid). Extracts the manufacturer
 * data, MAC and RSSI from each passive scan result and hands them to the
 * platform-independent VictronBLE::onAdvertisement().
 *
 * Copyright (c) 2025 Scott Penrose
 * License: MIT
 */
#include "../VictronBLE.h"

#if defined(VICTRON_BACKEND_ESP32)

#include <string>

// Scan complete callback — clears the flag so loop() restarts the scan
static bool s_scanning = false;
static void onScanDone(BLEScanResults results) {
    s_scanning = false;
}

bool VictronBLE::begin(uint32_t scanDuration) {
    if (initialized) return true;
    this->scanDuration = scanDuration;

    BLEDevice::init("VictronBLE");
    pBLEScan = BLEDevice::getScan();
    if (!pBLEScan) return false;

    scanCallbackObj = new VictronBLEAdvertisedDeviceCallbacks(this);
    pBLEScan->setAdvertisedDeviceCallbacks(scanCallbackObj, true);
    pBLEScan->setActiveScan(false);   // passive: Victron beacons are non-connectable
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    initialized = true;
    if (debugEnabled) Serial.println("[VictronBLE] Initialized (ESP32 backend)");
    return true;
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
    // Debug: print every BLE device seen (before any filtering)
    if (debugEnabled) {
        Serial.printf("[VictronBLE] MAC=%-17s  RSSI=%-4d  Name=%-20s  ManData=%s\n",
            advertisedDevice.getAddress().toString().c_str(),
            advertisedDevice.getRSSI(),
            advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "(none)",
            advertisedDevice.haveManufacturerData() ? "yes" : "no");
    }

    if (!advertisedDevice.haveManufacturerData()) return;

    // getManufacturerData() returns std::string on older ESP32 BLE libraries and
    // an Arduino String on newer ones. Both expose c_str()/length(); building a
    // std::string from (ptr, len) preserves the binary payload's null bytes.
    auto mfg = advertisedDevice.getManufacturerData();
    std::string raw(mfg.c_str(), mfg.length());

    onAdvertisement(reinterpret_cast<const uint8_t*>(raw.data()), raw.length(),
                    advertisedDevice.getAddress().toString().c_str(),
                    advertisedDevice.getRSSI());
}

#endif // VICTRON_BACKEND_ESP32

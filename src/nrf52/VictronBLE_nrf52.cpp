/**
 * VictronBLE - nRF52 BLE scanning backend (Adafruit/Seeed Bluefruit)
 *
 * Uses the Bluefruit nRF52 library bundled with the Adafruit/Seeed nRF52 core.
 * Performs a continuous passive scan and extracts the manufacturer data, MAC
 * and RSSI from each advertisement, handing them to the platform-independent
 * VictronBLE::onAdvertisement().
 *
 * Tested target: Seeed XIAO nRF52840.
 *
 * Copyright (c) 2025 Scott Penrose
 * License: MIT
 */
#include "../VictronBLE.h"

#if defined(VICTRON_BACKEND_NRF52)

VictronBLE* VictronBLE::s_instance = nullptr;

bool VictronBLE::begin(uint32_t scanDuration) {
    if (initialized) return true;
    this->scanDuration = scanDuration;   // not used for nRF52 (scan is continuous)
    s_instance = this;

    Bluefruit.begin(0, 1);                // 0 peripheral, 1 central (observer)
    Bluefruit.setName("VictronBLE");

    Bluefruit.Scanner.setRxCallback(VictronBLE::scanCallback);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.setInterval(160, 80);   // 100ms interval / 50ms window (0.625ms units)
    Bluefruit.Scanner.useActiveScan(false);   // passive: Victron beacons are non-connectable
    Bluefruit.Scanner.start(0);               // 0 = scan forever

    initialized = true;
    if (debugEnabled) Serial.println("[VictronBLE] Initialized (nRF52 Bluefruit backend)");
    return true;
}

void VictronBLE::loop() {
    // Scanning is fully event-driven on nRF52 (SoftDevice invokes scanCallback);
    // nothing to pump here. Kept for API parity with the ESP32 backend.
}

void VictronBLE::scanCallback(ble_gap_evt_adv_report_t* report) {
    if (s_instance) {
        // Format MAC (little-endian to big-endian hex)
        const uint8_t* a = report->peer_addr.addr;
        char mac[18];
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                 a[5], a[4], a[3], a[2], a[1], a[0]);

        // Debug: print every BLE device seen (before any filtering)
        if (s_instance->debugEnabled) {
            // Manufacturer specific data (AD type 0xFF) — includes the 0x02E1 vendor ID
            uint8_t mfgBuf[31];
            uint8_t mfgLen = Bluefruit.Scanner.parseReportByType(
                report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, mfgBuf, sizeof(mfgBuf));

            // Try to get device name
            char nameBuf[32] = "(none)";
            uint8_t nameLen = Bluefruit.Scanner.parseReportByType(
                report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, (uint8_t*)nameBuf, sizeof(nameBuf) - 1);
            if (nameLen == 0) {
                nameLen = Bluefruit.Scanner.parseReportByType(
                    report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, (uint8_t*)nameBuf, sizeof(nameBuf) - 1);
            }
            if (nameLen > 0) nameBuf[nameLen] = '\0';

            Serial.printf("[VictronBLE] MAC=%-17s  RSSI=%-4d  Name=%-20s  ManData=%s\n",
                mac, report->rssi, nameBuf, mfgLen >= 2 ? "yes" : "no");
        }

        // Manufacturer specific data (AD type 0xFF) — includes the 0x02E1 vendor ID
        uint8_t buf[31];
        uint8_t len = Bluefruit.Scanner.parseReportByType(
            report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, buf, sizeof(buf));

        if (len >= 2) {
            s_instance->onAdvertisement(buf, len, mac, report->rssi);
        }
    }

    // Bluefruit pauses scanning while the RX callback runs — must resume.
    Bluefruit.Scanner.resume();
}

#endif // VICTRON_BACKEND_NRF52

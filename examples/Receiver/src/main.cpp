/**
 * VictronBLE ESPNow Receiver
 *
 * Standalone receiver for data sent by the Repeater example.
 * Does NOT depend on VictronBLE library - just ESPNow.
 *
 * Flash this on a second ESP32 and it will print Solar Charger
 * data received over ESPNow from the Repeater.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ESPNow packet structure - must match Repeater
struct __attribute__((packed)) SolarChargerPacket {
    uint8_t chargeState;
    float batteryVoltage;     // V
    float batteryCurrent;     // A
    float panelVoltage;       // V
    float panelPower;         // W
    uint16_t yieldToday;      // Wh
    float loadCurrent;        // A
    int8_t rssi;              // BLE RSSI
    char deviceName[16];      // Null-terminated, truncated
};

static uint32_t recvCount = 0;

static const char* chargeStateName(uint8_t state) {
    switch (state) {
        case 0:   return "Off";
        case 1:   return "Low Power";
        case 2:   return "Fault";
        case 3:   return "Bulk";
        case 4:   return "Absorption";
        case 5:   return "Float";
        case 6:   return "Storage";
        case 7:   return "Equalize";
        case 9:   return "Inverting";
        case 11:  return "Power Supply";
        case 252: return "External Control";
        default:  return "Unknown";
    }
}

void onDataRecv(const uint8_t* senderMac, const uint8_t* data, int len) {
    if (len != sizeof(SolarChargerPacket)) {
        Serial.println("Unexpected packet size: " + String(len));
        return;
    }

    const auto* pkt = reinterpret_cast<const SolarChargerPacket*>(data);
    recvCount++;

    // Ensure device name is null-terminated even if corrupted
    char name[17];
    memcpy(name, pkt->deviceName, 16);
    name[16] = '\0';

    Serial.printf("[RX #%lu] %s | State:%s Batt:%.2fV %.2fA PV:%.1fV %.0fW Yield:%uWh",
                  recvCount,
                  name,
                  chargeStateName(pkt->chargeState),
                  pkt->batteryVoltage,
                  pkt->batteryCurrent,
                  pkt->panelVoltage,
                  pkt->panelPower,
                  pkt->yieldToday);

    if (pkt->loadCurrent > 0) {
        Serial.printf(" Load:%.2fA", pkt->loadCurrent);
    }

    Serial.printf(" RSSI:%ddBm From:%02X:%02X:%02X:%02X:%02X:%02X\n",
                  pkt->rssi,
                  senderMac[0], senderMac[1], senderMac[2],
                  senderMac[3], senderMac[4], senderMac[5]);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== VictronBLE ESPNow Receiver ===\n");

    // Init WiFi in STA mode (required for ESPNow)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.println("MAC: " + WiFi.macAddress());

    // Init ESPNow
    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESPNow init failed!");
        while (1) delay(1000);
    }

    esp_now_register_recv_cb(onDataRecv);

    Serial.println("ESPNow initialized, waiting for packets...");
    Serial.println("Expecting " + String(sizeof(SolarChargerPacket)) + " byte packets\n");
}

void loop() {
    delay(100);
}

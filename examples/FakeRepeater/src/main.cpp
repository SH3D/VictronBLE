/**
 * VictronBLE FakeRepeater Example
 *
 * Sends fake Solar Charger data over ESPNow every 10 seconds.
 * Use with the Receiver example to test ESPNow without needing
 * a real Victron device or the VictronBLE library.
 *
 * No VictronBLE dependency - just WiFi + ESPNow.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ESPNow packet structure - must match Receiver
struct __attribute__((packed)) SolarChargerPacket {
    uint8_t chargeState;
    float batteryVoltage;     // V
    float batteryCurrent;     // A
    float panelPower;         // W
    uint16_t yieldToday;      // Wh
    float loadCurrent;        // A
    int8_t rssi;              // BLE RSSI
    char deviceName[16];      // Null-terminated, truncated
};

static const uint8_t BROADCAST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint32_t sendCount = 0;
static unsigned long lastSendTime = 0;
static const unsigned long SEND_INTERVAL_MS = 10000;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== VictronBLE FakeRepeater ===\n");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.println("MAC: " + WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESPNow init failed!");
        while (1) delay(1000);
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("ERROR: Failed to add broadcast peer!");
        while (1) delay(1000);
    }

    Serial.println("ESPNow initialized, sending fake data every 10s");
    Serial.println("Packet size: " + String(sizeof(SolarChargerPacket)) + " bytes\n");
}

void loop() {
    unsigned long now = millis();
    if (now - lastSendTime < SEND_INTERVAL_MS) {
        delay(100);
        return;
    }
    lastSendTime = now;
    sendCount++;

    // Generate varying fake data
    SolarChargerPacket pkt;
    pkt.chargeState = (sendCount % 4) + 3; // Cycle through Bulk(3), Absorption(4), Float(5), Storage(6)
    pkt.batteryVoltage = 51.0f + (sendCount % 20) * 0.15f;
    pkt.batteryCurrent = 2.0f + (sendCount % 10) * 0.5f;
    pkt.panelPower = pkt.batteryCurrent * pkt.batteryVoltage;
    pkt.yieldToday = 100 + sendCount * 10;
    pkt.loadCurrent = 0;
    pkt.rssi = -60 - (sendCount % 30);

    memset(pkt.deviceName, 0, sizeof(pkt.deviceName));
    strncpy(pkt.deviceName, "FakeMPPT", sizeof(pkt.deviceName) - 1);

    esp_err_t result = esp_now_send(BROADCAST_ADDR,
                                    reinterpret_cast<const uint8_t*>(&pkt),
                                    sizeof(pkt));

    if (result != ESP_OK) {
        Serial.println("[TX FAIL] " + String(esp_err_to_name(result)));
    } else {
        Serial.printf("[TX #%lu] %s Batt:%.2fV %.2fA PV:%.0fW Yield:%uWh RSSI:%d\n",
                      sendCount,
                      pkt.deviceName,
                      pkt.batteryVoltage,
                      pkt.batteryCurrent,
                      pkt.panelPower,
                      pkt.yieldToday,
                      pkt.rssi);
    }
}

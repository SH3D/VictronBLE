/**
 * VictronBLE Repeater Example
 *
 * Collects Solar Charger data via BLE and transmits the latest
 * readings over ESPNow broadcast every 30 seconds. Place this ESP32
 * near Victron devices and use a separate Receiver ESP32 at a distance.
 *
 * ESPNow range is typically much greater than BLE (~200m+ line of sight).
 *
 * Setup:
 * 1. Get your device encryption keys from the VictronConnect app
 * 2. Update the device configurations below with your MAC and key
 * 3. Flash the Receiver example on a second ESP32
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "VictronBLE.h"

// ESPNow packet structure - must match Receiver
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

// Broadcast address
static const uint8_t BROADCAST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static const unsigned long SEND_INTERVAL_MS = 5000; // 30 seconds

static uint32_t sendCount = 0;
static uint32_t sendFailCount = 0;
static uint32_t blePacketCount = 0;

// Cache latest packet per device
static const int MAX_DEVICES = 4;
static SolarChargerPacket cachedPackets[MAX_DEVICES];
static bool cachedValid[MAX_DEVICES] = {};
static int cachedCount = 0;
static unsigned long lastSendTime = 0;

VictronBLE victron;

// Find cached slot by device name, or allocate a new one
static int findOrAddCached(const char* name) {
    for (int i = 0; i < cachedCount; i++) {
        if (strncmp(cachedPackets[i].deviceName, name, sizeof(cachedPackets[i].deviceName)) == 0)
            return i;
    }
    if (cachedCount < MAX_DEVICES) return cachedCount++;
    return -1;
}

class RepeaterCallback : public VictronDeviceCallback {
public:
    void onSolarChargerData(const SolarChargerData& data) override {
        blePacketCount++;

        // Build packet
        SolarChargerPacket pkt;
        pkt.chargeState = static_cast<uint8_t>(data.chargeState);
        pkt.batteryVoltage = data.batteryVoltage;
        pkt.batteryCurrent = data.batteryCurrent;
        pkt.panelVoltage = data.panelVoltage;
        pkt.panelPower = data.panelPower;
        pkt.yieldToday = data.yieldToday;
        pkt.loadCurrent = data.loadCurrent;
        pkt.rssi = data.rssi;
        memset(pkt.deviceName, 0, sizeof(pkt.deviceName));
        strncpy(pkt.deviceName, data.deviceName.c_str(), sizeof(pkt.deviceName) - 1);

        // Cache it
        int idx = findOrAddCached(pkt.deviceName);
        if (idx >= 0) {
            cachedPackets[idx] = pkt;
            cachedValid[idx] = true;
        }
    }
};

RepeaterCallback callback;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== VictronBLE ESPNow Repeater ===\n");

    // Init WiFi in STA mode (required for ESPNow)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.println("MAC: " + WiFi.macAddress());

    // Init ESPNow
    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESPNow init failed!");
        while (1) delay(1000);
    }

    // Add broadcast peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
    peerInfo.channel = 0; // Use current channel
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("ERROR: Failed to add broadcast peer!");
        while (1) delay(1000);
    }

    Serial.println("ESPNow initialized, broadcasting on all channels");

    // Init VictronBLE
    if (!victron.begin(5)) {
        Serial.println("ERROR: Failed to initialize VictronBLE!");
        Serial.println(victron.getLastError());
        while (1) delay(1000);
    }

    victron.setDebug(false);
    victron.setCallback(&callback);

    // Add your devices here
    victron.addDevice(
        "Rainbow48V",
        "E4:05:42:34:14:F3",
        "0ec3adf7433dd61793ff2f3b8ad32ed8",
        DEVICE_TYPE_SOLAR_CHARGER
    );

    victron.addDevice(
        "ScottTrailer",
        "e64559783cfb",
        "3fa658aded4f309b9bc17a2318cb1f56",
        DEVICE_TYPE_SOLAR_CHARGER
    );

    Serial.println("Configured " + String(victron.getDeviceCount()) + " BLE devices");
    Serial.println("Packet size: " + String(sizeof(SolarChargerPacket)) + " bytes\n");
}

void loop() {
    victron.loop();

    // Send cached packets every 30 seconds
    unsigned long now = millis();
    if (now - lastSendTime >= SEND_INTERVAL_MS) {
        lastSendTime = now;

        int sent = 0;
        for (int i = 0; i < cachedCount; i++) {
            if (!cachedValid[i]) continue;

            esp_err_t result = esp_now_send(BROADCAST_ADDR,
                reinterpret_cast<const uint8_t*>(&cachedPackets[i]),
                sizeof(SolarChargerPacket));

            if (result == ESP_OK) {
                sendCount++;
                sent++;
                Serial.printf("[ESPNow] Sent %s: %.2fV %.1fA PV:%.1fV %.0fW State:%d\n",
                    cachedPackets[i].deviceName,
                    cachedPackets[i].batteryVoltage,
                    cachedPackets[i].batteryCurrent,
                    cachedPackets[i].panelVoltage,
                    cachedPackets[i].panelPower,
                    cachedPackets[i].chargeState);
            } else {
                sendFailCount++;
                Serial.printf("[ESPNow] FAIL sending %s (err=%d)\n",
                    cachedPackets[i].deviceName, result);
            }
        }

        Serial.printf("[Stats] BLE pkts:%lu  ESPNow sent:%lu fail:%lu  devices:%d\n",
            blePacketCount, sendCount, sendFailCount, cachedCount);
    }

    delay(100);
}

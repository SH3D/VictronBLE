/**
 * VictronBLE Repeater Example
 *
 * Collects Solar Charger data via BLE and forwards every packet
 * over ESPNow broadcast. Place this ESP32 near Victron devices and
 * use a separate Receiver ESP32 at a distance.
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

static uint32_t sendCount = 0;
static uint32_t sendFailCount = 0;

VictronBLE victron;

class RepeaterCallback : public VictronDeviceCallback {
public:
    void onSolarChargerData(const SolarChargerData& data) override {
        SolarChargerPacket pkt;
        pkt.chargeState = static_cast<uint8_t>(data.chargeState);
        pkt.batteryVoltage = data.batteryVoltage;
        pkt.batteryCurrent = data.batteryCurrent;
        pkt.panelVoltage = data.panelVoltage;
        pkt.panelPower = data.panelPower;
        pkt.yieldToday = data.yieldToday;
        pkt.loadCurrent = data.loadCurrent;
        pkt.rssi = data.rssi;

        // Copy device name, truncate to fit
        memset(pkt.deviceName, 0, sizeof(pkt.deviceName));
        strncpy(pkt.deviceName, data.deviceName.c_str(), sizeof(pkt.deviceName) - 1);

        esp_err_t result = esp_now_send(BROADCAST_ADDR,
                                        reinterpret_cast<const uint8_t*>(&pkt),
                                        sizeof(pkt));

        sendCount++;
        if (result != ESP_OK) {
            sendFailCount++;
            Serial.println("ESPNow send failed: " + String(esp_err_to_name(result)));
        } else {
            Serial.println("[TX] " + String(pkt.deviceName) +
                           " Batt:" + String(pkt.batteryVoltage, 2) + "V" +
                           " PV:" + String(pkt.panelPower, 0) + "W" +
                           " (sent:" + String(sendCount) +
                           " fail:" + String(sendFailCount) + ")");
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
    delay(100);
}

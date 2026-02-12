/**
 * VictronBLE Logger Example
 *
 * Demonstrates change-detection logging for Solar Charger data.
 * Only logs to serial when a value changes (ignoring RSSI), or once
 * per minute if nothing has changed. This keeps serial output quiet
 * and is useful for long-running monitoring / data logging.
 *
 * Setup:
 * 1. Get your device encryption keys from the VictronConnect app
 * 2. Update the device configurations below with your MAC and key
 */

#include <Arduino.h>
#include "VictronBLE.h"

VictronBLE victron;

// Tracks last-logged values per device for change detection
struct SolarChargerSnapshot {
    bool valid = false;
    SolarChargerState chargeState;
    float batteryVoltage;
    float batteryCurrent;
    float panelVoltage;
    float panelPower;
    uint16_t yieldToday;
    float loadCurrent;
    unsigned long lastLogTime = 0;
    uint32_t packetsSinceLastLog = 0;
};

// Store a snapshot per device (index by MAC string)
static const int MAX_DEVICES = 4;
static String deviceMACs[MAX_DEVICES];
static SolarChargerSnapshot snapshots[MAX_DEVICES];
static int deviceCount = 0;

static const unsigned long LOG_INTERVAL_MS = 60000; // 1 minute

static int findOrAddDevice(const String& mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (deviceMACs[i] == mac) return i;
    }
    if (deviceCount < MAX_DEVICES) {
        deviceMACs[deviceCount] = mac;
        return deviceCount++;
    }
    return -1;
}

static String chargeStateName(SolarChargerState state) {
    switch (state) {
        case CHARGER_OFF:              return "Off";
        case CHARGER_LOW_POWER:        return "Low Power";
        case CHARGER_FAULT:            return "Fault";
        case CHARGER_BULK:             return "Bulk";
        case CHARGER_ABSORPTION:       return "Absorption";
        case CHARGER_FLOAT:            return "Float";
        case CHARGER_STORAGE:          return "Storage";
        case CHARGER_EQUALIZE:         return "Equalize";
        case CHARGER_INVERTING:        return "Inverting";
        case CHARGER_POWER_SUPPLY:     return "Power Supply";
        case CHARGER_EXTERNAL_CONTROL: return "External Control";
        default:                       return "Unknown";
    }
}

static void logData(const SolarChargerData& data, const char* reason, uint32_t packets) {
    Serial.println("[" + data.deviceName + "] " + reason +
                   " pkts:" + String(packets) +
                   " | State:" + chargeStateName(data.chargeState) +
                   " Batt:" + String(data.batteryVoltage, 2) + "V" +
                   " " + String(data.batteryCurrent, 2) + "A" +
                   " PV:" + String(data.panelVoltage, 1) + "V" +
                   " " + String(data.panelPower, 0) + "W" +
                   " Yield:" + String(data.yieldToday) + "Wh" +
                   (data.loadCurrent > 0 ? " Load:" + String(data.loadCurrent, 2) + "A" : ""));
}

class LoggerCallback : public VictronDeviceCallback {
public:
    void onSolarChargerData(const SolarChargerData& data) override {
        int idx = findOrAddDevice(data.macAddress);
        if (idx < 0) return;

        SolarChargerSnapshot& prev = snapshots[idx];
        unsigned long now = millis();
        prev.packetsSinceLastLog++;

        if (!prev.valid) {
            // First reading - always log
            logData(data, "INIT", prev.packetsSinceLastLog);
        } else {
            // Check for changes (everything except RSSI)
            bool changed = false;
            if (prev.chargeState != data.chargeState)       changed = true;
            if (prev.batteryVoltage != data.batteryVoltage) changed = true;
            if (prev.batteryCurrent != data.batteryCurrent) changed = true;
            if (prev.panelVoltage != data.panelVoltage)     changed = true;
            if (prev.panelPower != data.panelPower)         changed = true;
            if (prev.yieldToday != data.yieldToday)         changed = true;
            if (prev.loadCurrent != data.loadCurrent)       changed = true;

            if (changed) {
                logData(data, "CHG", prev.packetsSinceLastLog);
            } else if (now - prev.lastLogTime >= LOG_INTERVAL_MS) {
                logData(data, "HEARTBEAT", prev.packetsSinceLastLog);
            } else {
                return; // Nothing to log
            }
        }

        // Update snapshot
        prev.packetsSinceLastLog = 0;
        prev.valid = true;
        prev.chargeState = data.chargeState;
        prev.batteryVoltage = data.batteryVoltage;
        prev.batteryCurrent = data.batteryCurrent;
        prev.panelVoltage = data.panelVoltage;
        prev.panelPower = data.panelPower;
        prev.yieldToday = data.yieldToday;
        prev.loadCurrent = data.loadCurrent;
        prev.lastLogTime = now;
    }
};

LoggerCallback callback;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== VictronBLE Logger Example ===\n");

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

    Serial.println("Configured " + String(victron.getDeviceCount()) + " devices");
    Serial.println("Logging on change, or every 60s heartbeat\n");
}

void loop() {
    victron.loop();
    delay(100);
}

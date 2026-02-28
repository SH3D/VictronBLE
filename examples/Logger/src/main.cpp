/**
 * VictronBLE Logger Example
 *
 * Demonstrates change-detection logging for Solar Charger data.
 * Only logs to serial when a value changes (ignoring RSSI), or once
 * per minute if nothing has changed.
 *
 * Setup:
 * 1. Get your device encryption keys from the VictronConnect app
 * 2. Update the device configurations below with your MAC and key
 */

#include <Arduino.h>
#include "VictronBLE.h"

VictronBLE victron;

struct SolarChargerSnapshot {
    bool valid = false;
    uint8_t chargeState;
    float batteryVoltage;
    float batteryCurrent;
    float panelPower;
    uint16_t yieldToday;
    float loadCurrent;
    unsigned long lastLogTime = 0;
    uint32_t packetsSinceLastLog = 0;
};

static const int MAX_DEVICES = 4;
static char deviceMACs[MAX_DEVICES][VICTRON_MAC_LEN];
static SolarChargerSnapshot snapshots[MAX_DEVICES];
static int deviceCount = 0;

static const unsigned long LOG_INTERVAL_MS = 60000;

static int findOrAddDevice(const char* mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(deviceMACs[i], mac) == 0) return i;
    }
    if (deviceCount < MAX_DEVICES) {
        strncpy(deviceMACs[deviceCount], mac, VICTRON_MAC_LEN - 1);
        deviceMACs[deviceCount][VICTRON_MAC_LEN - 1] = '\0';
        return deviceCount++;
    }
    return -1;
}

static const char* chargeStateName(uint8_t state) {
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

static void logData(const VictronDevice* dev, const VictronSolarData& s,
                    const char* reason, uint32_t packets) {
    Serial.printf("[%s] %s pkts:%lu | State:%s Batt:%.2fV %.2fA PV:%.0fW Yield:%uWh",
                  dev->name, reason, packets,
                  chargeStateName(s.chargeState),
                  s.batteryVoltage, s.batteryCurrent,
                  s.panelPower, s.yieldToday);
    if (s.loadCurrent > 0)
        Serial.printf(" Load:%.2fA", s.loadCurrent);
    Serial.println();
}

void onVictronData(const VictronDevice* dev) {
    if (dev->deviceType != DEVICE_TYPE_SOLAR_CHARGER) return;
    const auto& s = dev->solar;

    int idx = findOrAddDevice(dev->mac);
    if (idx < 0) return;

    SolarChargerSnapshot& prev = snapshots[idx];
    unsigned long now = millis();
    prev.packetsSinceLastLog++;

    if (!prev.valid) {
        logData(dev, s, "INIT", prev.packetsSinceLastLog);
    } else {
        bool changed = (prev.chargeState != s.chargeState) ||
                       (prev.batteryVoltage != s.batteryVoltage) ||
                       (prev.batteryCurrent != s.batteryCurrent) ||
                       (prev.panelPower != s.panelPower) ||
                       (prev.yieldToday != s.yieldToday) ||
                       (prev.loadCurrent != s.loadCurrent);

        if (changed) {
            logData(dev, s, "CHG", prev.packetsSinceLastLog);
        } else if (now - prev.lastLogTime >= LOG_INTERVAL_MS) {
            logData(dev, s, "HEARTBEAT", prev.packetsSinceLastLog);
        } else {
            return;
        }
    }

    prev.packetsSinceLastLog = 0;
    prev.valid = true;
    prev.chargeState = s.chargeState;
    prev.batteryVoltage = s.batteryVoltage;
    prev.batteryCurrent = s.batteryCurrent;
    prev.panelPower = s.panelPower;
    prev.yieldToday = s.yieldToday;
    prev.loadCurrent = s.loadCurrent;
    prev.lastLogTime = now;
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== VictronBLE Logger Example ===\n");

    if (!victron.begin(5)) {
        Serial.println("ERROR: Failed to initialize VictronBLE!");
        while (1) delay(1000);
    }

    victron.setDebug(false);
    victron.setCallback(onVictronData);

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

    Serial.printf("Configured %d devices\n", (int)victron.getDeviceCount());
    Serial.println("Logging on change, or every 60s heartbeat\n");
}

void loop() {
    victron.loop();
}

/**
 * VictronBLE Multi-Device Example
 *
 * Demonstrates reading data from multiple Victron device types via BLE.
 *
 * The same sketch runs on both ESP32 and nRF52840 — the BLE backend is selected
 * automatically at compile time. Pick the target with the PlatformIO
 * environment (see platformio.ini): e.g. `esp32dev` or `xiao_nrf52840`.
 *
 * Setup:
 * 1. Get your device encryption keys from the VictronConnect app
 *    (Settings > Product Info > Instant readout via Bluetooth > Show)
 * 2. Update the device configurations below with your MAC and key.
 */

#include <Arduino.h>
#include "VictronBLE.h"

VictronBLE victron;

static uint32_t solarChargerCount = 0;
static uint32_t batteryMonitorCount = 0;
static uint32_t inverterCount = 0;
static uint32_t dcdcConverterCount = 0;
static uint32_t acChargerCount = 0;

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

void onVictronData(const VictronDevice* dev) {
    switch (dev->deviceType) {
        case DEVICE_TYPE_SOLAR_CHARGER: {
            const auto& s = dev->solar;
            solarChargerCount++;
            Serial.printf("\n=== Solar Charger: %s (#%lu) ===\n", dev->name, solarChargerCount);
            Serial.printf("MAC: %s\n", dev->mac);
            Serial.printf("RSSI: %d dBm\n", dev->rssi);
            Serial.printf("State: %s\n", chargeStateName(s.chargeState));
            Serial.printf("Battery: %.2f V\n", s.batteryVoltage);
            Serial.printf("Current: %.2f A\n", s.batteryCurrent);
            Serial.printf("Panel Power: %.0f W\n", s.panelPower);
            Serial.printf("Yield Today: %u Wh\n", s.yieldToday);
            if (s.loadCurrent > 0)
                Serial.printf("Load Current: %.2f A\n", s.loadCurrent);
            Serial.printf("Last Update: %lus ago\n", (millis() - dev->lastUpdate) / 1000);
            break;
        }
        case DEVICE_TYPE_BATTERY_MONITOR: {
            const auto& b = dev->battery;
            batteryMonitorCount++;
            Serial.printf("\n=== Battery Monitor: %s (#%lu) ===\n", dev->name, batteryMonitorCount);
            Serial.printf("MAC: %s\n", dev->mac);
            Serial.printf("RSSI: %d dBm\n", dev->rssi);
            Serial.printf("Voltage: %.2f V\n", b.voltage);
            Serial.printf("Current: %.2f A\n", b.current);
            Serial.printf("SOC: %.1f %%\n", b.soc);
            Serial.printf("Consumed: %.2f Ah\n", b.consumedAh);
            if (b.remainingMinutes < 65535)
                Serial.printf("Time Remaining: %dh %dm\n", b.remainingMinutes / 60, b.remainingMinutes % 60);
            if (b.temperature > 0)
                Serial.printf("Temperature: %.1f C\n", b.temperature);
            if (b.auxVoltage > 0)
                Serial.printf("Aux Voltage: %.2f V\n", b.auxVoltage);
            if (b.alarmLowVoltage || b.alarmHighVoltage || b.alarmLowSOC ||
                b.alarmLowTemperature || b.alarmHighTemperature) {
                Serial.print("ALARMS:");
                if (b.alarmLowVoltage) Serial.print(" LOW-V");
                if (b.alarmHighVoltage) Serial.print(" HIGH-V");
                if (b.alarmLowSOC) Serial.print(" LOW-SOC");
                if (b.alarmLowTemperature) Serial.print(" LOW-TEMP");
                if (b.alarmHighTemperature) Serial.print(" HIGH-TEMP");
                Serial.println();
            }
            Serial.printf("Last Update: %lus ago\n", (millis() - dev->lastUpdate) / 1000);
            break;
        }
        case DEVICE_TYPE_INVERTER: {
            const auto& inv = dev->inverter;
            inverterCount++;
            Serial.printf("\n=== Inverter/Charger: %s (#%lu) ===\n", dev->name, inverterCount);
            Serial.printf("MAC: %s\n", dev->mac);
            Serial.printf("RSSI: %d dBm\n", dev->rssi);
            Serial.printf("Battery: %.2f V\n", inv.batteryVoltage);
            Serial.printf("Current: %.2f A\n", inv.batteryCurrent);
            Serial.printf("AC Power: %.0f W\n", inv.acPower);
            Serial.printf("State: %d\n", inv.state);
            if (inv.alarmLowVoltage || inv.alarmHighVoltage ||
                inv.alarmHighTemperature || inv.alarmOverload) {
                Serial.print("ALARMS:");
                if (inv.alarmLowVoltage) Serial.print(" LOW-V");
                if (inv.alarmHighVoltage) Serial.print(" HIGH-V");
                if (inv.alarmHighTemperature) Serial.print(" TEMP");
                if (inv.alarmOverload) Serial.print(" OVERLOAD");
                Serial.println();
            }
            Serial.printf("Last Update: %lus ago\n", (millis() - dev->lastUpdate) / 1000);
            break;
        }
        case DEVICE_TYPE_DCDC_CONVERTER: {
            const auto& dc = dev->dcdc;
            dcdcConverterCount++;
            Serial.printf("\n=== DC-DC Converter: %s (#%lu) ===\n", dev->name, dcdcConverterCount);
            Serial.printf("MAC: %s\n", dev->mac);
            Serial.printf("RSSI: %d dBm\n", dev->rssi);
            Serial.printf("Input: %.2f V\n", dc.inputVoltage);
            Serial.printf("Output: %.2f V\n", dc.outputVoltage);
            Serial.printf("Current: %.2f A\n", dc.outputCurrent);
            Serial.printf("State: %d\n", dc.chargeState);
            if (dc.errorCode != 0)
                Serial.printf("Error Code: %d\n", dc.errorCode);
            Serial.printf("Last Update: %lus ago\n", (millis() - dev->lastUpdate) / 1000);
            break;
        }
        case DEVICE_TYPE_AC_CHARGER: {
            const auto& ac = dev->acCharger;
            acChargerCount++;
            Serial.printf("\n=== AC Charger: %s (#%lu) ===\n", dev->name, acChargerCount);
            Serial.printf("MAC: %s\n", dev->mac);
            Serial.printf("RSSI: %d dBm\n", dev->rssi);
            Serial.printf("State: %s\n", chargeStateName(ac.chargeState));
            Serial.printf("Output 1: %.2f V  %.2f A\n", ac.voltage1, ac.current1);
            if (ac.voltage2 > 0) Serial.printf("Output 2: %.2f V  %.2f A\n", ac.voltage2, ac.current2);
            if (ac.voltage3 > 0) Serial.printf("Output 3: %.2f V  %.2f A\n", ac.voltage3, ac.current3);
            if (ac.temperature != 0) Serial.printf("Temperature: %.0f C\n", ac.temperature);
            if (ac.acCurrent > 0) Serial.printf("AC Current: %.2f A\n", ac.acCurrent);
            Serial.printf("Last Update: %lus ago\n", (millis() - dev->lastUpdate) / 1000);
            break;
        }
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    // Wait briefly for USB CDC serial (nRF52/native-USB boards); don't block forever
    uint32_t start = millis();
    while (!Serial && (millis() - start) < 5000) delay(10);

    Serial.println("\n\n=================================");
    Serial.println("VictronBLE Multi-Device Example");
    Serial.println("=================================\n");

    if (!victron.begin(5)) {
        Serial.println("ERROR: Failed to initialize VictronBLE!");
        while (1) delay(1000);
    }

    victron.setDebug(true);
    victron.setCallback(onVictronData);

    // Replace with your own devices (MAC + 32-char hex key from VictronConnect)
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
    Serial.println("\nStarting BLE scan...\n");
}

static uint32_t loopCount = 0;
static uint32_t lastReport = 0;

void loop() {
    victron.loop();  // Non-blocking on both backends
    loopCount++;

    uint32_t now = millis();
    if (now - lastReport >= 10000) {
        Serial.printf("Loop iterations in last 10s: %lu\n", loopCount);
        loopCount = 0;
        lastReport = now;
    }
}

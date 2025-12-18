/**
 * VictronBLE Example
 *
 * This example demonstrates how to use the VictronBLE library to read data
 * from multiple Victron devices simultaneously.
 *
 * Hardware Requirements:
 * - ESP32 board
 * - Victron devices with BLE (SmartSolar, SmartShunt, etc.)
 *
 * Setup:
 * 1. Get your device encryption keys from the VictronConnect app:
 *    - Open VictronConnect
 *    - Connect to your device
 *    - Go to Settings > Product Info
 *    - Enable "Instant readout via Bluetooth"
 *    - Click "Show" next to "Instant readout details"
 *    - Copy the encryption key (32 hex characters)
 *
 * 2. Update the device configurations below with your devices' MAC addresses
 *    and encryption keys
 */

#include <Arduino.h>
#include "VictronBLE.h"

// Create VictronBLE instance
VictronBLE victron;

// Device callback class - gets called when new data arrives
class MyVictronCallback : public VictronDeviceCallback {
public:
    void onSolarChargerData(const SolarChargerData& data) override {
        Serial.println("\n=== Solar Charger: " + data.deviceName + " ===");
        Serial.println("MAC: " + data.macAddress);
        Serial.println("RSSI: " + String(data.rssi) + " dBm");
        Serial.println("State: " + getChargeStateName(data.chargeState));
        Serial.println("Battery: " + String(data.batteryVoltage, 2) + " V");
        Serial.println("Current: " + String(data.batteryCurrent, 2) + " A");
        Serial.println("Panel Voltage: " + String(data.panelVoltage, 1) + " V");
        Serial.println("Panel Power: " + String(data.panelPower) + " W");
        Serial.println("Yield Today: " + String(data.yieldToday) + " Wh");
        if (data.loadCurrent > 0) {
            Serial.println("Load Current: " + String(data.loadCurrent, 2) + " A");
        }
        Serial.println("Last Update: " + String((millis() - data.lastUpdate) / 1000) + "s ago");
    }

    void onBatteryMonitorData(const BatteryMonitorData& data) override {
        Serial.println("\n=== Battery Monitor: " + data.deviceName + " ===");
        Serial.println("MAC: " + data.macAddress);
        Serial.println("RSSI: " + String(data.rssi) + " dBm");
        Serial.println("Voltage: " + String(data.voltage, 2) + " V");
        Serial.println("Current: " + String(data.current, 2) + " A");
        Serial.println("SOC: " + String(data.soc, 1) + " %");
        Serial.println("Consumed: " + String(data.consumedAh, 2) + " Ah");

        if (data.remainingMinutes < 65535) {
            int hours = data.remainingMinutes / 60;
            int mins = data.remainingMinutes % 60;
            Serial.println("Time Remaining: " + String(hours) + "h " + String(mins) + "m");
        }

        if (data.temperature > 0) {
            Serial.println("Temperature: " + String(data.temperature, 1) + " °C");
        }
        if (data.auxVoltage > 0) {
            Serial.println("Aux Voltage: " + String(data.auxVoltage, 2) + " V");
        }

        // Print alarms
        if (data.alarmLowVoltage || data.alarmHighVoltage || data.alarmLowSOC ||
            data.alarmLowTemperature || data.alarmHighTemperature) {
            Serial.print("ALARMS: ");
            if (data.alarmLowVoltage) Serial.print("LOW-V ");
            if (data.alarmHighVoltage) Serial.print("HIGH-V ");
            if (data.alarmLowSOC) Serial.print("LOW-SOC ");
            if (data.alarmLowTemperature) Serial.print("LOW-TEMP ");
            if (data.alarmHighTemperature) Serial.print("HIGH-TEMP ");
            Serial.println();
        }

        Serial.println("Last Update: " + String((millis() - data.lastUpdate) / 1000) + "s ago");
    }

    void onInverterData(const InverterData& data) override {
        Serial.println("\n=== Inverter/Charger: " + data.deviceName + " ===");
        Serial.println("MAC: " + data.macAddress);
        Serial.println("RSSI: " + String(data.rssi) + " dBm");
        Serial.println("Battery: " + String(data.batteryVoltage, 2) + " V");
        Serial.println("Current: " + String(data.batteryCurrent, 2) + " A");
        Serial.println("AC Power: " + String(data.acPower) + " W");
        Serial.println("State: " + String(data.state));

        // Print alarms
        if (data.alarmLowVoltage || data.alarmHighVoltage ||
            data.alarmHighTemperature || data.alarmOverload) {
            Serial.print("ALARMS: ");
            if (data.alarmLowVoltage) Serial.print("LOW-V ");
            if (data.alarmHighVoltage) Serial.print("HIGH-V ");
            if (data.alarmHighTemperature) Serial.print("TEMP ");
            if (data.alarmOverload) Serial.print("OVERLOAD ");
            Serial.println();
        }

        Serial.println("Last Update: " + String((millis() - data.lastUpdate) / 1000) + "s ago");
    }

    void onDCDCConverterData(const DCDCConverterData& data) override {
        Serial.println("\n=== DC-DC Converter: " + data.deviceName + " ===");
        Serial.println("MAC: " + data.macAddress);
        Serial.println("RSSI: " + String(data.rssi) + " dBm");
        Serial.println("Input: " + String(data.inputVoltage, 2) + " V");
        Serial.println("Output: " + String(data.outputVoltage, 2) + " V");
        Serial.println("Current: " + String(data.outputCurrent, 2) + " A");
        Serial.println("State: " + String(data.chargeState));
        if (data.errorCode != 0) {
            Serial.println("Error Code: " + String(data.errorCode));
        }
        Serial.println("Last Update: " + String((millis() - data.lastUpdate) / 1000) + "s ago");
    }

private:
    String getChargeStateName(SolarChargerState state) {
        switch (state) {
            case CHARGER_OFF: return "Off";
            case CHARGER_LOW_POWER: return "Low Power";
            case CHARGER_FAULT: return "Fault";
            case CHARGER_BULK: return "Bulk";
            case CHARGER_ABSORPTION: return "Absorption";
            case CHARGER_FLOAT: return "Float";
            case CHARGER_STORAGE: return "Storage";
            case CHARGER_EQUALIZE: return "Equalize";
            case CHARGER_INVERTING: return "Inverting";
            case CHARGER_POWER_SUPPLY: return "Power Supply";
            case CHARGER_EXTERNAL_CONTROL: return "External Control";
            default: return "Unknown";
        }
    }
};

MyVictronCallback callback;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=================================");
    Serial.println("VictronBLE Multi-Device Example");
    Serial.println("=================================\n");

    // Initialize VictronBLE with 5 second scan duration
    if (!victron.begin(5)) {
        Serial.println("ERROR: Failed to initialize VictronBLE!");
        Serial.println(victron.getLastError());
        while (1) delay(1000);
    }

    // Enable debug output (optional)
    victron.setDebug(true);

    // Set callback for data updates
    victron.setCallback(&callback);

    // Add your devices here
    // Replace with your actual MAC addresses and encryption keys

    // Temporary - Scott Example
    victron.addDevice(
        "Rainbow48V",                              // Device name
        "E4:05:42:34:14:F3",                        // MAC address
        "0ec3adf7433dd61793ff2f3b8ad32ed8",         // Encryption key (32 hex chars)
        DEVICE_TYPE_SOLAR_CHARGER                    // Device type
    );

    // Example: Solar Charger #1
    victron.addDevice(
        "MPPT 100/30",                              // Device name
        "E7:48:D4:28:B7:9C",                        // MAC address
        "0df4d0395b7d1a876c0c33ecb9e70dcd",         // Encryption key (32 hex chars)
        DEVICE_TYPE_SOLAR_CHARGER                    // Device type
    );

    // Example: Solar Charger #2
    victron.addDevice(
        "MPPT 75/15",
        "AA:BB:CC:DD:EE:FF",
        "1234567890abcdef1234567890abcdef",
        DEVICE_TYPE_SOLAR_CHARGER
    );

    // Example: Battery Monitor (SmartShunt)
    victron.addDevice(
        "SmartShunt",
        "11:22:33:44:55:66",
        "fedcba0987654321fedcba0987654321",
        DEVICE_TYPE_BATTERY_MONITOR
    );

    // Example: Inverter/Charger
    victron.addDevice(
        "MultiPlus",
        "99:88:77:66:55:44",
        "abcdefabcdefabcdefabcdefabcdefab",
        DEVICE_TYPE_INVERTER
    );

    Serial.println("Configured " + String(victron.getDeviceCount()) + " devices");
    Serial.println("\nStarting BLE scan...\n");
}

void loop() {
    // Process BLE scanning and data updates
    victron.loop();

    // Optional: You can also manually query device data
    // This is useful if you're not using callbacks
    /*
    SolarChargerData solarData;
    if (victron.getSolarChargerData("E7:48:D4:28:B7:9C", solarData)) {
        // Do something with solarData
    }

    BatteryMonitorData batteryData;
    if (victron.getBatteryMonitorData("11:22:33:44:55:66", batteryData)) {
        // Do something with batteryData
    }
    */

    // Add a small delay to avoid overwhelming the serial output
    delay(100);
}

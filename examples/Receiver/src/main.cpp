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

#ifdef USE_M5STICK
#include <M5StickC.h>
#endif

// ESPNow packet structure - must match Repeater
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

static uint32_t recvCount = 0;

#ifdef USE_M5STICK
// Display: cache latest packet per device for screen rotation
static const int MAX_DISPLAY_DEVICES = 4;
static SolarChargerPacket displayPackets[MAX_DISPLAY_DEVICES];
static bool displayValid[MAX_DISPLAY_DEVICES] = {};
static int displayCount = 0;
static int displayPage = 0;          // Which device to show
static bool displayDirty = true;
static unsigned long lastPageSwitch = 0;
static const unsigned long PAGE_SWITCH_MS = 5000; // Rotate pages every 5s

static int findOrAddDisplay(const char* name) {
    for (int i = 0; i < displayCount; i++) {
        if (strncmp(displayPackets[i].deviceName, name, 16) == 0) return i;
    }
    if (displayCount < MAX_DISPLAY_DEVICES) return displayCount++;
    return -1;
}
#endif

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

    Serial.printf("[RX #%lu] %s | State:%s Batt:%.2fV %.2fA PV:%.0fW Yield:%uWh",
                  recvCount,
                  name,
                  chargeStateName(pkt->chargeState),
                  pkt->batteryVoltage,
                  pkt->batteryCurrent,
                  pkt->panelPower,
                  pkt->yieldToday);

    if (pkt->loadCurrent > 0) {
        Serial.printf(" Load:%.2fA", pkt->loadCurrent);
    }

    Serial.printf(" RSSI:%ddBm From:%02X:%02X:%02X:%02X:%02X:%02X\n",
                  pkt->rssi,
                  senderMac[0], senderMac[1], senderMac[2],
                  senderMac[3], senderMac[4], senderMac[5]);

#ifdef USE_M5STICK
    int idx = findOrAddDisplay(name);
    if (idx >= 0) {
        displayPackets[idx] = *pkt;
        displayValid[idx] = true;
        displayDirty = true;
    }
#endif
}

void setup() {
#ifdef USE_M5STICK
    M5.begin();
    M5.Lcd.setRotation(3);  // Landscape, USB on right
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("ESPNow Receiver");
    M5.Lcd.println("Waiting...");
#endif

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
#ifdef USE_M5STICK
    M5.update();

    // Button A (front): manually cycle to next device
    if (M5.BtnA.wasPressed()) {
        if (displayCount > 0) {
            displayPage = (displayPage + 1) % displayCount;
            displayDirty = true;
            lastPageSwitch = millis();
        }
    }

    // Auto-rotate pages every 5 seconds if multiple devices
    if (displayCount > 1) {
        unsigned long now = millis();
        if (now - lastPageSwitch >= PAGE_SWITCH_MS) {
            lastPageSwitch = now;
            displayPage = (displayPage + 1) % displayCount;
            displayDirty = true;
        }
    }

    // Redraw screen when data changes or page switches
    if (displayDirty && displayCount > 0) {
        displayDirty = false;

        int p = displayPage % displayCount;
        if (!displayValid[p]) { delay(100); return; }

        const auto& pkt = displayPackets[p];

        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);

        // Row 0: device name + page indicator
        M5.Lcd.setTextColor(CYAN, BLACK);
        M5.Lcd.printf("%s", pkt.deviceName);
        if (displayCount > 1) {
            M5.Lcd.printf(" [%d/%d]", p + 1, displayCount);
        }
        M5.Lcd.println();

        // Row 1: charge state
        M5.Lcd.setTextColor(YELLOW, BLACK);
        M5.Lcd.printf("State: %s\n", chargeStateName(pkt.chargeState));

        // Row 2: battery voltage + current (large-ish)
        M5.Lcd.setTextColor(GREEN, BLACK);
        M5.Lcd.setTextSize(2);
        M5.Lcd.printf("%.2fV\n", pkt.batteryVoltage);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(WHITE, BLACK);
        M5.Lcd.printf("Batt: %.2fA\n", pkt.batteryCurrent);

        // Row 3: PV
        M5.Lcd.printf("PV: %.0fW\n", pkt.panelPower);

        // Row 4: yield + load
        M5.Lcd.printf("Yield: %uWh", pkt.yieldToday);
        if (pkt.loadCurrent > 0) {
            M5.Lcd.printf("  Ld:%.1fA", pkt.loadCurrent);
        }
        M5.Lcd.println();

        // Row 5: stats
        M5.Lcd.setTextColor(DARKGREY, BLACK);
        M5.Lcd.printf("RSSI:%d  RX:%lu", pkt.rssi, recvCount);
    }
#endif

    delay(100);
}

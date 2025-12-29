/*

Scott's original test code - this does work for MPPT chargers - use it as a base

*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>
#include <BLEScan.h>
#include <aes/esp_aes.h>  // AES decryption

typedef struct {
  char charMacAddr[13];       // 12 character MAC + \0 (initialized as quoted strings below for convenience)
  char charKey[33];           // 32 character keys + \0 (initialized as quoted strings below for convenience)
  char comment[16];           // 16 character comment (name) for printing during setup()
  byte byteMacAddr[6];        // 6 bytes for MAC - initialized by setup() from quoted strings
  byte byteKey[16];           // 16 bytes for encryption key - initialized by setup() from quoted strings
  char cachedDeviceName[32];  // 31 characters + \0 (filled in as we receive advertisements)
} solarController;

solarController solarControllers[] = {
  // { { .charMacAddr = "e64559783cfb" }, { .charKey = "3fa658aded4f309b9bc17a2318cb1f56" }, { .comment = "ScottTrailer" } },
  { { .charMacAddr = "e405423414f3" }, { .charKey = "0ec3adf7433dd61793ff2f3b8ad32ed8" }, { .comment = "Test" } },
};
int  knownSolarControllerCount = sizeof(solarControllers) / sizeof(solarControllers[0]);

BLEScan *pBLEScan;

#define AES_KEY_BITS 128
int scanTime = 1;  //In seconds

byte hexCharToByte(char hexChar) {
  if (hexChar >= '0' && hexChar <='9') {          // 0-9
    hexChar=hexChar - '0';
  }
  else if (hexChar >= 'a' && hexChar <= 'f') {   // a-f
    hexChar=hexChar - 'a' + 10;
  }
  else if (hexChar >= 'A' && hexChar <= 'F') {  // A-F
    hexChar=hexChar - 'A' + 10;
  }
  else {
    hexChar=255;
  }

  return hexChar;
}

// Decryption keys and MAC addresses obtained from the VictronConnect app will be
// a string of hex digits like this:
//
//   f4116784732a
//   dc73cb155351cf950f9f3a958b5cd96f
//
// Split that up and turn it into an array whose equivalent definition would be like this:
//
//   byte key[]={ 0xdc, 0x73, 0xcb, ... 0xd9, 0x6f };
//
void hexCharStrToByteArray(char * hexCharStr, byte * byteArray) {
  bool returnVal=false;

  int hexCharStrLength=strlen(hexCharStr);

  // There are simpler ways of doing this without the fancy nibble-munching,
  // but I do it this way so I parse things like colon-separated MAC addresses.
  // BUT: be aware that this expects digits in pairs and byte values need to be
  // zero-filled. i.e., a MAC address like 8:0:2b:xx:xx:xx won't come out the way
  // you want it.
  int byteArrayIndex=0;
  bool oddByte=true;
  byte hiNibble;
  for (int i=0; i<hexCharStrLength; i++) {
    byte nibble=hexCharToByte(hexCharStr[i]);
    if (nibble!=255) {
      if (oddByte) {
        hiNibble=nibble;
      } else {
        byteArray[byteArrayIndex]=(hiNibble<<4) | nibble;
        byteArrayIndex++;
      }
      oddByte=!oddByte;
    }
  }
  // do we have a leftover nibble? I guess we'll assume it's a low nibble?
  if (! oddByte) {
    byteArray[byteArrayIndex]=hiNibble;
  }
}

// Victron docs on the manufacturer data in advertisement packets can be found at:
//   https://community.victronenergy.com/storage/attachments/48745-extra-manufacturer-data-2022-12-14.pdf
//

// Usage/style note: I use uint16_t in places where I need to force 16-bit unsigned integers
// instead of whatever the compiler/architecture might decide to use. I might not need to do
// the same with byte variables, but I'll do it anyway just to be at least a little consistent.

// Must use the "packed" attribute to make sure the compiler doesn't add any padding to deal with
// word alignment.
typedef struct {
  uint8_t deviceState;
  uint8_t errorCode;
  int16_t batteryVoltage;
  int16_t batteryCurrent;
  uint16_t todayYield;
  uint16_t inputPower;
  uint8_t outputCurrentLo;  // Low 8 bits of output current (in 0.1 Amp increments)
  uint8_t outputCurrentHi;  // High 1 bit of ourput current (must mask off unused bits)
  uint8_t unused[4];
} __attribute__((packed)) victronPanelData;

typedef struct {
  uint16_t vendorID;                 // vendor ID
  uint8_t beaconType;                // Should be 0x10 (Product Advertisement) for the packets we want
  uint8_t unknownData1[3];           // Unknown data
  uint8_t victronRecordType;         // Should be 0x01 (Solar Charger) for the packets we want
  uint16_t nonceDataCounter;         // Nonce
  uint8_t encryptKeyMatch;           // Should match pre-shared encryption key byte 0
  uint8_t victronEncryptedData[21];  // (31 bytes max per BLE spec - size of previous elements)
  uint8_t nullPad;                   // extra byte because toCharArray() adds a \0 byte.
} __attribute__((packed)) victronManufacturerData;

int bestRSSI = -200;
int selectedSolarControllerIndex = -1;

time_t lastLEDBlinkTime=0;
time_t lastTick=0;
int displayRotation=3;
bool packetReceived=false;

char chargeStateNames[][6] = {
  "  off",
  "   1?",
  "   2?",
  " bulk",
  "  abs",
  "float",
  "   6?",
  "equal"
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {

      #define manDataSizeMax 31     // BLE specs say no more than 31 bytes, but see comments below!

      // See if we have manufacturer data and then look to see if it's coming from a Victron device.
      if (advertisedDevice.haveManufacturerData() == true) {
        uint8_t manCharBuf[manDataSizeMax+1];
        std::string manData = advertisedDevice.getManufacturerData(); // lib code returns std::string
        int manDataSize=manData.length(); // This does not include a null at the end.

        Serial.printf("Manufacturer data lengt=%d\n", manData.length());
        Serial.printf("Struct Size=%d\n", sizeof(victronManufacturerData));

        // Limit size just in case we get a malformed packet.
        if (manDataSize > manDataSizeMax) {
          Serial.printf("  Note: Truncating malformed %2d byte manufacturer data to max %d byte array size\n",manDataSize,manDataSizeMax);
          manDataSize=manDataSizeMax;
        }
        // Now copy the data from the String to a byte array. Must have the +1 so we
        // don't lose the last character to the null terminator.
        manData.copy((char *)manCharBuf, manDataSize + 1);

        // Now let's use a struct to get to the data more cleanly.
        victronManufacturerData * vicData=(victronManufacturerData *)manCharBuf;

        // ignore this packet if the Vendor ID isn't Victron.
        if (vicData->vendorID!=0x02e1) {
          return;
        }

        // ignore this packet if it isn't type 0x01 (Solar Charger).
        if (vicData->victronRecordType != 0x01) {
          return;
        }

        // Get the MAC address of the device we're hearing, and then use that to look up the encryption key
        // for the device.
        //
        // We go through a bit of trouble here to turn the String MAC address that we get from the BLE
        // code ("08:00:2b:xx:xx:xx") into a byte array. I'm divided on this... I could have just (and still might!)
        // left this as a string and just done a strcmp() match. This would have saved me some coding and execution time
        // in exchange for having to format the MAC addresses in my solarControllers list using the embedded colons.
        char receivedMacStr[18];
        strcpy(receivedMacStr,advertisedDevice.getAddress().toString().c_str());

        byte receivedMacByte[6];
        hexCharStrToByteArray(receivedMacStr,receivedMacByte);

        int solarControllerIndex=-1;
        for (int trySolarControllerIndex=0; trySolarControllerIndex<knownSolarControllerCount; trySolarControllerIndex++) {
          bool matchedMac=true;
          for (int i=0; i<6; i++) {
            if (receivedMacByte[i] != solarControllers[trySolarControllerIndex].byteMacAddr[i]) {
              matchedMac=false;
              break;
            }
          }
          if (matchedMac) {
            solarControllerIndex=trySolarControllerIndex;
            break;
          }
        }

        // Get the device name (if there's one in this packet).
        char deviceName[32]; // 31 characters + \0
        strcpy(deviceName,"(unknown device name)");
        bool deviceNameFound=false;
        if (advertisedDevice.haveName()) {
          // This works the same whether getName() returns String or std::string.
          strcpy(deviceName,advertisedDevice.getName().c_str());

          // This is prone to breaking because it's not very sophisticated. It's meant to
          // strip off "SmartSolar" if it's at the beginning of the name, but will do
          // ugly things if someone has put it elsewhere like "My SmartSolar Charger".
          if (strstr(deviceName,"SmartSolar ") == deviceName) {
            strcpy(deviceName,deviceName+11);
          }
          deviceNameFound=true;
        }

        // We didn't do this test earlier because we want to print out a name - if we got one.
        if (solarControllerIndex == -1) {
          Serial.printf("Discarding packet from unconfigured Victron SmartSolar %s at MAC %s\n",deviceName,receivedMacStr);
          return;
        }

        // If we found a device name, cache it for later display.
        if (deviceNameFound) {
          strcpy(solarControllers[solarControllerIndex].cachedDeviceName,deviceName);
        }

        // The manufacturer data from Victron contains a byte that's supposed to match the first byte
        // of the device's encryption key. If they don't match, when we don't have the right key for
        // this device and we just have to throw the data away. ALTERNATELY, we can go ahead and decrypt
        // the data - incorrectly - and use the crazy values to indicate that we have a problem.
        if (vicData->encryptKeyMatch != solarControllers[solarControllerIndex].byteKey[0]) {
          Serial.printf("Encryption key mismatch for %s at MAC %s\n",
            solarControllers[solarControllerIndex].cachedDeviceName,receivedMacStr);
          return;
        }

        // Get the signal strength (RSSI) of the beacon.
        int RSSI=advertisedDevice.getRSSI();

        // If we're showing our data on our integrated graphics hardware,
        // then show only the SmartSolar device with the strongest signal.
        // I debated on whether to do this with "#if defined..." for conditional compilation
        // or I should do this with a boolean "using graphics hardware" variable and a regular
        // "if". I decided on #if, but I might change my mind later.
          // Get the beacon's RSSI (signal strength). If it's stronger than other beacons we've received,
          // then lock on to this SmartSolar and don't display beacons from others anymore.
          if (selectedSolarControllerIndex==solarControllerIndex) {
            if (RSSI > bestRSSI) {
              bestRSSI=RSSI;
            }
          } else {
            if (RSSI > bestRSSI) {
              selectedSolarControllerIndex=solarControllerIndex;
              Serial.printf("Selected Victon SmartSolar %s at MAC %s as preferred device based on RSSI %d\n",
                solarControllers[solarControllerIndex].cachedDeviceName,receivedMacStr,RSSI);
            } else {
              Serial.printf("Discarding RSSI-based non-selected Victon SmartSolar %s at MAC %s\n",
                solarControllers[solarControllerIndex].cachedDeviceName,receivedMacStr);
              return;
            }
          }

        // Now that the packet received has met all the criteria for being displayed,
        // let's decrypt and decode the manufacturer data.

        byte inputData[16];
        byte outputData[16]={0};
        victronPanelData * victronData = (victronPanelData *) outputData;

        // The number of encrypted bytes is given by the number of bytes in the manufacturer
        // data as a while minus the number of bytes (10) in the header part of the data.
        int encrDataSize=manDataSize-10;
        for (int i=0; i<encrDataSize; i++) {
          inputData[i]=vicData->victronEncryptedData[i];   // copy for our decrypt below while I figure this out.
        }

        esp_aes_context ctx;
        esp_aes_init(&ctx);

        auto status = esp_aes_setkey(&ctx, solarControllers[solarControllerIndex].byteKey, AES_KEY_BITS);
        if (status != 0) {
          Serial.printf("  Error during esp_aes_setkey operation (%i).\n",status);
          esp_aes_free(&ctx);
          return;
        }

        byte data_counter_lsb=(vicData->nonceDataCounter) & 0xff;
        byte data_counter_msb=((vicData->nonceDataCounter) >> 8) & 0xff;
        u_int8_t nonce_counter[16] = {data_counter_lsb, data_counter_msb, 0};
        u_int8_t stream_block[16] = {0};

        size_t nonce_offset=0;
        status = esp_aes_crypt_ctr(&ctx, encrDataSize, &nonce_offset, nonce_counter, stream_block, inputData, outputData);
        if (status != 0) {
          Serial.printf("Error during esp_aes_crypt_ctr operation (%i).",status);
          esp_aes_free(&ctx);
          return;
        }
        esp_aes_free(&ctx);

        byte deviceState=victronData->deviceState;  // this is really more like "Charger State"
        byte errorCode=victronData->errorCode;
        float batteryVoltage=float(victronData->batteryVoltage)*0.01;
        float batteryCurrent=float(victronData->batteryCurrent)*0.1;
        float todayYield=float(victronData->todayYield)*0.01*1000;
        uint16_t inputPower=victronData->inputPower;  // this is in watts; no conversion needed


        // Getting the output current takes some magic.
        int integerOutputCurrent=((victronData->outputCurrentHi & 0x01)<<9) | victronData->outputCurrentLo;
        float outputCurrent=float(integerOutputCurrent)*0.1;

        // I don't know why, but every so often we'll get half-corrupted data from the Victron.
        // Towards the goal of filtering out this noise, I've found that I've rarely (if ever) seen
        // corrupted data when the 'unused' bits of the outputCurrent MSB equal 0xfe. We'll use this
        // as a litmus test here.
        byte unusedBits=victronData->outputCurrentHi & 0xfe;
        if (unusedBits != 0xfe) {
          return;
        }

        // The Victron docs say Device State but it's really a Charger State.
        char chargeStateName[6];
        sprintf(chargeStateName,"%4d?",deviceState);
        if (deviceState >=0 && deviceState<=7) {
          strcpy(chargeStateName,chargeStateNames[deviceState]);
        }

        Serial.printf("%-31s  Battery: %6.2f Volts %6.2f Amps  Solar: %6d Watts  Yield: %4.0f Wh  Load: %5.1f Amps  Charger: %-13s Err: %2d RSSI: %d\n",
          solarControllers[solarControllerIndex].cachedDeviceName,
          batteryVoltage, batteryCurrent,
          inputPower, todayYield,
          outputCurrent, chargeStateName, errorCode, RSSI
        );

        char screenDeviceName[14]; // 13 characters plus /0
        strncpy(screenDeviceName,solarControllers[solarControllerIndex].cachedDeviceName,13);
        screenDeviceName[13]='\0'; // make sure we have a null byte at the end.
        /*
        sh3dNbDisplay.line1 = String("Name:    ") + String(screenDeviceName);
        sh3dNbDisplay.line2 = String("Battery: ") + String(batteryVoltage) + String("V");
        sh3dNbDisplay.line3 = String("Charge:  ") + String(inputPower) + String("W ") + String(todayYield) + String("Wh");
        sh3dNb.setMessage(sh3dNb.iso8601());
        sh3dNbDisplay.update();
        */

        packetReceived=true;
      }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("Basic test");

    // VICTRON BLUETOOTH
    for (int i = 0; i < knownSolarControllerCount; i++) {
        hexCharStrToByteArray(solarControllers[i].charMacAddr, solarControllers[i].byteMacAddr);
        hexCharStrToByteArray(solarControllers[i].charKey, solarControllers[i].byteKey);
        strcpy(solarControllers[i].cachedDeviceName, "(unknown)");
    }

    for (int i = 0; i < knownSolarControllerCount; i++) {
        Serial.printf("  %-16s", solarControllers[i].comment);
        Serial.printf("  Mac:   ");
        for (int j = 0; j < 6; j++) {
            Serial.printf(" %2.2x", solarControllers[i].byteMacAddr[j]);
        }
        Serial.printf("    Key: ");
        for (int j = 0; j < 16; j++) {
            Serial.printf("%2.2x", solarControllers[i].byteKey[j]);
        }
    }

    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan(); //create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); //active scan uses more power, but gets results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99); // less or equal setInterval value
}


void loop() {
    Serial.println("tick");
    BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
    pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
    delay(100);
}

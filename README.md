# Project Gracia: Wireless Security & Protocol Analysis Toolkit

<p align="center">
  <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT">
  <img src="https://img.shields.io/badge/Platform-Arduino%20%7C%20ESP8266-orange.svg" alt="Platform: Arduino | ESP8266">
  <img src="https://img.shields.io/badge/Purpose-Educational-brightgreen.svg" alt="Purpose: Educational">
  <img src="https://img.shields.io/badge/Status-Active%20Development-yellow.svg" alt="Status: Active Development">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/⚠️-Handle%20with%20Extreme%20Care-red.svg" alt="Warning: Handle with Extreme Care">
</p>

Welcome to Project Gracia, an educational toolkit for researching and understanding the security of wireless protocols, specifically focusing on the IEEE 802.11 (Wi-Fi) standard and the 2.4GHz radio frequency spectrum.

---

## About This Project & Gemini's Role

This project was developed by **Muhammad Bilal Maulida (stdnt-c1)** with significant contributions and assistance from **Google's Gemini**. Gemini served as a co-author and an interactive assistant in the development, debugging, and documentation process. Its involvement was crucial for analyzing code, identifying flaws, generating robust solutions, and creating comprehensive documentation.

This collaboration demonstrates a modern workflow where human developers and advanced AI models work together to create and refine complex software.

---

## ⚠️ Ethical & Legal Disclaimer

The tools within this repository are powerful and can cause significant disruption. They are provided **strictly for educational and research purposes** to be used in a controlled, isolated lab environment on networks you own or have explicit permission to test.

### Core Code of Conduct
1.  **No Malicious Use:** You may not use this software to harm, harass, or disrupt any system or individual.
2.  **Lawful Use Only:** You are responsible for adhering to all applicable local and international laws regarding wireless communications.
3.  **Assume Full Responsibility:** The authors (human and AI) are not liable for any damages or legal consequences resulting from your misuse of these tools.

**By using this project, you agree to these terms.** For a full breakdown of the rules, see the [SECURITY.md](./SECURITY.md) file.

---

## Project Overview

This project contains a collection of Arduino-based scripts that demonstrate a clear progression of wireless attack methodologies, categorized into two main types:

1.  **Layer 2 (Protocol-Level) Attacks:** Exploiting weaknesses within the Wi-Fi protocol itself by crafting malicious management frames.
2.  **Layer 1 (Physical-Level) Attacks:** Generating raw radio frequency interference (noise) to disrupt all communications on a given frequency.

The `Hybrid.ino` script is the most potent tool, combining both Layer 1 and Layer 2 techniques into a single, adaptive weapon.

---

## Repository Structure & File Guide

This repository is organized to provide both functional code and comprehensive educational material.

| File / Directory | Purpose |
| :--- | :--- |
| **`/WiFi-Type/`** | Contains scripts for Layer 2 (protocol-level) attacks like Deauthentication and Beacon Flooding. The `README.md` inside guides you on fixing the packet-sniffing logic. |
| **`/RF-Type/`** | Contains scripts for Layer 1 (physical-level) RF jamming attacks. The `README.md` inside guides you on upgrading the `Hybrid.ino` script with a proper target scanner. |
| **`/IEEE 802.11/`** | Contains documentation and analysis related to the underlying Wi-Fi protocol itself, providing theoretical context for the attacks. |
| **`MethodologyGuide.md`** | **(Essential Reading)** Provides a detailed, step-by-step guide on how to use these tools ethically in a lab, understand the attack vectors, and test countermeasures. |
| **`GeminiReview.md`** | An in-depth cybersecurity review of each script, comparing their methodologies, effectiveness, and sophistication. This was co-authored with Gemini. |
| **`Gracia.ino`** | **(Historical Archive)** The original, messy proof-of-concept file. It is not intended for use and is kept for historical context to show the project's evolution. |
| **`SECURITY.md`** | The official security policy and code of conduct for the project. It outlines the rules of engagement and defines misuse. |
| **`LICENSE`** | The project's MIT license, with an addendum that explicitly forbids malicious use and reinforces the educational-only purpose. |
| **`/.gitignore`** | Configured to exclude temporary files, logs, and personal test directories (`-p/`) from the repository. |
| **`/logs/`** | A directory for storing logs. It is ignored by Git. |
| **`/-p/`** | A directory intended for personal, temporary, or experimental files. It is ignored by Git. |

---

## Getting Started

Before using any scripts, it is **critical** to understand the principles and risks involved.

1.  **Read the [Methodology Guide](./MethodologyGuide.md)** to learn how to set up a safe lab environment.
2.  **Review the [SECURITY.md](./SECURITY.md)** file to understand the code of conduct.
3.  Explore the `README.md` files inside the `WiFi-Type` and `RF-Type` directories for specific code improvement tasks.

This project is designed to be a learning experience. The scripts are intentionally left with minor flaws for you to fix, guided by the provided documentation.

---

> **A Note on Naming:** The author humorously named the local repository "Malicious" as an ironic nod to the nature of the code, even though the public-facing project name is "Gracia" and its purpose is strictly educational and defensive. It serves as a reminder that context and intent are everything in cybersecurity.
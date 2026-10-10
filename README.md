# NerdMiner CYD Modded

[![Web Flasher](https://img.shields.io/badge/Web%20Flasher-Launch%20Now-brightgreen?style=for-the-badge&logo=googlechrome)](https://guitarbeat.github.io/nerdminer-cyd-modded/)
[![GitHub Repo](https://img.shields.io/badge/GitHub-Repository-blue?style=for-the-badge&logo=github)](https://github.com/guitarbeat/nerdminer-cyd-modded)
[![License](https://img.shields.io/badge/License-GPL--3.0-orange?style=for-the-badge)](LICENSE)

An all-in-one ESP32 Bitcoin solo-mining project combining **high-performance firmware optimizations (~990 KH/s)** with full hardware support for **Cheap Yellow Display (CYD)** boards and an **instant browser-based 1-click Web Flasher**.

---

## ⚡ 1-Click Web Flasher (No Coding Required)

Flash your board directly from Google Chrome, Edge, or any Web-Serial-compatible browser:

👉 **[Launch NerdMiner CYD Web Flasher](https://guitarbeat.github.io/nerdminer-cyd-modded/)**

1. Connect your ESP32 / CYD to your computer via a data-capable USB cable.
2. Open the [Web Flasher](https://guitarbeat.github.io/nerdminer-cyd-modded/).
3. Select your hardware variant from the radio list.
4. Click **Connect**, choose your serial port, and follow the on-screen instructions to flash.

---

## 🖥️ Supported Hardware Variants

This repository unifies support for CYD displays alongside other popular ESP32 form factors:

| Board / Target | Display | Description & Specifics |
|---|---|---|
| **ESP32-2432S024R** | 2.4" TFT (240x320) | Single USB-C port, Backlight GPIO 27, RGB LED pins 16/17 swapped |
| **ESP32-2432S028R** | 2.8" TFT (240x320) | Single Micro-USB port, Backlight GPIO 21 |
| **ESP32-2432S028R-2USB** | 2.8" TFT (240x320) | Dual USB (USB-C + Micro-USB) |
| **LILYGO TTGO T-Display** | 1.14" IPS LCD | ST7789 display controller |
| **ESP32-C3 SuperMini** | None / External OLED | Compact RISC-V solo miner |
| **Wemos S2 / C2 Mini** | None / External | Low-cost ESP32 variant |
| **M5Stack M5Stick-C** | 0.96" TFT | Portable M5Stack wearable form factor |
| **ESP32 DevKit V1** | None / External | Standard 30/38 pin ESP32 developer boards |

---

## ⚙️ Initial Configuration (WiFi & Wallet)

Once flashed and powered on:

1. **Connect to WiFi AP**: On your phone or computer, connect to the WiFi network:
   - **SSID**: `NerdMinerAP`
   - **Password**: `MineYourCoins`
2. **Access Portal**: If the captive portal doesn't open automatically, open your browser and navigate to `http://192.168.4.1`.
3. **Configure Settings**:
   - Select your local WiFi SSID and enter your WiFi password.
   - Enter your **Bitcoin Wallet Address** (e.g., SegWit `bc1...` or legacy `1...`).
   - *(Optional)* Add a worker identifier: `YourAddress.workername`.
   - Select pool settings (defaults to [Public-Pool.io](https://public-pool.io/)).
   - Set color inversion if required for your screen revision.
4. **Save and Restart**: The miner will reboot, connect to your WiFi, and start solo mining!

To view your miner stats online, visit [web.public-pool.io](https://web.public-pool.io/) and enter your BTC payout address.

### 🌐 On-Device Web Settings Portal

Once connected to your home WiFi, the miner runs a local web configuration server accessible from any browser on your network:

1. Open `http://<miner-ip>` (e.g. `http://192.168.0.27`)
2. Log in with:
   - **Username**: `admin`
   - **Password**: `MineYourCoins`
3. You can change your **Timezone (UTC offset)**, **Bitcoin Wallet Address**, **Mining Pool**, **Currency**, **Display Brightness**, **Color Inversion**, and **Screen Rotation** directly without reflashing!

---

## 🛠️ Building & Developing with PlatformIO

If you want to modify source code or compile custom firmware locally:

### Prerequisites
- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE extension](https://platformio.org/) installed, OR `platformio` CLI (`pip install platformio`).

### Build & Upload Commands

Clone the repository:
```bash
git clone https://github.com/guitarbeat/nerdminer-cyd-modded.git
cd nerdminer-cyd-modded
```

Build and flash for your specific target environment:

- **2.4" CYD (Single USB-C)**:
  ```bash
  pio run -e ESP32-2432S024 -t upload
  ```

- **2.8" CYD (Single Micro-USB)**:
  ```bash
  pio run -e ESP32-2432S028R -t upload
  ```

- **2.8" CYD (Dual USB)**:
  ```bash
  pio run -e ESP32_2432S028_2USB -t upload
  ```

- **LILYGO TTGO T-Display**:
  ```bash
  pio run -e TTGO-T-Display -t upload
  ```

---

## 🔗 Upstream Projects & Credits

This project merges and builds upon the fantastic work of the open-source community:

- **[HenrysCat/NerdMiner-Modded](https://github.com/HenrysCat/NerdMiner-Modded)**: Source for the performance-tuned firmware achieving ~990 KH/s, custom UI themes, and expanded board platform definitions.
- **[jpduhen/NerdMiner_v2-Cheap-Yellow-Display-2.4-and-2.8-single-and-double-USB](https://github.com/jpduhen/NerdMiner_v2-Cheap-Yellow-Display-2.4-and-2.8-single-and-double-USB)**: Webflasher interface and 2.4"/2.8" CYD single and dual USB hardware specifications.
- **[Fr4nkFletcher/NerdMiner_v2-Cheap-Yellow-Display](https://github.com/Fr4nkFletcher/NerdMiner_v2-Cheap-Yellow-Display)**: Original Cheap Yellow Display port and webflasher integration.
- **[BitMaker-hub/NerdMiner_v2](https://github.com/BitMaker-hub/NerdMiner_v2)**: The foundational NerdMiner v2 project that started the ESP32 solo mining revolution.
- **[ESP Web Tools](https://esphome.github.io/esp-web-tools/)**: Browser-based flashing engine powering the Web Flasher.

---

## 📜 License

This project is licensed under the GNU General Public License v3.0 (GPL-3.0) consistent with upstream NerdMiner repositories. See [LICENSE](LICENSE) for details.

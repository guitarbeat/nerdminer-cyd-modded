# NerdMiner CYD Modded

[![Web Flasher](https://img.shields.io/badge/Web%20Flasher-Launch%20Now-brightgreen?style=for-the-badge&logo=googlechrome)](https://guitarbeat.github.io/nerdminer-cyd-modded/)
[![GitHub Repo](https://img.shields.io/badge/GitHub-Repository-blue?style=for-the-badge&logo=github)](https://github.com/guitarbeat/nerdminer-cyd-modded)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)](LICENSE)

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

### 💾 MicroSD Headless Boot Configuration (`config.json`)

If your CYD board has a MicroSD slot on the back, you can configure your miner headlessly without connecting to the setup access point:

1. Format a MicroSD card using **FAT32**.
2. Create a file named `config.json` in the root directory:
```json
{
  "SSID": "YourHomeWiFi",
  "WifiPW": "YourWiFiPassword",
  "PoolUrl": "public-pool.io",
  "PoolPort": 21496,
  "PoolPassword": "x",
  "BtcWallet": "bc1qYourBitcoinWalletAddress",
  "Timezone": -5,
  "SaveStats": false
}
```
3. Insert the card into the CYD slot and power on the board. The miner will read the configuration into flash memory.

---

### 🌐 On-Device Web Settings Portal (`http://<miner-ip>/`)

Once connected to your home network, the miner runs a local dashboard and settings server accessible from any browser:

- **URL**: `http://<miner-ip>` (e.g. `http://192.168.0.27`)
- **Username**: `admin`
- **Password**: `MineYourCoins`

#### ✨ Web Dashboard Features:
1. **AxeOS-Style Live Theme Switcher**: Hot-swap between **6 visual palettes** with one click. The screen updates in `<30ms` without dropping pool connections or rebooting the ESP32!
   - **Instrument Amber** *(Default)*: Matte black precision instrument with warm amber telemetry
   - **Matrix Green**: Retro phosphor terminal CRT aesthetic
   - **Cyber Amber**: Vintage 1980s monochrome orange CRT terminal
   - **Arcade Neon**: Electric cyan and magenta retro mod
   - **Ice Blue**: Deep midnight slate with arctic blue accents
   - **Monochrome**: High-contrast black and paper-white minimalist e-ink style
2. **Proof of Work Anatomy Breakdown**: Interactive guide explaining Hashrate & Nonces, Shares Submitted, Best Difficulty, and Target Block Reward (3.125 BTC subsidy + fees).
3. **Live Metrics & Tooltips**: Real-time stats with hover explanations for shares, best diff, core temperature, WiFi signal (dBm), and free heap.
4. **Instant Low-Diff Pool Presets**: 1-click presets for Public-Pool, NerdMiners.org, and SoloMining.de.
5. **REST API**: Switch themes remotely via HTTP: `POST /api/theme?id=1` or query current theme via `GET /api/theme`.

---

### 👆 Touchscreen & Physical Button Controls

#### CYD Resistive Touch Zones
- **Tap Wordmark (`NERDMINER`)**: Cycle active accent color
- **Tap Top-Right Corner**: Toggle LCD backlight ON / OFF
- **Tap Screen Center**: Cycle views (**MINE** &rarr; **MARKET** &rarr; **CLOCK**)

#### Physical BOOT Button
- **1 Click**: Cycle screen view
- **Double Click**: Rotate display orientation (0°, 90°, 180°, 270°)
- **Triple Click**: Toggle LCD backlight ON / OFF
- **Hold 5 Seconds**: Factory reset flash configuration and reboot into AP mode

---

### 🏊 Recommended Low-Difficulty Solo Mining Pools

> ⚠️ **Important:** Classic ESP32 solo miners generate low-difficulty shares. Standard vardiff pools (such as Braiins, standard CKPool, or Slush) require high difficulty targets (e.g., diff > 1000) and will not record your miner's shares. Always use a pool with a low-difficulty port:

| Pool URL | Port | Web Dashboard | Notes |
|:---|:---:|:---|:---|
| **public-pool.io** | `21496` or `3333` | [web.public-pool.io](https://web.public-pool.io/) | Open-source solo Bitcoin pool (Default) |
| **pool.nerdminers.org** | `3333` | [nerdminers.org](https://nerdminers.org/) | Official community NerdMiner pool |
| **pool.nerdminer.io** | `3333` | [nerdminer.io](https://nerdminer.io/) | Maintained by CHMEX |
| **pool.solomining.de** | `3333` | [solomining.de](https://pool.solomining.de/) | German low-diff solo pool |
| **pool.pyblock.xyz** | `3333` | [pyblock.xyz](https://pool.pyblock.xyz/) | Maintained by curly60e |
| **pool.sethforprivacy.com**| `3333` | [sethforprivacy.com](https://pool.sethforprivacy.com/) | Public-Pool fork |

---

### 🔬 Why This Hashrate is Real (~1000 KH/s)

The sustained **~990 – 1003 KH/s** is what the hardware actually computes, not an inflated counter:

- **Hardware Ceiling**: The classic ESP32 hardware SHA engine does not support loading an external midstate. Every 32-bit nonce requires three hardware hash blocks plus two engine loads (~228 CPU cycles hardware limit at 240 MHz &rarr; theoretical ceiling of ~1.05 MH/s).
- **Pipelined Dual-Core Mining**: Core 0 runs an optimized assembly/pipelined loop keeping the SHA engine busy for all but ~19 cycles per nonce (247 cycles/nonce &rarr; ~970 KH/s). Core 1 contributes ~25–30 KH/s in parallel in software.
- **Hardware Bus Contention Fix**: Writes to ESP32 SHA registers spaced less than 3 cycles apart can drop whenever the second core accesses the peripheral bus. This firmware enforces precise bus delays so that candidates are never dropped or silently corrupted.
- **Known-Answer Test (KAT)**: At power-on, the firmware tests itself against a known cryptographic test vector before mining. Every candidate found by hardware is recomputed in software, guaranteeing **100% valid shares submitted to the pool**.


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

This project is open-source and licensed under the [MIT License](LICENSE), matching the upstream NerdMiner_v2 foundation by BitMaker and Valerio Vaccaro. See [LICENSE](LICENSE) for full details.

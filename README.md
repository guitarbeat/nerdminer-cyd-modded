# NerdMiner CYD Modded

[![Web Flasher](https://img.shields.io/badge/Web%20Flasher-Launch%20Now-brightgreen?style=for-the-badge&logo=googlechrome)](https://guitarbeat.github.io/nerdminer-cyd-modded/)
[![GitHub Repo](https://img.shields.io/badge/GitHub-Repository-blue?style=for-the-badge&logo=github)](https://github.com/guitarbeat/nerdminer-cyd-modded)
[![Upstream: HenrysCat](https://img.shields.io/badge/Fork%20of-HenrysCat%2FNerdMiner--Modded-orange?style=for-the-badge&logo=git)](https://github.com/HenrysCat/NerdMiner-Modded)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)](LICENSE)

An optimized, all-in-one ESP32 Bitcoin solo-mining project combining **high-performance firmware optimizations (~860–1000 KH/s)** with full hardware support for **Cheap Yellow Display (CYD)** boards, **live settings without rebooting**, **backup pool failover/failback**, a **virtual on-device CYD web twin**, and an **instant browser-based 1-click Web Flasher**.

---

## ⚡ 1-Click Web Flasher (No Coding Required)

Flash your board directly from Google Chrome, Edge, or any Web-Serial-compatible browser:

👉 **[Launch NerdMiner CYD Web Flasher](https://guitarbeat.github.io/nerdminer-cyd-modded/)**

1. Connect your ESP32 / CYD to your computer via a data-capable USB cable.
2. Open the [Web Flasher](https://guitarbeat.github.io/nerdminer-cyd-modded/).
3. Select your hardware variant from the list.
4. Click **Connect**, choose your serial port, and follow the on-screen instructions to flash.

---

## 🖥️ Supported Hardware Variants

This repository provides dedicated environments for popular Cheap Yellow Display (CYD) revisions and ESP32 solo miners:

| Board / Target | Display | PlatformIO Env | Description & Specifics |
|---|---|---|---|
| **ESP32-2432S028R (BL27)** | 2.8" TFT (240x320) | `ESP32-2432S028R-BL27` | 2.8" CYD variant with **Backlight on GPIO 27** (fixes black screen on BL27 revisions) |
| **ESP32-2432S028R** | 2.8" TFT (240x320) | `ESP32-2432S028R` | Single Micro-USB port, Backlight GPIO 21 |
| **ESP32-2432S024R** | 2.4" TFT (240x320) | `ESP32-2432S024` | Single USB-C port, Backlight GPIO 27, RGB LED pins 16/17 swapped |
| **ESP32-2432S028R-2USB** | 2.8" TFT (240x320) | `ESP32_2432S028_2USB` | Dual USB (USB-C + Micro-USB) |
| **LILYGO TTGO T-Display** | 1.14" IPS LCD | `TTGO-T-Display` | ST7789 display controller |
| **ESP32-C3 SuperMini** | None / External OLED | `ESP32-C3` | Compact RISC-V solo miner |
| **Wemos S2 / C2 Mini** | None / External | `ESP32-S2` / `ESP32-C2` | Low-cost ESP32 variants |
| **M5Stack M5Stick-C** | 0.96" TFT | `M5Stick-C` | Portable M5Stack wearable form factor |
| **ESP32 DevKit V1** | None / External | `ESP32-DevKit` | Standard 30/38 pin ESP32 developer boards |

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
   - Configure optional backup pool for automatic failover.
4. **Save and Start Mining**: The miner connects to your Wi-Fi and begins solo mining immediately!

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
  "PoolPort": 3333,
  "PoolPassword": "x",
  "FallbackPoolUrl": "pool.nerdminers.org",
  "FallbackPoolPort": 3333,
  "BtcWallet": "bc1qYourBitcoinWalletAddress",
  "Timezone": -5,
  "SaveStats": false
}
```
3. Insert the card into the CYD slot and power on the board. The miner reads the configuration into flash memory automatically.

---

## 🖥️ 4-Screen Performance Carousel

The CYD firmware features a high-density, zero-overhead carousel of **4 specialized screens** designed specifically for the 240x320 portrait form factor. All rendering occurs without blocking network calls or sacrificing hashrate:

| Index | Screen | Highlights & Information Architecture |
|:---:|:---|:---|
| **0** | **MINE**<br>*(Pulse Dashboard)* | • **Hero Segmented Hashrate Arc**: 270° tachometer sweep (0 to 1200 KH/s) with peak hold.<br>• **Hero Hashrate Readout**: High-contrast, large KH/s display.<br>• **Hashes Tested**: Humanized counter of lifetime attempts (e.g. `1.1G HASHES`).<br>• **Shares & Best**: Crisp stat cards showing submitted 32-bit shares and session **★ BEST** difficulty.<br>• **Telemetry Footer**: Current worker tag, session uptime, and blocks found (`0`). |
| **1** | **MARKET**<br>*(Bitcoin Network)* | • **Hero Spot Price**: Real-time Bitcoin price formatted in USD (`$XX,XXX`).<br>• **Block Age & Height**: Time elapsed since last mined block (`Xm ago`) and current block height.<br>• **Network & Difficulty**: Global network hashrate (EH/s) and current difficulty (T).<br>• **Mempool Fee**: Half-hour transaction fee (sat/vB) with priority pill (**LOW**, **NORMAL**, **BUSY**).<br>• **Halving Progress Bar**: Visual progress bar and countdown to the next Bitcoin halving. |
| **2** | **CLOCK**<br>*(Ambient Desk Clock)* | • **Ambient Digital Clock**: High-legibility 12h/24h time with pulsing colon and calendar date.<br>• **Current Hashrate Badge**: Compact live KH/s indicator.<br>• **60-Second Sparkline**: Rolling 60s hashrate bar chart populated by real hardware samples.<br>• **Health Telemetry Strip**: ESP32 core temperature (°C), system uptime, and Wi-Fi RSSI bars. |
| **3** | **PROTOCOL**<br>*(Attempt Quality)* | • **Attempt Quality Scatter Plot**: Logarithmic difficulty Y-axis (higher = rarer hash attempt).<br>• **Live-Sampled Hardware Attempts**: Scatter points sampled at ~1 in 16,384 nonces for zero display latency.<br>• **Block Threshold Line**: Horizontal target line anchored to live network difficulty ($hash < target$).<br>• **In-Place Best Tracker**: Prominent marker tracking your closest candidate hash of the session.<br>• **Hashrate Footer**: Real-time KH/s status footer. |

---

### 👆 Touchscreen & Physical Controls

#### Touch Zones (Resistive & Capacitive CYD)
- **Tap Right Half**: Cycle forward to next screen (`MINE → MARKET → CLOCK → PROTOCOL → MINE`)
- **Tap Left Half**: Cycle backward to previous screen
- **Tap Top-Right Corner**: Toggle LCD backlight ON / OFF
- **Ambient Light Sensor (LDR)**: Automatic dynamic dimming in dark environments on CYD boards with light sensor (GPIO 34)
- **NVS Memory**: Automatically saves your active screen index to flash and restores it across reboots!

#### Physical BOOT Button
- **1 Click**: Cycle to the next screen view
- **Double Click**: Rotate display orientation (0° Portrait, 90° Landscape, 180° Inverted Portrait, 270° Inverted Landscape)
- **Triple Click**: Toggle LCD backlight ON / OFF
- **Hold 5 Seconds**: Factory reset flash configuration and reboot into setup AP mode

---

## ⚡ Live Settings & Resilience Features

### 🔄 Live Settings Without Rebooting
Unlike traditional firmware that reboots the ESP32 on every settings save, this firmware applies most configuration changes **live in memory**:
- **Zero-Reboot Settings**: Primary Pool URL/Port, Backup Pool URL/Port, BTC Wallet Address, Timezone, Fiat Currency, Mempool Host, Theme Palette, and Backlight Brightness apply immediately without dropping uptime.
- **Instant Stratum Recycling**: Changing pool credentials cleanly drops existing jobs, closes stratum sockets, and reconnects to the new pool instantly while keeping the miner running.
- **Smart Restart**: An 8-second reboot countdown is only triggered if physical display orientation (`screenRotation`, `flipDisplay`) or hardware color inversion (`invertColors`) is modified.

### 🛡️ Dual-Pool Failover & Failback
Protect against solo pool outages with automatic redundancy:
- **Automatic Failover**: Automatically switches to the configured fallback pool if 3 consecutive primary connection attempts fail (`POOL_FAILOVER_TRIES = 3`).
- **Periodic Failback Probe**: While operating on the fallback pool, a non-blocking background probe tests the primary pool every 10 minutes (`POOL_FAILBACK_ms`). Once the primary pool recovers and responds, the miner switches back automatically.
- **Interruptible Backoff**: Retry intervals can be interrupted instantly when new settings are saved via the web dashboard.

---

## 🌐 On-Device Web Dashboard & Virtual CYD Twin (`http://<miner-ip>/`)

Once connected to your home network, the miner serves a full web dashboard:

- **URL**: `http://<miner-ip>` (e.g. `http://192.168.0.27`)
- **Username**: `admin`
- **Password**: `MineYourCoins`

### ✨ Web Features:
1. **Interactive Virtual CYD Twin**:
   - An on-screen replica of the CYD hardware rendered via clean SVG/HTML5 canvas.
   - Synchronizes live with physical orientation, active screen index, live 60-second hashrate history, and hardware bezel label (`ESP32-2432S028R` or `024R`).
2. **AxeOS-Style Live Theme Switcher**:
   - Hot-swap between **6 visual palettes** with one click in `<30ms` without rebooting:
     - **Instrument Amber** *(Default)*, **Matrix Green**, **Cyber Amber**, **Arcade Neon**, **Ice Blue**, **Monochrome**.
3. **Interactive Proof-of-Work Simulator (Browser SHA256d)**:
   - Run the double-SHA256 loop directly in your browser using the native Web Crypto API.
   - Solve historical Bitcoin Block #286,819 or test custom nonces with the Ken Shirriff stepping-stone buttons.
4. **Interactive Bitcoin Supply & Satoshis Converter**:
   - Live unit converter translating between **BTC**, **Satoshis (sats)**, **Bits ($\mu$BTC)**, and **mBTC**.
5. **REST Telemetry APIs**:
   - `GET /api/stats`: Comprehensive JSON stats (fw git hash, hashrate, shares, diff, uptime, market data, hash samples).
   - `GET /api/status`: Upstream-compatible standard telemetry payload (state, hashrate, pool connection, Wi-Fi RSSI, free heap).
   - `POST /api/theme?id=X`: Instant theme palette switch.
   - `POST /api/reset-session`: Reset session counters while preserving lifetime flash stats.
6. **On-Device OTA Firmware Flashing**:
   - Web-based firmware updater at `http://<miner-ip>/update` for wireless firmware flashes.

---

## 🏊 Recommended Low-Difficulty Solo Mining Pools

> ⚠️ **Important:** Classic ESP32 solo miners generate low-difficulty shares. Standard vardiff pools (such as Braiins, standard CKPool, or Slush) require high difficulty targets (e.g., diff > 1000) and will not record your miner's shares. Always use a pool with a low-difficulty port:

| Pool URL | Port | Web Dashboard | Notes |
|:---|:---:|:---|:---|
| **public-pool.io** | `3333` or `21496` | [web.public-pool.io](https://web.public-pool.io/) | Open-source solo Bitcoin pool (Port 3333 Recommended) |
| **pool.nerdminers.org** | `3333` | [nerdminers.org](https://nerdminers.org/) | Official community NerdMiner pool |
| **pool.nerdminer.io** | `3333` | [nerdminer.io](https://nerdminer.io/) | Maintained by CHMEX |
| **pool.solomining.de** | `3333` | [solomining.de](https://pool.solomining.de/) | German low-diff solo pool |
| **pool.pyblock.xyz** | `3333` | [pyblock.xyz](https://pool.pyblock.xyz/) | Maintained by curly60e |
| **pool.sethforprivacy.com**| `3333` | [sethforprivacy.com](https://pool.sethforprivacy.com/) | Public-Pool fork |

---

## 🔬 Why This Hashrate is Real (~1000 KH/s)

The sustained **~860 – 1000 KH/s** is what the hardware actually computes, not an inflated counter:

- **Hardware Ceiling**: The classic ESP32 hardware SHA engine does not support loading an external midstate. Every 32-bit nonce requires three hardware hash blocks plus two engine loads (~228 CPU cycles hardware limit at 240 MHz &rarr; theoretical ceiling of ~1.05 MH/s).
- **Pipelined Dual-Core Mining**: Core 0 runs an optimized assembly/pipelined loop keeping the SHA engine busy for all but ~19 cycles per nonce (247 cycles/nonce &rarr; ~970 KH/s). Core 1 contributes ~25–30 KH/s in parallel in software.
- **Hardware Bus Contention Fix**: Writes to ESP32 SHA registers spaced less than 3 cycles apart can drop whenever the second core accesses the peripheral bus. This firmware enforces precise bus delays so that candidates are never dropped or silently corrupted.
- **Known-Answer Test (KAT)**: At power-on, the firmware tests itself against a known cryptographic test vector before mining. Every candidate found by hardware is recomputed in software, guaranteeing **100% valid shares submitted to the pool**.

---

## 🛠️ Building & Developing with PlatformIO

### Prerequisites
- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE extension](https://platformio.org/), OR CLI with `platformio` / `uvx --from platformio pio`.

### Build & Upload Commands

Clone the repository:
```bash
git clone https://github.com/guitarbeat/nerdminer-cyd-modded.git
cd nerdminer-cyd-modded
```

Build and flash for your specific target environment (e.g. using `pio` or `uvx`):

- **2.8" CYD with Backlight GPIO 27 (Recommended for modern 2.8" boards)**:
  ```bash
  pio run -e ESP32-2432S028R-BL27 -t upload
  # or with uvx:
  uvx --from platformio pio run -e ESP32-2432S028R-BL27 -t upload
  ```

- **2.8" CYD with Backlight GPIO 21 (Single Micro-USB)**:
  ```bash
  pio run -e ESP32-2432S028R -t upload
  ```

- **2.4" CYD (Single USB-C)**:
  ```bash
  pio run -e ESP32-2432S024 -t upload
  ```

- **2.8" CYD (Dual USB)**:
  ```bash
  pio run -e ESP32_2432S028_2USB -t upload
  ```

- **LILYGO TTGO T-Display**:
  ```bash
  pio run -e TTGO-T-Display -t upload
  ```

> 💡 **Tip:** Always use `-t upload` (without erase) so your saved Wi-Fi credentials and pool settings in NVS remain intact across firmware updates.

---

## 🏷️ Firmware Versioning

Every build embeds the git short hash (`-dirty` if built from uncommitted changes). You can verify your active firmware version from:
- The serial boot banner: `Firmware git: <hash>`
- The `fw` field of `http://<device-ip>/api/stats` and `http://<device-ip>/api/status`
- The web UI subtitle in your browser

Match the short hash against `git log --oneline` on `main`.

---

## 🔗 Upstream Projects & Credits

This project is an official GitHub fork of and builds upon:

- **[HenrysCat/NerdMiner-Modded](https://github.com/HenrysCat/NerdMiner-Modded)**: Upstream base for the performance-tuned SHA engine loop (~990 KH/s), theme engine, and expanded board platform definitions.
- **[Ken Shirriff's "Bitcoin Mining the Hard Way"](https://www.righto.com/2014/02/bitcoin-mining-hard-way-algorithms.html)**: Groundbreaking breakdown of Bitcoin mining down to raw bytes, Block #286,819 test vector, and Stratum V1 wire protocol.
- **[Click Miner](https://github.com/owencoles/click-miner)**: Educational block header mental model and 80-byte header breakdown.
- **[guerrerocarlos/bitcoin-miner](https://github.com/guerrerocarlos/bitcoin-miner)**: Educational test vectors and first-principles implementation for compact target unpacking and little-endian binary header serialization.
- **[jpduhen/NerdMiner_v2-Cheap-Yellow-Display-2.4-and-2.8-single-and-double-USB](https://github.com/jpduhen/NerdMiner_v2-Cheap-Yellow-Display-2.4-and-2.8-single-and-double-USB)**: Webflasher interface and CYD hardware specifications.
- **[BitMaker-hub/NerdMiner_v2](https://github.com/BitMaker-hub/NerdMiner_v2)**: The foundational NerdMiner v2 project that started the ESP32 solo mining revolution.
- **[ESP Web Tools](https://esphome.github.io/esp-web-tools/)**: Browser-based flashing engine powering the 1-click Web Flasher.

---

## 📜 License

This project is open-source and licensed under the [MIT License](LICENSE), matching the upstream NerdMiner_v2 foundation by BitMaker and Valerio Vaccaro. See [LICENSE](LICENSE) for full details.


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

## 🖥️ 8-Screen Carousel System

The display firmware features a seamlessly rotating carousel of **8 distinct views**, combining real-time telemetry, market analytics, and beginner-accessible educational breakdowns:

| Index | Screen Name | Layout & Key Information |
|:---:|:---|:---|
| **0** | **MINE (Pulse Dashboard)** | Sleek instrument gauge ring (0 to 1200 KH/s), 4 hero stat tiles (**Shares**, **Best Diff**, **Templates**, **Blocks Found**), and a 4-cell pool telemetry strip (**Workers**, **Pool Hashrate**, **Pool Best**, **Uptime**). |
| **1** | **MARKET (Market & Halving)** | Real-time Bitcoin price in your configured fiat currency, current block height, mempool fee rates (half-hour), network difficulty, global hashrate, and a live halving countdown with visual progress bar. |
| **2** | **CLOCK (Desk Clock & Bar Chart)** | Digital clock with blinking colon, full date, market mini-tiles, and a rolling **48-slot hashrate history bar chart** (8 minutes of history at 10s per slot). |
| **3** | **EXPLAINER (Pipeline & Nonce Dial)** | 4-card causal block header pipeline (`[Block Data]` &rarr; `[80B Header]` &rarr; `[SHA256d]` &rarr; `[256-Bit Test]`), guess-and-check comparator log, leading zeros historical context (2009: 8 zeros vs Today: 18 zeros), and live plain-English speed explanation. |
| **4** | **LOTTERY (Solo Lottery Analytics)** | Current block reward jackpot (3.125 BTC + fees &approx; $300k+), total hashes tested since boot, best difficulty rarity score (clarifying it is a score, not % progress), mathematical difficulty formula ($\text{Difficulty} = \text{Target}_1 / \text{Current Target}$), expected hashes calculation ($\text{Diff} \times 2^{32} \approx \text{Diff} \times 4.295\text{B}$), distinction between **bdiff** (truncated Satoshi target) and **pdiff** (pool target), block time equation ($\text{Time} = \text{Diff} \times 2^{32} / \text{Hashrate}$), and the **256-bit die roll finish line**. |
| **5** | **DIAGNOSTICS (System Telemetry)** | Stratum pool connection (host, port, live ping in milliseconds, primary vs. fallback failover route), Wi-Fi metrics (SSID, RSSI in dBm, quality bars, local IP), ESP32 hardware health (core temperature in °C/°F, free heap RAM, max alloc block, system uptime). |
| **6** | **HEADER (80-Byte Block Header)** | Complete educational breakdown of the 6 fields of the candidate block header (**Version 4B**, **Prev Hash 32B**, **Merkle Root 32B**, **Time 4B**, **Bits 4B**, **Nonce 4B**). Demonstrates that 76 bytes are fixed by the network and the Nonce is the only dial the CYD turns. Teaches target unpacking from Bits ($\text{Target} = \text{Mantissa} \times 256^{\text{Exp}-3}$), little-endian serialization, ExtraNonce rollover, pool coinbase commitment preventing stolen blocks, double-hash (**SHA256d**), and the **avalanche effect**. |
| **7** | **SUPPLY (21M Cap, Lost Coins & Satoshis)** | Educational breakdown of Bitcoin economics based on the **Bitcoin Wiki FAQ**: strict **20,999,999.9769 BTC** hard cap, halving subsidy decay via bitwise shift `>> 1` reaching zero at block #6,929,999 (~year 2140). Explains what happens when Bitcoins are lost (permanent out-of-circulation loss, gentle deflation, Satoshi's *"donation to everyone"* principle), how **infinite divisibility** prevents deflation crises (1 BTC = 100M Satoshis, 64-bit integer `int64_t` protocol storage, bits, and Lightning msats), and explains why blocks continue every 10 minutes forever sustained 100% by transaction fees. |

---

### 👆 Touchscreen & Physical Controls

#### Touch Zones (Resistive & Capacitive CYD)
- **Tap Right Half**: Cycle forward to next screen (`0 → 1 → 2 → 3 → 4 → 5 → 6 → 7 → 0`)
- **Tap Left Half**: Cycle backward to previous screen
- **Tap Top-Right Corner**: Toggle LCD backlight ON / OFF
- **NVS Memory**: Automatically saves your active screen index to flash and restores it across reboots!

#### Physical BOOT Button
- **1 Click**: Cycle to the next screen view
- **Double Click**: Rotate display orientation (0° Portrait, 90° Landscape, 180° Inverted Portrait, 270° Inverted Landscape)
- **Triple Click**: Toggle LCD backlight ON / OFF
- **Hold 5 Seconds**: Factory reset flash configuration and reboot into setup AP mode

---

### 🌐 On-Device Web Settings Portal (`http://<miner-ip>/`)

Once connected to your home network, the miner serves a full web dashboard and configuration portal:

- **URL**: `http://<miner-ip>` (e.g. `http://192.168.0.27`)
- **Username**: `admin`
- **Password**: `MineYourCoins`

#### ✨ Web Dashboard Features:
1. **Interactive Proof-of-Work Simulator (Browser SHA256d)**:
   - Experience the exact double-SHA256 loop running in real-time in your browser via the native Web Crypto API.
   - **Preset 1 — Historical Block #286,819 Solver**: Inspired by Ken Shirriff's blog and Carlos Guerrero's miner, solves real historical Block #286,819 (Feb 20, 2014) by constructing the genuine 80-byte binary header with little-endian fields, unpacking the 256-bit target from Bits `19015f53` ($\text{Target} = \text{Mantissa} \times 256^{\text{Exp}-3}$), testing starting Nonce `856,192,320`, and solving the block at winning Nonce `856,192,328` where `hash < target` evaluates to true!
   - **Ken Shirriff Nonce Stepping Stones**: Quick-jump buttons for key nonces from Ken's famous table (Nonce 0 [0 zeros], Nonce 39 [1 zero], Nonce 990 [3 zeros], Nonce 52,117 [4 zeros], Nonce 856,192,320 [Countdown], and Nonce 856,192,328 [WINNER!]).
   - **Preset 2 — Sandbox Mode**: Test custom candidate block headers with adjustable target leading zeros (3, 4, 5, or 6 leading zeros) to explore why attempts fail and why each trial is independent.
   - Inspect the candidate **80-byte block header** payload with live color-coded fields:
     `[Ver 4B]` `[Prev 32B]` `[Merkle 32B]` `[Time 4B]` `[Bits 4B]` `[Nonce 4B]`
   - Manually click **Test Next Nonce (+1)** or run **Auto Mine (10 H/s)**.
2. **📖 LEARN.TXT — How Bitcoin Mining Actually Works**:
   - Inspired by **Bitcoin Wiki: Difficulty**, **Ken Shirriff's "Bitcoin Mining the Hard Way"**, **Click Miner**, and **guerrerocarlos/bitcoin-miner**: explains what a hash is (256-bit fingerprint, avalanche effect with no "getting warmer", one-way function).
   - **The Mathematical Formula for Difficulty**:
     - $\text{Difficulty} = \text{Target}_1 / \text{Current Target}$.
     - Satoshi defined base $\text{Target}_1$ as `0x00000000FFFF0000000000000000000000000000000000000000000000000000` (packed bits `0x1d00ffff`).
     - Because $\text{Target}_1 / 2^{256} \approx 1 / 2^{32}$, finding a difficulty 1 share requires an expected **$2^{32} = 4,294,967,296$ hashes**! Any difficulty $D$ requires an expected $D \times 2^{32}$ hashes.
     - Distinguishes between **bdiff** (Bitcoin Core block difficulty using Satoshi's truncated mantissa) and **pdiff** (Stratum pool share difficulty using untruncated 32 leading zero bits `0x00000000FFFFFFFF...`).
      - Derives expected block time $\text{Time} = (D \times 2^{32}) / \text{Hashrate}$ and network hashrate $(D \times 2^{32}) / 600\text{s}$ retargeted every 2,016 blocks.
   - **21 Million Hard Cap, Lost Coins & Deflationary Model**:
     - Total supply is mathematically capped at **20,999,999.9769 BTC** with the final subsidy block (#6,929,999) mined around year 2140.
     - Subsidy halves every 210,000 blocks via arithmetic right bitwise shifts (`>> 1`) until rounding down to zero.
     - Explains what happens when Bitcoins are lost: private keys lost = coins permanently removed from circulation. As Satoshi explained: *"Lost coins only make everyone else's coins worth slightly more. Think of it as a donation to everyone."*
     - **Infinite Divisibility**: 1 BTC = 100,000,000 Satoshis ($10^{-8}$ BTC). Internally, Bitcoin Core stores all balances and values as 64-bit integer Satoshis (`int64_t`), never floats. Deflation cannot freeze trade because commerce seamlessly denominates in sats, bits, and Lightning millisats.
     - **Life After 2140**: Blocks continue to be mined every 10 minutes indefinitely, supported 100% by transaction fees.
   - **Satoshi Nakamoto's 2008 Whitepaper Insights**:
     - **Electronic Coin Definition (Section 2)**: Defined strictly as a *"chain of digital signatures"*. Double-spending is prevented by publishing transactions and ordering them through proof-of-work.
     - **The Gold Mining Analogy (Section 6)**: *"The steady addition of a constant amount of new coins is analogous to gold miners expending resources to add gold to circulation. In our case, it is CPU time and electricity that is expended."*
     - **Game-Theoretic Honesty**: Proves that an attacker with majority hashpower finds it strictly more profitable to play by the rules (collecting new coins and transaction fees) than to attack the network and destroy the validity of their own wealth.
     - **80-Byte Headers & SPV (Section 7 & 8)**: Generating an 80-byte header every 10 minutes totals only **4.2 MB per year** ($80\text{B} \times 52,596\text{ blocks}$). Pruned Merkle roots allow Simplified Payment Verification (SPV) where microcontrollers like the ESP32 can verify payments without full-node storage!
     - **The 6-Confirmation Rule (Section 11 — Gambler's Ruin)**: Modeling double-spend races as a Poisson / Binomial Random Walk, Satoshi proved with C code that against an attacker with 10% hashpower ($q=0.1$), waiting $z=6$ blocks drops the probability of a successful reversal exponentially to **$P = 0.0002428$ ($<0.024\%$)**, establishing 6 confirmations as the universal standard for settlement finality.
   - Detailed interactive breakdown of the **80 bytes you're actually hashing**, how `Bits` unpacks into the 256-bit target number, why headers use little-endian byte ordering, and why previous block hashes make the blockchain immutable.
   - **ExtraNonce & Merkle Trees**: Explains what happens when the 4.29B Nonce dial exhausts (incrementing ExtraNonce in the coinbase transaction generates a new Merkle Root, resetting the search space).
   - **Pool Payout Security**: Explains why miners cannot steal blocks (the pool payout address is committed into the coinbase and bound by the Merkle root; altering it scrambles the block hash and invalidates the proof-of-work).
   - Explains the target as the finish line: an independent roll of a 256-bit die where you win if `hash < target`.
3. **🪙 Interactive Bitcoin Supply & Satoshis Converter**:
   - Live bidirectional unit converter translating between **Bitcoin (BTC)**, **Satoshis (sats)**, **Bits ($\mu$BTC)**, and **Millibitcoins (mBTC)**.
   - Highlights internal 64-bit integer protocol storage and quotes Satoshi's law on lost coins.
4. **Conversational Plain-English Story**:
   - Translates raw metrics into normal English sentences: hashrate in hashes/sec, total hashes since boot, share submissions, best difficulty rarity, statistical wait time, and power consumption (~1.2 W).
5. **Historical Perspective Banner**:
   - Contrasts the 2009 Satoshi CPU era (~100 BTC in 3 hours with 8 zeros) with modern reality (18+ zeros, 800+ EH/s worldwide).
6. **AxeOS-Style Live Theme Switcher**:
   - Hot-swap between **6 visual palettes** with one click in `<30ms` without rebooting or dropping mining connections:
     - **Instrument Amber** *(Default)*
     - **Matrix Green**
     - **Cyber Amber**
     - **Arcade Neon**
     - **Ice Blue**
     - **Monochrome**
7. **Hardware Diagnostics & Quick Controls**:
   - Live telemetry for core temperature, Stratum ping latency, Wi-Fi RSSI signal quality, free heap RAM, and a 1-click **Session Reset** button (resets current session shares/best diff while preserving lifetime flash counters).
8. **Conversational Setup Guide**:
   - Inline blanks with 1-click solo pool presets (Public-Pool port 3333, NerdMiners.org, SoloMining.de), wallet & worker configuration, and screen rotation/brightness adjustments.
9. **REST API**:
   - Query live JSON statistics at `GET /api/stats`, switch themes at `POST /api/theme?id=X`, or reset session stats at `POST /api/reset-session`.

---

### 🏊 Recommended Low-Difficulty Solo Mining Pools

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

- **[Ken Shirriff's "Bitcoin Mining the Hard Way"](https://www.righto.com/2014/02/bitcoin-mining-hard-way-algorithms.html)**: Groundbreaking breakdown of Bitcoin mining down to the raw bytes, the Block #286,819 Python mining test vector, nonce progression table, ExtraNonce Merkle tree propagation, and Stratum V1 JSON-RPC wire protocol.
- **[Click Miner](https://github.com/owencoles/click-miner)**: Inspiration for the educational block header mental model, `LEARN.TXT` structure, and the 80-byte header component breakdown.
- **[guerrerocarlos/bitcoin-miner](https://github.com/guerrerocarlos/bitcoin-miner)**: Educational test vectors and first-principles implementation for target unpacking from compact bits ($\text{Target} = \text{Mantissa} \times 256^{\text{Exp}-3}$), little-endian binary header serialization, and byte-reversed hash comparison.
- **[Bitcoin Mining Simulator](https://github.com/mertokuyaz/bitcoinminingsimulator)**: Inspiration for the interactive browser-side Proof-of-Work guess-and-check simulator and historical context.
- **[HenrysCat/NerdMiner-Modded](https://github.com/HenrysCat/NerdMiner-Modded)**: Source for the performance-tuned firmware achieving ~990 KH/s, custom UI themes, and expanded board platform definitions.
- **[jpduhen/NerdMiner_v2-Cheap-Yellow-Display-2.4-and-2.8-single-and-double-USB](https://github.com/jpduhen/NerdMiner_v2-Cheap-Yellow-Display-2.4-and-2.8-single-and-double-USB)**: Webflasher interface and 2.4"/2.8" CYD single and dual USB hardware specifications.
- **[Fr4nkFletcher/NerdMiner_v2-Cheap-Yellow-Display](https://github.com/Fr4nkFletcher/NerdMiner_v2-Cheap-Yellow-Display)**: Original Cheap Yellow Display port and webflasher integration.
- **[BitMaker-hub/NerdMiner_v2](https://github.com/BitMaker-hub/NerdMiner_v2)**: The foundational NerdMiner v2 project that started the ESP32 solo mining revolution.
- **[ESP Web Tools](https://esphome.github.io/esp-web-tools/)**: Browser-based flashing engine powering the Web Flasher.

---

## 📜 License

This project is open-source and licensed under the [MIT License](LICENSE), matching the upstream NerdMiner_v2 foundation by BitMaker and Valerio Vaccaro. See [LICENSE](LICENSE) for full details.

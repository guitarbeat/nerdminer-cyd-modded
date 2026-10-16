# NerdMiner CYD — Repository & UI Conventions

## Embedded Display & Micro-UI Design (CYD, 240x320 SPI TFT)

- **Legibility & Minimum Font Floor**:
  - For small embedded TFT displays (e.g. 240x320 / 320x240, 2.4"–2.8" SPI screens viewed from 1–2 ft away), NEVER use font sizes below **10px** for uppercase labels or readable text (e.g. size 8 is unreadable).
  - Standard typography hierarchy:
    - Hero metrics: 34–42px
    - Section primary values: 16–22px
    - Sub-values & secondary units: 12–15px
    - Section labels & metadata: 10–11px (legibility floor)
- **Safe Character Encoding**:
  - Embedded micro-fonts and OpenFontRender sub-fonts lack extended Unicode glyphs. Avoid characters like degree (`°`), middle dots (`·`), emojis, or special bullets that render as blank missing boxes (`□`). Use standard ASCII fallbacks (`C`, `-`, `|`, `*`).
- **RAM & Zero-Sprite Allocation**:
  - Contiguous heap on ESP32 classic is strictly limited (~35–45 KB max single block).
  - Never allocate 16-bit sprites larger than 16 KB for full cards or plot panels.
  - Draw large panels (plots, halving progress bars, hero clocks) directly to the target/screen buffer using vertical offset translation (`dY(y)`) to consume **0 bytes of RAM**.
- **Multi-Core Thread Safety**:
  - Display rendering (`tft`, `wgt`, `shot`, and hardware gauge updates) MUST be protected by a display mutex (`g_displayMutex`) between background webserver capture handlers (Core 0) and the real-time miner/display loop (Core 1).
  - Physical display animations must only target physical hardware (`tft`), never offscreen screenshot sprites (`shot`), and must use non-blocking `try_lock()` to prevent frame stalls.
- **Metric Formatting & Units**:
  - Stacking long metrics into multi-line groups (e.g. value on top at 15–16px, uppercase label below at 10–11px) prevents horizontal truncation and preserves gauge aperture geometry.
  - When parsing uptime strings or raw seconds, handle both `d hh:mm:ss` formatted text and raw numeric seconds cleanly.
  - Default embedded miner thermal readouts to **Fahrenheit** (`F = round(C * 1.8 + 32)`), rendered as clean ASCII `F` (e.g. `121 F`) without Unicode degree marks (`°`).
- **Anti-Aliasing & Text Overdraw Prevention**:
  - OpenFontRender does not clear background pixels for zero-alpha glyph areas by default (`bg_fill_method == None`).
  - All dynamic numeric and text readouts MUST be rendered into a freshly cleared scratch sprite (`wBegin`/`wEnd`) or have their bounding rect cleared with `fillRect()` before drawing to prevent character ghosting.
- **Card Geometry & Border Clearance**:
  - Inner dynamic widget sprites and outer chrome panels must share identical bounding coordinates (`px, py, pw, ph`).
  - Maintain a minimum of 4–6px vertical clearance below font baselines to avoid clipping against card border lines (`C_EDGE`).
- **Geometric Icon Accuracy & Minimal Clutter**:
  - Never assemble icons with ad-hoc triangles without testing polygon symmetry. Prefer clean, mathematically balanced vector vertices or 1-bit bitmaps.
  - Keep metric card headers clean and centered; avoid decorative stars, dots, or unneeded glyphs.

## Mining Performance & Hardware Architecture (ESP32 Classic / D0WD)

- **SHA Hardware / DPORT Bus Contention Prevention**:
  - Never call `mbedtls` or touch DPORT/SHA hardware registers from Core 0 during active mining on classic ESP32 (D0WD).
  - On dual-core ESP32 classic, any concurrent DPORT register access while Core 1 executes the 247-cycle assembly loop (`pipelined_hw_mine_classic_v2.S`) causes silent silicon register corruption.
  - The firmware safety governor `pl_health` detects these verification failures and demotes the hardware loop to `fast, wide margins` (317 cycles, dropping HW hashrate from ~974 KH/s to ~757 KH/s, locking total rate at ~845 KH/s).
  - All display telemetry sampling (such as Screen 3 scatter plot or hash digests) MUST be decoupled: push real share digests from `runStratumWorker`, or sample at low rate in `runMonitor` using pure software SHA (`nerd_sha256d_baked`).
  - Core 0 is restricted to pure software hashing and WiFi/web telemetry.


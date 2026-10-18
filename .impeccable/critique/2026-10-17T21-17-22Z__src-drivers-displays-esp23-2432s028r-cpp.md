---
target: all screens (PNG screenshots)
total_score: 26
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 2
target_identity: "file:/Users/aaron/Projects/nerdminer-cyd-modded/src/drivers/displays/esp23_2432s028r.cpp"
target_fingerprint: "sha256:a9be6866ac0d70e277b77b421118bb7e3289d1f3192978eeca9c04ed413daf83"
target_path: /Users/aaron/Projects/nerdminer-cyd-modded/src/drivers/displays/esp23_2432s028r.cpp
timestamp: 2026-10-17T21-17-22Z
slug: src-drivers-displays-esp23-2432s028r-cpp
---
# Impeccable Design Critique: All Screens (ESP32-2432S024 240x320 TFT)

Method: design review only (detector not applicable, native platform)

### Design Health Score

| # | Heuristic | Score | Key Issue |
|---|-----------|-------|-----------|
| 1 | Visibility of System Status | 3 | API fetch failure shows bare `--` on Screen 1 with no reconnect/sync pulse |
| 2 | Match System / Real World | 3 | Halving reward shows past epoch (6.25 -> 3.125 BTC instead of 3.125 -> 1.5625 BTC) |
| 3 | User Control and Freedom | 2 | No screen index / progress dot indicator; user cannot tell where they are in cyclic rotation |
| 4 | Consistency and Standards | 2 | Top header is 12h ("3:58 PM") but Screen 2 hero clock is 24h ("15:58"); Screen 3 Y-axis ticks clip left edge |
| 5 | Error Prevention | 3 | Solid thermal/disconnect warnings; numeric scaling prevents buffer overruns |
| 6 | Recognition Rather Than Recall | 3 | Screen 0 "THIS BLOCK" (18.23M) and "LIFETIME" (41.35B) lack hash unit ("H") causing share confusion |
| 7 | Flexibility and Efficiency | 2 | Single fixed presentation per screen; no aggregated master cockpit screen |
| 8 | Aesthetic and Minimalist Design | 3 | Screen 1 suffers from "card-in-card" border noise (4 stacked boxes); Screen 0 tachometer is very clean |
| 9 | Error Recovery | 3 | Status tags ("ONLINE", "HOT", "LINKING") give clear immediate state feedback |
| 10 | Help and Documentation | 2 | Screen 3 explainer text is 8px (under 10px floor, unreadable at 2 ft); no web IP hint shown |
| **Total** | | **26/40** | **Acceptable (65%)** |

### Design Specificity Verdict

**LLM assessment**: Highly authored and distinct embedded instrument cluster tailored directly to Bitcoin solo mining. The 270° tachometer arc gauge with amber/redline segments on Screen 0 and the real-time log2 difficulty PoW scatter plot on Screen 3 are completely unique to this hardware domain, far exceeding generic IoT dashboard templates. However, several micro-display execution defects (clipped text, contradictory clock formats, card border fatigue) hold it back from top-tier instrument craft.

**Deterministic scan**: not applicable (native platform)

**Visual overlays**: not applicable (embedded micro-display hardware)

### Overall Impression
A genuinely ambitious, purpose-built crypto-hardware interface that successfully transforms a cheap \$10 ESP32 CYD into a high-end luxury desk ornament. Screen 0 (MINE) is a visual home-run with its automotive tachometer gauge, but Screen 1 feels boxed-in and dated, Screen 2 contradicts its own header clock format, and Screen 3 suffers from off-screen text clipping on its high-difficulty boundary.

### What's Working
1. **Screen 0 Tachometer Arc (Instrument Feel)**: The 30-segment radial gauge with orange progress, green peak-hold, and pink-red redline ticks immediately communicates hashrate scale and vitality without requiring textual parsing.
2. **Screen 2 Hashrate Sparkline**: The 60-second rolling bar chart provides instant visual confirmation that mining hashing threads have not stalled, with a clean grounded baseline.
3. **Unified Dual-KPI Header**: The new top-left trophy badge (`[🏆 0]`) and top-right 12-hour AM/PM clock (`3:58 PM`) establish a consistent hardware status frame across all screens without intruding on the display centers.

### Priority Issues

- **[P1] Clock Format Contradiction (Screen 2)**:
  - *Why it matters*: The top header displays `3:58 PM` (12-hour format), while the hero clock card directly below it displays `15:58` (24-hour format). This breaks visual consistency and directly conflicts with user expectations.
  - *Fix*: Format `c.hh` in `esp32_2432S028R_ClockScreen` using 12-hour conversion matching `fmt12HourTime()`, and append an `AM`/`PM` indicator or synchronize with the header format.
  - *Suggested command*: `$impeccable typeset`

- **[P1] Screen 3 Y-Axis Tick Clipping (`140.6T` -> `40.6T`)**:
  - *Why it matters*: In Screen 3 (PROTOCOL), the top difficulty boundary line renders `av.netDiff` (`140.6T`) right-aligned to `plotX + G - 4`. With `plotX = 6` and `G = 22`, the text starts at negative coordinate `x = -11`, truncating `140.6T` to `40.6T` on the physical LCD edge.
  - *Fix*: Increase left gutter `G` from `textW("1G", 8) + 8` to `textW(av.netDiff.c_str(), 9) + 6`, or place the target difficulty label inside the plot area right under the `BLOCK THRESHOLD` header banner.
  - *Suggested command*: `$impeccable layout`

- **[P2] Redundant Footer Metric on Screen 2 (`ONLINE` over `ONLINE`)**:
  - *Why it matters*: The rightmost column of the Screen 2 bottom strip renders a header label `"ONLINE"` and a status value `"ONLINE"` in green text. Labeling a field with the exact same word as its value is extraneous cognitive noise.
  - *Fix*: Change the column label from `"ONLINE"` to `"STATUS"` or `"POOL"` in `labs[2]`.
  - *Suggested command*: `$impeccable clarify`

- **[P2] Screen 1 Border Noise & Stale Halving Data**:
  - *Why it matters*: Screen 1 stacks 4 separate rounded cards (`BTC/USD`, `NETWORK/DIFF`, `FEE`, `HALVING`), creating 8 horizontal dividing borders in 320px. Furthermore, the block reward reads `6.25 -> 3.125 BTC`, which is the 2024 halving that already occurred; the next halving reward is `3.125 -> 1.5625 BTC`.
  - *Fix*: Remove individual inner card borders in favor of subtle horizontal hairline dividers, and correct the halving epoch calculation to `3.125 -> 1.5625 BTC`.
  - *Suggested command*: `$impeccable distill`

- **[P2] Screen 0 Telemetry Ambiguity (Hashes vs Shares)**:
  - *Why it matters*: The middle tiles display `18.23M` (THIS BLOCK) and `41.35B` (LIFETIME) with no unit qualifier. Most miners expect share counts here; displaying raw hash count without an `"H"` suffix leads to confusion with pool shares.
  - *Fix*: Render as `18.23 MH` and `41.35 GH` or replace `THIS BLOCK` with `SHARES (ACC/REJ)`.
  - *Suggested command*: `$impeccable clarify`

### Persona Red Flags

- **Alex (Power User)**: Looking for share submission feedback (accepted vs rejected count) and pool ping. Screen 0 hides share count entirely in favor of hash sums; Screen 2 shows uptime and temp but no pool latency. Alex cannot diagnose stratum stale rates without opening the web console.
- **Jordan (First-Timer)**: Screen 3 displays "waiting for samples..." and cryptic formulas (`hash < target -> valid block`, `RARER ^`). Jordan has no idea if the miner is working or broken during this state, and cannot read the 8px labels from across the desk.
- **Sam (Low-Vision / 2-Foot Viewing Distance)**: The 8px text on Screen 3 (`40.6T`, `1G`, `1M`, `1K`, `<1`, `hash < target`) fails the embedded 10px minimum legibility floor. From 2 feet away on a 2.4" display, these characters blur into unreadable noise.

### Minor Observations
- In Screen 1, when BTC price is `--`, the card looks deserted; adding a small muted `"SYNCING..."` or `"CONNECTING API..."` placeholder preserves intent.
- In Screen 2, the chart caption `LIVE SAMPLE - HASHING ACTIVE` is squeezed tightly against the bottom card border (`y = 244` vs bottom strip at `y = 269`), with uneven top/bottom padding.
- In Screen 0, the yellow `BEST 5.3333` in the bottom strip could benefit from a `#` prefix or label alignment to distinguish it from the `11h` uptime.

### Questions to Consider
- What if Screen 0 merged shares and difficulty into a clean 4-quadrant bento grid under the tachometer, eliminating the distinction between hashes and shares?
- What if Screen 1 adopted a full-bleed candlestick or line sparkline for BTC/USD, giving it the same instrument energy as Screen 0 and 2?
- What if Screen 3 enlarged the Y-axis ticks to 10px bold and automatically zoomed the scale based on the session's highest difficulty share?

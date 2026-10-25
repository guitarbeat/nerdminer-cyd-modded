# Mine chart: Screen 0 spectrogram, TAP-only sampler

Design notes for the CYD portrait (240x320) Mine chart. Code:
`src/drivers/displays/esp23_2432s028r.cpp` (`drawSharesFigure`),
`src/mining.{h,cpp}` (attempt sampler), `scripts/check_mining_starvation.py`.

## Bits, difficulty, odds (one quantity, three labels)

    bits  ~= 32 + log2(difficulty)     difficulty = 2^(bits - 32)
    odds per hash of >= bits zeros = 2^-bits = 1 in (difficulty * 2^32)

Difficulty 1 = 32 leading-zero bits. Bitcoin network (~1.4e14) ~= 79 bits.

## Screen 0: leading-zero-bit spectrogram

- Three lanes MISS | SHARE | BLOCK (labels once, top band). Bottom bins are
  numbers only.
- y = time, newest on top: `ATT_ROWS` 28 rows x `ATT_ROW_MS` (~2.14 s) = 60 s.
- x = 16 columns per lane, equal bits per column inside each lane:
  - MISS:  `col = floor(bits / (Tb/16))`            (~2 bits/col at diff 1)
  - SHARE: `col = floor((bits - Tb) / ((Nb-Tb)/16))` (1 bit/col if net unknown)
  - BLOCK: `col = floor(bits - Nb)`, 1 bit/col, clamped to 15
- Cells are 3x3 px squares (4 px column pitch, 5 px row pitch).
- Brightness = count per cell: 0 | 1 | 2-3 | 4-7 | 8+.
- Thresholds are live: `Tb = 32 + log2(poolDiff)` (share, cyan pill, e.g. 32),
  `Nb = 32 + log2(netDiff)` (network, gold pill, e.g. 79); `--` before the
  first job. No tick strip.
- Tap the MISS|SHARE|BLOCK band or chart body to toggle Screen 0 / 1
  (also `GET/POST /api/minechart?mode=0|1`).

## Screen 1: E target ruler

Unchanged: per-second max-bits sweep + staircase against 0 / 16 / share / NET.

## Sampler: why TAP-only

The miner does ~1e6 hashes/s; the chart sees ~4 of them per second.

Previously the ring had two sources, which drew **two columns** in MISS:

1. **TAP** - `hashTapSw()`: every `HASH_TAP_EVERY` (256) nonce of the core-0
   SW miner gets a full double-SHA, rate-capped >= 250 ms. Unbiased ordinary
   attempts -> geometric pile on the left (half land at 0 bits, a quarter at 1...).
2. **CAND** - `noteCandidate()`: hashes that passed the 16-bit prefilter.
   The prefilter is a mining speed trick, not a chart feature:
   `nerd_sha256d_baked` (`src/ShaTests/nerdSHA256plus.cpp`) checks after
   round 60 of the second SHA `if ((a7 & 0xFFFF) != 0x32E7) return false;`
   (0x32E7 = low half of -0x5BE0CD19, the last SHA-256 IV word) and skips
   the last rounds + digest writeout on ~65535/65536 nonces. Survivors are
   pre-selected at >= ~16 bits, so they piled up mid-lane.

Mixing a uniform sample with a pre-filtered one shows the filter, not the
miner. Screen 0 is meant to show "how good are my ordinary guesses", so the
ring is now **TAP-only**: `noteCandidate()` only counts (`s_candidates`,
reported as `attempts.candidates`) and feeds `updateBestDiff()`. The bit
value of a rejected nonce is unknown without finishing the hash, so there is
no free per-guess column; TAP pays for ~4 full hashes/s on core 0 only.

Expected picture: one left-heavy geometric falloff in MISS; SHARE/BLOCK light
only from the real share log.

## Hashrate discipline (~980-1000 KH/s, fast HW loop)

- Measured failure: repainting the figure ~5x/s starved the stratum/job feed,
  ~1000 -> ~690 KH/s.
- Layered redraw: rows pinned to absolute time; only the newest row repaints
  (<= 1/s, `ATT_DOTS_MIN_MS`); the full body only on row scroll, frame change
  or screenshot; MISS bin text <= every 5 s; dots layer is text-free.
- Never sample on the core-1 HW hot path: no `millis()`, locks, `hashTapSw`,
  `hashSamplePut` or Serial in `minerWorkerHw`; `noteCandidate` only behind a
  prefilter hit and must not sample/lock/read the clock.
- `python3 scripts/check_mining_starvation.py --selftest` pins all of this
  (13 mutations, each must be caught) and must pass before every flash.
- After each flash: settle ~3 min, take 3 `/api/status` readings; revert if
  hashrate drops.

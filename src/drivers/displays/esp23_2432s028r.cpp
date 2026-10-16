// "PULSE" theme for the ESP32-2432S028R ("CYD", 320x240 landscape).
//
// A dark, glassy, gauge-driven look: no bitmaps at all, everything is drawn
// from primitives so the palette can be re-tinted at runtime (tap the
// NERDMINER wordmark, top-left, to cycle accent colours).
//
//   MINE    segmented hashrate ring, stat tiles, pool strip
//   MARKET  BTC price, block height, fees, difficulty, halving progress
//   CLOCK   hero desk clock, current KH/s, last-60s hashrate spark
//
// The original theme is kept in esp23_2432s028r_classic.cpp (build the
// ESP32-2432S028R-Classic env, or define CYD_THEME_CLASSIC).
//
// Dev aid: -D CYD_UI_SCREENSHOT=1 renders every screen once at boot into an
// offscreen 8-bit sprite and dumps it over serial (see SHOT_BEGIN/SHOT_END).
#include "displayDriver.h"

#if (defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)) && !defined(CYD_THEME_CLASSIC)

#include <TFT_eSPI.h>
#include <TFT_eTouch.h>
#include <WiFi.h>
#include <Preferences.h>
#include "media/myFonts.h"
#include "version.h"
#include "monitor.h"
#include "mining.h"
#include "OpenFontRender.h"
#include <SPI.h>
#include "rotation.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"
#include "currency.h"
#include "rgbLed.h"

#define WIDTH 130
#define HEIGHT 170

extern nvMemory nvMem;
extern monitor_data mMonitor;
extern pool_data pData;
extern DisplayDriver *currentDisplayDriver;
extern bool invertColors;
extern TSettings Settings;
extern unsigned long mPoolUpdate;
extern String readCustomAPName(); // wManager.cpp

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI(); // pins defined in platformio.ini
SPIClass hSPI(HSPI);
TFT_eTouch<TFT_eSPI> touch(tft, ETOUCH_CS, 0xFF, hSPI);

// ---------------------------------------------------------------- palette --

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

struct ThemePalette {
  const char *name;
  uint16_t bg;
  uint16_t panel;
  uint16_t edge;
  uint16_t track;
  uint16_t text;
  uint16_t dim;
  uint16_t meter;
  uint16_t good;
  uint16_t warn;
  uint16_t bad;
  uint16_t gold;
};

static const ThemePalette THEMES[] = {
  // 0: Instrument Amber (Default) - Bitcoin Amber precision instrument
  { "Instrument Amber", RGB565(8, 10, 14), RGB565(16, 20, 28), RGB565(34, 42, 54), RGB565(24, 30, 40), RGB565(245, 247, 250), RGB565(175, 185, 200), RGB565(247, 147, 26), RGB565(52, 211, 153), RGB565(251, 191, 36), RGB565(248, 113, 113), RGB565(245, 158, 11) },
  // 1: Matrix Phosphor Green - Retro CRT terminal
  { "Matrix Green", RGB565(4, 8, 4), RGB565(8, 20, 10), RGB565(18, 52, 24), RGB565(12, 32, 16), RGB565(220, 252, 231), RGB565(120, 235, 160), RGB565(34, 197, 94), RGB565(74, 222, 128), RGB565(250, 204, 21), RGB565(248, 113, 113), RGB565(245, 158, 11) },
  // 2: Cyber Amber (1980s CRT) - Warm amber monochrome terminal
  { "Cyber Amber", RGB565(10, 8, 4), RGB565(24, 18, 8), RGB565(56, 40, 16), RGB565(36, 26, 12), RGB565(254, 243, 199), RGB565(245, 190, 100), RGB565(245, 158, 11), RGB565(52, 211, 153), RGB565(251, 191, 36), RGB565(248, 113, 113), RGB565(245, 158, 11) },
  // 3: Arcade Neon - Playful classic mod neon
  { "Arcade Neon", RGB565(10, 8, 18), RGB565(22, 16, 38), RGB565(54, 38, 86), RGB565(32, 24, 54), RGB565(245, 243, 255), RGB565(215, 175, 255), RGB565(6, 182, 212), RGB565(52, 211, 153), RGB565(251, 191, 36), RGB565(248, 113, 113), RGB565(236, 72, 153) },
  // 4: Ice Blue (Clean Slate) - Cool slate & ice blue tech
  { "Ice Blue", RGB565(10, 14, 22), RGB565(17, 24, 39), RGB565(35, 48, 68), RGB565(24, 34, 50), RGB565(240, 249, 255), RGB565(160, 225, 255), RGB565(56, 189, 248), RGB565(52, 211, 153), RGB565(251, 191, 36), RGB565(248, 113, 113), RGB565(245, 158, 11) },
  // 5: Minimalist Monochrome - E-ink stark white on black
  { "Monochrome", RGB565(0, 0, 0), RGB565(18, 18, 18), RGB565(48, 48, 48), RGB565(28, 28, 28), RGB565(255, 255, 255), RGB565(195, 195, 195), RGB565(255, 255, 255), RGB565(255, 255, 255), RGB565(200, 200, 200), RGB565(255, 255, 255), RGB565(255, 255, 255) },
};

static const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
static int currentThemeIdx = 0;

static uint16_t C_BG = THEMES[0].bg;
static uint16_t C_PANEL = THEMES[0].panel;
static uint16_t C_EDGE = THEMES[0].edge;
static uint16_t C_TRACK = THEMES[0].track;
static uint16_t C_TEXT = THEMES[0].text;
static uint16_t C_DIM = THEMES[0].dim;
static uint16_t C_GOOD = THEMES[0].good;
static uint16_t C_WARN = THEMES[0].warn;
static uint16_t C_BAD = THEMES[0].bad;
static uint16_t C_GOLD = THEMES[0].gold;
static uint16_t C_METER = THEMES[0].meter;

static int accentIdx = 0;
static uint16_t cAcc = THEMES[0].meter;
static uint16_t cAlt = THEMES[0].meter;

// t = 0..255 blends a -> b
static uint16_t mix565(uint16_t a, uint16_t b, uint8_t t)
{
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + ((br - ar) * t) / 255;
  int g = ag + ((bg - ag) * t) / 255;
  int bl = ab + ((bb - ab) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// --------------------------------------------------------------- geometry --

#define SCR_W 320
#define SCR_H 240
#define HDR_H 26 // header band; hairline sits on the row below

#define SCR_MINE 0
#define SCR_MARKET 1
#define SCR_CLOCK 2
#define SCR_ARCH 3

extern volatile uint32_t g_stratumPing;
extern double g_networkDifficulty;
extern bool g_usingFallback;
extern global_data gData;

static inline bool isPortrait()
{
  int r = tft.getRotation();
  return (r == 0 || r == 2);
}

// Mine ring owns most of the upper screen (portrait) / left column (landscape).
static inline int gCX() { return isPortrait() ? 120 : 100; }
static inline int gCY() { return isPortrait() ? 128 : 116; }
static inline int gR()  { return isPortrait() ? 98 : 86; }
static inline int gRI() { return isPortrait() ? 84 : 74; }

// Hashrate ring
#define G_CX (gCX())
#define G_CY (gCY())
#define G_R (gR())
#define G_RI (gRI())
#define G_SEGS 30
#define G_MAX_KH 1200.0f // full-scale of the ring: 40 KH/s per segment (the CYD runs ~950)

// Bottom strip (pool cells / hashrate history)
#define STRIP_Y 198
#define STRIP_H 38

// --------------------------------------------------------------- UI state --

static int uiScreen = -1;    // screen whose chrome is currently on the panel
static bool uiLive = false;  // false while the boot / setup screens own the panel
static float gaugeTarget = 0;
static float gaugeShown = 0;
static uint8_t segLvl[G_SEGS];
static unsigned long shareFlashUntil = 0;
static uint32_t lastShares = 0;

// history of hashrate for the CLOCK screen (last ~60 seconds)
#define HIST_N 60
#define HIST_EVERY_MS 1000
static float hist[HIST_N];
static uint8_t histCount = 0;
static uint32_t histEpoch = 0;
static unsigned long histLastMs = 0;

// Every widget remembers the "key" it last drew so it only repaints on change.
enum WidgetId
{
  W_HDR,
  W_HERO,
  W_TOTAL,
  W_T0, // 4 tiles
  W_T1,
  W_T2,
  W_T3,
  W_P0, // 4 strip cells
  W_P1,
  W_P2,
  W_P3,
  W_PRICE,
  W_BLOCK,
  W_M0, // 3 market tiles
  W_M1,
  W_M2,
  W_HALV,
  W_CANDLES,
  W_HH,
  W_COLON,
  W_MM,
  W_DATE,
  W_C0, // 3 clock tiles
  W_C1,
  W_C2,
  W_CHART,
  W_EXP_STREAM,
  W_EXP_TEXT,
  W_LOT_JACKPOT,
  W_LOT_WORK,
  W_LOT_DIFF,
  W_DIAG_NET,
  W_DIAG_WIFI,
  W_DIAG_TEMP,
  W_DIAG_RAM,
  W_DIAG_UPTIME,
  W_HDR_LIVE,
  W_DIAG_ARC0, // mini arc: core temp
  W_DIAG_ARC1, // mini arc: free heap
  W_DIAG_ARC2, // mini arc: ping
  W_ECON_ARC,  // mini arc: odds meter (best diff / net diff)
  W_PROTO_PLOT,
  W_PROTO_FOOT,
  W_COUNT
};
static String wkey[W_COUNT];

static bool need(int id, const String &k) { return wkey[id] != k; }
static void done(int id, const String &k) { wkey[id] = k; }
static void resetWidgets()
{
  for (int i = 0; i < W_COUNT; i++)
    wkey[i] = "\x01";
}

// ---------------------------------------------------------- output target --
// Everything draws either to the panel or (dev builds) to an offscreen shot.

static TFT_eSprite wgt = TFT_eSprite(&tft); // scratch sprite reused by every widget

#ifdef CYD_UI_SCREENSHOT
static TFT_eSprite shot = TFT_eSprite(&tft);
static bool shotOn = false;
static TFT_eSPI &out() { return shotOn ? (TFT_eSPI &)shot : tft; }
#else
static TFT_eSPI &out() { return tft; }
#endif

static bool wBegin(int w, int h, uint16_t bg)
{
  wgt.setColorDepth(16);
  wgt.createSprite(w, h);
  if (!wgt.created())
  {
    Serial.printf("[ui] sprite %dx%d alloc failed (free %u, max block %u)\n", w, h,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return false;
  }
  wgt.fillSprite(bg);
  return true;
}

static void wEnd(int x, int y)
{
#ifdef CYD_UI_SCREENSHOT
  if (shotOn)
  {
    wgt.pushToSprite(&shot, x, y);
    wgt.deleteSprite();
    return;
  }
#endif
  wgt.pushSprite(x, y);
  wgt.deleteSprite();
}

// ------------------------------------------------------------ draw helpers --

// fillScreen() is not virtual in TFT_eSPI, so it would ignore sprite targets
static void clearScreen(TFT_eSPI &d) { d.fillRect(0, 0, d.width(), d.height(), C_BG); }

static void text(TFT_eSPI &d, const char *s, int x, int y, unsigned size, uint16_t fg, uint16_t bg,
                 Align a = Align::TopLeft)
{
  render.setDrawer(d);
  render.setFontSize(size);
  render.setAlignment(a);
  render.drawString(s, x, y, fg, bg);
}

static int textW(const char *s, unsigned size)
{
  render.setFontSize(size);
  return render.getTextWidth("%s", s);
}

// value + smaller unit, centred on cx
static void textPair(TFT_eSPI &d, int cx, int y, const char *val, const char *unit, unsigned size,
                     uint16_t cv, uint16_t cu, uint16_t bg)
{
  unsigned usz = size > 12 ? size - 3 : size;
  int wv = textW(val, size);
  int wu = textW(unit, usz);
  int x0 = cx - (wv + 4 + wu) / 2;
  text(d, val, x0, y, size, cv, bg);
  text(d, unit, x0 + wv + 4, y + (size - usz), usz, cu, bg);
}

// structural cell: crisp 1px hairline border, flat surface
static void panel(TFT_eSPI &d, int x, int y, int w, int h, int r, uint16_t fill, uint16_t edge, uint16_t behind)
{
  d.fillRect(x, y, w, h, fill);
  d.drawRect(x, y, w, h, edge);
}

// horizontal gradient bar (alt -> accent) with softly rounded ends
static void gradBar(TFT_eSPI &d, int x, int y, int w, int h, uint16_t bg)
{
  for (int i = 0; i < w; i++)
  {
    int inset = 0;
    int e = i < w - 1 - i ? i : w - 1 - i; // distance to nearest end
    if (h >= 6)
      inset = e < 1 ? 2 : (e < 3 ? 1 : 0);
    uint16_t c = mix565(cAlt, cAcc, (uint8_t)(w > 1 ? i * 255 / (w - 1) : 255));
    d.drawFastVLine(x + i, y + inset, h - 2 * inset, c);
  }
  (void)bg;
}

// 240° sweep mini arc gauge drawn into the current wgt sprite.
// Inspired by CarCYD's mini_gauge widget (lv_arc 240° rotation at 150°).
// val = 0.0..1.0 (fill fraction). warnAt/critAt are 0..1 zone thresholds.
// invert=true flips severity: high val = good (e.g. free heap).
static void drawMiniArcInSprite(int cx, int cy, int r, int ri, float val,
                                 float warnAt, float critAt, bool invert)
{
  const int startAngle = 150;
  const int sweepTotal = 240;
  if (val < 0.0f) val = 0.0f;
  if (val > 1.0f) val = 1.0f;

  uint16_t fillCol;
  if (!invert)
    fillCol = (val >= critAt) ? C_BAD : (val >= warnAt ? C_WARN : C_GOOD);
  else
    fillCol = (val <= critAt) ? C_BAD : (val <= warnAt ? C_WARN : C_GOOD);

  // Track (full 240°)
  wgt.drawArc(cx, cy, r, ri, startAngle, startAngle + sweepTotal, C_TRACK, C_PANEL, true);
  // Fill
  if (val > 0.005f)
  {
    int fillEnd = startAngle + (int)(val * sweepTotal + 0.5f);
    if (fillEnd > startAngle + sweepTotal) fillEnd = startAngle + sweepTotal;
    wgt.drawArc(cx, cy, r, ri, startAngle, fillEnd, fillCol, C_PANEL, true);
  }
}

static String commas(const String &s)
{
  int n = s.length();
  for (int i = 0; i < n; i++)
    if (!isDigit(s[i]))
      return s;
  String o;
  for (int i = 0; i < n; i++)
  {
    o += s[i];
    int rem = n - 1 - i;
    if (rem > 0 && rem % 3 == 0)
      o += ',';
  }
  return o;
}

static String fmtPrice(const String &s)
{
  // btcPrice is "<ascii prefix><digits>"; swap the prefix for the real symbol
  // (the embedded Pulse font carries the glyphs for all of them)
  int i = 0;
  while (i < (int)s.length() && !isdigit((unsigned char)s[i]))
    i++;
  String digits = s.substring(i);
  if (digits.toInt() <= 0)
    return "--";
  return String(currencyFor(Settings.Currency).symbol) + commas(digits);
}

static String btcPairLabel()
{
  const CoinProfile &coin = getActiveCoinProfile();
  String c = currencyFor(Settings.Currency).code;
  c.toUpperCase();
  return String(coin.ticker) + " / " + c;
}

static String fmtUptime(const String &s)
{
  int d = 0, h = 0, m = 0, sec = 0;
  sscanf(s.c_str(), "%d %d:%d:%d", &d, &h, &m, &sec);
  char b[16];
  if (d > 0)
    snprintf(b, sizeof(b), "%dd %02dh", d, h);
  else
    snprintf(b, sizeof(b), "%02d:%02d:%02d", h, m, sec);
  return String(b);
}

static String fmtDate(const String &s)
{
  static const char *mon[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  static const char *wd[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int d = 0, m = 0, y = 0;
  if (sscanf(s.c_str(), "%d/%d/%d", &d, &m, &y) != 3 || m < 1 || m > 12)
    return "";
  int yy = y - (m < 3 ? 1 : 0);
  int dow = (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7;
  char b[32];
  snprintf(b, sizeof(b), "%s  %d %s %d", wd[dow], d, mon[m - 1], y);
  return String(b);
}

// "233.8B" style humanised count of total hashes (input in MH).
static String fmtHashCount(const String &mhashes)
{
  double h = mhashes.toDouble() * 1e6;
  static const char *u[] = {"", "K", "M", "B", "T", "Q"};
  int i = 0;
  while (h >= 1000.0 && i < 5)
  {
    h /= 1000.0;
    i++;
  }
  char b[24];
  if (i == 0)
    snprintf(b, sizeof(b), "%.0f", h);
  else
    snprintf(b, sizeof(b), h >= 100 ? "%.1f%s" : "%.2f%s", h, u[i]);
  return String(b);
}

// compact uptime: "2d", "5h", "12m"
static String fmtUptimeShort(const String &s)
{
  int d = 0, h = 0, m = 0, sec = 0;
  sscanf(s.c_str(), "%d %d:%d:%d", &d, &h, &m, &sec);
  char b[12];
  if (d > 0)
    snprintf(b, sizeof(b), "%dd", d);
  else if (h > 0)
    snprintf(b, sizeof(b), "%dh", h);
  else
    snprintf(b, sizeof(b), "%dm", m);
  return String(b);
}

static String fmtTotalHashes(const String &mhashes)
{
  double m = mhashes.toDouble();
  if (m >= 1000.0)
  {
    char b[32];
    snprintf(b, sizeof(b), "%.2f Billion", m / 1000.0);
    return String(b);
  }
  else if (m > 0.0)
  {
    char b[32];
    snprintf(b, sizeof(b), "%.1f Million", m);
    return String(b);
  }
  return "0 Hashes";
}

static const char *wifiSignalLabel(int rssi)
{
  if (rssi > -55) return "Excellent";
  if (rssi > -67) return "Good";
  if (rssi > -80) return "Fair";
  return "Weak";
}

static int wifiBars()
{
  if (WiFi.status() != WL_CONNECTED)
    return 0;
  int r = WiFi.RSSI();
  return r > -55 ? 4 : r > -65 ? 3
                   : r > -75   ? 2
                   : r > -85   ? 1
                               : 0;
}

static float lastKh = 0;

// The firmware never sets NM_hashing itself (status stays NM_Connecting once
// WiFi is up), so derive what the header shows from what is actually happening.
static int uiStatus()
{
  if (mMonitor.NerdStatus == NM_waitingConfig)
    return NM_waitingConfig;
  return (WiFi.status() == WL_CONNECTED && lastKh > 0) ? NM_hashing : NM_Connecting;
}

static void noteHashrate(float kh)
{
  lastKh = kh;
  unsigned long now = millis();
  if (histCount != 0 && now - histLastMs < HIST_EVERY_MS)
    return;
  histLastMs = now;
  if (histCount < HIST_N)
    hist[histCount++] = kh;
  else
  {
    memmove(hist, hist + 1, (HIST_N - 1) * sizeof(float));
    hist[HIST_N - 1] = kh;
  }
  histEpoch++;
}

// -------------------------------------------------------------- gauge ring --

static void gaugeDraw(TFT_eSPI &d, float v)
{
  float pos = v * G_SEGS;
  for (int i = 0; i < G_SEGS; i++)
  {
    float f = pos - i;
    uint8_t lvl = f >= 1.0f ? 4 : (f <= 0.05f ? 0 : 1 + (int)(f * 3.0f)); // 0, 1..3 (partial), 4
    if (lvl == segLvl[i])
      continue;
    // CarCYD-style zone coloring: last ~20% transitions warm→red (redline)
    uint16_t mCol = (i >= 27) ? C_BAD : (i >= 24 ? C_WARN : C_METER);
    uint16_t tCol = (i >= 27) ? mix565(C_BAD, C_BG, 64) : C_TRACK;
    uint16_t col = lvl == 0 ? tCol : (lvl == 4 ? mCol : mix565(tCol, mCol, lvl * 64));
    int a0 = 45 + i * 9; // 270 degree sweep, gap at 6 o'clock
    d.drawArc(G_CX, G_CY, G_R, G_RI, a0, a0 + 7, col, C_BG, true);
    segLvl[i] = lvl;
  }
}

// ------------------------------------------------------------------ header --

struct HdrV
{
  String time;
  String ip;
  int bars;
  int status;
  int screen; // current screen index (0-5) for nav dots
};

static void drawHeader(const HdrV &h)
{
  char k[80];
  snprintf(k, sizeof(k), "%s|%s|%d|%d|%d|%d", h.time.c_str(), h.ip.c_str(), h.bars, h.status, isPortrait() ? 1 : 0, h.screen);
  if (!need(W_HDR, k))
    return;
  const int X0 = 10, W = (isPortrait() ? 240 : 320) - X0;
  if (!wBegin(W, HDR_H, C_BG))
    return;

  // Dynamic Screen Title in the top-left corner
  static const char *const SCREEN_TITLES[4] = {
      "MINE",
      "MARKET",
      "CLOCK",
      "PROTOCOL"
  };
  const char *title = (h.screen >= 0 && h.screen < 4) ? SCREEN_TITLES[h.screen] : "MINER";
  // MARKET / PROTOCOL / CLOCK: compact "TITLE|HASHING" wordmark (mock).
  // CLOCK: no top-right clock — the hero IS the time.
  const bool wordmark = (h.screen == SCR_ARCH || h.screen == SCR_MARKET || h.screen == SCR_CLOCK);
  const bool hideTime = (h.screen == SCR_CLOCK);
  if (wordmark)
  {
    text(wgt, title, 0, 5, 12, C_TEXT, C_BG);
    int wm0 = textW(title, 12);
    text(wgt, "|", wm0 + 1, 5, 12, C_DIM, C_BG);
    const char *lab = (h.status == NM_hashing) ? "HASHING" : (h.status == NM_Connecting ? "LINKING" : "IDLE");
    uint16_t col = (h.status == NM_hashing) ? C_GOOD : (h.status == NM_Connecting ? C_WARN : C_DIM);
    text(wgt, lab, wm0 + 8, 5, 12, col, C_BG);
  }
  else
  {
    text(wgt, title, 0, 5, 13, C_TEXT, C_BG);
  }
  int wm = wordmark
               ? (textW(title, 12) + 8 + textW((h.status == NM_hashing) ? "HASHING" : (h.status == NM_Connecting ? "LINKING" : "IDLE"), 12))
               : textW(title, 13);

  // Status: purely typographic indicator with no dot prefix to save space
  const char *label = "SETUP";
  uint16_t col = C_DIM;
  if (h.status == NM_hashing)
  {
    label = "HASHING";
    col = C_GOOD;
  }
  else if (h.status == NM_Connecting)
  {
    label = "LINKING";
    col = C_WARN;
  }
  int sx = wm + (isPortrait() ? 8 : 12);
  // Wordmark already shows HASHING — skip duplicate.
  if (!wordmark)
    text(wgt, label, sx, 6, 9, col, C_BG);
  else
    sx = wm + 4;

  // Optional green status dot (CLOCK / hashing) after the wordmark
  if (h.screen == SCR_CLOCK && h.status == NM_hashing)
  {
    wgt.fillCircle(sx + 6, 13, 3, C_GOOD);
    sx += 14;
  }

  // Time (right-aligned) — omitted on CLOCK (hero owns the clock)
  int gapRight = W - 8;
  if (!hideTime)
  {
    text(wgt, h.time.c_str(), W - 4, 5, 13, C_TEXT, C_BG, Align::TopRight);
    int timeW = textW(h.time.c_str(), 13);
    int timeLeft = (W - 4) - timeW;
    gapRight = timeLeft - 8;
    // Local IP (landscape only, quiet secondary information to the left of time)
    if (!isPortrait() && h.ip.length())
    {
      int ipRight = timeLeft - 14;
      text(wgt, h.ip.c_str(), ipRight, 6, 10, C_DIM, C_BG, Align::TopRight);
      gapRight = ipRight - textW(h.ip.c_str(), 10) - 10;
    }
  }

  // ── CarCYD-inspired nav dots: 4 tiny pills in the mid-header gap ──
  {
    const int nScreens = 4;
    const int dotW = 7, dotH = 3, dotGap = 2;
    int totalW = nScreens * dotW + (nScreens - 1) * dotGap;
    int dotY = 12; // y of top edge, centered vertically in 26px header

    int statusEnd = wordmark ? sx : (sx + textW(label, 9) + 6);
    int center = (statusEnd + gapRight) / 2;
    int dotX0 = center - totalW / 2;
    if (dotX0 < statusEnd + 4) dotX0 = statusEnd + 4;

    for (int i = 0; i < nScreens; i++)
    {
      int dx = dotX0 + i * (dotW + dotGap);
      uint16_t dc = (i == h.screen) ? C_METER : C_TRACK;
      // Pill shape: filled rect with rounded ends via fillCircle
      wgt.fillRect(dx + 1, dotY, dotW - 2, dotH, dc);
      wgt.fillCircle(dx + 1, dotY + dotH / 2, dotH / 2, dc);
      wgt.fillCircle(dx + dotW - 2, dotY + dotH / 2, dotH / 2, dc);
    }
  }

  wEnd(X0, 0);
  done(W_HDR, k);
}

static void drawPulse(unsigned long frame)
{
  // No-op: eliminated distracting pulsing dot
}

// -------------------------------------------------------------- pool strip --

struct StripV
{
  String l[4];
  String v[4];
};

static void chromeStrip(TFT_eSPI &d)
{
  if (isPortrait())
    panel(d, 6, 266, 228, 48, 8, C_PANEL, C_EDGE, C_BG);
  else
    panel(d, 8, STRIP_Y, 304, STRIP_H, 8, C_PANEL, C_EDGE, C_BG);
}

static void drawStrip(const StripV &s)
{
  int sy = isPortrait() ? 269 : (STRIP_Y + 3);
  int cw = isPortrait() ? 55 : 72;
  int ch = isPortrait() ? 42 : 32;
  int xstep = isPortrait() ? 57 : 74;
  int x0 = isPortrait() ? 7 : 12;
  for (int i = 0; i < 4; i++)
  {
    String k = s.l[i] + "|" + s.v[i] + "|" + accentIdx + "|" + (isPortrait() ? "P" : "L");
    if (!need(W_P0 + i, k))
      continue;
    if (!wBegin(cw, ch, C_PANEL))
      continue;
    text(wgt, s.l[i].c_str(), isPortrait() ? 2 : 4, isPortrait() ? 2 : 2, isPortrait() ? 10 : 11, C_DIM, C_PANEL);
    text(wgt, s.v[i].c_str(), isPortrait() ? 2 : 4, isPortrait() ? 17 : 15, isPortrait() ? 14 : 15, C_TEXT, C_PANEL);
    wEnd(x0 + xstep * i, sy);
    done(W_P0 + i, k);
  }
}

static void chromeStripCells(TFT_eSPI &d)
{
  chromeStrip(d);
  if (isPortrait())
  {
    for (int i = 1; i < 4; i++)
      d.drawFastVLine(6 + 57 * i, 272, 36, C_EDGE);
  }
  else
  {
    for (int i = 1; i < 4; i++)
      d.drawFastVLine(12 + 74 * i, STRIP_Y + 8, STRIP_H - 16, C_EDGE);
  }
}

// generic stat tile (quiet uppercase label over bold value, crisp hairline border)
static void drawTile(int id, int x, int y, int w, int h, const char *label, const char *value,
                     uint16_t valCol, uint16_t edge)
{
  char k[64];
  snprintf(k, sizeof(k), "%s|%s|%u|%u", label, value, valCol, edge);
  if (!need(id, k))
    return;
  if (!wBegin(w, h, C_BG))
    return;
  wgt.fillRect(0, 0, w, h, C_PANEL);
  wgt.drawRect(0, 0, w, h, edge);
  int top = (h - 26) / 2;
  text(wgt, label, 8, top - 1, 10, C_DIM, C_PANEL);
  text(wgt, value, 8, top + 11, strlen(value) > 8 ? 13 : 16, valCol, C_PANEL);
  wEnd(x, y);
  done(id, k);
}

// ============================================================ MINE screen ==

struct MineV
{
  HdrV hdr;
  String hash, shares, best, tmpl, blocks, total;
  float kh;
  bool flash;
  StripV strip; // 3 cells: v = big value, l = trailing label ("1" "worker")
};

// Mine layout (mock): big ring + hero, two equal cards, 3-cell footer strip.
//   Portrait : ring cy=128 r=98, cards y=222..264, strip y=270..314
//   Landscape: ring left cx=100 cy=116 r=86, cards right column, strip y=198
#define MINE_CARD_PY 222
#define MINE_CARD_PH 42
#define MINE_STRIP_PY 270
#define MINE_STRIP_PH 44

static void mineStripGeom(int &x, int &y, int &w, int &h)
{
  if (isPortrait()) { x = 6; y = MINE_STRIP_PY; w = 228; h = MINE_STRIP_PH; }
  else { x = 8; y = STRIP_Y; w = 304; h = STRIP_H; }
}

static void chromeMine(TFT_eSPI &d)
{
  memset(segLvl, 0xFF, sizeof(segLvl));
  gaugeDraw(d, gaugeShown);
  int x, y, w, h;
  mineStripGeom(x, y, w, h);
  panel(d, x, y, w, h, 8, C_PANEL, C_EDGE, C_BG);
  for (int i = 1; i < 3; i++)
    d.drawFastVLine(x + (w * i) / 3, y + 8, h - 16, C_EDGE);
}

// small 5-point star filled into the wgt sprite
static void wStar(int cx, int cy, int r, uint16_t col)
{
  float ox[5], oy[5], ix[5], iy[5];
  for (int i = 0; i < 5; i++)
  {
    float a = -PI / 2 + i * 2 * PI / 5;
    ox[i] = cx + r * cosf(a);
    oy[i] = cy + r * sinf(a);
    ix[i] = cx + r * 0.42f * cosf(a + PI / 5);
    iy[i] = cy + r * 0.42f * sinf(a + PI / 5);
  }
  for (int i = 0; i < 5; i++)
  {
    wgt.fillTriangle(cx, cy, ox[i], oy[i], ix[i], iy[i], col);
    wgt.fillTriangle(cx, cy, ix[i], iy[i], ox[(i + 1) % 5], oy[(i + 1) % 5], col);
  }
}

static void drawMineCard(int id, int x, int y, int w, int h, const char *label, const char *value,
                         uint16_t valCol, bool star)
{
  char k[64];
  snprintf(k, sizeof(k), "mc|%s|%s|%u|%d", label, value, valCol, accentIdx);
  if (!need(id, k))
    return;
  if (!wBegin(w, h, C_BG))
    return;
  wgt.fillRect(0, 0, w, h, C_PANEL);
  wgt.drawRect(0, 0, w, h, C_EDGE);
  int lw = textW(label, 10) + (star ? 12 : 0);
  int lx = (w - lw) / 2;
  if (star)
  {
    wStar(lx + 4, 9, 5, C_GOLD);
    lx += 12;
  }
  text(wgt, label, lx, 4, 10, C_DIM, C_PANEL);
  unsigned vs = h >= 60 ? 24 : 20;
  if (strlen(value) > 8)
    vs -= 4;
  text(wgt, value, w / 2, h >= 60 ? 26 : 17, vs, valCol, C_PANEL, Align::TopCenter);
  wEnd(x, y);
  done(id, k);
}

static void drawMineStrip(const StripV &s)
{
  int x, y, w, h;
  mineStripGeom(x, y, w, h);
  int cw = w / 3 - 4, ch = h - 6;
  for (int i = 0; i < 3; i++)
  {
    String k = "m3|" + s.l[i] + "|" + s.v[i] + "|" + (isPortrait() ? "P" : "L");
    if (!need(W_P0 + i, k))
      continue;
    if (!wBegin(cw, ch, C_PANEL))
      continue;
    unsigned vs = isPortrait() ? 16 : 15, ls = isPortrait() ? 10 : 11;
    int wv = textW(s.v[i].c_str(), vs), wl = textW(s.l[i].c_str(), ls);
    int x0 = (cw - (wv + 4 + wl)) / 2;
    int ty = (ch - vs) / 2;
    text(wgt, s.v[i].c_str(), x0, ty, vs, C_TEXT, C_PANEL);
    text(wgt, s.l[i].c_str(), x0 + wv + 4, ty + (vs - ls) - 1, ls, C_DIM, C_PANEL);
    wEnd(x + (w * i) / 3 + 2, y + 3);
    done(W_P0 + i, k);
  }
}

static void paintMine(const MineV &m)
{
  drawHeader(m.hdr);

  const bool P = isPortrait();
  int cx = G_CX;
  int cy = G_CY;

  // hero readout inside the ring: HASHRATE / value / KH/s / hashes attempted
  String k = m.hash + "|" + m.total + "|" + (P ? "P" : "L");
  if (need(W_HERO, k))
  {
    // sprite corners stay inside RI (top) or the 6-o'clock gap (bottom)
    const int hw = P ? 124 : 110, hh = P ? 112 : 100;
    if (wBegin(hw, hh, C_BG))
    {
      int c = hw / 2;
      text(wgt, "HASHRATE", c, 2, P ? 11 : 10, C_DIM, C_BG, Align::TopCenter);
      unsigned sz = P ? (m.hash.length() <= 5 ? 40 : 34) : (m.hash.length() <= 5 ? 34 : 28);
      text(wgt, m.hash.c_str(), c, P ? 18 : 16, sz, C_TEXT, C_BG, Align::TopCenter);
      text(wgt, "KH/s", c, P ? 62 : 54, P ? 13 : 12, C_METER, C_BG, Align::TopCenter);
      String att = fmtHashCount(m.total) + " hashes attempted";
      text(wgt, att.c_str(), c, P ? 90 : 80, P ? 10 : 9, C_DIM, C_BG, Align::TopCenter);
      wEnd(cx - c, cy - (P ? 44 : 40));
      done(W_HERO, k);
    }
  }

  uint16_t sharesCol = m.flash ? C_GOOD : C_TEXT;
  if (P)
  {
    drawMineCard(W_T0, 6, MINE_CARD_PY, 112, MINE_CARD_PH, "SHARES", m.shares.c_str(), sharesCol, false);
    drawMineCard(W_T1, 122, MINE_CARD_PY, 112, MINE_CARD_PH, "BEST", m.best.c_str(), C_GOLD, true);
  }
  else
  {
    drawMineCard(W_T0, 198, 34, 114, 76, "SHARES", m.shares.c_str(), sharesCol, false);
    drawMineCard(W_T1, 198, 116, 114, 76, "BEST", m.best.c_str(), C_GOLD, true);
  }

  drawMineStrip(m.strip);
}

// ========================================================== MARKET screen ==

struct MarketV
{
  HdrV hdr;
  String price, block, diff, net;
  int age;          // minutes since tip change, -1 = unknown (height only)
  int fee, feeHour, feeFast; // sat/vB, 0 = unknown
  int pct;
  long remaining;
  String eta;       // "~Apr 2028" or ""
  long height;      // raw height (reward math)
};

// Market layout (mock):
//   Portrait : hero y=30..104, NETWORK|DIFFICULTY y=108..158, FEES y=162..218,
//              NEXT HALVING y=222..316.
//   Landscape: hero y=30..86, NETWORK|DIFFICULTY y=90..134, FEES y=138..178,
//              NEXT HALVING y=182..236.
struct MkGeom { int hx, hy, hw, hh, ry, rh, cw, fy, fh, vy, vh; };
static MkGeom mkGeom()
{
  if (isPortrait()) return {6, 30, 228, 74, 108, 50, 112, 162, 56, 222, 94};
  return {8, 30, 304, 56, 90, 44, 150, 138, 40, 182, 54};
}

static void chromeMarket(TFT_eSPI &d)
{
  MkGeom g = mkGeom();
  panel(d, g.hx, g.hy, g.hw, g.hh, 8, C_PANEL, C_EDGE, C_BG);
  panel(d, g.hx, g.ry, g.cw, g.rh, 8, C_PANEL, C_EDGE, C_BG);
  panel(d, g.hx + g.hw - g.cw, g.ry, g.cw, g.rh, 8, C_PANEL, C_EDGE, C_BG);
  panel(d, g.hx, g.fy, g.hw, g.fh, 8, C_PANEL, C_EDGE, C_BG);
  panel(d, g.hx, g.vy, g.hw, g.vh, 8, C_PANEL, C_EDGE, C_BG);
}

// classify the half-hour fee; returns nullptr when unknown
static const char *feeClass(int fee, uint16_t &col)
{
  if (fee <= 0) return nullptr;
  if (fee <= 4) { col = C_GOOD; return "LOW"; }
  if (fee <= 20) { col = C_METER; return "NORMAL"; }
  col = C_BAD;
  return "BUSY";
}

static String fmtReward(float r)
{
  String s = String(r, 4);
  while (s.endsWith("0")) s.remove(s.length() - 1);
  if (s.endsWith(".")) s.remove(s.length() - 1);
  return s;
}

// "3.125 -> 1.5625 BTC" right-aligned at xr, with a drawn arrow (font-safe)
static void drawRewardRight(int xr, int y, unsigned sz, const String &a, const String &b)
{
  int wa = textW(a.c_str(), sz), wb = textW(b.c_str(), sz);
  const int aw = 14;
  int x0 = xr - (wa + aw + wb);
  text(wgt, a.c_str(), x0, y, sz, C_DIM, C_PANEL);
  int ax = x0 + wa + 3, ay = y + sz / 2;
  wgt.drawFastHLine(ax, ay, 7, C_DIM);
  wgt.fillTriangle(ax + 6, ay - 3, ax + 6, ay + 3, ax + 9, ay, C_DIM);
  text(wgt, b.c_str(), x0 + wa + aw, y, sz, C_GOLD, C_PANEL);
}

static void paintMarket(const MarketV &m)
{
  drawHeader(m.hdr);
  const bool P = isPortrait();
  MkGeom g = mkGeom();

  // 1. Hero: BTC / USD price + latest block
  String bl = "BLOCK #" + m.block;
  String ag = m.age >= 0 ? (m.age < 1 ? String("just now") : String(m.age) + "m ago") : String("");
  String k = m.price + "|" + bl + "|" + ag + (P ? "P" : "L");
  if (need(W_PRICE, k))
  {
    int w = g.hw - 4, h = g.hh - 4;
    if (wBegin(w, h, C_PANEL))
    {
      if (P)
      {
        text(wgt, btcPairLabel().c_str(), 8, 4, 10, C_DIM, C_PANEL);
        text(wgt, m.price.c_str(), w / 2, 18, m.price.length() > 9 ? 26 : 30, C_GOLD, C_PANEL, Align::TopCenter);
        int lw = textW(bl.c_str(), 10);
        int aw = ag.length() ? textW(ag.c_str(), 10) : 0;
        int tw = lw + (ag.length() ? 14 + aw : 0);
        int x0 = (w - tw) / 2;
        text(wgt, bl.c_str(), x0, 54, 10, C_TEXT, C_PANEL);
        if (ag.length())
        {
          wgt.fillCircle(x0 + lw + 7, 60, 1, C_DIM);
          text(wgt, ag.c_str(), x0 + lw + 14, 54, 10, C_DIM, C_PANEL);
        }
      }
      else
      {
        text(wgt, btcPairLabel().c_str(), 6, 3, 10, C_DIM, C_PANEL);
        text(wgt, m.price.c_str(), 6, 18, 28, C_GOLD, C_PANEL);
        text(wgt, bl.c_str(), w - 6, 8, 14, C_TEXT, C_PANEL, Align::TopRight);
        if (ag.length())
          text(wgt, ag.c_str(), w - 6, 30, 10, C_DIM, C_PANEL, Align::TopRight);
      }
      wEnd(g.hx + 2, g.hy + 2);
      done(W_PRICE, k);
    }
  }

  // 2. NETWORK | DIFFICULTY cards
  const char *lab[2] = {"NETWORK", "DIFFICULTY"};
  const String *val[2] = {&m.net, &m.diff};
  uint16_t vc[2] = {C_GOOD, C_GOLD};
  for (int i = 0; i < 2; i++)
  {
    String kk = *val[i] + (P ? "P" : "L");
    if (!need(W_M0 + i, kk))
      continue;
    int w = g.cw - 4, h = g.rh - 4;
    if (!wBegin(w, h, C_PANEL))
      continue;
    text(wgt, lab[i], w / 2, P ? 5 : 3, 10, C_DIM, C_PANEL, Align::TopCenter);
    text(wgt, val[i]->c_str(), w / 2, P ? 21 : 18, P ? 18 : 17, vc[i], C_PANEL, Align::TopCenter);
    wEnd(g.hx + (i ? g.hw - g.cw : 0) + 2, g.ry + 2);
    done(W_M0 + i, kk);
  }

  // 3. FEES (full width): half-hour fee + congestion pill + range hint
  char fk[48];
  snprintf(fk, sizeof(fk), "%d|%d|%d|%d", m.fee, m.feeHour, m.feeFast, P ? 1 : 0);
  if (need(W_M2, fk))
  {
    int w = g.hw - 4, h = g.fh - 4;
    if (wBegin(w, h, C_PANEL))
    {
      String fv = m.fee > 0 ? String(m.fee) : String("--");
      uint16_t pc = C_DIM;
      const char *cls = feeClass(m.fee, pc);
      const CoinProfile &coin = getActiveCoinProfile();
      String range;
      if (m.feeHour > 0 && m.feeFast > 0 && m.feeFast != m.feeHour)
        range = "range " + String(min(m.feeHour, m.feeFast)) + "-" + String(max(m.feeHour, m.feeFast)) + " " + coin.feeUnit;
      text(wgt, "HALF-HOUR FEE", 6, P ? 4 : 3, 10, C_DIM, C_PANEL);
      unsigned vs = P ? 22 : 18;
      int vx = 6, vy = P ? 20 : 17;
      if (!P)
      {
        vx = 130;
        vy = 9;
      }
      text(wgt, fv.c_str(), vx, vy, vs, C_TEXT, C_PANEL);
      int fw = textW(fv.c_str(), vs);
      text(wgt, coin.feeUnit, vx + fw + 4, vy + vs - 11, 10, C_DIM, C_PANEL);
      if (cls)
      {
        int pw = textW(cls, 9) + 12, ph = 14;
        int px = w - pw - 6, py = P ? 4 : 3;
        if (!P)
          py = 10;
        wgt.fillRoundRect(px, py, pw, ph, 7, pc);
        text(wgt, cls, px + pw / 2, py + 3, 9, C_BG, pc, Align::TopCenter);
      }
      if (range.length())
      {
        if (P)
          text(wgt, range.c_str(), w - 6, h - 14, 9, C_DIM, C_PANEL, Align::TopRight);
        else
          text(wgt, range.c_str(), 6, h - 13, 9, C_DIM, C_PANEL);
      }
      wEnd(g.hx + 2, g.fy + 2);
      done(W_M2, fk);
    }
  }

  // 4. NEXT HALVING
  char kb[80];
  snprintf(kb, sizeof(kb), "%d|%ld|%s|%ld|%d", m.pct, m.remaining, m.eta.c_str(), m.height, P ? 1 : 0);
  if (need(W_HALV, kb))
  {
    int w = g.hw - 4, h = g.vh - 4;
    if (wBegin(w, h, C_PANEL))
    {
      const CoinProfile &coin = getActiveCoinProfile();
      char pc[8];
      snprintf(pc, sizeof(pc), "%d%%", m.pct);
      int era = m.height > 0 ? (int)(m.height / coin.halvingInterval) : 4;
      float now = coin.initialSubsidy / (float)(1UL << era);
      String r0 = fmtReward(now), r1 = fmtReward(now / 2) + " " + coin.ticker;
      String rem = m.remaining > 0 ? commas(String(m.remaining)) + " blocks left" : String("-- blocks left");
      const int bx = 6, bw = w - 12;
      if (P)
      {
        text(wgt, "NEXT HALVING", bx, 4, 10, C_DIM, C_PANEL);
        text(wgt, pc, w - 6, 2, 16, C_METER, C_PANEL, Align::TopRight);
        wgt.fillRoundRect(bx, 24, bw, 10, 5, C_TRACK);
        int fw = (bw * m.pct) / 100;
        if (fw > 0) wgt.fillRoundRect(bx, 24, fw, 10, 5, C_METER);
        text(wgt, rem.c_str(), bx, 42, 11, C_TEXT, C_PANEL);
        if (m.eta.length())
          text(wgt, m.eta.c_str(), w - 6, 42, 11, C_DIM, C_PANEL, Align::TopRight);
        wgt.drawFastHLine(bx, 60, bw, C_EDGE);
        text(wgt, "BLOCK REWARD", bx, 68, 9, C_DIM, C_PANEL);
        drawRewardRight(w - 6, 66, 12, r0, r1);
      }
      else
      {
        text(wgt, "NEXT HALVING", bx, 3, 10, C_DIM, C_PANEL);
        text(wgt, pc, bx + textW("NEXT HALVING", 10) + 8, 1, 13, C_METER, C_PANEL);
        drawRewardRight(w - 6, 2, 11, r0, r1);
        wgt.fillRoundRect(bx, 20, bw, 8, 4, C_TRACK);
        int fw = (bw * m.pct) / 100;
        if (fw > 0) wgt.fillRoundRect(bx, 20, fw, 8, 4, C_METER);
        text(wgt, rem.c_str(), bx, 33, 10, C_TEXT, C_PANEL);
        if (m.eta.length())
          text(wgt, m.eta.c_str(), w - 6, 33, 10, C_DIM, C_PANEL, Align::TopRight);
      }
      wEnd(g.hx + 2, g.vy + 2);
      done(W_HALV, kb);
    }
  }
}

// ============================================================ CLOCK screen ==
// Mock layout: CLOCK|HASHING wordmark (no header clock — hero IS the time),
// CURRENT HASHRATE (KH/s only), LAST 60 SECONDS rolling hashrate bars,
// TEMP | UPTIME | ONLINE strip. No BTC / block / workers / pool / best / shares.

struct ClockV
{
  HdrV hdr;
  String hh, mm;
  bool colon;
  String date, hash;
  String temp, uptime, link; // strip cells
  uint16_t linkCol;
  uint16_t tempCol;
  bool flash;
};

static void chromeClock(TFT_eSPI &d)
{
  const bool P = isPortrait();
  if (P)
  {
    panel(d, 6, 30, 228, 78, 8, C_PANEL, C_EDGE, C_BG);
    panel(d, 6, 114, 228, 52, 8, C_PANEL, C_EDGE, C_BG);
    panel(d, 6, 172, 228, 88, 8, C_PANEL, C_EDGE, C_BG);
    panel(d, 6, 266, 228, 48, 8, C_PANEL, C_EDGE, C_BG);
    for (int i = 1; i < 3; i++)
      d.drawFastVLine(6 + (228 * i) / 3, 272, 36, C_EDGE);
  }
  else
  {
    panel(d, 8, 30, 152, 70, 8, C_PANEL, C_EDGE, C_BG);
    panel(d, 168, 30, 144, 70, 8, C_PANEL, C_EDGE, C_BG);
    panel(d, 8, 106, 304, 84, 8, C_PANEL, C_EDGE, C_BG);
    panel(d, 8, STRIP_Y, 304, STRIP_H, 8, C_PANEL, C_EDGE, C_BG);
    for (int i = 1; i < 3; i++)
      d.drawFastVLine(8 + (304 * i) / 3, STRIP_Y + 8, STRIP_H - 16, C_EDGE);
  }
}

// Rolling hashrate spark — display only; samples come from noteHashrate().
static void drawChart()
{
  char k[40];
  snprintf(k, sizeof(k), "%lu|%u|%d|%d", (unsigned long)histEpoch, histCount, accentIdx, isPortrait() ? 1 : 0);
  if (!need(W_CHART, k))
    return;
  const bool P = isPortrait();
  const int W = P ? 216 : 288;
  const int H = P ? 52 : 50;
  if (!wBegin(W, H, C_PANEL))
    return;
  text(wgt, "HASHRATE HISTORY", 0, 0, 9, C_DIM, C_PANEL);
  text(wgt, "LAST 60 SECONDS", W - 1, 0, 9, C_DIM, C_PANEL, Align::TopRight);
  const int baseY = H - 1;
  wgt.drawFastHLine(0, baseY, W, C_TRACK);
  float mx = 1;
  for (int i = 0; i < histCount; i++)
    if (hist[i] > mx)
      mx = hist[i];
  mx *= 1.08f;
  const int numSlots = HIST_N;
  const int barH = H - 14;
  const float step = (float)W / (float)numSlots;
  const int bw = step >= 3.0f ? (int)step - 1 : 1;
  for (int i = 0; i < numSlots; i++)
  {
    int slot = i - (numSlots - histCount); // right-align newest
    int x = (int)(i * step);
    if (slot < 0 || slot >= HIST_N)
    {
      wgt.fillRect(x, baseY - 2, bw, 2, C_TRACK);
      continue;
    }
    int bh = (int)(barH * hist[slot] / mx);
    if (bh < 2)
      bh = 2;
    wgt.fillRect(x, baseY - bh, bw, bh, C_METER);
  }
  wEnd(P ? 12 : 16, P ? 186 : 118);
  done(W_CHART, k);
}

static void paintClock(const ClockV &c)
{
  drawHeader(c.hdr);
  const bool P = isPortrait();

  // Hero: large centered HH:MM + date (portrait); landscape keeps date beside
  {
    String k = c.hh + ":" + c.mm + "|" + String(c.colon) + "|" + c.date + (P ? "P" : "L");
    if (need(W_HH, k))
    {
      int pw = P ? 220 : 144, ph = P ? 70 : 62;
      if (wBegin(pw, ph, C_PANEL))
      {
        char tim[8];
        snprintf(tim, sizeof(tim), "%s%s%s", c.hh.c_str(), c.colon ? ":" : " ", c.mm.c_str());
        if (P)
        {
          text(wgt, tim, pw / 2, 4, 42, C_TEXT, C_PANEL, Align::TopCenter);
          text(wgt, c.date.c_str(), pw / 2, 52, 12, C_DIM, C_PANEL, Align::TopCenter);
        }
        else
        {
          text(wgt, tim, 6, 6, 36, C_TEXT, C_PANEL);
          text(wgt, c.date.c_str(), 6, 48, 10, C_DIM, C_PANEL);
        }
        wEnd(P ? 10 : 12, P ? 34 : 34);
        done(W_HH, k);
      }
    }
  }

  // CURRENT HASHRATE — big KH/s only (brief share-accepted flash OK)
  {
    String k = c.hash + "|" + String(c.flash) + (P ? "P" : "L");
    if (need(W_C0, k))
    {
      int pw = P ? 220 : 136, ph = P ? 44 : 62;
      if (wBegin(pw, ph, C_PANEL))
      {
        text(wgt, "CURRENT HASHRATE", P ? 8 : 6, P ? 4 : 6, 10, C_DIM, C_PANEL);
        uint16_t vc = c.flash ? C_GOOD : C_TEXT;
        unsigned sz = P ? 26 : 28;
        text(wgt, c.hash.c_str(), P ? 8 : 6, P ? 18 : 24, sz, vc, C_PANEL);
        int nw = textW(c.hash.c_str(), sz);
        text(wgt, "KH/s", P ? (8 + nw + 6) : (6 + nw + 4), P ? 28 : 36, 12, C_METER, C_PANEL);
        wEnd(P ? 10 : 172, P ? 118 : 34);
        done(W_C0, k);
      }
    }
  }

  drawChart();

  // Optional live-sample caption under the chart (portrait: inside hist panel)
  {
    String k = String("live|") + (P ? "P" : "L");
    if (need(W_C1, k))
    {
      int pw = P ? 216 : 288, ph = 12;
      if (wBegin(pw, ph, C_PANEL))
      {
        text(wgt, "LIVE SAMPLE · MINER HASHING CONTINUOUSLY", 0, 0, 8, C_DIM, C_PANEL);
        wEnd(P ? 12 : 16, P ? 244 : 176);
        done(W_C1, k);
      }
    }
  }

  // Bottom strip: TEMP | UPTIME | ONLINE (value warns if disconnect/hot)
  {
    const char *labs[3] = {"TEMP", "UPTIME", "ONLINE"};
    String vals[3] = {c.temp, c.uptime, c.link};
    uint16_t cols[3] = {c.tempCol, C_TEXT, c.linkCol};
    int sy = P ? 269 : (STRIP_Y + 3);
    int cw = P ? 72 : 96;
    int ch = P ? 42 : 32;
    int xstep = P ? 76 : 101;
    int x0 = P ? 10 : 12;
    for (int i = 0; i < 3; i++)
    {
      String k = String(labs[i]) + "|" + vals[i] + "|" + accentIdx + (P ? "P" : "L");
      if (!need(W_P0 + i, k))
        continue;
      if (!wBegin(cw, ch, C_PANEL))
        continue;
      text(wgt, labs[i], P ? 2 : 4, P ? 2 : 2, P ? 10 : 11, C_DIM, C_PANEL);
      text(wgt, vals[i].c_str(), P ? 2 : 4, P ? 17 : 15, P ? 14 : 15, cols[i], C_PANEL);
      wEnd(x0 + xstep * i, sy);
      done(W_P0 + i, k);
    }
  }
}

// =================================== PROTOCOL · live difficulty scatter ==
// Y = log2(achieved difficulty) — higher = rarer/better. X = recent samples
// (right = newest). Display-only: miner pushes g_hashSamples at ~1/16384;
// UI redraw is 5–10 Hz and never touches g_hwShaMutex / the SHA engine.

struct ArchitectureV
{
  HdrV hdr;
  String netDiff;   // e.g. "133T"
  String hashRate;  // KH/s text
  String bestDiff;  // session best (suffix string)
  double netDiffRaw;
  double bestDiffRaw;
  StripV strip;     // unused (kept so call sites compile)
};

static uint16_t protoLog2Q8FromDiff(double d)
{
  return hashLog2Q8FromDifficulty(d);
}

static void drawProtoX(TFT_eSPI &d, int cx, int cy, uint16_t col)
{
  d.drawLine(cx - 2, cy - 2, cx + 2, cy + 2, col);
  d.drawLine(cx - 2, cy + 2, cx + 2, cy - 2, col);
}

static void chromeArchitecture(TFT_eSPI &d)
{
  // Static chrome only: plot panel frame + footer band. Live ink is paintArchitecture.
  const bool P = isPortrait();
  const int scrW = P ? 240 : 320;
  const int scrH = P ? 320 : 240;
  const int plotX = P ? 6 : 8;
  const int plotY = 28;
  const int plotW = scrW - (P ? 12 : 16);
  const int footH = P ? 52 : 44;
  const int footY = scrH - footH - 4;

  panel(d, plotX, plotY, plotW, footY - plotY - 4, 6, C_PANEL, C_EDGE, C_BG);
  panel(d, plotX, footY, plotW, footH, 6, C_PANEL, C_EDGE, C_BG);
}

// Sample → difficulty: diff = truediffone / hash ≈ 2^224 / hash, so
// log2(diff) = 224 - log2(hash). Samples store log2(hash) in Q8; plot
// log2(diff) in signed Q8 (higher = rarer). Pure display math — no mining.
static inline int32_t protoDiffQ8FromHashQ8(uint16_t hashLog2q8)
{
  return (int32_t)(224 << 8) - (int32_t)hashLog2q8;
}

// Y window in log2(diff): bottom ≈ 2^-32 (typical random hash), top =
// network target + 4 bits headroom. Linear in log2 → compressed rarity.
static int protoYDiff(int32_t dq8, int32_t topQ8, int32_t botQ8, int plotH)
{
  int32_t span = topQ8 - botQ8;
  if (span <= 0) return plotH / 2;
  int32_t v = topQ8 - dq8;
  if (v < 0) v = 0;
  if (v > span) v = span;
  return 2 + (int)((v * (plotH - 5)) / span);
}

static void paintProtocolPlot(const ArchitectureV &av)
{
  const bool P = isPortrait();
  const int scrW = P ? 240 : 320;
  const int scrH = P ? 320 : 240;
  const int plotX = P ? 6 : 8;
  const int plotY = 28;
  const int plotW = scrW - (P ? 12 : 16);
  const int footH = P ? 52 : 44;
  const int footY = scrH - footH - 4;
  const int plotH = footY - plotY - 4;

  // Snapshot ring (volatile reads; no mutex).
  uint8_t count = g_hashSampleCount;
  uint8_t head = g_hashSampleHead;
  uint16_t latest = g_hashSampleLatest;
  uint16_t snap[HASH_SAMPLE_N];
  if (count > HASH_SAMPLE_N)
    count = HASH_SAMPLE_N;
  for (uint8_t i = 0; i < count; i++)
  {
    uint8_t idx = (uint8_t)((head + HASH_SAMPLE_N - count + i) % HASH_SAMPLE_N);
    snap[i] = g_hashSamples[idx]; // oldest .. newest
  }

  double net = av.netDiffRaw > 0 ? av.netDiffRaw : 1e14;
  // hashLog2Q8FromDifficulty returns log2(truediffone/diff); invert to log2(diff).
  int32_t tgtQ8 = protoDiffQ8FromHashQ8(protoLog2Q8FromDiff(net));
  int32_t bestQ8 = av.bestDiffRaw > 0
                       ? protoDiffQ8FromHashQ8(protoLog2Q8FromDiff(av.bestDiffRaw))
                       : INT32_MIN;
  // Fixed meaning: top edge = live network difficulty (block threshold),
  // bottom baseline = diff 1 ("<1": nearly every hash lands here).
  const int32_t topQ8 = tgtQ8;
  const int32_t botQ8 = 0;
  // Highest plotted sample; BEST must never sit below a plotted point.
  int bestI = -1;
  int32_t maxSampQ8 = INT32_MIN;
  for (int i = 0; i < count; i++)
  {
    int32_t q = protoDiffQ8FromHashQ8(snap[i]);
    if (q > maxSampQ8) { maxSampQ8 = q; bestI = i; }
  }
  if (maxSampQ8 > bestQ8)
    bestQ8 = maxSampQ8; // race guard (best_diff also tracks samples)
  const bool bestIsSample = (bestI >= 0 && maxSampQ8 >= bestQ8 - 8);

  char kbuf[96];
  snprintf(kbuf, sizeof(kbuf), "%u|%u|%ld|%ld|%s|%d",
           (unsigned)count, (unsigned)latest, (long)tgtQ8, (long)bestQ8,
           av.bestDiff.c_str(), P ? 1 : 0);
  if (need(W_PROTO_PLOT, kbuf) && wBegin(plotW, plotH, C_PANEL))
  {
    const int G = textW("1G", 8) + 8;
    const int sx0 = G + 10;
    const int sxSpan = plotW - sx0 - 14;
    const int yTop = 2, yBot = plotH - 16; // leave room for x caption
    auto Y = [&](int32_t q) -> int {
      int32_t span = topQ8 - botQ8;
      if (span <= 0) return (yTop + yBot) / 2;
      int32_t v = topQ8 - q;
      if (v < 0) v = 0;
      if (v > span) v = span;
      return yTop + (int)((v * (yBot - yTop)) / span);
    };
    // Mid ticks: 1K, 1M, 1G (log2 ≈ 10/20/30 bits) when below the top.
    static const char *tk[] = {"1K", "1M", "1G"};
    for (int t = 0; t < 3; t++)
    {
      int32_t q = (int32_t)((t + 1) * 10) << 8;
      if (q >= topQ8 - (4 << 8)) break;
      int ty = Y(q);
      for (int x = G + 2; x < plotW - 4; x += 4)
        wgt.drawPixel(x, ty, C_EDGE);
      text(wgt, tk[t], G - 4, ty - 4, 8, C_DIM, C_PANEL, Align::TopRight);
    }
    wgt.drawFastVLine(G, yTop, yBot - yTop + 1, C_EDGE);
    // Baseline "<1".
    wgt.drawFastHLine(G, yBot, plotW - G - 4, C_EDGE);
    text(wgt, "<1", G - 4, yBot - 4, 8, C_DIM, C_PANEL, Align::TopRight);
    // Top: orange BLOCK THRESHOLD at live network difficulty.
    wgt.drawFastHLine(G, yTop, plotW - G - 4, C_WARN);
    wgt.drawFastHLine(G, yTop + 1, plotW - G - 4, C_WARN);
    text(wgt, av.netDiff.c_str(), G - 4, yTop, 8, C_WARN, C_PANEL, Align::TopRight);
    char tlab[48];
    snprintf(tlab, sizeof(tlab), "BLOCK THRESHOLD · %s diff", av.netDiff.c_str());
    text(wgt, tlab, plotW - 6, yTop + 4, 8, C_WARN, C_PANEL, Align::TopRight);
    text(wgt, "hash < target -> valid block", plotW - 6, yTop + 15, 7, C_DIM, C_PANEL, Align::TopRight);
    text(wgt, "RARER ^", G + 4, yTop + 4, 8, C_DIM, C_PANEL);

    if (count < 1)
    {
      text(wgt, "waiting for samples...", G + (plotW - G) / 2, plotH / 2 - 6, 10, C_DIM, C_PANEL, Align::TopCenter);
    }
    else
    {
      for (int i = 0; i < count; i++)
      {
        if (bestIsSample && i == bestI) continue;
        int x = sx0 + (i * sxSpan) / (count > 1 ? (count - 1) : 1);
        drawProtoX(wgt, x, Y(protoDiffQ8FromHashQ8(snap[i])), C_BAD);
      }
      // Newest: thin outline only.
      int cx = sx0 + sxSpan;
      int cy = Y(protoDiffQ8FromHashQ8(snap[count - 1]));
      wgt.drawCircle(cx, cy, 5, C_DIM);
    }
    // BEST: the highest plotted point, labeled in place. If it came from a
    // share (not in the 48-sample ring) it is pinned at the left edge.
    if (bestQ8 != INT32_MIN)
    {
      int bx = bestIsSample ? sx0 + (bestI * sxSpan) / (count > 1 ? (count - 1) : 1) : sx0;
      int by = Y(bestQ8);
      wgt.fillCircle(bx, by, 4, C_METER);
      wgt.drawCircle(bx, by, 5, C_WARN);
      char blab[32];
      snprintf(blab, sizeof(blab), "* BEST %s", av.bestDiff.c_str());
      int lw = textW(blab, 9);
      bool right = bx + 8 + lw < plotW - 4;
      int ly = (by > yTop + 30) ? by - 12 : by + 7;
      text(wgt, blab, right ? bx + 8 : bx - 8, ly, 9, C_METER, C_PANEL,
           right ? Align::TopLeft : Align::TopRight);
    }
    text(wgt, "recent samples · newest ->", G + (plotW - G) / 2, plotH - 11, 8, C_DIM, C_PANEL, Align::TopCenter);

    wEnd(plotX, plotY);
    done(W_PROTO_PLOT, kbuf);
  }

  // Footer: lightning KH/s + attempts/sec + live-sample note
  char fkey[64];
  snprintf(fkey, sizeof(fkey), "%s|%d", av.hashRate.c_str(), P ? 1 : 0);
  if (!need(W_PROTO_FOOT, fkey))
    return;
  if (!wBegin(plotW, footH, C_PANEL))
    return;
  float kh = av.hashRate.toFloat();
  char rateBuf[24];
  if (kh > 0)
    snprintf(rateBuf, sizeof(rateBuf), "%.0f KH/s", kh);
  else
    snprintf(rateBuf, sizeof(rateBuf), "-- KH/s");
  int bx = 10, by = P ? 6 : 8;
  wgt.fillTriangle(bx, by + 14, bx + 8, by, bx + 5, by + 8, C_METER);
  wgt.fillTriangle(bx + 4, by + 6, bx + 12, by + 6, bx + 6, by + 20, C_METER);
  text(wgt, rateBuf, 28, P ? 4 : 6, 20, C_METER, C_PANEL);
  char att[40];
  if (kh > 0)
    snprintf(att, sizeof(att), "~ %.0f attempts/sec", kh * 1000.0f);
  else
    snprintf(att, sizeof(att), "~ -- attempts/sec");
  if (P)
    text(wgt, att, 28, 27, 10, C_TEXT, C_PANEL);
  else
    text(wgt, att, plotW - 8, 10, 11, C_TEXT, C_PANEL, Align::TopRight);
  text(wgt, "LIVE SAMPLE · MINING UNINTERRUPTED", 28, P ? 40 : 28, 8, C_DIM, C_PANEL);
  wEnd(plotX, footY);
  done(W_PROTO_FOOT, fkey);
}

static void paintArchitecture(const ArchitectureV &av)
{
  // Tiny top title: PROTOCOL|HASHING + time (reuse header chrome)
  HdrV h = av.hdr;
  h.screen = SCR_ARCH;
  drawHeader(h);
  paintProtocolPlot(av);
}

// ------------------------------------------------------ boot / setup pages --

static void paintLoading(TFT_eSPI &d)
{
  clearScreen(d);
  const int cx = isPortrait() ? 120 : 160;
  const int cy = isPortrait() ? 110 : 92;
  const int r = 50, ri = 41, n = 24;
  for (int i = 0; i < n; i++)
  {
    int a0 = i * 15;
    d.drawArc(cx, cy, r, ri, a0, a0 + 11, C_METER, C_BG, true);
  }
  text(d, "#", cx, cy - 27, 54, C_TEXT, C_BG, Align::TopCenter);
  int wn = textW("NERDMINER", 26);
  int x0 = cx - wn / 2;
  int textY = isPortrait() ? 180 : 152;
  text(d, "NERDMINER", x0, textY, 26, C_TEXT, C_BG);
  text(d, CURRENT_VERSION, cx, textY + 40, 12, C_DIM, C_BG, Align::TopCenter);
  text(d, "INITIALIZING", cx, textY + 58, 9, C_DIM, C_BG, Align::TopCenter);
}

static void paintSetup(TFT_eSPI &d)
{
  clearScreen(d);
  text(d, "SETUP MODE", 16, 12, 22, C_TEXT, C_BG);
  text(d, "Connect to miner access point to configure", 16, 40, 11, C_DIM, C_BG);

  String ap = readCustomAPName();
  if (ap.length() == 0)
    ap = DEFAULT_SSID;
  const char *lab[3] = {"WIFI NETWORK", "PASSWORD", "SETTINGS URL"};
  const char *val[3] = {ap.c_str(), DEFAULT_WIFIPW, "http://192.168.4.1"};
  int pw = isPortrait() ? 224 : 304;
  for (int i = 0; i < 3; i++)
  {
    int y = 66 + i * 50;
    panel(d, 8, y, pw, 44, 0, C_PANEL, C_EDGE, C_BG);
    char n[4];
    snprintf(n, sizeof(n), "0%d", i + 1);
    text(d, n, 16, y + 13, 14, C_METER, C_PANEL);
    text(d, lab[i], 46, y + 6, 8, C_DIM, C_PANEL);
    text(d, val[i], 46, y + 18, isPortrait() ? 13 : 15, C_TEXT, C_PANEL);
  }
  text(d, "Settings will persist across reboots", isPortrait() ? 120 : 160, isPortrait() ? 230 : 222, 10, C_DIM, C_BG, Align::TopCenter);
}

// ---------------------------------------------------------------- chrome --

static void paintChrome(TFT_eSPI &d, int scr)
{
  clearScreen(d);
  d.drawFastHLine(0, HDR_H, isPortrait() ? 240 : 320, C_EDGE);
  switch (scr)
  {
  case SCR_MINE:
    chromeMine(d);
    break;
  case SCR_MARKET:
    chromeMarket(d);
    break;
  case SCR_CLOCK:
    chromeClock(d);
    break;
  case SCR_ARCH:
    chromeArchitecture(d);
    break;
  default:
    chromeMine(d);
    break;
  }
}

static void ensureChrome(int scr)
{
  if (uiScreen == scr)
    return;
  uiScreen = scr;
  resetWidgets();
  paintChrome(out(), scr);
}

static void repaintCached(int scr);

static void setAccent(int idx)
{
  (void)idx;
  accentIdx = 0;
  cAcc = C_METER;
  cAlt = C_METER;
}

// Last view painted per screen, so a page change can repaint instantly
// instead of waiting for the next (possibly network-blocked) data tick.
static MineV lastMine;
static MarketV lastMarket;
static ClockV lastClock;
static ArchitectureV lastArch;
static bool haveMine = false, haveMarket = false, haveClock = false;
static bool haveArch = false;

static void repaintCached(int scr)
{
  if (scr == SCR_MINE && haveMine)
    paintMine(lastMine);
  else if (scr == SCR_MARKET && haveMarket)
    paintMarket(lastMarket);
  else if (scr == SCR_CLOCK && haveClock)
    paintClock(lastClock);
  else if (scr == SCR_ARCH && haveArch)
    paintArchitecture(lastArch);
}

void esp32_2432S028R_SetTheme(int idx)
{
  if (idx < 0 || idx >= THEME_COUNT)
    idx = 0;
  currentThemeIdx = idx;
  Settings.displayTheme = idx;
  const ThemePalette &p = THEMES[idx];
  C_BG = p.bg;
  C_PANEL = p.panel;
  C_EDGE = p.edge;
  C_TRACK = p.track;
  C_TEXT = p.text;
  C_DIM = p.dim;
  C_GOOD = p.good;
  C_WARN = p.warn;
  C_BAD = p.bad;
  C_GOLD = p.gold;
  C_METER = p.meter;
  cAcc = p.meter;
  cAlt = p.meter;

  if (uiLive)
  {
    int s = currentDisplayDriver->current_cyclic_screen;
    uiScreen = -1;
    resetWidgets();
    clearScreen(out());
    ensureChrome(s);
    repaintCached(s);
  }
}

int esp32_2432S028R_GetTheme()
{
  return currentThemeIdx;
}

// ------------------------------------------------------- saved UI choices --
// Last-viewed page lives in its own NVS namespace (the
// pool/wallet/brightness/invert/flip settings are in the main config file).

#define UI_PREFS_NS "cyd_ui"

static int savedPage = 0;
static int restorePage = 0; // applied on the first data tick (the monitor resets the page to 0 at start)

static void loadUiPrefs()
{
  Preferences p;
  if (!p.begin(UI_PREFS_NS, true))
    return; // namespace not created yet: defaults
  int page = p.getUChar("page", 0);
  p.end();
  accentIdx = 0;
  cAcc = C_METER;
  cAlt = C_METER;
  savedPage = restorePage = page;
}

static void saveUiPrefs()
{
  int page = currentDisplayDriver->current_cyclic_screen;
  if (page == savedPage)
    return;
  Preferences p;
  if (p.begin(UI_PREFS_NS, false))
  {
    p.putUChar("page", page);
    p.end();
  }
  savedPage = page;
}

// -------------------------------------------------------------- live data --

static void refreshPool()
{
  if (Settings.PoolAddress != "tn.vkbit.com")
  {
    pData = getPoolData(); // throttled internally (UPDATE_POOL_min)
  }
  else
  {
    pData.bestDifficulty = "TESTNET";
    pData.workersHash = "TESTNET";
    pData.workersCount = 1;
    mPoolUpdate = millis();
  }
}

static HdrV liveHeader(const String &time)
{
  HdrV h;
  h.time = time;
  h.ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  h.bars = wifiBars();
  h.status = uiStatus();
  h.screen = currentDisplayDriver ? currentDisplayDriver->current_cyclic_screen : 0;
  return h;
}

static void logShare(const String &shares, const String &khashes, const String &rate)
{
  // Parsed by the bench scripts (see project notes) -- keep the format.
  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                shares.c_str(), khashes.c_str(), rate.c_str());
}

void esp32_2432S028R_MinerScreen(unsigned long mElapsed)
{
  if (restorePage > 0 && restorePage < currentDisplayDriver->num_cyclic_screens)
  {
    // First tick after boot: go straight to the page that was showing before.
    int p = restorePage;
    restorePage = 0;
    currentDisplayDriver->current_cyclic_screen = p;
    currentDisplayDriver->cyclic_screens[p](mElapsed);
    return;
  }
  restorePage = 0;
  mining_data data = getMiningData(mElapsed);
  refreshPool();
  uiLive = true;
  ensureChrome(SCR_MINE);

  float kh = data.currentHashRate.toFloat();
  noteHashrate(kh);
  gaugeTarget = constrain(kh / G_MAX_KH, 0.0f, 1.0f);

  uint32_t sh = data.completedShares.toInt();
  if (sh > lastShares && lastShares != 0)
    shareFlashUntil = millis() + 1500;
  lastShares = sh;

  MineV m;
  m.hdr = liveHeader(data.currentTime);
  m.hash = data.currentHashRate;
  m.shares = commas(data.completedShares);
  m.best = data.bestDiff;
  m.tmpl = commas(data.templates);
  m.blocks = data.valids;
  m.total = data.totalMHashes;
  m.kh = kh;
  m.flash = millis() < shareFlashUntil;
  m.strip.v[0] = String(pData.workersCount);
  m.strip.l[0] = pData.workersCount == 1 ? "worker" : "workers";
  m.strip.v[1] = fmtUptimeShort(data.timeMining);
  m.strip.l[1] = "uptime";
  m.strip.v[2] = data.valids;
  m.strip.l[2] = data.valids.toInt() == 1 ? "block" : "blocks";
  lastMine = m;
  haveMine = true;
  paintMine(m);

  logShare(data.completedShares, data.totalKHashes, data.currentHashRate);
}

void esp32_2432S028R_MarketScreen(unsigned long mElapsed)
{
  coin_data data = getCoinData(mElapsed);
  refreshPool();
  uiLive = true;
  ensureChrome(SCR_MARKET);
  noteHashrate(data.currentHashRate.toFloat());

  MarketV m;
  m.hdr = liveHeader(data.currentTime);
  m.price = fmtPrice(data.btcPrice);
  m.height = data.blockHeight.toInt();
  m.block = commas(data.blockHeight);
  m.age = data.blockAgeMin;
  m.fee = data.halfHourFee.toInt();
  m.feeHour = data.feeHour;
  m.feeFast = data.feeFast;
  m.diff = data.netwrokDifficulty.length() ? data.netwrokDifficulty : String("--");
  m.net = data.globalHashRate.length() ? data.globalHashRate + " EH/s" : String("--");
  m.pct = (int)data.progressPercent;
  m.remaining = data.remainingBlocks.toInt();
  m.eta = data.halvingEta;
  lastMarket = m;
  haveMarket = true;
  paintMarket(m);

  logShare(data.completedShares, data.totalKHashes, data.currentHashRate);
}

void esp32_2432S028R_ClockScreen(unsigned long mElapsed)
{
  clock_data data = getClockData(mElapsed);
  uiLive = true;
  ensureChrome(SCR_CLOCK);
  noteHashrate(data.currentHashRate.toFloat());

  uint32_t sh = data.completedShares.toInt();
  if (sh > lastShares && lastShares != 0)
    shareFlashUntil = millis() + 1200;
  lastShares = sh;

  ClockV c;
  c.hdr = liveHeader(data.currentTime);
  int colon = data.currentTime.indexOf(':');
  c.hh = colon > 0 ? data.currentTime.substring(0, colon) : String("--");
  String rest = colon > 0 ? data.currentTime.substring(colon + 1) : String("--");
  int colon2 = rest.indexOf(':');
  c.mm = colon2 > 0 ? rest.substring(0, colon2) : rest;
  c.colon = (millis() / 1000) & 1;
  c.date = fmtDate(data.currentDate);
  c.hash = data.currentHashRate;
  c.flash = millis() < shareFlashUntil;
  c.temp = data.temp.length() ? (data.temp + "°C") : String("--");
  c.uptime = fmtUptimeShort(data.timeMining);
  int tC = data.temp.toInt();
  bool hot = (data.temp.length() && tC >= 70);
  c.tempCol = hot ? C_WARN : C_TEXT;
  if (WiFi.status() != WL_CONNECTED)
  {
    c.link = "OFFLINE";
    c.linkCol = C_BAD;
  }
  else if (hot)
  {
    c.link = "HOT";
    c.linkCol = C_WARN;
  }
  else if (uiStatus() == NM_hashing)
  {
    c.link = "ONLINE";
    c.linkCol = C_GOOD;
  }
  else
  {
    c.link = "LINKING";
    c.linkCol = C_WARN;
  }
  lastClock = c;
  haveClock = true;
  paintClock(c);

  logShare(data.completedShares, data.totalKHashes, data.currentHashRate);
}

void esp32_2432S028R_ArchitectureScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  uiLive = true;
  ensureChrome(SCR_ARCH);
  float kh = data.currentHashRate.toFloat();
  noteHashrate(kh);

  ArchitectureV av;
  av.hdr = liveHeader(data.currentTime);
  av.netDiffRaw = g_networkDifficulty > 0.0 ? g_networkDifficulty : 1e14;
  {
    char b[32];
    double t = av.netDiffRaw / 1e12;
    if (t >= 100.0)
      snprintf(b, sizeof(b), "%.0fT", t);
    else
      snprintf(b, sizeof(b), "%.1fT", t);
    av.netDiff = String(b);
  }
  av.hashRate = data.currentHashRate;
  av.bestDiff = data.bestDiff;
  // best_diff is the real session/persisted best (updated on improve).
  av.bestDiffRaw = best_diff;
  lastArch = av;
  haveArch = true;
  paintArchitecture(av);

  logShare(data.completedShares, data.totalKHashes, data.currentHashRate);
}

// --------------------------------------------------------- driver plumbing --

static void touchTask(void *);
void esp32_2432S028R_AlternateRotation(void);

static volatile bool g_backlightReady = false;
static volatile int g_currentDuty = 255;

void esp32_2432S028R_Init(void)
{
  tft.init();
  if (nvMem.loadConfig(&Settings))
  {
    invertColors = Settings.invertColors;
  }
#if defined(TFT_INVERSION_ON)
  invertColors = !invertColors;
#endif
  tft.invertDisplay(invertColors);
  int rot = Settings.screenRotation;
  if (rot < 0 || rot > 3)
    rot = Settings.flipDisplay ? 3 : 1;
  tft.setRotation(rot); // 0=Portrait (USB bottom), 1=Landscape (USB right), 2=Portrait Inv (USB top), 3=Landscape Inv (USB left)
  tft.setSwapBytes(true);
  if (invertColors)
  {
    tft.writecommand(ILI9341_GAMMASET);
    tft.writedata(2);
    delay(120);
    tft.writecommand(ILI9341_GAMMASET); // Gamma curve selected
    tft.writedata(1);
  }
  hSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, ETOUCH_CS);
  touch.init();

  // Backlight: LEDC channel 0, 5kHz, 8 bit (0-255 duty)
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  int initBright = Settings.Brightness > 0 ? Settings.Brightness : 255;
  g_currentDuty = initBright;
  ledcWrite(0, initBright);
  g_backlightReady = true;

  TFT_eTouchBase::Calibation calibation = {233, 3785, 3731, 120, 2};
  touch.setCalibration(calibation);
  xTaskCreatePinnedToCore(touchTask, "cydTouch", 3072, NULL, 1, NULL, 0);

  loadUiPrefs();
  esp32_2432S028R_SetTheme(Settings.displayTheme);

  if (render.loadFont(NotoSans_Bold, sizeof(NotoSans_Bold)))
  {
    Serial.println("Initialise error");
    return;
  }

#ifdef HAS_RGB_LED
  rgbLedInit();
#else
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN_B, OUTPUT);
  pinMode(LED_PIN_G, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_PIN_B, HIGH);
  digitalWrite(LED_PIN_G, HIGH);
#endif
  pData.bestDifficulty = "0";
  pData.workersHash = "0";
  pData.workersCount = 0;

  resetWidgets();
  memset(segLvl, 0xFF, sizeof(segLvl));

#ifdef CYD_UI_SCREENSHOT
  extern void cydDumpScreens(void);
  cydDumpScreens();
#endif
}

void esp32_2432S028R_AlternateScreenState(void)
{
  Serial.println("Switching display state");
  if (!g_backlightReady)
    return;
  // Switching the duty cycle for the ledc channel, where the TFT_BL pin is attached.
  if (g_currentDuty > 0)
  {
    g_currentDuty = 0;
    ledcWrite(0, 0);
  }
  else
  {
    g_currentDuty = Settings.Brightness > 0 ? Settings.Brightness : 255;
    ledcWrite(0, g_currentDuty);
  }
#ifdef HAS_RGB_LED
  rgbLedTick();
#endif
}

// Save the orientation into the main config file, same field the portal and
// LAN page use. Load a fresh copy first so only this one value changes
// (the in-memory Settings can hold portal defaults that must not be saved).
static void persistRotation(int rot)
{
  Settings.screenRotation = rot;
  Settings.flipDisplay = (rot == 3);
  TSettings stored;
  if (nvMem.loadConfig(&stored))
  {
    stored.screenRotation = rot;
    stored.flipDisplay = (rot == 3);
    nvMem.saveConfig(&stored);
  }
}

void esp32_2432S028R_AlternateRotation(void)
{
  int nextRot = (tft.getRotation() + 1) % 4;
  tft.setRotation(nextRot);
  persistRotation(nextRot);
  if (uiLive)
  {
    int s = currentDisplayDriver->current_cyclic_screen;
    uiScreen = -1;
    resetWidgets();
    clearScreen(out());
    ensureChrome(s);
    repaintCached(s);
  }
}

void esp32_2432S028R_LoadingScreen(void)
{
  uiLive = false;
  uiScreen = -1;
  paintLoading(out());
}

void esp32_2432S028R_SetupScreen(void)
{
  uiLive = false;
  uiScreen = -1;
  paintSetup(out());
}

void esp32_2432S028R_AnimateCurrentScreen(unsigned long frame)
{
  if (!uiLive || uiScreen < 0)
    return;
  drawPulse(frame);
  if (uiScreen == SCR_MINE && fabsf(gaugeTarget - gaugeShown) > 0.002f)
  {
    gaugeShown += (gaugeTarget - gaugeShown) * 0.3f; // ease toward the measured rate
    gaugeDraw(out(), gaugeShown);
  }
  // Protocol scatter: 5–10 Hz partial redraw from the sample ring.
  if (uiScreen == SCR_ARCH && haveArch && (frame & 1) == 0)
  {
    // Refresh hashrate text periodically from lastArch; samples read live.
    paintProtocolPlot(lastArch);
  }
}

unsigned long previousMillis = 0;

// Touch is sampled by its own small task at ~50Hz (the monitor loop is too
// slow and can block on network calls, which made taps get missed). It only
// records the gesture; all drawing stays on the monitor task.
enum TouchAction
{
  TA_NONE = 0,
  TA_NEXT,
  TA_PREV,
  TA_BACKLIGHT,
  TA_ACCENT
};
static volatile int pendingTouch = TA_NONE;

// Ambient light sensor (LDR on GPIO 34, ADC1) auto-dimming
#define PIN_LDR 34
#define LDR_MIN_DUTY 15      // backlight floor in a dark room
#define LDR_RAW_DARK 3800    // ADC reading treated as "dark"
#define LDR_RAW_BRIGHT 500   // ADC reading treated as "bright"
static float ldrEma = -1.0f;
static unsigned long lastLdrCheck = 0;

static void touchTask(void *)
{
  bool wasDown = false;
  int downCount = 0;
  pinMode(PIN_LDR, INPUT); // CYD LDR photoresistor
  for (;;)
  {
    int16_t t_x, t_y;
    bool down = touch.getXY(t_x, t_y);
    if (down)
    {
      if (++downCount == 2 && !wasDown) // two consecutive samples: real press, act once per touch
      {
        wasDown = true;
        if (t_y < HDR_H + 4 && t_x > (isPortrait() ? 175 : 235))
          pendingTouch = TA_BACKLIGHT; // top-right: backlight on/off
        else
          pendingTouch = t_x > (isPortrait() ? 120 : 160) ? TA_NEXT : TA_PREV;
      }
    }
    else
    {
      downCount = 0;
      wasDown = false;
    }

    // Auto-dimming: sample the LDR every 100 ms (cheap ADC1 read, no Wi-Fi conflict)
    unsigned long now = millis();
    if (g_backlightReady && (now - lastLdrCheck >= 100))
    {
      lastLdrCheck = now;
      if (g_currentDuty > 0) // only adapt if backlight is active (not switched off by tap)
      {
        int raw = analogRead(PIN_LDR);
        // EMA smoothing (alpha 0.05) so passing shadows don't pump the light.
        ldrEma = (ldrEma < 0) ? (float)raw : (0.05f * (float)raw + 0.95f * ldrEma);
        // CYD LDR divider reads high in the dark (~3500-4095) and low in
        // bright light (~200-1000). Map dark -> floor, bright -> user max.
        int maxDuty = Settings.Brightness > 0 ? Settings.Brightness : 255;
        if (maxDuty < LDR_MIN_DUTY)
          maxDuty = LDR_MIN_DUTY;
        int targetDuty = map(constrain((int)ldrEma, LDR_RAW_BRIGHT, LDR_RAW_DARK),
                             LDR_RAW_DARK, LDR_RAW_BRIGHT, LDR_MIN_DUTY, maxDuty);
        if (abs(g_currentDuty - targetDuty) > 3)
        {
          // slew a few steps per tick for a smooth fade
          int stepSz = abs(targetDuty - g_currentDuty) > 24 ? 6 : 2;
          int newDuty = g_currentDuty + (targetDuty > g_currentDuty ? stepSz : -stepSz);
          g_currentDuty = constrain(newDuty, LDR_MIN_DUTY, maxDuty);
          ledcWrite(0, g_currentDuty);
        }
      }
    }

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

void esp32_2432S028R_DoLedStuff(unsigned long frame)
{
  unsigned long currentMillis = millis();
  int action = pendingTouch;
  pendingTouch = TA_NONE;
  DisplayDriver *drv = currentDisplayDriver;
  switch (action)
  {
  case TA_BACKLIGHT:
    esp32_2432S028R_AlternateScreenState();
    break;
  case TA_ACCENT:
    break;
  case TA_NEXT:
    drv->current_cyclic_screen = (drv->current_cyclic_screen + 1) % drv->num_cyclic_screens;
    break;
  case TA_PREV:
    drv->current_cyclic_screen = drv->current_cyclic_screen - 1;
    if (drv->current_cyclic_screen < 0)
      drv->current_cyclic_screen = drv->num_cyclic_screens - 1;
    break;
  }

  // Screen changed by touch or the boot button: draw the new page right away
  // from the last values it showed; the next data tick refreshes them.
  if (uiLive && currentDisplayDriver->current_cyclic_screen != uiScreen)
  {
    int s = currentDisplayDriver->current_cyclic_screen;
    ensureChrome(s);
    repaintCached(s);
  }
  if (uiLive)
    saveUiPrefs();

#ifdef HAS_RGB_LED
  rgbLedTick();
#else
  switch (mMonitor.NerdStatus)
  {
  case NM_waitingConfig:
    digitalWrite(LED_PIN, LOW); // steady
    break;

  case NM_Connecting:
    if (currentMillis - previousMillis >= 500)
    {
      previousMillis = currentMillis;
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(LED_PIN_B, !digitalRead(LED_PIN));
    }
    break;

  case NM_hashing:
    if (currentMillis - previousMillis >= 500)
    {
      previousMillis = currentMillis;
      digitalWrite(LED_PIN_B, HIGH);
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    break;
  }
#endif
}

// ----------------------------------------------------- dev: screenshot dump --

#ifdef CYD_UI_SCREENSHOT
static void dumpShot(const char *name)
{
  Serial.printf("\nSHOT_BEGIN %s %d %d\n", name, SCR_W, SCR_H);
  Serial.write((const uint8_t *)shot.getPointer(), SCR_W * SCR_H);
  Serial.printf("\nSHOT_END\n");
  Serial.flush();
}

void cydDumpScreens(void)
{
  shot.setColorDepth(8);
  shot.createSprite(SCR_W, SCR_H);
  if (!shot.created())
  {
    Serial.println("[ui] shot sprite alloc failed");
    return;
  }
  shotOn = true;

  HdrV h;
  h.time = "21:47";
  h.ip = "192.168.100.100";
  h.bars = 3;
  h.status = NM_hashing;

  for (int i = 0; i < HIST_N; i++)
    hist[i] = 610 + 60 * sinf(i * 0.45f) + (i * 7 % 23);
  histCount = HIST_N;
  histEpoch = 1;

  for (int acc = 0; acc < 2; acc++)
  {
    // accent 0 shows every screen; accent 2 (amber/rose) shows the mining screen again
    setAccent(acc == 0 ? 0 : 2);

    // loading + setup
    paintLoading(shot);
    dumpShot(acc == 0 ? "loading" : "loading_alt");
    if (acc == 0)
    {
      paintSetup(shot);
      dumpShot("setup");
    }

    // mine
    uiLive = true;
    gaugeShown = gaugeTarget = 0.84f;
    uiScreen = -1;
    ensureChrome(SCR_MINE);
    MineV m;
    m.hdr = h;
    m.hash = "672.4";
    m.shares = "1,284";
    m.best = "8.73M";
    m.tmpl = "412";
    m.blocks = "0";
    m.total = "58213";
    m.kh = 672.4f;
    m.flash = acc == 1;
    m.strip.v[0] = "1";
    m.strip.l[0] = "worker";
    m.strip.v[1] = "2d";
    m.strip.l[1] = "uptime";
    m.strip.v[2] = "0";
    m.strip.l[2] = "blocks";
    paintMine(m);
    dumpShot(acc == 0 ? "mine" : "mine_alt");
    if (acc == 1)
      continue;

    // market
    uiScreen = -1;
    ensureChrome(SCR_MARKET);
    MarketV k;
    k.hdr = h;
    k.price = "$97,412";
    k.block = "912,345";
    k.height = 912345;
    k.age = 4;
    k.fee = 12;
    k.feeHour = 9;
    k.feeFast = 15;
    k.diff = "146.72T";
    k.net = "912 EH/s";
    k.pct = 34;
    k.remaining = 137655;
    k.eta = "~Apr 2028";
    paintMarket(k);
    dumpShot("market");

    // clock
    uiScreen = -1;
    ensureChrome(SCR_CLOCK);
    ClockV c;
    c.hdr = h;
    c.hh = "21";
    c.mm = "47";
    c.colon = true;
    c.date = fmtDate("12/10/2026");
    c.hash = "951.4";
    c.flash = false;
    c.temp = "51°C";
    c.uptime = "1d";
    c.link = "ONLINE";
    c.linkCol = C_GOOD;
    c.tempCol = C_TEXT;
    paintClock(c);
    dumpShot("clock");
  }

  shotOn = false;
  shot.deleteSprite();
  uiLive = false;
  uiScreen = -1;
  resetWidgets();
}
#endif

CyclicScreenFunction esp32_2432S028RCyclicScreens[] = {
    esp32_2432S028R_MinerScreen,
    esp32_2432S028R_MarketScreen,
    esp32_2432S028R_ClockScreen,
    esp32_2432S028R_ArchitectureScreen};

DisplayDriver esp32_2432S028RDriver = {
    esp32_2432S028R_Init,
    esp32_2432S028R_AlternateScreenState,
    esp32_2432S028R_AlternateRotation,
    esp32_2432S028R_LoadingScreen,
    esp32_2432S028R_SetupScreen,
    esp32_2432S028RCyclicScreens,
    esp32_2432S028R_AnimateCurrentScreen,
    esp32_2432S028R_DoLedStuff,
    SCREENS_ARRAY_SIZE(esp32_2432S028RCyclicScreens),
    0,
    WIDTH,
    HEIGHT};
#endif

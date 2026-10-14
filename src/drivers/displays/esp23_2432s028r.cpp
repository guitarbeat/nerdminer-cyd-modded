// "PULSE" theme for the ESP32-2432S028R ("CYD", 320x240 landscape).
//
// A dark, glassy, gauge-driven look: no bitmaps at all, everything is drawn
// from primitives so the palette can be re-tinted at runtime (tap the
// NERDMINER wordmark, top-left, to cycle accent colours).
//
//   MINE    segmented hashrate ring, stat tiles, pool strip
//   MARKET  BTC price, block height, fees, difficulty, halving progress
//   CLOCK   big desk clock, key numbers, hashrate history bars
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
#include "OpenFontRender.h"
#include <SPI.h>
#include "rotation.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"
#include "currency.h"

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

static inline int gCX() { return isPortrait() ? 120 : 92; }
static inline int gCY() { return isPortrait() ? 104 : 110; }
static inline int gR()  { return isPortrait() ? 72 : 78; }
static inline int gRI() { return isPortrait() ? 62 : 67; }

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

// history of hashrate for the CLOCK screen
#define HIST_N 48
#define HIST_EVERY_MS 10000
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
  String c = currencyFor(Settings.Currency).code;
  c.toUpperCase();
  return "BTC / " + c;
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
  text(wgt, title, 0, 5, 13, C_TEXT, C_BG);
  int wm = textW(title, 13);

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
  text(wgt, label, sx, 6, 9, col, C_BG);

  // Time (right-aligned)
  text(wgt, h.time.c_str(), W - 4, 5, 13, C_TEXT, C_BG, Align::TopRight);

  int timeW = textW(h.time.c_str(), 13);
  int timeLeft = (W - 4) - timeW;

  // Local IP (landscape only, quiet secondary information to the left of time)
  int gapRight = timeLeft - 8;
  if (!isPortrait() && h.ip.length())
  {
    int ipRight = timeLeft - 14;
    text(wgt, h.ip.c_str(), ipRight, 6, 10, C_DIM, C_BG, Align::TopRight);
    gapRight = ipRight - textW(h.ip.c_str(), 10) - 10;
  }

  // ── CarCYD-inspired nav dots: 4 tiny pills in the mid-header gap ──
  {
    const int nScreens = 4;
    const int dotW = 7, dotH = 3, dotGap = 2;
    int totalW = nScreens * dotW + (nScreens - 1) * dotGap;
    int dotY = 12; // y of top edge, centered vertically in 26px header

    int statusEnd = sx + textW(label, 9) + 6;
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
  StripV strip;
};

static const int TILE_Y[4] = {32, 72, 112, 152};

static void chromeMine(TFT_eSPI &d)
{
  memset(segLvl, 0xFF, sizeof(segLvl));
  gaugeDraw(d, gaugeShown);
  chromeStripCells(d);
}

static void paintMine(const MineV &m)
{
  drawHeader(m.hdr);

  int cx = G_CX;
  int cy = G_CY;

  // hero readout inside the ring
  String k = m.hash + "|" + (isPortrait() ? "P" : "L");
  if (need(W_HERO, k))
  {
    if (wBegin(104, 74, C_BG))
    {
      text(wgt, "HASHRATE", 52, 4, 9, C_DIM, C_BG, Align::TopCenter);
      unsigned sz = m.hash.length() <= 5 ? 36 : (m.hash.length() == 6 ? 30 : 26);
      text(wgt, m.hash.c_str(), 52, 15, sz, C_TEXT, C_BG, Align::TopCenter);
      text(wgt, "KH/s", 52, 56, 11, C_DIM, C_BG, Align::TopCenter);
      wEnd(cx - 52, cy - 38);
      done(W_HERO, k);
    }
  }

  // lifetime hashes, in the gap at the bottom of the ring
  k = m.total + "|" + (isPortrait() ? "P" : "L");
  if (need(W_TOTAL, k))
  {
    int tw = isPortrait() ? 92 : 120;
    if (wBegin(tw, 16, C_BG))
    {
      textPair(wgt, tw / 2, 1, commas(m.total).c_str(), "MH TOTAL", isPortrait() ? 10 : 11, C_TEXT, C_DIM, C_BG);
      wEnd(cx - (tw / 2), isPortrait() ? 158 : 171);
      done(W_TOTAL, k);
    }
  }

  bool found = m.blocks.toInt() > 0;
  uint16_t sharesCol = m.flash ? C_GOOD : C_TEXT;
  uint16_t blocksCol = found ? C_GOLD : C_TEXT;

  if (isPortrait())
  {
    const int tw = 112, th = 36;
    drawTile(W_T0, 6, 180, tw, th, "SHARES", m.shares.c_str(), sharesCol, C_EDGE);
    drawTile(W_T1, 122, 180, tw, th, "BEST DIFF", m.best.c_str(), C_TEXT, C_EDGE);
    drawTile(W_T2, 6, 222, tw, th, "TEMPLATES", m.tmpl.c_str(), C_TEXT, C_EDGE);
    drawTile(W_T3, 122, 222, tw, th, "BLOCKS FOUND", m.blocks.c_str(), blocksCol, C_EDGE);
  }
  else
  {
    const int tx = 184, tw = 128, th = 36;
    drawTile(W_T0, tx, TILE_Y[0], tw, th, "SHARES", m.shares.c_str(), sharesCol, C_EDGE);
    drawTile(W_T1, tx, TILE_Y[1], tw, th, "BEST DIFF", m.best.c_str(), C_TEXT, C_EDGE);
    drawTile(W_T2, tx, TILE_Y[2], tw, th, "TEMPLATES", m.tmpl.c_str(), C_TEXT, C_EDGE);
    drawTile(W_T3, tx, TILE_Y[3], tw, th, "BLOCKS FOUND", m.blocks.c_str(), blocksCol, C_EDGE);
  }

  drawStrip(m.strip);
}

// ========================================================== MARKET screen ==

struct MarketV
{
  HdrV hdr;
  String price, block, fee, diff, net;
  int pct;
  String remaining;
  StripV strip;
};

static void chromeMarket(TFT_eSPI &d)
{
  if (isPortrait())
  {
    // Hero Price Card (y=30..84, h=54)
    panel(d, 6, 30, 228, 54, 8, C_PANEL, C_EDGE, C_BG);
    text(d, "BTC / USDT • BINANCE", 14, 33, 10, C_DIM, C_PANEL);

    // Candlestick Container (y=88..206, h=118)
    panel(d, 6, 88, 228, 118, 8, C_PANEL, C_EDGE, C_BG);
    text(d, "BTC/USDT 1H CANDLES", 14, 92, 10, C_DIM, C_PANEL);

    // Halving & Network Card (y=210..260, h=50)
    panel(d, 6, 210, 228, 50, 8, C_PANEL, C_EDGE, C_BG);

    chromeStripCells(d);
  }
  else
  {
    panel(d, 8, 30, 304, 52, 8, C_PANEL, C_EDGE, C_BG);
    text(d, btcPairLabel().c_str(), 16, 34, 10, C_DIM, C_PANEL);
    text(d, "LATEST BLOCK", 300, 34, 10, C_DIM, C_PANEL, Align::TopRight);

    // Candlesticks (y=86..150, h=64)
    panel(d, 8, 86, 304, 68, 6, C_PANEL, C_EDGE, C_BG);

    // Halving Card (y=158..192)
    panel(d, 8, 158, 304, 36, 6, C_PANEL, C_EDGE, C_BG);

    chromeStripCells(d);
  }
}

static void drawCandlesticks(int x, int y, int w, int h)
{
  if (!wBegin(w, h, C_PANEL))
    return;

  // Background grid lines (3 dotted/subtle rules)
  const int plotX = 4, plotY = 16, plotW = w - 8, plotH = h - 26;
  for (int i = 0; i < 3; i++)
  {
    int gy = plotY + (plotH / 3) * i;
    wgt.drawFastHLine(plotX, gy, plotW, C_TRACK);
  }

  // Use live Binance US klines if available, otherwise simulated realistic curve
  int n = g_klineCount > 0 ? g_klineCount : 22;
  float minP = 1e9f, maxP = -1e9f;

  for (int i = 0; i < n; i++)
  {
    float low = g_klineCount > 0 ? g_btcKlines[i].l : (66200.0f + (sinf(i * 0.35f) * 1400.0f) - 300.0f);
    float high = g_klineCount > 0 ? g_btcKlines[i].h : (66200.0f + (sinf(i * 0.35f) * 1400.0f) + 400.0f);
    if (low < minP) minP = low;
    if (high > maxP) maxP = high;
  }
  float rng = (maxP - minP) > 1.0f ? (maxP - minP) : 100.0f;

  // Draw Timeframe Badge + High / Low Labels
  char hBuf[16], lBuf[16];
  snprintf(hBuf, sizeof(hBuf), "H: %.1fK", maxP / 1000.0f);
  snprintf(lBuf, sizeof(lBuf), "L: %.1fK", minP / 1000.0f);
  text(wgt, hBuf, w - 6, 2, 9, C_GOLD, C_PANEL, Align::TopRight);
  text(wgt, lBuf, 6, h - 11, 9, C_DIM, C_PANEL);
  text(wgt, "1H [MA7]", w / 2, 2, 9, C_METER, C_PANEL, Align::TopCenter);

  // Candlesticks
  float ma7[24] = {0};
  int step = plotW / n;
  int cW = step > 4 ? step - 2 : 3;

  for (int i = 0; i < n; i++)
  {
    float o = g_klineCount > 0 ? g_btcKlines[i].o : (66200.0f + sinf(i * 0.35f) * 1400.0f);
    float c = g_klineCount > 0 ? g_btcKlines[i].c : (66200.0f + sinf((i + 1) * 0.35f) * 1400.0f);
    float hi = g_klineCount > 0 ? g_btcKlines[i].h : (fmaxf(o, c) + 350.0f);
    float lo = g_klineCount > 0 ? g_btcKlines[i].l : (fminf(o, c) - 250.0f);

    // Running MA-7
    float sum = 0; int cnt = 0;
    for (int j = (i >= 6 ? i - 6 : 0); j <= i; j++)
    {
      sum += (g_klineCount > 0 ? g_btcKlines[j].c : (66200.0f + sinf((j + 1) * 0.35f) * 1400.0f));
      cnt++;
    }
    ma7[i] = sum / cnt;

    bool isBull = c >= o;
    uint16_t col = isBull ? C_GOOD : C_BAD;
    int cx = plotX + i * step + step / 2;
    int yHi = plotY + plotH - (int)(((hi - minP) / rng) * plotH);
    int yLo = plotY + plotH - (int)(((lo - minP) / rng) * plotH);
    int yOp = plotY + plotH - (int)(((o - minP) / rng) * plotH);
    int yCl = plotY + plotH - (int)(((c - minP) / rng) * plotH);

    // Wick
    wgt.drawFastVLine(cx, yHi, yLo - yHi > 0 ? yLo - yHi : 1, col);

    // Body
    int yTop = yOp < yCl ? yOp : yCl;
    int bH = abs(yCl - yOp);
    if (bH < 2) bH = 2;
    wgt.fillRect(cx - cW / 2, yTop, cW, bH, col);
  }

  // Draw MA-7 Smoothed Line
  for (int i = 1; i < n; i++)
  {
    int x0 = plotX + (i - 1) * step + step / 2;
    int y0 = plotY + plotH - (int)(((ma7[i - 1] - minP) / rng) * plotH);
    int x1 = plotX + i * step + step / 2;
    int y1 = plotY + plotH - (int)(((ma7[i] - minP) / rng) * plotH);
    wgt.drawLine(x0, y0, x1, y1, C_GOLD);
  }

  wEnd(x, y);
}

static void paintMarket(const MarketV &m)
{
  drawHeader(m.hdr);

  // 1. Hero Card: Price & Stats
  String k = m.price + "|" + String(g_btc24hChange, 1) + "|" + (isPortrait() ? "P" : "L");
  if (need(W_PRICE, k))
  {
    if (isPortrait())
    {
      if (wBegin(216, 38, C_PANEL))
      {
        text(wgt, m.price.c_str(), 2, 0, 22, C_GOLD, C_PANEL);
        char chgBuf[16];
        snprintf(chgBuf, sizeof(chgBuf), "%s%.2f%% 24h", g_btc24hChange >= 0 ? "+" : "", g_btc24hChange);
        uint16_t cCol = g_btc24hChange >= 0 ? C_GOOD : C_BAD;
        text(wgt, chgBuf, 212, 1, 10, cCol, C_PANEL, Align::TopRight);

        text(wgt, m.block.c_str(), 212, 14, 12, C_TEXT, C_PANEL, Align::TopRight);
        text(wgt, (m.fee + " fee").c_str(), 212, 26, 10, C_DIM, C_PANEL, Align::TopRight);
        wEnd(12, 44);
        done(W_PRICE, k);
      }
    }
    else
    {
      if (wBegin(292, 34, C_PANEL))
      {
        text(wgt, m.price.c_str(), 4, 0, 24, C_GOLD, C_PANEL);
        char chgBuf[16];
        snprintf(chgBuf, sizeof(chgBuf), "%s%.2f%%", g_btc24hChange >= 0 ? "+" : "", g_btc24hChange);
        text(wgt, chgBuf, 120, 4, 11, g_btc24hChange >= 0 ? C_GOOD : C_BAD, C_PANEL);
        text(wgt, m.block.c_str(), 288, 2, 14, C_TEXT, C_PANEL, Align::TopRight);
        wEnd(14, 44);
        done(W_PRICE, k);
      }
    }
  }

  // 2. Candlesticks
  String ck = String(g_klineCount) + "|" + (g_klineCount > 0 ? String(g_btcKlines[g_klineCount - 1].c, 0) : "0") + "|" + (isPortrait() ? "P" : "L");
  if (need(W_CANDLES, ck))
  {
    if (isPortrait())
      drawCandlesticks(10, 92, 220, 110);
    else
      drawCandlesticks(12, 90, 296, 60);
    done(W_CANDLES, ck);
  }

  // 3. Halving Progress Card
  char kb[64];
  snprintf(kb, sizeof(kb), "%d|%s|%s|%s|%d", m.pct, m.remaining.c_str(), m.diff.c_str(), m.net.c_str(), isPortrait() ? 1 : 0);
  if (need(W_HALV, kb))
  {
    if (isPortrait())
    {
      if (wBegin(216, 44, C_PANEL))
      {
        text(wgt, "HALVING #5 PROGRESS", 2, 1, 10, C_DIM, C_PANEL);
        char pc[8];
        snprintf(pc, sizeof(pc), "%d%%", m.pct);
        text(wgt, pc, 116, 1, 10, C_METER, C_PANEL);
        String rem = commas(m.remaining) + " blk left";
        text(wgt, rem.c_str(), 212, 1, 9, C_TEXT, C_PANEL, Align::TopRight);

        // Bar
        wgt.fillRect(2, 13, 212, 6, C_TRACK);
        int fw = (212 * m.pct) / 100;
        if (fw > 0) wgt.fillRect(2, 13, fw, 6, C_METER);

        // Subline: Difficulty & Hashrate
        text(wgt, ("Diff: " + m.diff).c_str(), 2, 23, 10, C_GOLD, C_PANEL);
        text(wgt, ("Net: " + m.net).c_str(), 212, 23, 10, C_GOOD, C_PANEL, Align::TopRight);

        wEnd(12, 213);
        done(W_HALV, kb);
      }
    }
    else
    {
      if (wBegin(288, 30, C_PANEL))
      {
        text(wgt, "HALVING #5", 2, 1, 10, C_DIM, C_PANEL);
        char pc[8];
        snprintf(pc, sizeof(pc), "%d%%", m.pct);
        text(wgt, pc, 72, 1, 10, C_METER, C_PANEL);
        text(wgt, ("Diff: " + m.diff + " | Net: " + m.net).c_str(), 284, 1, 10, C_TEXT, C_PANEL, Align::TopRight);

        wgt.fillRect(2, 15, 284, 6, C_TRACK);
        int fw = (284 * m.pct) / 100;
        if (fw > 0) wgt.fillRect(2, 15, fw, 6, C_METER);

        wEnd(16, 160);
        done(W_HALV, kb);
      }
    }
  }

  drawStrip(m.strip);
}

// ============================================================ CLOCK screen ==

struct ClockV
{
  HdrV hdr;
  String hh, mm;
  bool colon;
  String date, price, block, hash;
};

static void chromeClock(TFT_eSPI &d)
{
  if (isPortrait())
  {
    panel(d, 6, 214, 228, 46, 6, C_PANEL, C_EDGE, C_BG);
    text(d, "ROLLING 36-SLOT HASHRATE HISTORY", 12, 217, 10, C_DIM, C_PANEL);
    chromeStripCells(d);
  }
  else
  {
    chromeStrip(d);
  }
}

static void drawChart()
{
  char k[32];
  snprintf(k, sizeof(k), "%lu|%u|%d|%d", (unsigned long)histEpoch, histCount, accentIdx, isPortrait() ? 1 : 0);
  if (!need(W_CHART, k))
    return;
  const int W = isPortrait() ? 216 : 288, H = 30;
  if (!wBegin(W, H, C_PANEL))
    return;
  wgt.drawFastHLine(0, H - 1, W, C_TRACK); // baseline grounding
  float mx = 1;
  for (int i = 0; i < histCount; i++)
    if (hist[i] > mx)
      mx = hist[i];
  mx *= 1.08f;
  const int numSlots = isPortrait() ? 36 : HIST_N;
  const int step = 6;
  for (int i = 0; i < numSlots; i++)
  {
    int slot = i - (numSlots - histCount); // right-align so the newest bar is on the right
    int x = i * step;
    if (slot < 0 || slot >= HIST_N)
    {
      wgt.fillRect(x, H - 2, 5, 2, C_TRACK); // not sampled yet
      continue;
    }
    int bh = (int)(H * hist[slot] / mx);
    if (bh < 2)
      bh = 2;
    wgt.fillRect(x, H - bh, 5, bh, C_METER);
  }
  wEnd(isPortrait() ? 12 : 16, isPortrait() ? 228 : (STRIP_Y + 4));
  done(W_CHART, k);
}

static void paintClock(const ClockV &c)
{
  drawHeader(c.hdr);

  if (isPortrait())
  {
    String k = c.hh + "|P";
    if (need(W_HH, k) && wBegin(86, 56, C_BG))
    {
      text(wgt, c.hh.c_str(), 84, -22, 70, C_TEXT, C_BG, Align::TopRight);
      wEnd(16, 36);
      done(W_HH, k);
    }
    k = c.mm + "|P";
    if (need(W_MM, k) && wBegin(86, 56, C_BG))
    {
      text(wgt, c.mm.c_str(), 2, -22, 70, C_TEXT, C_BG, Align::TopLeft);
      wEnd(138, 36);
      done(W_MM, k);
    }
    k = String(c.colon) + "|P";
    if (need(W_COLON, k) && wBegin(28, 56, C_BG))
    {
      uint16_t dot = c.colon ? C_DIM : C_BG;
      wgt.fillCircle(14, 18, 4, dot);
      wgt.fillCircle(14, 38, 4, dot);
      wEnd(106, 36);
      done(W_COLON, k);
    }

    k = c.date + "|P";
    if (need(W_DATE, k) && wBegin(220, 18, C_BG))
    {
      text(wgt, c.date.c_str(), 110, 1, 12, C_DIM, C_BG, Align::TopCenter);
      wEnd(10, 96);
      done(W_DATE, k);
    }

    drawTile(W_C0, 6, 120, 112, 44, btcPairLabel().c_str(), c.price.c_str(), C_TEXT, C_EDGE);
    drawTile(W_C1, 122, 120, 112, 44, "BLOCK", c.block.c_str(), C_TEXT, C_EDGE);
    drawTile(W_C2, 6, 170, 228, 44, "HASHRATE KH/s", c.hash.c_str(), C_TEXT, C_EDGE);
  }
  else
  {
    String k = c.hh + "|L";
    if (need(W_HH, k) && wBegin(112, 70, C_BG))
    {
      text(wgt, c.hh.c_str(), 110, -31, 92, C_TEXT, C_BG, Align::TopRight);
      wEnd(34, 32);
      done(W_HH, k);
    }
    k = c.mm + "|L";
    if (need(W_MM, k) && wBegin(112, 70, C_BG))
    {
      text(wgt, c.mm.c_str(), 2, -31, 92, C_TEXT, C_BG, Align::TopLeft);
      wEnd(174, 32);
      done(W_MM, k);
    }
    k = String(c.colon) + "|L";
    if (need(W_COLON, k) && wBegin(28, 70, C_BG))
    {
      uint16_t dot = c.colon ? C_DIM : C_BG;
      wgt.fillCircle(14, 22, 5, dot);
      wgt.fillCircle(14, 47, 5, dot);
      wEnd(146, 32);
      done(W_COLON, k);
    }

    k = c.date + "|L";
    if (need(W_DATE, k) && wBegin(220, 18, C_BG))
    {
      text(wgt, c.date.c_str(), 110, 1, 12, C_DIM, C_BG, Align::TopCenter);
      wEnd(50, 108);
      done(W_DATE, k);
    }

    const int y = 140, w = 98, h = 48;
    drawTile(W_C0, 8, y, w, h, btcPairLabel().c_str(), c.price.c_str(), C_TEXT, C_EDGE);
    drawTile(W_C1, 111, y, w, h, "BLOCK", c.block.c_str(), C_TEXT, C_EDGE);
    drawTile(W_C2, 214, y, w, h, "HASHRATE KH/s", c.hash.c_str(), C_TEXT, C_EDGE);
  }

  drawChart();
}

// =================================== PROTOCOL (ARCH & ECONOMICS) screen ==
// Hardware instrument view unifying 80-byte block header, SHA256d PoW mechanics,
// solo block jackpot (3.125 BTC), and network target economics.

struct ArchitectureV
{
  HdrV hdr;
  String netDiff;
  StripV strip;
};

static void drawBitcoinCoin(TFT_eSPI &d, int cx, int cy, int r)
{
  d.fillCircle(cx, cy, r, C_GOLD);
  d.drawCircle(cx, cy, r, 0xFEA0);
  d.fillCircle(cx, cy, r - 2, 0xD4A0);
  d.fillCircle(cx, cy, r - 4, 0x9280);

  // Crisp Bitcoin "B" glyph with double prongs
  int h = r - 7;
  int bx = cx - 3;
  int by = cy - h;
  d.fillRect(bx, by, 3, h * 2 + 1, TFT_WHITE);
  d.drawFastHLine(bx, by + 1, 5, TFT_WHITE);
  d.drawFastVLine(bx + 5, by + 1, h, TFT_WHITE);
  d.drawFastHLine(bx, by + h, 5, TFT_WHITE);
  d.drawFastHLine(bx, by + h * 2, 6, TFT_WHITE);
  d.drawFastVLine(bx + 6, by + h, h, TFT_WHITE);
  // Prongs
  d.drawFastVLine(bx + 1, by - 2, 2, TFT_WHITE);
  d.drawFastVLine(bx + 3, by - 2, 2, TFT_WHITE);
  d.drawFastVLine(bx + 1, by + h * 2 + 1, 2, TFT_WHITE);
  d.drawFastVLine(bx + 3, by + h * 2 + 1, 2, TFT_WHITE);
}

static void drawPipelineArrow(TFT_eSPI &d, int x, int y, int size, uint16_t color)
{
  d.fillTriangle(x, y - size, x + (size * 5) / 4, y, x, y + size, color);
}

static void chromeArchitecture(TFT_eSPI &d)
{
  if (isPortrait())
  {
    // 1. Hero Block Reward Jackpot Card (y=30, h=54)
    panel(d, 6, 30, 228, 54, 8, C_PANEL, C_EDGE, C_BG);
    drawBitcoinCoin(d, 30, 57, 18);
    text(d, "SOLO BLOCK REWARD", 58, 35, 9, C_DIM, C_PANEL);
    text(d, "3.125 BTC", 58, 48, 22, C_GOLD, C_PANEL);
    // Green Pill Badge
    panel(d, 58, 68, 120, 12, 3, 0x0A24, C_GOOD, C_PANEL);
    text(d, "★ 100% TO YOUR WALLET", 62, 69, 8, C_GOOD, 0x0A24);

    // 2. 80-Byte Candidate Block Header Diagram (y=88, h=58)
    panel(d, 6, 88, 228, 58, 6, C_PANEL, C_EDGE, C_BG);
    text(d, "80-BYTE CANDIDATE HEADER", 12, 92, 8, C_DIM, C_PANEL);

    // Segmented block bar (w=216 at y=104)
    const int bx = 12, by = 104;
    d.fillRect(bx, by, 12, 16, 0x3DEF);        // Ver 4B
    text(d, "V4", bx + 1, by + 3, 7, TFT_WHITE, 0x3DEF);
    d.fillRect(bx + 13, by, 84, 16, 0x6479);   // Prev 32B
    text(d, "PREV HASH 32B", bx + 16, by + 3, 7, TFT_WHITE, 0x6479);
    d.fillRect(bx + 98, by, 84, 16, 0xBC9F);   // Merkle 32B
    text(d, "MERKLE ROOT 32B", bx + 102, by + 3, 7, TFT_WHITE, 0xBC9F);
    d.fillRect(bx + 183, by, 10, 16, 0xFC00);  // Time 4B
    text(d, "T", bx + 185, by + 3, 7, TFT_BLACK, 0xFC00);
    d.fillRect(bx + 194, by, 10, 16, 0xF800);  // Bits 4B
    text(d, "B", bx + 196, by + 3, 7, TFT_WHITE, 0xF800);
    d.fillRect(bx + 205, by, 11, 16, C_GOLD);  // Nonce 4B
    text(d, "N", bx + 207, by + 3, 7, TFT_BLACK, C_GOLD);

    // Nonce dial indicator box
    panel(d, 12, 124, 216, 18, 3, C_TRACK, C_EDGE, C_PANEL);
    text(d, "76B Template  +", 16, 128, 8, C_DIM, C_TRACK);
    text(d, "NONCE DIAL: 0x330776", 110, 128, 9, C_GOLD, C_TRACK);

    // 3. 4-Stage PoW Flow Diagram with arrows (y=150, h=48)
    panel(d, 6, 150, 228, 48, 6, C_PANEL, C_EDGE, C_BG);
    const int py = 154;
    // Stage 1: 80B Header
    panel(d, 10, py, 42, 38, 4, C_TRACK, 0x3DEF, C_PANEL);
    text(d, "80B", 31, py + 4, 9, 0x3DEF, C_TRACK, Align::TopCenter);
    text(d, "HEADER", 31, py + 18, 7, C_DIM, C_TRACK, Align::TopCenter);
    drawPipelineArrow(d, 55, py + 19, 4, C_METER);

    // Stage 2: 2x SHA-256
    panel(d, 64, py, 42, 38, 4, C_TRACK, C_METER, C_PANEL);
    text(d, "SHA256", 85, py + 4, 8, C_METER, C_TRACK, Align::TopCenter);
    text(d, "DOUBLE", 85, py + 18, 7, C_DIM, C_TRACK, Align::TopCenter);
    drawPipelineArrow(d, 109, py + 19, 4, C_METER);

    // Stage 3: Target Zeros
    panel(d, 118, py, 42, 38, 4, C_TRACK, C_GOLD, C_PANEL);
    text(d, "TARGET", 139, py + 4, 8, C_GOLD, C_TRACK, Align::TopCenter);
    text(d, "18+ 0s", 139, py + 18, 7, C_DIM, C_TRACK, Align::TopCenter);
    drawPipelineArrow(d, 163, py + 19, 4, C_METER);

    // Stage 4: Test Win
    panel(d, 172, py, 46, 38, 4, C_TRACK, C_GOOD, C_PANEL);
    text(d, "H < TGT", 195, py + 4, 8, C_GOOD, C_TRACK, Align::TopCenter);
    text(d, "WINNER", 195, py + 18, 7, C_GOOD, C_TRACK, Align::TopCenter);

    // 4. Protocol Metrics & Scarcity (y=202, h=58)
    drawTile(W_LOT_JACKPOT, 6, 202, 112, 58, "GLOBAL TARGET", "132+ T", C_TEXT, C_EDGE);

    // Hard Cap Supply & Scarcity Bar
    panel(d, 122, 202, 112, 58, 6, C_PANEL, C_EDGE, C_BG);
    text(d, "HARD CAP SUPPLY", 128, 207, 8, C_DIM, C_PANEL);
    text(d, "20,999,999", 128, 219, 13, C_GOLD, C_PANEL);
    // Golden Scarcity Progress Bar
    d.fillRect(128, 237, 100, 4, C_TRACK);
    d.fillRect(128, 237, 94, 4, C_GOLD);
    text(d, "94.3% Mined • 21M Cap", 128, 245, 7, C_DIM, C_PANEL);

    // Bottom strip (y=266..314)
    chromeStripCells(d);
  }
  else
  {
    // Landscape 320x240
    // 1. Hero Block Reward Jackpot Card (y=30, h=48, w=304)
    panel(d, 8, 30, 304, 48, 8, C_PANEL, C_EDGE, C_BG);
    drawBitcoinCoin(d, 28, 54, 16);
    text(d, "SOLO BLOCK REWARD", 52, 34, 8, C_DIM, C_PANEL);
    text(d, "3.125 BTC", 52, 46, 22, C_GOLD, C_PANEL);
    text(d, "+ fees (~$250,000+ USD)", 156, 52, 9, C_DIM, C_PANEL);
    // Green Pill Badge
    panel(d, 230, 36, 76, 16, 4, 0x0A24, C_GOOD, C_PANEL);
    text(d, "100% TO YOU", 268, 40, 8, C_GOOD, 0x0A24, Align::TopCenter);

    // 2. Connected 4-Stage PoW Flow Diagram (y=82, h=50, w=304)
    panel(d, 8, 82, 304, 50, 6, C_PANEL, C_EDGE, C_BG);
    const int py = 88;
    // Stage 1
    panel(d, 14, py, 60, 38, 4, C_TRACK, 0x3DEF, C_PANEL);
    text(d, "80B HEADER", 44, py + 4, 8, 0x3DEF, C_TRACK, Align::TopCenter);
    text(d, "76B + Nonce", 44, py + 20, 7, C_DIM, C_TRACK, Align::TopCenter);
    drawPipelineArrow(d, 77, py + 19, 4, C_METER);

    // Stage 2
    panel(d, 85, py, 60, 38, 4, C_TRACK, C_METER, C_PANEL);
    text(d, "2x SHA-256", 115, py + 4, 8, C_METER, C_TRACK, Align::TopCenter);
    text(d, "Double Hash", 115, py + 20, 7, C_DIM, C_TRACK, Align::TopCenter);
    drawPipelineArrow(d, 148, py + 19, 4, C_METER);

    // Stage 3
    panel(d, 156, py, 60, 38, 4, C_TRACK, C_GOLD, C_PANEL);
    text(d, "TARGET", 186, py + 4, 8, C_GOLD, C_TRACK, Align::TopCenter);
    text(d, "18+ Zeros", 186, py + 20, 7, C_DIM, C_TRACK, Align::TopCenter);
    drawPipelineArrow(d, 219, py + 19, 4, C_METER);

    // Stage 4
    panel(d, 227, py, 66, 38, 4, C_TRACK, C_GOOD, C_PANEL);
    text(d, "H < TARGET", 260, py + 4, 8, C_GOOD, C_TRACK, Align::TopCenter);
    text(d, "Jackpot Win!", 260, py + 20, 7, C_GOOD, C_TRACK, Align::TopCenter);

    // 3. Protocol Metrics & Scarcity (y=136, h=54, w=304)
    drawTile(W_LOT_JACKPOT, 8, 136, 148, 54, "GLOBAL TARGET", "132.72 T", C_TEXT, C_EDGE);

    // Hard Cap Supply Card
    panel(d, 164, 136, 148, 54, 6, C_PANEL, C_EDGE, C_BG);
    text(d, "HARD CAP SUPPLY", 172, 141, 8, C_DIM, C_PANEL);
    text(d, "20,999,999 BTC", 172, 153, 14, C_GOLD, C_PANEL);
    d.fillRect(172, 171, 132, 4, C_TRACK);
    d.fillRect(172, 171, 124, 4, C_GOLD);
    text(d, "94.3% Mined • 21M Cap", 172, 178, 7, C_DIM, C_PANEL);

    // Bottom strip (y=196..236)
    chromeStripCells(d);
  }
}

static void paintArchitecture(const ArchitectureV &av)
{
  drawHeader(av.hdr);
  if (isPortrait())
    drawTile(W_LOT_JACKPOT, 6, 202, 112, 58, "GLOBAL TARGET", av.netDiff.c_str(), C_TEXT, C_EDGE);
  else
    drawTile(W_LOT_JACKPOT, 8, 136, 148, 54, "GLOBAL TARGET", av.netDiff.c_str(), C_TEXT, C_EDGE);

  drawStrip(av.strip);
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
  m.strip.l[0] = "WORKERS";
  m.strip.v[0] = String(pData.workersCount);
  m.strip.l[1] = isPortrait() ? "POOL" : "POOL RATE";
  m.strip.v[1] = pData.workersHash;
  m.strip.l[2] = isPortrait() ? "BEST" : "POOL BEST";
  m.strip.v[2] = pData.bestDifficulty;
  m.strip.l[3] = "UPTIME";
  m.strip.v[3] = fmtUptime(data.timeMining);
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
  m.block = commas(data.blockHeight);
  m.fee = data.halfHourFee.startsWith("0 ") ? String("--") : data.halfHourFee;
  m.diff = data.netwrokDifficulty.length() ? data.netwrokDifficulty : String("--");
  m.net = data.globalHashRate.length() ? data.globalHashRate + " EH/s" : String("--");
  m.pct = (int)data.progressPercent;
  m.remaining = String(data.remainingBlocks.toInt());
  m.strip.l[0] = "WORKERS";
  m.strip.v[0] = String(pData.workersCount);
  m.strip.l[1] = isPortrait() ? "POOL" : "POOL RATE";
  m.strip.v[1] = pData.workersHash;
  m.strip.l[2] = isPortrait() ? "BEST" : "POOL BEST";
  m.strip.v[2] = pData.bestDifficulty;
  m.strip.l[3] = "SHARES";
  m.strip.v[3] = commas(data.completedShares);
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

  ClockV c;
  c.hdr = liveHeader(data.currentTime);
  int colon = data.currentTime.indexOf(':');
  c.hh = colon > 0 ? data.currentTime.substring(0, colon) : String("--");
  c.mm = colon > 0 ? data.currentTime.substring(colon + 1) : String("--");
  c.colon = (millis() / 1000) & 1;
  c.date = fmtDate(data.currentDate);
  c.price = fmtPrice(data.btcPrice);
  c.block = commas(data.blockHeight);
  c.hash = data.currentHashRate;
  lastClock = c;
  haveClock = true;
  paintClock(c);

  logShare(data.completedShares, data.totalKHashes, data.currentHashRate);
}

void esp32_2432S028R_ArchitectureScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  refreshPool();
  uiLive = true;
  ensureChrome(SCR_ARCH);
  float kh = data.currentHashRate.toFloat();
  noteHashrate(kh);

  ArchitectureV av;
  av.hdr = liveHeader(data.currentTime);
  if (g_networkDifficulty > 0.0)
  {
    char b[32];
    snprintf(b, sizeof(b), "%.2f T", g_networkDifficulty / 1e12);
    av.netDiff = String(b);
  }
  else
  {
    av.netDiff = gData.difficulty.length() ? gData.difficulty : String("146.72 T");
  }
  av.strip.l[0] = "WORKERS";
  av.strip.v[0] = String(pData.workersCount);
  av.strip.l[1] = isPortrait() ? "POOL" : "POOL RATE";
  av.strip.v[1] = pData.workersHash;
  av.strip.l[2] = isPortrait() ? "BEST" : "POOL BEST";
  av.strip.v[2] = pData.bestDifficulty;
  av.strip.l[3] = "ODDS";
  av.strip.v[3] = "1/70M";
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

  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN_B, OUTPUT);
  pinMode(LED_PIN_G, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_PIN_B, HIGH);
  digitalWrite(LED_PIN_G, HIGH);
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

static float ldrEma = -1.0f;
static unsigned long lastLdrCheck = 0;

static void touchTask(void *)
{
  bool wasDown = false;
  int downCount = 0;
  pinMode(34, INPUT); // CYD LDR photoresistor on GPIO 34
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

    // Auto-dimming check every 500ms using LDR (GPIO 34)
    unsigned long now = millis();
    if (g_backlightReady && (now - lastLdrCheck >= 500))
    {
      lastLdrCheck = now;
      if (g_currentDuty > 0) // only adapt if backlight is active (not switched off by tap)
      {
        int raw = analogRead(34);
        ldrEma = (ldrEma < 0) ? (float)raw : (0.10f * (float)raw + 0.90f * ldrEma);
        // Dark room raw ~ 3500-4095; Bright room raw ~ 200-1000 (LDR pulls to GND in light)
        // Map brightness smoothly between min duty (25) and max (Settings.Brightness or 255)
        int maxDuty = Settings.Brightness > 0 ? Settings.Brightness : 255;
        int targetDuty = map(constrain((int)ldrEma, 500, 3800), 3800, 500, 25, maxDuty);
        if (abs(g_currentDuty - targetDuty) > 6)
        {
          int newDuty = g_currentDuty + (targetDuty > g_currentDuty ? 4 : -4);
          g_currentDuty = constrain(newDuty, 20, maxDuty);
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
    m.strip.l[0] = "WORKERS";
    m.strip.v[0] = "3";
    m.strip.l[1] = "POOL RATE";
    m.strip.v[1] = "1.92M";
    m.strip.l[2] = "POOL BEST";
    m.strip.v[2] = "412.6M";
    m.strip.l[3] = "UPTIME";
    m.strip.v[3] = "3d 07h";
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
    k.fee = "12 sat/vB";
    k.diff = "146.72T";
    k.net = "912 EH/s";
    k.pct = 47;
    k.remaining = "111,655";
    k.strip = m.strip;
    k.strip.l[3] = "SHARES";
    k.strip.v[3] = "1,284";
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
    c.date = fmtDate("30/09/2026");
    c.price = "$97,412";
    c.block = "912,345";
    c.hash = "672.4";
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

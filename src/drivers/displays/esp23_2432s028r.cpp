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
#include "mining.h"

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
  { "Instrument Amber", RGB565(8, 10, 14), RGB565(16, 20, 28), RGB565(34, 42, 54), RGB565(24, 30, 40), RGB565(245, 247, 250), RGB565(135, 145, 160), RGB565(247, 147, 26), RGB565(52, 211, 153), RGB565(251, 191, 36), RGB565(248, 113, 113), RGB565(245, 158, 11) },
  // 1: Matrix Phosphor Green - Retro CRT terminal
  { "Matrix Green", RGB565(4, 8, 4), RGB565(8, 20, 10), RGB565(18, 52, 24), RGB565(12, 32, 16), RGB565(220, 252, 231), RGB565(74, 222, 128), RGB565(34, 197, 94), RGB565(74, 222, 128), RGB565(250, 204, 21), RGB565(248, 113, 113), RGB565(245, 158, 11) },
  // 2: Cyber Amber (1980s CRT) - Warm amber monochrome terminal
  { "Cyber Amber", RGB565(10, 8, 4), RGB565(24, 18, 8), RGB565(56, 40, 16), RGB565(36, 26, 12), RGB565(254, 243, 199), RGB565(217, 119, 6), RGB565(245, 158, 11), RGB565(52, 211, 153), RGB565(251, 191, 36), RGB565(248, 113, 113), RGB565(245, 158, 11) },
  // 3: Arcade Neon - Playful classic mod neon
  { "Arcade Neon", RGB565(10, 8, 18), RGB565(22, 16, 38), RGB565(54, 38, 86), RGB565(32, 24, 54), RGB565(245, 243, 255), RGB565(192, 132, 252), RGB565(6, 182, 212), RGB565(52, 211, 153), RGB565(251, 191, 36), RGB565(248, 113, 113), RGB565(236, 72, 153) },
  // 4: Ice Blue (Clean Slate) - Cool slate & ice blue tech
  { "Ice Blue", RGB565(10, 14, 22), RGB565(17, 24, 39), RGB565(35, 48, 68), RGB565(24, 34, 50), RGB565(240, 249, 255), RGB565(125, 211, 252), RGB565(56, 189, 248), RGB565(52, 211, 153), RGB565(251, 191, 36), RGB565(248, 113, 113), RGB565(245, 158, 11) },
  // 5: Minimalist Monochrome - E-ink stark white on black
  { "Monochrome", RGB565(0, 0, 0), RGB565(18, 18, 18), RGB565(48, 48, 48), RGB565(28, 28, 28), RGB565(255, 255, 255), RGB565(160, 160, 160), RGB565(255, 255, 255), RGB565(255, 255, 255), RGB565(200, 200, 200), RGB565(255, 255, 255), RGB565(255, 255, 255) },
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
  W_HH,
  W_COLON,
  W_MM,
  W_DATE,
  W_C0, // 3 clock tiles
  W_C1,
  W_C2,
  W_CHART,
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

static String fmtUptimeWords(const String &s)
{
  int d = 0, h = 0, m = 0, sec = 0;
  sscanf(s.c_str(), "%d %d:%d:%d", &d, &h, &m, &sec);
  char b[24];
  if (d > 0)
    snprintf(b, sizeof(b), "%dd %dh", d, h);
  else if (h > 0)
    snprintf(b, sizeof(b), "%dh %02dm", h, m);
  else
    snprintf(b, sizeof(b), "%dm %02ds", m, sec);
  return String(b);
}

static String fmtHps(float kh)
{
  if (kh <= 0)
    return "0";
  unsigned long hps = (unsigned long)(kh * 1000.0f + 0.5f);
  return commas(String(hps));
}

static String fmtTotalHashes(const String &mhashesStr)
{
  double mh = mhashesStr.toDouble();
  char b[32];
  if (mh >= 1000000.0)
    snprintf(b, sizeof(b), "%.1f trillion", mh / 1000000.0);
  else if (mh >= 1000.0)
    snprintf(b, sizeof(b), "%.1f billion", mh / 1000.0);
  else if (mh > 0.0)
    snprintf(b, sizeof(b), "%.1f million", mh);
  else
    return "<1 million";
  return String(b);
}

static String fmtNetDiff(double diff)
{
  if (diff <= 0.0)
    return "~132.7T";
  char b[32];
  if (diff >= 1e12)
    snprintf(b, sizeof(b), "%.2fT", diff / 1e12);
  else if (diff >= 1e9)
    snprintf(b, sizeof(b), "%.2fB", diff / 1e9);
  else if (diff >= 1e6)
    snprintf(b, sizeof(b), "%.2fM", diff / 1e6);
  else
    snprintf(b, sizeof(b), "%.0f", diff);
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
    uint16_t col = lvl == 0 ? C_TRACK : (lvl == 4 ? C_METER : mix565(C_TRACK, C_METER, lvl * 64));
    int a0 = 45 + i * 9; // 270 degree sweep, gap at 6 o'clock
    d.drawArc(G_CX, G_CY, G_R, G_RI, a0, a0 + 7, col, C_BG, true);
  }
}

// ------------------------------------------------------------------ header --

struct HdrV
{
  String time;
  String ip;
  int bars;
  int status;
};

static void drawHeader(const HdrV &h)
{
  char k[64];
  snprintf(k, sizeof(k), "%s|%s|%d|%d|%d", h.time.c_str(), h.ip.c_str(), h.bars, h.status, isPortrait() ? 1 : 0);
  if (!need(W_HDR, k))
    return;
  const int X0 = 10, W = (isPortrait() ? 240 : 320) - X0;
  if (!wBegin(W, HDR_H, C_BG))
    return;

  // Wordmark: unified, crisp typography
  text(wgt, "NERDMINER", 0, 5, 13, C_TEXT, C_BG);
  int wm = textW("NERDMINER", 13);

  // Status: purely typographic indicator with a minimal 3px status dot (no pill container)
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
  int sx = wm + (isPortrait() ? 10 : 14);
  wgt.fillCircle(sx + 2, 12, 3, col);
  text(wgt, label, sx + 9, 6, 9, col, C_BG);

  // Time (right-aligned)
  text(wgt, h.time.c_str(), W - 4, 5, 13, C_TEXT, C_BG, Align::TopRight);

  int timeW = textW(h.time.c_str(), 13);
  int barX = (W - 4) - timeW - 18;

  // Signal indicator (neutral, functional)
  for (int i = 0; i < 4; i++)
  {
    int bh = 3 + i * 3;
    wgt.fillRect(barX + i * 4, 18 - bh, 2, bh, i < h.bars ? C_TEXT : C_TRACK);
  }

  // Local IP (landscape only, quiet secondary information)
  if (!isPortrait() && h.ip.length())
  {
    int right = barX - 12;
    text(wgt, h.ip.c_str(), right, 6, 10, C_DIM, C_BG, Align::TopRight);
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
  int cw = isPortrait() ? 54 : 70;
  int ch = isPortrait() ? 42 : 32;
  int xstep = isPortrait() ? 57 : 74;
  int x0 = isPortrait() ? 8 : 14;
  for (int i = 0; i < 4; i++)
  {
    String k = s.l[i] + "|" + s.v[i] + "|" + accentIdx + "|" + (isPortrait() ? "P" : "L");
    if (!need(W_P0 + i, k))
      continue;
    if (!wBegin(cw, ch, C_PANEL))
      continue;
    text(wgt, s.l[i].c_str(), isPortrait() ? 2 : 6, 3, isPortrait() ? 8 : 9, C_DIM, C_PANEL);
    text(wgt, s.v[i].c_str(), isPortrait() ? 2 : 6, isPortrait() ? 18 : 15, isPortrait() ? 12 : 14, C_TEXT, C_PANEL);
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
  text(wgt, label, 8, top, 9, C_DIM, C_PANEL);
  text(wgt, value, 8, top + 11, strlen(value) > 8 ? 12 : 15, valCol, C_PANEL);
  wEnd(x, y);
  done(id, k);
}

// ============================================================ MINE screen ==

struct MineV
{
  HdrV hdr;
  String hash;
  String hps;
  String shares;
  String best;
  String netDiff;
  String uptime;
  String totalHashes;
  String blocks;
  float kh;
  bool flash;
};

static void drawDownArrow(TFT_eSPI &d, int x, int y, uint16_t col)
{
  d.drawFastVLine(x, y, 4, col);
  d.fillTriangle(x - 2, y + 4, x + 2, y + 4, x, y + 7, col);
}

static void chromeMine(TFT_eSPI &d)
{
  if (isPortrait())
  {
    // Card 1: The Mining Pipeline (Click Miner causal flow) (Y = 28, H = 88)
    panel(d, 6, 28, 228, 88, 0, C_PANEL, C_EDGE, C_BG);
    text(d, "THE MINING PIPELINE: HASH < TARGET", 12, 32, 9, C_METER, C_PANEL);
    text(d, "Block candidate -> 80B header (+1 nonce) -> dSHA256", 12, 44, 8, C_TEXT, C_PANEL);

    // Target visual stream box
    d.fillRect(10, 56, 220, 42, C_BG);
    d.drawRect(10, 56, 220, 42, C_EDGE);
    text(d, "n.381 -> 9F28...", 14, 58, 8, C_DIM, C_BG);
    text(d, "> target", 120, 58, 8, C_DIM, C_BG);
    text(d, "X", 224, 58, 8, C_BAD, C_BG, Align::TopRight);

    text(d, "n.382 -> 037A...", 14, 68, 8, C_DIM, C_BG);
    text(d, "> target", 120, 68, 8, C_DIM, C_BG);
    text(d, "X", 224, 68, 8, C_BAD, C_BG, Align::TopRight);

    d.drawFastHLine(14, 78, 212, C_EDGE);
    text(d, "TARGET (Numerically smaller)", 120, 74, 8, C_WARN, C_BG, Align::TopCenter);

    text(d, "winner-> 0001...", 14, 85, 8, C_GOOD, C_BG);
    text(d, "< target", 120, 85, 8, C_GOOD, C_BG);
    text(d, "OK", 224, 85, 8, C_GOOD, C_BG, Align::TopRight);

    text(d, "Rule: Numerically < target (why winners start with 0s).", 12, 101, 8, C_DIM, C_PANEL);

    // Card 2 Frame (Speed & Work, Y = 118, H = 48)
    panel(d, 6, 118, 228, 48, 0, C_PANEL, C_EDGE, C_BG);

    // Card 3 Frame (Shares, Y = 168, H = 44)
    panel(d, 6, 168, 228, 44, 0, C_PANEL, C_EDGE, C_BG);

    // Card 4 Frame (Best Try vs Target, Y = 214, H = 46)
    panel(d, 6, 214, 228, 46, 0, C_PANEL, C_EDGE, C_BG);

    // Card 5 Frame (Blockchain & Prize Banner, Y = 262, H = 54)
    panel(d, 6, 262, 228, 54, 0, C_PANEL, C_EDGE, C_BG);
    text(d, "[B100] -> [B101] -> [NEXT]", 120, 265, 8, C_METER, C_PANEL, Align::TopCenter);
    text(d, "Prev hash links chain. Old change breaks all.", 12, 276, 8, C_DIM, C_PANEL);
    text(d, "LOTTERY: Next hash could win--or never!", 12, 287, 8, C_TEXT, C_PANEL);
    text(d, "Solve block = 3.125 BTC reward + tx fees!", 12, 298, 8, C_GOLD, C_PANEL);
  }
  else
  {
    // Landscape (320x240)
    // Left Column: The Causal Mining Pipeline (X = 6, Y = 28, W = 150, H = 176)
    panel(d, 6, 28, 150, 176, 0, C_PANEL, C_EDGE, C_BG);
    text(d, "THE MINING PIPELINE", 12, 32, 9, C_METER, C_PANEL);

    // Stage 1
    text(d, "1. CANDIDATE BLOCK", 12, 43, 8, C_TEXT, C_PANEL);
    text(d, "Txs + prev block hash", 12, 53, 8, C_DIM, C_PANEL);
    drawDownArrow(d, 81, 62, C_METER);

    // Stage 2
    text(d, "2. 80-BYTE HEADER", 12, 70, 8, C_TEXT, C_PANEL);
    text(d, "Tweaks changing nonce (+1)", 12, 80, 8, C_DIM, C_PANEL);
    drawDownArrow(d, 81, 89, C_METER);

    // Stage 3
    text(d, "3. DOUBLE SHA-256", 12, 97, 8, C_TEXT, C_PANEL);
    text(d, "Computes 256-bit hash", 12, 107, 8, C_DIM, C_PANEL);
    drawDownArrow(d, 81, 116, C_METER);

    // Stage 4: Attempt stream vs target box
    d.fillRect(10, 125, 142, 38, C_BG);
    d.drawRect(10, 125, 142, 38, C_EDGE);

    text(d, "n.381 -> 9F28...", 14, 127, 8, C_DIM, C_BG);
    text(d, ">tgt", 98, 127, 8, C_DIM, C_BG);
    text(d, "X", 146, 127, 8, C_BAD, C_BG, Align::TopRight);

    d.drawFastHLine(14, 137, 134, C_EDGE);
    text(d, "TARGET LINE", 81, 134, 8, C_WARN, C_BG, Align::TopCenter);

    text(d, "winner-> 0001...", 14, 142, 8, C_GOOD, C_BG);
    text(d, "<tgt", 98, 142, 8, C_GOOD, C_BG);
    text(d, "OK", 148, 142, 8, C_GOOD, C_BG, Align::TopRight);

    // Rule explanation
    text(d, "Rule: Numerically < target", 81, 166, 8, C_TEXT, C_PANEL, Align::TopCenter);
    text(d, "(Tiny numbers start with 0s)", 81, 177, 8, C_DIM, C_PANEL, Align::TopCenter);

    // Right Column Panel Frame (X = 162, Y = 28, W = 152, H = 176)
    panel(d, 162, 28, 152, 176, 0, C_PANEL, C_EDGE, C_BG);
    d.drawFastHLine(166, 90, 144, C_EDGE);
    d.drawFastHLine(166, 138, 144, C_EDGE);

    // Bottom Banner Frame (X = 6, Y = 208, W = 308, H = 28)
    panel(d, 6, 208, 308, 28, 0, C_PANEL, C_EDGE, C_BG);
  }
}

static void paintMine(const MineV &m)
{
  drawHeader(m.hdr);

  uint16_t sharesCol = m.flash ? C_GOOD : C_TEXT;

  if (isPortrait())
  {
    // Sub-card 1: Speed & Work (X = 8, Y = 114, W = 224, H = 44)
    String k1 = m.hps + "|" + m.hash + "|" + m.uptime + "|" + m.totalHashes + "|" + accentIdx + "|P";
    if (need(W_T0, k1) && wBegin(224, 44, C_PANEL))
    {
      text(wgt, "SPEED & TOTAL WORK", 4, 2, 9, C_METER, C_PANEL);
      text(wgt, "Trying ~", 4, 14, 8, C_DIM, C_PANEL);
      int w1 = textW("Trying ~", 8);
      text(wgt, m.hps.c_str(), 4 + w1, 14, 8, C_METER, C_PANEL);
      int w2 = textW(m.hps.c_str(), 8);
      text(wgt, " /s (hashrate: ", 4 + w1 + w2, 14, 8, C_DIM, C_PANEL);
      int w3 = textW(" /s (hashrate: ", 8);
      text(wgt, m.hash.c_str(), 4 + w1 + w2 + w3, 14, 8, C_TEXT, C_PANEL);
      int w4 = textW(m.hash.c_str(), 8);
      text(wgt, " KH/s)", 4 + w1 + w2 + w3 + w4, 14, 8, C_DIM, C_PANEL);
      text(wgt, "(KH/s = thousands of guesses per second)", 4, 24, 8, C_DIM, C_PANEL);
      text(wgt, "In ", 4, 34, 8, C_DIM, C_PANEL);
      int w5 = textW("In ", 8);
      text(wgt, m.uptime.c_str(), 4 + w5, 34, 8, C_TEXT, C_PANEL);
      int w6 = textW(m.uptime.c_str(), 8);
      text(wgt, ": tried ~", 4 + w5 + w6, 34, 8, C_DIM, C_PANEL);
      int w7 = textW(": tried ~", 8);
      text(wgt, m.totalHashes.c_str(), 4 + w5 + w6 + w7, 34, 8, C_METER, C_PANEL);
      int w8 = textW(m.totalHashes.c_str(), 8);
      text(wgt, " hashes", 4 + w5 + w6 + w7 + w8, 34, 8, C_DIM, C_PANEL);
      wEnd(8, 114);
      done(W_T0, k1);
    }

    // Sub-card 2: Shares (X = 8, Y = 164, W = 224, H = 40)
    String k2 = m.shares + "|" + (m.flash ? "1" : "0") + "|" + accentIdx + "|P";
    if (need(W_T1, k2) && wBegin(224, 40, C_PANEL))
    {
      text(wgt, m.shares.c_str(), 4, 2, 9, sharesCol, C_PANEL);
      int ws1 = textW(m.shares.c_str(), 9);
      text(wgt, " POOL SHARES ACCEPTED", 4 + ws1 + 4, 2, 9, sharesCol, C_PANEL);
      text(wgt, "Passed pool's easier test proving CYD does work.", 4, 14, 8, C_TEXT, C_PANEL);
      text(wgt, "They are NOT pieces of a Bitcoin!", 4, 24, 8, C_WARN, C_PANEL);
      wEnd(8, 164);
      done(W_T1, k2);
    }

    // Sub-card 3: Best Try vs Target (X = 8, Y = 210, W = 224, H = 44)
    String k3 = m.best + "|" + m.netDiff + "|" + accentIdx + "|P";
    if (need(W_T2, k3) && wBegin(224, 44, C_PANEL))
    {
      text(wgt, "BEST TRY vs NETWORK TARGET", 4, 2, 9, C_METER, C_PANEL);
      text(wgt, "Best: diff ", 4, 14, 8, C_DIM, C_PANEL);
      int wb1 = textW("Best: diff ", 8);
      text(wgt, m.best.c_str(), 4 + wb1, 14, 8, C_METER, C_PANEL);
      int wb2 = textW(m.best.c_str(), 8);
      text(wgt, " (rarest attempt; higher is rarer)", 4 + wb1 + wb2, 14, 8, C_DIM, C_PANEL);
      text(wgt, "Real Bitcoin target: ", 4, 24, 8, C_DIM, C_PANEL);
      int wb3 = textW("Real Bitcoin target: ", 8);
      text(wgt, m.netDiff.c_str(), 4 + wb3, 24, 8, C_TEXT, C_PANEL);
      int wb4 = textW(m.netDiff.c_str(), 8);
      text(wgt, " (independent odds)", 4 + wb3 + wb4, 24, 8, C_DIM, C_PANEL);
      text(wgt, "Each hash is fresh random chance (not 'closer').", 4, 34, 8, C_DIM, C_PANEL);
      wEnd(8, 210);
      done(W_T2, k3);
    }
  }
  else
  {
    // Landscape (320x240)
    // Sub-card 1: Speed & Work (X = 164, Y = 30, W = 148, H = 58)
    String k1 = m.hps + "|" + m.hash + "|" + m.uptime + "|" + m.totalHashes + "|" + accentIdx + "|L";
    if (need(W_T0, k1) && wBegin(148, 58, C_PANEL))
    {
      text(wgt, "SPEED & TOTAL WORK", 4, 2, 9, C_METER, C_PANEL);
      text(wgt, "Trying ~", 4, 14, 8, C_DIM, C_PANEL);
      int w1 = textW("Trying ~", 8);
      text(wgt, m.hps.c_str(), 4 + w1, 14, 8, C_METER, C_PANEL);
      int w2 = textW(m.hps.c_str(), 8);
      text(wgt, " /s", 4 + w1 + w2, 14, 8, C_DIM, C_PANEL);

      text(wgt, "Speed = hashrate: ", 4, 25, 8, C_DIM, C_PANEL);
      int w3 = textW("Speed = hashrate: ", 8);
      text(wgt, m.hash.c_str(), 4 + w3, 25, 8, C_TEXT, C_PANEL);
      int w4 = textW(m.hash.c_str(), 8);
      text(wgt, " KH/s", 4 + w3 + w4, 25, 8, C_DIM, C_PANEL);

      text(wgt, "(KH/s = 1,000s guesses/sec)", 4, 36, 8, C_DIM, C_PANEL);

      text(wgt, "In ", 4, 47, 8, C_DIM, C_PANEL);
      int w5 = textW("In ", 8);
      text(wgt, m.uptime.c_str(), 4 + w5, 47, 8, C_TEXT, C_PANEL);
      int w6 = textW(m.uptime.c_str(), 8);
      text(wgt, ": ~", 4 + w5 + w6, 47, 8, C_DIM, C_PANEL);
      int w7 = textW(": ~", 8);
      text(wgt, m.totalHashes.c_str(), 4 + w5 + w6 + w7, 47, 8, C_METER, C_PANEL);

      wEnd(164, 30);
      done(W_T0, k1);
    }

    // Sub-card 2: Shares (X = 164, Y = 92, W = 148, H = 44)
    String k2 = m.shares + "|" + (m.flash ? "1" : "0") + "|" + accentIdx + "|L";
    if (need(W_T1, k2) && wBegin(148, 44, C_PANEL))
    {
      text(wgt, m.shares.c_str(), 4, 2, 9, sharesCol, C_PANEL);
      int ws1 = textW(m.shares.c_str(), 9);
      text(wgt, " POOL SHARES", 4 + ws1 + 4, 2, 9, sharesCol, C_PANEL);

      text(wgt, "Passed pool's easier test", 4, 14, 8, C_TEXT, C_PANEL);
      text(wgt, "proving CYD does work.", 4, 24, 8, C_DIM, C_PANEL);
      text(wgt, "NOT pieces of a Bitcoin!", 4, 34, 8, C_WARN, C_PANEL);

      wEnd(164, 92);
      done(W_T1, k2);
    }

    // Sub-card 3: Best Try vs Target (X = 164, Y = 140, W = 148, H = 58)
    String k3 = m.best + "|" + m.netDiff + "|" + accentIdx + "|L";
    if (need(W_T2, k3) && wBegin(148, 58, C_PANEL))
    {
      text(wgt, "BEST TRY vs NETWORK", 4, 2, 9, C_METER, C_PANEL);

      text(wgt, "Best: diff ", 4, 14, 8, C_DIM, C_PANEL);
      int wb1 = textW("Best: diff ", 8);
      text(wgt, m.best.c_str(), 4 + wb1, 14, 8, C_METER, C_PANEL);
      int wb2 = textW(m.best.c_str(), 8);
      text(wgt, " (rarest)", 4 + wb1 + wb2, 14, 8, C_DIM, C_PANEL);

      text(wgt, "Target needs: ", 4, 25, 8, C_DIM, C_PANEL);
      int wb3 = textW("Target needs: ", 8);
      text(wgt, m.netDiff.c_str(), 4 + wb3, 25, 8, C_TEXT, C_PANEL);

      text(wgt, "Each try is independent.", 4, 36, 8, C_DIM, C_PANEL);
      text(wgt, "Higher is rarer, not 'closer'.", 4, 47, 8, C_DIM, C_PANEL);

      wEnd(164, 140);
      done(W_T2, k3);
    }

    // Bottom Banner (X = 8, Y = 210, W = 304, H = 24)
    String k4 = m.blocks + "|" + accentIdx + "|L";
    if (need(W_T3, k4) && wBegin(304, 24, C_PANEL))
    {
      if (m.blocks.toInt() > 0)
      {
        text(wgt, "*** BLOCK SOLVED! 3.125 BTC WON! ***", 152, 2, 9, C_GOLD, C_PANEL, Align::TopCenter);
        text(wgt, "Valid Bitcoin block solved and submitted to the network!", 152, 14, 8, C_GOOD, C_PANEL, Align::TopCenter);
      }
      else
      {
        text(wgt, "LOTTERY: Every try is independent. Next hash could win--or never.", 4, 2, 8, C_TEXT, C_PANEL);
        text(wgt, "Solve block = 3.125 BTC (~$200k+) reward + tx fees to your wallet!", 4, 13, 8, C_GOLD, C_PANEL);
      }
      wEnd(8, 210);
      done(W_T3, k4);
    }
  }
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
    panel(d, 6, 32, 228, 68, 8, C_PANEL, C_EDGE, C_BG);
    text(d, btcPairLabel().c_str(), 14, 38, 9, C_DIM, C_PANEL);
    text(d, "LATEST BLOCK", 224, 38, 9, C_DIM, C_PANEL, Align::TopRight);
    panel(d, 6, 196, 228, 36, 8, C_PANEL, C_EDGE, C_BG);
    chromeStripCells(d);
  }
  else
  {
    panel(d, 8, 32, 304, 70, 10, C_PANEL, C_EDGE, C_BG);
    text(d, btcPairLabel().c_str(), 20, 39, 9, C_DIM, C_PANEL);
    text(d, "LATEST BLOCK", 300, 39, 9, C_DIM, C_PANEL, Align::TopRight);
    panel(d, 8, 156, 304, 36, 8, C_PANEL, C_EDGE, C_BG);
    chromeStripCells(d);
  }
}

static void paintMarket(const MarketV &m)
{
  drawHeader(m.hdr);

  String k = m.price + "|" + accentIdx + "|" + (isPortrait() ? "P" : "L");
  int priceW = isPortrait() ? 130 : 176;
  if (need(W_PRICE, k) && wBegin(priceW, 46, C_PANEL))
  {
    const unsigned len = m.price.length();
    int baseSz = isPortrait() ? 32 : 38;
    text(wgt, m.price.c_str(), 2, 0, len > 8 ? baseSz * 8 / len : baseSz, C_TEXT, C_PANEL);
    wEnd(isPortrait() ? 12 : 18, 52);
    done(W_PRICE, k);
  }

  k = m.block + "|" + accentIdx + "|" + (isPortrait() ? "P" : "L");
  int blockW = isPortrait() ? 84 : 110;
  if (need(W_BLOCK, k) && wBegin(blockW, 46, C_PANEL))
  {
    text(wgt, m.block.c_str(), blockW - 4, isPortrait() ? 10 : 17, isPortrait() ? 18 : 22, C_TEXT, C_PANEL, Align::TopRight);
    wEnd(isPortrait() ? 144 : 190, 52);
    done(W_BLOCK, k);
  }

  if (isPortrait())
  {
    drawTile(W_M0, 6, 106, 112, 40, "FEE / HALF HOUR", m.fee.c_str(), C_TEXT, C_EDGE);
    drawTile(W_M1, 122, 106, 112, 40, "DIFFICULTY", m.diff.c_str(), C_TEXT, C_EDGE);
    drawTile(W_M2, 6, 150, 228, 40, "NETWORK RATE", m.net.c_str(), C_TEXT, C_EDGE);
  }
  else
  {
    const int y = 108, w = 98, h = 42;
    drawTile(W_M0, 8, y, w, h, "FEE / HALF HOUR", m.fee.c_str(), C_TEXT, C_EDGE);
    drawTile(W_M1, 111, y, w, h, "DIFFICULTY", m.diff.c_str(), C_TEXT, C_EDGE);
    drawTile(W_M2, 214, y, w, h, "NETWORK RATE", m.net.c_str(), C_TEXT, C_EDGE);
  }

  char kb[48];
  snprintf(kb, sizeof(kb), "%d|%s|%d|%d", m.pct, m.remaining.c_str(), accentIdx, isPortrait() ? 1 : 0);
  int halvW = isPortrait() ? 216 : 288;
  int halvY = isPortrait() ? 199 : 159;
  int barW = isPortrait() ? 208 : 280;
  if (need(W_HALV, kb) && wBegin(halvW, 30, C_PANEL))
  {
    text(wgt, "NEXT HALVING", 4, 1, 9, C_DIM, C_PANEL);
    char pc[8];
    snprintf(pc, sizeof(pc), "%d%%", m.pct);
    text(wgt, pc, 4 + textW("NEXT HALVING", 9) + 8, 0, 11, C_TEXT, C_PANEL);
    String rem = commas(m.remaining) + (isPortrait() ? " blk" : " blocks left");
    text(wgt, rem.c_str(), halvW - 4, 1, 10, C_TEXT, C_PANEL, Align::TopRight);
    wgt.fillRect(4, 17, barW, 6, C_TRACK);
    int fw = (barW * m.pct) / 100;
    if (fw > 0)
      wgt.fillRect(4, 17, fw, 6, C_METER);
    wEnd(isPortrait() ? 12 : 16, halvY);
    done(W_HALV, kb);
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
  chromeStrip(d);
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
  wEnd(isPortrait() ? 12 : 16, isPortrait() ? 272 : (STRIP_Y + 4));
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
  default:
    chromeClock(d);
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
static bool haveMine = false, haveMarket = false, haveClock = false;

static void repaintCached(int scr)
{
  if (scr == SCR_MINE && haveMine)
    paintMine(lastMine);
  else if (scr == SCR_MARKET && haveMarket)
    paintMarket(lastMarket);
  else if (scr == SCR_CLOCK && haveClock)
    paintClock(lastClock);
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
  m.hps = fmtHps(kh);
  m.shares = commas(data.completedShares);
  m.best = data.bestDiff;
  m.netDiff = fmtNetDiff(g_networkDifficulty);
  m.uptime = fmtUptimeWords(data.timeMining);
  m.totalHashes = fmtTotalHashes(data.totalMHashes);
  m.blocks = data.valids;
  m.kh = kh;
  m.flash = millis() < shareFlashUntil;
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
  m.strip.l[1] = "POOL RATE";
  m.strip.v[1] = pData.workersHash;
  m.strip.l[2] = "POOL BEST";
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

// --------------------------------------------------------- driver plumbing --

static void touchTask(void *);
void esp32_2432S028R_AlternateRotation(void);

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

  TFT_eTouchBase::Calibation calibation = {233, 3785, 3731, 120, 2};
  touch.setCalibration(calibation);
  xTaskCreatePinnedToCore(touchTask, "cydTouch", 3072, NULL, 1, NULL, 0);

  // Backlight: LEDC channel 0, 5kHz, 8 bit (0-255 duty)
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, Settings.Brightness);

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
  int screen_state_duty = ledcRead(0);
  // Switching the duty cycle for the ledc channel, where the TFT_BL pin is attached.
  if (screen_state_duty > 0)
  {
    ledcWrite(0, 0);
  }
  else
  {
    ledcWrite(0, Settings.Brightness);
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

static void touchTask(void *)
{
  bool wasDown = false;
  int downCount = 0;
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
    m.hps = "672,400";
    m.shares = "1,284";
    m.best = "8.73M";
    m.netDiff = "146.72T";
    m.uptime = "3d 07h";
    m.totalHashes = "58.2 billion";
    m.blocks = "0";
    m.kh = 672.4f;
    m.flash = acc == 1;
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
    k.strip.l[0] = "WORKERS";
    k.strip.v[0] = "3";
    k.strip.l[1] = "POOL RATE";
    k.strip.v[1] = "1.92M";
    k.strip.l[2] = "POOL BEST";
    k.strip.v[2] = "412.6M";
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

CyclicScreenFunction esp32_2432S028RCyclicScreens[] = {esp32_2432S028R_MinerScreen, esp32_2432S028R_MarketScreen, esp32_2432S028R_ClockScreen};

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

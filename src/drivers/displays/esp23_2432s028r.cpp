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
#include <mutex>
#include <SPI.h>
#include "rotation.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"
#include "currency.h"
#include "rgbLed.h"
#include "display.h"
#include "utils.h"

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
extern volatile uint32_t valids;

static inline bool isPortrait()
{
  int r = tft.getRotation();
  return (r == 0 || r == 2);
}

// Mine ring owns most of the upper screen (portrait) / left column (landscape).
static inline int gCX() { return isPortrait() ? 120 : 100; }
static inline int gCY() { return isPortrait() ? 110 : 116; }
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
  W_HDR_BADGE,
  W_HDR_TIME,
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
  W_PROTO_PLOT,
  W_PROTO_FOOT,
  W_COUNT
};
static String wkey[W_COUNT];

// ---------------------------------------------------------- output target --
// Everything draws either to the panel or to an offscreen shot.

class DmaWidgetSprite : public TFT_eSprite {
public:
  DmaWidgetSprite(TFT_eSPI *t) : TFT_eSprite(t) {}

  bool initPersistent(int maxW, int maxH) {
    setColorDepth(16);
    void *p = createSprite(maxW, maxH);
    return (p != nullptr);
  }

  void resize(int w, int h) {
    _iwidth  = _dwidth  = _bitwidth = w;
    _iheight = _dheight = h;
    _sx = 0; _sy = 0; _sw = w; _sh = h;
    cursor_x = 0; cursor_y = 0;
    // Clip to the reshaped buffer; the viewport otherwise keeps the
    // createSprite() size (240 wide / 56 tall) and clips or wraps rows.
    setViewport(0, 0, w, h);
    setPivot(w / 2, h / 2);
  }

  uint16_t* getBuffer() { return (uint16_t*)_img8; }
};

static DmaWidgetSprite wgt(&tft); // persistent DMA-ready scratch sprite
static bool wgtReady = false;
static TFT_eSprite shot = TFT_eSprite(&tft);
static std::mutex g_displayMutex;
static volatile int g_onb = 0;  // onboarding step (OB_*), 0 = off; see "onboarding" below
static int g_lastStatic = 1;    // 1 = loading splash, 2 = Wi-Fi setup screen (repainted after onboarding)
static bool shotOn = false;
static int g_shotYOffset = 0;
static int g_shotTargetScr = 0;
static inline int dY(int y) { return shotOn ? (y - g_shotYOffset) : y; }
static TFT_eSPI &out() { return shotOn ? (TFT_eSPI &)shot : tft; }

static bool need(int id, const String &k) { return shotOn || (wkey[id] != k); }
static void done(int id, const String &k) { if (!shotOn) wkey[id] = k; }
static void resetWidgets()
{
  for (int i = 0; i < W_COUNT; i++)
    wkey[i] = "\x01";
}

static int wgtMaxW = 0, wgtMaxH = 0;
static bool wgtAllocFailedLogged = false;

// Allocate the persistent DMA scratch sprite as early as possible (Init),
// before WiFi/TLS fragment the heap. Mine-ring widgets need up to ~86px tall.
static bool ensureWgtReady()
{
  if (wgtReady)
    return true;

  // The buffer is reshaped per widget (resize), so what matters is pixel
  // AREA, not height. Largest widgets: landscape protocol footer 304x44 =
  // 13376 px, portrait clock/market panels ~228x52 / 124x86 hero. 240x56 =
  // 13440 px (~26KB) covers every widget in all rotations; the old 240x90
  // (~43KB) was more than needed.
  const int wantW = 240;
  int wantH = 56;
  const uint32_t maxBlock = ESP.getMaxAllocHeap();
  if (maxBlock > 1024)
  {
    int fitH = (int)((maxBlock - 512) / (wantW * 2));
    if (fitH > 0 && fitH < wantH)
      wantH = fitH;
  }
  while (wantH >= 16)
  {
    if (wgt.initPersistent(wantW, wantH))
    {
      wgtReady = true;
      wgtMaxW = wantW;
      wgtMaxH = wantH;
      if (wantH < 56)
        Serial.printf("[ui] DMA sprite %dx%d (max block %u)\n", wantW, wantH, maxBlock);
      return true;
    }
    wantH -= 8;
  }
  if (!wgtAllocFailedLogged)
  {
    wgtAllocFailedLogged = true;
    Serial.printf("[ui] persistent DMA sprite alloc failed (free %u, max block %u)\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  return false;
}

static bool wBegin(int w, int h, uint16_t bg)
{
  if (!ensureWgtReady())
    return false;
  // Compare area, not each dimension: a 110x76 panel fits a 240x56 buffer.
  if (w <= 0 || h <= 0 || w * h > wgtMaxW * wgtMaxH)
  {
    static uint32_t lastLogged = 0;
    uint32_t key = ((uint32_t)w << 16) | (uint32_t)h;
    if (key != lastLogged)
    {
      lastLogged = key;
      Serial.printf("[ui] widget %dx%d exceeds sprite %dx%d, skipped\n", w, h, wgtMaxW, wgtMaxH);
    }
    return false;
  }
  wgt.resize(w, h);
  wgt.fillSprite(bg);
  return true;
}

static void wEnd(int x, int y)
{
  if (shotOn)
  {
    wgt.pushToSprite(&shot, x, dY(y));
    return;
  }
  // Blocking push. The SPI DMA path (pushImageDMA + dmaWait) never got its
  // completion on the CYD: the first transfer was queued and the next
  // dmaWait() blocked forever, freezing the Monitor task -- hashrate stuck at
  // 0.00, uptime frozen, screen not updating, stats never saved.
  wgt.pushSprite(x, y);
  g_dmaTransferCount++;
}


// ------------------------------------------------------------ draw helpers --

// fillScreen() is not virtual in TFT_eSPI, so it would ignore sprite targets
static void clearScreen(TFT_eSPI &d)
{
  if (&d == (TFT_eSPI*)&tft) tft.dmaWait();
  d.fillRect(0, 0, d.width(), d.height(), C_BG);
}


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

// structural cell: crisp 1px hairline border, flat surface with rounded corners
static void panel(TFT_eSPI &d, int x, int y, int w, int h, int r, uint16_t fill, uint16_t edge, uint16_t behind)
{
  int py = (&d == (TFT_eSPI*)&wgt) ? y : dY(y);
  d.fillRoundRect(x, py, w, h, r, fill);
  d.drawRoundRect(x, py, w, h, r, edge);
  (void)behind;
}

// horizontal gradient bar (alt -> accent) with softly rounded ends
static void gradBar(TFT_eSPI &d, int x, int y, int w, int h, uint16_t bg)
{
  int gy = dY(y);
  for (int i = 0; i < w; i++)
  {
    int inset = 0;
    int e = i < w - 1 - i ? i : w - 1 - i; // distance to nearest end
    if (h >= 6)
      inset = e < 1 ? 2 : (e < 3 ? 1 : 0);
    uint16_t c = mix565(cAlt, cAcc, (uint8_t)(w > 1 ? i * 255 / (w - 1) : 255));
    d.drawFastVLine(x + i, gy + inset, h - 2 * inset, c);
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

// "233.8 GH" style humanised count of total hashes (input in MH).
static String fmtHashCount(const String &mhashes)
{
  double h = mhashes.toDouble() * 1e6;
  static const char *u[] = {"H", "KH", "MH", "GH", "TH", "PH"};
  int i = 0;
  while (h >= 1000.0 && i < 5)
  {
    h /= 1000.0;
    i++;
  }
  char b[24];
  if (i == 0)
    snprintf(b, sizeof(b), "%.0f H", h);
  else
    snprintf(b, sizeof(b), h >= 100 ? "%.1f %s" : "%.2f %s", h, u[i]);
  return String(b);
}

static String fmtHashCountU64(uint64_t rawHashes)
{
  double h = (double)rawHashes;
  static const char *u[] = {"H", "KH", "MH", "GH", "TH", "PH"};
  int i = 0;
  while (h >= 1000.0 && i < 5)
  {
    h /= 1000.0;
    i++;
  }
  char b[24];
  if (i == 0)
    snprintf(b, sizeof(b), "%.0f H", h);
  else
    snprintf(b, sizeof(b), h >= 100 ? "%.1f %s" : "%.2f %s", h, u[i]);
  return String(b);
}

static String fmtBlockTime(uint32_t sec)
{
  uint32_t m = sec / 60;
  uint32_t s = sec % 60;
  char b[16];
  if (m >= 60)
  {
    uint32_t h = m / 60;
    m = m % 60;
    snprintf(b, sizeof(b), "%uh%02um", h, m);
  }
  else
  {
    snprintf(b, sizeof(b), "%02u:%02u", m, s);
  }
  return String(b);
}

// compact uptime: "2d", "5h", "12m"
static String fmtUptimeShort(const String &s)
{
  int d = 0, h = 0, m = 0, sec = 0;
  if (s.indexOf(':') >= 0)
  {
    sscanf(s.c_str(), "%d %d:%d:%d", &d, &h, &m, &sec);
  }
  else
  {
    unsigned long totalSec = s.toInt();
    d = totalSec / 86400UL;
    h = (totalSec % 86400UL) / 3600UL;
    m = (totalSec % 3600UL) / 60UL;
  }
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

static String fmt12HourTime(const String &t24)
{
  int h = 0, m = 0, s = 0;
  if (sscanf(t24.c_str(), "%d:%d:%d", &h, &m, &s) < 2)
  {
    if (sscanf(t24.c_str(), "%d:%d", &h, &m) < 2)
      return t24.length() ? t24 : String("--");
  }
  const char *period = (h >= 12) ? "PM" : "AM";
  int h12 = h % 12;
  if (h12 == 0) h12 = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d %s", h12, m, period);
  return String(buf);
}

// 12x12 vector trophy icon
static void drawTrophy(TFT_eSPI &d, int x, int y, uint16_t col)
{
  d.fillRect(x + 2, y, 8, 4, col);
  d.fillTriangle(x + 2, y + 4, x + 9, y + 4, x + 5, y + 7, col);
  d.fillTriangle(x + 3, y + 4, x + 8, y + 4, x + 6, y + 7, col);
  d.fillRect(x + 5, y + 7, 2, 3, col);
  d.fillRect(x + 2, y + 10, 8, 2, col);
  d.drawPixel(x + 1, y + 1, col); d.drawPixel(x, y + 2, col); d.drawPixel(x + 1, y + 3, col);
  d.drawPixel(x + 10, y + 1, col); d.drawPixel(x + 11, y + 2, col); d.drawPixel(x + 10, y + 3, col);
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
  if (&d == (TFT_eSPI*)&tft) tft.dmaWait();
  float pos = v * G_SEGS;
  int cy = dY(G_CY);
  for (int i = 0; i < G_SEGS; i++)
  {
    float f = pos - i;
    uint8_t lvl = f >= 1.0f ? 4 : (f <= 0.05f ? 0 : 1 + (int)(f * 3.0f)); // 0, 1..3 (partial), 4
    if (!shotOn && lvl == segLvl[i])
      continue;
    // CarCYD-style zone coloring: last ~20% transitions warm→red (redline)
    uint16_t mCol = (i >= 27) ? C_BAD : (i >= 24 ? C_WARN : C_METER);
    uint16_t tCol = (i >= 27) ? mix565(C_BAD, C_BG, 64) : C_TRACK;
    uint16_t col = lvl == 0 ? tCol : (lvl == 4 ? mCol : mix565(tCol, mCol, lvl * 64));
    int a0 = 45 + i * 9; // 270 degree sweep, gap at 6 o'clock
    d.drawArc(G_CX, cy, G_R, G_RI, a0, a0 + 7, col, C_BG, true);
    if (!shotOn)
      segLvl[i] = lvl;
  }
}

// ------------------------------------------------------------------ header --

struct HdrV
{
  String time;
  String time12;
  String blocks;
  String ip;
  int bars;
  int status;
  int screen; // current screen index (0-3) for nav dots
};

static void drawHeader(const HdrV &h)
{
  const bool P = isPortrait();
  const int bx = P ? 6 : 8;
  const int tx = P ? 166 : 244;
  const int wBadge = 48, hBadge = 22;
  const int wClock = 68, hClock = 20;

  // Top-left trophy badge (x=6/8, y=4, w=48, h=22)
  String kBadge = "hbdg|" + h.blocks + "|" + (P ? "P" : "L");
  if (need(W_HDR_BADGE, kBadge))
  {
    if (wBegin(wBadge, hBadge, C_BG))
    {
      panel(wgt, 0, 0, wBadge, hBadge, 6, C_PANEL, C_EDGE, C_BG);
      uint16_t trophyCol = (h.blocks.toInt() > 0) ? C_GOLD : C_DIM;
      drawTrophy(wgt, 6, 5, trophyCol);
      uint16_t blockNumCol = (h.blocks.toInt() > 0) ? C_GOLD : C_TEXT;
      text(wgt, h.blocks.c_str(), 32, 4, 13, blockNumCol, C_PANEL, Align::TopCenter);
      wEnd(bx, 4);
      done(W_HDR_BADGE, kBadge);
    }
  }

  // Top-right 12-hour clock (x=166/244, y=4, w=68, h=20)
  String kClock = "hclk|" + h.time12 + "|" + (P ? "P" : "L");
  if (need(W_HDR_TIME, kClock))
  {
    if (wBegin(wClock, hClock, C_BG))
    {
      text(wgt, h.time12.c_str(), 66, 3, 13, C_TEXT, C_BG, Align::TopRight);
      wEnd(tx, 4);
      done(W_HDR_TIME, kClock);
    }
  }
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
  String hash, shares, best, tmpl, blocks, total, blockHashes, time12;
  float kh;
  bool flash;
  StripV strip; // 3 cells: v = value, l = label
};

// Mine layout (mock): big ring + hero, two equal cards, 3-cell footer strip.
//   Portrait : ring cy=128 r=98, cards y=222..264, strip y=270..314
//   Landscape: ring left cx=100 cy=116 r=86, cards right column, strip y=198
#define MINE_CARD_PY 190
#define MINE_CARD_PH 54
#define MINE_STRIP_PY 252
#define MINE_STRIP_PH 60

static void mineStripGeom(int &x, int &y, int &w, int &h)
{
  if (isPortrait()) { x = 6; y = MINE_STRIP_PY; w = 228; h = MINE_STRIP_PH; }
  else { x = 8; y = STRIP_Y; w = 304; h = STRIP_H; }
}

static void chromeMine(TFT_eSPI &d)
{
  if (!shotOn)
    memset(segLvl, 0xFF, sizeof(segLvl));
  gaugeDraw(d, gaugeShown);
  if (isPortrait())
  {
    // 2x2 Bento Grid panels (6px corner radius, matching other cards)
    panel(d, 6, 172, 112, 42, 6, C_PANEL, C_EDGE, C_BG);
    panel(d, 122, 172, 112, 42, 6, C_PANEL, C_EDGE, C_BG);
    panel(d, 6, 218, 112, 42, 6, C_PANEL, C_EDGE, C_BG);
    panel(d, 122, 218, 112, 42, 6, C_PANEL, C_EDGE, C_BG);
    // Bottom 3-cell strip
    panel(d, 6, 264, 228, 50, 8, C_PANEL, C_EDGE, C_BG);
    int vy = dY(264);
    for (int i = 1; i < 3; i++)
      d.drawFastVLine(6 + (228 * i) / 3, vy + 8, 34, C_EDGE);
  }
  else
  {
    panel(d, 198, 30, 114, 76, 8, C_PANEL, C_EDGE, C_BG);
    panel(d, 198, 112, 114, 76, 8, C_PANEL, C_EDGE, C_BG);
    panel(d, 8, STRIP_Y, 304, STRIP_H, 8, C_PANEL, C_EDGE, C_BG);
    int vy = dY(STRIP_Y);
    for (int i = 1; i < 3; i++)
      d.drawFastVLine(8 + (304 * i) / 3, vy + 8, STRIP_H - 16, C_EDGE);
  }
}

// Crisp, iconic lightning bolt glyph (12x18)
static void drawBolt(TFT_eSPI &d, int x, int y, uint16_t col)
{
  int p0x = x + 7,  p0y = y + 0;
  int p1x = x + 1,  p1y = y + 9;
  int p2x = x + 6,  p2y = y + 9;
  int p3x = x + 5,  p3y = y + 17;
  int p4x = x + 11, p4y = y + 7;
  int p5x = x + 6,  p5y = y + 7;

  d.fillTriangle(p0x, p0y, p1x, p1y, p5x, p5y, col);
  d.fillTriangle(p1x, p1y, p2x, p2y, p5x, p5y, col);
  d.fillTriangle(p2x, p2y, p5x, p5y, p4x, p4y, col);
  d.fillTriangle(p2x, p2y, p4x, p4y, p3x, p3y, col);
}

static void drawMineCard(int id, int x, int y, int w, int h, const char *label, const char *value,
                         uint16_t valCol)
{
  char k[64];
  snprintf(k, sizeof(k), "mc|%s|%s|%u|%d", label, value, valCol, accentIdx);
  if (!need(id, k))
    return;
  if (!wBegin(w, h, C_BG))
    return;
  panel(wgt, 0, 0, w, h, 6, C_PANEL, C_EDGE, C_BG);
  int lw = textW(label, 10);
  int lx = (w - lw) / 2;
  text(wgt, label, lx, 4, 10, C_DIM, C_PANEL);
  unsigned vs = 16;
  if (strlen(value) > 9)
    vs = 13;
  else if (strlen(value) > 7)
    vs = 14;
  text(wgt, value, w / 2, 20, vs, valCol, C_PANEL, Align::TopCenter);
  wEnd(x, y);
  done(id, k);
}

static void drawMineStrip(const StripV &s)
{
  const bool P = isPortrait();
  int x = P ? 6 : 8;
  int y = P ? 264 : STRIP_Y;
  int w = P ? 228 : 304;
  int h = P ? 50 : STRIP_H;
  int cw = w / 3 - 4, ch = h - 6;
  for (int i = 0; i < 3; i++)
  {
    String k = "m3|" + s.l[i] + "|" + s.v[i] + "|" + (P ? "P" : "L");
    if (!need(W_P0 + i, k))
      continue;
    if (!wBegin(cw, ch, C_PANEL))
      continue;
    int c = cw / 2;
    unsigned vs = P ? 16 : 14, ls = 10;
    uint16_t vCol = (i == 2 ? C_WARN : C_TEXT);
    text(wgt, s.v[i].c_str(), c, P ? 7 : 5, vs, vCol, C_PANEL, Align::TopCenter);
    text(wgt, s.l[i].c_str(), c, P ? 27 : 21, ls, C_DIM, C_PANEL, Align::TopCenter);
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

  // hero readout inside the ring: HASHRATE / value / KH/s
  String k = m.hash + "|" + (P ? "P" : "L");
  if (need(W_HERO, k))
  {
    // sprite corners stay inside RI (top) or the 6-o'clock gap (bottom)
    const int hw = P ? 124 : 110, hh = P ? 86 : 76;
    if (wBegin(hw, hh, C_BG))
    {
      int c = hw / 2;
      text(wgt, "HASHRATE", c, 4, P ? 11 : 10, C_DIM, C_BG, Align::TopCenter);
      unsigned sz = P ? (m.hash.length() <= 5 ? 42 : 36) : (m.hash.length() <= 5 ? 36 : 30);
      text(wgt, m.hash.c_str(), c, P ? 18 : 16, sz, C_TEXT, C_BG, Align::TopCenter);
      text(wgt, "KH/s", c, P ? 64 : 52, P ? 14 : 12, C_METER, C_BG, Align::TopCenter);
      wEnd(cx - c, cy - (P ? 38 : 34));
      done(W_HERO, k);
    }
  }

  uint16_t shareCol = m.flash ? C_GOOD : C_TEXT;
  if (P)
  {
    // 2x2 Bento Grid: SHARES, BEST DIFF, THIS BLOCK, SESSION
    drawMineCard(W_T0, 6, 172, 112, 42, "SHARES", m.shares.c_str(), shareCol);
    drawMineCard(W_T1, 122, 172, 112, 42, "BEST DIFF", m.best.c_str(), C_WARN);
    drawMineCard(W_T2, 6, 218, 112, 42, "THIS BLOCK", m.blockHashes.c_str(), C_TEXT);
    drawMineCard(W_T3, 122, 218, 112, 42, "SESSION", m.total.c_str(), C_TEXT);
  }
  else
  {
    drawMineCard(W_T0, 198, 30, 114, 76, "SHARES", m.shares.c_str(), shareCol);
    drawMineCard(W_T1, 198, 112, 114, 76, "BEST DIFF", m.best.c_str(), C_WARN);
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

static void drawRewardRightD(TFT_eSPI &d, int xr, int y, unsigned sz, const String &a, const String &b)
{
  int wa = textW(a.c_str(), sz), wb = textW(b.c_str(), sz);
  const int aw = 14;
  int x0 = xr - (wa + aw + wb);
  text(d, a.c_str(), x0, dY(y), sz, C_DIM, C_PANEL);
  int ax = x0 + wa + 3, ay = dY(y + sz / 2);
  d.drawFastHLine(ax, ay, 7, C_DIM);
  d.fillTriangle(ax + 6, ay - 3, ax + 6, ay + 3, ax + 9, ay, C_DIM);
  text(d, b.c_str(), x0 + wa + aw, dY(y), sz, C_GOLD, C_PANEL);
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
    int px = g.hx + 2, py = g.hy + 2;
    TFT_eSPI &d = out();
    panel(d, px, py, w, h, 8, C_PANEL, C_EDGE, C_BG);
    if (P)
    {
      text(d, btcPairLabel().c_str(), px + 8, dY(py + 4), 10, C_DIM, C_PANEL);
      String pr = (m.price.length() && m.price != "--") ? m.price : String("SYNCING...");
      uint16_t prCol = (m.price.length() && m.price != "--") ? C_GOLD : C_WARN;
      text(d, pr.c_str(), px + w / 2, dY(py + 18), pr.length() > 9 ? 24 : 30, prCol, C_PANEL, Align::TopCenter);
      int lw = textW(bl.c_str(), 10);
      int aw = ag.length() ? textW(ag.c_str(), 10) : 0;
      int tw = lw + (ag.length() ? 14 + aw : 0);
      int x0 = px + (w - tw) / 2;
      text(d, bl.c_str(), x0, dY(py + 54), 10, C_TEXT, C_PANEL);
      if (ag.length())
      {
        d.fillCircle(x0 + lw + 7, dY(py + 60), 1, C_DIM);
        text(d, ag.c_str(), x0 + lw + 14, dY(py + 54), 10, C_DIM, C_PANEL);
      }
    }
    else
    {
      text(d, btcPairLabel().c_str(), px + 6, dY(py + 3), 10, C_DIM, C_PANEL);
      String pr = (m.price.length() && m.price != "--") ? m.price : String("SYNCING...");
      uint16_t prCol = (m.price.length() && m.price != "--") ? C_GOLD : C_WARN;
      text(d, pr.c_str(), px + 6, dY(py + 18), 28, prCol, C_PANEL);
      text(d, bl.c_str(), px + w - 6, dY(py + 8), 14, C_TEXT, C_PANEL, Align::TopRight);
      if (ag.length())
        text(d, ag.c_str(), px + w - 6, dY(py + 30), 10, C_DIM, C_PANEL, Align::TopRight);
    }
    done(W_PRICE, k);
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
    String vStr = (val[i]->length() && *val[i] != "--") ? *val[i] : String("SYNCING");
    uint16_t vCol = (val[i]->length() && *val[i] != "--") ? vc[i] : C_DIM;
    text(wgt, vStr.c_str(), w / 2, P ? 21 : 18, P ? (vStr == "SYNCING" ? 14 : 18) : 17, vCol, C_PANEL, Align::TopCenter);
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
      String fv = m.fee > 0 ? String(m.fee) : String("SYNCING");
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
    TFT_eSPI &d = out();
    int x0 = g.hx + 2;
    int y0 = g.vy + 2;
    int w = g.hw - 4;
    panel(d, g.hx, g.vy, g.hw, g.vh, 8, C_PANEL, C_EDGE, C_BG);

    const CoinProfile &coin = getActiveCoinProfile();
    char pc[8];
    snprintf(pc, sizeof(pc), "%d%%", m.pct);
    int era = m.height > 0 ? (int)(m.height / coin.halvingInterval) : 4;
    float now = coin.initialSubsidy / (float)(1UL << era);
    String r0 = fmtReward(now), r1 = fmtReward(now / 2) + " " + coin.ticker;
    String rem = m.remaining > 0 ? commas(String(m.remaining)) + " blocks left" : String("-- blocks left");
    const int bx = x0 + 6, bw = w - 12;
    if (P)
    {
      text(d, "NEXT HALVING", bx, dY(y0 + 4), 10, C_DIM, C_PANEL);
      text(d, pc, x0 + w - 6, dY(y0 + 2), 16, C_METER, C_PANEL, Align::TopRight);
      int trackY = dY(y0 + 24);
      d.fillRoundRect(bx, trackY, bw, 10, 5, C_TRACK);
      int fw = (bw * m.pct) / 100;
      if (fw > 0) d.fillRoundRect(bx, trackY, fw, 10, 5, C_METER);
      text(d, rem.c_str(), bx, dY(y0 + 42), 11, C_TEXT, C_PANEL);
      if (m.eta.length())
        text(d, m.eta.c_str(), x0 + w - 6, dY(y0 + 42), 11, C_DIM, C_PANEL, Align::TopRight);
      d.drawFastHLine(bx, dY(y0 + 60), bw, C_EDGE);
      text(d, "BLOCK REWARD", bx, dY(y0 + 68), 9, C_DIM, C_PANEL);
      drawRewardRightD(d, x0 + w - 6, y0 + 66, 12, r0, r1);
    }
    else
    {
      text(d, "NEXT HALVING", bx, dY(y0 + 3), 10, C_DIM, C_PANEL);
      text(d, pc, bx + textW("NEXT HALVING", 10) + 8, dY(y0 + 1), 13, C_METER, C_PANEL);
      drawRewardRightD(d, x0 + w - 6, y0 + 2, 11, r0, r1);
      int trackY = dY(y0 + 20);
      d.fillRoundRect(bx, trackY, bw, 8, 4, C_TRACK);
      int fw = (bw * m.pct) / 100;
      if (fw > 0) d.fillRoundRect(bx, trackY, fw, 8, 4, C_METER);
      text(d, rem.c_str(), bx, dY(y0 + 33), 10, C_TEXT, C_PANEL);
      if (m.eta.length())
        text(d, m.eta.c_str(), x0 + w - 6, dY(y0 + 33), 10, C_DIM, C_PANEL, Align::TopRight);
    }
    done(W_HALV, kb);
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
  String period;
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
  const int px = P ? 12 : 16;
  const int py = P ? 186 : 118;
  TFT_eSPI &d = out();
  d.fillRect(px, dY(py), W, H, C_PANEL);
  text(d, "HASHRATE HISTORY", px, dY(py), 9, C_DIM, C_PANEL);
  text(d, "LAST 60 SECONDS", px + W - 1, dY(py), 9, C_DIM, C_PANEL, Align::TopRight);
  const int baseY = dY(py + H - 1);
  d.drawFastHLine(px, baseY, W, C_TRACK);
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
    int x = px + (int)(i * step);
    if (slot < 0 || slot >= HIST_N)
    {
      d.fillRect(x, baseY - 2, bw, 2, C_TRACK);
      continue;
    }
    int bh = (int)(barH * hist[slot] / mx);
    if (bh < 2)
      bh = 2;
    d.fillRect(x, baseY - bh, bw, bh, C_METER);
  }
  done(W_CHART, k);
}

static void paintClock(const ClockV &c)
{
  drawHeader(c.hdr);
  const bool P = isPortrait();

  // Hero: large centered HH:MM + date (portrait); landscape keeps date beside
  {
    String k = c.hh + ":" + c.mm + "|" + c.period + "|" + String(c.colon) + "|" + c.date + (P ? "P" : "L");
    if (need(W_HH, k))
    {
      int pw = P ? 220 : 144, ph = P ? 70 : 62;
      int px = P ? 10 : 12, py = P ? 34 : 34;
      TFT_eSPI &d = out();
      panel(d, px, py, pw, ph, 8, C_PANEL, C_EDGE, C_BG);
      char tim[16];
      snprintf(tim, sizeof(tim), "%s%s%s", c.hh.c_str(), c.colon ? ":" : " ", c.mm.c_str());
      if (P)
      {
        int tw = textW(tim, 42);
        int cx = px + pw / 2 - (c.period.length() ? 12 : 0);
        text(d, tim, cx, dY(py + 4), 42, C_TEXT, C_PANEL, Align::TopCenter);
        if (c.period.length())
          text(d, c.period.c_str(), cx + tw / 2 + 5, dY(py + 10), 12, C_WARN, C_PANEL);
        text(d, c.date.c_str(), px + pw / 2, dY(py + 52), 12, C_DIM, C_PANEL, Align::TopCenter);
      }
      else
      {
        int tw = textW(tim, 36);
        text(d, tim, px + 6, dY(py + 6), 36, C_TEXT, C_PANEL);
        if (c.period.length())
          text(d, c.period.c_str(), px + 6 + tw + 4, dY(py + 10), 11, C_WARN, C_PANEL);
        text(d, c.date.c_str(), px + 6, dY(py + 48), 10, C_DIM, C_PANEL);
      }
      done(W_HH, k);
    }
  }

  // CURRENT HASHRATE — big KH/s only (brief share-accepted flash OK)
  {
    String k = c.hash + "|" + String(c.flash) + (P ? "P" : "L");
    if (need(W_C0, k))
    {
      int pw = P ? 228 : 144, ph = P ? 52 : 70;
      int px = P ? 6 : 168, py = P ? 114 : 30;
      if (wBegin(pw, ph, C_BG))
      {
        wgt.fillRect(0, 0, pw, ph, C_PANEL);
        wgt.drawRect(0, 0, pw, ph, C_EDGE);
        text(wgt, "CURRENT HASHRATE", P ? 12 : 8, P ? 6 : 8, 10, C_DIM, C_PANEL);
        uint16_t vc = c.flash ? C_GOOD : C_TEXT;
        unsigned sz = P ? 26 : 28;
        text(wgt, c.hash.c_str(), P ? 12 : 8, P ? 20 : 26, sz, vc, C_PANEL);
        int nw = textW(c.hash.c_str(), sz);
        text(wgt, "KH/s", (P ? 12 : 8) + nw + 6, P ? 30 : 38, 12, C_METER, C_PANEL);
        wEnd(px, py);
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
      int pw = P ? 216 : 288, ph = 14;
      if (wBegin(pw, ph, C_PANEL))
      {
        text(wgt, "LIVE SAMPLE - HASHING ACTIVE", 0, 0, 10, C_DIM, C_PANEL);
        wEnd(P ? 12 : 16, P ? 244 : 176);
        done(W_C1, k);
      }
    }
  }

  // Bottom strip: TEMP | UPTIME | STATUS (value warns if disconnect/hot)
  {
    const char *labs[3] = {"TEMP", "UPTIME", "STATUS"};
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
  if (need(W_PROTO_PLOT, kbuf))
  {
    TFT_eSPI &d = out();
    panel(d, plotX, plotY, plotW, plotH, 6, C_PANEL, C_EDGE, C_BG);

    const int G = textW("1G", 10) + 8;
    const int sx0 = plotX + G + 10;
    const int sxSpan = plotW - (G + 10) - 14;
    const int yTop = plotY + 16, yBot = plotY + plotH - 16; // leave room for top banner and x caption
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
      for (int x = plotX + G + 2; x < plotX + plotW - 4; x += 4)
        d.drawPixel(x, dY(ty), C_EDGE);
      text(d, tk[t], plotX + G - 4, dY(ty - 5), 10, C_DIM, C_PANEL, Align::TopRight);
    }
    d.drawFastVLine(plotX + G, dY(yTop), yBot - yTop + 1, C_EDGE);
    // Baseline "<1".
    d.drawFastHLine(plotX + G, dY(yBot), plotW - G - 4, C_EDGE);
    text(d, "<1", plotX + G - 4, dY(yBot - 5), 10, C_DIM, C_PANEL, Align::TopRight);

    // Top banner above threshold line
    text(d, "RARER ^", plotX + G + 6, dY(plotY + 3), 10, C_DIM, C_PANEL);
    char tlab[48];
    snprintf(tlab, sizeof(tlab), "TARGET: %s diff", av.netDiff.c_str());
    text(d, tlab, plotX + plotW - 6, dY(plotY + 3), 10, C_WARN, C_PANEL, Align::TopRight);

    // Top: orange BLOCK THRESHOLD at live network difficulty.
    d.drawFastHLine(plotX + G, dY(yTop), plotW - G - 4, C_WARN);
    d.drawFastHLine(plotX + G, dY(yTop + 1), plotW - G - 4, C_WARN);
    text(d, "TGT", plotX + G - 4, dY(yTop - 5), 10, C_WARN, C_PANEL, Align::TopRight);
    text(d, "hash < target -> valid block", plotX + plotW - 6, dY(yTop + 4), 9, C_DIM, C_PANEL, Align::TopRight);

    if (count < 1)
    {
      text(d, "waiting for samples...", plotX + G + (plotW - G) / 2, dY(plotY + plotH / 2 - 6), 10, C_DIM, C_PANEL, Align::TopCenter);
    }
    else
    {
      for (int i = 0; i < count; i++)
      {
        if (bestIsSample && i == bestI) continue;
        int x = sx0 + (i * sxSpan) / (count > 1 ? (count - 1) : 1);
        drawProtoX(d, x, dY(Y(protoDiffQ8FromHashQ8(snap[i]))), C_BAD);
      }
      // Newest: thin outline only.
      int cx = sx0 + sxSpan;
      int cy = Y(protoDiffQ8FromHashQ8(snap[count - 1]));
      d.drawCircle(cx, dY(cy), 5, C_DIM);
    }
    // BEST: the highest plotted point, labeled in place. If it came from a
    // share (not in the 48-sample ring) it is pinned at the left edge.
    if (bestQ8 != INT32_MIN)
    {
      int bx = bestIsSample ? sx0 + (bestI * sxSpan) / (count > 1 ? (count - 1) : 1) : sx0;
      int by = Y(bestQ8);
      d.fillCircle(bx, dY(by), 4, C_METER);
      d.drawCircle(bx, dY(by), 5, C_WARN);
      char blab[32];
      snprintf(blab, sizeof(blab), "* BEST %s", av.bestDiff.c_str());
      int lw = textW(blab, 10);
      bool right = bx + 8 + lw < plotX + plotW - 4;
      int ly = (by > yTop + 30) ? by - 12 : by + 7;
      text(d, blab, right ? bx + 8 : bx - 8, dY(ly), 10, C_METER, C_PANEL,
           right ? Align::TopLeft : Align::TopRight);
    }
    text(d, "recent samples - newest ->", plotX + G + (plotW - G) / 2, dY(plotY + plotH - 13), 10, C_DIM, C_PANEL, Align::TopCenter);
    done(W_PROTO_PLOT, kbuf);
  }

  // Footer: lightning KH/s + attempts/sec + live-sample note
  char fkey[64];
  snprintf(fkey, sizeof(fkey), "%s|%d", av.hashRate.c_str(), P ? 1 : 0);
  if (!need(W_PROTO_FOOT, fkey))
    return;
  if (!wBegin(plotW, footH, C_PANEL))
    return;
  wgt.drawRoundRect(0, 0, plotW, footH, 6, C_EDGE);
  float kh = av.hashRate.toFloat();
  char rateBuf[24];
  if (kh > 0)
    snprintf(rateBuf, sizeof(rateBuf), "%.0f KH/s", kh);
  else
    snprintf(rateBuf, sizeof(rateBuf), "-- KH/s");
  int bx = 10, by = P ? 5 : 7;
  drawBolt(wgt, bx, by, C_METER);
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
  text(wgt, "LIVE SAMPLE - MINING ACTIVE", 28, P ? 40 : 28, 10, C_DIM, C_PANEL);
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
  text(d, CURRENT_VERSION, cx, textY + 38, 14, C_GOLD, C_BG, Align::TopCenter);
  text(d, "INITIALIZING", cx, textY + 58, 11, C_DIM, C_BG, Align::TopCenter);
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
  if (!shotOn && uiScreen == scr)
    return;
  if (!shotOn)
  {
    uiScreen = scr;
    resetWidgets();
  }
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

// Colour polarity. Settings.invertColors is the exact argument handed to
// tft.invertDisplay() -- no hidden XOR with the TFT_INVERSION_ON build flag.
// (TFT_eSPI sends INVON once inside tft.init() when that flag is set; the
// explicit invertDisplay() call right after always overrides it.)
static void applyPanelInvert(bool inv)
{
  invertColors = inv;
  tft.invertDisplay(inv);
  Serial.printf("CYD colors: Settings.invertColors=%d calibrated=%d -> tft.invertDisplay(%d)\n",
                (int)Settings.invertColors, (int)Settings.colorCalibrated, (int)inv);
}

void esp32_2432S028R_SetInvert(bool inv)
{
  Settings.invertColors = inv;
  applyPanelInvert(inv);
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
  String t = (time.length() && time != "--") ? time : getTime();
  h.time12 = fmt12HourTime(t);
  h.blocks = String(valids);
  h.ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  h.bars = wifiBars();
  h.status = uiStatus();
  h.screen = shotOn ? g_shotTargetScr : (currentDisplayDriver ? currentDisplayDriver->current_cyclic_screen : 0);
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
  if (g_onb)
    return; // onboarding owns the panel
  if (restorePage > 0 && restorePage < currentDisplayDriver->num_cyclic_screens)
  {
    // First tick after boot: go straight to the page that was showing before.
    // Must NOT hold g_displayMutex here: the target cyclic screen locks it
    // itself, and std::mutex is non-recursive, so double-locking on this same
    // thread deadlocks and freezes the panel on the INITIALIZING splash while
    // the mining core keeps running.
    int p = restorePage;
    restorePage = 0;
    currentDisplayDriver->current_cyclic_screen = p;
    currentDisplayDriver->cyclic_screens[p](mElapsed);
    return;
  }
  std::lock_guard<std::mutex> lock(g_displayMutex);
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
  m.blocks = data.valids.length() ? data.valids : String("0");
  m.total = fmtHashCount(data.totalMHashes);
  m.blockHashes = fmtHashCountU64(data.blockHashes);
  m.time12 = fmt12HourTime(data.currentTime);
  m.kh = kh;
  m.flash = millis() < shareFlashUntil;
  m.strip.v[0] = fmtBlockTime(data.blockTimeSec);
  m.strip.l[0] = "BLK TIME";
  m.strip.v[1] = fmtUptimeShort(data.timeMining);
  m.strip.l[1] = "UPTIME";
  float tC = data.temp.toFloat();
  float tF = tC * 1.8f + 32.0f;
  m.strip.v[2] = data.temp.length() ? (String((int)roundf(tF)) + " F") : String("-- F");
  m.strip.l[2] = "TEMP";
  lastMine = m;
  haveMine = true;
  paintMine(m);

  logShare(data.completedShares, data.totalKHashes, data.currentHashRate);
}

void esp32_2432S028R_MarketScreen(unsigned long mElapsed)
{
  if (g_onb)
    return; // onboarding owns the panel
  std::lock_guard<std::mutex> lock(g_displayMutex);
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
  m.diff = data.netwrokDifficulty.length() ? data.netwrokDifficulty : (g_networkDifficulty > 0.0 ? formatNetworkDifficulty(g_networkDifficulty, true) : String("--"));
  m.net = data.globalHashRate.length() ? data.globalHashRate : String("--");
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
  if (g_onb)
    return; // onboarding owns the panel
  std::lock_guard<std::mutex> lock(g_displayMutex);
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
  int h = 0, m = 0, s = 0;
  if (sscanf(data.currentTime.c_str(), "%d:%d:%d", &h, &m, &s) >= 2 ||
      sscanf(data.currentTime.c_str(), "%d:%d", &h, &m) >= 2)
  {
    c.period = (h >= 12) ? "PM" : "AM";
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    c.hh = String(h12);
    char mBuf[8];
    snprintf(mBuf, sizeof(mBuf), "%02d", m);
    c.mm = String(mBuf);
  }
  else
  {
    c.hh = "--";
    c.mm = "--";
    c.period = "";
  }
  c.colon = (millis() / 1000) & 1;
  c.date = fmtDate(data.currentDate);
  c.hash = data.currentHashRate;
  c.flash = millis() < shareFlashUntil;
  float tC = data.temp.toFloat();
  float tF = tC * 1.8f + 32.0f;
  c.temp = data.temp.length() ? (String((int)roundf(tF)) + " F") : String("--");
  c.uptime = fmtUptimeShort(data.timeMining);
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
  if (g_onb)
    return; // onboarding owns the panel
  std::lock_guard<std::mutex> lock(g_displayMutex);
  mining_data data = getMiningData(mElapsed);
  uiLive = true;
  ensureChrome(SCR_ARCH);
  float kh = data.currentHashRate.toFloat();
  noteHashrate(kh);

  ArchitectureV av;
  av.hdr = liveHeader(data.currentTime);
  av.netDiffRaw = g_networkDifficulty > 0.0 ? g_networkDifficulty : 1e14;
  av.netDiff = formatNetworkDifficulty(av.netDiffRaw, true);

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

// ------------------------------------------------------------- onboarding --
// First boot (or after "Recalibrate touch" / "Recalibrate colors" on the web
// Display tab) the CYD walks through two short checks on its own screen:
//
//   1. Touch: tap 4 crosshairs, then tap a dot to verify (within 15 px, else
//      redo). The fit is stored as a TFT_eTouch Calibation {x0,x1,y0,y1,
//      rel_rotation} in raw XPT2046 units, so it stays valid when the screen
//      rotation changes; TFT_eTouch applies the rotation at read time.
//   2. Colors: a big Bitcoin-orange square, "Orange" / "Not orange". Not
//      orange flips the panel polarity and asks "orange now?".
//
// Both answers land in /config.json (touchCalibrated + touchCal,
// colorCalibrated + invertColors). It runs on the touch task after boot, so
// mining, Wi-Fi and the web UI keep running while it waits (and
// /api/screenshot shows the prompt). Holding BOOT for 1.5 s skips the
// current step with the defaults, so a dead touch panel can't lock the UI.
// Serial 'o'/'y' and 'x'/'n' answer the colour step, for headless testing.

#if defined(TFT_INVERSION_ON)
#define CYD_PANEL_INVERT_DEFAULT true
#else
#define CYD_PANEL_INVERT_DEFAULT false
#endif

#define PIN_BOOT_BTN 0
#define OB_INSET 20
#define OB_VERIFY_TOL 15

enum OnbState { OB_OFF = 0, OB_TOUCH, OB_VERIFY, OB_COLOR_ASK, OB_COLOR_CONFIRM };
static bool obHaveConfig = false;
static int obTarget = 0;
static int32_t obRawX[4], obRawY[4];
static int obLandX = -1, obLandY = -1, obErr = -1;
static int obColorRound = 0;
static const char *obNote = "";
static const TFT_eTouchBase::Calibation CAL_DEFAULT = {233, 3785, 3731, 120, 2};
static TFT_eTouchBase::Calibation obPrevCal = CAL_DEFAULT;

static void obTargetXY(int i, int &x, int &y)
{
  const int W = tft.width(), H = tft.height();
  x = (i == 1 || i == 2) ? W - 1 - OB_INSET : OB_INSET;
  y = (i >= 2) ? H - 1 - OB_INSET : OB_INSET;
}

static void obColorButtons(int &by, int &bw, int &bh)
{
  const int W = tft.width(), H = tft.height(), pad = 10;
  bh = 56;
  bw = (W - 3 * pad) / 2;
  by = H - bh - pad;
}

static void obButton(TFT_eSPI &d, int x, int y, int w, int h, const char *label)
{
  const uint16_t fill = RGB565(20, 20, 20);
  d.fillRoundRect(x, dY(y), w, h, 8, fill);
  d.drawRoundRect(x, dY(y), w, h, 8, TFT_WHITE);
  d.setTextColor(TFT_WHITE, fill);
  d.setTextDatum(MC_DATUM);
  d.setTextSize(d.textWidth(label, 2) * 2 + 12 < w ? 2 : 1);
  d.drawString(label, x + w / 2, dY(y + h / 2), 2);
  d.setTextSize(1);
}

// Paints the current onboarding step to out(): the panel, or a screenshot slice.
static void paintOnboarding()
{
  TFT_eSPI &d = out();
  const int W = tft.width(), H = tft.height();
  d.fillRect(0, dY(0), W, H, TFT_BLACK);
  d.setTextDatum(MC_DATUM);
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  const int st = g_onb;
  if (st == OB_TOUCH || st == OB_VERIFY)
  {
    char hdr[40];
    if (st == OB_TOUCH)
      snprintf(hdr, sizeof(hdr), "Touch setup  %d / 4", obTarget + 1);
    else
      snprintf(hdr, sizeof(hdr), "Touch setup  check");
    d.drawString(hdr, W / 2, dY(H / 2 - 44), 2);
    d.drawString(st == OB_TOUCH ? "Tap the center of the cross" : "Tap the dot", W / 2, dY(H / 2 - 22), 2);
    d.setTextColor(RGB565(170, 170, 170), TFT_BLACK);
    if (obNote[0])
      d.drawString(obNote, W / 2, dY(H / 2 + 44), 2);
    d.drawString("Hold BOOT to skip", W / 2, dY(H / 2 + 66), 2);
    if (st == OB_TOUCH)
    {
      int x, y;
      obTargetXY(obTarget, x, y);
      d.drawFastHLine(x - 15, dY(y), 31, TFT_WHITE);
      d.drawFastVLine(x, dY(y - 15), 31, TFT_WHITE);
      d.drawCircle(x, dY(y), 7, TFT_WHITE);
    }
    else
    {
      const int x = W / 2, y = H / 2 + 12;
      d.fillCircle(x, dY(y), 6, TFT_WHITE);
      d.drawCircle(x, dY(y), 11, TFT_WHITE);
      if (obLandX >= 0)
      {
        d.drawFastHLine(obLandX - 6, dY(obLandY), 13, RGB565(247, 147, 26));
        d.drawFastVLine(obLandX, dY(obLandY - 6), 13, RGB565(247, 147, 26));
      }
    }
  }
  else
  {
    const int pad = 10, top = 34;
    int by, bw, bh;
    obColorButtons(by, bw, bh);
    d.drawString(st == OB_COLOR_ASK ? "What color is the big square?" : "Is the big square orange now?", W / 2, dY(top / 2), 2);
    d.fillRect(pad, dY(top), W - 2 * pad, by - pad - top, RGB565(247, 147, 26)); // Bitcoin orange #f7931a
    obButton(d, pad, by, bw, bh, st == OB_COLOR_ASK ? "Orange" : "Yes");
    obButton(d, 2 * pad + bw, by, bw, bh, st == OB_COLOR_ASK ? "Not orange" : "No");
  }
  d.setTextDatum(TL_DATUM);
}

static void obRepaint()
{
  std::lock_guard<std::mutex> lock(g_displayMutex);
  paintOnboarding();
}

static void obSave()
{
  // No config yet means the Wi-Fi setup portal still has to run; writing a
  // config now would skip it. The portal's own save persists these fields.
  if (obHaveConfig)
    nvMem.saveConfig(&Settings);
}

static TFT_eTouchBase::Calibation calFromSettings()
{
  TFT_eTouchBase::Calibation c = {Settings.touchCal[0], Settings.touchCal[1], Settings.touchCal[2],
                                  Settings.touchCal[3], (uint8_t)(Settings.touchCal[4] & 3)};
  return c;
}

static void obStartTouch()
{
  obTarget = 0;
  obLandX = obLandY = obErr = -1;
  g_onb = OB_TOUCH;
  obRepaint();
}

static void obStartColor()
{
  obColorRound = 0;
  g_onb = OB_COLOR_ASK;
  Serial.println("CYD colors: not calibrated, asking (serial: o=orange/yes, x=not orange/no)");
  obRepaint();
}

static void obFinish()
{
  g_onb = OB_OFF;
  std::lock_guard<std::mutex> lock(g_displayMutex);
  tft.fillScreen(C_BG);
  resetWidgets();
  memset(segLvl, 0xFF, sizeof(segLvl));
  if (uiLive)
  {
    uiScreen = -1; // next monitor tick repaints the whole page
  }
  else if (g_lastStatic == 2)
    paintSetup(out());
  else
    paintLoading(out());
  Serial.println("CYD onboarding: done");
}

static void obNext()
{
  if (!Settings.touchCalibrated)
    obStartTouch();
  else if (!Settings.colorCalibrated)
    obStartColor();
  else
    obFinish();
}

static uint16_t clampRaw(long v) { return (uint16_t)(v < 0 ? 0 : (v > 4095 ? 4095 : v)); }

// Linear fit of raw (r) against screen (s) at two screen positions a < b,
// extrapolated to pixels 0 and smax. Returns false when the span is too small.
static bool fitAxis(float rA, float rB, int a, int b, int smax, uint16_t &r0, uint16_t &r1)
{
  const float k = (rB - rA) / (float)(b - a);
  if (fabsf(k * smax) < 800.0f)
    return false;
  r0 = clampRaw(lroundf(rA - k * a));
  r1 = clampRaw(lroundf(rA + k * (smax - a)));
  return r0 != r1;
}

// Builds a rotation-independent Calibation from the 4 targets tapped in the
// current rotation. Detects axis swap from which raw axis moved left-right.
static bool obComputeCal(TFT_eTouchBase::Calibation &c)
{
  const int W = tft.width(), H = tft.height();
  const int rot = tft.getRotation() & 3;
  int xl, yt, xr, yb;
  obTargetXY(0, xl, yt);
  obTargetXY(2, xr, yb);
  // averages per side: targets 0=TL 1=TR 2=BR 3=BL
  const float rxL = (obRawX[0] + obRawX[3]) / 2.0f, rxR = (obRawX[1] + obRawX[2]) / 2.0f;
  const float ryL = (obRawY[0] + obRawY[3]) / 2.0f, ryR = (obRawY[1] + obRawY[2]) / 2.0f;
  const float rxT = (obRawX[0] + obRawX[1]) / 2.0f, rxB = (obRawX[3] + obRawX[2]) / 2.0f;
  const float ryT = (obRawY[0] + obRawY[1]) / 2.0f, ryB = (obRawY[3] + obRawY[2]) / 2.0f;
  const bool swap = fabsf(ryR - ryL) > fabsf(rxR - rxL);
  if (!swap)
  {
    // d_rot 0: sx = (rx - x0) * (W-1) / (x1 - x0), sy = (ry - y0) * (H-1) / (y1 - y0)
    if (!fitAxis(rxL, rxR, xl, xr, W - 1, c.x0, c.x1)) return false;
    if (!fitAxis(ryT, ryB, yt, yb, H - 1, c.y0, c.y1)) return false;
    c.rel_rotation = rot;
  }
  else
  {
    // d_rot 1: sx = (ry - y0) * (W-1) / (y1 - y0), sy = (H-1) - (rx - x0) * (H-1) / (x1 - x0)
    if (!fitAxis(ryL, ryR, xl, xr, W - 1, c.y0, c.y1)) return false;
    // u = (H-1) - sy grows from bottom to top
    if (!fitAxis(rxB, rxT, (H - 1) - yb, (H - 1) - yt, H - 1, c.x0, c.x1)) return false;
    c.rel_rotation = (rot + 3) & 3;
  }
  return true;
}

// Press tracking shared by the touch steps. Returns true once per completed
// tap (press of >= 5 samples, then release) with the averaged value.
struct TapAcc
{
  int n = 0;
  int32_t sx = 0, sy = 0;
};
static TapAcc obAcc;
static bool obTap(bool down, int32_t x, int32_t y, int32_t &ox, int32_t &oy)
{
  if (down)
  {
    if (++obAcc.n > 2) // drop the first samples while the pen settles
    {
      obAcc.sx += x;
      obAcc.sy += y;
    }
    return false;
  }
  const int n = obAcc.n - 2;
  const bool ok = n >= 3;
  if (ok)
  {
    ox = obAcc.sx / n;
    oy = obAcc.sy / n;
  }
  obAcc = TapAcc();
  return ok;
}

static void obSkipCurrent()
{
  const int st = g_onb;
  if (st == OB_TOUCH || st == OB_VERIFY)
  {
    touch.setCalibration(obPrevCal);
    Settings.touchCalibrated = true; // keep the current/default mapping
    Serial.println("CYD touch: skipped with BOOT, keeping current mapping");
  }
  else
  {
    Settings.invertColors = invertColors;
    Settings.colorCalibrated = true;
    Serial.printf("CYD colors: skipped with BOOT, keeping invertColors=%d\n", (int)invertColors);
  }
  obSave();
  obNext();
}

static unsigned long obBootSince = 0;
static bool obBootLatched = false;

// One 20 ms tick of the onboarding state machine (touch task).
static void onboardingTick()
{
  // BOOT escape (active low). Must be released before it can skip again.
  if (digitalRead(PIN_BOOT_BTN) == LOW)
  {
    if (!obBootSince) obBootSince = millis();
    if (!obBootLatched && millis() - obBootSince > 1500)
    {
      obBootLatched = true;
      obSkipCurrent();
      return;
    }
  }
  else
  {
    obBootSince = 0;
    obBootLatched = false;
  }

  const int st = g_onb;
  if (st == OB_TOUCH)
  {
    uint16_t x, y, z1, z2, rz;
    const bool down = touch.getRaw(x, y, z1, z2, rz);
    int32_t ax, ay;
    if (obTap(down, x, y, ax, ay))
    {
      obRawX[obTarget] = ax;
      obRawY[obTarget] = ay;
      Serial.printf("CYD touch: target %d raw %ld,%ld\n", obTarget, (long)ax, (long)ay);
      if (++obTarget < 4)
      {
        obNote = "";
        obRepaint();
        return;
      }
      TFT_eTouchBase::Calibation c;
      if (!obComputeCal(c))
      {
        obNote = "Taps didn't line up, again";
        obStartTouch();
        return;
      }
      Serial.printf("CYD touch: fit {%u,%u,%u,%u,%u}\n", c.x0, c.x1, c.y0, c.y1, c.rel_rotation);
      touch.setCalibration(c); // candidate; the verify step decides
      obNote = "";
      obLandX = obLandY = -1;
      g_onb = OB_VERIFY;
      obRepaint();
    }
  }
  else if (st == OB_VERIFY)
  {
    int16_t x = 0, y = 0;
    const bool down = touch.getXY(x, y);
    int32_t ax, ay;
    if (obTap(down, x, y, ax, ay))
    {
      const int tx = tft.width() / 2, ty = tft.height() / 2 + 12;
      obLandX = ax;
      obLandY = ay;
      obErr = (int)lroundf(sqrtf((float)((ax - tx) * (ax - tx) + (ay - ty) * (ay - ty))));
      Serial.printf("CYD touch: verify landed %ld,%ld, off by %d px\n", (long)ax, (long)ay, obErr);
      if (obErr <= OB_VERIFY_TOL)
      {
        TFT_eTouchBase::Calibation &c = touch.calibration();
        Settings.touchCal[0] = c.x0;
        Settings.touchCal[1] = c.x1;
        Settings.touchCal[2] = c.y0;
        Settings.touchCal[3] = c.y1;
        Settings.touchCal[4] = c.rel_rotation;
        Settings.touchCalibrated = true;
        obPrevCal = c;
        obSave();
        obNote = "";
        obRepaint(); // show where it landed for a moment
        vTaskDelay(600 / portTICK_PERIOD_MS);
        obNext();
      }
      else
      {
        static char note[40];
        snprintf(note, sizeof(note), "Off by %d px, let's redo", obErr);
        obNote = note;
        obRepaint();
        vTaskDelay(1200 / portTICK_PERIOD_MS);
        touch.setCalibration(obPrevCal);
        obStartTouch();
      }
    }
  }
  else if (st == OB_COLOR_ASK || st == OB_COLOR_CONFIRM)
  {
    int choice = -1;
    if (Serial.available())
    {
      int c = tolower(Serial.read());
      if (c == 'o' || c == 'y') choice = 0;
      if (c == 'x' || c == 'n') choice = 1;
    }
    int16_t x = 0, y = 0;
    const bool down = touch.getXY(x, y);
    int32_t ax, ay;
    if (choice < 0 && obTap(down, x, y, ax, ay))
    {
      int by, bw, bh;
      obColorButtons(by, bw, bh);
      if (ay >= by - 10) // anywhere in the bottom strip, split by halves
        choice = ax < tft.width() / 2 ? 0 : 1;
    }
    if (choice < 0)
      return;
    bool save = choice == 0; // "Orange" / "Yes": what's on screen is right
    if (!save && st == OB_COLOR_ASK)
    {
      applyPanelInvert(!invertColors); // wrong polarity: flip and confirm
      g_onb = OB_COLOR_CONFIRM;
      obRepaint();
      return;
    }
    if (!save)
    {
      applyPanelInvert(!invertColors); // neither looked right: back to the first question
      if (++obColorRound >= 3)
        save = true; // don't loop forever; the Display tab can redo it
      else
      {
        g_onb = OB_COLOR_ASK;
        obRepaint();
        return;
      }
    }
    Settings.invertColors = invertColors;
    Settings.colorCalibrated = true;
    obSave();
    Serial.printf("CYD colors: calibrated, invertColors=%d (%s)\n", (int)Settings.invertColors,
                  obHaveConfig ? "saved to /config.json" : "saved with first setup");
    obNext();
  }
}

static volatile bool g_backlightReady = false;
static volatile int g_currentDuty = 255;

void esp32_2432S028R_Init(void)
{
  tft.init();
  // No tft.initDMA(): see wEnd(). With DMA disabled every dmaWait() is a no-op.
  const bool haveConfig = nvMem.loadConfig(&Settings);
  if (!haveConfig)
    Settings.invertColors = CYD_PANEL_INVERT_DEFAULT; // fresh flash: best guess, calibration confirms
  else if (Settings.invertLegacy)
  {
    // Config written by older firmware stored the setting XOR'd with
    // TFT_INVERSION_ON. Convert once so the panel looks exactly as before;
    // the calibration prompt below then confirms (or flips) it.
    Settings.invertColors = Settings.invertColors != CYD_PANEL_INVERT_DEFAULT;
    Settings.invertLegacy = false;
  }
  applyPanelInvert(Settings.invertColors);
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

  // Touch mapping: the on-device calibration when there is one, else the
  // stock CYD constants (still the start point for calibrating).
  obPrevCal = Settings.touchCalibrated ? calFromSettings() : CAL_DEFAULT;
  touch.setCalibration(obPrevCal);
  Serial.printf("CYD touch: %s {%u,%u,%u,%u,%u}\n", Settings.touchCalibrated ? "calibrated" : "default",
                obPrevCal.x0, obPrevCal.x1, obPrevCal.y0, obPrevCal.y1, obPrevCal.rel_rotation);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  obHaveConfig = haveConfig;
  xTaskCreatePinnedToCore(touchTask, "cydTouch", 4096, NULL, 1, NULL, 0);

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

  // Touch first (the colour step needs taps to land), then colours.
  if (!Settings.touchCalibrated || !Settings.colorCalibrated)
    obNext();
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
#ifdef HAS_BACKLIGHT_CONTROL
    g_currentDuty = backlightLevel();
#else
    g_currentDuty = Settings.Brightness > 0 ? Settings.Brightness : 255;
#endif
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
  g_lastStatic = 1;
  if (!g_onb)
    paintLoading(out());
}

void esp32_2432S028R_SetupScreen(void)
{
  uiLive = false;
  uiScreen = -1;
  g_lastStatic = 2;
  if (!g_onb)
    paintSetup(out());
}

void esp32_2432S028R_AnimateCurrentScreen(unsigned long frame)
{
  if (g_onb)
    return;
  if (!uiLive || uiScreen < 0)
    return;
  if (!g_displayMutex.try_lock())
    return;
  drawPulse(frame);
  if (uiScreen == SCR_MINE && fabsf(gaugeTarget - gaugeShown) > 0.002f)
  {
    gaugeShown += (gaugeTarget - gaugeShown) * 0.3f; // ease toward the measured rate
    gaugeDraw(tft, gaugeShown);
  }
  // Protocol scatter: 5–10 Hz partial redraw from the sample ring.
  if (uiScreen == SCR_ARCH && haveArch && (frame & 1) == 0)
  {
    // Refresh hashrate text periodically from lastArch; samples read live.
    paintProtocolPlot(lastArch);
  }
  g_displayMutex.unlock();
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
    if (g_onb != OB_OFF)
    {
      onboardingTick();
      vTaskDelay(20 / portTICK_PERIOD_MS);
      continue;
    }
    int16_t t_x, t_y;
    bool down = touch.getXY(t_x, t_y);
    if (down)
    {
      if (++downCount == 2 && !wasDown) // two consecutive samples: real press, act once per touch
      {
        wasDown = true;
        if (ledcRead(0) == 0)
          pendingTouch = TA_BACKLIGHT; // dark screen: any touch wakes it
        else if (t_y < HDR_H + 4 && t_x > (isPortrait() ? 175 : 235))
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
  if (action != TA_NONE)
  {
    noteScreenActivity();
  }
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

// ---------------------------------------------------- runtime: screen capture --

bool esp32_2432S028R_CaptureBmp(WiFiClient &client, int screenIdx)
{
  int w = isPortrait() ? 240 : 320;
  int h = isPortrait() ? 320 : 240;
  const int nSlices = 4;
  int sliceH = h / nSlices; // 80 portrait, 60 landscape (only 19.2 KB!)

  shot.setColorDepth(8);
  shot.createSprite(w, sliceH);
  if (!shot.created())
  {
    Serial.printf("[ui] shot sprite %dx%d alloc failed (free %u, max %u)\n",
                  w, sliceH, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return false;
  }

  uint8_t bmpHeader[54] = {0};
  uint32_t fileSize = 1078 + w * h;
  uint32_t offBits = 1078;
  uint32_t dibHeaderSize = 40;
  int32_t width = w;
  int32_t height = h; // positive height for bottom-up order
  uint16_t planes = 1;
  uint16_t bitCount = 8;
  uint32_t compression = 0; // BI_RGB
  uint32_t imageSize = w * h;

  // BITMAPFILEHEADER (14 bytes)
  bmpHeader[0] = 'B';
  bmpHeader[1] = 'M';
  bmpHeader[2] = (uint8_t)(fileSize & 0xFF);
  bmpHeader[3] = (uint8_t)((fileSize >> 8) & 0xFF);
  bmpHeader[4] = (uint8_t)((fileSize >> 16) & 0xFF);
  bmpHeader[5] = (uint8_t)((fileSize >> 24) & 0xFF);
  bmpHeader[10] = (uint8_t)(offBits & 0xFF);
  bmpHeader[11] = (uint8_t)((offBits >> 8) & 0xFF);
  bmpHeader[12] = (uint8_t)((offBits >> 16) & 0xFF);
  bmpHeader[13] = (uint8_t)((offBits >> 24) & 0xFF);

  // BITMAPINFOHEADER (40 bytes)
  bmpHeader[14] = (uint8_t)(dibHeaderSize & 0xFF);
  bmpHeader[15] = (uint8_t)((dibHeaderSize >> 8) & 0xFF);
  bmpHeader[16] = (uint8_t)((dibHeaderSize >> 16) & 0xFF);
  bmpHeader[17] = (uint8_t)((dibHeaderSize >> 24) & 0xFF);

  bmpHeader[18] = (uint8_t)(width & 0xFF);
  bmpHeader[19] = (uint8_t)((width >> 8) & 0xFF);
  bmpHeader[20] = (uint8_t)((width >> 16) & 0xFF);
  bmpHeader[21] = (uint8_t)((width >> 24) & 0xFF);

  bmpHeader[22] = (uint8_t)(height & 0xFF);
  bmpHeader[23] = (uint8_t)((height >> 8) & 0xFF);
  bmpHeader[24] = (uint8_t)((height >> 16) & 0xFF);
  bmpHeader[25] = (uint8_t)((height >> 24) & 0xFF);

  bmpHeader[26] = (uint8_t)(planes & 0xFF);
  bmpHeader[27] = (uint8_t)((planes >> 8) & 0xFF);
  bmpHeader[28] = (uint8_t)(bitCount & 0xFF);
  bmpHeader[29] = (uint8_t)((bitCount >> 8) & 0xFF);
  bmpHeader[30] = (uint8_t)(compression & 0xFF);
  bmpHeader[34] = (uint8_t)(imageSize & 0xFF);
  bmpHeader[35] = (uint8_t)((imageSize >> 8) & 0xFF);
  bmpHeader[36] = (uint8_t)((imageSize >> 16) & 0xFF);
  bmpHeader[37] = (uint8_t)((imageSize >> 24) & 0xFF);
  bmpHeader[46] = 0x00;
  bmpHeader[47] = 0x01; // 256 colors

  client.write(bmpHeader, sizeof(bmpHeader));

  uint8_t palette[1024];
  for (int i = 0; i < 256; i++)
  {
    uint8_t r = (i >> 5) & 7;
    uint8_t g = (i >> 2) & 7;
    uint8_t b = i & 3;
    palette[i * 4 + 0] = (uint8_t)(b * 255 / 3);
    palette[i * 4 + 1] = (uint8_t)(g * 255 / 7);
    palette[i * 4 + 2] = (uint8_t)(r * 255 / 7);
    palette[i * 4 + 3] = 0;
  }
  client.write(palette, sizeof(palette));

  int targetScr = (screenIdx >= 0 && screenIdx <= 3)
                      ? screenIdx
                      : (currentDisplayDriver ? currentDisplayDriver->current_cyclic_screen : 0);
  if (targetScr < 0 || targetScr > 3)
    targetScr = 0;
  g_shotTargetScr = targetScr;

  // Freeze the mine values for the whole capture: slices are rendered one at
  // a time with network writes between them, and the monitor task updates
  // lastMine every second, so a widget spanning two slices could otherwise
  // show the top of one hashrate and the bottom of the next.
  MineV shotMine;
  bool shotHaveMine;
  {
    std::lock_guard<std::mutex> lock(g_displayMutex);
    shotHaveMine = haveMine;
    if (shotHaveMine)
      shotMine = lastMine;
  }

  auto renderSlice = [&](int yOff) {
    std::lock_guard<std::mutex> lock(g_displayMutex);
    g_shotYOffset = yOff;
    shotOn = true;
    shot.fillSprite(C_BG);
    if (g_onb)
    {
      paintOnboarding(); // the panel is showing an onboarding step: capture that
      shotOn = false;
      return;
    }
    ensureChrome(targetScr);
    if (targetScr == SCR_MINE)
    {
      if (shotHaveMine)
      {
        shotMine.hdr = liveHeader(getTime());
        shotMine.time12 = fmt12HourTime(getTime());
        uint32_t nowSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        uint32_t blkSec = (g_blockStartTimeSec > 0 && nowSec >= g_blockStartTimeSec) ? (nowSec - g_blockStartTimeSec) : 0;
        shotMine.strip.v[0] = fmtBlockTime(blkSec);
        shotMine.blockHashes = fmtHashCountU64(g_currentBlockHashes);
        shotMine.blocks = String(valids);
        paintMine(shotMine);
      }
      else
      {
        MineV m;
        m.hdr = liveHeader(getTime());
        m.hash = getCurrentHashRate(0);
        m.shares = commas(String(shares));
        char bestBuf[16];
        suffix_string(best_diff, bestBuf, 16, 0);
        m.best = String(bestBuf);
        m.tmpl = commas(String(templates));
        m.blocks = String(valids);
        m.total = fmtHashCount(String(Mhashes));
        m.blockHashes = fmtHashCountU64(g_currentBlockHashes);
        m.time12 = fmt12HourTime(getTime());
        m.kh = m.hash.toFloat();
        m.flash = false;
        uint32_t nowSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        uint32_t blkSec = (g_blockStartTimeSec > 0 && nowSec >= g_blockStartTimeSec) ? (nowSec - g_blockStartTimeSec) : 0;
        m.strip.v[0] = fmtBlockTime(blkSec);
        m.strip.l[0] = "BLK TIME";
        m.strip.v[1] = fmtUptimeShort(String((unsigned long)upTime));
        m.strip.l[1] = "UPTIME";
        float tC = temperatureRead();
        float tF = tC * 1.8f + 32.0f;
        m.strip.v[2] = String((int)roundf(tF)) + " F";
        m.strip.l[2] = "TEMP";
        paintMine(m);
      }
      gaugeDraw(out(), gaugeShown > 0.0f ? gaugeShown : 0.84f);
    }
    else if (targetScr == SCR_MARKET)
    {
      if (haveMarket)
      {
        lastMarket.hdr = liveHeader(getTime());
        paintMarket(lastMarket);
      }
      else
      {
        MarketV m;
        m.hdr = liveHeader(getTime());
        m.price = fmtPrice(String(bitcoin_price));
        m.height = current_block.toInt();
        m.block = commas(current_block);
        m.age = blockAgeMin();
        m.fee = gData.halfHourFee;
        m.feeHour = gData.feeHour;
        m.feeFast = gData.feeFast;
        m.diff = gData.difficulty.length() ? gData.difficulty : (g_networkDifficulty > 0.0 ? formatNetworkDifficulty(g_networkDifficulty, true) : String("--"));
        m.net = gData.globalHash.length() ? gData.globalHash : String("--");
        unsigned long cb = current_block.toInt();
        unsigned long rem = cb ? (((cb / HALVING_BLOCKS) + 1) * HALVING_BLOCKS) - cb : 0;
        m.pct = (int)((HALVING_BLOCKS - rem) * 100 / HALVING_BLOCKS);
        m.remaining = rem;
        m.eta = halvingEtaStr(rem);
        paintMarket(m);
      }
    }
    else if (targetScr == SCR_CLOCK)
    {
      if (haveClock)
      {
        lastClock.hdr = liveHeader(getTime());
        paintClock(lastClock);
      }
      else
      {
        ClockV c;
        String t = getTime();
        c.hdr = liveHeader(t);
        int h = 0, m = 0, s = 0;
        if (sscanf(t.c_str(), "%d:%d:%d", &h, &m, &s) >= 2 ||
            sscanf(t.c_str(), "%d:%d", &h, &m) >= 2)
        {
          c.period = (h >= 12) ? "PM" : "AM";
          int h12 = h % 12;
          if (h12 == 0) h12 = 12;
          c.hh = String(h12);
          char mBuf[8];
          snprintf(mBuf, sizeof(mBuf), "%02d", m);
          c.mm = String(mBuf);
        }
        else
        {
          c.hh = "--";
          c.mm = "--";
          c.period = "";
        }
        c.colon = true;
        c.date = fmtDate(getDate());
        c.hash = getCurrentHashRate(0);
        float tC = temperatureRead();
        float tF = tC * 1.8f + 32.0f;
        c.temp = String((int)roundf(tF)) + " F";
        c.uptime = fmtUptimeShort(String((unsigned long)upTime));
        c.link = (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE";
        c.linkCol = (WiFi.status() == WL_CONNECTED) ? C_GOOD : C_BAD;
        c.tempCol = C_TEXT;
        c.flash = false;
        paintClock(c);
      }
    }
    else if (targetScr == SCR_ARCH)
    {
      if (haveArch)
      {
        lastArch.hdr = liveHeader(getTime());
        paintArchitecture(lastArch);
      }
      else
      {
        ArchitectureV a;
        a.hdr = liveHeader(getTime());
        a.netDiff = gData.difficulty.length() ? gData.difficulty : (g_networkDifficulty > 0.0 ? formatNetworkDifficulty(g_networkDifficulty, true) : String("133T"));
        a.netDiffRaw = g_networkDifficulty;
        char bestBuf[16];
        suffix_string(best_diff, bestBuf, 16, 0);
        a.bestDiff = String(bestBuf);
        a.bestDiffRaw = best_diff;
        a.hashRate = getCurrentHashRate(0);
        paintArchitecture(a);
      }
    }
    shotOn = false;
  };

  // Stream slices bottom-up for BMP
  for (int s = nSlices - 1; s >= 0; s--)
  {
    renderSlice(s * sliceH);
    const uint8_t *pixels = (const uint8_t *)shot.getPointer();
    for (int y = sliceH - 1; y >= 0; y--)
    {
      client.write(pixels + y * w, w);
      if ((y & 15) == 0)
      {
        vTaskDelay(1); // yield to Wi-Fi / lwIP task
      }
    }
    vTaskDelay(2);
  }

  shot.deleteSprite();
  g_shotYOffset = 0;
  return true;
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
    m.time12 = "9:47 PM";
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
    c.temp = "124 F";
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

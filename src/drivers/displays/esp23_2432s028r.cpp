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
// Screens (see "the three screens"): MINE, ODDS, WORLD. /api/screenshot
// renders any of them (and the boot/setup pages) into an offscreen sprite.
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
#ifdef CYD_TOUCH_SHARED_SPI
#if (defined(TFT_RST) && TFT_RST == 12) || defined(TOUCH_CLK) || !defined(TFT_MISO) || TFT_MISO != 12
#error "shared-bus CYD: GPIO12 must be TFT_MISO (not TFT_RST) and the 2.8in TOUCH_* bus pins unset"
#endif
// 2.4" CYD: touch hangs off the display's SPI bus, so use TFT_eSPI's own
// SPIClass. Both libraries wrap every access in beginTransaction(), whose
// HAL lock keeps the touch task and the drawing tasks from interleaving.
TFT_eTouch<TFT_eSPI> touch(tft, ETOUCH_CS, 0xFF, TFT_eSPI::getSPIinstance());
#else
// 2.8" CYD: dedicated touch bus on HSPI (25/32/39).
SPIClass hSPI(HSPI);
TFT_eTouch<TFT_eSPI> touch(tft, ETOUCH_CS, 0xFF, hSPI);
#endif

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

#define SCR_MINE 0  // unified mining dashboard
#define SCR_ODDS 0  // legacy alias (merged into Mine)
// The WORLD page was retired from the CYD (its numbers live on the web
// dashboard); set CYD_WORLD_SCREEN 1 to build it back in.
#define CYD_WORLD_SCREEN 0
#if CYD_WORLD_SCREEN
#define SCR_WORLD 1 // how do I fit into Bitcoin?
#define SCR_COUNT 2
#else
#define SCR_COUNT 1
#endif

extern volatile uint32_t g_stratumPing;
extern double g_networkDifficulty;
extern bool g_usingFallback;
extern global_data gData;
extern volatile uint32_t valids;

// Screenshots can render as if in another rotation (GET /api/screenshot?rot=N)
// without touching the panel; only while shotOn, so live drawing is unaffected.
static int g_shotRot = -1;
static bool shotOn = false;
void cydSetShotRotation(int r) { g_shotRot = (r >= 0 && r <= 3) ? r : -1; }
static inline bool isPortrait()
{
  int r = (shotOn && g_shotRot >= 0) ? g_shotRot : tft.getRotation();
  return (r == 0 || r == 2);
}

// Mine ring owns most of the upper screen (portrait) / left column (landscape).
// Three layers: outer = speed, middle = best hash digits, inner = its zeros.
// Half-circle dial: the rings sweep 180 degrees over the top, left to right,
// around (gCX, gCY); nothing is drawn below gCY, so the big number sits in the
// dome and the toasts / panels use the full width underneath.
static inline int gCX() { return isPortrait() ? 120 : 104; }
// The arc rises into the top bar, between the trophy badge and the clock.
// Portrait: top edge y=9; at the badge's bottom (y=27) the outer ring spans
// x 60..180 (badge ends x=54) and at the clock's bottom (y=22) it ends at
// x=171 (clock widget starts x=174). Landscape: the badge sits close to the
// dial's centre column, so the top stops at y=17 (outer ring x>=60 at y=27,
// badge ends x=56); the clock is far right, beyond the dial.
static inline int gCY() { return isPortrait() ? 83 : 117; }
static inline int gR()  { return isPortrait() ? 77 : 100; }
static inline int gRI() { return isPortrait() ? 68 : 88; }
static inline int hexR() { return isPortrait() ? 87 : 79; }  // middle ring: centres of the hex cells
#define HEX_N 20

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
// Inner ring: tries this network round (resets at every block)
static float roundTarget = 0;
static float roundShown = 0;
static uint8_t rndLvl[G_SEGS];
static char hexShown[HEX_N]; // middle ring: char drawn per cell (0 = redraw)
static bool rndDrain = false;  // a new block: drain the inner ring to 0 first
// Screen 0 colors: orange = speed, blue (C_GUESS) = guesses this round (inner
// ring + counter), magenta = my best hash (its leading zeros).
#define C_GUESS 0x3CDF // blue (58,154,255): inner ring and the guesses-this-round counter
#define C_BEST 0xFA99  // magenta: my best hash this round
#define C_CYAN 0x07FF  // bright cyan: accepted shares / pool line
// Card fill shared by MINE's lower panel and the ODDS cards: black inside a
// thin gray edge (C_EDGE), so both screens read as one design.
#define C_CARD C_BG
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
  W_ARCH_TOP,
  W_ARCH_ROW0, // up to 6 list rows
  W_ARCH_ROW1,
  W_ARCH_ROW2,
  W_ARCH_ROW3,
  W_ARCH_ROW4,
  W_ARCH_ROW5,
  W_HDR_COIN,
  W_RND_A, // MINE: small round lines around the hero
  W_RND_B,
  W_RND_C,
  W_MP_REC, // MINE panel: zero records
  W_MP_GSS, // MINE panel: guesses this round
  W_CONVEYOR, // MINE: animated mini conveyor machine
  W_ATT_DOTS, // MINE RECENT ATTEMPTS: point layer
  W_ATT_BINS, // MINE RECENT ATTEMPTS: MISS/SHARE/BLOCK bins
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
static int g_shotYOffset = 0;
static int g_shotTargetScr = 0;
static bool g_bandOn = false; // drawing a large region band-by-band into wgt (see banded())
static inline int dY(int y) { return (shotOn && !g_bandOn) ? (y - g_shotYOffset) : y; }
static TFT_eSPI &out() { return g_bandOn ? (TFT_eSPI &)wgt : (shotOn ? (TFT_eSPI &)shot : (TFT_eSPI &)tft); }

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

// Large regions (chart, clock face, ladder, market hero) used to be cleared on
// the panel with fillRect() and then redrawn, which showed as a flash on
// every refresh. banded() renders the same drawing code into the persistent
// widget sprite in horizontal bands (sized by AREA, so any width fits) and
// pushes each finished band with a blocking pushSprite(): the panel only ever
// receives final pixels. The draw code keeps its absolute screen coordinates;
// a viewport datum on the sprite translates them. Screenshots go through the
// same bands (pushed into the shot slice), so /api/screenshot shows exactly
// what the panel gets.
template <typename F>
static void banded(int x, int y, int w, int h, uint16_t bg, F draw)
{
  if (w <= 0 || h <= 0)
  {
    draw();
    return;
  }
  if (!ensureWgtReady())
  {
    draw();
    return;
  }
  int bandH = (wgtMaxW * wgtMaxH) / w;
  if (bandH > h)
    bandH = h;
  if (bandH < 1)
  {
    draw();
    return;
  }
  g_bandOn = true;
  for (int by = 0; by < h; by += bandH)
  {
    const int bh = (h - by) < bandH ? (h - by) : bandH;
    wgt.resize(w, bh);
    wgt.fillSprite(bg);
    wgt.setViewport(-x, -(y + by), w + x, bh + y + by, true);
    draw();
    wgt.setViewport(0, 0, w, bh);
    if (shotOn)
      wgt.pushToSprite(&shot, x, y + by - g_shotYOffset);
    else
    {
      wgt.pushSprite(x, y + by);
      g_dmaTransferCount++;
    }
  }
  g_bandOn = false;
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
  // btcPrice is "<ascii prefix><digits>". ASCII prefix only: this font has
  // no glyphs for the pound/euro/yen signs (or a middle dot) and draws boxes.
  int i = 0;
  while (i < (int)s.length() && !isdigit((unsigned char)s[i]))
    i++;
  String digits = s.substring(i);
  if (digits.toInt() <= 0)
    return "--";
  return String(currencyFor(Settings.Currency).prefix) + commas(digits);
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

// Caption: muted, small, shrinks to fit maxW. A few words only.
static uint16_t capColor() { return mix565(C_DIM, C_PANEL, 70); }
static void caption(TFT_eSPI &d, const char *s, int x, int y, int maxW, Align a = Align::TopLeft,
                    uint16_t bg = 0xFFFF, uint16_t fg = 0xFFFF)
{
  if (bg == 0xFFFF) bg = C_PANEL;
  if (fg == 0xFFFF) fg = capColor();
  unsigned sz = 10;
  while (sz > 8 && textW(s, sz) > maxW)
    sz--;
  text(d, s, x, y, sz, fg, bg, a);
}

// 10x16 thermometer: tube outline, filled bulb, mercury column
static void drawThermoIcon(TFT_eSPI &d, int x, int y, uint16_t col)
{
  d.drawRoundRect(x + 2, y, 6, 12, 3, col);
  d.fillCircle(x + 5, y + 12, 4, col);
  d.fillRect(x + 4, y + 5, 2, 7, col);
  d.drawFastHLine(x + 8, y + 3, 2, col);
  d.drawFastHLine(x + 8, y + 6, 2, col);
}

// 14x14 clock face with tick marks
static void drawClockIcon(TFT_eSPI &d, int x, int y, uint16_t col)
{
  d.drawCircle(x + 7, y + 7, 6, col);
  d.drawCircle(x + 7, y + 7, 7, col);
  d.drawFastVLine(x + 7, y + 3, 5, col);
  d.drawFastHLine(x + 7, y + 7, 4, col);
}

// 14x14 "block": a cube drawn as front square + top/side faces
static void drawBlockIcon(TFT_eSPI &d, int x, int y, uint16_t col)
{
  d.drawRect(x, y + 4, 10, 10, col);
  d.drawLine(x, y + 4, x + 4, y, col);
  d.drawLine(x + 10, y + 4, x + 14, y, col);
  d.drawLine(x + 4, y, x + 14, y, col);
  d.drawLine(x + 14, y, x + 14, y + 10, col);
  d.drawLine(x + 10, y + 14, x + 14, y + 10, col);
}


// Plain count: 163.8B, 180.6M, 12.3K (no unit jargon)
static void fmtDiff(double v, char *b, size_t n);
static String fmtCount(double v);

// "164B" -> "billion" (the word for a fmtCount/fmtDiff suffix)
static const char *suffixWord(const String &v)
{
  if (!v.length()) return "";
  switch (v[v.length() - 1])
  {
  case 'K': return "thousand";
  case 'M': return "million";
  case 'B': return "billion";
  case 'T': return "trillion";
  case 'Q': return "quadrillion";
  default: return "";
  }
}

// Average wait for this miner to find a block: value + unit ("19T", "trillion yrs")
static void fmtWait(double sec, String &val, String &unit)
{
  char b[16];
  if (!(sec > 0)) { val = "--"; unit = ""; return; }
  if (sec < 5400) { snprintf(b, sizeof(b), "%.0f", sec / 60); unit = "minutes"; }
  else if (sec < 172800) { snprintf(b, sizeof(b), "%.0f", sec / 3600); unit = "hours"; }
  else if (sec < 31557600.0 * 2) { snprintf(b, sizeof(b), "%.0f", sec / 86400); unit = "days"; }
  else
  {
    double y = sec / 31557600.0;
    String c = fmtCount(y);
    const char *w = suffixWord(c);
    if (w[0])
    {
      c.remove(c.length() - 1); // "19.3B" -> "19.3" + "billion yrs"
      unit = String(w) + " yrs";
    }
    else
      unit = "years";
    val = c;
    return;
  }
  val = b;
}

// "59s ago", "12m ago", "1h05m ago"
static String fmtAgo(uint32_t sec)
{
  char b[16];
  if (sec < 60) snprintf(b, sizeof(b), "%us ago", (unsigned)sec);
  else if (sec < 3600) snprintf(b, sizeof(b), "%um ago", (unsigned)(sec / 60));
  else snprintf(b, sizeof(b), "%uh%02um ago", (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60));
  return String(b);
}

// Strip cell: icon on the left, tiny LABEL over the value.
// icon: 0 block, 1 clock, 2 thermometer, 3 status dot, 4 check, 5 pie, 6 puzzle piece
static void drawIconCell(int id, int x, int y, int cw, int ch, int icon, const char *label, const char *value,
                         uint16_t vCol, uint16_t iCol)
{
  char k[64];
  snprintf(k, sizeof(k), "ic|%d|%s|%s|%u|%u|%d", icon, label, value, vCol, iCol, cw);
  if (!need(id, k))
    return;
  if (!wBegin(cw, ch, C_PANEL))
    return;
  const int iy = (ch - 15) / 2;
  if (icon == 0) drawBlockIcon(wgt, 4, iy, iCol);
  else if (icon == 1) drawClockIcon(wgt, 3, iy, iCol);
  else if (icon == 2) drawThermoIcon(wgt, 5, iy - 1, iCol);
  else if (icon == 3) wgt.fillCircle(9, ch / 2, 4, iCol);
  else if (icon == 6)
  { // puzzle piece = a job from the pool (filled when it just arrived)
    const int px = 3, py = iy + 3;
    wgt.drawRect(px, py, 11, 11, iCol);
    wgt.fillCircle(px + 5, py - 1, 2, iCol);  // knob on top
    wgt.fillCircle(px + 12, py + 5, 2, iCol); // knob on the right
    if (iCol == C_METER)
      wgt.fillRect(px + 2, py + 2, 7, 7, iCol);
  }
  else if (icon == 5)
  { // the world (circle) and your sliver of it
    wgt.drawCircle(9, ch / 2, 7, C_DIM);
    wgt.drawLine(9, ch / 2, 9, ch / 2 - 7, iCol);
    wgt.drawLine(9, ch / 2, 10, ch / 2 - 7, iCol);
  }
  else
  { // check mark: "accepted"
    wgt.drawLine(3, iy + 8, 7, iy + 12, iCol); wgt.drawLine(3, iy + 9, 7, iy + 13, iCol);
    wgt.drawLine(7, iy + 12, 15, iy + 2, iCol); wgt.drawLine(7, iy + 13, 15, iy + 3, iCol);
  }
  const int tx = 21;
  const int top = (ch - 22) / 2;
  text(wgt, label, tx, top, 8, C_DIM, C_PANEL);
  unsigned vs = 12;
  while (vs > 9 && textW(value, vs) > cw - tx - 2)
    vs--;
  text(wgt, value, tx, top + (ch < 28 ? 8 : 10), vs, vCol, C_PANEL); // short cells: room for descenders (g in "ago")
  wEnd(x, y);
  done(id, k);
}

static String fmtCount(double v)
{
  static const char *u[] = {"", "K", "M", "B", "T", "Q"};
  int i = 0;
  while (v >= 1000.0 && i < 5) { v /= 1000.0; i++; }
  char b[16];
  if (i == 0) snprintf(b, sizeof(b), "%.0f", v);
  else snprintf(b, sizeof(b), v >= 100 ? "%.0f%s" : "%.1f%s", v, u[i]);
  return String(b);
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
    uint16_t tCol = (i >= 27) ? mix565(C_BAD, C_BG, 64) : mix565(C_BG, C_METER, 56); // all orange: no theme (blue) track
    uint16_t col = lvl == 0 ? tCol : (lvl == 4 ? mCol : mix565(tCol, mCol, lvl * 64));
    int a0 = 90 + i * 6; // 180 degree sweep: 9 o'clock over the top to 3 o'clock
    d.drawArc(G_CX, cy, G_R, G_RI, a0, a0 + 5, col, C_BG, true);
    if (!shotOn)
      segLvl[i] = lvl;
  }
}

// Leading hex zeros of the last winning block hash (what a block needs).
static int winnerZeros()
{
  int z = 0;
  while (z < 64 && g_lastWinnerHashHex[z] == '0') z++;
  return z;
}

// Angle (drawArc convention: 0 = 6 o'clock, clockwise) of middle/inner cell i.
static inline float hexAng(int i) { return 90.0f + (i + 0.5f) * (180.0f / HEX_N); }

// Inner ring (thin, 20 segments under the 20 hex cells): v = guesses this
// round in 20ths of a typical block's worth (coin's block time) at the average speed
// (0..20, capped full). Dim blue track, lit segments blue.
static inline int rR()  { return isPortrait() ? 78 : 70; }
static inline int rRI() { return isPortrait() ? 73 : 66; }
static void roundRingDraw(TFT_eSPI &d, float v, int need)
{
  (void)d; (void)v; (void)need;
  return; // inner blue guesses ring removed per user request (simplified dial)
  if (&d == (TFT_eSPI*)&tft) tft.dmaWait();
  if (!(v > 0)) v = 0;
  need = HEX_N; // the whole track is shown
  const uint16_t trk = mix565(C_BG, C_GUESS, 60);
  int cy = dY(G_CY);
  for (int i = 0; i < HEX_N; i++)
  {
    float f = v - i;
    uint8_t lvl = f >= 1.0f ? 4 : (f <= 0.05f ? 0 : 1 + (int)(f * 3.0f));
    if (lvl == 0 && i >= need) lvl = 9; // past what a block needs: no track
    if (!shotOn && lvl == rndLvl[i])
      continue;
    uint16_t col = lvl == 9 ? C_BG : lvl == 0 ? trk : lvl < 4 ? mix565(trk, C_GUESS, lvl * 64) : C_GUESS;
    const float step = 180.0f / HEX_N;
    int a0 = (int)(90.0f + i * step + 1.0f);
    d.drawArc(G_CX, cy, rR(), rRI(), a0, (int)(90.0f + (i + 1) * step - 1.0f), col, C_BG, true);
    if (!shotOn)
      rndLvl[i] = lvl;
  }
}

// Middle ring: the best hash this round, printed order, one hex digit per
// cell around the dial. Leading zeros are orange cells; the rest stay dim.
static void hexRingDraw(const String &hex)
{
  (void)hex;
  return; // middle hex-digit ring removed per user request (simplified dial)
  const bool P = isPortrait();
  const int cs = 11, r = hexR();
  bool lead = true;
  for (int i = 0; i < HEX_N; i++)
  {
    const char c = i < (int)hex.length() ? hex[i] : '-';
    const bool z = lead && c == '0';
    if (c != '0') lead = false;
    const char key = z ? 'Z' : c; // a lit zero and a plain one look different
    if (!shotOn && hexShown[i] == key)
      continue;
    const float a = hexAng(i) * (float)M_PI / 180.0f;
    const int x = G_CX - (int)lroundf(r * sinf(a)) - cs / 2;
    const int y = G_CY + (int)lroundf(r * cosf(a)) - cs / 2;
    if (!wBegin(cs, cs, C_BG))
      continue;
    const char t[2] = {c, 0};
    if (z)
    {
      wgt.fillRoundRect(0, 0, cs, cs, 2, C_BEST);
      text(wgt, t, cs / 2, -2, 9, TFT_BLACK, C_BEST, Align::TopCenter);
    }
    else
      text(wgt, t, cs / 2, -2, 9, c == '-' ? mix565(C_BG, C_DIM, 110) : mix565(C_BG, C_DIM, 170), C_BG, Align::TopCenter);
    wEnd(x, y);
    if (!shotOn)
      hexShown[i] = key;
  }
  (void)P;
}

// ------------------------------------------------------------------ header --

struct HdrV
{
  String time;
  String time12;
  String blocks;
  String netHeight; // network block height being mined (thin header, left)
  String job;       // seconds since the last mining.notify job (header, left)
  String ip;
  int bars;
  int status;
  int screen; // current screen index (0-3) for nav dots
};

static void drawHeader(const HdrV &h)
{
  const bool P = isPortrait();
  const int bx = P ? 6 : 8;
  // Clock widget kept narrow and short so it never covers the dial's arc
  // (which now rises into the bar): x 174..233 / 252..311, y 2..22.
  const int tx = P ? 174 : 252;
  const int wBadge = 48, hBadge = 22;
  const int wClock = 60, hClock = 21;

  // Thin BLOCK / JOB header on the left (the dial arc owns the centre; the clock
  // is on the right). No trophy: the block COUNT lives in the bottom BLOCK bin.
  (void)wBadge; (void)hBadge;
  String kBlk = "hblk|" + h.netHeight + "|" + h.job + "|" + (P ? "P" : "L");
  if (need(W_HDR_BADGE, kBlk))
  {
    if (wBegin(64, 22, C_BG))
    {
      // tiny gold block cube
      wgt.fillRect(1, 4, 6, 6, C_GOLD);
      wgt.drawRect(1, 4, 6, 6, mix565(C_GOLD, C_BG, 120));
      String bn = "#" + h.netHeight;
      unsigned bs = 10;
      while (bs > 8 && textW(bn.c_str(), bs) > 52) bs--;
      text(wgt, bn.c_str(), 10, 1, bs, C_GOLD, C_BG);
      String jb = "JOB " + h.job;
      text(wgt, jb.c_str(), 0, 12, 8, C_DIM, C_BG);
      wEnd(bx, 2);
      done(W_HDR_BADGE, kBlk);
    }
  }

  // Top-right 12-hour clock (x=174/252, y=2, w=60, h=21); same text spot as before
  String kClock = "hclk|" + h.time12 + "|" + (P ? "P" : "L");
  if (need(W_HDR_TIME, kClock))
  {
    if (wBegin(wClock, hClock, C_BG))
    {
      unsigned cs = 13;
      while (cs > 10 && textW(h.time12.c_str(), cs) > wClock - 2) cs--;
      text(wgt, h.time12.c_str(), wClock - 2, 5, cs, C_TEXT, C_BG, Align::TopRight);
      wEnd(tx, 2);
      done(W_HDR_TIME, kClock);
    }
  }
}

static void drawPulse(unsigned long frame)
{
  // No-op: eliminated distracting pulsing dot
}

// ===================================================== the three screens ==
// Each screen answers one question for someone new to Bitcoin mining:
//   MINE  - what is my miner doing right now?   (speed dial, last minute, proofs, pool)
//   ODDS  - how close am I to winning a block?  (share ladder, odds, the race)
//   WORLD - how does my miner fit into Bitcoin? (you vs everyone, puzzle, halving)
// Static frames are drawn once per screen/rotation (ensureChrome); each value
// is a keyed widget that is only repainted when its text changes, through the
// widget sprite (wBegin/wEnd) or banded() for large regions, so the panel
// never shows a clear-then-draw flash.

// ---------------------------------------------------------- shared pieces --

static String fmtReward(float r)
{
  String s = String(r, 4);
  while (s.endsWith("0")) s.remove(s.length() - 1);
  if (s.endsWith(".")) s.remove(s.length() - 1);
  return s;
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


// Crisp lightning bolt glyph (12x18)
static void drawBolt(TFT_eSPI &d, int x, int y, uint16_t col)
{
  d.fillTriangle(x + 7, y, x + 1, y + 9, x + 6, y + 7, col);
  d.fillTriangle(x + 1, y + 9, x + 6, y + 9, x + 6, y + 7, col);
  d.fillTriangle(x + 6, y + 9, x + 6, y + 7, x + 11, y + 7, col);
  d.fillTriangle(x + 6, y + 9, x + 11, y + 7, x + 5, y + 17, col);
}

// Bottom 3-cell strip, same place on every screen.
struct Rect { int x, y, w, h; };
static Rect stripRect() { return isPortrait() ? Rect{6, 285, 228, 32} : Rect{8, 202, 304, 34}; }

static void chromeStrip3(TFT_eSPI &d)
{
  const Rect r = stripRect();
  panel(d, r.x, r.y, r.w, r.h, 6, C_PANEL, C_EDGE, C_BG);
  for (int i = 1; i < 3; i++)
    d.drawFastVLine(r.x + (r.w * i) / 3, dY(r.y) + 6, r.h - 12, C_EDGE);
}

static void stripCell(int i, int icon, const char *label, const char *value, uint16_t vCol, uint16_t iCol)
{
  const Rect r = stripRect();
  const int cw = r.w / 3 - 4, ch = r.h - 6;
  drawIconCell(W_P0 + i, r.x + (r.w * i) / 3 + 2, r.y + 3, cw, ch, icon, label, value, vCol, iCol);
}

// Two-line card: LABEL (muted) / value + unit word (muted), optional log bar.
static void drawCardRow(int id, int x, int y, int w, int h, const char *label, const char *value,
                        uint16_t valCol, const char *unit, float bar = -1)
{
  char k[96];
  snprintf(k, sizeof(k), "cr|%s|%s|%u|%s|%d|%d", label, value, valCol, unit, (int)(bar * 100), w);
  if (!need(id, k))
    return;
  if (!wBegin(w, h, C_BG))
    return;
  panel(wgt, 0, 0, w, h, 6, C_CARD, C_EDGE, C_BG);
  text(wgt, label, 7, 3, 9, C_DIM, C_CARD);
  const int vy = bar >= 0 ? 14 : 15;
  unsigned vs = bar >= 0 ? 13 : 15;
  while (vs > 11 && textW(value, vs) > w - 14)
    vs--;
  text(wgt, value, 7, vy, vs, valCol, C_CARD);
  if (unit && unit[0])
  {
    int ux = 7 + textW(value, vs) + 4;
    caption(wgt, unit, ux, vy + (vs - 9), w - ux - 4, Align::TopLeft, C_CARD);
  }
  if (bar >= 0)
  {
    const int bx = 7, bw = w - 14, by = h - 5;
    wgt.drawFastHLine(bx, by, bw, C_EDGE);
    wgt.drawFastHLine(bx, by + 1, bw, C_EDGE);
    int fw = (int)(bw * bar);
    if (fw < 2) fw = 2;
    wgt.fillRect(bx, by, fw, 2, C_METER);
    wgt.fillRect(bx + bw - 2, by - 2, 2, 6, C_GOLD);
  }
  wEnd(x, y);
  done(id, k);
}

// Centred card: LABEL / value / caption.
static void drawStatCard(int id, int x, int y, int w, int h, const char *label, const char *value,
                         const char *cap, bool known)
{
  char k[96];
  snprintf(k, sizeof(k), "sc|%s|%s|%s|%d|%d", label, value, cap, known, w);
  if (!need(id, k))
    return;
  if (!wBegin(w, h, C_BG))
    return;
  panel(wgt, 0, 0, w, h, 6, C_CARD, C_EDGE, C_BG);
  const bool P = isPortrait();
  text(wgt, label, w / 2, 3, 9, C_DIM, C_CARD, Align::TopCenter);
  unsigned vs = P ? 16 : 15;
  while (vs > 11 && textW(value, vs) > w - 10)
    vs--;
  text(wgt, known ? value : "--", w / 2, P ? 14 : 13, vs, known ? C_TEXT : C_DIM, C_CARD, Align::TopCenter);
  caption(wgt, cap, w / 2, h - 12, w - 6, Align::TopCenter, C_CARD);
  wEnd(x, y);
  done(id, k);
}


// Pool link in plain words.
static void poolState(String &v, uint16_t &col)
{
  char perr[48];
  getPoolError(perr, sizeof(perr));
  if (WiFi.status() != WL_CONNECTED) { v = "NO WI-FI"; col = C_BAD; }
  else if (uiStatus() == NM_hashing) { v = "ONLINE"; col = C_GOOD; }
  else if (perr[0]) { v = "REFUSED"; col = C_BAD; }
  else { v = "LINKING"; col = C_WARN; }
}

// "~73 min": how often a proof (share) should arrive at this speed
static String fmtEvery(double poolDiff, float kh)
{
  if (!(kh > 0) || !(poolDiff > 0)) return "--";
  double sec = poolDiff * 4294967296.0 / (kh * 1000.0);
  char b[24];
  if (sec >= 5400) snprintf(b, sizeof(b), "~%.1f h", sec / 3600.0);
  else if (sec >= 90) snprintf(b, sizeof(b), "~%.0f min", sec / 60.0);
  else snprintf(b, sizeof(b), "~%.0f s", sec);
  return String(b);
}

// Everyone's guesses per second, from the live job's difficulty (per coin).
static double worldHs()
{
  return g_networkDifficulty > 0 ? g_networkDifficulty * 4294967296.0 / (double)blockTargetSecs() : 0;
}

static String uptimeNow()
{
  return fmtUptime(String((unsigned long)(upTime / 86400)) + " " + String((unsigned long)((upTime / 3600) % 24)) + ":" +
                   String((unsigned long)((upTime / 60) % 60)) + ":" + String((unsigned long)(upTime % 60)));
}

// Compact difficulty: 0.00015, 0.84, 3.21, 31.1, 412, 1.8K, 140.6T
static void fmtDiff(double v, char *b, size_t n)
{
  static const char *suf[] = {"", "K", "M", "B", "T", "P", "E"};
  if (!(v > 0)) { snprintf(b, n, "0"); return; }
  if (v < 1) { snprintf(b, n, "%.3g", v); return; }
  int i = 0;
  while (v >= 1000.0 && i < 6) { v /= 1000.0; i++; }
  if (i == 0)
    snprintf(b, n, v >= 100 ? "%.0f" : "%.3g", v);
  else
    snprintf(b, n, v >= 100 ? "%.0f%s" : "%.1f%s", v, suf[i]);
}

static void fmtAge(uint32_t now, uint32_t t, char *b, size_t n)
{
  if (!now || !t || t > now + 60) { snprintf(b, n, "--"); return; }
  uint32_t s = now > t ? now - t : 0;
  if (s < 60) snprintf(b, n, "now");
  else if (s < 3600) snprintf(b, n, "%um", (unsigned)(s / 60));
  else if (s < 86400) snprintf(b, n, "%uh", (unsigned)(s / 3600));
  else snprintf(b, n, "%ud", (unsigned)(s / 86400));
}

// Where a difficulty sits between the pool floor (0) and a block (1), log scale.
static float ladderFrac(double v, double lo, double hi)
{
  if (!(v > 0) || hi - lo < 0.1) return 0;
  double f = (log10(v) - lo) / (hi - lo);
  return f < 0 ? 0 : (f > 1 ? 1 : (float)f);
}

// thin "how high on the ladder" bar with a trophy-coloured end cap
static void ladderBar(TFT_eSPI &d, int x, int y, int w, int h, float f, uint16_t col)
{
  d.fillRoundRect(x, y, w, h, h / 2, C_EDGE);
  int fw = (int)(w * f);
  if (fw < h) fw = h;
  d.fillRoundRect(x, y, fw, h, h / 2, col);
  d.fillRect(x + w - 2, y - 2, 2, h + 4, C_GOLD);
}

// ============================================================ MINE screen ==
// Dial = guesses per second; inner thin ring = tries this network round
// (drains at every new block); the last minute of speed; proofs; pool link.
//   Portrait : dial cy=136 r=98, chart 6,208 228x74, strip y=285
//   Landscape: dial cx=100 cy=116 r=86, chart 196,30 116x160, strip y=202

struct MineV
{
  HdrV hdr;
  String hash, okBad, jobLabel, job, every;
  String rA, rB, rC; // small round lines (above / below the hero)
  String bestHex;    // first 20 hex digits of the best hash this round ("" = none yet)
  int winZ = 0;      // leading hex zeros of the last winning block hash
  String guesses;    // guesses this round (resets at a new block)
  uint16_t rACol = 0, rBCol = 0;
  uint16_t jobCol = 0, jobICol = 0, jobIcon = 0, okCol = 0;
  float kh = 0;
  bool flash = false;

  // Odds / Multifunctional Shares / Race additions
  double netDiffRaw = 0;
  double bestDiffRaw = 0;
  double poolDiff = 1;
  uint32_t nowUtc = 0;
  uint32_t seq = 0;
  uint32_t blockSec = 0; // since network's last block
  String tries;          // your guesses since then
  char coin[8] = "BTC";
  int nShares = 0;
  ShareRec shares[SHARE_HIST_N];
};

// Panels under the half-circle dial.
//   Portrait : one box 6,122 228x196:
//              top: Closest Try & Guesses (y=124..160, h=36)
//              spd: 5m Speed Microchart (y=163..215, h=52)
//              shares: Multifunctional Shares Ladder (y=218..316, h=98)
//   Landscape: under the dial (4,122 200x116) has 5m microchart over shares ladder,
//              right column 208,26 108x212 has closest try over guesses over race
static Rect mineChartRect() { return isPortrait() ? Rect{6, 96, 228, 222} : Rect{4, 122, 200, 116}; }
static Rect mineChart2Rect() { return isPortrait() ? Rect{0, 0, 0, 0} : Rect{208, 26, 108, 212}; }
struct MinePanelG
{
  Rect top;    // Closest Try (and Guesses in portrait)
  Rect spd;    // 5m Speed Microchart
  Rect shares; // Multifunctional Shares Ladder
  Rect gss;    // Guesses (landscape right column)
  Rect race;   // Last Block race (landscape right column)
  int sep1Y, sep2Y; // separators in panel 1
  int divX;         // vertical divider in portrait row 1
  int sepCol1Y, sepCol2Y; // separators in landscape right column
};
static MinePanelG minePanelG()
{
  if (isPortrait())
    return {
        Rect{0, 0, 0, 0},       // top: Closest Try + Guesses removed per user request
        Rect{0, 0, 0, 0},       // spd: removed (5m speed microchart removed per user request)
        Rect{8, 96, 224, 222}, // shares: RECENT ATTEMPTS (conveyor strip sits above, y 88..120)
        Rect{0, 0, 0, 0},
        Rect{0, 0, 0, 0},
        -1, -1, -1,            // sep1Y, sep2Y, divX (none)
        -1, -1
    };
  return {
      Rect{0, 0, 0, 0},         // top: Closest Try removed per user request
      Rect{6, 124, 196, 52},    // spd: 5m Microchart under dial
      Rect{6, 180, 196, 56},    // shares: Shares Ladder under microchart
      Rect{0, 0, 0, 0},         // gss: Guesses removed per user request
      Rect{210, 30, 104, 206},  // race: Last Block race fills the right column
      178, -1, -1,              // sep1Y, sep2Y, divX
      -1, -1                    // sepCol1Y, sepCol2Y (none)
  };
}

static void chromeMine(TFT_eSPI &d)
{
  if (!shotOn)
    memset(segLvl, 0xFF, sizeof(segLvl)), memset(rndLvl, 0xFF, sizeof(rndLvl)), memset(hexShown, 0, sizeof(hexShown));
  gaugeDraw(d, gaugeShown);
  roundRingDraw(d, roundShown, winnerZeros());
  const Rect c = mineChartRect();
  const MinePanelG g = minePanelG();
  panel(d, c.x, c.y, c.w, c.h, 6, C_BG, C_EDGE, C_BG); // black inside, thin gray border
  const Rect c2 = mineChart2Rect();
  if (c2.w > 0) panel(d, c2.x, c2.y, c2.w, c2.h, 6, C_BG, C_EDGE, C_BG);
  if (g.sep1Y > 0) d.drawFastHLine(c.x + 6, dY(g.sep1Y), c.w - 12, C_EDGE);
  if (g.sep2Y > 0) d.drawFastHLine(c.x + 6, dY(g.sep2Y), c.w - 12, C_EDGE);
  if (g.divX > 0) d.drawFastVLine(g.divX, dY(g.top.y + 3), g.top.h - 6, C_EDGE);
  if (g.sepCol1Y > 0) d.drawFastHLine(c2.x + 6, dY(g.sepCol1Y), c2.w - 12, C_EDGE);
  if (g.sepCol2Y > 0) d.drawFastHLine(c2.x + 6, dY(g.sepCol2Y), c2.w - 12, C_EDGE);
}

// ---- data, sampled once a second on any screen (display task, no I/O) ----
// Speed: 60 x 1 s samples (0.1 KH/s units) from the miner's KH counter.
// Records: the last 3 times my best leading-zero count this round went UP.
extern uint32_t totalKHashes;
#define HR_N 300 // 5 minutes of ~1 Hz speed samples (0.1 KH/s each)
#define HR_RM 8   // chart line: rolling mean over the last 8 samples (~8 s)
static uint16_t hrS[HR_N];
static uint16_t hrHead = 0, hrCount = 0;
// i-th newest sample (0 = newest); i < hrCount
static inline uint16_t hrBack(int i) { return hrS[(hrHead + HR_N - 1 - i) % HR_N]; }
// Held average of the window (0.1 KH/s; 0 = none yet): the panel's header and
// dashed line. Taken once 10 samples exist, then refreshed every 60 samples so
// it settles instead of jittering. Also the reference for the outlier filter.
static float hrAvgHeld = 0.0f;
static uint32_t hrAvgAt = 0; // hrEpoch when it was taken
static uint32_t hrEpoch = 0;
#define REC_N 3
static uint32_t recT[REC_N];
static uint8_t recZ[REC_N];
static uint8_t recHead = 0, recCount = 0;
static int recBest = 0;      // best zeros seen this round (0 = none yet)
static uint32_t recEpoch = 0; // bumps on a new record or a new round
static int bestZerosNow()
{
  uint8_t bh[32];
  if (!getRoundBestHash(bh)) return 0;
  int z = 0;
  for (int i = 0; i < 32; i++)
  {
    if (bh[i] == 0) { z += 2; continue; }
    if (bh[i] < 0x10) z++;
    break;
  }
  return z;
}
static void panelSample()
{
  static uint32_t lastMs = 0, lastKh = 0, lastBlk = 0;
  const uint32_t now = millis();
  // speed: one sample per miner stats tick (about 1 Hz), timed with the tick's
  // own timestamp, not when this loop happens to notice it (that lag made
  // +/-50% spikes)
  const uint32_t s1 = g_khTickSeq;
  const uint32_t kh = g_khTickKh, tickMs = g_khTickMs;
  if (!(s1 & 1) && s1 == g_khTickSeq && kh != lastKh)
  {
    const uint32_t dt = tickMs - lastMs;
    // The first few ticks after boot, and while the miner is reconnecting /
    // has no job (plus a few ticks after it resumes), are not a real rate.
    static uint8_t warm = 0, odd = 0;
    uint32_t v = (lastMs && kh > lastKh && dt >= 500UL && dt <= 5000UL)
                     ? (uint32_t)(((uint64_t)(kh - lastKh) * 10000ULL) / dt) // 0.1 KH/s
                     : 0;
    if (!getMinerStatus().subscribed || lastJobAgeSec() < 0) { warm = 0; v = 0; }
    if (v && warm < 8) { warm++; v = 0; }
    // one-off outliers (> 2x or < 0.5x the held average) would stretch the
    // 5 min band; skip them, but accept after 10 in a row (a real change)
    if (v && hrAvgHeld > 0.0f && (v > 2.0f * hrAvgHeld || v < 0.5f * hrAvgHeld))
    {
      if (++odd < 10) v = 0;
      else { odd = 0; hrAvgHeld = 0.0f; } // re-take the average from new samples
    }
    else if (v) odd = 0;
    if (v >= 1000) // 100 KH/s floor
    {
      hrS[hrHead] = v > 65535 ? 65535 : (uint16_t)v;
      hrHead = (hrHead + 1) % HR_N;
      if (hrCount < HR_N) hrCount++;
      hrEpoch++;
      if (hrAvgHeld <= 0.0f ? hrCount >= 10 : hrEpoch - hrAvgAt >= 60)
      {
        uint32_t sum = 0;
        for (int i = 0; i < hrCount; i++) sum += hrS[i];
        hrAvgHeld = (float)sum / hrCount;
        hrAvgAt = hrEpoch;
      }
    }
    lastKh = kh;
    lastMs = tickMs;
  }
  // a new block (the prev-block hash changed): the round starts over
  const uint32_t nb = g_newBlockMs;
  if (nb != lastBlk)
  {
    lastBlk = nb;
    recCount = 0;
    recHead = 0;
    recBest = 0;
    recEpoch++;
  }
  const int z = bestZerosNow();
  if (z > recBest)
  { // strictly better than this round's best so far: a record
    recBest = z;
    recT[recHead] = now;
    recZ[recHead] = (uint8_t)z;
    recHead = (recHead + 1) % REC_N;
    if (recCount < REC_N) recCount++;
    recEpoch++;
  }
}

// Guesses this round / what a typical block (blockTargetSecs) takes at the average
// speed (last 60 s; the dial reading until samples exist). 1.0 = one block's worth.
static float roundRatio(float khFallback)
{
  uint32_t sum = 0; // last 60 s only (the history buffer holds 5 min)
  const int n60 = hrCount < 60 ? hrCount : 60;
  for (int i = 0; i < n60; i++) sum += hrBack(i);
  const float kh = n60 ? (float)sum / n60 / 10.0f : khFallback;
  if (!(kh > 1.0f)) return 0.0f;
  return (float)((double)g_currentBlockHashes / ((double)kh * 1000.0 * (double)blockTargetSecs()));
}
static float roundFill(float khFallback)
{
  const float r = roundRatio(khFallback);
  return r >= 1.0f ? (float)HEX_N : r * HEX_N;
}

static void fmtAge(uint32_t s, char *b, size_t n)
{
  if (s < 3) snprintf(b, n, "now");
  else if (s < 60) snprintf(b, n, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(b, n, "%lum", (unsigned long)(s / 60));
  else snprintf(b, n, "%luh", (unsigned long)(s / 3600));
}

// The panel's header row also carries the round news, so no strip under
// the dial is kept empty for it: normally "5m AVG 982 KH/s" (+ "1.4x AVG
// ROUND" on the right in a long round); for 5 s after a network block it
// shows "NEW #N" (green) and "ZEROS RESET".
static void drawSpeedPanel(const Rect &r, float khNow, const String &news, uint16_t newsCol,
                           const String &note, uint16_t noteCol)
{
  const bool P = isPortrait() || r.w > 150; // wide box: one-line header
  (void)khNow;
  char k[96];
  snprintf(k, sizeof(k), "s|%lu|%d|%s|%s", (unsigned long)hrEpoch, P ? 1 : 0, news.c_str(), note.c_str());
  if (!need(W_CHART, k))
    return;
  const float avgHeld = hrAvgHeld; // held in panelSample()
  const bool haveAvg = avgHeld > 0.0f;
  // Fixed band: anchor +/-15%. The anchor is the held average and only
  // re-centers when that average drifts more than 5% from it, so the top and
  // bottom stay put and the line moves inside them. Samples outside the band
  // are clipped to the edge and marked with a caret there (spikes stay
  // visible but never stretch the scale). Units: 0.1 KH/s.
  static float anchor = 0.0f;
  float anc = anchor;
  if (haveAvg)
  {
    if (anchor <= 0.0f || fabsf(avgHeld - anchor) > 0.05f * anchor) anchor = avgHeld;
    anc = anchor;
  }
  else if (hrCount)
  { // before the first held average: centre on what we have (not stored)
    uint32_t sum = 0;
    for (int i = 0; i < hrCount; i++) sum += hrS[i];
    anc = (float)sum / hrCount;
  }
  const int hi = (int)lroundf(anc * 1.15f), lo = (int)lroundf(anc * 0.85f);
  const int x0 = r.x + 4, x1 = r.x + r.w - 5;
  const int pTop = r.y + 17, pBot = r.y + r.h - 5; // microchart band (header row above)
  const int mid = (pTop + pBot) / 2;
  banded(r.x, r.y, r.w, r.h, C_BG, [&] {
    TFT_eSPI &d = out();
    d.fillRect(r.x, dY(r.y), r.w, r.h, C_BG);
    char t[24];
    if (news.length())
    { // a network block just landed: the header row becomes the news
      unsigned ns = 14;
      const int nw = (x1 - x0) - textW("ZEROS RESET", 10) - 8;
      while (ns > 10 && textW(news.c_str(), ns) + 1 > nw) ns--;
      text(d, news.c_str(), x0, dY(r.y), ns, newsCol, C_BG);
      text(d, news.c_str(), x0 + 1, dY(r.y), ns, newsCol, C_BG); // bolder stroke
      text(d, "ZEROS RESET", x1, dY(r.y + 2), 10, C_TEXT, C_BG, Align::TopRight);
    }
    else
    {
      const char *lbl = "5m AVG"; // the held average; the dial already shows the live number
      text(d, lbl, x0, dY(r.y + 2), 10, C_DIM, C_BG);
      if (haveAvg) snprintf(t, sizeof(t), "%d KH/s", (int)(avgHeld / 10.0f + 0.5f));
      else snprintf(t, sizeof(t), "-- KH/s");
      text(d, t, x0 + 44, dY(r.y), 13, C_METER, C_BG);
      if (note.length())
      {
        unsigned ts = 10;
        const int room = (x1 - x0) - (44 + textW(t, 13) + 8);
        while (ts > 8 && textW(note.c_str(), ts) > room) ts--;
        text(d, note.c_str(), x1, dY(r.y + 2), ts, noteCol, C_BG, Align::TopRight);
      }
    }
    // thin rule under the header row, like the panel's other separators
    d.drawFastHLine(x0 + 2, dY(r.y + 15), x1 - x0 - 4, mix565(C_BG, C_EDGE, 160));
    const uint16_t guide = mix565(C_BG, C_EDGE, 200);
    if (hrCount < 2)
    {
      for (int x = x0; x <= x1; x += 4)
      {
        d.drawPixel(x, dY(pTop), guide);
        d.drawPixel(x, dY(pBot), guide);
      }
      caption(d, "collecting...", x0, dY(mid - 5), x1 - x0, Align::TopLeft, C_BG);
      return;
    }
    // right gutter: the band's real top / bottom in KH/s
    const int gx1 = x1 - (P ? 24 : 22); // plot's right edge
    char tl[8];
    snprintf(tl, sizeof(tl), "%d", (int)lroundf(anc * 0.115f)); // KH/s
    text(d, tl, x1, dY(pTop - 4), 10, C_DIM, C_BG, Align::TopRight);
    snprintf(tl, sizeof(tl), "%d", (int)lroundf(anc * 0.085f));
    text(d, tl, x1, dY(pBot - 5), 10, C_DIM, C_BG, Align::TopRight);
    for (int x = x0; x <= gx1; x += 4) // faint top / bottom guides
    {
      d.drawPixel(x, dY(pTop), guide);
      d.drawPixel(x, dY(pBot), guide);
    }
    const int bandH = pBot - pTop;
    auto Y = [&](float v) {
      int y = pBot - (int)lroundf((v - lo) * bandH / (float)(hi - lo));
      if (y < pTop) y = pTop;
      if (y > pBot) y = pBot;
      return y;
    };
    // fixed 5 min axis, oldest .. newest; a short history grows from the right
    const int n = hrCount, span = HR_N - 1, w = gx1 - x0;
    auto X = [&](int i) { return gx1 - (int)((long)(n - 1 - i) * w / span); };
    // The line is a trailing rolling mean of the last HR_RM samples (~8 s):
    // calm enough that one slow / fast second doesn't spike it, short enough
    // that a real change shows within a few seconds. Oldest .. newest.
    static uint16_t sm[HR_N];
    {
      uint32_t run = 0;
      for (int i = 0; i < n; i++)
      {
        run += hrS[(hrHead + HR_N - n + i) % HR_N];
        if (i >= HR_RM) run -= hrS[(hrHead + HR_N - n + i - HR_RM) % HR_N];
        const int cnt = i + 1 < HR_RM ? i + 1 : HR_RM;
        sm[i] = (uint16_t)((run + cnt / 2) / cnt);
      }
    }
    // soft area under the line: per pixel column, down from its highest sample
    const uint16_t fill = mix565(C_BG, C_METER, 55);
    int cx = -1, cy = pBot;
    for (int i = 0; i < n; i++)
    {
      const int x = X(i), y = Y(sm[i]);
      if (x != cx)
      {
        if (cx >= 0 && cy < pBot) d.drawFastVLine(cx, dY(cy), pBot - cy, fill);
        cx = x;
        cy = y;
      }
      else if (y < cy) cy = y;
    }
    if (cx >= 0 && cy < pBot) d.drawFastVLine(cx, dY(cy), pBot - cy, fill);
    int px = -1, py = 0, lastCx = -99;
    for (int i = 0; i < n; i++)
    {
      const uint16_t v = sm[i];
      const int x = X(i), y = Y(v);
      if (px >= 0) d.drawLine(px, dY(py), x, dY(y), C_METER);
      if ((v > hi || v < lo) && x - lastCx >= 3) // clipped: caret at that edge
      {
        if (v > hi) d.fillTriangle(x - 2, dY(pTop + 3), x + 2, dY(pTop + 3), x, dY(pTop - 1), C_TEXT);
        else d.fillTriangle(x - 2, dY(pBot - 3), x + 2, dY(pBot - 3), x, dY(pBot + 1), C_TEXT);
        lastCx = x;
      }
      px = x;
      py = y;
    }
    d.fillCircle(px, dY(py), 2, C_METER); // now (tip of the rolling mean)
    text(d, "now", gx1, dY(pBot - 6), 10, C_METER, C_BG, Align::TopRight);
  });
  done(W_CHART, k);
}

static void diamond(TFT_eSPI &d, int x, int y, int s, uint16_t c)
{
  d.fillTriangle(x - s, dY(y), x + s, dY(y), x, dY(y - s), c);
  d.fillTriangle(x - s, dY(y), x + s, dY(y), x, dY(y + s), c);
}

// Closest try + guesses this round
static void drawClosestTry(const Rect &r, const MineV &m)
{
  const bool P = isPortrait();
  const bool netKnown = m.netDiffRaw > 0;
  const double net = netKnown ? m.netDiffRaw : 1e6;
  double best = m.bestDiffRaw;
  for (int i = 0; i < m.nShares; i++)
    if (m.shares[i].diff > best) best = m.shares[i].diff;

  char bestS[16], netS[16], needS[16];
  fmtDiff(best, bestS, sizeof(bestS));
  if (netKnown) fmtDiff(net, netS, sizeof(netS));
  else snprintf(netS, sizeof(netS), "--");
  const bool haveBest = best > 0 && netKnown;
  fmtDiff(haveBest ? net / best : 0, needS, sizeof(needS));

  const double poolFloor = m.poolDiff > 0 ? m.poolDiff : 1.0;
  double floorD = poolFloor;
  for (int i = 0; i < m.nShares; i++)
    if (m.shares[i].diff > 0 && m.shares[i].diff < floorD) floorD = m.shares[i].diff;
  const double lo = log10(floorD);
  const double hi = log10(net > floorD ? net : floorD * 10.0);

  char k[128];
  snprintf(k, sizeof(k), "ct|%s|%s|%s|%s|%s|%d", bestS, netS, needS, m.guesses.c_str(), m.rA.c_str(), P ? 1 : 0);
  if (!need(W_ARCH_TOP, k))
    return;

  banded(r.x, r.y, r.w, r.h, C_BG, [&] {
    TFT_eSPI &d = out();
    d.fillRect(r.x, dY(r.y), r.w, r.h, C_BG);
    const int x0 = r.x + 4;
    const int y0 = r.y;

    if (P)
    {
      // Left 65% (x=8..150): Closest Try (or network block toast)
      if (m.rA.length())
      {
        text(d, m.rA.c_str(), x0, dY(y0 + 2), 12, m.rACol, C_BG);
        text(d, m.rB.c_str(), 148, dY(y0 + 2), 10, m.rBCol, C_BG, Align::TopRight);
      }
      else
      {
        text(d, "CLOSEST TRY", x0, dY(y0 + 2), 10, C_DIM, C_BG);
        if (haveBest)
        {
          char l1[24];
          if (best >= net) snprintf(l1, sizeof(l1), "TARGET MET");
          else snprintf(l1, sizeof(l1), "1 in %s", needS);
          text(d, l1, 148, dY(y0 + 2), 10, best >= net ? C_GOLD : C_TEXT, C_BG, Align::TopRight);
        }
        else
        {
          text(d, "no shares", 148, dY(y0 + 2), 10, C_DIM, C_BG, Align::TopRight);
        }
      }

      // Row 2: score + need
      const unsigned vs = 17;
      text(d, best > 0 ? bestS : "--", x0, dY(y0 + 15), vs, C_BEST, C_BG);
      if (netKnown)
      {
        char of[24];
        if (best >= net) snprintf(of, sizeof(of), "target %s", netS);
        else snprintf(of, sizeof(of), "need %s", netS);
        int ox = x0 + textW(best > 0 ? bestS : "--", vs) + 6;
        text(d, of, ox, dY(y0 + 19), 10, C_DIM, C_BG);
      }

      // Row 3: wide sleek progress bar
      ladderBar(d, x0, y0 + 35, 148 - x0, 4, haveBest ? ladderFrac(best, lo, hi) : 0, C_BEST);

      // Right 35% (x=156..230): Guesses this round in C_GUESS blue
      const int cxG = 193;
      text(d, m.guesses.c_str(), cxG, dY(y0 + 5), 18, C_GUESS, C_BG, Align::TopCenter);
      text(d, "GUESSES", cxG, dY(y0 + 25), 10, C_DIM, C_BG, Align::TopCenter);
    }
    else
    {
      // Landscape: right column card (w=104, h=58)
      text(d, "CLOSEST TRY", x0, dY(y0 + 2), 10, C_DIM, C_BG);
      const unsigned vs = 17;
      text(d, best > 0 ? bestS : "--", x0, dY(y0 + 13), vs, C_BEST, C_BG);
      if (haveBest)
      {
        char l1[24];
        if (best >= net) snprintf(l1, sizeof(l1), "TARGET MET");
        else snprintf(l1, sizeof(l1), "1 in %s", needS);
        text(d, l1, r.x + r.w - 4, dY(y0 + 15), 10, best >= net ? C_GOLD : C_TEXT, C_BG, Align::TopRight);
      }
      if (netKnown)
      {
        char of[24];
        if (best >= net) snprintf(of, sizeof(of), "target %s", netS);
        else snprintf(of, sizeof(of), "need %s", netS);
        text(d, of, x0, dY(y0 + 33), 10, C_DIM, C_BG);
      }
      ladderBar(d, x0, y0 + 47, r.w - 8, 4, haveBest ? ladderFrac(best, lo, hi) : 0, C_BEST);
    }
  });
  done(W_ARCH_TOP, k);
}

// Multifunctional Shares Figure: 6 signals in one module
// 1. Share difficulty (dot height on log10 ladder)
// 2. Best share (magenta circle + outer ring)
// 3. Leading-zero records (magenta diamonds on the ladder rail)
// 4. Block target (gold NEED line + trophy icon)
// 5. Share count & cadence (caption)
// 6. Block odds (caption)
// RECENT ATTEMPTS: three lanes MISS | SHARE | BLOCK, time falls down (NOW at top).
//  MISS  = real sampled attempts below the pool share target (mining.h sampler:
//          uniform SW tap = dim dots, 16-bit prefilter near-misses = bright).
//          x = log difficulty from the smallest possible (2^-32) to pool diff.
//  SHARE = actual shares the pool answered (share log); hollow = pool refused.
//          x = log(diff/pool) up to the network target.
//  BLOCK = share-log hashes >= network target; x = log(diff/net) over 16x.
// Bins: MISS = attempts since boot - shares found; SHARE = shares found
// (accepted shown when different); BLOCK = valid blocks.
// Redraw budget for RECENT ATTEMPTS (checked by scripts/check_mining_starvation.py).
// Redrawing this figure too often starves the stratum/job feed and the miner
// (measured: ~5 redraws/s dropped ~1000 KH/s to ~690). Do not lower these.
#define ATT_DOTS_MIN_MS 1000     // dot layer (text-free): at most once per second
#define ATT_MISS_BIN_MIN_MS 5000 // MISS bin counter: at most every 5 s
static uint8_t g_attWin = 0; // 0=15s 1=30s 2=60s (tab row; touch-selectable)
static const uint16_t kAttWinSec[3] = {15, 30, 60};
static Rect attTabRect(int i)
{
  const Rect r = mineChartRect();
  const int tw = 28, gap = 3;
  const int x = r.x + r.w - 4 - (3 - i) * tw - (2 - i) * gap;
  return Rect{x, r.y + 2, tw, 14};
}
static void fmtCompact(double v, char *out, size_t n)
{
  if (v < 10000.0) snprintf(out, n, "%lu", (unsigned long)(v + 0.5));
  else if (v < 1e6) snprintf(out, n, "%.1fK", v / 1e3);
  else if (v < 1e9) snprintf(out, n, "%.1fM", v / 1e6);
  else if (v < 1e12) snprintf(out, n, "%.1fB", v / 1e9);
  else snprintf(out, n, "%.1fT", v / 1e12);
}

static void drawSharesFigure(const Rect &r, const MineV &m)
{
  const bool P = isPortrait();

  // Samples read directly here (not carried in MineV): keeps the paint struct
  // small so the screenshot path doesn't overflow the web task stack.
  static HashSampleRec samp[HASH_SAMP_N];
  const uint32_t nowMs = millis();

  const bool netKnown = m.netDiffRaw > 0;
  const double poolD = m.poolDiff > 0 ? m.poolDiff : 0.0008;
  double netD = netKnown ? m.netDiffRaw : poolD * 1e6;
  if (netD <= poolD) netD = poolD * 16.0;

  char netS[16], poolS[16];
  if (netKnown) fmtDiff(netD, netS, sizeof(netS)); else snprintf(netS, sizeof(netS), "net");
  fmtDiff(poolD, poolS, sizeof(poolS));

  const uint32_t win = kAttWinSec[g_attWin < 3 ? g_attWin : 0];
  const double LMIN = -9.632959861; // log10(2^-32): smallest possible difficulty
  const double lPool = log10(poolD), lNet = log10(netD);
  const uint16_t tapCol = mix565(C_BEST, C_BG, 110);
  const uint16_t candCol = C_BEST;

  // PERF: the plot is split into three layers with separate keys. Measured:
  // repainting the whole 228x222 figure (all its text) every second starved
  // the stratum/job feed (chunk gaps ~28M cycles, ~690 KH/s vs ~930 without).
  //  frame : titles, tabs, lane headers, age labels -- only on threshold/window change
  //  dots  : text-free point layer -- at most once per second
  //  bins  : counters -- on share/block change, MISS value at most every 5 s
  char kf[96];
  snprintf(kf, sizeof(kf), "rf3|%s|%s|%u|%d|%u", netS, poolS, (unsigned)recEpoch, P ? 1 : 0, (unsigned)win);
  const bool frame = need(W_PROTO_PLOT, kf);

  if (!P)
  {
    char kl[48];
    snprintf(kl, sizeof(kl), "%s|%lu", kf, (unsigned long)(nowMs / (2 * ATT_DOTS_MIN_MS)));
    if (!need(W_PROTO_PLOT, kl)) return;
    const int nSamp = getHashSamples(samp, HASH_SAMP_N);
    banded(r.x, r.y, r.w, r.h, C_BG, [&] {
      TFT_eSPI &d = out();
      d.fillRect(r.x, dY(r.y), r.w, r.h, C_BG);
      // Compact landscape fallback (device runs portrait): misses only, by time.
      const int x0 = r.x + 6, x1 = r.x + r.w - 6, yT = r.y + 6, yB = r.y + r.h - 6;
      for (int i = 0; i < nSamp; i++)
      {
        const bool cand = samp[i].diff < 0;
        const double dv = cand ? -samp[i].diff : samp[i].diff;
        const uint32_t age = nowMs - samp[i].ms;
        if (age > win * 1000u || dv >= poolD || !(dv > 0)) continue;
        double t = (log10(dv) - LMIN) / (lPool - LMIN); if (t < 0) t = 0; if (t > 1) t = 1;
        int x = x0 + (int)(t * (x1 - x0));
        int y = yT + (int)((uint64_t)age * (yB - yT) / (win * 1000u));
        d.fillRect(x, dY(y), 2, 2, cand ? candCol : tapCol);
      }
    });
    done(W_PROTO_PLOT, kl);
    return;
  }

  // ---- portrait geometry (shared by the layers) ----
  const int ageW = 22;
  const int x0 = r.x + 2;
  const int x1 = r.x + r.w - ageW;
  const int colW = (x1 - x0) / 3;
  const int cratesH = 54;
  const int cratesTop = r.y + r.h - cratesH - 2;
  const int yHdr = r.y + 21;
  const int yTop = yHdr + 26; // NOW
  const int yBot = cratesTop - 9;
  const int H = yBot - yTop;
  const int pad = 4;
  const int yMid = (yTop + yBot) / 2;
  auto laneX = [&](int lane, double t) -> int {
    if (t < 0) t = 0; if (t > 1) t = 1;
    const int lx0 = x0 + lane * colW + pad, lw = colW - 2 * pad;
    return lx0 + (int)(t * lw);
  };
  auto ageY = [&](double ageSec) -> int {
    int y = yTop + (int)(ageSec * H / win);
    if (y < yTop) y = yTop; if (y > yBot) y = yBot;
    return y;
  };

  // ================= frame layer =================
  if (frame)
  {
    banded(r.x, r.y, r.w, yTop - 1 - r.y, C_BG, [&] {
      TFT_eSPI &d = out();
      d.fillRect(r.x, dY(r.y), r.w, yTop - 1 - r.y, C_BG);
      text(d, "RECENT ATTEMPTS", r.x + 4, dY(r.y + 4), 10, C_CYAN, C_BG);
      for (int i = 0; i < 3; i++)
      {
        const Rect t = attTabRect(i);
        char lb[6]; snprintf(lb, sizeof(lb), "%us", (unsigned)kAttWinSec[i]);
        const bool sel = i == g_attWin;
        if (sel) d.fillRoundRect(t.x, dY(t.y), t.w, t.h, 3, mix565(C_CYAN, C_BG, 70));
        d.drawRoundRect(t.x, dY(t.y), t.w, t.h, 3, sel ? C_CYAN : mix565(C_CYAN, C_BG, 90));
        text(d, lb, t.x + t.w / 2, dY(t.y + 3), 9, sel ? C_TEXT : mix565(C_CYAN, C_BG, 60),
             sel ? mix565(C_CYAN, C_BG, 70) : C_BG, Align::TopCenter);
      }
      const uint16_t colC[3] = {C_BEST, C_CYAN, C_GOLD};
      const char *colN[3] = {"MISS", "SHARE", "BLOCK"};
      char th[3][20];
      snprintf(th[0], sizeof(th[0]), "<%s", poolS);
      snprintf(th[1], sizeof(th[1]), ">=%s", poolS);
      snprintf(th[2], sizeof(th[2]), ">=%s", netS);
      for (int c = 0; c < 3; c++)
      {
        const int cx = x0 + c * colW + colW / 2;
        text(d, colN[c], cx, dY(yHdr), 10, colC[c], C_BG, Align::TopCenter);
        unsigned ts = 9;
        while (ts > 7 && textW(th[c], ts) > colW - 2) ts--;
        text(d, th[c], cx, dY(yHdr + 11), ts, colC[c], C_BG, Align::TopCenter);
      }
      d.drawFastHLine(x0, dY(yTop - 3), x1 - x0, mix565(C_DIM, C_BG, 160));
      for (int c = 1; c < 3; c++)
        for (int y = yHdr; y < yTop - 4; y += 5) d.drawFastVLine(x0 + c * colW, dY(y), 2, C_DIM);
      text(d, "NOW", r.x + r.w - 1, dY(yTop - 14), 9, C_TEXT, C_BG, Align::TopRight);
    });
    // age labels (right gutter, beside the dots layer)
    banded(x1 + 1, yTop - 1, r.x + r.w - x1 - 1, yBot - yTop + 2, C_BG, [&] {
      TFT_eSPI &d = out();
      d.fillRect(x1 + 1, dY(yTop - 1), r.x + r.w - x1 - 1, yBot - yTop + 2, C_BG);
      const uint16_t ageCol = C_TEXT;
      char midL[8], botL[8];
      snprintf(midL, sizeof(midL), "-%us", (unsigned)(win / 2));
      snprintf(botL, sizeof(botL), "-%us", (unsigned)win);
      text(d, midL, r.x + r.w - 1, dY(yMid - 4), 9, ageCol, C_BG, Align::TopRight);
      text(d, botL, r.x + r.w - 1, dY(yBot - 11), 9, ageCol, C_BG, Align::TopRight);
      for (int yy : {yTop, yMid, yBot}) d.drawFastHLine(x1 + 1, dY(yy), 3, C_DIM);
    });
    // gap between dots and bins
    banded(r.x, yBot + 1, r.w, cratesTop - 3 - yBot - 1, C_BG, [&] {
      TFT_eSPI &d = out();
      d.fillRect(r.x, dY(yBot + 1), r.w, cratesTop - 3 - yBot - 1, C_BG);
    });
    done(W_PROTO_PLOT, kf);
    wkey[W_ATT_DOTS] = "";
    wkey[W_ATT_BINS] = "";
  }

  // ================= dots layer (no text) =================
  char kd[40];
  snprintf(kd, sizeof(kd), "d|%lu|%u", (unsigned long)(nowMs / ATT_DOTS_MIN_MS), (unsigned)win);
  if (need(W_ATT_DOTS, kd))
  {
    const int nSamp = getHashSamples(samp, HASH_SAMP_N);
    banded(x0, yTop - 1, x1 - x0 + 1, yBot - yTop + 2, C_BG, [&] {
      TFT_eSPI &d = out();
      d.fillRect(x0, dY(yTop - 1), x1 - x0 + 1, yBot - yTop + 2, C_BG);
      for (int c = 1; c < 3; c++)
        for (int y = yTop; y < yBot; y += 5) d.drawFastVLine(x0 + c * colW, dY(y), 2, C_DIM);
      // MISS lane: faint tick where the 16-bit prefilter starts (2^-16):
      // left of it = uniform taps, right = real near-misses.
      const int xp = laneX(0, (-4.816479931 - LMIN) / (lPool - LMIN));
      for (int y = yTop; y < yBot; y += 9) d.drawPixel(xp, dY(y), mix565(C_BEST, C_BG, 70));

      // MISS lane: real sampled attempts (never synthetic)
      for (int i = 0; i < nSamp; i++)
      {
        const bool cand = samp[i].diff < 0;
        const double dv = cand ? -samp[i].diff : samp[i].diff;
        const uint32_t ageMs = nowMs - samp[i].ms;
        if (ageMs > win * 1000u || dv >= poolD || !(dv > 0)) continue; // >= pool: it is a share (share log)
        const int x = laneX(0, (log10(dv) - LMIN) / (lPool - LMIN));
        const int y = ageY(ageMs / 1000.0);
        if (cand) d.fillRect(x - 1, dY(y - 1), 3, 3, candCol);
        else d.fillRect(x, dY(y), 2, 2, tapCol);
      }
      // SHARE / BLOCK lanes: the actual share log (hollow = pool refused)
      const uint32_t nowU = m.nowUtc;
      for (int i = 0; i < m.nShares; i++)
      {
        const ShareRec &s = m.shares[i];
        if (!s.utc || !nowU || s.diff <= 0) continue;
        const uint32_t age = nowU > s.utc ? nowU - s.utc : 0;
        if (age > win) continue;
        const bool blk = netKnown && s.diff >= netD;
        const double t = blk ? log10(s.diff / netD) / log10(16.0) : (log10(s.diff) - lPool) / (lNet - lPool);
        const int x = laneX(blk ? 2 : 1, t);
        const int y = ageY((double)age);
        const uint16_t c = blk ? C_GOLD : C_CYAN;
        if (s.accepted) d.fillCircle(x, dY(y), 2, c);
        else d.drawCircle(x, dY(y), 2, c);
      }
    });
    done(W_ATT_DOTS, kd);
  }

  // ================= bins layer =================
  const AttemptCounters ac = getAttemptCounters();
  char kb[64];
  snprintf(kb, sizeof(kb), "b|%lu|%lu|%lu|%lu", (unsigned long)ac.found, (unsigned long)ac.accepted,
           (unsigned long)ac.blocks, (unsigned long)(nowMs / ATT_MISS_BIN_MIN_MS));
  if (need(W_ATT_BINS, kb))
  {
    banded(r.x, cratesTop - 3, r.w, cratesH + 5, C_BG, [&] {
      TFT_eSPI &d = out();
      d.fillRect(r.x, dY(cratesTop - 3), r.w, cratesH + 5, C_BG);
      const int gap = 8;
      const int cw = (r.w - 4 * gap) / 3;
      const int cy0 = cratesTop;
      const int ch = cratesH;
      char vb[3][16];
      fmtCompact(ac.missSession, vb[0], sizeof(vb[0]));
      fmtCompact((double)ac.found, vb[1], sizeof(vb[1]));
      fmtCompact((double)ac.blocks, vb[2], sizeof(vb[2]));
      struct Crate { uint16_t col; const char *label; const char *val; char icon; };
      Crate cr[3] = {
        { C_BEST, "MISS",  vb[0], 'x' },
        { C_CYAN, "SHARE", vb[1], 'v' },
        { C_GOLD, "BLOCK", vb[2], 't' },
      };
      for (int c = 0; c < 3; c++)
      {
        int cx = r.x + gap + c * (cw + gap);
        uint16_t col = cr[c].col;
        uint16_t dim = mix565(col, C_BG, 70);
        d.fillRoundRect(cx, dY(cy0), cw, 8, 3, dim);
        d.drawRoundRect(cx, dY(cy0), cw, 8, 3, col);
        d.fillRoundRect(cx, dY(cy0 + 9), cw, ch - 9, 4, C_BG);
        d.drawRoundRect(cx, dY(cy0 + 9), cw, ch - 9, 4, col);
        d.fillRect(cx + cw / 2 - 4, dY(cy0 - 2), 8, 3, col);
        int mid = cx + cw / 2;
        int iy = cy0 + 12;
        if (c == 1 && ac.accepted != ac.found)
        {
          char ab[20], an[12];
          fmtCompact((double)ac.accepted, an, sizeof(an));
          snprintf(ab, sizeof(ab), "acc %s", an);
          text(d, ab, mid, dY(iy), 9, col, C_BG, Align::TopCenter);
        }
        else if (cr[c].icon == 'x')
        {
          d.drawLine(mid - 5, dY(iy), mid + 5, dY(iy + 8), col);
          d.drawLine(mid - 5, dY(iy + 8), mid + 5, dY(iy), col);
          d.drawLine(mid - 4, dY(iy), mid + 6, dY(iy + 8), col);
          d.drawLine(mid - 6, dY(iy + 8), mid + 4, dY(iy), col);
        }
        else if (cr[c].icon == 'v')
        {
          d.drawLine(mid - 6, dY(iy + 4), mid - 1, dY(iy + 9), col);
          d.drawLine(mid - 1, dY(iy + 9), mid + 7, dY(iy), col);
          d.drawLine(mid - 6, dY(iy + 5), mid - 1, dY(iy + 10), col);
          d.drawLine(mid - 1, dY(iy + 10), mid + 7, dY(iy + 1), col);
        }
        else
          drawTrophy(d, mid - 4, dY(iy), col);
        unsigned vs = 14;
        while (vs > 9 && textW(cr[c].val, vs) > cw - 6) vs--;
        text(d, cr[c].val, mid, dY(cy0 + 26), vs, col, C_BG, Align::TopCenter);
        int pw = textW(cr[c].label, 9) + 10;
        int px = mid - pw / 2;
        int py = cy0 + ch - 13;
        d.drawRoundRect(px, dY(py), pw, 12, 4, col);
        text(d, cr[c].label, mid, dY(py + 2), 9, col, C_BG, Align::TopCenter);
      }
    });
    done(W_ATT_BINS, kb);
  }
}

// Landscape right column card 2: Guesses
static void drawGuessesLandscape(const Rect &r, const MineV &m)
{
  String k = "gl|" + m.guesses;
  if (!need(W_MP_GSS, k))
    return;
  banded(r.x, r.y, r.w, r.h, C_BG, [&] {
    TFT_eSPI &d = out();
    d.fillRect(r.x, dY(r.y), r.w, r.h, C_BG);
    const int cx = r.x + r.w / 2;
    text(d, m.guesses.c_str(), cx, dY(r.y + 5), 18, C_GUESS, C_BG, Align::TopCenter);
    text(d, "GUESSES (ROUND)", cx, dY(r.y + 28), 10, C_DIM, C_BG, Align::TopCenter);
  });
  done(W_MP_GSS, k);
}

// Landscape right column card 3: Last Block Race & Odds
static void drawRaceLandscape(const Rect &r, const MineV &m)
{
  const bool netKnown = m.netDiffRaw > 0;
  const float kh = hrAvgHeld > 0.0f ? hrAvgHeld / 10.0f : m.kh;
  String waitV, waitU;
  fmtWait((netKnown && kh > 0) ? m.netDiffRaw * 4294967296.0 / (kh * 1000.0) : 0, waitV, waitU);
  const double wh = worldHs();
  String odds = (wh > 0 && kh > 0) ? ("1 in " + fmtCount(wh / (kh * 1000.0))) : String("--");

  char blkEvery[24];
  uint32_t t = blockTargetSecs();
  if (t % 60 == 0) snprintf(blkEvery, sizeof(blkEvery), "1 per %u min", (unsigned)(t / 60));
  else snprintf(blkEvery, sizeof(blkEvery), "1 per %u s", (unsigned)t);

  char k[96];
  snprintf(k, sizeof(k), "rl|%lu|%s|%s", (unsigned long)m.blockSec, m.tries.c_str(), waitV.c_str());
  if (!need(W_T2, k))
    return;

  banded(r.x, r.y, r.w, r.h, C_BG, [&] {
    TFT_eSPI &d = out();
    d.fillRect(r.x, dY(r.y), r.w, r.h, C_BG);
    const int x0 = r.x + 4;
    const int y0 = r.y;
    drawBlockIcon(d, x0, dY(y0 + 5), C_DIM);
    text(d, "LAST BLOCK", x0 + 18, dY(y0 + 4), 10, C_DIM, C_BG);
    text(d, fmtAgo(m.blockSec).c_str(), x0 + 18, dY(y0 + 17), 13, C_TEXT, C_BG);
    char tr[32];
    snprintf(tr, sizeof(tr), "you: %s", m.tries.c_str());
    text(d, tr, x0, dY(y0 + 35), 10, C_GUESS, C_BG);
    text(d, blkEvery, x0, dY(y0 + 48), 10, C_DIM, C_BG);
    d.drawFastHLine(x0, dY(y0 + 60), r.w - 8, C_EDGE);
    String wstr = "WAIT " + waitV + (waitU.length() ? " " + waitU : "");
    text(d, wstr.c_str(), x0, dY(y0 + 64), 10, C_TEXT, C_BG);
    String ostr = "ODDS " + odds;
    text(d, ostr.c_str(), x0, dY(y0 + 77), 10, C_METER, C_BG);
  });
  done(W_T2, k);
}

static void paintMine(const MineV &m)
{
  {
    HdrV h = m.hdr;
    h.screen = SCR_MINE;
    drawHeader(h);
  }
  const bool P = isPortrait();
  // Hero inside the ring: what the number is / value / unit in words.
  // In the dome of the half circle; the box ends 2 px above the centre line
  // and its top corners stay inside the inner ring (r 73 / 66).
  const int hw = P ? 78 : 80, hh = P ? 52 : 48;
  String k = m.hash + "|" + (P ? "P" : "L");
  if (need(W_HERO, k) && wBegin(hw, hh, C_BG))
  {
    int c = hw / 2;
    unsigned sz = P ? 36 : 32;
    while (sz > 20 && textW(m.hash.c_str(), sz) > hw - 2) sz--;
    text(wgt, m.hash.c_str(), c, 0, sz, C_TEXT, C_BG, Align::TopCenter);
    text(wgt, "KH/s", c, hh - 13, 12, C_METER, C_BG, Align::TopCenter);
    wEnd(G_CX - c, G_CY - hh - 2);
    done(W_HERO, k);
  }
  hexRingDraw(m.bestHex);
  const MinePanelG g = minePanelG();
  if (g.top.w > 0)
    drawClosestTry(g.top, m);
  if (g.spd.w > 0)
    drawSpeedPanel(g.spd, m.kh, m.rA, m.rACol, m.rA.length() ? String("") : m.rB, m.rBCol);
  drawSharesFigure(g.shares, m);
  if (!P)
  {
    if (g.gss.w > 0)
      drawGuessesLandscape(g.gss, m);
    drawRaceLandscape(g.race, m);
  }
}

static void fillMine(MineV &m, const String &rate, uint32_t sharesN, uint32_t blockSec = 0, double blockHashes = 0)
{
  (void)sharesN;
  m.hash = rate;
  m.kh = rate.toFloat();
  m.netDiffRaw = g_networkDifficulty;
  m.bestDiffRaw = getBestDiff();
  m.blockSec = blockSec;
  m.tries = fmtCount(blockHashes > 0 ? blockHashes : (double)g_currentBlockHashes);
  m.nShares = getShareHistory(m.shares, SHARE_HIST_N);
  m.seq = getShareHistorySeq();
  m.poolDiff = getPoolDifficulty();
  {
    char key[64];
    const char *c = "BTC";
    getActivePool(key, sizeof(key), &c);
    strncpy(m.coin, c, sizeof(m.coin) - 1);
  }
  time_t u = 0;
  m.nowUtc = (getUtcTime(u, false) && u > 1600000000) ? (uint32_t)u : 0;
  // proofs the pool checked: accepted / refused
  MinerStatus ms = getMinerStatus();
  m.okBad = String(ms.accepted) + " / " + String(ms.rejected);
  m.okCol = ms.rejected > 0 ? C_WARN : C_TEXT;
  // how the pool works: it keeps handing out fresh puzzles (jobs)
  String st;
  uint16_t sc;
  poolState(st, sc);
  const long age = lastJobAgeSec();
  if (st == "ONLINE" && age >= 0)
  {
    m.jobLabel = "NEW PUZZLE";
    m.job = fmtAgo((uint32_t)age);
    m.jobIcon = 6;
    m.jobCol = C_TEXT;
    m.jobICol = age < 3 ? C_METER : C_DIM; // piece fills orange for a moment when a job lands
  }
  else if (st == "REFUSED")
  {
    char perr[48];
    getPoolError(perr, sizeof(perr));
    const char *mm = strstr(perr, ": ");
    m.jobLabel = "REFUSED";
    m.job = String(mm ? mm + 2 : perr).substring(0, 14);
    m.jobIcon = 3;
    m.jobCol = m.jobICol = C_BAD;
  }
  else
  {
    m.jobLabel = "POOL";
    m.job = st == "ONLINE" ? String("LINKING...") : st + (st == "LINKING" ? "..." : "");
    m.jobIcon = 3;
    m.jobCol = m.jobICol = (sc == C_GOOD ? C_WARN : sc);
  }
  m.every = fmtEvery(getPoolDifficulty(), m.kh);
  m.flash = millis() < shareFlashUntil;
  m.guesses = fmtCount((double)g_currentBlockHashes);
  // this network round (since the last block anywhere)
  const bool P = isPortrait();
  char wb[24];
  if (g_lastWinnerDiff > 0) fmtDiff(g_lastWinnerDiff, wb, sizeof(wb));
  else snprintf(wb, sizeof(wb), "--");
  const String w = wb;
  const uint32_t nb = g_newBlockMs;
  if (nb && (millis() - nb) < 5000UL)
  { // a block was just found: the small lines become the news
    m.rA = "NEW #" + commas(String((unsigned long)g_lastWinnerHeight)); // network block, short = bigger
    m.rACol = C_GOOD;
    m.rB = "ZEROS RESET";
    m.rBCol = C_TEXT;
    m.rC = "";
  }
  else
  {
    // guesses this round now live in the bottom panel (same teal); the dial
    // center keeps only the speed so the number is not shown twice
    m.rA = "";
    m.rACol = C_GUESS;
    // a long round: the inner ring stays full; say how many typical blocks' worth
    const float rr = roundRatio(m.kh);
    if (rr >= 1.05f)
    {
      char ob[24];
      snprintf(ob, sizeof(ob), "%.1fx AVG ROUND", rr); // guesses this round vs a typical round
      m.rB = ob;
    }
    else
      m.rB = "";
    m.rBCol = C_GUESS;
    m.rC = "";
  }
  {
    uint8_t bh[32];
    m.bestHex = "";
    if (getRoundBestHash(bh))
    {
      char hx[21];
      for (int i = 0; i < 10; i++) snprintf(hx + 2 * i, 3, "%02x", bh[i]);
      m.bestHex = hx;
    }
    m.winZ = 0;
    while (m.winZ < 64 && g_lastWinnerHashHex[m.winZ] == '0') m.winZ++;
  }
}


#if CYD_WORLD_SCREEN
// =========================================================== WORLD screen ==
// You vs everyone (computed locally from the difficulty, never "waiting"),
// the puzzle, the chain height and halving, price/fee only when the fetch
// works (otherwise one honest line), and housekeeping in the strip.

struct WorldV
{
  HdrV hdr;
  double worldHs = 0;
  float kh = 0;
  String diff, height, eta, price, why, uptime, temp, blocks;
  long heightRaw = 0, remaining = 0;
  int pct = 0, fee = 0;
  uint16_t tempCol = 0;
};

struct WorldGeo
{
  Rect hero, c0, c1, halv, line;
};

static WorldGeo worldGeo()
{
  if (isPortrait())
    return {{6, 30, 228, 80}, {6, 114, 112, 44}, {122, 114, 112, 44}, {6, 162, 228, 86}, {6, 252, 228, 29}};
  return {{8, 30, 304, 56}, {8, 90, 150, 40}, {162, 90, 150, 40}, {8, 134, 304, 46}, {8, 183, 304, 16}};
}

static void chromeWorld(TFT_eSPI &d)
{
  const WorldGeo g = worldGeo();
  panel(d, g.hero.x, g.hero.y, g.hero.w, g.hero.h, 8, C_PANEL, C_EDGE, C_BG);
  panel(d, g.halv.x, g.halv.y, g.halv.w, g.halv.h, 8, C_PANEL, C_EDGE, C_BG);
  if (isPortrait())
    panel(d, g.line.x, g.line.y, g.line.w, g.line.h, 6, C_PANEL, C_EDGE, C_BG);
  chromeStrip3(d);
}

static void fillWorld(WorldV &w, float kh, const String &priceStr, int fee)
{
  const CoinProfile &coin = getActiveCoinProfile();
  w.kh = kh;
  w.worldHs = worldHs();
  w.diff = g_networkDifficulty > 0 ? formatNetworkDifficulty(g_networkDifficulty, true) : String("--");
  w.heightRaw = getBlockHeightValue();
  w.height = w.heightRaw > 0 ? ("#" + commas(String(w.heightRaw))) : String("--");
  if (w.heightRaw > 0)
  {
    const long hi = coin.halvingInterval;
    w.remaining = ((w.heightRaw / hi) + 1) * hi - w.heightRaw;
    w.pct = (int)((hi - w.remaining) * 100 / hi);
    w.eta = halvingEtaStr(w.remaining);
  }
  w.price = getBTCpriceValue() > 0 ? fmtPrice(priceStr) : String("--");
  w.fee = fee;
  w.why = getMarketError();
  w.uptime = uptimeNow();
  float tC = temperatureRead();
  w.temp = String((int)roundf(tC * 1.8f + 32.0f)) + "F";
  w.tempCol = tC >= 70 ? C_WARN : C_TEXT;
  w.blocks = String(valids);
}

static void paintWorld(const WorldV &w)
{
  {
    HdrV h = w.hdr;
    h.screen = SCR_WORLD;
    drawHeader(h);
  }
  const bool P = isPortrait();
  const WorldGeo g = worldGeo();
  const CoinProfile &coin = getActiveCoinProfile();

  // ---- Hero: you vs the world --------------------------------------------
  String ratio = (w.worldHs > 0 && w.kh > 0) ? ("1 in " + fmtCount(w.worldHs / (w.kh * 1000.0))) : String("--");
  char you[24], world[32];
  snprintf(you, sizeof(you), "you %.0f KH/s", w.kh);
  snprintf(world, sizeof(world), "all %s", w.worldHs > 0 ? formatNetworkHashrate(w.worldHs, true).c_str() : "--");
  String hk = ratio + "|" + you + "|" + world + (P ? "P" : "L");
  if (need(W_PRICE, hk))
  {
    const Rect r = g.hero;
    banded(r.x + 2, r.y + 2, r.w - 4, r.h - 4, C_PANEL, [&] {
      TFT_eSPI &d = out();
      d.fillRect(r.x + 2, dY(r.y + 2), r.w - 4, r.h - 4, C_PANEL);
      const int x0 = r.x + 8, x1 = r.x + r.w - 8;
      text(d, "YOUR SHARE OF ALL GUESSES", x0, dY(r.y + 4), 9, C_DIM, C_PANEL);
      text(d, ratio.c_str(), x0, dY(r.y + 15), P ? 22 : 20, C_METER, C_PANEL);
      text(d, you, x1, dY(r.y + (P ? 17 : 15)), 11, C_TEXT, C_PANEL, Align::TopRight);
      text(d, world, x1, dY(r.y + (P ? 31 : 28)), 11, C_DIM, C_PANEL, Align::TopRight);
      // the picture: everyone's guessing is the long bar; you are the dot
      const int by = r.y + (P ? 52 : 43), bh = P ? 10 : 8;
      const int bx = P ? x0 : x0 + 30;
      d.fillRoundRect(bx, dY(by), x1 - bx, bh, bh / 2, C_EDGE);
      d.fillCircle(bx + 1, dY(by + bh / 2), 2, C_METER);
      if (P)
      {
        text(d, "you", x0, dY(by + bh + 3), 9, C_METER, C_PANEL);
        caption(d, "everyone mining", x1, dY(by + bh + 3), 120, Align::TopRight);
      }
      else
        text(d, "you", x0, dY(by - 1), 9, C_METER, C_PANEL);
    });
    done(W_PRICE, hk);
  }

  // ---- Puzzle + chain --------------------------------------------------------
  drawStatCard(W_M0, g.c0.x, g.c0.y, g.c0.w, g.c0.h, "DIFFICULTY", w.diff.c_str(), "puzzle hardness", w.diff != "--");
  drawStatCard(W_M1, g.c1.x, g.c1.y, g.c1.w, g.c1.h, "BLOCK HEIGHT", w.height.c_str(), "blocks so far", w.heightRaw > 0);

  // ---- Halving + reward ------------------------------------------------------
  char kb[80];
  snprintf(kb, sizeof(kb), "%d|%ld|%s|%ld|%d", w.pct, w.remaining, w.eta.c_str(), w.heightRaw, P ? 1 : 0);
  if (need(W_HALV, kb))
  {
    const Rect r = g.halv;
    banded(r.x + 2, r.y + 2, r.w - 4, r.h - 4, C_PANEL, [&] {
      TFT_eSPI &d = out();
      d.fillRect(r.x + 2, dY(r.y + 2), r.w - 4, r.h - 4, C_PANEL);
      const bool known = w.heightRaw > 0;
      char pc[8];
      if (known) snprintf(pc, sizeof(pc), "%d%%", w.pct);
      else snprintf(pc, sizeof(pc), "--");
      int era = known ? (int)(w.heightRaw / coin.halvingInterval) : 4;
      float now = coin.initialSubsidy / (float)(1UL << era);
      String r0 = fmtReward(now), r1 = fmtReward(now / 2) + " " + coin.ticker;
      String rem = known ? commas(String(w.remaining)) + " blocks to go" : String("--");
      const int bx = r.x + 8, bw = r.w - 16, y0 = r.y;
      if (P)
      {
        text(d, "NEXT HALVING", bx, dY(y0 + 4), 10, C_DIM, C_PANEL);
        text(d, pc, bx + bw, dY(y0 + 2), 15, C_METER, C_PANEL, Align::TopRight);
        d.fillRoundRect(bx, dY(y0 + 21), bw, 8, 4, C_TRACK);
        int fw = (bw * w.pct) / 100;
        if (known && fw > 0) d.fillRoundRect(bx, dY(y0 + 21), fw, 8, 4, C_METER);
        text(d, rem.c_str(), bx, dY(y0 + 33), 11, C_TEXT, C_PANEL);
        if (w.eta.length()) text(d, w.eta.c_str(), bx + bw, dY(y0 + 33), 11, C_DIM, C_PANEL, Align::TopRight);
        text(d, "REWARD PER BLOCK", bx, dY(y0 + 54), 9, C_DIM, C_PANEL);
        drawRewardRightD(d, bx + bw, y0 + 52, 12, r0, r1);
        char hc[48];
        snprintf(hc, sizeof(hc), "halves every %s blocks", commas(String(coin.halvingInterval)).c_str());
        caption(d, hc, bx, dY(y0 + 69), bw);
      }
      else
      {
        text(d, "NEXT HALVING", bx, dY(y0 + 3), 10, C_DIM, C_PANEL);
        text(d, pc, bx + textW("NEXT HALVING", 10) + 8, dY(y0 + 1), 13, C_METER, C_PANEL);
        drawRewardRightD(d, bx + bw, y0 + 2, 11, r0, r1);
        d.fillRoundRect(bx, dY(y0 + 18), bw, 7, 3, C_TRACK);
        int fw = (bw * w.pct) / 100;
        if (known && fw > 0) d.fillRoundRect(bx, dY(y0 + 18), fw, 7, 3, C_METER);
        text(d, rem.c_str(), bx, dY(y0 + 29), 10, C_TEXT, C_PANEL);
        if (w.eta.length()) text(d, w.eta.c_str(), bx + bw, dY(y0 + 29), 10, C_DIM, C_PANEL, Align::TopRight);
      }
    });
    done(W_HALV, kb);
  }

  // ---- Price / fee: only when the fetch works, else one honest line ---------
  const bool havePrice = w.price != "--";
  String cur = currencyFor(Settings.Currency).code;
  cur.toUpperCase();
  char lk[96];
  snprintf(lk, sizeof(lk), "%s|%d|%s|%d", w.price.c_str(), w.fee, w.why.c_str(), P ? 1 : 0);
  if (need(W_M2, lk))
  {
    const Rect r = g.line;
    const uint16_t bg = P ? C_PANEL : C_BG;
    const int lw = P ? r.w - 4 : r.w, lh = P ? r.h - 4 : r.h;
    if (wBegin(lw, lh, bg))
    {
      const int ty = (lh - 12) / 2;
      if (havePrice)
      {
        String l = String("1 ") + coin.ticker + " = " + w.price;
        text(wgt, l.c_str(), 6, ty, 12, C_TEXT, bg);
        if (w.fee > 0)
        {
          char f[32];
          snprintf(f, sizeof(f), "fee %d %s", w.fee, coin.feeUnit);
          text(wgt, f, lw - 6, ty + 1, 11, C_DIM, bg, Align::TopRight);
        }
      }
      else
      {
        String l = String("price: ") + (w.why.length() ? w.why : String("updating..."));
        caption(wgt, l.c_str(), lw / 2, ty + 1, lw - 8, Align::TopCenter, bg);
      }
      wEnd(P ? r.x + 2 : r.x, P ? r.y + 2 : r.y);
      done(W_M2, lk);
    }
  }

  // ---- Housekeeping strip ----------------------------------------------------
  stripCell(0, 1, "RUNNING", w.uptime.c_str(), C_TEXT, C_DIM);
  stripCell(1, 2, "CHIP TEMP", w.temp.c_str(), w.tempCol, w.tempCol == C_TEXT ? C_METER : w.tempCol);
  stripCell(2, 0, "WINS", w.blocks.c_str(), w.blocks != "0" ? C_GOLD : C_TEXT, C_GOLD);
}
#endif // CYD_WORLD_SCREEN

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
    d.drawArc(cx, dY(cy), r, ri, a0, a0 + 11, C_METER, C_BG, true);
  }
  text(d, "#", cx, dY(cy - 27), 54, C_TEXT, C_BG, Align::TopCenter);
  unsigned bsz = 26;
  while (bsz > 18 && textW("AARON MINER", bsz) > (isPortrait() ? 224 : 300))
    bsz--;
  int wn = textW("AARON MINER", bsz);
  int x0 = cx - wn / 2;
  int textY = isPortrait() ? 180 : 152;
  text(d, "AARON MINER", x0, dY(textY), bsz, C_TEXT, C_BG);
  text(d, CURRENT_VERSION, cx, dY(textY + 38), 14, C_GOLD, C_BG, Align::TopCenter);
  text(d, "STARTING UP", cx, dY(textY + 58), 11, C_DIM, C_BG, Align::TopCenter);
}

static void paintSetup(TFT_eSPI &d)
{
  clearScreen(d);
  text(d, "SETUP MODE", 16, dY(12), 22, C_TEXT, C_BG);
  text(d, "On your phone:", 16, dY(40), 11, C_DIM, C_BG);

  String ap = readCustomAPName();
  if (ap.length() == 0)
    ap = DEFAULT_SSID;
  const char *lab[3] = {"JOIN WI-FI", "PASSWORD", "THEN OPEN"};
  const char *val[3] = {ap.c_str(), DEFAULT_WIFIPW, "http://192.168.4.1"};
  int pw = isPortrait() ? 224 : 304;
  for (int i = 0; i < 3; i++)
  {
    int y = 66 + i * 50;
    panel(d, 8, y, pw, 44, 0, C_PANEL, C_EDGE, C_BG);
    char n[4];
    snprintf(n, sizeof(n), "0%d", i + 1);
    text(d, n, 16, dY(y + 13), 14, C_METER, C_PANEL);
    text(d, lab[i], 46, dY(y + 6), 8, C_DIM, C_PANEL);
    text(d, val[i], 46, dY(y + 18), isPortrait() ? 13 : 15, C_TEXT, C_PANEL);
  }
  text(d, "Saved on the device", isPortrait() ? 120 : 160, dY(isPortrait() ? 230 : 222), 10, C_DIM, C_BG, Align::TopCenter);
}

// ---------------------------------------------------------------- chrome --

static void paintChrome(TFT_eSPI &d, int scr)
{
  clearScreen(d);
#if CYD_WORLD_SCREEN
  if (scr == SCR_WORLD)
    chromeWorld(d);
  else
#endif
    chromeMine(d);
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

static HdrV liveHeader(const String &time);

// Last view painted per screen, so a page change can repaint instantly
// instead of waiting for the next (possibly network-blocked) data tick.
static MineV lastMine;
static bool haveMine = false;
static uint32_t lastMineMs = 0; // millis() when lastMine was filled

static void repaintCached(int scr)
{
  if (scr == SCR_MINE && haveMine)
    paintMine(lastMine);
#if CYD_WORLD_SCREEN
  else if (scr == SCR_WORLD)
  {
    WorldV w;
    w.hdr = liveHeader(getTime());
    fillWorld(w, lastKh, String(bitcoin_price), gData.halfHourFee);
    paintWorld(w);
  }
#endif
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

  if (uiLive && !g_onb)
  {
    std::lock_guard<std::mutex> lock(g_displayMutex); // called from the web task
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
  int page;
  if (p.isKey("page3"))
    page = p.getUChar("page3", 0);
  else
  { // v2.0.0 had 4 pages: 0 mining, 1 market, 2 clock, 3 architecture
    static const uint8_t map4[4] = {SCR_MINE, SCR_MINE, SCR_MINE, SCR_MINE};
    int old = p.getUChar("page", 0);
    page = (old >= 0 && old < 4) ? map4[old] : SCR_MINE;
  }
  p.end();
  if (page < 0 || page >= SCR_COUNT)
    page = SCR_MINE;
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
    p.putUChar("page3", page);
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
  {
    uint32_t bh = getBlockHeightValue();
    h.netHeight = bh > 0 ? String(bh) : String("--");
    uint32_t lj = g_lastJobMs;
    if (lj > 0) { uint32_t age = (millis() - lj) / 1000; h.job = String(age) + "s"; }
    else h.job = "--";
  }
  h.ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  h.bars = wifiBars();
  h.status = uiStatus();
  h.screen = shotOn ? g_shotTargetScr : (currentDisplayDriver ? currentDisplayDriver->current_cyclic_screen : 0);
  return h;
}

static void logShare(const String &shares, const String &khashes, const String &rate)
{
  extern volatile bool g_serialMuted;
  if (g_serialMuted) return;
  // Parsed by the bench scripts (see project notes) -- keep the format.
  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                shares.c_str(), khashes.c_str(), rate.c_str());
}

static void netKpiTick(unsigned long mElapsed);

void esp32_2432S028R_MinerScreen(unsigned long mElapsed)
{
  if (g_onb)
  {
    (void)getMiningData(mElapsed); // keep the per-second hashrate sample ticking
    return;             // onboarding owns the panel
  }
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
  extern volatile bool g_serialMuted;
  if (!shotOn && !g_serialMuted)
  {
    netKpiTick(mElapsed); // price / fee / global hashrate for the web dashboard (runs on screen 0)
    refreshPool();
  }
  uiLive = true;
  ensureChrome(SCR_MINE);

  float kh = data.currentHashRate.toFloat();
  noteHashrate(kh);
  gaugeTarget = constrain(kh / G_MAX_KH, 0.0f, 1.0f);
  roundTarget = roundFill(kh); // inner ring = guesses this round vs a typical block of this coin
  uint32_t sh = data.completedShares.toInt();
  if (sh > lastShares && lastShares != 0)
    shareFlashUntil = millis() + 1500;
  lastShares = sh;

  MineV m;
  m.hdr = liveHeader(data.currentTime);
  fillMine(m, data.currentHashRate, sh, data.blockTimeSec, (double)data.blockHashes);
  lastMine = m;
  lastMineMs = millis();
  haveMine = true;
  paintMine(m);

  logShare(data.completedShares, data.totalKHashes, data.currentHashRate);
}

void esp32_2432S028R_OddsScreen(unsigned long mElapsed)
{
  esp32_2432S028R_MinerScreen(mElapsed);
}

// An HTTPS fetch needs ~40 KB of heap for TLS. With Wi-Fi, the web server and
// the 26 KB widget sprite all resident, the handshake failed with
// "BIGNUM - Memory allocation failed". Lend the sprite's buffer to the fetch
// and take it back right after.
template <typename F>
static auto withTlsHeap(F fetch) -> decltype(fetch())
{
  const bool lend = wgtReady && marketFetchSoon();
  if (lend)
  {
    wgt.deleteSprite();
    wgtReady = false;
  }
  auto r = fetch();
  if (lend)
    ensureWgtReady();
  return r;
}

// Price, fee and global hashrate (mempool / price APIs) feed the web
// dashboard's network numbers. The WORLD page used to fetch them; now the ODDS
// page does, on the same throttles (price 1 min, global 2 min). Called with
// the display mutex held: the TLS fetch borrows the widget sprite's heap.
static void netKpiTick(unsigned long mElapsed)
{
  (void)withTlsHeap([&] { return getCoinData(mElapsed); });
}

#if CYD_WORLD_SCREEN
void esp32_2432S028R_WorldScreen(unsigned long mElapsed)
{
  if (g_onb)
  {
    (void)getCoinData(mElapsed);
    return;
  }
  std::lock_guard<std::mutex> lock(g_displayMutex);
  coin_data data = withTlsHeap([&] { return getCoinData(mElapsed); });
  refreshPool();
  uiLive = true;
  ensureChrome(SCR_WORLD);
  noteHashrate(data.currentHashRate.toFloat());

  WorldV w;
  w.hdr = liveHeader(data.currentTime);
  fillWorld(w, data.currentHashRate.toFloat(), data.btcPrice, data.halfHourFee.toInt());
  paintWorld(w);

  logShare(data.completedShares, data.totalKHashes, data.currentHashRate);
}
#endif

// --------------------------------------------------------- driver plumbing --

static void touchTask(void *);
void esp32_2432S028R_AlternateRotation(void);
static void persistRotation(int rot);

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
  memset(segLvl, 0xFF, sizeof(segLvl)), memset(rndLvl, 0xFF, sizeof(rndLvl)), memset(hexShown, 0, sizeof(hexShown));
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

// BOOT hold: get out of onboarding entirely, keeping what's in use now.
static void obSkipAll()
{
  if (!Settings.touchCalibrated)
  {
    touch.setCalibration(obPrevCal);
    Settings.touchCal[0] = obPrevCal.x0;
    Settings.touchCal[1] = obPrevCal.x1;
    Settings.touchCal[2] = obPrevCal.y0;
    Settings.touchCal[3] = obPrevCal.y1;
    Settings.touchCal[4] = obPrevCal.rel_rotation;
    Settings.touchCalibrated = true;
  }
  if (!Settings.colorCalibrated)
  {
    Settings.invertColors = invertColors;
    Settings.colorCalibrated = true;
  }
  Serial.printf("CYD onboarding: skipped with BOOT, keeping touch {%u,%u,%u,%u,%u} invertColors=%d\n",
                obPrevCal.x0, obPrevCal.x1, obPrevCal.y0, obPrevCal.y1, obPrevCal.rel_rotation, (int)invertColors);
  obSave();
  obFinish();
}

// Diagnostics + web escape hatch (GET /api/onboarding, POST /api/onboarding/skip)
static volatile bool obSkipReq = false;
static uint32_t obPolls = 0, obRawHits = 0, obXyHits = 0, obLastHitMs = 0;
static TFT_eTouchBase::Measure obLastRaw = {};

String cydOnboardingJson()
{
  char b[300];
  snprintf(b, sizeof(b),
           "{\"step\":%d,\"target\":%d,\"touchCalibrated\":%s,\"colorCalibrated\":%s,\"invertColors\":%s,"
           "\"boot\":%d,\"polls\":%lu,\"rawHits\":%lu,\"xyHits\":%lu,\"lastHitAgoMs\":%ld,"
           "\"lastRaw\":[%u,%u,%u,%u,%u]}",
           (int)g_onb, obTarget, Settings.touchCalibrated ? "true" : "false", Settings.colorCalibrated ? "true" : "false",
           Settings.invertColors ? "true" : "false", digitalRead(PIN_BOOT_BTN), (unsigned long)obPolls,
           (unsigned long)obRawHits, (unsigned long)obXyHits, obLastHitMs ? (long)(millis() - obLastHitMs) : -1L,
           obLastRaw.x, obLastRaw.y, obLastRaw.z1, obLastRaw.z2, obLastRaw.rz);
  return String(b);
}

void cydOnboardingSkip()
{
  if (g_onb)
    obSkipReq = true;
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
      obSkipAll();
      return;
    }
  }
  else
  {
    obBootSince = 0;
    obBootLatched = false;
  }

  const int st = g_onb;
  {
    // diagnostics: raw controller readout every 200 ms, and a heartbeat
    static unsigned long lastDiag = 0, lastBeat = 0;
    TFT_eTouchBase::Measure m;
    const bool d = touch.getRaw(m);
    int16_t gx = -1, gy = -1;
    const bool gd = touch.getXY(gx, gy);
    obPolls++;
    if (d)
    {
      obRawHits++;
      obLastRaw = m;
      obLastHitMs = millis();
    }
    if (gd)
      obXyHits++;
    if ((d || gd) && millis() - lastDiag > 200)
    {
      lastDiag = millis();
      Serial.printf("CYD touch diag: st=%d raw=%d x=%u y=%u z1=%u z2=%u rz=%u xy=%d (%d,%d)\n",
                    st, (int)d, m.x, m.y, m.z1, m.z2, m.rz, (int)gd, gx, gy);
    }
    if (millis() - lastBeat > 3000)
    {
      lastBeat = millis();
      Serial.printf("CYD onboarding: st=%d target=%d boot=%d\n", st, obTarget, digitalRead(PIN_BOOT_BTN));
    }
  }
  if (Serial.peek() == 's' || obSkipReq)
  {
    if (!obSkipReq)
      Serial.read();
    obSkipReq = false;
    obSkipAll();
    return;
  }
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
  Settings.screenRotation = 0;
  Settings.flipDisplay = false;
  persistRotation(0);
  tft.setRotation(0); // 0=Portrait only (USB / charger at bottom)
  tft.setSwapBytes(true);
  if (invertColors)
  {
    tft.writecommand(ILI9341_GAMMASET);
    tft.writedata(2);
    delay(120);
    tft.writecommand(ILI9341_GAMMASET); // Gamma curve selected
    tft.writedata(1);
  }
#ifndef CYD_TOUCH_SHARED_SPI
  hSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, ETOUCH_CS);
#endif
  touch.init(); // CS pin only; never re-begins the bus

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
  if (abs((int)obPrevCal.x1 - (int)obPrevCal.x0) < 500 || abs((int)obPrevCal.y1 - (int)obPrevCal.y0) < 500)
  {
    // garbage/zeroed calibration would divide by zero in TFT_eTouch::transform()
    Serial.println("CYD touch: stored calibration invalid, using defaults and asking again");
    obPrevCal = CAL_DEFAULT;
    Settings.touchCalibrated = false;
  }
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
  memset(segLvl, 0xFF, sizeof(segLvl)), memset(rndLvl, 0xFF, sizeof(rndLvl)), memset(hexShown, 0, sizeof(hexShown));


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
  // Device is fixed to portrait only with charger at bottom (rotation 0)
  tft.setRotation(0);
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
  panelSample(); // any screen: the MINE panel keeps its speed history and records
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
  {
    static uint32_t seenBlk = 0;
    if (g_newBlockMs != seenBlk) { seenBlk = g_newBlockMs; rndDrain = roundShown > 0.05f; }
  }
  const float rt = rndDrain ? 0.0f : roundTarget; // a new block: empty first, then refill
  if (uiScreen == SCR_MINE && fabsf(rt - roundShown) > 0.002f)
  { // ~100 ms frames: a drain from full to empty takes about 1 s
    roundShown += (rt - roundShown) * 0.3f;
    if (fabsf(rt - roundShown) <= 0.02f) roundShown = rt;
    if (rndDrain && roundShown <= 0.05f) rndDrain = false;
    roundRingDraw(tft, roundShown, winnerZeros());
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
  TA_ACCENT,
  TA_WIN0, // RECENT ATTEMPTS window tabs: TA_WIN0 + 0/1/2
  TA_WIN1,
  TA_WIN2
};
// Generous hit boxes (+-6 px) around the small 15s/30s/60s tabs.
static int attTabHit(int x, int y)
{
  for (int i = 0; i < 3; i++)
  {
    const Rect t = attTabRect(i);
    if (x >= t.x - 2 && x < t.x + t.w + 2 && y >= t.y - 6 && y < t.y + t.h + 6)
      return i;
  }
  return -1;
}
static volatile int pendingTouch = TA_NONE;

// Ambient light sensor (LDR on GPIO 34, ADC1) auto-dimming
#define PIN_LDR 34
#ifdef CYD_TOUCH_SHARED_SPI
// ------------------------------------------------- shared-bus touch health --
// The 2.4" board's XPT2046 hangs off TFT_eSPI's SPI peripheral, so anything
// else that begin()s that peripheral steals its MISO input: SDCard used the
// global SPI object (same VSPI unit, MISO 19) and every touch read then came
// back as zeros. SD now has its own unit (SD_ID=HSPI); the touch task also
// re-routes MISO back to GPIO12 once setup() is done, as a guard.
// GET /api/touchdiag reports what the controller actually returns.
struct TouchDiag
{
  uint16_t xpt[4] = {};   // last z1, z2, x, y (12 bit)
  uint16_t temp = 0;      // TEMP0: answers whether or not the panel is pressed
  uint32_t reads = 0, nonZero = 0, irq36Low = 0, samples = 0;
};
static TouchDiag tdiag;

static uint16_t xptCmd(SPIClass &spi, uint8_t cmd)
{
  spi.transfer(cmd);
  return (spi.transfer16(0x00) >> 3) & 0x0fff;
}

static void touchDiagRead()
{
  static const uint8_t cmds[4] = {0xB1, 0xC1, 0xD1, 0x91}; // z1 z2 x y
  SPIClass &spi = TFT_eSPI::getSPIinstance();
  uint16_t v[4];
  spi.beginTransaction(SPISettings(SPI_TOUCH_FREQUENCY, MSBFIRST, SPI_MODE0));
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(ETOUCH_CS, LOW);
  for (int i = 0; i < 4; i++)
    v[i] = xptCmd(spi, cmds[i]);
  xptCmd(spi, 0x87); // TEMP0, reference on (first conversion settles it)
  tdiag.temp = xptCmd(spi, 0x87);
  xptCmd(spi, 0x90); // back to power-down with PENIRQ enabled
  digitalWrite(ETOUCH_CS, HIGH);
  spi.endTransaction();
  memcpy(tdiag.xpt, v, sizeof(v));
  tdiag.reads++;
  if (v[0] | v[1] | v[2] | v[3])
    tdiag.nonZero++;
}

static void touchDiagTick()
{
  static unsigned long last = 0;
  static bool rerouted = false;
  tdiag.samples++;
  if (digitalRead(TOUCH_IRQ) == LOW)
    tdiag.irq36Low++;
  if (!rerouted && millis() > 8000)
  {
    // setup() has finished with the SD card by now
    rerouted = true;
    std::lock_guard<std::mutex> lock(g_displayMutex);
    spiAttachMISO(TFT_eSPI::getSPIinstance().bus(), TFT_MISO);
  }
  if (millis() - last > 2000)
  {
    last = millis();
    touchDiagRead();
  }
}

String cydTouchDiagJson()
{
  char b[260];
  snprintf(b, sizeof(b),
           "{\"bus\":\"shared 14/13/12 cs%d\",\"temp\":%u,\"xpt\":[%u,%u,%u,%u],\"reads\":%lu,\"nonZero\":%lu,"
           "\"irqLowSamples\":%lu,\"samples\":%lu,\"irq\":%d}",
           ETOUCH_CS, tdiag.temp, tdiag.xpt[0], tdiag.xpt[1], tdiag.xpt[2], tdiag.xpt[3],
           (unsigned long)tdiag.reads, (unsigned long)tdiag.nonZero, (unsigned long)tdiag.irq36Low,
           (unsigned long)tdiag.samples, digitalRead(TOUCH_IRQ));
  return String(b);
}

#endif

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
#ifdef CYD_TOUCH_SHARED_SPI
  pinMode(TOUCH_IRQ, INPUT);
#endif
  for (;;)
  {
#ifdef CYD_TOUCH_SHARED_SPI
    touchDiagTick();
#endif
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
        else if (uiScreen == SCR_MINE && isPortrait() && attTabHit(t_x, t_y) >= 0)
          pendingTouch = TA_WIN0 + attTabHit(t_x, t_y); // RECENT ATTEMPTS 15s/30s/60s
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
  case TA_WIN0:
  case TA_WIN1:
  case TA_WIN2:
    g_attWin = (uint8_t)(action - TA_WIN0);
    if (!g_onb && uiLive)
    {
      std::lock_guard<std::mutex> lock(g_displayMutex);
      repaintCached(uiScreen); // plot key includes the window: only it redraws
    }
    break;
  case TA_NEXT:
  case TA_PREV:
    if (drv->num_cyclic_screens > 1)
      drv->current_cyclic_screen = (drv->current_cyclic_screen + 1) % drv->num_cyclic_screens;
    break;
  }

  // Screen changed by touch or the boot button: draw the new page right away
  // from the last values it showed; the next data tick refreshes them.
  // Under the display mutex: a web screenshot flips shotOn while it renders,
  // and an unlocked repaint here would land in the screenshot sprite instead
  // of the panel (the tap "did nothing" until the next data tick).
  if (!g_onb && uiLive && currentDisplayDriver->current_cyclic_screen != uiScreen)
  {
    std::lock_guard<std::mutex> lock(g_displayMutex);
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

bool esp32_2432S028R_CaptureBmp(Print &client, int screenIdx)
{
  int rot = g_shotRot >= 0 ? g_shotRot : tft.getRotation();
  bool portrait = (rot == 0 || rot == 2);
  int w = portrait ? 240 : 320;
  int h = portrait ? 320 : 240;
  const int nSlices = 4;
  int sliceH = h / nSlices; // 80 portrait, 60 landscape (only 19.2 KB!)

  shot.setColorDepth(8);
  shot.createSprite(w, sliceH);
  if (!shot.created())
  {
    Serial.printf("[ui] shot sprite %dx%d alloc failed (free %u, max %u)\n",
                  w, sliceH, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    g_shotRot = -1;
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
  client.flush();

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
  client.flush();

  int targetScr = (screenIdx >= 0 && screenIdx < SCR_COUNT)
                      ? screenIdx
                      : (currentDisplayDriver ? currentDisplayDriver->current_cyclic_screen : 0);
  if (targetScr < 0 || targetScr >= SCR_COUNT)
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
    // lastMine is only refreshed while the panel shows MINE; if it's older
    // than 0.3 s (panel on ODDS, or a block just landed after the last tick) capture a fresh one so the toast, guesses
    // and round lines are current.
    shotHaveMine = haveMine && (millis() - lastMineMs) < 300UL;
    if (shotHaveMine)
      shotMine = lastMine;
    else
    {
      fillMine(shotMine, getCurrentHashRate(0), shares);
      shotHaveMine = true;
    }
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
    if (screenIdx == 9 || screenIdx == 10)
    { // dev: preview the boot splash / Wi-Fi setup page without rebooting
      if (screenIdx == 9)
        paintLoading(shot);
      else
        paintSetup(shot);
      shotOn = false;
      return;
    }
    ensureChrome(targetScr);
    if (targetScr == SCR_MINE)
    {
      MineV m;
      if (shotHaveMine)
        m = shotMine;
      else
      {
        uint32_t nowSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        uint32_t blkSec = (g_blockStartTimeSec > 0 && nowSec >= g_blockStartTimeSec) ? (nowSec - g_blockStartTimeSec) : 0;
        fillMine(m, getCurrentHashRate(0), shares, blkSec, (double)g_currentBlockHashes);
      }
      m.hdr = liveHeader(getTime());
      paintMine(m);
      gaugeDraw(out(), gaugeShown > 0.0f ? gaugeShown : 0.84f);
      roundRingDraw(out(), roundShown > 0.001f ? roundShown : roundFill(getCurrentHashRate(0).toFloat()), winnerZeros());
    }
#if CYD_WORLD_SCREEN
    else
    {
      WorldV w;
      w.hdr = liveHeader(getTime());
      fillWorld(w, getCurrentHashRate(0).toFloat(), String(bitcoin_price), gData.halfHourFee);
      paintWorld(w);
    }
#endif
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
      client.flush();
      if ((y & 7) == 0)
      {
        vTaskDelay(1); // yield to Wi-Fi / lwIP task
      }
    }
    vTaskDelay(2);
  }

  shot.deleteSprite();
  g_shotYOffset = 0;
  g_shotRot = -1;
  return true;
}

CyclicScreenFunction esp32_2432S028RCyclicScreens[] = {
    esp32_2432S028R_MinerScreen
#if CYD_WORLD_SCREEN
    , esp32_2432S028R_WorldScreen
#endif
};

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

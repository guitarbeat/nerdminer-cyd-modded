#include "version.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "webConfig.h"
#include "wManager.h"
#include "drivers/storage/storage.h"
#include "currency.h"
#include "monitor.h"
#include "drivers/displays/display.h"
#include "mining.h"
#include "webCydTwin.h"
#include "drivers/displays/rgbLed.h"
#include "cities.h"
#include "webMonitorPage.h"
#ifdef M5STICK_C_PLUS2
#include "m5stickPlus2Hw.h"
#endif

// Settings page login (optional change via webpass/webpass2 form fields).
#define WEBCFG_USER "admin"
#define WEBCFG_PASS_MAX 63

extern TSettings Settings;
extern monitor_data mMonitor;
extern pool_data pData;
extern uint32_t templates;
extern uint64_t upTime;
extern volatile uint32_t shares, valids;
extern double best_diff;
mining_data getMiningData(unsigned long mElapsed);

static WebServer webCfgServer(80);
static bool webCfgStarted = false;

static String htmlEscape(const String &in)
{
  String out = in;
  out.replace("&", "&amp;");
  out.replace("\"", "&quot;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("'", "&#39;"); // the page puts every value between single quotes
  return out;
}

// A form posted from another site still reaches this miner from the user's
// browser, so state-changing POSTs only take requests whose Origin (or,
// without one, Referer) is this device. Browsers send Origin with every POST;
// a request with neither header (curl, scripts) is let through. Upstream #17.
static bool sameOrigin()
{
  const String self = "http://" + webCfgServer.hostHeader();
  const String origin = webCfgServer.header("Origin");
  if (origin.length())
    return origin.equalsIgnoreCase(self);
  const String referer = webCfgServer.header("Referer");
  if (referer.length())
    return referer.equalsIgnoreCase(self) || referer.startsWith(self + "/");
  return true;
}
static bool refuseForeign()
{
  if (sameOrigin())
    return false;
  webCfgServer.send(403, "text/plain", "Refused: the request was not sent from this miner's own page.");
  return true;
}


static String getCleanWallet(const String &w)
{
  int dot = w.indexOf('.');
  return dot >= 0 ? w.substring(0, dot) : w;
}

static String getWorkerName(const String &w)
{
  int dot = w.indexOf('.');
  return dot >= 0 ? w.substring(dot + 1) : "";
}

static int getWifiQuality(int rssi)
{
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return 2 * (rssi + 100);
}

static String formatUptimeWeb(const String &s)
{
  int d = 0, h = 0, m = 0, sec = 0;
  if (sscanf(s.c_str(), "%d %d:%d:%d", &d, &h, &m, &sec) == 4)
  {
    char b[24];
    if (d > 0)
      snprintf(b, sizeof(b), "%dd %02d:%02d:%02d", d, h, m, sec);
    else
      snprintf(b, sizeof(b), "%02d:%02d:%02d", h, m, sec);
    return String(b);
  }
  return s.length() ? s : String("--");
}

String getDate(); // monitor.cpp
static String buildStatsJson(const mining_data &d)
{
  String json;
  json.reserve(3072);
  json += "{";
  json += "\"fw\":\"" FW_GIT "\",";
  json += "\"status\":";
  bool isHashing = (mMonitor.NerdStatus == NM_hashing || (WiFi.status() == WL_CONNECTED && d.currentHashRate.toFloat() > 0));
  if (isHashing)
    json += "\"hashing\"";
  else if (mMonitor.NerdStatus == NM_Connecting)
    json += "\"connecting\"";
  else
    json += "\"idle\"";
  json += ",\"hashrate\":\"" + d.currentHashRate + "\"";
  json += ",\"shares\":" + (d.completedShares.length() ? d.completedShares : String("0"));
  json += ",\"best_diff\":\"" + d.bestDiff + "\"";
  json += ",\"best_diff_raw\":" + String(best_diff, 6);
  json += ",\"mhashes\":" + (d.totalMHashes.length() ? d.totalMHashes : String("0"));
  json += ",\"uptime\":\"" + d.timeMining + "\"";
  json += ",\"temp\":\"" + d.temp + "\"";
  json += ",\"workers\":" + String(pData.workersCount);
  json += ",\"blocks\":" + (d.valids.length() ? d.valids : String("0"));
  json += ",\"block_hashes\":" + String((unsigned long)(d.blockHashes / 1000000ULL));
  json += ",\"block_time\":" + String(d.blockTimeSec);
  json += ",\"last_block_hashes\":" + String((unsigned long)(d.lastBlockHashes / 1000000ULL));
  json += ",\"time\":\"" + d.currentTime + "\"";
  json += ",\"date\":\"" + getDate() + "\"";
  json += ",\"ssid\":\"" + htmlEscape(WiFi.SSID()) + "\"";
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += ",\"theme\":" + String(Settings.displayTheme);
  json += ",\"ping\":" + String(g_stratumPing);
  {
    MinerStatus ms = getMinerStatus();
    json += ",\"last_job_s\":" + String(lastJobAgeSec());   // seconds since the pool's last job (-1 = none)
    json += ",\"shares_accepted\":" + String(ms.accepted);
    json += ",\"shares_rejected\":" + String(ms.rejected);
    json += ",\"pool_ping_ms\":" + String(g_stratumPing); // submit -> answer round trip (smoothed)
  }
  {
    // The network round: hashes since the last Bitcoin block (prevhash change).
    uint32_t nowSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t age = (g_blockStartTimeSec > 0 && nowSec >= g_blockStartTimeSec) ? nowSec - g_blockStartTimeSec : 0;
    time_t now = 0;
    if (!getUtcTime(now, false)) now = 0; // web task: never touches the RTC chip
    MinerStatus ms2 = getMinerStatus();
    char b[48];
    json += ",\"round_tries\":" + String((double)g_currentBlockHashes, 0);
    json += ",\"round_age_s\":" + String(age);
    json += ",\"round_start_s\":" + String((now > 1600000000 && g_blockStartTimeSec > 0) ? (unsigned long)(now - age) : 0UL); // unix time, 0 = unknown
    json += ",\"round_expected\":" + String((double)ms2.avgHashrateKHs * 1000.0 * (double)blockTargetSecs(), 0); // tries in an average round (coin's block time)
    json += ",\"block_s\":" + String(blockTargetSecs()); // active coin's target block time
    json += ",\"round_partial\":" + String(g_roundPartial ? "true" : "false"); // began at boot, not at a block
    snprintf(b, sizeof(b), "%.6g", getRoundBest());
    json += ",\"round_best\":" + String(b);
    snprintf(b, sizeof(b), "%.6g", g_lastWinnerDiff);
    json += ",\"last_winner_diff\":" + String(b);
    json += ",\"last_winner_height\":" + String((unsigned long)g_lastWinnerHeight);
    json += ",\"last_winner_hash\":\"" + String(g_lastWinnerHashHex) + "\"";
    {
      uint8_t bh[32];
      char hx[65] = "";
      if (getRoundBestHash(bh))
        for (int i = 0; i < 32; i++) snprintf(hx + 2 * i, 3, "%02x", bh[i]);
      json += ",\"round_best_hash\":\"" + String(hx) + "\""; // "" = none yet this round
    }
    json += ",\"last_winner_ok\":" + String((g_lastWinnerDiff > 0 && g_lastWinnerDiff >= g_networkDifficulty * 0.999) ? "true" : "false");
    json += ",\"new_block_s\":" + String(g_newBlockMs ? (long)((millis() - g_newBlockMs) / 1000UL) : -1L);
    RoundRec rr[ROUND_HIST_N];
    int n = getRounds(rr, ROUND_HIST_N);
    json += ",\"rounds\":[";
    for (int i = 0; i < n; i++)
    {
      snprintf(b, sizeof(b), "%.6g", rr[i].myBest);
      String best = b;
      snprintf(b, sizeof(b), "%.6g", rr[i].winner);
      json += String(i ? "," : "") + "{\"height\":" + String((unsigned long)rr[i].height) + ",\"secs\":" + String(rr[i].secs) +
              ",\"tries\":" + String((double)rr[i].tries, 0) + ",\"my_best\":" + best + ",\"winner\":" + String(b) +
              ",\"partial\":" + (rr[i].partial ? "true" : "false") + "}";
    }
    json += "]";
  }
  json += ",\"net_diff\":" + String(g_networkDifficulty > 0.0 ? g_networkDifficulty : 0.0, 6); // job nbits; 0 = no job yet
  json += ",\"using_fallback\":" + String(g_usingFallback ? "true" : "false");
  json += ",\"screen\":" + String(getDisplayScreen());
  bool isPort = (Settings.screenRotation == 0 || Settings.screenRotation == 2);
  json += ",\"is_portrait\":" + String(isPort ? "true" : "false");
  json += ",\"rotation\":" + String(Settings.screenRotation);
  json += ",\"market\":" + getMarketJson();
  // Every share the pool answered (newest last): d = difficulty the hash
  // reached, t = UTC seconds (0 = clock unknown), ok = accepted.
  {
    // Active pool and the per-pool table (shares / best / blocks are per pool).
    char key[64];
    const char *coin = "BTC";
    getActivePool(key, sizeof(key), &coin);
    json += ",\"pool\":\"" + htmlEscape(String(key)) + "\",\"coin\":\"" + String(coin) + "\"";
    char perr[96];
    getPoolError(perr, sizeof(perr));
    json += ",\"pool_error\":\"" + String(perr) + "\"";
    static PoolStatSummary tab[POOLSTAT_N];
    int n = getPoolStatsTable(tab, POOLSTAT_N);
    json += ",\"pool_stats\":[";
    char b[200];
    for (int i = 0; i < n; i++)
    {
      snprintf(b, sizeof(b), "%s{\"pool\":\"%s\",\"coin\":\"%s\",\"shares\":%lu,\"best_diff\":%.6g,\"blocks\":%lu,\"active\":%s}",
               i ? "," : "", htmlEscape(String(tab[i].key)).c_str(), poolCoinLabel(tab[i].key), (unsigned long)tab[i].shares,
               tab[i].best, (unsigned long)tab[i].valids, tab[i].active ? "true" : "false");
      json += b;
    }
    json += "]";
  }
  json += ",\"pool_diff\":" + String(getPoolDifficulty(), 6);
  {
    time_t nowUtc = 0;
    json += ",\"now_utc\":" + String((unsigned long)(getUtcTime(nowUtc, false) ? nowUtc : 0));
    static ShareRec hist[SHARE_HIST_N];
    int n = getShareHistory(hist, SHARE_HIST_N);
    json += ",\"share_log\":[";
    char b[64];
    for (int i = 0; i < n; i++)
    {
      snprintf(b, sizeof(b), "%s{\"d\":%.6g,\"t\":%lu,\"ok\":%d}", i ? "," : "", hist[i].diff,
               (unsigned long)hist[i].utc, hist[i].accepted ? 1 : 0);
      json += b;
    }
    json += "]";
  }
  json += ",\"hash_samples\":[";
  {
    uint8_t count = g_hashSampleCount;
    uint8_t head = g_hashSampleHead;
    if (count > HASH_SAMPLE_N)
      count = HASH_SAMPLE_N;
    for (uint8_t i = 0; i < count; i++)
    {
      uint8_t idx = (uint8_t)((head + HASH_SAMPLE_N - count + i) % HASH_SAMPLE_N);
      if (i)
        json += ",";
      json += String((unsigned)g_hashSamples[idx]);
    }
  }
  json += "]";
  json += "}";
  return json;
}

static void handleApiStats()
{
  mining_data d = getMiningData(0);
  webCfgServer.send(200, "application/json", buildStatsJson(d));
}

static void handleApiProfile()
{
  TaskStackMetrics t = getTaskStackMetrics();
  String json = "{";
  json += "\"hw_cycles_per_hash\":" + String((unsigned long)g_cycleProfile.hw_cycles_per_hash) + ",";
  json += "\"hw_cycle_jitter\":" + String((unsigned long)g_cycleProfile.hw_cycle_jitter) + ",";
  json += "\"sw_cycles_per_hash\":" + String((unsigned long)g_cycleProfile.sw_cycles_per_hash) + ",";
  json += "\"chunk_gap_cycles\":" + String((unsigned long)g_cycleProfile.chunk_gap_cycles) + ",";
  json += "\"hit_overhead_cycles\":" + String((unsigned long)g_cycleProfile.hit_overhead_cycles) + ",";
  json += "\"total_hw_hashes\":" + String((unsigned long)g_cycleProfile.total_hw_hashes) + ",";
  json += "\"total_sw_hashes\":" + String((unsigned long)g_cycleProfile.total_sw_hashes) + ",";
  json += "\"dma_transfers\":" + String((unsigned long)g_dmaTransferCount) + ",";
  json += "\"tasks\":{";
  json += "\"monitor_stack_hwm\":" + String((unsigned long)t.monitor_stack_hwm) + ",";
  json += "\"stratum_stack_hwm\":" + String((unsigned long)t.stratum_stack_hwm) + ",";
  json += "\"miner_hw_stack_hwm\":" + String((unsigned long)t.miner_hw_stack_hwm) + ",";
  json += "\"miner_sw_stack_hwm\":" + String((unsigned long)t.miner_sw_stack_hwm) + ",";
  json += "\"touch_stack_hwm\":" + String((unsigned long)t.touch_stack_hwm);
  json += "},";
  json += "\"heap\":{";
  json += "\"free_internal\":" + String((unsigned long)t.free_heap_internal) + ",";
  json += "\"min_free\":" + String((unsigned long)t.min_free_heap) + ",";
  json += "\"max_alloc\":" + String((unsigned long)t.max_alloc_heap) + ",";
  json += "\"allocated_blocks\":" + String((unsigned long)t.allocated_blocks) + ",";
  json += "\"free_blocks\":" + String((unsigned long)t.free_blocks);
  json += "}";
  json += "}";
  webCfgServer.send(200, "application/json", json);
}

static String hexString(const uint8_t *data, size_t len)
{
  String s = "";
  s.reserve(len * 2);
  const char hexChars[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i)
  {
    s += hexChars[(data[i] >> 4) & 0x0F];
    s += hexChars[data[i] & 0x0F];
  }
  return s;
}

static void handleApiCryptoAudit()
{
  String json = "{";
  json += "\"job_id\":" + String(g_cryptoAudit.job_id) + ",";
  json += "\"pool_difficulty\":" + String(g_cryptoAudit.pool_difficulty, 8) + ",";
  json += "\"active_midstate\":\"" + hexString(g_cryptoAudit.active_midstate, 32) + "\",";
  json += "\"active_header_tail\":\"" + hexString(g_cryptoAudit.active_header_tail, 16) + "\",";
  json += "\"pool_target\":\"" + hexString(g_cryptoAudit.pool_target, 32) + "\",";
  json += "\"nonce_ranges\":{";
  json += "\"hw_cursor\":" + String(g_cryptoAudit.hw_nonce_cursor) + ",";
  json += "\"sw_cursor\":" + String(g_cryptoAudit.sw_nonce_cursor) + ",";
  json += "\"nonce_collisions\":" + String(g_cryptoAudit.nonce_collisions);
  json += "},";
  json += "\"sample_count\":" + String(g_cryptoAudit.sample_count) + ",";
  json += "\"samples\":[";
  for (uint8_t i = 0; i < g_cryptoAudit.sample_count; ++i)
  {
    if (i > 0) json += ",";
    json += "{";
    json += "\"nonce\":" + String(g_cryptoAudit.samples[i].nonce) + ",";
    json += "\"diff\":" + String(g_cryptoAudit.samples[i].difficulty, 8) + ",";
    json += "\"hash\":\"" + hexString(g_cryptoAudit.samples[i].hash, 32) + "\"";
    json += "}";
  }
  json += "]";
  json += "}";
  webCfgServer.send(200, "application/json", json);
}

static void handleResetSession()
{
  if (refuseForeign())
    return;
  resetSessionStats();
  webCfgServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Session stats reset\"}");
}

static void handleGetTheme()
{
  String json = "{\"theme\":" + String(Settings.displayTheme) + "}";
  webCfgServer.send(200, "application/json", json);
}

static void handleSetTheme()
{
  int th = 0;
  if (webCfgServer.hasArg("id"))
    th = webCfgServer.arg("id").toInt();
  else if (webCfgServer.hasArg("theme"))
    th = webCfgServer.arg("theme").toInt();

  if (th < 0 || th > 5)
    th = 0;

  Settings.displayTheme = th;
  setDisplayTheme(th);
  saveSettingsToFlash();

  String json = "{\"status\":\"ok\",\"theme\":" + String(th) + "}";
  webCfgServer.send(200, "application/json", json);
}

static void handleSetInvert()
{
  bool inv = false;
  if (webCfgServer.hasArg("val"))
  {
    String v = webCfgServer.arg("val");
    inv = (v == "1" || v.equalsIgnoreCase("true"));
  }
  else if (webCfgServer.hasArg("invert"))
  {
    String v = webCfgServer.arg("invert");
    inv = (v == "1" || v.equalsIgnoreCase("true"));
  }
  Settings.invertColors = inv;
  setDisplayInvert(inv);
  saveSettingsToFlash();

  String json = "{\"status\":\"ok\",\"invert\":" + String(inv ? "true" : "false") + "}";
  webCfgServer.send(200, "application/json", json);
}

// Forget an on-device calibration and reboot; the CYD asks again at boot.
static void recalibrateAndRestart()
{
  saveSettingsToFlash();
  webCfgServer.send(200, "application/json", "{\"status\":\"restarting\"}");
  delay(600);
  ESP.restart();
}

static void handleRecalibrateColors()
{
  Settings.colorCalibrated = false;
  recalibrateAndRestart();
}

static void handleRecalibrateTouch()
{
  Settings.touchCalibrated = false;
  recalibrateAndRestart();
}

#if (defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)) && !defined(CYD_THEME_CLASSIC)
String cydOnboardingJson();
void cydOnboardingSkip();
#ifdef CYD_TOUCH_SHARED_SPI
String cydTouchDiagJson();
static void handleTouchDiag() { webCfgServer.send(200, "application/json", cydTouchDiagJson()); }
#endif
static void handleOnboarding() { webCfgServer.send(200, "application/json", cydOnboardingJson()); }
static void handleOnboardingSkip()
{
  cydOnboardingSkip();
  webCfgServer.send(200, "application/json", "{\"status\":\"ok\"}");
}
#endif

static void handleGetScreen()
{
  String json = "{\"screen\":" + String(getDisplayScreen()) + "}";
  webCfgServer.send(200, "application/json", json);
}

static void handleSetScreen()
{
  // Single-screen unified dashboard: all screen requests map to 0
  setDisplayScreen(0);
  String json = "{\"status\":\"ok\",\"screen\":0}";
  webCfgServer.send(200, "application/json", json);
}

#if (defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)) && !defined(CYD_THEME_CLASSIC)
// Mine chart body: 0 = spectrogram, 1 = target ruler (same as tapping the
// MISS | SHARE | BLOCK band). GET reads, POST ?mode=0|1 sets.
extern void cydSetMineChartMode(int m);
extern int cydGetMineChartMode();
static void handleMineChart()
{
  if (webCfgServer.method() == HTTP_POST)
  {
    if (refuseForeign()) return;
    if (webCfgServer.hasArg("mode")) cydSetMineChartMode(webCfgServer.arg("mode").toInt());
  }
  webCfgServer.send(200, "application/json", "{\"mode\":" + String(cydGetMineChartMode()) + "}");
}
#endif

static void handleScreenshot()
{
  int screenIdx = -1;
  if (webCfgServer.hasArg("screen"))
    screenIdx = webCfgServer.arg("screen").toInt();
#if (defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)) && !defined(CYD_THEME_CLASSIC)
  // ?rot=0..3 renders the capture as if in that rotation (panel untouched),
  // so portrait and landscape layouts can both be checked remotely.
  extern void cydSetShotRotation(int r);
  cydSetShotRotation(webCfgServer.hasArg("rot") ? webCfgServer.arg("rot").toInt() : -1);
#endif

  webCfgServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webCfgServer.sendHeader("Pragma", "no-cache");
  webCfgServer.sendHeader("Expires", "0");
  webCfgServer.setContentLength(77878);
  webCfgServer.send(200, "image/bmp", "");

  WiFiClient client = webCfgServer.client();
  if (!captureDisplayBmp(client, screenIdx))
  {
    client.stop();
  }
}

static void handleRestart()
{
  if (refuseForeign())
    return;
  webCfgServer.send(200, "application/json", "{\"status\":\"restarting\"}");
  delay(600);
  ESP.restart();
}

static void handleUpdateDone()
{
  bool ok = !Update.hasError();
  String res = ok ? "{\"status\":\"success\",\"message\":\"Update complete. Rebooting...\"}"
                  : "{\"status\":\"error\",\"message\":\"Update failed\"}";
  webCfgServer.sendHeader("Connection", "close");
  webCfgServer.send(ok ? 200 : 500, "application/json", res);
  if (ok)
  {
    delay(800);
    ESP.restart();
  }
}

static void handleUpdateUpload()
{
  HTTPUpload &upload = webCfgServer.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    Serial.printf("[OTA] Starting firmware update: %s\n", upload.filename.c_str());
    mMonitor.NerdStatus = NM_waitingConfig; // Pause active mining status
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (Update.end(true))
    {
      Serial.printf("[OTA] Update Success: %u bytes written\n", upload.totalSize);
    }
    else
    {
      Update.printError(Serial);
    }
  }
}

static const char DASH_STYLE[] PROGMEM = R"rawcss(
/* Data-terminal palette (taste-skill app sheet, seed 199590601, kind
   markets, dark). Cards sit a step darker than the ground with one soft
   shadow; colour goes on data only: the hashrate arc carries the one hue
   (--accent), rejects use --negative, and every selection is a 1px accent
   line. The older role names below alias these so existing rules keep
   working. --amber is left to the device theme (set inline on <html>) and
   is not used by the page. */
:root{--ground: #171719; --surface: #131217; --raised: #212023; --border: #222224; --rule: #1b1a1f; --control: #3a3a3e; --text: #fff; --soft: #bdbdc0; --muted: #8a8c92; --accent: #4a8ad6; --positive: #44a282; --negative: #e5566f; --warning: #d9a441; --chart-1: #f2f2f3; --shadow: 0 12px 28px rgba(0,0,0,0.2);
  --bg: var(--ground); --bg-2: var(--ground); --bg-rgb: 23,23,25; --card: var(--surface); --card-2: var(--raised); --card-border: var(--border); --input-bg: var(--ground); --track: var(--raised); --track-hi: var(--control); --text-soft: var(--soft); --dim: var(--muted); --gold: var(--soft); --gold-rgb: 189,189,192; --rose: var(--negative); --rose-rgb: 229,86,111; --periwinkle: var(--accent); --periwinkle-rgb: 138,140,146; --green: var(--positive); --warn: var(--warning); --red: var(--negative); --on-accent: #131217;
  --mono: ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; --sans: 'Public Sans',-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif; --r: 6px; --ctl: 32px; --tbh: 70px; color-scheme: dark;}
*{box-sizing: border-box; margin: 0; padding: 0;}
html{-webkit-text-size-adjust: 100%; scroll-behavior: smooth; scroll-padding-top: calc(var(--tbh) + 8px);}
body{background: var(--bg); color: var(--text); font: 13px/1.4 var(--sans); padding-bottom: 12px; -webkit-font-smoothing: antialiased;}
a{color: var(--periwinkle);}
code{font: 0.88em var(--mono); background: var(--input-bg); border: 1px solid var(--card-border); padding: 0 4px; border-radius: 3px; word-break: break-all;}
.muted{color: var(--muted);}
.skip{position: absolute; left: -999px; top: 0; z-index: 60; background: var(--periwinkle); color: var(--on-accent); padding: 6px 10px;}
.skip:focus{left: 8px;}
.container{max-width: 1240px; margin: 0 auto; padding: 8px max(10px,env(safe-area-inset-right)) 0 max(10px,env(safe-area-inset-left));}

/* Sticky status bar + anchor nav */
.tb{position: sticky; top: 0; z-index: 50; background: var(--bg); border-bottom: 1px solid var(--card-border);}
.tb-in{max-width: 1240px; margin: 0 auto; display: flex; align-items: center; gap: 10px; padding: max(5px,env(safe-area-inset-top)) 10px 3px; min-height: 38px;}
.brand{display: flex; align-items: center; gap: 8px; min-width: 0; flex: 1; text-decoration: none; color: var(--text);}
.brand-t{display: flex; flex-direction: column; min-width: 0; line-height: 1.15;}
.logo-box{width: 24px; height: 24px; border-radius: var(--r); background: var(--accent); color: var(--on-accent); display: flex; align-items: center; justify-content: center; font-weight: 800; font-size: 14px; flex-shrink: 0;}
.brand-title{font-size: 13px; font-weight: 700; letter-spacing: 0.05em;}
.brand-subtitle{font: 11px var(--mono); color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis;}
.tb-hr{font: 11px var(--mono); color: var(--muted); white-space: nowrap;}
.tb-hr b{font-size: 16px; color: var(--accent); font-variant-numeric: tabular-nums;}
.coin-pill{font: 600 11px var(--mono); color: var(--gold); border: 1px solid rgba(var(--gold-rgb),0.4); border-radius: 999px; padding: 2px 8px; white-space: nowrap; max-width: 15em; overflow: hidden; text-overflow: ellipsis;}
.status-badge{display: inline-flex; align-items: center; gap: 5px; font-size: 12px; font-weight: 600; white-space: nowrap;}
.status-badge::before{content: ''; width: 7px; height: 7px; border-radius: 50%; background: var(--muted);}
.status-badge:has(.txt-green)::before{background: var(--green);}
.status-badge:has(.txt-amber)::before{background: var(--warn);}
.txt-green{color: var(--green);}.txt-amber{color: var(--warn);}.txt-gray{color: var(--muted);}
@media (max-width: 640px){.coin-pill,.brand-subtitle{display: none;}}
.an{max-width: 1240px; margin: 0 auto; display: flex; padding: 0 4px; overflow-x: auto; scrollbar-width: none;}
.an::-webkit-scrollbar{display: none;}
.an a{flex: 0 0 auto; color: var(--muted); text-decoration: none; font-size: 11px; font-weight: 700; letter-spacing: 0.12em; text-transform: uppercase; padding: 7px 10px 6px; border-bottom: 2px solid transparent;}
.an a:hover{color: var(--text);}
.an a.on{color: var(--periwinkle); border-bottom-color: var(--periwinkle);}

/* KPI strip: big numbers, tiny labels (1px gap grid = hairline dividers) */
.kpi{display: grid; grid-template-columns: repeat(4,minmax(0,1fr)); gap: 1px; background: var(--card-border); border: 1px solid var(--card-border); border-radius: var(--r); overflow: hidden; margin-bottom: 12px;}
.k{padding: 5px 10px; min-width: 0; background: var(--card);}
.kl{display: block; font-size: 10px; letter-spacing: 0.12em; text-transform: uppercase; color: var(--muted);}
.kv{display: block; font: 600 14px var(--mono); font-variant-numeric: tabular-nums; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;}
.kv small{font-size: 11px; color: var(--muted); font-weight: 400; margin-left: 4px;}
.k1 .kv{font-size: 22px; line-height: 1.2; font-weight: 700;}
.k1:first-child .kv{color: var(--accent);}
@media (min-width: 960px){.kpi{grid-template-columns: repeat(4,minmax(0,1.3fr)) repeat(4,minmax(0,1fr));}}
@media (max-width: 560px){.k1{grid-column: span 2;}.k{padding: 4px 8px;}.kv{font-size: 12.5px;}.kv small{margin-left: 2px;}}

.console-layout{display: grid; grid-template-columns: 290px minmax(0,1fr); gap: 14px; align-items: start;}
@media (max-width: 960px){.console-layout{grid-template-columns: 1fr;}.console-sidebar{order: 2;}}
.console-sidebar{display: flex; flex-direction: column; gap: 8px; min-width: 0;}
@media (min-width: 961px){.console-sidebar{position: sticky; top: calc(var(--tbh) + 8px);}}
.console-main{min-width: 0;}

/* Sections: tiny uppercase title on a thin cyan rule, flat panels */
.page-sec{display: flex; flex-direction: column; gap: 8px;}
.page-sec+.page-sec,.settings-sec+.settings-sec{margin-top: 16px;}
.settings-sec{display: grid; grid-template-columns: minmax(0,1fr); gap: 8px; align-content: start;}
.settings-h{grid-column: 1/-1; font-size: 11px; font-weight: 700; letter-spacing: 0.16em; text-transform: uppercase; color: var(--periwinkle); padding-bottom: 3px; border-bottom: 1px solid rgba(var(--periwinkle-rgb),0.3);}
@media (min-width: 1100px){#pane-system{grid-template-columns: 1fr 1fr;}#pane-system>details{grid-column: 1/-1;}}
.overview-grid{display: grid; grid-template-columns: repeat(auto-fit,minmax(min(100%,250px),1fr)); gap: 8px; align-items: start;}
.tele-card,.cfg-card,.pool-banner{background: var(--card); border: 1px solid var(--card-border); border-radius: var(--r); min-width: 0;}
.tele-card{padding: 5px 10px 6px; display: flex; flex-direction: column;}
.tele-card-hdr,.cfg-card-hdr{display: flex; align-items: center; justify-content: space-between; gap: 8px; flex-wrap: wrap;}
.tele-card-hdr{padding-bottom: 3px;}
.cfg-card-hdr{padding: 6px 10px 0;}
.tele-card-title,.cfg-card-title{font-size: 11px; font-weight: 700; letter-spacing: 0.1em; text-transform: uppercase; color: var(--rose);}
.tele-card-title .muted{text-transform: none; letter-spacing: 0; font-weight: 400;}
.tele-hero-sub{font-size: 11.5px; color: var(--muted); padding: 1px 0 3px;}
.unit{font-size: 12px; color: var(--muted); font-family: var(--sans);}
.tele-row{display: flex; justify-content: space-between; align-items: baseline; gap: 10px; padding: 2px 0; border-top: 1px solid var(--card-border); font-size: 12px; color: var(--muted);}
.tele-row>span:first-child{white-space: nowrap;}
.hl-val{color: var(--text); font: 12px var(--mono); font-variant-numeric: tabular-nums; text-align: right;}
.share-log{display: flex; flex-direction: column; border-top: 1px solid var(--card-border); max-height: 200px; overflow-y: auto; margin-top: 3px;}
.sl-row{display: grid; grid-template-columns: 5.5em 1fr 6em 5.5em; gap: 8px; align-items: center; padding: 2px 6px; font: 12px var(--mono); font-variant-numeric: tabular-nums;}
.sl-row:nth-child(even){background: rgba(255,255,255,0.03);}
.sl-row:not(.hdr):hover{background: rgba(var(--periwinkle-rgb),0.08);}
.sl-row.hdr{position: sticky; top: 0; background: var(--card); font: 600 10.5px var(--sans); color: var(--muted);}
.sl-row.rej{color: var(--red);}
.sl-row.best .sl-d{color: var(--gold);}
.sl-bar{height: 5px; background: var(--card-border); border-radius: 3px; overflow: hidden;}
.sl-bar>i{display: block; height: 100%; background: var(--muted);}
.sl-r{text-align: right; color: var(--muted); white-space: nowrap;}
.btn-reset-stats{background: transparent; border: 1px solid var(--card-border); border-radius: var(--r); color: var(--muted); padding: 2px 9px; font: 12px var(--sans); cursor: pointer; min-height: 26px;}
.btn-reset-stats:hover{color: var(--text); border-color: var(--dim);}
.btn-reset-stats:disabled{opacity: 0.6; cursor: wait;}
.pool-banner{padding: 7px 10px; display: flex; flex-direction: column; gap: 6px;}
.pool-info{font-size: 12.5px; min-width: 0; overflow-wrap: anywhere;}
.pool-btn{display: inline-flex; align-items: center; justify-content: center; border: 1px solid rgba(var(--periwinkle-rgb),0.4); color: var(--periwinkle); text-decoration: none; font-weight: 600; font-size: 12.5px; padding: 4px 10px; border-radius: var(--r); min-height: var(--ctl);}
.pool-btn:hover{background: rgba(var(--periwinkle-rgb),0.08);}

/* Disclosure */

details.more>summary{cursor: pointer; list-style: none; display: flex; align-items: center; justify-content: space-between; gap: 10px; font-size: 12.5px; font-weight: 600; color: var(--text); user-select: none;}

details.more>summary::-webkit-details-marker{display: none;}

details.more>summary::after{content: '+'; font: 15px var(--mono); color: var(--periwinkle); flex-shrink: 0;}
details.more[open]>summary::after{content: '\2212';}




details.more{border-top: 1px solid var(--card-border); padding-top: 6px;}
.more-body{padding-top: 8px; display: flex; flex-direction: column; gap: 8px;}

/* Controls: label above field, one height */
input[type=text],input[type=number],input[type=password],input[type=color],select,.form-control{width: 100%; background: var(--input-bg); border: 1px solid var(--card-border); border-radius: var(--r); color: var(--text); padding: 4px 9px; font: 13px var(--mono); outline: none; transition: border-color 0.15s; height: var(--ctl);}
input[type=color]{padding: 3px; cursor: pointer;}
input:focus,select:focus,.form-control:focus{border-color: var(--periwinkle); outline: 1px solid var(--periwinkle);}
input::placeholder{color: var(--dim);}
select{appearance: none; -webkit-appearance: none; cursor: pointer; padding-right: 30px; background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%238a8c92' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E"); background-repeat: no-repeat; background-position: right 10px center;}
select.ctl-auto{width: auto; display: inline-block;}
.file-input{flex: 1; min-width: 0; width: 100%; font-size: 12.5px; color: var(--text); background: var(--input-bg); padding: 4px 6px; border: 1px solid var(--card-border); border-radius: var(--r); min-height: var(--ctl);}
.cfg-card-body{padding: 6px 10px 9px; display: flex; flex-direction: column; gap: 8px;}
.form-grid-3{display: grid; grid-template-columns: 2fr 1fr 1fr; gap: 8px;}
.form-grid-2{display: grid; grid-template-columns: 1fr 1fr; gap: 8px 14px;}
.form-grid-4{display: grid; grid-template-columns: repeat(4,1fr); gap: 8px;}
@media (max-width: 720px){.form-grid-4{grid-template-columns: 1fr 1fr;}}
@media (max-width: 560px){.form-grid-3,.form-grid-2{grid-template-columns: 1fr;}}
.form-group{display: flex; flex-direction: column; gap: 3px; min-width: 0;}
.form-label{display: flex; align-items: center; justify-content: space-between; gap: 8px; font-size: 11px; font-weight: 600; letter-spacing: 0.04em; color: var(--muted);}
.form-label-sub{font-size: 11px; font-weight: 400; color: var(--muted); letter-spacing: 0;}
.form-sublabel{display: block; font-size: 11px; color: var(--muted);}
.form-hint{font-size: 11.5px; color: var(--muted); line-height: 1.35;}
.row-between{display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 8px;}
.form-check{display: flex; align-items: flex-start; gap: 8px; font-size: 12.5px; color: var(--text); cursor: pointer; user-select: none; line-height: 1.35;}
.form-check input[type=checkbox]{accent-color: var(--periwinkle); width: 16px; height: 16px; margin-top: 1px; flex-shrink: 0; cursor: pointer;}
.form-check input[type=checkbox]:disabled{cursor: wait;}
.slider-track-wrap{display: flex; align-items: center; min-height: var(--ctl);}
input[type=range]{-webkit-appearance: none; appearance: none; width: 100%; height: 4px; background: var(--card-border); border-radius: 2px; outline: none; accent-color: var(--periwinkle);}
input[type=range]::-webkit-slider-thumb{-webkit-appearance: none; width: 16px; height: 16px; border-radius: 50%; background: var(--periwinkle); cursor: pointer;}
input[type=range]::-moz-range-thumb{width: 16px; height: 16px; border-radius: 50%; background: var(--periwinkle); cursor: pointer; border: none;}
.preset-bar{display: flex; flex-direction: column; gap: 5px;}
.preset-pills{display: flex; flex-wrap: wrap; gap: 4px; align-items: center;}
.preset-pill{background: transparent; border: 1px solid var(--card-border); color: var(--text); font: 11.5px var(--mono); padding: 3px 8px; border-radius: 999px; cursor: pointer; min-height: 26px;}
.preset-pill:hover{border-color: var(--dim);}
.preset-pill.active{border-color: var(--periwinkle); color: var(--periwinkle);}
.preset-pill.sm{font-size: 11px; padding: 2px 7px; min-height: 24px;}
.preset-comment{font-size: 11.5px; line-height: 1.35; color: var(--muted);}
.active-pool-pill{display: inline-flex; align-items: center; gap: 6px; font-size: 11.5px; color: var(--muted);}
.active-pool-pill::before{content: ''; width: 6px; height: 6px; border-radius: 50%; background: var(--green);}
.active-pool-pill.fallback::before{background: var(--warn);}
.theme-grid{display: flex; flex-wrap: wrap; gap: 6px;}
.theme-card{display: inline-flex; align-items: center; gap: 6px; background: var(--input-bg); border: 1px solid var(--card-border); border-radius: 999px; padding: 3px 11px 3px 6px; min-height: 30px; font: 12px var(--sans); color: var(--text); cursor: pointer;}
.theme-card:hover{border-color: var(--dim);}
.theme-card.active{outline: 2px solid var(--accent); outline-offset: 2px;}
.theme-card.busy{opacity: 0.6; cursor: wait;}
.swatch-dot{width: 16px; height: 16px; border-radius: 50%; display: block; flex: none;}

/* Sticky actions: Save primary, Reboot quiet */
.actions{position: sticky; bottom: 0; z-index: 5; margin: 14px 0 4px; padding: 8px 0 max(8px,env(safe-area-inset-bottom)); display: flex; gap: 8px; background: rgba(var(--bg-rgb),0.94); border-top: 1px solid var(--card-border);}
.btn-save{background: var(--accent); color: var(--on-accent); border: none; border-radius: var(--r); padding: 8px 14px; font: 700 14px var(--sans); letter-spacing: 0.03em; cursor: pointer; flex: 2 1 0; min-height: 42px;}
.btn-save:hover{filter: brightness(1.08);}
.btn-save:disabled{opacity: 0.6; cursor: wait;}
.btn-restart{background: transparent; border: 1px solid var(--card-border); border-radius: var(--r); color: var(--muted); padding: 6px 10px; font: 12.5px var(--sans); cursor: pointer; flex: 1 1 0; min-height: 40px;}
.btn-restart:hover{color: var(--red); border-color: rgba(var(--rose-rgb),0.5);}
.btn-restart:disabled{opacity: 0.6; cursor: wait;}
.ota-row{display: flex; flex-wrap: wrap; gap: 8px; align-items: center;}
.btn-ota{background: transparent; border: 1px solid rgba(var(--periwinkle-rgb),0.45); border-radius: var(--r); color: var(--periwinkle); padding: 5px 12px; font: 600 12.5px var(--sans); cursor: pointer; min-height: var(--ctl);}
.btn-ota:hover{background: rgba(var(--periwinkle-rgb),0.08);}
.btn-ota:disabled{opacity: 0.6; cursor: wait;}
.ota-progress-box{display: none;}
.ota-progress-bar{height: 4px; border-radius: 2px; background: var(--input-bg); overflow: hidden;}
.ota-progress-fill{height: 100%; width: 0%; background: var(--periwinkle); transition: width 0.1s linear;}
.ota-status{font-size: 12px; color: var(--muted); margin-top: 3px; font-variant-numeric: tabular-nums;}
.guide-grid{display: grid; grid-template-columns: 1fr; gap: 10px;}
@media (min-width: 480px){.guide-grid{grid-template-columns: repeat(2,1fr);}}
.guide-card{font-size: 12px; color: var(--muted); min-width: 0;}
.guide-title{font-size: 11px; font-weight: 700; letter-spacing: 0.08em; text-transform: uppercase; color: var(--rose); margin-bottom: 2px;}
.guide-row{display: flex; justify-content: space-between; gap: 10px; padding: 2px 0; border-top: 1px solid var(--card-border);}
.guide-key{color: var(--text);}
.guide-val{color: var(--muted); text-align: right;}


.toast{position: fixed; left: 50%; bottom: max(16px,env(safe-area-inset-bottom)); transform: translateX(-50%); background: var(--card); border: 1px solid var(--card-border); border-left: 3px solid var(--green); color: var(--text); padding: 8px 14px; border-radius: var(--r); font-size: 13px; z-index: 1000; max-width: calc(100vw - 28px); display: none;}
.toast.err{border-left-color: var(--red);}
.toast.show{display: block;}
#overlay{display: none; position: fixed; inset: 0; background: rgba(var(--bg-rgb),0.96); z-index: 999; flex-direction: column; align-items: center; justify-content: center; text-align: center; padding: 20px;}
#overlay h3{font-size: 16px; margin-bottom: 6px;}
#overlay p{color: var(--muted); font-size: 13px;}
.spinner{width: 32px; height: 32px; border: 3px solid var(--card-border); border-top-color: var(--periwinkle); border-radius: 50%; animation: spin 1s linear infinite; margin-bottom: 14px;}
@keyframes spin{to{transform: rotate(360deg);}}
a:focus-visible,button:focus-visible,summary:focus-visible,input[type=range]:focus-visible,input[type=checkbox]:focus-visible,input[type=file]:focus-visible{outline: 2px solid var(--periwinkle); outline-offset: 2px;}
/* Viewport app shell: the page never scrolls; panes do */
html,body{height: 100%;}
body{height: 100vh; height: 100dvh; overflow: hidden; display: flex; flex-direction: column; padding: 0;}
.tb{position: relative; flex: none;}
.tb-in{min-height: 34px; padding-top: max(3px,env(safe-area-inset-top)); padding-bottom: 2px;}
.an a{padding: 5px 10px 4px;}
.container{flex: 1; min-height: 0; width: 100%; display: flex; flex-direction: column; padding-top: 6px; padding-bottom: 0;}
.kpi{flex: none; margin-bottom: 6px;}
.k{padding: 3px 9px;}
.k1 .kv{font-size: 19px;}
.console-layout{flex: 1; min-height: 0; overflow-y: auto; overscroll-behavior: contain; padding-bottom: 62px; gap: 10px; scrollbar-width: thin;}
.console-layout>*,#pane-telemetry,#pane-settings{scroll-padding-top: 6px;}
.page-sec,.settings-sec{gap: 6px;}
.page-sec+.page-sec,.settings-sec+.settings-sec{margin-top: 10px;}
.tele-card{padding: 4px 9px 5px;}
.tele-row{padding: 1px 0; font-size: 11.5px;}
.hl-val{font-size: 11.5px;}
.cfg-card-hdr{padding: 5px 9px 0;}
.cfg-card-body{padding: 5px 9px 7px; gap: 6px;}
input[type=text],input[type=number],input[type=password],input[type=color],select,.form-control{font-size: 12px; padding: 3px 8px;}
.form-hint{font-size: 11px;}
details.more{padding-top: 4px;}
details.more>summary{font-size: 12px; min-height: 24px;}

.more-body{padding-top: 6px; gap: 6px;}
.preset-bar.more{border-top: 0; padding-top: 0;}
.theme-grid{gap: 5px;}
.theme-card{font-size: 0; width: 28px; height: 28px; min-height: 28px; padding: 0; justify-content: center; border-radius: 50%;}
.theme-card .swatch-dot{width: 16px; height: 16px;}
.btn-restart{min-height: 34px; padding: 4px 8px; font-size: 12px;}
#btn-recal-touch,#btn-recal-colors{min-height: 28px;}
.actions{position: fixed; left: 0; right: 0; bottom: 0; margin: 0; padding: 6px max(10px,env(safe-area-inset-right)) max(6px,env(safe-area-inset-bottom)) max(10px,env(safe-area-inset-left)); z-index: 40;}
.btn-save{min-height: 40px;}
.actions .btn-restart{min-height: 40px;}
.lx{color: var(--periwinkle); text-decoration: none; border: 1px solid rgba(var(--periwinkle-rgb),0.45); border-radius: var(--r); padding: 4px 12px; min-height: 32px; display: inline-flex; align-items: center; letter-spacing: 0.08em;}
@media (max-width: 560px){.k1 .kv{font-size: 16px;}.kl{font-size: 9px;}}
/* Desktop: three columns that fill the viewport, each scrolls on its own */
@media (min-width: 1100px){
.console-layout{display: grid; grid-template-columns: 250px minmax(0,1fr) minmax(0,1.25fr); grid-template-rows: minmax(0,1fr); overflow: hidden; padding-bottom: 8px; align-items: stretch;}
.console-main,#cfg-form{display: contents;}
.console-sidebar{position: static; top: auto; overflow-y: auto; min-height: 0; scrollbar-width: thin;}
#pane-telemetry,#pane-settings{overflow-y: auto; min-height: 0; padding-right: 4px; scrollbar-width: thin;}
#pane-settings{display: flex; flex-direction: column;}
#pane-settings{margin-top: 0;}
#pane-telemetry>.tele-card:last-child{flex: 1 1 auto; min-height: 140px;}
#pane-telemetry .share-log{flex: 1 1 0; max-height: none; min-height: 60px;}
.overview-grid{grid-template-columns: minmax(0,1fr);}
#pane-system{grid-template-columns: minmax(0,1fr);}
.actions{position: sticky; left: auto; right: auto; padding: 6px 0 0; margin-top: auto;}
}
/* Infographic overview */
.sr{position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0 0 0 0); white-space: nowrap;}
.kb{display: block; height: 3px; background: var(--track); border-radius: 2px; margin-top: 2px; position: relative; overflow: hidden;}
.kb>i{display: block; height: 100%; width: var(--v,0%); background: var(--accent); transition: width 0.6s;}
.kb::after{content: ''; position: absolute; left: 79.2%; width: 8.3%; top: 0; bottom: 0; background: rgba(var(--periwinkle-rgb),0.45);}
.kc{display: flex; align-items: center; gap: 7px;}
.kc>span:last-child{min-width: 0;}
.ic{flex: none; position: relative; display: inline-block;}
.th{width: 8px; height: 22px; border: 1.5px solid var(--muted); border-radius: 4px; overflow: hidden;}
.th::after{content: ''; position: absolute; left: 0; right: 0; bottom: 0; height: var(--v,0%); background: var(--rose);}
.th.hot::after{background: var(--red);}
.wb{display: inline-flex; align-items: flex-end; gap: 2px; height: 18px;}
.wb i{width: 3px; background: var(--track-hi); border-radius: 1px;}
.wb i:nth-child(1){height: 5px;}.wb i:nth-child(2){height: 9px;}.wb i:nth-child(3){height: 13px;}.wb i:nth-child(4){height: 18px;}
.wb[data-l='1'] i:nth-child(-n+1),.wb[data-l='2'] i:nth-child(-n+2),.wb[data-l='3'] i:nth-child(-n+3),.wb[data-l='4'] i{background: var(--periwinkle);}
.pg{width: 12px; height: 12px; border-radius: 50%; background: var(--muted);}
.pg.g{background: var(--green);}.pg.a{background: var(--warn);}.pg.r{background: var(--red);}
.bt{width: 22px; height: 12px; border: 1.5px solid var(--muted); border-radius: 2px; padding: 1px;}
.bt::before{content: ''; position: absolute; right: -4px; top: 2px; width: 2px; height: 5px; background: var(--muted);}
.bt::after{content: ''; display: block; height: 100%; width: var(--v,0%); background: var(--green); border-radius: 1px;}
.bt.low::after{background: var(--warn);}
.vz2{display: grid; grid-template-columns: minmax(0,1fr) minmax(0,1fr); gap: 8px;}
.vz-tag{font: 10px var(--mono); color: var(--muted); text-transform: none; letter-spacing: 0;}
.vz-sub{font-size: 11px; color: var(--muted); line-height: 1.4;}
.vz-sub b,.vz-sub span[id]{color: var(--text); font: 11px var(--mono);}
.vz-k{font-size: 9.5px; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); white-space: nowrap;}
.gauge{display: block; width: 100%; max-width: 190px; margin: 0 auto;}
.gauge path{fill: none; stroke-width: 9; stroke-linecap: round;}
.g-trk{stroke: var(--track);}
.gauge .g-band{stroke: rgba(var(--periwinkle-rgb),0.4); stroke-width: 13; stroke-linecap: butt;}
.g-val{stroke: var(--accent); transition: stroke-dasharray 0.6s;}
.g-num{fill: var(--text); font: 700 17px var(--mono); text-anchor: middle;}
.g-u{fill: var(--muted); font: 7.5px var(--sans); letter-spacing: 0.12em; text-anchor: middle;}
.g-ax{fill: var(--dim); font: 7px var(--mono); text-anchor: middle;}
.dn-wrap{display: flex; align-items: center; gap: 10px; padding: 2px 0;}
.donut{width: 84px; height: 84px; flex: none;}
.donut circle{fill: none; stroke-width: 9;}
.d-trk{stroke: var(--track);}
.d-ok,.d-bad{transform: rotate(-90deg); transform-origin: 40px 40px; transition: stroke-dasharray 0.6s;}
.d-ok{stroke: var(--accent);}
.d-bad{stroke: var(--rose);}
.d-n{fill: var(--text); font: 700 15px var(--mono); text-anchor: middle;}
.d-l{fill: var(--muted); font: 6.5px var(--sans); letter-spacing: 0.1em; text-anchor: middle;}
.dn-lg{display: flex; flex-direction: column; gap: 3px; font-size: 11.5px; min-width: 0;}
.dn-lg b{font-family: var(--mono);}
.sw{display: inline-block; width: 8px; height: 8px; border-radius: 2px; margin-right: 5px;}.sw.ok{background: var(--accent);}.sw.bad{background: var(--rose);}
.lad{position: relative; height: 46px; margin: 2px 8px 2px;}
.lad-bar{position: absolute; left: 0; right: 0; top: 17px; height: 10px; border-radius: 5px; background: var(--track);}
#lad-t i{position: absolute; top: 16px; width: 3px; height: 12px; margin-left: -1.5px; background: var(--text); border-radius: 1px;}
#lad-t i.rj{background: var(--red);}
.mk{position: absolute; transform: translateX(calc(var(--sh,50)*-1%)); font: 600 9px var(--mono); letter-spacing: 0.04em; text-transform: uppercase; white-space: nowrap;}
.mk::after{content: ''; position: absolute; left: calc(var(--sh,50)*1%); width: 2px; margin-left: -1px; height: 14px; background: currentColor;}
.mk-best,.mk-win{top: 0;}.mk-best::after,.mk-win::after{top: 11px;}
.mk-pool,.mk-rb,.mk-blk{top: 34px;}.mk-pool::after,.mk-rb::after,.mk-blk::after{bottom: 11px;}
.mk-pool{color: var(--periwinkle);}.mk-best{color: var(--rose);}.mk-rb{color: var(--accent);}.mk-win{color: var(--text);}
.mk-blk{color: var(--gold);}
.kc .kl{white-space: nowrap; overflow: hidden; text-overflow: ellipsis;}
.hs,.rnd{display: flex; align-items: center; gap: 8px; margin-top: 4px; min-width: 0;}
.hs-v{font: 11px var(--mono); letter-spacing: 1px; overflow: hidden; white-space: nowrap; min-width: 0;}
.rbar{flex: 1; min-width: 40px; height: 6px; border-radius: 3px; background: var(--track); overflow: hidden;}
.rbar>div{height: 100%; width: 0;}
.rnd>.vz-k:last-child{text-transform: none; letter-spacing: 0; white-space: normal; font: 10.5px var(--mono); max-width: 55%;}
.bdgs{display: flex; flex-wrap: wrap; gap: 4px;}
.bdg{display: inline-flex; align-items: baseline; gap: 5px; border: 1px solid var(--card-border); background: var(--input-bg); border-radius: 999px; padding: 2px 9px; font-size: 11px;}
.bdg i{font-style: normal; font-size: 9px; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted);}
.bdg b{font: 600 11px var(--mono); color: var(--text);}
.bdg-p{border-color: rgba(var(--periwinkle-rgb),0.4); align-items: center;}
.bdg-p .glow{width: 8px; height: 8px; border-radius: 50%; background: var(--muted);}
.bdg-p.on .glow{background: var(--green); animation: pl 2s infinite;}
.bdg-p.wait .glow{background: var(--warn);}
.bdg-p b{color: var(--gold);}
@keyframes pl{50%{opacity: 0.45;}}
@media (max-width: 560px){.kc{gap: 5px;}.kc .rs{display: none;}.donut{width: 68px; height: 68px;}.dn-wrap{gap: 6px;}}
/* No section nav: one integrated view */
.tb-in{min-height: 38px;}
.stc{display: flex; align-items: center; gap: 8px; min-width: 0;}
.lrn{color: var(--periwinkle); text-decoration: none; font-size: 11px; font-weight: 700; letter-spacing: 0.1em; text-transform: uppercase; border: 1px solid rgba(var(--periwinkle-rgb),0.45); border-radius: var(--r); padding: 5px 10px; min-height: 28px; display: inline-flex; align-items: center;}
.lrn:hover{background: rgba(var(--periwinkle-rgb),0.08);}
.kpi{grid-template-columns: repeat(5,minmax(0,1fr));}
@media (min-width: 960px){.kpi{grid-template-columns: repeat(5,minmax(0,1fr));}}
.k1{grid-column: auto;}
.mk.nl{font-size: 0;}
.mk.nl::after{height: 14px;}
@media (max-width: 560px){.kpi{grid-template-columns: repeat(5,minmax(0,1fr));}.kc{flex-direction: column; align-items: flex-start; gap: 2px;}.k{padding: 3px 5px;}.kc .kl{font-size: 8.5px;}.kc .kv{font-size: 11.5px;}.brand-t{display: none;}.coin-pill{display: inline-block; max-width: 9em;}}
@media (min-width: 1100px){
.console-layout{grid-template-columns: 240px minmax(0,0.9fr) minmax(0,1.35fr);}
#pane-system .form-grid-3,#pane-settings .form-grid-2{grid-template-columns: minmax(0,1fr) minmax(0,1fr);}
#pane-system .form-grid-3>:first-child{grid-column: 1/-1;}
#pane-display .form-grid-2{grid-template-columns: minmax(0,1fr);}
#pane-settings .form-group:not(:focus-within)>.form-hint{display: none;}
#pane-settings .settings-sec{gap: 5px;}
}
/* Tall enough: settings flow into two columns (Wallet+Display | System+Save), no pane scroll */
@media (min-width: 1100px) and (min-height: 740px){
#pane-settings{flex-wrap: wrap; align-content: flex-start; column-gap: 10px; overflow: hidden;}
#pane-settings>*{width: calc(50% - 5px); flex: none;}
#pane-settings>#pane-system{margin-top: 0;}
#pane-settings>.actions{margin-top: auto; position: static; padding-top: 8px;}
}
/* Reduced motion: name the animated parts instead of a blanket override. */
/* Merged speed + shares card; settings cards follow each other without headings */
.tele-card>.vz2{gap: 12px;}
.tele-card>.vz2>div{display: flex; flex-direction: column; min-width: 0;}
.tele-card>.vz2>div+div{border-left: 1px solid var(--card-border); padding-left: 12px;}
.bdgs{margin-top: 4px;}
.shot-btn{font: inherit; font-size: 11px; font-weight: 700; background: none; cursor: pointer; margin-right: 6px;}
/* Device view: hidden (no box, no column) until a screenshot has loaded */
html:not(.shot-ok) .console-sidebar{display: none;}
html.shot-ok .shot-btn{display: none;}
@media (min-width: 1100px){html:not(.shot-ok) .console-layout{grid-template-columns: minmax(0,1fr) minmax(0,1.25fr);}}
/* Health strip: four chips (mining time moved into the speed card) */
.kpi,.kpi:has(.kc){grid-template-columns: repeat(4,minmax(0,1fr));}
/* Type and alignment pass (taste-skill app rules): sentence case, no
   tracking, nothing under 11px, tabular figures, one 4px spacing scale,
   one inline card padding so titles, labels and values share a left edge. */
body{font-variant-numeric: tabular-nums;}
.brand-title{font-size: 15px; font-weight: 700; line-height: 1.2;}
.kl,.tele-card-title,.cfg-card-title,.vz-k,.vz-tag,.bdg i,.sh-h,.mk,.lrn,.lx,.sheet-h,.settings-h,.sl-row.hdr,.form-label,.coin-pill,.brand-title{text-transform: none; letter-spacing: 0;}
.kl{font-size: 12px;}
.tele-card-title,.cfg-card-title,.sh-h{font-size: 13px; font-weight: 600; color: var(--text-soft);}
.vz-tag,.vz-k,.bdg i,.mk{font-size: 11px;}
.rnd>.vz-k:last-child{font-size: 11px;}
.vz-sub,.vz-sub b,.vz-sub span[id]{font-size: 12px;}
.sl-row.hdr{font-size: 12px; font-weight: 400;}
.sl-row:nth-child(even){background: none;}
.sl-row>:first-child,.sl-row>:nth-child(3){text-align: right;}
.sl-bar{height: 3px;}
.form-label,.form-label-sub,.form-hint,.brand-subtitle,.coin-pill,.preset-pill,.preset-pill.sm{font-size: 12px;}
.lrn{height: 30px; min-height: 30px; font-size: 12px; font-weight: 600; align-items: center; justify-content: center;}
.tele-card{padding: 8px 10px;}
.cfg-card-hdr{padding: 8px 10px 0;}
.cfg-card-body{padding: 6px 10px 10px;}
/* Theme swatches: one 28px box each; the selection is an outline ring
   that takes no layout space, so every dot sits on one centre line. */
.theme-grid{display: flex; flex-wrap: wrap; align-items: center; gap: 8px; padding: 2px 0 2px 4px;}
.theme-card,.theme-card.active{box-sizing: border-box; flex: none; display: inline-flex; align-items: center; justify-content: center; width: 28px; height: 28px; min-height: 28px; padding: 0; gap: 0; border: 1px solid var(--track-hi); border-radius: 50%; font-size: 0; line-height: 0;}
.theme-card .sr{font-size: 12px;}
.theme-card .swatch-dot{width: 16px; height: 16px;}
/* Controls in one row share one height */
input[type=text],input[type=number],input[type=password],input[type=color],select,.form-control,.file-input{height: var(--ctl); min-height: var(--ctl);}
.btn-save,.actions .btn-restart{height: 40px; min-height: 40px;}
.btn-restart,.btn-ota,.btn-reset-stats{display: inline-flex; align-items: center; justify-content: center;}
/* Data-terminal surfaces and controls */
.tele-card,.cfg-card,.pool-banner{border: 0; background: var(--surface); box-shadow: var(--shadow);}
.btn-save{background: var(--text); color: var(--on-accent); border-radius: 999px; font-weight: 600; letter-spacing: 0;}
.btn-restart,.btn-ota,.btn-reset-stats,.lrn,.lx,.preset-pill{border: 1px solid var(--control); border-radius: 999px; color: var(--text); background: none;}
.btn-restart:hover,.btn-ota:hover,.btn-reset-stats:hover,.lrn:hover,.lx:hover,.preset-pill:hover{border-color: var(--muted); background: none;}
.preset-pill.active{border-color: var(--control); color: var(--text); text-decoration: underline 1px var(--accent); text-underline-offset: 4px;}
.theme-card.active{outline: 1px solid var(--accent); outline-offset: 3px;}
.coin-pill{color: var(--soft); border-color: var(--control);}
.logo-box{background: var(--accent); color: var(--on-accent);}
.guide-title{font-size: 12px; font-weight: 600; letter-spacing: 0; text-transform: none; color: var(--soft);}
.g-val{stroke: var(--accent); filter: none;}
.g-band{stroke: var(--control);}
.g-sc{display: flex; justify-content: space-between; max-width: 190px; width: 100%; margin: 0 auto; padding: 0 2px; font: 11px var(--mono); color: var(--muted);}
.g-sc>span:nth-child(2){font-family: var(--sans);}
.d-ok,.sw.ok{stroke: var(--chart-1);}
.sw.ok{background: var(--chart-1);}
.d-bad,.sw.bad{stroke: var(--negative);}
.sw.bad{background: var(--negative);}
.th::after,.kb>i{background: var(--soft);}
.mk-pool{color: var(--muted);}
.mk-rb,.mk-win{color: var(--soft);}
.mk-best{color: var(--accent);}
.mk-blk{color: var(--chart-1);}
#lad-t i{background: var(--soft);}
.bdg{border-color: var(--border); background: var(--raised);}
.bdg-p b,.sl-row.best .sl-d{color: var(--text);}
.tb{background: var(--ground);}
.wb[data-l='1'] i:nth-child(-n+1),.wb[data-l='2'] i:nth-child(-n+2),.wb[data-l='3'] i:nth-child(-n+3),.wb[data-l='4'] i{background: var(--soft);}
details.more>summary::after{color: var(--muted);}

.sheet-h{color: var(--text);}
.settings-h{font-size: 16px; font-weight: 600; color: var(--text); border-bottom-color: var(--border);}

.dn-lg>span{white-space: nowrap;}
.dn-lg>span.muted{white-space: normal;}
.tb-in{padding-left: max(16px,env(safe-area-inset-left)); padding-right: max(16px,env(safe-area-inset-right));}
.vz-sub,.vz-sub b,.vz-sub span[id],.dn-lg,.sl-row,.bdg,.bdg b,.form-check,.cyd-shot-cap,details.more>summary{font-size: 13px;}
.btn-restart,.btn-reset-stats,.btn-ota{white-space: nowrap;}
/* Share history lives inside the odds card */
.sh-h{margin-top: 8px; font-size: 13px; font-weight: 600; color: var(--text-soft);}
.set-btn,.sheet-h{display: none;}
.lrn .bt-t{pointer-events: none;}
/* Phone (600px and below): the top bar and health strip stay put, the
   dashboard pane scrolls on its own, settings open as a full-height sheet
   from the top bar. Targets are 44px, text 13px+, inputs 16px (no iOS zoom). */
@media (max-width: 600px){
body{font-size: 14px;}
.tb-in{gap: 8px; min-height: 56px; padding: max(6px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) 6px max(16px,env(safe-area-inset-left));}
.brand{flex: 1 1 auto; min-width: 0;}
.brand-t{display: flex;}
.brand-subtitle{display: none;}
.brand-title{font-size: 17px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;}
.logo-box{width: 30px; height: 30px; font-size: 16px;}
.stc{flex: none; gap: 8px;}
.coin-pill{display: none;}
.status-badge{font-size: 14px; flex: none;}
.lrn{flex: none; width: 44px; height: 44px; min-width: 44px; min-height: 44px; padding: 0; font-size: 19px; font-weight: 400;}
.lrn::before{content: attr(data-i);}
.lrn .bt-t{display: none;}
.set-btn{display: inline-flex;}
.container{padding: 8px max(16px,env(safe-area-inset-right)) 0 max(16px,env(safe-area-inset-left));}
.kpi,.kpi:has(.kc){grid-template-columns: repeat(2,minmax(0,1fr)); margin-bottom: 8px;}
.k{padding: 0 12px; min-height: 40px;}
.kc{flex-direction: row; align-items: center; justify-content: flex-start; gap: 8px;}
.kc>span:last-child{display: flex; flex: 1 1 auto; align-items: baseline; justify-content: space-between; gap: 8px; min-width: 0;}
.kc .kl{font-size: 13px;}
.kc .kv{font: 600 14px var(--sans); font-variant-numeric: tabular-nums;}
.kc .kv small{font-size: 12px; margin-left: 2px;}
.console-layout{gap: 8px; padding-bottom: calc(12px + env(safe-area-inset-bottom));}
#pane-telemetry{gap: 8px;}
.tele-card{padding: 12px;}
.tele-card-title,.sh-h{font-size: 14px;}
.vz-sub,.vz-sub b,.vz-sub span[id],.dn-lg{font-size: 13px;}
.vz-k,.vz-tag,.bdg i,.rnd>.vz-k:last-child{font-size: 12px;}
.mk{font-size: 11px;}
.lad{height: 52px;}
.mk-pool,.mk-rb,.mk-blk{top: 38px;}
.hs-v{font-size: 13px; letter-spacing: 0;}
.bdg{font-size: 13px; padding: 5px 10px;}
.bdg b{font-size: 13px;}
.sl-row{grid-template-columns: 5em minmax(0,1fr) 4.4em 5.4em; gap: 6px; padding: 4px 4px; font-size: 13px;}
.sl-row.hdr{font-size: 12px;}
#pane-telemetry .share-log{flex: none; max-height: none;}
#pane-telemetry>.tele-card:last-child{min-height: 0;}
.btn-reset-stats{min-height: 44px; min-width: 64px; font-size: 13px;}
.cyd-btn-nav{min-height: 44px; padding: 0 16px; font-size: 14px;}
.cyd-shot-cap{font-size: 13px;}
#pane-settings{display: none;}
#pane-settings:target{display: flex; flex-direction: column; gap: 8px; position: fixed; top: 0; right: 0; bottom: 0; left: 0; z-index: 70; margin: 0; padding: 0 max(16px,env(safe-area-inset-right)) 0 max(16px,env(safe-area-inset-left)); overflow-y: auto; overscroll-behavior: contain; background: var(--bg);}
.sheet-h{display: flex; justify-content: space-between; align-items: center; position: sticky; top: 0; z-index: 3; padding: max(6px,env(safe-area-inset-top)) 0 6px; background: var(--bg); border-bottom: 1px solid var(--border); font-size: 17px; font-weight: 600; color: var(--text);}
.lx{min-height: 44px; font-size: 14px;}
#pane-settings .settings-sec+.settings-sec{margin-top: 0;}
#pane-settings .settings-sec{gap: 8px;}
#pane-settings>.actions{position: sticky; bottom: 0; left: auto; right: auto; margin: auto 0 0; padding: 8px 0 max(8px,env(safe-area-inset-bottom)); z-index: 3;}
.btn-save,.actions .btn-restart{height: 48px; min-height: 48px; font-size: 15px;}
.cfg-card-hdr{padding: 12px 12px 0;}
.cfg-card-title{font-size: 14px;}
.cfg-card-body{padding: 8px 12px 12px; gap: 12px;}
.form-label,.form-label-sub{font-size: 13px;}
input[type=text],input[type=number],input[type=password],input[type=color],select,.form-control,.file-input{height: 44px;}
.theme-grid{gap: 12px;}
.theme-card,.theme-card.active{width: 44px; height: 44px; min-height: 44px;}
.form-hint{font-size: 13px;}
input[type=text],input[type=number],input[type=password],select,.form-control{font-size: 16px; min-height: 44px; padding: 8px 10px;}
input[type=color]{min-height: 44px;}
.file-input{font-size: 16px; min-height: 44px;}
.btn-ota{min-height: 44px; font-size: 14px;}
.form-check{align-items: center; min-height: 44px; font-size: 14px;}
.form-check input[type=checkbox]{width: 22px; height: 22px; margin-top: 0;}
details.more>summary{min-height: 44px; font-size: 14px;}
.preset-pill{min-height: 44px; padding: 6px 12px; font-size: 13px;}
.theme-card .swatch-dot{width: 22px; height: 22px;}
.btn-restart{min-height: 44px; font-size: 14px;}
input[type=range]{height: 6px; margin: 14px 0;}
input[type=range]::-webkit-slider-thumb{width: 28px; height: 28px;}
input[type=range]::-moz-range-thumb{width: 28px; height: 28px;}
.form-grid-3{grid-template-columns: minmax(0,1fr) minmax(0,1fr);}
.form-grid-3>:first-child{grid-column: 1/-1;}
.form-grid-2{grid-template-columns: minmax(0,1fr);}

}
@media (prefers-reduced-motion: reduce){html{scroll-behavior: auto;}input[type=text],input[type=number],input[type=password],input[type=color],select,.form-control,.ota-progress-fill,.kb>i,.g-val,.d-ok,.d-bad{transition: none;}.spinner,.bdg-p.on .glow{animation: none;}}
)rawcss";

static const char DASH_JS[] PROGMEM = R"rawjs(
let toastTimer=null;
function showToast(msg,isErr){
const t=document.getElementById('thm-toast');if(!t)return;
t.textContent=msg;t.className='toast show'+(isErr?' err':'');
if(toastTimer)clearTimeout(toastTimer);
toastTimer=setTimeout(()=>{t.className='toast';},isErr?4000:2500);
}
function postJson(url){return fetch(url,{method:'POST'}).then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return r.json();});}
function choosePreset(h,p,pass,note){document.getElementById('f-pool').value=h;document.getElementById('f-port').value=p;document.getElementById('f-pass').value=pass;const n=document.getElementById('preset-note');if(n)n.textContent=note;if(window.event&&window.event.target){document.querySelectorAll('.preset-pill').forEach(b=>{if(b.closest('#pane-stratum'))b.classList.remove('active');});window.event.target.classList.add('active');}}
function setPool(h,p,pass){choosePreset(h,p,pass,'Switched pool to '+h+':'+p);}
function updateBright(v){document.getElementById('b-val').textContent=v+' ('+Math.round(v*100/255)+'%)';}
function applyTz(v){if(v!=='')document.getElementById('f-tz').value=v;}
function selectTheme(id,col){
const el=document.getElementById('thm-'+id);
if(el&&el.classList.contains('busy'))return;
const prevId=document.getElementById('f-theme').value;
const prevCol=getComputedStyle(document.documentElement).getPropertyValue('--amber');
document.getElementById('f-theme').value=id;
document.querySelectorAll('.theme-card').forEach(c=>c.classList.remove('active'));
if(el)el.classList.add('active','busy');
if(col){document.documentElement.style.setProperty('--amber',col);}
postJson('/api/theme?id='+id).then(()=>{showToast('Theme applied');}).catch(()=>{
document.getElementById('f-theme').value=prevId;
document.querySelectorAll('.theme-card').forEach(c=>c.classList.toggle('active',c.id==='thm-'+prevId));
if(prevCol)document.documentElement.style.setProperty('--amber',prevCol.trim());
showToast('Could not apply theme',true);
}).finally(()=>{if(el)el.classList.remove('busy');});}
function toggleInvert(inv){
const cb=document.getElementById('cb-invert');if(cb)cb.disabled=true;
postJson('/api/invert?val='+(inv?'1':'0')).then(()=>{showToast(inv?'Colors inverted':'Colors restored');}).catch(()=>{
if(cb)cb.checked=!inv;showToast('Could not change colors',true);
}).finally(()=>{if(cb)cb.disabled=false;});}
function recal(what){if(!confirm('Restart the miner and redo the on-screen '+(what==='touch'?'touch':'color')+' check?'))return;
const b=document.getElementById('btn-recal-'+what);if(b){b.disabled=true;b.textContent='Restarting\u2026';}
postJson('/api/recalibrate-'+what).then(()=>{showToast('Restarting \u2014 follow the prompt on the miner');pollReboot();}).catch(()=>{if(b){b.disabled=false;b.textContent=what==='touch'?'Recalibrate touch':'Recalibrate colors';}showToast('Could not restart the miner',true);});}
function pickCity(v){
if(!v)return;
const p=v.split(',');
if(p.length===2){
document.getElementById('f-lat').value=p[0];
document.getElementById('f-lon').value=p[1];
}
}
function pollReboot(){let s=8;document.getElementById('overlay').style.display='flex';
const t=setInterval(()=>{s--;const cd=document.getElementById('cd');if(cd&&s>=0)cd.textContent=s;
if(s<=0){clearInterval(t);checkBack();}},1000);}
function checkBack(){fetch('/',{cache:'no-store'}).then(()=>location.replace('/')).catch(()=>setTimeout(checkBack,1000));}
document.getElementById('cfg-form').addEventListener('submit',function(e){
const b=document.getElementById('btn-save');if(b){b.disabled=true;b.textContent='Saving\u2026';}
const r=document.querySelector("select[name='rot']");
const initR=this.dataset.initRot;
if(r&&r.value!==initR){
setTimeout(pollReboot,100);
}
});
function rebootDevice(){if(confirm('Reboot the miner now?')){const b=document.getElementById('btn-reboot');if(b){b.disabled=true;b.textContent='Rebooting\u2026';}fetch('/restart',{method:'POST'}).then(pollReboot).catch(pollReboot);}}
function uploadOta(){
const fi=document.getElementById('ota-file');
if(!fi||!fi.files||!fi.files.length){showToast('Choose a firmware .bin file first',true);return;}
const f=fi.files[0];
if(!f.name.endsWith('.bin')){if(!confirm('This file does not end in .bin. Flash it anyway?'))return;}
const pBox=document.getElementById('ota-progress-box'),pFill=document.getElementById('ota-progress-fill'),pStat=document.getElementById('ota-status'),btn=document.getElementById('btn-ota-start');
const label=btn?btn.textContent:'';
function done(msg){if(btn){btn.disabled=false;btn.textContent=label;}fi.disabled=false;if(pStat)pStat.textContent=msg;showToast(msg,true);}
if(pBox)pBox.style.display='block';
if(btn){btn.disabled=true;btn.textContent='Uploading\u2026';}
fi.disabled=true;
if(pStat)pStat.textContent='Uploading '+f.name+' ('+Math.round(f.size/1024)+' KB)';
const fd=new FormData();
fd.append('firmware',f,f.name);
const xhr=new XMLHttpRequest();
xhr.open('POST','/update',true);
xhr.upload.onprogress=function(e){if(e.lengthComputable&&pFill){const pct=Math.round((e.loaded/e.total)*100);pFill.style.width=pct+'%';if(btn)btn.textContent=pct<100?'Uploading '+pct+'%':'Flashing\u2026';if(pStat)pStat.textContent=pct+'% ('+Math.round(e.loaded/1024)+' / '+Math.round(e.total/1024)+' KB)';}};
xhr.onload=function(){
if(xhr.status===200){
if(btn)btn.textContent='Rebooting\u2026';
if(pStat)pStat.textContent='Update installed. Rebooting the miner\u2026';
showToast('Update installed. Rebooting\u2026');
setTimeout(pollReboot,500);
}else{
done('Update failed: '+(xhr.responseText||'HTTP '+xhr.status));
}
};
xhr.onerror=function(){done('Upload failed: connection lost');};
xhr.send(fd);
}
function fmtNum(n){if(isNaN(n)||n<=0)return'0';if(n>=1e12)return(n/1e12).toFixed(2)+' trillion';if(n>=1e9)return(n/1e9).toFixed(1)+' billion';if(n>=1e6)return(n/1e6).toFixed(1)+' million';return Math.round(n).toLocaleString();}
function fmtUptime(u){if(!u)return'--';const p=u.trim().split(/\s+/);let d=0,t=u;if(p.length===2){d=parseInt(p[0])||0;t=p[1];}const c=t.split(':');if(c.length===3){const hrs=parseInt(c[0])||0,mins=parseInt(c[1])||0;return(d>0?d+'d ':'')+(hrs>0?hrs+'h ':'')+mins+'m';}return u;}
function resetSession(){
if(confirm('Reset this session\u2019s shares and best difficulty? Lifetime hashes stay saved.')){
const b=document.getElementById('btn-reset-session');if(b){b.disabled=true;b.textContent='Resetting\u2026';}
postJson('/api/reset-session').then(()=>{showToast('Session stats reset');pollStats();}).catch(()=>{showToast('Could not reset stats',true);}).finally(()=>{if(b){b.disabled=false;b.textContent='Reset';}});}}
function fmtDiffC(v){if(!(v>0))return'0';if(v<1)return v.toPrecision(2);const u=['','K','M','G','T','P','E'];let i=0;while(v>=1000&&i<6){v/=1000;i++;}return i?(v>=100?v.toFixed(0):v.toFixed(1))+u[i]:(v>=100?v.toFixed(0):String(Number(v.toPrecision(3))));}
function fmtAgo(now,t){if(!now||!t)return'--';const s=Math.max(0,now-t);if(s<60)return'just now';if(s<3600)return Math.floor(s/60)+' min ago';if(s<86400)return Math.floor(s/3600)+' h ago';return Math.floor(s/86400)+' d ago';}
function renderShareLog(d,net){
const log=Array.isArray(d.share_log)?d.share_log:[],pool=Number(d.pool_diff)||1,now=Number(d.now_utc)||0;
let best=Number(d.best_diff_raw)||0;log.forEach(s=>{if(s.d>best)best=s.d;});
const set=(id,t)=>{const e=document.getElementById(id);if(e)e.textContent=t;};if(d.pool){set('dyn-poolcoin','\u00b7 '+(d.coin||'')+' on '+d.pool);set('sl-coin','\u00b7 '+(d.coin||''));}
set('sl-pool',fmtDiffC(pool));set('sl-net',net>0?fmtDiffC(net):'unknown until the pool sends a job');
if(best>0)set('sl-best',fmtDiffC(best));
if(best>0&&net>0){set('sl-best',fmtDiffC(best));set('sl-best-gap','needs '+fmtDiffC(net/best)+'\u00d7 more \u00b7 '+Math.log10(net/best).toFixed(1)+' orders to go');}
const box=document.getElementById('share-log');if(!box)return;
if(!log.length){box.innerHTML="<div class='muted' style='font-size:13px;padding:6px 0'>No shares recorded yet. One lands about every "+(d.hashrate>0?Math.round(pool*4294967296/(parseFloat(d.hashrate)*1000)/60)+' min':'while')+'.</div>';return;}
const lo=Math.log10(Math.min(pool,...log.map(s=>s.d>0?s.d:pool))),hi=net>0?Math.log10(net):lo+6,span=Math.max(hi-lo,0.1);
let h="<div class='sl-row hdr'><span>Difficulty</span><span>Pool floor \u2192 block</span><span class='sl-r'>Orders to go</span><span class='sl-r'>When</span></div>";
log.slice().reverse().forEach(s=>{const pct=Math.max(1,Math.min(100,(Math.log10(s.d)-lo)/span*100));const cls='sl-row'+(s.ok?'':' rej')+(s.d>=best*0.9999?' best':'');
h+="<div class='"+cls+"'><span class='sl-d'>"+fmtDiffC(s.d)+(s.ok?'':' (rejected)')+"</span><span class='sl-bar' title='"+pct.toFixed(1)+"% of the way on a log scale'><i style='width:"+pct.toFixed(1)+"%'></i></span><span class='sl-r'>"+(net>0?Math.log10(net/s.d).toFixed(1):'--')+"</span><span class='sl-r'>"+fmtAgo(now,s.t)+"</span></div>";});
box.innerHTML=h;}
function viz(d){const $=id=>document.getElementById(id),sv=(e,k,v)=>{if(e)e.style.setProperty(k,v);},hr=parseFloat(d.hashrate)||0,gp=Math.max(0,Math.min(hr/1200*100,100));
const ga=$('g-arc');if(ga)ga.setAttribute('stroke-dasharray',gp.toFixed(1)+' 100');const gh=$('g-hr');if(gh)gh.textContent=d.hashrate||'--';const g=$('g');if(g)g.setAttribute('aria-label','Hashrate '+(d.hashrate||'--')+' KH/s on a 0 to 1200 scale; expected band about 950 to 1050');sv($('k-hrb'),'--v',gp+'%');
const ok=Number(d.shares_accepted)||0,bad=Number(d.shares_rejected)||0,tot=ok+bad,op=tot?ok/tot*100:0,a=$('dn-ok'),b=$('dn-bad');
if(a)a.setAttribute('stroke-dasharray',op.toFixed(2)+' 100');if(b){b.setAttribute('stroke-dasharray',(tot?100-op:0).toFixed(2)+' 100');b.setAttribute('stroke-dashoffset',(-op).toFixed(2));}
const st=(id,t)=>{const e=$(id);if(e)e.textContent=t;};st('dn-n',ok.toLocaleString());st('dn-okn',ok.toLocaleString());st('dn-badn',bad.toLocaleString());const dn=$('dn');if(dn)dn.setAttribute('aria-label',ok+' shares accepted, '+bad+' rejected');
const pool=Number(d.pool_diff)||1,net=Number(d.net_diff)||0,lg=Array.isArray(d.share_log)?d.share_log:[],L=Math.log10,bd=Number(d.best_diff_raw)||0,top=Math.max(net,bd,Number(d.last_winner_diff)||0);let lo=L(pool)-1,hi=top>0?L(top)+0.15:L(pool)+6;if(hi-lo<1)hi=lo+1;
const pos=x=>Math.max(0,Math.min(100,(L(x)-lo)/(hi-lo)*100)),mk=(id,x)=>{const e=$(id);if(!e)return;if(x>0){const p=pos(x);e.style.left=p+'%';e.style.setProperty('--sh',p.toFixed(1));e.style.display='';e.dataset.p=p;e.textContent=e.dataset.n+' '+fmtDiffC(x);e.classList.remove('nl');e.title=e.title.split(':')[0]+': '+fmtDiffC(x);}else e.style.display='none';};
mk('mk-blk',net);mk('mk-pool',pool);mk('mk-best',bd);mk('mk-rb',Number(d.round_best)||0);mk('mk-win',Number(d.last_winner_diff)||0);const ov=(a,b)=>{const A=a.getBoundingClientRect(),B=b.getBoundingClientRect();return A.width&&B.width?(A.left<B.right+6&&B.left<A.right+6):Math.abs(a.dataset.p-b.dataset.p)<22;};[['mk-best','mk-win'],['mk-blk','mk-pool','mk-rb']].forEach(r=>{const k=r.map($).filter(e=>e&&e.style.display!=='none');k.forEach((e,i)=>{for(let j=0;j<i;j++)if(!k[j].classList.contains('nl')&&ov(e,k[j])){e.classList.add('nl');e.textContent='';break;}});});
const t=$('lad-t');if(t)t.innerHTML=lg.map((s,i)=>"<i class='"+(s.ok?'':'rj')+"' style='left:"+pos(s.d).toFixed(1)+"%;opacity:"+(0.3+0.7*(i+1)/lg.length).toFixed(2)+"' title='share "+fmtDiffC(s.d)+"'></i>").join('');
const ld=$('lad');if(ld)ld.setAttribute('aria-label','Log scale from pool target '+fmtDiffC(pool)+' to block '+(net>0?fmtDiffC(net):'unknown')+'; session best '+fmtDiffC(bd)+'; '+lg.length+' recent shares');
const bp=$('bp');if(bp)bp.className='bdg bdg-p '+(d.status==='hashing'?'on':(d.status==='connecting'?'wait':'off'));if(d.pool){st('bp-c',d.coin||'');st('bp-n',d.pool+(d.using_fallback?' (fallback)':''));}
const tc=parseFloat(d.temp),it=$('ic-t');if(it){sv(it,'--v',isNaN(tc)?'0%':Math.max(0,Math.min(100,(tc-20)/70*100))+'%');it.classList.toggle('hot',tc>70);}
if(d.rssi!==undefined){const q=Math.min(Math.max(2*(d.rssi+100),0),100),w=$('ic-w');if(w)w.dataset.l=q>=75?4:q>=50?3:q>=25?2:q>0?1:0;}
const pg=Number(d.ping),ip=$('ic-p');if(ip)ip.className='ic pg '+(!(pg>=0)?'':pg<150?'g':pg<500?'a':'r');
const im=$('ic-m');if(im&&d.free_heap){sv(im,'--v',Math.min(100,d.free_heap/65536*100)+'%');im.classList.toggle('low',d.free_heap<20480);}
const hpd=hr*1000*86400,dayOdds=hpd>0&&net>0?net*4294967296/hpd:0;if(dayOdds>0&&dayOdds<1){st('dyn-dayodds','~'+(1/dayOdds).toFixed(1)+' / day');st('dyn-yrodds','~'+Math.round(365.25/dayOdds).toLocaleString()+' / yr');st('dyn-avgwait',(dayOdds*24).toFixed(1)+' h');}}
function updateStats(d){
const hr=parseFloat(d.hashrate)||0,hps=Math.round(hr*1000),hpd=hr*1000*86400;
const eCoin=document.getElementById('st-coin');if(eCoin&&d.pool){let h=d.pool.split(':')[0].replace(/^((stratum|pool|solo|www|eu|us)\.)+(?=.*\.)/,'');h=/^shears\./.test(h)?'Shears':h.split('.')[0];eCoin.textContent=(d.coin||'')+' \u00b7 '+h;eCoin.title=d.pool;}const eTopHr=document.getElementById('st-top-hr');if(eTopHr&&d.hashrate)eTopHr.textContent=d.hashrate;const eK=document.getElementById('k-hr');if(eK&&d.hashrate)eK.textContent=d.hashrate;
const mhashes=Number(d.mhashes)||0,totHashes=mhashes*1e6,shares=parseInt(d.shares)||0,diff=parseFloat(d.best_diff)||0;
const eHps=document.getElementById('dyn-hps');if(eHps)eHps.textContent=hps.toLocaleString();
const eHpd=document.getElementById('dyn-hpd');if(eHpd)eHpd.textContent=fmtNum(hpd);
const eUp=document.getElementById('dyn-uptime');if(eUp)eUp.textContent=fmtUptime(d.uptime);
const eTot=document.getElementById('dyn-totwork');if(eTot)eTot.textContent=fmtNum(totHashes);
const eSh=document.getElementById('dyn-shares');if(eSh)eSh.textContent=shares.toLocaleString();
const hPerShare=shares>0?(totHashes/shares):0;
const eHpshare=document.getElementById('dyn-hpshare');if(eHpshare)eHpshare.textContent=fmtNum(hPerShare)+' hashes';
const tPerShareSec=(hps>0&&hPerShare>0)?(hPerShare/hps):0;
const eTpshare=document.getElementById('dyn-tpshare');if(eTpshare){if(tPerShareSec>=3600)eTpshare.textContent=(tPerShareSec/3600).toFixed(1)+' hours';else if(tPerShareSec>=60)eTpshare.textContent=Math.round(tPerShareSec/60)+' minutes';else eTpshare.textContent=Math.round(tPerShareSec)+' seconds';}
const eDiff=document.getElementById('dyn-diff');if(eDiff)eDiff.textContent=diff>0?diff.toFixed(3):'0.000';
const rarity=diff*4294967296;
const eRar=document.getElementById('dyn-rarity');if(eRar)eRar.textContent=diff>0?fmtNum(rarity):'N/A';
const netKnown=Number(d.net_diff)>0,netDiff=netKnown?Number(d.net_diff):1;
const eNetdiff=document.getElementById('dyn-netdiff');if(eNetdiff)eNetdiff.textContent=fmtNum(netDiff);
const eNetpct=document.getElementById('dyn-netpct');if(eNetpct){if(diff>0){const pct=(diff/netDiff)*100;eNetpct.textContent=pct.toExponential(2)+'%';}else eNetpct.textContent='0%';}
const targetHashes=netDiff*4294967296,dayOdds=hpd>0?(targetHashes/hpd):0,yrOdds=dayOdds/365.25;
const eDayOdds=document.getElementById('dyn-dayodds');if(eDayOdds)eDayOdds.textContent=dayOdds>0?('1 in '+fmtNum(dayOdds)):'--';
const eYrOdds=document.getElementById('dyn-yrodds');if(eYrOdds)eYrOdds.textContent=yrOdds>0?('1 in '+fmtNum(yrOdds)):'--';
const eAvgWait=document.getElementById('dyn-avgwait');if(eAvgWait)eAvgWait.textContent=yrOdds>0?(fmtNum(yrOdds)+' years'):'--';
const netHps=netDiff*4294967296/(Number(d.block_s)||600),eNetpow=document.getElementById('dyn-netpow');const eNetHr=document.getElementById('dyn-nethr');if(eNetHr){const u=['H/s','KH/s','MH/s','GH/s','TH/s','PH/s','EH/s','ZH/s'];let v=netHps,i=0;while(v>=1000&&i<7){v/=1000;i++;}eNetHr.textContent=v.toFixed(v>=100?0:1)+' '+u[i];}if(eNetpow){const netSharePct=(hps/netHps)*100;eNetpow.textContent=netSharePct.toExponential(1)+'%';}
if(d.temp){const e=document.getElementById('st-temp');if(e)e.textContent=d.temp;}
if(d.uptime){const u=document.getElementById('st-uptime');if(u){const m=d.uptime.match(/^(\d+)\s+(\d{2}:\d{2}:\d{2})$/);u.textContent=m?(parseInt(m[1])>0?m[1]+'d '+m[2]:m[2]):d.uptime;}}
const ePing=document.getElementById('st-ping');if(ePing&&d.ping!==undefined)ePing.textContent=d.ping;
const eJob=document.getElementById('dyn-lastjob');if(eJob&&d.last_job_s!==undefined){const s=d.last_job_s;eJob.textContent=s<0?'waiting for pool':(s<60?s+'s ago':(s<3600?Math.floor(s/60)+'m ago':Math.floor(s/3600)+'h ago'));}
{const fd=v=>{v=Number(v);if(!(v>0))return '0';const u=['','K','M','B','T','P','E'];let i=0;while(v>=1000&&i<6){v/=1000;i++;}return (i===0?(v>=100?v.toFixed(0):v.toPrecision(3)):(v>=100?v.toFixed(0):v.toFixed(1)))+u[i];};
const eRt=document.getElementById('dyn-rtries');if(eRt&&d.round_tries!==undefined){eRt.textContent=fd(d.round_tries);const ex=Number(d.round_expected)||0,p=ex>0?d.round_tries/ex*100:0;const eP=document.getElementById('dyn-rpct');if(eP)eP.textContent=ex>0?('('+p.toFixed(0)+'% of a '+(function(b){b=Number(b)||600;return b%60?b+'-s':(b/60)+'-min';})(d.block_s)+' round'+(d.round_partial?', since boot':'')+')'):'';const eB=document.getElementById('dyn-rbar');if(eB){eB.style.width=Math.min(p,100)+'%';eB.style.background=p>100?'var(--accent)':'var(--accent)';}}
const fr=fd;
const eRb=document.getElementById('dyn-rbest');if(eRb&&d.round_best!==undefined){eRb.textContent=fr(d.round_best);eRb.title='score (difficulty) '+fd(d.round_best);}
const eRw=document.getElementById('dyn-rwin');if(eRw&&d.last_winner_diff!==undefined){eRw.textContent=d.last_winner_diff>0?('#'+Number(d.last_winner_height).toLocaleString()+' score '+fr(d.last_winner_diff)):'waiting for a job';eRw.title='score (difficulty) '+fd(d.last_winner_diff)+(d.last_winner_ok?' - beats the network difficulty':'');}
const eHs=document.getElementById('dyn-hstrip');if(eHs&&d.round_best_hash!==undefined){const h=(d.round_best_hash||'').slice(0,20),wz=((d.last_winner_hash||'').match(/^0*/)||[''])[0].length,wn=Math.min(wz,20);let o='';for(let i=0;i<20;i++){const c=h[i]||'-';o+=(c==='0'?'<span style="background:var(--accent);color:var(--on-accent)">0</span>':'<span style="color:var(--muted)">'+c+'</span>');if(i+1===wn)o+='<span style="color:var(--accent)" title="winner zeros">|</span>';}eHs.innerHTML=o+(wn>0?' <span class="muted" style="font-family:system-ui;font-size:11px;letter-spacing:0">winner: '+wz+' zeros</span>':'');}
const eRr=document.getElementById('dyn-rounds');if(eRr&&d.rounds)eRr.textContent=d.rounds.length?d.rounds.slice(0,5).map(r=>fr(r.my_best)+' vs '+fr(r.winner)).join(' | '):'none yet (one per block)';}
const eOkBad=document.getElementById('dyn-okbad');if(eOkBad&&d.shares_accepted!==undefined)eOkBad.textContent=d.shares_accepted+' ok / '+d.shares_rejected+' bad';
if(d.free_heap){const e=document.getElementById('st-heap');if(e)e.textContent=Math.round(d.free_heap/1024)+' KB';}
if(d.rssi!==undefined){const q=Math.min(Math.max(2*(d.rssi+100),0),100);const w=document.getElementById('st-wifi'),r=document.getElementById('st-rssi');if(w)w.textContent=q+'%';if(r)r.textContent=d.rssi;}
const txt=document.getElementById('st-txt');if(txt){
if(d.status==='hashing'){txt.className='txt-green';txt.textContent=d.using_fallback?'Hashing (fallback)':'Hashing';}
else if(d.pool_error){txt.className='txt-amber';txt.textContent='Pool refused: '+d.pool_error;}
else if(d.status==='connecting'){txt.className='txt-amber';txt.textContent='Connecting';}
else{txt.className='txt-gray';txt.textContent='Idle';}}
if(!netKnown)['dyn-netdiff','dyn-netpct','dyn-dayodds','dyn-yrodds','dyn-avgwait','dyn-netpow','dyn-nethr'].forEach(id=>{const e=document.getElementById(id);if(e)e.textContent='--';});
viz(d);renderShareLog(d,netKnown?netDiff:0);
cydLastStats=d;if(typeof cydRetry==='function')cydRetry();if(d.screen!==undefined){cydDevScr=d.screen;cydUpdateDevBadge();cydSyncInitialTab();}cydRender();}
function pollStats(){fetch('/api/stats').then(r=>r.json()).then(updateStats).catch(()=>{});}
document.getElementById('cfg-form').addEventListener('keydown',function(e){if(e.key==='Enter'&&e.target.tagName==='INPUT'&&!e.target.name)e.preventDefault();});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&location.hash==='#pane-settings')location.hash='ov';});
)rawjs";

// Bump the suffix when the page CSS/JS changes so browsers drop the 24 h cache.
#define WEB_UI_V FW_GIT "-ux14"
static const char *const kThemeAccent[] = {"#f7931a", "#22c55e", "#f59e0b", "#06b6d4", "#38bdf8", "#ffffff"};
static const char *const kThemeName[] = {"Instrument Amber", "Matrix Green", "Cyber Amber", "Arcade Neon", "Ice Blue", "Monochrome"};

// The settings as the page should show them: what is in use, or what was
// saved and is still waiting for the stratum or monitor task to apply it.
static TSettings currentSettings()
{
  TSettings s;
  {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    s = Settings;
  }
  readPoolSettings(s);
  s.Currency = currencySetting();
  return s;
}

static void handleRoot()
{
  const TSettings Settings = currentSettings(); // shadows global Settings with live/pending
  mining_data d = getMiningData(0);
  String fullWallet = String(Settings.BtcWallet);
  String cleanWallet = getCleanWallet(fullWallet);
  String worker = getWorkerName(fullWallet);
  bool isPublicPool = Settings.PoolAddress.indexOf("public-pool") >= 0;
  const CoinProfile &coin = getActiveCoinProfile();

  String page;
  // Sent in parts (chunked, as upstream #17 does): the whole page never has to
  // sit in one String. One ~32 KB String needed a contiguous block bigger than
  // the heap usually has (max_alloc ~21-24 KB), so the page came out cut.
  page.reserve(6000);
  bool started = false;
  auto flush = [&]() {
    if (!started)
    {
      webCfgServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
      webCfgServer.send(200, "text/html", page);
      started = true;
    }
    else if (page.length())
      webCfgServer.sendContent(page);
    page = "";
  };

  if (page.length() > 3500) flush();
  int thIdx = Settings.displayTheme;
  if (thIdx < 0 || thIdx > 5) thIdx = 0;
  page += F("<!DOCTYPE html><html lang='en' style='--amber:");
  page += kThemeAccent[thIdx];
  page += F("'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'>"
            "<meta name='theme-color' content='#171719'>"
            "<title>Aaron Miner</title>"
            "<link rel='stylesheet' href='/dash.css?v=" WEB_UI_V "'>"
            "<link rel='stylesheet' href='/cyd-twin.css?v=" WEB_UI_V "'></head><body>"
            "<a class='skip' href='#main'>Skip to settings</a>"
            "<header class='tb'><div class='tb-in'>"
            "<a class='brand' href='#ov' title='" CURRENT_VERSION " (" FW_GIT ")'>"
            "<span class='logo-box' id='site-logo' aria-hidden='true'>&#8383;</span>"
            "<span class='brand-t'><h1 class='brand-title'>Aaron Miner</h1><span class='brand-subtitle'>");
  page += htmlEscape(WiFi.localIP().toString()) + F(" &middot; ") + htmlEscape(WiFi.SSID());
  page += F("</span></span></a>");
  {
    char key[64], shortName[24];
    const char *coin = "BTC";
    getActivePool(key, sizeof(key), &coin);
    poolShortName(key, shortName, sizeof(shortName));
    page += F("<div class='stc'><div class='coin-pill' id='st-coin' title='");
    page += htmlEscape(String(key));
    page += F("'>");
    page += String(coin) + F(" &middot; ") + htmlEscape(String(shortName));
    page += F("</div>");
  }
  if (page.length() > 3500) flush();
  page += F("<div class='status-badge' role='status' aria-live='polite'><span id='st-txt' class='");
  bool isHashing = (mMonitor.NerdStatus == NM_hashing || (WiFi.status() == WL_CONNECTED && d.currentHashRate.toFloat() > 0));
  if (isHashing)
    page += F("txt-green'>Hashing</span>");
  else if (mMonitor.NerdStatus == NM_Connecting)
    page += F("txt-amber'>Connecting</span>");
  else
    page += F("txt-gray'>Idle</span>");
  page += F("</div></div>"
            "<button type='button' class='lrn shot-btn' data-i='&#9635;' onclick='cydSnapshot()' title='Load the device screen mirror' aria-label='Load device screen'><span class='bt-t'>Screen</span></button>"
            "<a class='lrn set-btn' data-i='&#9881;&#65038;' href='#pane-settings' title='Settings' aria-label='Settings'><span class='bt-t'>Settings</span></a>"
            "</div></header>"
            "<div class='container'>"
            "<div class='kpi' id='ov'>"
            "<span class='sr'>Session shares <span id='dyn-shares'></span>, best difficulty <span id='dyn-diff'></span></span>"
            "<div class='k kc'><span class='ic th' id='ic-t' aria-hidden='true'></span><span><span class='kl'>Temp</span><span class='kv'><span id='st-temp'>");
  page += d.temp.length() ? d.temp : String("--");
  page += F("</span><small>&deg;C</small></span></span></div>"
            "<div class='k kc'><span class='ic wb' id='ic-w' aria-hidden='true'><i></i><i></i><i></i><i></i></span><span><span class='kl'>Wi-Fi</span><span class='kv'><span id='st-wifi'>");
  page += String(getWifiQuality(WiFi.RSSI())) + F("%</span><small class='rs'><span id='st-rssi'>");
  page += String(WiFi.RSSI()) + F("</span> dBm</small></span></span></div>"
            "<div class='k kc'><span class='ic pg' id='ic-p' aria-hidden='true'></span><span><span class='kl'>Pool ping</span><span class='kv'><span id='st-ping'>");
  page += String(g_stratumPing) + F("</span><small>ms</small></span></span></div>"
            "<div class='k kc'><span class='ic bt' id='ic-m' aria-hidden='true'></span><span><span class='kl'>Free mem</span><span class='kv' id='st-heap'>");
  page += String(ESP.getFreeHeap() / 1024) + F(" KB</span></span></div>"
            "</div>"
            "<div class='console-layout'>"
            "<aside class='console-sidebar'>");
  appendCydTwinHtml(page);
  if (page.length() > 3500) flush();
  page += F("</aside>"
            "<main class='console-main' id='main'>");

  page += "<form method='POST' action='/save' id='cfg-form' data-init-rot='" + String(Settings.screenRotation) + "'>";

  // Section 1: Overview (single page, no tabs)
  page += F("<section class='page-sec' id='pane-telemetry'>"
            "<div class='tele-card'><div class='vz2'>"
            "<div>"
            "<div class='tele-card-hdr'><div class='tele-card-title' title='Shaded band: this chip tops out near 1 MH/s'>Speed</div></div>"
            "<svg class='gauge' id='g' viewBox='0 0 120 67' role='img' aria-label='Hashrate gauge, 0 to 1.2 MH/s'>"
            "<path class='g-trk' d='M10 62A50 50 0 0 1 110 62' pathLength='100'/>"
            "<path class='g-band' d='M10 62A50 50 0 0 1 110 62' pathLength='100' stroke-dasharray='8.3 100' stroke-dashoffset='-79.2'/>"
            "<path class='g-val' id='g-arc' d='M10 62A50 50 0 0 1 110 62' pathLength='100' stroke-dasharray='0 100'/>"
            "<text class='g-num' id='g-hr' x='60' y='52'>--</text></svg>"
            "<div class='g-sc'><span>0</span><span>KH/s</span><span>1.2M</span></div>"
            "<span class='sr'><span id='dyn-hps'></span> H/s</span>"
            "<div class='vz-sub'><span id='dyn-hpd'>--</span> hashes/day</div>"
            "<div class='vz-sub'>Life <span id='dyn-totwork'>");
  uint64_t mhashes = (uint64_t)d.totalMHashes.toInt();
  if (mhashes >= 1000) page += String((float)mhashes / 1000.0f, 1) + F(" billion");
  else page += String((unsigned long)mhashes) + F(" million");
  if (page.length() > 3500) flush();
  page += F("</span> &middot; job <span id='dyn-lastjob'>--</span></div>"
            "<div class='vz-sub'>Mining for <span id='dyn-uptime'>");
  page += (d.timeMining.length() ? d.timeMining : F("--"));
  page += F("</span></div>"
            "</div>"
            "<div>"
            "<div class='tele-card-hdr'><div class='tele-card-title'>Shares</div>"
            "<button type='button' class='btn-reset-stats' id='btn-reset-session' onclick='resetSession()' title='Reset session shares and best difficulty'>Reset</button></div>"
            "<div class='dn-wrap'>"
            "<svg class='donut' id='dn' viewBox='0 0 80 80' role='img' aria-label='Shares checked by pool'>"
            "<circle class='d-trk' cx='40' cy='40' r='31'/>"
            "<circle class='d-ok' id='dn-ok' cx='40' cy='40' r='31' pathLength='100' stroke-dasharray='0 100'/>"
            "<circle class='d-bad' id='dn-bad' cx='40' cy='40' r='31' pathLength='100' stroke-dasharray='0 100'/>"
            "<text class='d-n' id='dn-n' x='40' y='46'>--</text>"
            "</svg>"
            "<div class='dn-lg'>"
            "<span><i class='sw ok'></i>accepted</span>"
            "<span><i class='sw bad'></i><b id='dn-badn'>--</b> bad</span>"
            "<span class='muted'>1 per <b id='dyn-tpshare'>--</b></span>"
            "</div></div>"
            "<span class='sr'><span id='dyn-okbad'></span> <span id='dyn-hpshare'></span> <span id='dyn-poolcoin'></span></span>"
            "</div>"
            "</div>"
            "</div>" // closes the speed + shares card
            "<div class='tele-card'>"
            "<div class='tele-card-hdr'><div class='tele-card-title'>Odds of a block</div>"
            "<span class='vz-tag'>log scale</span></div>"
            "<span class='sr'><span id='sl-coin'></span> pool <span id='sl-pool'></span> block <span id='sl-net'></span> best <span id='sl-best'></span> round <span id='dyn-rbest'></span> last block <span id='dyn-rwin'></span></span>"
            "<div class='lad' id='lad' role='img' aria-label='Difficulty ladder'>"
            "<div class='lad-bar'></div><div id='lad-t'></div>"
            "<b class='mk mk-pool' id='mk-pool' title='Pool target: a share' data-n='pool'>pool</b>"
            "<b class='mk mk-rb' id='mk-rb' title='Your best this round' data-n='round'>round</b>"
            "<b class='mk mk-best' id='mk-best' title='Your best this session' data-n='best'>best</b>"
            "<b class='mk mk-win' id='mk-win' title='Last block winner' data-n='winner'>winner</b>"
            "<b class='mk mk-blk' id='mk-blk' title='Network difficulty: a block' data-n='block'>block</b>"
            "</div>"
            "<div class='vz-sub'>Best to block: <b id='sl-best-gap'>--</b></div>"
            "<div class='hs'><span class='vz-k'>Best hash</span><span id='dyn-hstrip' class='hs-v'>--</span></div>"
            "<div class='rnd'><span class='vz-k'>Round</span><div class='rbar' role='presentation'><div id='dyn-rbar'></div></div><span class='vz-k'><span id='dyn-rtries'>--</span> <span id='dyn-rpct'></span></span></div>"
            "<div class='vz-sub' title='Recent rounds: my best vs winner'>Rounds <span id='dyn-rounds'>--</span></div>"
            "<div class='bdgs'>"
            "<span class='bdg'><i>reward</i><b>3.125 BTC</b></span>"
            "<span class='bdg'><i>daily</i><b id='dyn-dayodds'>--</b></span>"
            "<span class='bdg'><i>yearly</i><b id='dyn-yrodds'>--</b></span>"
            "<span class='bdg'><i>avg wait</i><b id='dyn-avgwait'>--</b></span>"
            "<span class='bdg'><i>my share</i><b><span id='dyn-netpow'>--</span> of <span id='dyn-nethr'>--</span></b></span>"
            "<span class='sr'><span id='dyn-netpct'></span> of <span id='dyn-netdiff'></span>, rarity <span id='dyn-rarity'></span></span>"
            "<span class='bdg'><i>power</i><b>~1.2 W</b></span>"
            "</div>"
            "<div class='sh-h' title='Newest first'>Share history</div>"
            "<div id='share-log' class='share-log'><div class='muted' style='font-size:12px;padding:6px 0'>No shares yet.</div></div>"
            "</div>"
            "</section>");

  // Section 2: Settings -- Wallet & pool, Display, System
  page += F("<section class='page-sec' id='pane-settings'>"
            "<div class='sheet-h'><span>Settings</span><a class='lx' href='#ov'>Close &times;</a></div>"
            "<section class='settings-sec' id='pane-stratum'>");

  // Wallet: the one setting that decides where a block reward goes
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Wallet &amp; pool</div>"
            "<span class='active-pool-pill");
  page += (onBackupPool() ? F(" fallback'>Using fallback") : F("'>Using primary"));
  page += F("</span>"
            "</div>"
            "<div class='cfg-card-body'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-wallet'>");
  page += String(coin.name) + " (" + String(coin.ticker) + F(") address</label>"
            "<input type='text' id='f-wallet' class='form-control' name='wallet' value='");
  page += htmlEscape(String(Settings.BtcWallet));
  if (page.length() > 3500) flush();
  page += F("' placeholder='Your on-chain address (e.g. bc1q...)' required>"
            "<span class='form-hint'>A found block pays 3.125 ");
  page += String(coin.ticker) + F(" + fees straight to this address. Add <code>.name</code> to set a worker name, e.g. <code>");
  if (page.length() > 3500) flush();
  page += (cleanWallet.length() ? htmlEscape(cleanWallet) : F("bc1q...")) + F(".cyd</code>).</span>"
            "</div>");

  // Pool: primary first, presets next to it, fallback folded away
  if (page.length() > 3500) flush();
  page += F("<div class='form-grid-3'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-pool'>Host</label>"
            "<input type='text' id='f-pool' class='form-control' name='pool' value='");
  page += htmlEscape(Settings.PoolAddress);
  if (page.length() > 3500) flush();
  page += F("' placeholder='public-pool.io' required>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-port'>Port</label>"
            "<input type='number' id='f-port' class='form-control' name='port' value='");
  page += String(Settings.PoolPort);
  if (page.length() > 3500) flush();
  page += F("' placeholder='3333' required>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-pass'>Password</label>"
            "<input type='text' id='f-pass' class='form-control' name='poolpass' value='");
  page += htmlEscape(String(Settings.PoolPassword));
  if (page.length() > 3500) flush();
  page += F("' placeholder='x'>"
            "</div>"
            "</div>"
            "<details class='more preset-bar'><summary>Pool presets</summary>"
            "<div class='preset-pills more-body' style='flex-direction:row'>"
            "<button type='button' class='preset-pill active' onclick='choosePreset(\"public-pool.io\",3333,\"x\",\"public-pool.io:3333 is the recommended port.\")'>public-pool.io:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"pool.nerdminers.org\",3333,\"x\",\"pool.nerdminers.org is a solo pool for low-hashrate miners.\")'>pool.nerdminers.org:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"pool.solomining.de\",3333,\"x\",\"pool.solomining.de is a European solo pool.\")'>pool.solomining.de:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"public-pool.io\",21496,\"x\",\"Legacy port. Use 3333 if connections drop.\")'>public-pool.io:21496 (legacy)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"stratum.shears.co.uk\",3690,\"x\",\"Shears ESP32-only port for Polaris (POL). Mines an altcoin, not BTC; needs a free shears.co.uk account. Payout is to that account, not this BTC address.\")'>Shears ESP32 POL :3690 (needs free account)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"stratum.shears.co.uk\",3700,\"x\",\"Shears ESP32-only port for Embargocoin (EMB). Mines an altcoin, not BTC; needs a free shears.co.uk account. Payout is to that account, not this BTC address.\")'>Shears ESP32 EMB :3700 (needs free account)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"stratum.shears.co.uk\",3710,\"x\",\"Shears ESP32-only port for Fixed Trade Coin (FXTC). Mines an altcoin, not BTC; needs a free shears.co.uk account. Payout is to that account, not this BTC address.\")'>Shears ESP32 FXTC :3710 (needs free account)</button>"
            "</div>"
            "<div id='preset-note' class='preset-comment'>Presets fill in the pool above. Save to apply.</div>"
            "</details>");
  page += (onBackupPool() ? F("<details class='more' open>") : F("<details class='more'>"));
  if (page.length() > 3500) flush();
  page += F("<summary>Fallback pool (optional)</summary>"
            "<div class='more-body'>"
            "<span class='form-hint'>Used after 3 failed connections to the pool above.</span>"
            "<div class='form-grid-3'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-fbpool'>Host</label>"
            "<input type='text' id='f-fbpool' class='form-control' name='fbpool' value='");
  page += htmlEscape(Settings.FallbackPoolAddress);
  if (page.length() > 3500) flush();
  page += F("' placeholder='pool.nerdminers.org'>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-fbport'>Port</label>"
            "<input type='number' id='f-fbport' class='form-control' name='fbport' value='");
  page += String(Settings.FallbackPoolPort);
  if (page.length() > 3500) flush();
  page += F("' placeholder='3333'>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-fbpass'>Password</label>"
            "<input type='text' id='f-fbpass' class='form-control' name='fbpass' value='");
  page += htmlEscape(String(Settings.FallbackPoolPassword));
  if (page.length() > 3500) flush();
  page += F("' placeholder='x'>"
            "</div>"
            "</div>"
            "</div></details>"
            "</div></div>");
  if (isPublicPool && cleanWallet.length() > 0)
  {
    page += F("<div class='pool-banner'>"
              "<div class='pool-info'>"
              "<strong>Mining on public-pool.io</strong><br>"
              "<span class='muted'>Worker: ");
    page += (worker.length() ? htmlEscape(worker) : F("(default)")) + F("</span></div>"
              "<a href='https://web.public-pool.io/#/app/");
    page += htmlEscape(cleanWallet) + F("' target='_blank' rel='noopener' class='pool-btn'>"
              "View on public-pool.io &#8599;</a></div>");
  }

  page += F("</section>" // closes pane-stratum
            "<section class='settings-sec' id='pane-display'>");

  // Display
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Display</div>"
            "</div>"
            "<div class='cfg-card-body'>"
            "<div class='form-grid-2'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-rot'>Orientation</label>"
            "<select name='rot' id='f-rot' class='form-control'>");
  int curRot = Settings.screenRotation;
  if (page.length() > 3500) flush();
  page += "<option value='0'" + String(curRot == 0 ? " selected" : "") + ">Portrait, USB at bottom (default)</option>";
  if (page.length() > 3500) flush();
  page += "<option value='1'" + String(curRot == 1 ? " selected" : "") + ">Landscape, USB on right</option>";
  if (page.length() > 3500) flush();
  page += "<option value='2'" + String(curRot == 2 ? " selected" : "") + ">Portrait flipped, USB at top</option>";
  if (page.length() > 3500) flush();
  page += "<option value='3'" + String(curRot == 3 ? " selected" : "") + ">Landscape flipped, USB on left</option>";
  if (page.length() > 3500) flush();
  page += F("</select>"
            "<span class='form-hint'>Changing orientation restarts the miner when you save.</span>"
            "</div>"
            "<div class='form-group'>"
            "<div class='form-label'>"
            "<span>Brightness</span>"
            "<span id='b-val' class='form-label-sub'>");
  page += String(Settings.Brightness) + " (" + String((Settings.Brightness * 100) / 255) + F("%)</span>"
            "</div>"
            "<div class='slider-track-wrap'>"
            "<input type='range' name='brightness' id='b-slider' min='10' max='255' value='");
  page += String(Settings.Brightness) + F("' oninput='updateBright(this.value)'>"
            "</div>"
            "<label class='form-check'>"
            "<input type='checkbox' name='invert' id='cb-invert' onchange='toggleInvert(this.checked)'");
  if (Settings.invertColors) page += F(" checked");
  if (page.length() > 3500) flush();
  page += F("> <span>Invert colors</span></label>"
            "<div style='display:flex;gap:8px;margin-top:10px'>"
            "<button type='button' class='btn-restart' id='btn-recal-touch' onclick='recal(\"touch\")'>Recalibrate touch</button>"
            "<button type='button' class='btn-restart' id='btn-recal-colors' onclick='recal(\"colors\")'>Recalibrate colors</button>"
            "</div>"
            "<span class='form-hint'>Restarts the miner and runs that check on its screen. Mining resumes right away; hold BOOT to skip.</span>"
            "</div></div>"
            "<div class='form-group'>"
            "<span class='form-label'>Theme</span>"
            "<div class='theme-grid'>");
  int curTheme = Settings.displayTheme;
  for (int i = 0; i < 6; i++)
  {
    page += "<button type='button' class='theme-card" + String(curTheme == i ? " active" : "") + "' id='thm-" + String(i) +
            "' title='" + kThemeName[i] + "' aria-label='" + kThemeName[i] + " theme' onclick='selectTheme(" + String(i) + ",\"" + kThemeAccent[i] + "\")'><span class='swatch-dot' style='background:" +
            kThemeAccent[i] + "'></span><span class='sr'>" + kThemeName[i] + "</span></button>";
  }
  page += F("</div>"
            "<input type='hidden' id='f-theme' name='theme' value='");
  page += String(curTheme) + F("'>"
            "</div>");
#ifdef HAS_BACKLIGHT_CONTROL
  page += F("<details class='more'>"
            "<summary>Sleep and night dimming</summary>"
            "<div class='more-body'>"
            "<div class='form-grid-3'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-screenoff'>Sleep after (min)</label>"
            "<input type='number' id='f-screenoff' class='form-control' name='screenoff' min='0' max='1440' value='");
  page += String(Settings.ScreenTimeout);
  if (page.length() > 3500) flush();
  page += F("' placeholder='0'>"
            "<span class='form-hint'>0 = never. Touch or BOOT wakes it.</span>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-nbright'>Night brightness (1–255)</label>"
            "<input type='number' id='f-nbright' class='form-control' name='nightbright' min='1' max='255' value='");
  page += String(Settings.NightBrightness);
  if (page.length() > 3500) flush();
  page += F("'>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-nfade'>Fade (min)</label>"
            "<input type='number' id='f-nfade' class='form-control' name='nightfade' min='0' max='240' value='");
  page += String(Settings.NightFade);
  if (page.length() > 3500) flush();
  page += F("'>"
            "</div>"
            "</div>"
            "<label class='form-check'>"
            "<input type='checkbox' name='nightdim'");
  if (Settings.nightDim) page += F(" checked");
  if (page.length() > 3500) flush();
  page += F("> <span>Dim at night, based on sunset at the location below</span></label>"
            "<div class='form-grid-3'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-city'>Nearest city</label>"
            "<select id='f-city' onchange='pickCity(this.value)' class='form-control'>"
            "<option value=''>Pick nearest city…</option>");
  for (unsigned i = 0; i < sizeof(kCities) / sizeof(kCities[0]); i++)
  {
    page += "<option value='" + String(kCities[i].latitude, 2) + "," + String(kCities[i].longitude, 2) + "'>" + String(kCities[i].name) + "</option>";
  }
  if (page.length() > 3500) flush();
  page += F("</select></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-lat'>Latitude (&deg;)</label>"
            "<input type='text' id='f-lat' name='lat' class='form-control' value='");
  page += String(Settings.Latitude, 2);
  if (page.length() > 3500) flush();
  page += F("'></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-lon'>Longitude (&deg;)</label>"
            "<input type='text' id='f-lon' name='lon' class='form-control' value='");
  page += String(Settings.Longitude, 2);
  if (page.length() > 3500) flush();
  page += F("'></div>"
            "</div>"
            "<span class='form-hint'>Status: ");
  page += nightDimStatus();
  if (page.length() > 3500) flush();
  page += F("</span></div></details>");
#endif

#ifdef HAS_RGB_LED
  // Rear RGB LED, folded into the Display card
  page += F("<details class='more'>"
            "<summary>Rear LED</summary>"
            "<div class='more-body'>"
            "<div class='form-grid-3'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-ledmode'>Mode</label>"
            "<select name='ledmode' id='f-ledmode' class='form-control'>");
  static const char *const kLedModes[] = {
    "Off",
    "Status (blink while mining, green on share, blue while connecting)",
    "Steady color"
  };
  for (int i = LED_MODE_OFF; i <= LED_MODE_STEADY; i++)
  {
    page += "<option value='" + String(i) + "'";
    if (Settings.ledMode == i) page += " selected";
    page += ">" + String(kLedModes[i]) + "</option>";
  }
  if (page.length() > 3500) flush();
  page += F("</select></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-ledcolor'>Color</label>"
            "<input type='color' id='f-ledcolor' name='ledcolor' class='form-control' value='");
  char colorHex[8];
  snprintf(colorHex, sizeof(colorHex), "#%06lx", (unsigned long)(Settings.ledColor & 0xFFFFFF));
  if (page.length() > 3500) flush();
  page += String(colorHex) + F("'></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-ledbright'>Brightness (1–255)</label>"
            "<input type='number' id='f-ledbright' name='ledbright' class='form-control' min='1' max='255' value='");
  page += String(Settings.ledBrightness) + F("'></div></div>"
            "<label class='form-check'>"
            "<input type='checkbox' name='ledscreen'");
  if (Settings.ledScreenOff) page += F(" checked");
  if (page.length() > 3500) flush();
  page += F("> <span>Turn the LED off when the screen sleeps</span></label>"
            "</div></details>");
#endif

  page += F("</div></div>" // closes Display card
            "</section>"); // closes pane-display


  page += F("<section class='settings-sec' id='pane-system'>");

  // Clock and data
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>System</div>"
            "</div>"
            "<div class='cfg-card-body'>"
            "<div class='form-grid-3'>"
            "<div class='form-group'>"
            "<label class='form-label' for='tz-select'>Timezone</label>"
            "<select id='tz-select' onchange='applyTz(this.value)' class='form-control'>"
            "<option value=''>Pick a timezone…</option>"
            "<option value='-8'>UTC-8 (US Pacific / PST)</option>"
            "<option value='-7'>UTC-7 (US Mountain / MST)</option>"
            "<option value='-6'>UTC-6 (US Central / CST)</option>"
            "<option value='-5'>UTC-5 (US Eastern / EST)</option>"
            "<option value='-4'>UTC-4 (Atlantic / AST)</option>"
            "<option value='0'>UTC+0 (London / GMT)</option>"
            "<option value='1'>UTC+1 (Central Europe / CET)</option>"
            "<option value='2'>UTC+2 (Eastern Europe / EET)</option>"
            "<option value='8'>UTC+8 (China, Singapore, Perth)</option>"
            "<option value='9'>UTC+9 (Japan, Korea)</option>"
            "<option value='10'>UTC+10 (Sydney, Melbourne)</option>"
            "</select></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-tz'>UTC offset</label>"
            "<input type='number' name='tz' id='f-tz' class='form-control' min='-12' max='14' value='");
  page += String(Settings.Timezone) + F("'></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-currency'>Currency</label>"
            "<select name='currency' id='f-currency' class='form-control'>");
  for (int i = 0; i < kCurrencyCount; i++)
  {
    page += "<option value='" + String(kCurrencies[i].code) + "'";
    if (Settings.Currency.equalsIgnoreCase(kCurrencies[i].code))
      page += " selected";
    page += ">" + String(kCurrencies[i].label) + "</option>";
  }
  if (page.length() > 3500) flush();
  page += F("</select></div></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-mempool'>Mempool API</label>"
            "<input type='text' id='f-mempool' name='mempool' class='form-control' maxlength='80' placeholder='" DEFAULT_MEMPOOLHOST "' value='");
  page += htmlEscape(Settings.MempoolHost) + F("'>"
            "<span class='form-hint'>Used for block height, fees and difficulty, e.g. <code>https://mempool.space</code> or a local node like <code>http://umbrel.local:3006</code>.</span>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-check'>"
            "<input type='checkbox' name='savestats'");
  if (Settings.saveStats) page += F(" checked");
  if (page.length() > 3500) flush();
  page += F("> <span>Keep lifetime hashes and shares across reboots (stored in flash)</span></label>"
            "</div>"
            "<details class='more'><summary>Firmware update (OTA)</summary>"
            "<div class='more-body'>"
            "<span class='form-hint'>Running <code>" CURRENT_VERSION " (" FW_GIT ")</code>. Upload a <code>firmware.bin</code> over Wi-Fi. The miner flashes it and reboots.</span>"
            "<div class='ota-row'>"
            "<input type='file' id='ota-file' class='file-input' accept='.bin'>"
            "<button type='button' class='btn-ota' id='btn-ota-start' onclick='uploadOta()'>Upload and flash</button>"
            "</div>"
            "<div class='ota-progress-box' id='ota-progress-box'>"
            "<div class='ota-progress-bar'><div class='ota-progress-fill' id='ota-progress-fill'></div></div>"
            "<div class='ota-status' id='ota-status'>Ready</div>"
            "</div>"
            "</div></details>"
            "<details class='more'>"
            "<summary>Touch and button controls</summary>"
            "<div class='more-body'>"
            "<div class='guide-grid'>"
            "<div class='guide-card'>"
            "<div class='guide-title'>Touch</div>"
            "<div class='guide-row'><span class='guide-key'>Top-left logo</span><span class='guide-val'>Next accent color</span></div>"
            "<div class='guide-row'><span class='guide-key'>Top-right corner</span><span class='guide-val'>Backlight on/off</span></div>"
            "<div class='guide-row'><span class='guide-key'>Center</span><span class='guide-val'>Next screen</span></div>"
            "</div>"
            "<div class='guide-card'>"
            "<div class='guide-title'>BOOT button</div>"
            "<div class='guide-row'><span class='guide-key'>1 tap</span><span class='guide-val'>Next screen</span></div>"
            "<div class='guide-row'><span class='guide-key'>2 taps</span><span class='guide-val'>Rotate 90&deg;</span></div>"
            "<div class='guide-row'><span class='guide-key'>3 taps</span><span class='guide-val'>Backlight on/off</span></div>"
            "<div class='guide-row'><span class='guide-key'>Hold 5 s</span><span class='guide-val'>Factory reset and reboot</span></div>"
            "</div></div>"
            "</div></details>"
            "</div></div>"
            "</section>"); // closes pane-system

  page += F("<div class='actions' id='form-actions'>"
            "<button type='submit' class='btn-save' id='btn-save'>Save settings</button>"
            "<button type='button' class='btn-restart' id='btn-reboot' onclick='rebootDevice()' title='Reboot without saving'>Reboot</button>"
            "</div>"
            "</section>"); // closes pane-settings


  page += F("</form>"
            "</main>"
            "</div>"  // closes console-layout
            "</div>"  // closes container
            "<div id='overlay'>"
            "<div class='spinner'></div>"
            "<h3>Restarting</h3>"
            "<p>Reconnecting in <span id='cd'>8</span> s</p>"
            "</div>"
            "<div id='thm-toast' class='toast' role='status' aria-live='polite'></div>"
            "<script src='/cyd-twin.js?v=" WEB_UI_V "'></script>"
            "<script src='/dash.js?v=" WEB_UI_V "'></script>");
  page += F("<script>const initStats=");
  if (page.length() > 3500) flush();
  page += buildStatsJson(d);
  if (page.length() > 3500) flush();
  page += F(";updateStats(initStats);setInterval(pollStats,3000);</script></body></html>");

  flush();
  webCfgServer.sendContent(""); // ends the chunked response
}

static void handleSave()
{
  if (refuseForeign())
    return;
  const TSettings before = currentSettings();
  TSettings next = before;

  // New page password: checked first, so a typo saves nothing at all.
  const String webPass = webCfgServer.arg("webpass");
  if (!webPass.isEmpty() || !webCfgServer.arg("webpass2").isEmpty())
  {
    const char *problem = webPass != webCfgServer.arg("webpass2") ? "The two new passwords are not the same."
                          : webPass.length() > WEBCFG_PASS_MAX   ? "The new password is longer than 63 characters."
                                                                  : nullptr;
    if (problem)
    {
      webCfgServer.send(400, "text/html",
                        String("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                               "<meta name='viewport' content='width=device-width, initial-scale=1'></head>"
                               "<body style='font-family:sans-serif'><p>") + problem +
                            " Nothing was saved.</p><p><a href='/'>Back to settings</a></p></body></html>");
      return;
    }
    next.WebPassword = webPass;
  }

  if (webCfgServer.hasArg("pool"))
  {
    next.PoolAddress = webCfgServer.arg("pool");
    next.PoolAddress.trim(); // a pasted address often ends with a space or a newline
  }
  if (webCfgServer.hasArg("port"))
    next.PoolPort = constrain(webCfgServer.arg("port").toInt(), 0, 65535); // like fbport (upstream #8)
  if (webCfgServer.hasArg("fbpool"))
  {
    next.FallbackPoolAddress = webCfgServer.arg("fbpool");
    next.FallbackPoolAddress.trim();
  }
  if (webCfgServer.hasArg("fbport"))
    next.FallbackPoolPort = constrain(webCfgServer.arg("fbport").toInt(), 0, 65535);
  if (webCfgServer.hasArg("fbpass"))
    strncpy(next.FallbackPoolPassword, webCfgServer.arg("fbpass").c_str(), sizeof(next.FallbackPoolPassword) - 1);
  if (webCfgServer.hasArg("poolpass"))
    strlcpy(next.PoolPassword, webCfgServer.arg("poolpass").c_str(), sizeof(next.PoolPassword));
  if (webCfgServer.hasArg("wallet"))
  {
    String wallet = webCfgServer.arg("wallet");
    wallet.trim();
    strlcpy(next.BtcWallet, wallet.c_str(), sizeof(next.BtcWallet));
  }
  if (webCfgServer.hasArg("tz"))
    next.Timezone = constrain(webCfgServer.arg("tz").toInt(), -12, 14);
  if (webCfgServer.hasArg("currency"))
    next.Currency = currencyFor(webCfgServer.arg("currency")).code;
  if (webCfgServer.hasArg("mempool"))
  {
    String mh = webCfgServer.arg("mempool");
    mh.trim();
    next.MempoolHost = mh.length() ? mh : String(DEFAULT_MEMPOOLHOST);
  }
  next.saveStats = webCfgServer.hasArg("savestats");

  if (webCfgServer.hasArg("theme"))
    next.displayTheme = webCfgServer.arg("theme").toInt();

#if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
  next.invertColors = webCfgServer.hasArg("invert");
  if (webCfgServer.hasArg("rot"))
  {
    next.screenRotation = webCfgServer.arg("rot").toInt();
    next.flipDisplay = (next.screenRotation == 3);
  }
  else if (webCfgServer.hasArg("flip"))
  {
    next.flipDisplay = true;
    next.screenRotation = 3;
  }
  else
  {
    next.flipDisplay = false;
    next.screenRotation = 1;
  }
  if (webCfgServer.hasArg("brightness"))
    next.Brightness = constrain(webCfgServer.arg("brightness").toInt(), 0, 255);
#endif
#if defined(ES3C35P) || defined(ESP32_4IN_ST7796)
  next.invertColors = webCfgServer.hasArg("invert");
  next.flipDisplay = webCfgServer.hasArg("flip");
  if (webCfgServer.hasArg("brightness"))
    next.Brightness = constrain(webCfgServer.arg("brightness").toInt(), 0, 255);
#endif
#ifdef TDISPLAY_PULSE
  next.flipDisplay = webCfgServer.hasArg("flip");
  if (webCfgServer.hasArg("brightness"))
    next.Brightness = constrain(webCfgServer.arg("brightness").toInt(), 0, 255);
#endif
#if defined(M5STICK_C_PLUS2) && PLUS2_BACKLIGHT_PWM
  if (webCfgServer.hasArg("brightness"))
    next.Brightness = constrain(webCfgServer.arg("brightness").toInt(), 0, 255);
#endif
#ifdef HAS_RGB_LED
  if (webCfgServer.hasArg("ledmode"))
    next.ledMode = constrain(webCfgServer.arg("ledmode").toInt(), LED_MODE_OFF, LED_MODE_STEADY);
  const String ledColor = webCfgServer.arg("ledcolor"); // "#rrggbb"
  if (ledColor.length() == 7 && ledColor[0] == '#')
    next.ledColor = strtoul(ledColor.c_str() + 1, NULL, 16) & 0xFFFFFF;
  if (webCfgServer.hasArg("ledbright"))
    next.ledBrightness = constrain(webCfgServer.arg("ledbright").toInt(), 1, 255);
  next.ledScreenOff = webCfgServer.hasArg("ledscreen");
#endif
#ifdef HAS_BACKLIGHT_CONTROL
  if (webCfgServer.hasArg("screenoff"))
    next.ScreenTimeout = max(0, (int)webCfgServer.arg("screenoff").toInt());
  next.nightDim = webCfgServer.hasArg("nightdim");
  if (webCfgServer.hasArg("nightbright"))
    next.NightBrightness = constrain((int)webCfgServer.arg("nightbright").toInt(), 1, 255);
  if (webCfgServer.hasArg("nightfade"))
    next.NightFade = max(0, (int)webCfgServer.arg("nightfade").toInt());
  if (webCfgServer.hasArg("lat"))
    next.Latitude = webCfgServer.arg("lat").toFloat();
  if (webCfgServer.hasArg("lon"))
    next.Longitude = webCfgServer.arg("lon").toFloat();
#endif

  saveSettingsToFlash(&next);

  const bool restart = next.screenRotation != before.screenRotation || next.flipDisplay != before.flipDisplay;
  if (!restart)
  {
    const bool poolChanged = next.PoolAddress != before.PoolAddress || next.PoolPort != before.PoolPort ||
                             strcmp(next.BtcWallet, before.BtcWallet) != 0 ||
                             strcmp(next.PoolPassword, before.PoolPassword) != 0;
    const bool backupChanged = next.FallbackPoolAddress != before.FallbackPoolAddress ||
                               next.FallbackPoolPort != before.FallbackPoolPort ||
                               strcmp(next.FallbackPoolPassword, before.FallbackPoolPassword) != 0;
    if (poolChanged || backupChanged)
      requestPoolChange(next);

    if (!next.Currency.equalsIgnoreCase(before.Currency))
      requestCurrencyChange(currencyFor(next.Currency).code);

    Settings.Timezone = next.Timezone;
    Settings.MempoolHost = next.MempoolHost;
    Settings.saveStats = next.saveStats;
    Settings.WebPassword = next.WebPassword;
    Settings.displayTheme = next.displayTheme;
    setDisplayTheme(Settings.displayTheme);
    Settings.invertColors = next.invertColors;
    setDisplayInvert(Settings.invertColors);
    Settings.Brightness = next.Brightness;
    ledcWrite(0, Settings.Brightness);
#ifdef HAS_RGB_LED
    Settings.ledMode = next.ledMode;
    Settings.ledColor = next.ledColor;
    Settings.ledBrightness = next.ledBrightness;
    Settings.ledScreenOff = next.ledScreenOff;
    rgbLedTick();
#endif
#ifdef HAS_BACKLIGHT_CONTROL
    Settings.ScreenTimeout = next.ScreenTimeout;
    Settings.nightDim = next.nightDim;
    Settings.NightBrightness = next.NightBrightness;
    Settings.NightFade = next.NightFade;
    Settings.Latitude = next.Latitude;
    Settings.Longitude = next.Longitude;
    if (next.ScreenTimeout != before.ScreenTimeout)
      wakeScreen();
#endif

    String msg = poolChanged ? F("Saved. Reconnecting to the pool…")
                             : F("Saved and applied.");
    webCfgServer.send(200, "text/html",
                      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                      "<meta http-equiv='refresh' content='2;url=/'>"
                      "<style>body{background:#0b0d12;color:#e6ebf2;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}"
                      ".card{background:#11151c;padding:24px;border-radius:6px;border:1px solid #1e2530;max-width:360px}</style></head>"
                      "<body><div class='card'><h3 style='color:#44a282;margin-top:0'>Saved</h3><p>" + msg + "</p>"
                      "<p><a href='/' style='color:#e6ebf2'>Back to dashboard</a></p></div></body></html>");
    return;
  }

  webCfgServer.send(200, "text/html",
                     "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                     "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                     "<title>Restarting...</title>"
                     "<style>body{background:#0b0d12;color:#e6ebf2;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}"
                     ".sp{width:40px;height:40px;border:3px solid #1e2530;border-top-color:#f7931a;border-radius:50%;animation:s 1s linear infinite;margin:0 auto 16px}"
                     "@keyframes s{to{transform:rotate(360deg)}}</style></head>"
                     "<body><div><div class='sp'></div><h2>Saved</h2><p style='color:#8a8c92'>Orientation changed. Restarting…</p></div>"
                     "<script>function p(){fetch('/',{cache:'no-store'}).then(()=>location.replace('/')).catch(()=>setTimeout(p,1200))};setTimeout(p,5000);</script>"
                     "</body></html>");

  saveStat(); // stats mined since the last periodic save
  delay(1000);
  ESP.restart();
}

static void handleStatus()
{
  const MinerStatus m = getMinerStatus();
  DynamicJsonDocument doc(2048);
  doc["version"] = CURRENT_VERSION;
  doc["fw"] = FW_GIT;
  doc["uptime_s"] = (uint32_t)(esp_timer_get_time() / 1000000ULL); // 64-bit clock: millis() wraps after 49.7 days
  doc["state"] = mMonitor.NerdStatus == NM_hashing ? "mining" : mMonitor.NerdStatus == NM_Connecting ? "connecting" : "starting";
  doc["hashrate_khs"] = round(m.hashrateKHs * 10.0) / 10.0;
  doc["hashrate_avg_khs"] = round(m.avgHashrateKHs * 10.0) / 10.0;
  doc["hw_loop"] = m.hwLoop ? m.hwLoop : "default";
  doc["total_mhashes"] = (uint32_t)(getTotalHashes() / 1000000ULL); // one 64-bit read, never mid-fold
  doc["mining_time_s"] = (uint32_t)upTime;
  doc["templates"] = templates;
  doc["best_diff"] = getBestDiff();
  doc["shares_32bit"] = shares;                    // accepted by the pool
  doc["shares_32bit_rejected"] = shares32Rejected; // refused by the pool
  doc["blocks_found"] = valids;
  doc["shares_accepted"] = m.accepted;
  doc["shares_rejected"] = m.rejected;
  {
    // Attempt accounting (new keys; existing keys above unchanged). Session =
    // since boot. miss = attempts that did not reach the pool share target.
    const AttemptCounters ac = getAttemptCounters();
    JsonObject at = doc.createNestedObject("attempts");
    at["total"] = ac.attemptsTotal;        // lifetime hashes
    at["session"] = ac.attemptsSession;    // hashes since boot
    at["miss_session"] = ac.missSession;   // session - shares_found
    at["candidates"] = ac.candidates;      // 16-bit prefilter hits
    at["shares_found"] = ac.found;         // >= pool share target
    at["shares_submitted"] = ac.submitted; // sent to the pool
    at["shares_accepted"] = ac.accepted;   // pool accepted
    at["shares_rejected"] = ac.rejected;   // pool refused (the only "rejected")
    at["blocks"] = ac.blocks;              // >= network target
    at["tap_samples"] = ac.taps;           // uniform sampler taps taken
    at["tap_every"] = HASH_TAP_EVERY;      // 1 tap per N SW-miner nonces
  }
  JsonObject pool = doc.createNestedObject("pool");
  {
    // The stratum task swaps a new pool into Settings under this mutex
    // (takePoolChange); this runs on the other core, so copy it under it too.
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    pool["address"] = activePoolAddress();
    pool["port"] = activePoolPort();
  }
  pool["backup"] = onBackupPool();
  pool["subscribed"] = m.subscribed;
  if (m.authorized >= 0)
    pool["authorized"] = m.authorized == 1; // missing: no answer yet
  pool["difficulty"] = m.poolDifficulty;
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = WiFi.SSID();
  wifi["rssi"] = WiFi.RSSI();
  wifi["ip"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();

  // Cached values only: nothing here may start an HTTPS fetch or an NTP
  // request (those run on the monitor task and pause the miner).
  JsonObject clock = doc.createNestedObject("clock");
  int hh, mi, ss, dd, mo, yy;
  if (getLocalClock(hh, mi, ss, dd, mo, yy)) {
    char buf[16];
    clock["synced"] = true;
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mi, ss);
    clock["time"] = buf;
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d", dd, mo, yy);
    clock["date"] = buf;
  } else {
    clock["synced"] = false;
  }
  JsonObject btc = doc.createNestedObject("btc");
  btc["price"] = getBTCpriceValue();                         // 0 = not fetched yet
  btc["prefix"] = currencyFor(currencySetting()).prefix;
  btc["block_height"] = getBlockHeightValue();               // 0 = not fetched yet
#if defined(M5STICK_C_PLUS2) && defined(PIN_BATTERY_ADC)
  const int batMv = plus2BatteryCachedMillivolts();
  if (batMv > 0) {
    JsonObject bat = doc.createNestedObject("battery");
    bat["mv"] = batMv;
    bat["percent"] = plus2BatteryPercent(batMv);
  }
#endif

  String out;
  serializeJson(doc, out);
  webCfgServer.sendHeader("Cache-Control", "no-store");
  webCfgServer.send(200, "application/json", out);
}

// --------------------------------------------------------------------------
// Live monitor (webMonitorPage.h) and its hashrate history: one sample every
// 30 s, the last hour, kept here in RAM (240 bytes).
// --------------------------------------------------------------------------
#define HISTORY_LEN 120
#define HISTORY_PERIOD_ms 30000
static uint16_t s_history[HISTORY_LEN]; // KH/s x 10
static uint8_t s_historyCount = 0, s_historyHead = 0;
static uint32_t s_historyLast = 0;

static void historyTick()
{
  if (millis() - s_historyLast < HISTORY_PERIOD_ms)
    return;
  s_historyLast = millis();
  uint32_t v = (uint32_t)(getMinerStatus().hashrateKHs * 10.0f + 0.5f);
  if (v > 65535) v = 65535;
  s_history[s_historyHead] = (uint16_t)v;
  s_historyHead = (s_historyHead + 1) % HISTORY_LEN;
  if (s_historyCount < HISTORY_LEN) s_historyCount++;
}

static void handleHistory()
{
  String out;
  out.reserve(16 + s_historyCount * 7);
  out = F("{\"interval_s\":");
  out += HISTORY_PERIOD_ms / 1000;
  out += F(",\"khs\":[");
  for (uint8_t i = 0; i < s_historyCount; i++) {
    const uint16_t v = s_history[(s_historyHead + HISTORY_LEN - s_historyCount + i) % HISTORY_LEN];
    if (i) out += ',';
    out += v / 10;
    out += '.';
    out += v % 10;
  }
  out += F("]}");
  webCfgServer.sendHeader("Cache-Control", "no-store");
  webCfgServer.send(200, "application/json", out);
}

static void handleMonitor()
{
  webCfgServer.sendHeader("Cache-Control", "no-cache");
  webCfgServer.send_P(200, "text/html; charset=utf-8", MONITOR_HTML);
}

void setup_webConfig(void)
{
  if (webCfgStarted)
    return;

  webCfgServer.on("/favicon.ico", HTTP_GET, []() {
    webCfgServer.send(204, "image/x-icon", "");
  });
  webCfgServer.on("/cyd-twin.css", HTTP_GET, []() {
    webCfgServer.sendHeader("Cache-Control", "public, max-age=86400");
    webCfgServer.send_P(200, "text/css", CYD_TWIN_STYLE);
  });
  webCfgServer.on("/cyd-twin.js", HTTP_GET, []() {
    webCfgServer.sendHeader("Cache-Control", "public, max-age=86400");
    webCfgServer.send_P(200, "application/javascript", CYD_TWIN_JS);
  });
  webCfgServer.on("/dash.css", HTTP_GET, []() {
    webCfgServer.sendHeader("Cache-Control", "public, max-age=86400");
    webCfgServer.send_P(200, "text/css", DASH_STYLE);
  });
  webCfgServer.on("/dash.js", HTTP_GET, []() {
    webCfgServer.sendHeader("Cache-Control", "public, max-age=86400");
    webCfgServer.send_P(200, "application/javascript", DASH_JS);
  });
  static const char *kHeaders[] = {"Origin", "Referer"}; // read by sameOrigin()
  webCfgServer.collectHeaders(kHeaders, 2);
  webCfgServer.on("/", HTTP_GET, handleRoot);
  webCfgServer.on("/monitor", HTTP_GET, handleMonitor); // upstream live monitor page
  webCfgServer.on("/api/history", HTTP_GET, handleHistory);
  webCfgServer.on("/save", HTTP_POST, handleSave);
  webCfgServer.on("/api/stats", HTTP_GET, handleApiStats);
  webCfgServer.on("/api/profile", HTTP_GET, handleApiProfile);
  webCfgServer.on("/api/crypto-audit", HTTP_GET, handleApiCryptoAudit);
  webCfgServer.on("/api/status", HTTP_GET, handleStatus);
  webCfgServer.on("/api/reset-session", HTTP_POST, handleResetSession);
  webCfgServer.on("/api/theme", HTTP_GET, handleGetTheme);
  webCfgServer.on("/api/theme", HTTP_POST, handleSetTheme);
  webCfgServer.on("/api/invert", HTTP_POST, handleSetInvert);
  webCfgServer.on("/api/recalibrate-colors", HTTP_POST, handleRecalibrateColors);
  webCfgServer.on("/api/recalibrate-touch", HTTP_POST, handleRecalibrateTouch);
#if (defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)) && !defined(CYD_THEME_CLASSIC)
  webCfgServer.on("/api/onboarding", HTTP_GET, handleOnboarding);
  webCfgServer.on("/api/onboarding/skip", HTTP_POST, handleOnboardingSkip);
#ifdef CYD_TOUCH_SHARED_SPI
  webCfgServer.on("/api/touchdiag", HTTP_GET, handleTouchDiag);
#endif
#endif
  webCfgServer.on("/api/screen", HTTP_GET, handleGetScreen);
  webCfgServer.on("/api/screen", HTTP_POST, handleSetScreen);
  webCfgServer.on("/api/screenshot", HTTP_GET, handleScreenshot);
#if (defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)) && !defined(CYD_THEME_CLASSIC)
  webCfgServer.on("/api/minechart", HTTP_ANY, handleMineChart);
#endif
  webCfgServer.on("/restart", HTTP_POST, handleRestart);
  webCfgServer.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
#ifdef TDISPLAY_UI_SCREENSHOT
  // Dev builds: the screens rendered at boot (tDisplayPulseDriver.cpp),
  // run-length encoded; tools/tdisplay_shots.py turns them into PNGs.
  webCfgServer.on("/shot", HTTP_GET, []()
  {
    extern bool tDisplayShot(int i, const char **name, const uint8_t **data, size_t *len);
    const char *name;
    const uint8_t *data;
    size_t len;
    if (!tDisplayShot(webCfgServer.arg("i").toInt(), &name, &data, &len))
    {
      webCfgServer.send(404, "text/plain", "no such shot");
      return;
    }
    webCfgServer.sendHeader("X-Shot-Name", name);
    webCfgServer.send_P(200, "application/octet-stream", (PGM_P)data, len);
  });
#endif
  webCfgServer.onNotFound([]()
  {
    webCfgServer.sendHeader("Location", "/");
    webCfgServer.send(302, "text/plain", "");
  });
  webCfgServer.begin();
  webCfgStarted = true;

  Serial.print("[WEBCFG] Dashboard at http://");
  Serial.print(WiFi.localIP());
  Serial.println("/  (upstream monitor: /monitor)");
}

volatile bool g_serialMuted = false;

String getStatsJson(void)
{
  mining_data d = getMiningData(0);
  return buildStatsJson(d);
}

void webConfigProcess(void)
{
  historyTick(); // also before the server is up, so the graph has the full hour
  if (webCfgStarted)
    webCfgServer.handleClient();

  static char s_cmdBuf[32];
  static uint8_t s_cmdLen = 0;
  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n')
    {
      s_cmdBuf[s_cmdLen] = '\0';
      if (s_cmdLen > 0)
      {
        if (strncmp(s_cmdBuf, "SHOT", 4) == 0)
        {
          int screenIdx = 0;
          int rot = -1;
          sscanf(s_cmdBuf + 4, "%d %d", &screenIdx, &rot);
#if (defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)) && !defined(CYD_THEME_CLASSIC)
          extern void cydSetShotRotation(int r);
          cydSetShotRotation(rot);
#endif
          g_serialMuted = true;
          esp_log_level_set("*", ESP_LOG_NONE);
          delay(40);
          while (Serial.available()) Serial.read();
          Serial.println(F(">>>BMP_BEGIN 77878<<<"));
          captureDisplayBmp(Serial, screenIdx);
          Serial.println(F("\n>>>BMP_END<<<"));
          esp_log_level_set("*", ESP_LOG_WARN);
          g_serialMuted = false;
        }
        else if (strcmp(s_cmdBuf, "STATS") == 0)
        {
          Serial.println(F(">>>STATS_BEGIN<<<"));
          Serial.println(getStatsJson());
          Serial.println(F(">>>STATS_END<<<"));
        }
      }
      s_cmdLen = 0;
    }
    else if (s_cmdLen < sizeof(s_cmdBuf) - 1)
    {
      s_cmdBuf[s_cmdLen++] = c;
    }
  }
}

#include "version.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
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
extern uint32_t templates, hashes, Mhashes;
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
  return out;
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
    json += ",\"round_expected\":" + String((double)ms2.avgHashrateKHs * 1000.0 * 600.0, 0); // tries in an average 10-min round
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
  int scr = 0;
  if (webCfgServer.hasArg("id"))
    scr = webCfgServer.arg("id").toInt();
  else if (webCfgServer.hasArg("screen"))
    scr = webCfgServer.arg("screen").toInt();

  if (scr < 0 || scr > 2) // Mine, Odds, World
    scr = 0;

  setDisplayScreen(scr);
  String json = "{\"status\":\"ok\",\"screen\":" + String(scr) + "}";
  webCfgServer.send(200, "application/json", json);
}

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
:root{
--bg:#0b0d12;--card:#11151c;--card-border:#1e2530;--input-bg:#0b0e13;
--text:#e6ebf2;--muted:#8a96a6;--dim:#58636f;--amber:#f7931a;--green:#3fb950;--warn:#d29922;--red:#f85149;
--mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;--sans:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;--r:6px;--ctl:38px;
}
*{box-sizing:border-box;margin:0;padding:0}
html{-webkit-text-size-adjust:100%}
body{background:var(--bg);color:var(--text);font-family:var(--sans);font-size:14px;padding:max(12px,env(safe-area-inset-top)) max(14px,env(safe-area-inset-right)) 40px max(14px,env(safe-area-inset-left));line-height:1.5;-webkit-font-smoothing:antialiased}
a{color:var(--text)}
code{font-family:var(--mono);font-size:.88em;background:var(--input-bg);border:1px solid var(--card-border);padding:0 4px;border-radius:3px;word-break:break-all}
.muted{color:var(--muted)}
.container{max-width:1160px;margin:0 auto}

/* Header: identity on the left, mining state on the right */
.coin-pill{font-family:var(--mono);font-size:12.5px;font-weight:600;color:var(--amber);border:1px solid var(--card-border);border-radius:999px;padding:3px 10px;white-space:nowrap}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:2px 0 12px;margin-bottom:12px}
.brand{display:flex;align-items:center;gap:10px;min-width:0}
.brand>div{min-width:0}
.logo-box{width:28px;height:28px;border-radius:var(--r);background:var(--amber);display:flex;align-items:center;justify-content:center;color:#000;font-weight:800;font-size:16px;flex-shrink:0}
.brand-title{font-size:15px;font-weight:700}
.brand-subtitle{font-size:12px;color:var(--muted);font-family:var(--mono);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.status-badge{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--card-border);border-radius:var(--r);padding:3px 10px;font-size:13px;font-weight:600;flex-shrink:0}
.status-badge::before{content:'';width:8px;height:8px;border-radius:50%;background:currentColor;color:var(--muted)}
.status-badge:has(.txt-green)::before{color:var(--green)}
.status-badge:has(.txt-amber)::before{color:var(--warn)}
.txt-green{color:var(--green)}
.txt-amber{color:var(--warn)}
.txt-gray{color:var(--muted)}

/* Status strip: hashrate is the biggest number on the page */
.diag-strip{display:grid;grid-template-columns:repeat(2,1fr);background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);margin-bottom:16px}
.diag-item{padding:9px 14px;display:flex;flex-direction:column;justify-content:center;min-width:0}
.diag-item.primary{grid-column:1/-1;border-bottom:1px solid var(--card-border)}
@media(min-width:720px){.diag-strip{grid-template-columns:1.6fr repeat(4,1fr)}.diag-item.primary{grid-column:auto;border-bottom:0;border-right:1px solid var(--card-border)}}
.diag-label{display:block;font-size:12px;color:var(--muted)}
.diag-val{font-size:15px;font-weight:600;color:var(--text);font-family:var(--mono);font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.diag-item.primary .diag-val{font-size:26px;line-height:1.2;color:var(--amber)}
.diag-sub{font-size:12px;font-weight:400;color:var(--muted)}
.share-log{display:flex;flex-direction:column;border-top:1px solid var(--card-border)}
.sl-row{display:grid;grid-template-columns:5.5em 1fr 6.5em 5em;gap:10px;align-items:center;padding:5px 0;border-bottom:1px solid var(--card-border);font-size:12.5px;font-family:var(--mono);font-variant-numeric:tabular-nums;color:var(--text)}
.sl-row.hdr{font-family:inherit;color:var(--muted);font-size:12px}
.sl-row.rej{color:var(--red,#f85149)}
.sl-row.best .sl-d{color:var(--amber)}
.sl-bar{height:6px;background:var(--card-border);border-radius:3px;overflow:hidden}
.sl-bar>i{display:block;height:100%;background:var(--amber)}
.sl-r{text-align:right;color:var(--muted)}
.btn-reset-stats{background:transparent;border:1px solid var(--card-border);border-radius:var(--r);color:var(--muted);padding:2px 9px;font-size:12px;font-family:inherit;cursor:pointer}
.btn-reset-stats:hover{color:var(--text);border-color:var(--dim)}
.btn-reset-stats:disabled{opacity:.6;cursor:wait}

.console-layout{display:grid;grid-template-columns:310px 1fr;gap:20px;align-items:start}
@media(max-width:960px){.console-layout{grid-template-columns:1fr}.console-sidebar{order:2}}
.console-sidebar{display:flex;flex-direction:column;gap:12px;min-width:0}
@media(min-width:961px){.console-sidebar{position:sticky;top:14px}}
.console-main{display:flex;flex-direction:column;min-width:0}

/* Tabs: one scrolling row, never wraps */
.console-tabs{display:flex;flex-wrap:nowrap;gap:2px;border-bottom:1px solid var(--card-border);margin-bottom:14px;overflow-x:auto;scrollbar-width:none;-webkit-overflow-scrolling:touch}
.console-tabs::-webkit-scrollbar{display:none}
.tab-btn{flex:1 1 0;min-width:0;text-align:center;padding:8px 12px;background:none;border:0;border-bottom:2px solid transparent;margin-bottom:-1px;color:var(--muted);font-size:13px;font-weight:600;font-family:inherit;cursor:pointer;white-space:nowrap}
.tab-btn:hover{color:var(--text)}
.tab-btn.active{color:var(--text);border-bottom-color:var(--amber)}
.tab-pane{display:none}
.tab-pane.active{display:flex;flex-direction:column;gap:12px}
.settings-sec{display:flex;flex-direction:column;gap:12px}
.settings-sec+.settings-sec{margin-top:14px}
.settings-h{font-size:16px;font-weight:600;color:var(--text);margin:4px 0 0}

/* Overview */
.overview-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,250px),1fr));gap:12px}
.tele-card{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);padding:14px;display:flex;flex-direction:column;gap:6px;min-width:0}
.tele-card-hdr{display:flex;align-items:center;justify-content:space-between;gap:8px}
.tele-card-title{font-size:13px;font-weight:600;color:var(--muted)}
.tele-hero-val{font-size:22px;font-weight:700;color:var(--text);font-family:var(--mono);font-variant-numeric:tabular-nums;line-height:1.25;overflow-wrap:anywhere}
.tele-hero-val.amber{color:var(--amber)}
.tele-hero-sub{font-size:12px;color:var(--muted);margin-bottom:4px}
.unit{font-size:13px;color:var(--muted);font-weight:500;font-family:var(--sans)}
.tele-row{display:flex;justify-content:space-between;align-items:baseline;gap:10px;padding:5px 0;border-top:1px solid var(--card-border);font-size:13px;color:var(--muted)}
.tele-row>span:first-child{white-space:nowrap}
.hl-val{color:var(--text);font-family:var(--mono);font-size:12.5px;font-variant-numeric:tabular-nums;text-align:right}

.pool-banner{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);padding:12px 14px;display:flex;flex-direction:column;gap:8px}
.pool-info{font-size:13px;min-width:0;overflow-wrap:anywhere}
.pool-btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;border:1px solid var(--card-border);color:var(--text);text-decoration:none;font-weight:600;font-size:13px;padding:7px 12px;border-radius:var(--r);min-height:var(--ctl)}
.pool-btn:hover{border-color:var(--dim)}

/* Disclosure: long explanations and advanced settings start closed */
.section-hdr{font-size:13px;font-weight:600;color:var(--muted);margin:6px 0 0}
details.edu-acc{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);overflow:hidden}
summary.edu-sum{padding:10px 14px;font-size:13px;font-weight:600;color:var(--text);cursor:pointer;list-style:none;display:flex;align-items:center;justify-content:space-between;gap:10px;user-select:none}
summary.edu-sum::-webkit-details-marker{display:none}
summary.edu-sum:hover{background:rgba(255,255,255,0.02)}
summary.edu-sum::after,details.more>summary::after{content:'+';font-size:15px;color:var(--muted);flex-shrink:0}
details.edu-acc[open] summary.edu-sum::after,details.more[open]>summary::after{content:'\2212'}
.edu-body{padding:10px 14px 14px;border-top:1px solid var(--card-border);font-size:13px;line-height:1.6;color:#c3cbd5}
.edu-body a{color:var(--text)}
details.more{border-top:1px solid var(--card-border);padding-top:10px}
details.more>summary{cursor:pointer;list-style:none;display:flex;align-items:center;justify-content:space-between;gap:10px;font-size:13px;font-weight:600;color:var(--text);user-select:none}
details.more>summary::-webkit-details-marker{display:none}
.more-body{padding-top:12px;display:flex;flex-direction:column;gap:12px}

/* Controls: label -> control -> hint, one height everywhere */
input[type=text],input[type=number],input[type=password],input[type=color],select,.form-control{width:100%;background:var(--input-bg);border:1px solid var(--card-border);border-radius:var(--r);color:var(--text);padding:8px 11px;font-size:14px;font-family:var(--mono);outline:none;transition:border-color .15s;min-height:var(--ctl);height:var(--ctl);box-sizing:border-box}
input[type=color]{padding:4px;cursor:pointer}
input[type=text]:focus,input[type=number]:focus,input[type=password]:focus,select:focus,.form-control:focus{border-color:var(--amber)}
input::placeholder{color:var(--dim)}
select{appearance:none;-webkit-appearance:none;cursor:pointer;padding-right:32px;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%238592a3' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center}
select.ctl-auto{width:auto;display:inline-block}
.file-input{flex:1;min-width:0;width:100%;font-size:13px;color:var(--text);background:var(--input-bg);padding:8px;border:1px solid var(--card-border);border-radius:var(--r);min-height:var(--ctl)}
.cfg-card{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r)}
.cfg-card-hdr{display:flex;align-items:center;justify-content:space-between;padding:12px 14px 0;gap:10px;flex-wrap:wrap}
.cfg-card-title{font-size:14px;font-weight:600;color:var(--text)}
.cfg-card-body{padding:12px 14px 14px;display:flex;flex-direction:column;gap:12px}
.form-grid-3{display:grid;grid-template-columns:2fr 1fr 1fr;gap:10px}
.form-grid-2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.form-grid-4{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}
@media(max-width:720px){.form-grid-4{grid-template-columns:1fr 1fr}}
@media(max-width:540px){.form-grid-3,.form-grid-2{grid-template-columns:1fr}}
@media(max-width:400px){.form-grid-4{grid-template-columns:1fr}}
.form-group{display:flex;flex-direction:column;gap:4px;min-width:0}
.form-label{display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:13px;font-weight:600;color:var(--text)}
.form-label-sub{font-size:12px;font-weight:400;color:var(--muted)}
.form-sublabel{display:block;font-size:12px;color:var(--muted);margin-bottom:3px}
.form-hint{font-size:12px;color:var(--muted);line-height:1.5}
.row-between{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px}
.form-check{display:flex;align-items:flex-start;gap:9px;font-size:13px;color:var(--text);cursor:pointer;user-select:none;padding:2px 0;line-height:1.45}
.form-check input[type=checkbox]{accent-color:var(--amber);width:16px;height:16px;margin-top:2px;flex-shrink:0;cursor:pointer}
.form-check input[type=checkbox]:disabled{cursor:wait}
.slider-track-wrap{display:flex;align-items:center;gap:14px;min-height:var(--ctl)}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:4px;background:var(--card-border);border-radius:2px;outline:none;accent-color:var(--amber)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:16px;height:16px;border-radius:50%;background:var(--amber);cursor:pointer}
input[type=range]::-moz-range-thumb{width:16px;height:16px;border-radius:50%;background:var(--amber);cursor:pointer;border:none}
.preset-bar{display:flex;flex-direction:column;gap:8px}
.preset-pills{display:flex;flex-wrap:wrap;gap:6px;align-items:center}
.preset-pill{background:transparent;border:1px solid var(--card-border);color:var(--text);font-size:12px;font-family:inherit;padding:5px 10px;border-radius:var(--r);cursor:pointer}
.preset-pill:hover{border-color:var(--dim)}
.preset-pill.active{border-color:var(--amber)}
.preset-pill.sm{font-size:12px;padding:3px 8px}
.preset-comment{font-size:12px;line-height:1.45;color:var(--muted)}
.active-pool-pill{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:var(--muted)}
.active-pool-pill::before{content:'';width:6px;height:6px;border-radius:50%;background:var(--green)}
.active-pool-pill.fallback::before{background:var(--warn)}
.theme-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
@media(min-width:520px){.theme-grid{grid-template-columns:repeat(3,1fr)}}
.theme-card{background:var(--input-bg);border:1px solid var(--card-border);border-radius:var(--r);padding:9px 11px;cursor:pointer;user-select:none;position:relative}
.theme-card:hover{border-color:var(--dim)}
.theme-card.active{border-color:var(--amber)}
.theme-card.busy{opacity:.6;cursor:wait}
.theme-card.active::after{content:'\2713';position:absolute;top:8px;right:10px;font-size:12px;color:var(--text)}
.theme-swatch{display:flex;align-items:center;gap:4px;margin-bottom:5px}
.swatch-dot{width:12px;height:12px;border-radius:50%;display:inline-block;box-shadow:0 0 0 1px rgba(255,255,255,.15)}
.theme-card h4{font-size:13px;font-weight:600;color:var(--text)}
.theme-card p{font-size:12px;color:var(--muted);line-height:1.35}

/* One primary action: Save. Everything else is bordered and quiet. */
.actions{position:sticky;bottom:0;z-index:5;margin:16px 0 24px;padding:10px 0;display:flex;flex-direction:column;gap:8px;background:var(--bg)}
.btn-save{background:var(--amber);color:#000;border:none;border-radius:var(--r);padding:11px 16px;font-size:14px;font-weight:600;font-family:inherit;cursor:pointer;width:100%;min-height:42px}
.btn-save:hover{opacity:.92}
.btn-save:disabled{opacity:.6;cursor:wait}
.btn-restart{background:transparent;border:1px solid var(--card-border);border-radius:var(--r);color:var(--muted);padding:9px;font-size:13px;font-family:inherit;cursor:pointer;width:100%;min-height:var(--ctl)}
.btn-restart:hover{color:var(--red);border-color:rgba(248,81,73,.45)}
.btn-restart:disabled{opacity:.6;cursor:wait}
.ota-row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.btn-ota{background:transparent;border:1px solid var(--card-border);border-radius:var(--r);color:var(--text);padding:8px 14px;font-size:13px;font-weight:600;font-family:inherit;cursor:pointer;min-height:var(--ctl)}
.btn-ota:hover{border-color:var(--dim)}
.btn-ota:disabled{opacity:.6;cursor:wait}
.ota-progress-box{display:none}
.ota-progress-bar{height:4px;border-radius:2px;background:var(--input-bg);overflow:hidden}
.ota-progress-fill{height:100%;width:0%;background:var(--amber);transition:width .1s linear}
.ota-status{font-size:12px;color:var(--muted);margin-top:6px;font-variant-numeric:tabular-nums}
.guide-grid{display:grid;grid-template-columns:1fr;gap:16px}
@media(min-width:480px){.guide-grid{grid-template-columns:repeat(2,1fr)}}
.guide-card{font-size:13px;color:var(--muted);line-height:1.6;min-width:0}
.guide-title{font-size:13px;font-weight:600;color:var(--text);margin-bottom:2px}
.guide-row{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:5px 0;border-top:1px solid var(--card-border);font-size:13px}
.guide-key{color:var(--text)}
.guide-val{color:var(--muted);text-align:right}

/* Proof-of-work simulator */
.sim-intro{font-size:13px;color:var(--muted)}
.sim-box{background:var(--input-bg);border:1px solid var(--card-border);border-radius:var(--r);padding:12px;font-family:var(--mono);font-size:12px;display:flex;flex-direction:column;gap:8px;min-width:0}
.sim-fields{border:1px solid var(--card-border);border-radius:4px;padding:8px;font-size:11px;color:var(--muted);word-break:break-all}
.sim-k{color:var(--muted);font-size:11px;font-family:var(--sans)}
.sim-v{word-break:break-all;margin-top:2px}
.sim-nonce{font-size:14px;font-weight:700}
.sim-hash{font-weight:700;color:var(--muted)}
.sim-result{font-weight:700;color:var(--muted)}

/* Live-action feedback: one toast for theme, invert, screenshot, OTA, reset */
.toast{position:fixed;left:50%;bottom:max(16px,env(safe-area-inset-bottom));transform:translateX(-50%);background:var(--card);border:1px solid var(--card-border);border-left:3px solid var(--green);color:var(--text);padding:8px 14px;border-radius:var(--r);font-size:13px;z-index:1000;max-width:calc(100vw - 28px);display:none}
.toast.err{border-left-color:var(--red)}
.toast.show{display:block}

#overlay{display:none;position:fixed;inset:0;background:rgba(11,13,18,0.96);z-index:999;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:20px}
#overlay h3{font-size:16px;margin-bottom:6px}
#overlay p{color:var(--muted);font-size:13px}
.spinner{width:32px;height:32px;border:3px solid var(--card-border);border-top-color:var(--amber);border-radius:50%;animation:spin 1s linear infinite;margin-bottom:14px}
@keyframes spin{to{transform:rotate(360deg)}}
button:focus-visible,a:focus-visible,.theme-card:focus-visible,summary:focus-visible{outline:2px solid var(--amber);outline-offset:2px}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
)rawcss";

static const char DASH_JS[] PROGMEM = R"rawjs(
const TAB_ALIAS={stratum:'settings',display:'settings',system:'settings',pow:'learn'};
function switchTab(tId){
tId=TAB_ALIAS[tId]||tId;if(!document.getElementById('pane-'+tId)||!document.getElementById('tbtn-'+tId))tId='telemetry';
document.querySelectorAll('.tab-btn').forEach(b=>{const on=b.id==='tbtn-'+tId;b.classList.toggle('active',on);b.setAttribute('aria-selected',on?'true':'false');});
document.querySelectorAll('.tab-pane').forEach(p=>p.classList.toggle('active',p.id==='pane-'+tId));
try{localStorage.setItem('cyd_tab',tId);}catch(e){}
}
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
function updateStats(d){
const hr=parseFloat(d.hashrate)||0,hps=Math.round(hr*1000),hpd=hr*1000*86400;
const eCoin=document.getElementById('st-coin');if(eCoin&&d.pool){let h=d.pool.split(':')[0].replace(/^((stratum|pool|solo|www|eu|us)\.)+(?=.*\.)/,'');h=/^shears\./.test(h)?'Shears':h.split('.')[0];eCoin.textContent=(d.coin||'')+' \u00b7 '+h;eCoin.title=d.pool;}const eTopHr=document.getElementById('st-top-hr');if(eTopHr&&d.hashrate)eTopHr.textContent=d.hashrate;
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
const netHps=netDiff*4294967296/600,eNetpow=document.getElementById('dyn-netpow');const eNetHr=document.getElementById('dyn-nethr');if(eNetHr){const u=['H/s','KH/s','MH/s','GH/s','TH/s','PH/s','EH/s','ZH/s'];let v=netHps,i=0;while(v>=1000&&i<7){v/=1000;i++;}eNetHr.textContent=v.toFixed(v>=100?0:1)+' '+u[i];}if(eNetpow){const netSharePct=(hps/netHps)*100;eNetpow.textContent=netSharePct.toExponential(1)+'%';}
if(d.temp){const e=document.getElementById('st-temp');if(e)e.textContent=d.temp;}
if(d.uptime){const u=document.getElementById('st-uptime');if(u){const m=d.uptime.match(/^(\d+)\s+(\d{2}:\d{2}:\d{2})$/);u.textContent=m?(parseInt(m[1])>0?m[1]+'d '+m[2]:m[2]):d.uptime;}}
const ePing=document.getElementById('st-ping');if(ePing&&d.ping!==undefined)ePing.textContent=d.ping;
const eJob=document.getElementById('dyn-lastjob');if(eJob&&d.last_job_s!==undefined){const s=d.last_job_s;eJob.textContent=s<0?'waiting for pool':(s<60?s+'s ago':(s<3600?Math.floor(s/60)+'m ago':Math.floor(s/3600)+'h ago'));}
{const fd=v=>{v=Number(v);if(!(v>0))return '0';const u=['','K','M','B','T','P','E'];let i=0;while(v>=1000&&i<6){v/=1000;i++;}return (i===0?(v>=100?v.toFixed(0):v.toPrecision(3)):(v>=100?v.toFixed(0):v.toFixed(1)))+u[i];};
const eRt=document.getElementById('dyn-rtries');if(eRt&&d.round_tries!==undefined){eRt.textContent=fd(d.round_tries);const ex=Number(d.round_expected)||0,p=ex>0?d.round_tries/ex*100:0;const eP=document.getElementById('dyn-rpct');if(eP)eP.textContent=ex>0?('('+p.toFixed(0)+'% of a 10-min round'+(d.round_partial?', since boot':'')+')'):'';const eB=document.getElementById('dyn-rbar');if(eB){eB.style.width=Math.min(p,100)+'%';eB.style.background=p>100?'#e5534b':'#f7931a';}}
const fr=fd;
const eRb=document.getElementById('dyn-rbest');if(eRb&&d.round_best!==undefined){eRb.textContent=fr(d.round_best);eRb.title='score (difficulty) '+fd(d.round_best);}
const eRw=document.getElementById('dyn-rwin');if(eRw&&d.last_winner_diff!==undefined){eRw.textContent=d.last_winner_diff>0?('#'+Number(d.last_winner_height).toLocaleString()+' score '+fr(d.last_winner_diff)):'waiting for a job';eRw.title='score (difficulty) '+fd(d.last_winner_diff)+(d.last_winner_ok?' - beats the network difficulty':'');}
const eHs=document.getElementById('dyn-hstrip');if(eHs&&d.round_best_hash!==undefined){const h=(d.round_best_hash||'').slice(0,20),wz=((d.last_winner_hash||'').match(/^0*/)||[''])[0].length,wn=Math.min(wz,20);let o='';for(let i=0;i<20;i++){const c=h[i]||'-';o+=(c==='0'?'<span style="background:#ff50c8;color:#000">0</span>':'<span style="color:#9aa4b2">'+c+'</span>');if(i+1===wn)o+='<span style="color:#e5534b" title="winner zeros">|</span>';}eHs.innerHTML=o+(wn>0?' <span class="muted" style="font-family:system-ui;font-size:11px;letter-spacing:0">winner: '+wz+' zeros</span>':'');}
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
renderShareLog(d,netKnown?netDiff:0);
cydLastStats=d;if(d.screen!==undefined){cydDevScr=d.screen;cydUpdateDevBadge();cydSyncInitialTab();}cydRender();}
let simIsHist=true,simN=856192320,simAutoTimer=null;
function toHex32LE(n){
const b=new Uint8Array(4);new DataView(b.buffer).setUint32(0,n>>>0,true);
return Array.from(b).map(x=>x.toString(16).padStart(2,'0')).join('');
}
function hexToBytes(hex){
const b=new Uint8Array(hex.length/2);
for(let i=0;i<hex.length;i+=2)b[i/2]=parseInt(hex.substr(i,2),16);
return b;
}
async function sha256dBuffer(buf){
try{
const h1=await crypto.subtle.digest('SHA-256',buf);
const h2=await crypto.subtle.digest('SHA-256',h1);
return Array.from(new Uint8Array(h2)).reverse().map(b=>b.toString(16).padStart(2,'0')).join('');
}catch(e){return'';}
}
async function simStep(){
const curN=simN;
const hexLE=toHex32LE(curN);
const hexBE=(curN>>>0).toString(16).padStart(8,'0');
const eN=document.getElementById('sim-nonce');if(eN)eN.textContent=curN.toLocaleString()+' (0x'+hexBE+')';
const eNHex=document.getElementById('sim-nonce-hex');if(eNHex)eNHex.textContent=hexLE;
let hash='',tgt='';
if(simIsHist){
tgt='00000000000000015f5300000000000000000000000000000000000000000000';
const buf=new Uint8Array(80);
const view=new DataView(buf.buffer);
view.setInt32(0,2,true);
buf.set(hexToBytes('000000000000000117c80378b8da0e33559b5997f2ad55e2f7d18ec1975b9717').reverse(),4);
buf.set(hexToBytes('871714dcbae6c8193a2bb9b2a69fe1c0440399f38d94b3a0f1b447275a29978a').reverse(),36);
view.setUint32(68,1392872245,true);
view.setUint32(72,0x19015f53,true);
view.setUint32(76,curN>>>0,true);
hash=await sha256dBuffer(buf);
}else{
const zeros=parseInt(document.getElementById('sim-diff').value)||4;
tgt='0'.repeat(zeros)+'f'.repeat(64-zeros);
const raw=new TextEncoder().encode('020000000000000000000000000213a48e4a5e1e4baab89f3a325165b821a01704e6c3'+hexLE);
hash=await sha256dBuffer(raw);
}
const isWin=(hash < tgt);
const eHash=document.getElementById('sim-hash');
const eRes=document.getElementById('sim-result');
if(isWin){
if(eHash)eHash.innerHTML='<span style="color:var(--green);font-weight:800;">'+hash.substring(0,18)+'</span>'+hash.substring(18);
if(eRes){
if(simIsHist)eRes.innerHTML='<span style="color:var(--green);">&#10003; Block #286,819 solved at nonce '+curN.toLocaleString()+' (hash &lt; target, 25 BTC subsidy)</span>';
else eRes.innerHTML='<span style="color:var(--green);">&#10003; Hash below target</span>';
}
if(simAutoTimer)simToggleAuto();
}else{
if(eHash)eHash.innerHTML='<span style="color:var(--red);">'+hash.substring(0,6)+'</span>'+hash.substring(6);
if(eRes)eRes.innerHTML='<span style="color:var(--red);">&#10005; Hash above target</span>';
}
simN++;
}
function simToggleAuto(){
const btn=document.getElementById('sim-auto-btn');
if(simAutoTimer){
clearInterval(simAutoTimer);simAutoTimer=null;
if(btn)btn.textContent='Auto (10 H/s)';
}else{
simAutoTimer=setInterval(simStep,100);
if(btn)btn.textContent='Pause';
}
}
function simSetMode(hist){
simIsHist=hist;
if(simAutoTimer)simToggleAuto();
const bHist=document.getElementById('sim-mode-hist');
const bRand=document.getElementById('sim-mode-rand');
const zBox=document.getElementById('sim-zeros-box');
const sBox=document.getElementById('sim-shirriff-box');
const hTitle=document.getElementById('sim-hdr-title');
const fVer=document.getElementById('sim-f-ver');
const fPrev=document.getElementById('sim-f-prev');
const fMrkl=document.getElementById('sim-f-mrkl');
const fTime=document.getElementById('sim-f-time');
const fBits=document.getElementById('sim-f-bits');
const eTgt=document.getElementById('sim-target');
if(hist){
if(bHist)bHist.className='preset-pill active';
if(bRand)bRand.className='preset-pill';
if(zBox)zBox.style.display='none';
if(sBox)sBox.style.display='flex';
if(hTitle)hTitle.textContent='Block header #286,819 (80 bytes)';
if(fVer)fVer.textContent='[Ver 4B: 02000000]'; 
if(fPrev)fPrev.textContent='[Prev 32B: 00000000...9717]'; 
if(fMrkl)fMrkl.textContent='[Merkle 32B: 871714...978a]'; 
if(fTime)fTime.textContent='[Time 4B: 53059335]'; 
if(fBits)fBits.textContent='[Bits 4B: 19015f53]'; 
if(eTgt)eTgt.textContent='00000000000000015f5300000000000000000000000000000000000000000000';
simN=856192320;
}else{
if(bHist)bHist.className='preset-pill';
if(bRand)bRand.className='preset-pill active';
if(zBox)zBox.style.display='flex';
if(sBox)sBox.style.display='none';
if(hTitle)hTitle.textContent='Block header, sandbox (80 bytes)';
if(fVer)fVer.textContent='[Ver 4B: 02000000]'; 
if(fPrev)fPrev.textContent='[Prev 32B: 00000000...a1b2]'; 
if(fMrkl)fMrkl.textContent='[Merkle 32B: 4a5e1e...c3d4]'; 
if(fTime)fTime.textContent='[Time 4B: 65b821a0]'; 
if(fBits)fBits.textContent='[Bits 4B: 1704e6c3]'; 
simN=0;
}
simUpdateTarget();
simReset();
}
function simUpdateTarget(){
const eFml=document.getElementById('sim-diff-formula');
if(simIsHist){
if(eFml)eFml.innerHTML='Difficulty = Target<sub>1</sub> / Target = <strong>3,129,573,174.52</strong> (Expected: ~1.34 &times; 10<sup>19</sup> hashes)';
return;
}
const zeros=parseInt(document.getElementById('sim-diff').value)||4;
const eTgt=document.getElementById('sim-target');
if(eTgt)eTgt.textContent='0'.repeat(zeros)+'f'.repeat(64-zeros);
const expH=Math.pow(16,zeros);
const diff=(expH/4294967296);
if(eFml)eFml.innerHTML='Difficulty = Target<sub>1</sub> / Target = <strong>'+diff.toExponential(4)+'</strong> (Expected: '+expH.toLocaleString()+' hashes = 16<sup>'+zeros+'</sup>)';
}
function simJump(n){
if(simAutoTimer)simToggleAuto();
simN=n;
simStep();
}
function simReset(){
if(simAutoTimer)simToggleAuto();
simN=simIsHist?856192320:0;
const hex=(simN>>>0).toString(16).padStart(8,'0');
const eN=document.getElementById('sim-nonce');if(eN)eN.textContent=simN.toLocaleString()+' (0x'+hex+')';
const eNHex=document.getElementById('sim-nonce-hex');if(eNHex)eNHex.textContent=toHex32LE(simN);
const eH=document.getElementById('sim-hash');if(eH)eH.textContent='Press "Next nonce" to hash';
const eR=document.getElementById('sim-result');if(eR)eR.textContent=simIsHist?'Ready. Solves at nonce 856,192,328.':'Ready.';
}
function calcFromBtc(v){const n=parseFloat(v);if(isNaN(n))return;document.getElementById('calc-sats').value=Math.round(n*1e8).toLocaleString();document.getElementById('calc-bits').value=(n*1e6).toLocaleString();document.getElementById('calc-mbtc').value=(n*1e3).toLocaleString();}
function calcFromSats(v){const s=parseFloat(String(v).replace(/,/g,''));if(isNaN(s))return;document.getElementById('calc-btc').value=(s/1e8).toFixed(8);document.getElementById('calc-bits').value=(s/100).toLocaleString();document.getElementById('calc-mbtc').value=(s/1e5).toLocaleString();}
function calcFromMbtc(v){const m=parseFloat(String(v).replace(/,/g,''));if(isNaN(m))return;document.getElementById('calc-btc').value=(m/1e3).toFixed(8);document.getElementById('calc-sats').value=Math.round(m*1e5).toLocaleString();document.getElementById('calc-bits').value=(m*1e3).toLocaleString();}
function calcFromBits(v){const bt=parseFloat(String(v).replace(/,/g,''));if(isNaN(bt))return;document.getElementById('calc-btc').value=(bt/1e6).toFixed(8);document.getElementById('calc-sats').value=Math.round(bt*100).toLocaleString();document.getElementById('calc-mbtc').value=(bt/1000).toLocaleString();}
function pollStats(){fetch('/api/stats').then(r=>r.json()).then(updateStats).catch(()=>{});}
document.getElementById('cfg-form').addEventListener('keydown',function(e){if(e.key==='Enter'&&e.target.tagName==='INPUT'&&!e.target.name)e.preventDefault();});
(function(){let t='telemetry';try{const l=localStorage.getItem('cyd_tab');if(l)t=l;}catch(e){}switchTab(t);})();
)rawjs";

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
  page.reserve(32000);

  page += F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'>"
            "<meta name='theme-color' content='#0b0d12'>"
            "<title>Aaron Miner</title>"
            "<link rel='stylesheet' href='/dash.css?v=" FW_GIT "'>"
            "<link rel='stylesheet' href='/cyd-twin.css?v=" FW_GIT "'></head><body>"
            "<div class='container'>"
            "<header>"
            "<div class='brand'>"
            "<div class='logo-box' id='site-logo'>&#8383;</div>"
            "<div><div class='brand-title'>Aaron Miner</div><div class='brand-subtitle'>" CURRENT_VERSION " (" FW_GIT ") &bull; ");

  page += htmlEscape(WiFi.localIP().toString()) + F(" &bull; ") + htmlEscape(WiFi.SSID()) + F("</div></div></div>");
  {
    char key[64], shortName[24];
    const char *coin = "BTC";
    getActivePool(key, sizeof(key), &coin);
    poolShortName(key, shortName, sizeof(shortName));
    page += F("<div class='coin-pill' id='st-coin' title='");
    page += htmlEscape(String(key));
    page += F("'>");
    page += String(coin) + F(" &middot; ") + htmlEscape(String(shortName));
    page += F("</div>");
  }
  page += F(""
            "<div class='status-badge' role='status' aria-live='polite'>"
            "<span id='st-txt' class='");

  bool isHashing = (mMonitor.NerdStatus == NM_hashing || (WiFi.status() == WL_CONNECTED && d.currentHashRate.toFloat() > 0));
  if (isHashing)
    page += F("txt-green'>Hashing</span>");
  else if (mMonitor.NerdStatus == NM_Connecting)
    page += F("txt-amber'>Connecting</span>");
  else
    page += F("txt-gray'>Idle</span>");

  page += F("</div></header>"
            "<div class='diag-strip'>"
            "<div class='diag-item primary'><span class='diag-label'>Speed</span><span class='diag-val'><span id='st-top-hr'>");
  page += d.currentHashRate.length() ? d.currentHashRate : String("--");
  page += F("</span> <span class='diag-sub'>KH/s</span></span></div>"
            "<div class='diag-item'><span class='diag-label'>Temp</span><span class='diag-val'><span id='st-temp'>");
  page += d.temp.length() ? d.temp : String("--");
  page += F("</span>&deg;C</span></div>"
            "<div class='diag-item'><span class='diag-label'>Wi-Fi</span><span class='diag-val'><span id='st-wifi'>");
  page += String(getWifiQuality(WiFi.RSSI())) + F("%</span> <span class='diag-sub'>(<span id='st-rssi'>");
  page += String(WiFi.RSSI()) + F("</span> dBm)</span></span></div>"
            "<div class='diag-item'><span class='diag-label'>Pool ping</span><span class='diag-val'><span id='st-ping'>");
  page += String(g_stratumPing) + F("</span> <span class='diag-sub'>ms</span></span></div>"
            "<div class='diag-item'><span class='diag-label'>Free memory</span><span class='diag-val' id='st-heap'>");
  page += String(ESP.getFreeHeap() / 1024) + F(" KB</span></div>"
            "</div>"
            "<div class='console-layout'>"
            "<aside class='console-sidebar'>");
  appendCydTwinHtml(page);
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
  page += F("</aside>"
            "<main class='console-main'>"
            "<nav class='console-tabs' role='tablist'>"
            "<button type='button' class='tab-btn active' id='tbtn-telemetry' onclick=\"switchTab('telemetry')\">Overview</button>"
            "<button type='button' class='tab-btn' id='tbtn-settings' onclick=\"switchTab('settings')\">Settings</button>"
            "<button type='button' class='tab-btn' id='tbtn-learn' onclick=\"switchTab('learn')\">Learn</button>"
            "</nav>");

  page += "<form method='POST' action='/save' id='cfg-form' data-init-rot='" + String(Settings.screenRotation) + "'>";

  // Tab 1: Overview
  page += F("<section class='tab-pane active' id='pane-telemetry'>"
            "<div class='overview-grid'>"
            "<div class='tele-card'>"
            "<div class='tele-card-hdr'>"
            "<div class='tele-card-title'>Speed <span class='muted' style='font-weight:400'>guesses per second (hashrate)</span></div>"
            "</div>"
            "<div class='tele-hero-val amber'><span id='dyn-hps'>");
  float hr = d.currentHashRate.toFloat();
  unsigned long hps = (unsigned long)(hr * 1000.0f);
  page += (hps > 0 ? String(hps) : F("0"));
  page += F("</span> <span class='unit'>H/s</span></div>"
            "<div class='tele-hero-sub'><span id='dyn-hpd'>--</span> hashes per day</div>"
            "<div class='tele-row'><span>Total hashes</span><span class='hl-val'><span id='dyn-totwork'>");
  uint64_t mhashes = (uint64_t)d.totalMHashes.toInt();
  if (mhashes >= 1000) page += String((float)mhashes / 1000.0f, 1) + F(" billion");
  else page += String((unsigned long)mhashes) + F(" million");
  page += F("</span></span></div>"
            "<div class='tele-row' title='Why it matters: the pool hands your miner a fresh puzzle (a job) every 30-60 s, and right away when anyone finds a block. Old puzzles are worthless, so a recent time means you are working on current work.'><span>New puzzle from pool</span><span class='hl-val' id='dyn-lastjob'>--</span></div>"
            "<div class='tele-row' title='Why it matters: every proof (share) is checked by the pool. ok = it counted; bad = refused (stale after a new block, or a duplicate). A few bad ones are normal.'><span>Proofs checked by pool</span><span class='hl-val' id='dyn-okbad'>--</span></div>"
            "<div class='tele-row' title='Why it matters: every new block starts a new round and all earlier work is thrown away - everyone starts over. The bar fills with your tries since the last block; 100% = what you try in an average 10-minute round.'><span>Tries this round</span><span class='hl-val'><span id='dyn-rtries'>--</span> <span class='muted' id='dyn-rpct'></span></span></div>"
            "<div style='height:6px;border-radius:3px;background:var(--card-border);overflow:hidden;margin:2px 0 4px'><div id='dyn-rbar' style='height:100%;width:0;background:#f7931a'></div></div>"
            "<div class='tele-row' title='Why it matters: a hash is a random 64-digit hex number. A block needs one that starts with enough zeros (the red mark = how many the last winner had). Each extra zero is 16x rarer. This is your best hash since the last block.'><span>Best hash this round</span><span class='hl-val' id='dyn-hstrip' style='font-family:ui-monospace,monospace;font-size:12px;letter-spacing:1px'>--</span></div>"
            "<div class='tele-row' title='Why it matters: your closest hash since the last block. It resets every block, because old work cannot win the next one.'><span>My best this round</span><span class='hl-val' id='dyn-rbest'>--</span></div>"
            "<div class='tele-row' title='Why it matters: the winning block hash itself, scored like your proofs. It had to beat the network difficulty, which is why one CYD almost never wins.'><span>Last winner</span><span class='hl-val' id='dyn-rwin'>--</span></div>"
            "<div class='tele-row' title='Why it matters: each round you get a fresh lottery ticket. Your best vs the winner shows how far off a small miner is, round after round.'><span>Recent rounds (my best vs winner)</span><span class='hl-val' id='dyn-rounds'>--</span></div>"
            "<div class='tele-row'><span>Uptime</span><span class='hl-val' id='dyn-uptime'>");
  page += (d.timeMining.length() ? d.timeMining : F("--"));
  page += F("</span></div>"
            "</div>"
            "<div class='tele-card'>"
            "<div class='tele-card-hdr'>"
            "<div class='tele-card-title'>Shares <span class='muted' style='font-weight:400'>proofs of work</span> <span class='muted' id='dyn-poolcoin' style='font-weight:400'></span></div>"
            "<button type='button' class='btn-reset-stats' id='btn-reset-session' onclick='resetSession()' title='Reset session shares and best difficulty'>Reset</button>"
            "</div>"
            "<div class='tele-hero-val'><span id='dyn-shares'>");
  page += (d.completedShares.length() ? d.completedShares : F("0"));
  page += F("</span> <span class='unit'>shares</span></div>"
            "<div class='tele-hero-sub' title='Why it matters: shares are easy proofs the pool asks for so it can see you are working. On a solo pool they do not pay.'>About 1 per <span id='dyn-hpshare'>--</span> (<span id='dyn-tpshare'>--</span>)</div>"
            "<div class='tele-row' title='Why it matters: difficulty measures how good a guess is. A block needs the network difficulty; this is your best so far.'><span>Closest try (best difficulty, session)</span><span class='hl-val'><span id='dyn-diff'>");
  page += (d.bestDiff.length() ? d.bestDiff : F("0.000"));
  page += F("</span></span></div>"
            "<div class='tele-row'><span>Rarity</span><span class='hl-val'>1 in <span id='dyn-rarity'>--</span></span></div>"
            "</div>"
            "<div class='tele-card'>"
            "<div class='tele-card-hdr'>"
            "<div class='tele-card-title'>Block reward</div>"
            "</div>"
            "<div class='tele-hero-val'>3.125 BTC <span class='unit'>+ fees</span></div>"
            "<div class='tele-hero-sub'>Paid to your address if this miner finds a block</div>"
            "<div class='tele-row'><span>Best vs. network</span><span class='hl-val'><span id='dyn-netpct'>--</span> <span class='muted'>of <span id='dyn-netdiff'>100T</span></span></span></div>"
            "<div class='tele-row'><span>Daily odds</span><span class='hl-val'><span id='dyn-dayodds'>--</span></span></div>"
            "<div class='tele-row'><span>Yearly odds</span><span class='hl-val'><span id='dyn-yrodds'>--</span></span></div>"
            "<div class='tele-row' title='Why it matters: mining is a lottery. This is the average time for THIS miner to win one block at the current difficulty.'><span>Expected wait (avg)</span><span class='hl-val' id='dyn-avgwait'>--</span></div>"
            "<div class='tele-row' title='Why it matters: your slice of all the guessing in the world. Blocks go to whoever guesses right first.'><span>Share of network</span><span class='hl-val'><span id='dyn-netpow'>--</span> of ~<span id='dyn-nethr'>--</span></span></div>"
            "<div class='tele-row'><span>Power</span><span class='hl-val'>~1.2 W, under 1 kWh/month</span></div>"
            "</div>"
            "</div>"
            "<div class='tele-card'>"
            "<div class='tele-card-hdr'><div class='tele-card-title'>How close each share got <span class='muted' id='sl-coin' style='font-weight:400'></span></div>"
            "<span class='muted' style='font-size:12px'>pool asks for <span id='sl-pool'>--</span></span></div>"
            "<div class='tele-hero-sub'>A share proves work but pays nothing on a solo pool. Only a share that reaches the network difficulty (<span id='sl-net'>--</span>) is a block.</div>"
            "<div class='tele-row'><span>Closest try</span><span class='hl-val'><span id='sl-best'>--</span> <span class='muted' id='sl-best-gap'></span></span></div>"
            "<div id='share-log' class='share-log'><div class='muted' style='font-size:13px;padding:6px 0'>No shares yet.</div></div>"
            "</div>"
            "</section>");

  // Tab 2: Settings -- one form pane: Wallet & pool, Display, System
  page += F("<section class='tab-pane' id='pane-settings'>"
            "<section class='settings-sec' id='pane-stratum'>"
            "<h2 class='settings-h'>Wallet &amp; pool</h2>");

  // Wallet: the one setting that decides where a block reward goes
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Wallet</div>"
            "</div>"
            "<div class='cfg-card-body'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-wallet'>");
  page += String(coin.name) + " (" + String(coin.ticker) + F(") address</label>"
            "<input type='text' id='f-wallet' class='form-control' name='wallet' value='");
  page += htmlEscape(String(Settings.BtcWallet));
  page += F("' placeholder='Your on-chain address (e.g. bc1q...)' required>"
            "<span class='form-hint'>A found block pays 3.125 ");
  page += String(coin.ticker) + F(" + fees straight to this address. Add <code>.name</code> to set a worker name, e.g. <code>");
  page += (cleanWallet.length() ? htmlEscape(cleanWallet) : F("bc1q...")) + F(".cyd</code>).</span>"
            "</div>"
            "</div></div>");

  // Pool: primary first, presets next to it, fallback folded away
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Pool</div>"
            "<span class='active-pool-pill");
  page += (onBackupPool() ? F(" fallback'>Using fallback") : F("'>Using primary"));
  page += F("</span>"
            "</div>"
            "<div class='cfg-card-body'>"
            "<div class='form-grid-3'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-pool'>Host</label>"
            "<input type='text' id='f-pool' class='form-control' name='pool' value='");
  page += htmlEscape(Settings.PoolAddress);
  page += F("' placeholder='public-pool.io' required>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-port'>Port</label>"
            "<input type='number' id='f-port' class='form-control' name='port' value='");
  page += String(Settings.PoolPort);
  page += F("' placeholder='3333' required>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-pass'>Password</label>"
            "<input type='text' id='f-pass' class='form-control' name='poolpass' value='");
  page += htmlEscape(String(Settings.PoolPassword));
  page += F("' placeholder='x'>"
            "</div>"
            "</div>"
            "<div class='preset-bar'>"
            "<div class='preset-pills'>"
            "<span class='form-label-sub'>Presets</span>"
            "<button type='button' class='preset-pill active' onclick='choosePreset(\"public-pool.io\",3333,\"x\",\"public-pool.io:3333 is the recommended port.\")'>public-pool.io:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"pool.nerdminers.org\",3333,\"x\",\"pool.nerdminers.org is a solo pool for low-hashrate miners.\")'>pool.nerdminers.org:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"pool.solomining.de\",3333,\"x\",\"pool.solomining.de is a European solo pool.\")'>pool.solomining.de:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"public-pool.io\",21496,\"x\",\"Legacy port. Use 3333 if connections drop.\")'>public-pool.io:21496 (legacy)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"stratum.shears.co.uk\",3690,\"x\",\"Shears ESP32-only port for Polaris (POL). Mines an altcoin, not BTC; needs a free shears.co.uk account. Payout is to that account, not this BTC address.\")'>Shears ESP32 POL :3690 (needs free account)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"stratum.shears.co.uk\",3700,\"x\",\"Shears ESP32-only port for Embargocoin (EMB). Mines an altcoin, not BTC; needs a free shears.co.uk account. Payout is to that account, not this BTC address.\")'>Shears ESP32 EMB :3700 (needs free account)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"stratum.shears.co.uk\",3710,\"x\",\"Shears ESP32-only port for Fixed Trade Coin (FXTC). Mines an altcoin, not BTC; needs a free shears.co.uk account. Payout is to that account, not this BTC address.\")'>Shears ESP32 FXTC :3710 (needs free account)</button>"
            "</div>"
            "<div id='preset-note' class='preset-comment'>Presets fill in the pool above. Save to apply.</div>"
            "</div>");
  page += (onBackupPool() ? F("<details class='more' open>") : F("<details class='more'>"));
  page += F("<summary>Fallback pool (optional)</summary>"
            "<div class='more-body'>"
            "<span class='form-hint'>Used after 3 failed connections to the pool above.</span>"
            "<div class='form-grid-3'>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-fbpool'>Host</label>"
            "<input type='text' id='f-fbpool' class='form-control' name='fbpool' value='");
  page += htmlEscape(Settings.FallbackPoolAddress);
  page += F("' placeholder='pool.nerdminers.org'>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-fbport'>Port</label>"
            "<input type='number' id='f-fbport' class='form-control' name='fbport' value='");
  page += String(Settings.FallbackPoolPort);
  page += F("' placeholder='3333'>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-fbpass'>Password</label>"
            "<input type='text' id='f-fbpass' class='form-control' name='fbpass' value='");
  page += htmlEscape(String(Settings.FallbackPoolPassword));
  page += F("' placeholder='x'>"
            "</div>"
            "</div>"
            "</div></details>"
            "</div></div>");

  page += F("</section>" // closes pane-stratum
            "<section class='settings-sec' id='pane-display'>"
            "<h2 class='settings-h'>Display</h2>");

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
  page += "<option value='0'" + String(curRot == 0 ? " selected" : "") + ">Portrait, USB at bottom (default)</option>";
  page += "<option value='1'" + String(curRot == 1 ? " selected" : "") + ">Landscape, USB on right</option>";
  page += "<option value='2'" + String(curRot == 2 ? " selected" : "") + ">Portrait flipped, USB at top</option>";
  page += "<option value='3'" + String(curRot == 3 ? " selected" : "") + ">Landscape flipped, USB on left</option>";
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
  page += F("> <span>Invert colors</span></label>"
            "<div style='display:flex;gap:8px;margin-top:10px'>"
            "<button type='button' class='btn-restart' id='btn-recal-touch' onclick='recal(\"touch\")'>Recalibrate touch</button>"
            "<button type='button' class='btn-restart' id='btn-recal-colors' onclick='recal(\"colors\")'>Recalibrate colors</button>"
            "</div>"
            "<span class='form-hint'>Restarts the miner and runs that check on its screen. Mining resumes right away; hold BOOT to skip.</span>"
            "</div></div>"
            "<div class='form-group'>"
            "<span class='form-label'>Theme <span class='form-label-sub'>applies immediately</span></span>"
            "<div class='theme-grid'>");
  int curTheme = Settings.displayTheme;
  page += F("<div class='theme-card");
  if (curTheme == 0) page += F(" active");
  page += F("' id='thm-0' onclick='selectTheme(0,\"#f7931a\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#f7931a'></span><span class='swatch-dot' style='background:#10141c'></span></div>"
            "<h4>Instrument Amber</h4><p>Default</p></div>"
            "<div class='theme-card");
  if (curTheme == 1) page += F(" active");
  page += F("' id='thm-1' onclick='selectTheme(1,\"#22c55e\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#22c55e'></span><span class='swatch-dot' style='background:#08140a'></span></div>"
            "<h4>Matrix Green</h4><p>Green on black</p></div>"
            "<div class='theme-card");
  if (curTheme == 2) page += F(" active");
  page += F("' id='thm-2' onclick='selectTheme(2,\"#f59e0b\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#f59e0b'></span><span class='swatch-dot' style='background:#181208'></span></div>"
            "<h4>Cyber Amber</h4><p>Amber CRT</p></div>"
            "<div class='theme-card");
  if (curTheme == 3) page += F(" active");
  page += F("' id='thm-3' onclick='selectTheme(3,\"#06b6d4\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#06b6d4'></span><span class='swatch-dot' style='background:#ec4899'></span></div>"
            "<h4>Arcade Neon</h4><p>Cyan and magenta</p></div>"
            "<div class='theme-card");
  if (curTheme == 4) page += F(" active");
  page += F("' id='thm-4' onclick='selectTheme(4,\"#38bdf8\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#38bdf8'></span><span class='swatch-dot' style='background:#111827'></span></div>"
            "<h4>Ice Blue</h4><p>Slate and blue</p></div>"
            "<div class='theme-card");
  if (curTheme == 5) page += F(" active");
  page += F("' id='thm-5' onclick='selectTheme(5,\"#ffffff\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#ffffff'></span><span class='swatch-dot' style='background:#181818'></span></div>"
            "<h4>Monochrome</h4><p>White on black</p></div>"
            "</div>"
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
  page += F("' placeholder='0'>"
            "<span class='form-hint'>0 = never. Touch or BOOT wakes it.</span>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-nbright'>Night brightness (1–255)</label>"
            "<input type='number' id='f-nbright' class='form-control' name='nightbright' min='1' max='255' value='");
  page += String(Settings.NightBrightness);
  page += F("'>"
            "</div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-nfade'>Fade (min)</label>"
            "<input type='number' id='f-nfade' class='form-control' name='nightfade' min='0' max='240' value='");
  page += String(Settings.NightFade);
  page += F("'>"
            "</div>"
            "</div>"
            "<label class='form-check'>"
            "<input type='checkbox' name='nightdim'");
  if (Settings.nightDim) page += F(" checked");
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
  page += F("</select></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-lat'>Latitude (&deg;)</label>"
            "<input type='text' id='f-lat' name='lat' class='form-control' value='");
  page += String(Settings.Latitude, 2);
  page += F("'></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-lon'>Longitude (&deg;)</label>"
            "<input type='text' id='f-lon' name='lon' class='form-control' value='");
  page += String(Settings.Longitude, 2);
  page += F("'></div>"
            "</div>"
            "<span class='form-hint'>Status: ");
  page += nightDimStatus();
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
  page += F("</select></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-ledcolor'>Color</label>"
            "<input type='color' id='f-ledcolor' name='ledcolor' class='form-control' value='");
  char colorHex[8];
  snprintf(colorHex, sizeof(colorHex), "#%06lx", (unsigned long)(Settings.ledColor & 0xFFFFFF));
  page += String(colorHex) + F("'></div>"
            "<div class='form-group'>"
            "<label class='form-label' for='f-ledbright'>Brightness (1–255)</label>"
            "<input type='number' id='f-ledbright' name='ledbright' class='form-control' min='1' max='255' value='");
  page += String(Settings.ledBrightness) + F("'></div></div>"
            "<label class='form-check'>"
            "<input type='checkbox' name='ledscreen'");
  if (Settings.ledScreenOff) page += F(" checked");
  page += F("> <span>Turn the LED off when the screen sleeps</span></label>"
            "</div></details>");
#endif

  page += F("</div></div>" // closes Display card
            "</section>"); // closes pane-display


  page += F("<section class='settings-sec' id='pane-system'>"
            "<h2 class='settings-h'>System</h2>");

  // Clock and data
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Clock and data</div>"
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
            "<label class='form-label' for='f-tz'>UTC offset (hours)</label>"
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
  page += F("> <span>Keep lifetime hashes and shares across reboots (stored in flash)</span></label>"
            "</div></div></div>"
            "<div class='cfg-card'>"
            "<div class='cfg-card-hdr'><div class='cfg-card-title'>Firmware update</div></div>"
            "<div class='cfg-card-body'>"
            "<span class='form-hint'>Upload a <code>firmware.bin</code> over Wi-Fi. The miner flashes it and reboots.</span>"
            "<div class='ota-row'>"
            "<input type='file' id='ota-file' class='file-input' accept='.bin'>"
            "<button type='button' class='btn-ota' id='btn-ota-start' onclick='uploadOta()'>Upload and flash</button>"
            "</div>"
            "<div class='ota-progress-box' id='ota-progress-box'>"
            "<div class='ota-progress-bar'><div class='ota-progress-fill' id='ota-progress-fill'></div></div>"
            "<div class='ota-status' id='ota-status'>Ready</div>"
            "</div>"
            "</div></div>"
            "<details class='edu-acc'>"
            "<summary class='edu-sum'>Touch and button controls</summary>"
            "<div class='edu-body'>"
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
            "</section>"); // closes pane-system

  page += F("<div class='actions' id='form-actions'>"
            "<button type='submit' class='btn-save' id='btn-save'>Save settings</button>"
            "<button type='button' class='btn-restart' id='btn-reboot' onclick='rebootDevice()'>Reboot without saving</button>"
            "</div>"
            "</section>"); // closes pane-settings

  // Tab 3: Learn
  page += F("<section class='tab-pane' id='pane-learn'>");
  // Start here: the pictures on the CYD, explained (device screens only have room for a few words)
  page += F("<section class='settings-sec' id='pane-start'>"
            "<div class='cfg-card'>"
            "<div class='cfg-card-hdr'><div class='cfg-card-title'>Start here: what your CYD is showing</div></div>"
            "<div class='cfg-card-body edu-body' style='line-height:1.55'>"
            "<svg viewBox='0 0 520 70' width='100%' style='max-width:520px;display:block;margin:0 auto 10px' role='img' aria-label='Pool sends a job, the CYD guesses, good guesses go back as shares'>"
            "<g font-family='system-ui,sans-serif' font-size='12' text-anchor='middle'>"
            "<rect x='4' y='18' width='90' height='34' rx='8' fill='none' stroke='#8a96a8'/><text x='49' y='40' fill='#e6edf3'>Pool</text>"
            "<rect x='214' y='18' width='90' height='34' rx='8' fill='none' stroke='#f7931a'/><text x='259' y='40' fill='#f7931a'>CYD guesses</text>"
            "<rect x='424' y='18' width='90' height='34' rx='8' fill='none' stroke='#8a96a8'/><text x='469' y='40' fill='#e6edf3'>Pool</text>"
            "<path d='M96 35H208' stroke='#8a96a8'/><path d='M208 35l-7-4v8z' fill='#8a96a8'/><text x='152' y='28' fill='#8a96a8'>job (puzzle)</text>"
            "<path d='M306 35H418' stroke='#8a96a8'/><path d='M418 35l-7-4v8z' fill='#8a96a8'/><text x='362' y='28' fill='#8a96a8'>share (proof)</text>"
            "</g></svg>"
            "&bull; <strong>Guesses / sec</strong> (hashrate, KH/s = thousand per second). Each guess runs the block header through SHA-256 and checks if the result is small enough. It is a lottery: every guess is an independent ticket.<br>"
            "&bull; <strong>Share</strong>: a guess good enough to clear the pool's easy bar. It proves your miner is really working. On a solo pool shares pay nothing.<br>"
            "&bull; <strong>Closest try</strong> (best difficulty): your single best guess so far. Higher is closer to a block.<br>"
            "&bull; <strong>Block</strong> = the jackpot. A guess must reach the network difficulty (about 140 trillion for Bitcoin today). Your CYD finds a share roughly every hour, a block maybe once in millions of years. Small coins on ESP32 pools have far lower difficulty.<br>"
            "<svg viewBox='0 0 520 120' width='100%' style='max-width:520px;display:block;margin:10px auto' role='img' aria-label='Ladder: each rung is ten times harder; your closest try lit near the bottom, the block trophy at the top'>"
            "<g font-family='system-ui,sans-serif' font-size='11'>"
            "<line x1='30' y1='16' x2='30' y2='112' stroke='#3a4658'/><line x1='54' y1='16' x2='54' y2='112' stroke='#3a4658'/>"
            "<g stroke-width='3'><line x1='31' y1='110' x2='53' y2='110' stroke='#f7931a'/><line x1='31' y1='103' x2='53' y2='103' stroke='#f7931a'/><line x1='31' y1='96' x2='53' y2='96' stroke='#3a4658'/><line x1='31' y1='89' x2='53' y2='89' stroke='#3a4658'/><line x1='31' y1='82' x2='53' y2='82' stroke='#3a4658'/><line x1='31' y1='75' x2='53' y2='75' stroke='#3a4658'/><line x1='31' y1='68' x2='53' y2='68' stroke='#3a4658'/><line x1='31' y1='61' x2='53' y2='61' stroke='#3a4658'/><line x1='31' y1='54' x2='53' y2='54' stroke='#3a4658'/><line x1='31' y1='47' x2='53' y2='47' stroke='#3a4658'/><line x1='31' y1='40' x2='53' y2='40' stroke='#3a4658'/><line x1='31' y1='33' x2='53' y2='33' stroke='#3a4658'/><line x1='31' y1='26' x2='53' y2='26' stroke='#3a4658'/><line x1='31' y1='18' x2='53' y2='18' stroke='#f59e0b'/></g>"
            "<text x='66' y='22' fill='#f59e0b'>block (jackpot)</text><text x='66' y='113' fill='#f7931a'>your closest try</text>"
            "<text x='200' y='50' fill='#e6edf3'>The ODDS screen is a ladder.</text>"
            "<text x='200' y='68' fill='#8a96a8'>Each rung is 10x harder than the one below.</text>"
            "<text x='200' y='86' fill='#8a96a8'>Lit rungs = how far your best guess climbed.</text>"
            "<text x='200' y='104' fill='#8a96a8'>Dots = your shares, oldest to newest.</text>"
            "</g></svg>"
            "<p style='margin:10px 0 4px'><strong>The three screens</strong> (tap the right half of the CYD for the next one, the left half to go back):</p>"
            "&bull; <strong>MINE</strong> &ndash; what is my miner doing right now? The dial is <em>guesses / sec</em>. Around it, the middle ring spells out your best hash this round, one hex digit per cell (leading zeros lit magenta: the only zeros count on the dial). The inner blue ring is your guesses this round; full = what you'd make in a typical 10-minute block at your average speed (it stays full if a round runs long). When someone finds a block both empty and start over, because all earlier work is thrown away. The panel below shows <em>Hash rate</em> (your speed over the last 60 s against its average, &plusmn;11%), <em>New zero records</em> (the last 3 times your best zeros count went up this round, with how long ago) and <em>Guesses</em> this round (blue, like the inner ring). <em>Proofs sent</em> = shares the pool accepted (they prove the miner works; on a solo pool they earn nothing). <em>Pool</em> = online / linking / refused. <em>Proof every</em> = how often a share should arrive at your speed.<br>"
            "&bull; <strong>ODDS</strong> &ndash; how close am I to winning? <em>Closest try / 141T</em>: your best guess next to what a block needs; <em>1 in 4.5T</em> means it would need to be 4.5 trillion times better. The ladder plots every share (gold = your best). <em>Avg wait for block</em> = network difficulty &times; 2<sup>32</sup> &divide; your guesses per second; for Bitcoin it is billions of years, which is why solo mining on a CYD is a lottery ticket, not an income. <em>Odds per block</em> = your chance of winning any one block. <em>Last net block</em>: someone else found a block X ago (about one every 10 minutes); <em>you: N tries</em> is how many guesses your CYD made since.<br>"
            "&bull; <strong>WORLD</strong> &ndash; how does my miner fit into Bitcoin? <em>Your share of all guesses</em>: the long bar is every miner on Earth, the dot is you (computed from the difficulty, so it works offline). <em>Difficulty</em> = how hard the puzzle is; it adjusts so a block is found about every 10 minutes. <em>Block height</em> = blocks so far (read from the pool's job). <em>Next halving</em>: every 210,000 blocks (~4 years) the reward per block is cut in half. Price and fee show when the price service answers; otherwise the line says why. The strip shows time running, chip temperature and blocks won.<br>"
            "<span class='muted'>Aaron Miner is built on the open-source <a href='https://github.com/BitMaker-hub/NerdMiner_v2' target='_blank' rel='noopener'>NerdMiner</a> project and its community forks. Thank you to everyone who built it.</span>"
            "</div></div>"
            "</section>");
  page += F("<section class='settings-sec' id='pane-pow'>"
            "<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Proof-of-work simulator</div>"
            "</div>"
            "<div class='cfg-card-body'>"
            "<p class='sim-intro'>The 80-byte header your miner hashes about a million times a second. Step through a real block, or try a sandbox target.</p>"
            "<div class='preset-pills'>"
            "<button type='button' class='preset-pill active' id='sim-mode-hist' onclick='simSetMode(true)'>Block #286,819</button>"
            "<button type='button' class='preset-pill' id='sim-mode-rand' onclick='simSetMode(false)'>Sandbox</button>"
            "</div>"
            "<div id='sim-shirriff-box' class='preset-pills' style='display:flex'>"
            "<span class='form-label-sub'>Jump to nonce</span>"
            "<button type='button' class='preset-pill sm' onclick='simJump(0)'>0 (0 zeros)</button>"
            "<button type='button' class='preset-pill sm' onclick='simJump(39)'>39 (1 zero)</button>"
            "<button type='button' class='preset-pill sm' onclick='simJump(990)'>990 (3 zeros)</button>"
            "<button type='button' class='preset-pill sm' onclick='simJump(52117)'>52,117 (4 zeros)</button>"
            "<button type='button' class='preset-pill sm' onclick='simJump(856192320)'>856,192,320 (just before)</button>"
            "<button type='button' class='preset-pill sm' onclick='simJump(856192328)'>856,192,328 (solves the block)</button>"
            "</div>"
            "<div class='sim-box'>"
            "<div class='sim-k'><strong id='sim-hdr-title'>Block header #286,819 (80 bytes)</strong></div>"
            "<div class='sim-fields'>"
            "<span style='color:#79c0ff' id='sim-f-ver'>[Ver 4B: 02000000]</span> "
            "<span style='color:#7ee787' id='sim-f-prev'>[Prev 32B: 00000000...9717]</span> "
            "<span style='color:#d2a8ff' id='sim-f-mrkl'>[Merkle 32B: 871714...978a]</span> "
            "<span style='color:#ffa657' id='sim-f-time'>[Time 4B: 53059335]</span> "
            "<span style='color:#ff7b72' id='sim-f-bits'>[Bits 4B: 19015f53]</span> "
            "<span style='color:var(--amber);font-weight:700;'>[Nonce 4B: <span id='sim-nonce-hex'>40750833</span>]</span>"
            "</div>"
            "<div class='row-between'>"
            "<div>Nonce <span id='sim-nonce' class='sim-nonce'>856,192,320 (0x33087540)</span></div>"
            "<div id='sim-zeros-box' class='preset-pills' style='display:none'>"
            "<label class='sim-k' for='sim-diff'>Leading zeros</label>"
            "<select id='sim-diff' class='ctl-auto' onchange='simUpdateTarget()'>"
            "<option value='3'>3</option>"
            "<option value='4' selected>4</option>"
            "<option value='5'>5</option>"
            "<option value='6'>6</option>"
            "</select>"
            "</div></div>"
            "<div>"
            "<div class='sim-k'>Target</div>"
            "<div id='sim-target' class='sim-v'>00000000000000015f5300000000000000000000000000000000000000000000</div>"
            "<div id='sim-diff-formula' class='sim-k sim-v'>Difficulty = Target<sub>1</sub> / Target = <strong>3,129,573,174.52</strong> (Expected: ~1.34 &times; 10<sup>19</sup> hashes)</div>"
            "</div>"
            "<div>"
            "<div class='sim-k'>Hash (SHA256d, byte-reversed)</div>"
            "<div id='sim-hash' class='sim-v sim-hash'>Press \"Next nonce\" to hash</div>"
            "</div>"
            "<div id='sim-result' class='sim-result'>Ready. Solves at nonce 856,192,328.</div>"
            "</div>"
            "<div class='preset-pills'>"
            "<button type='button' class='preset-pill active' onclick='simStep()'>Next nonce</button>"
            "<button type='button' class='preset-pill' id='sim-auto-btn' onclick='simToggleAuto()'>Auto (10 H/s)</button>"
            "<button type='button' class='preset-pill' onclick='simReset()'>Reset</button>"
            "</div>"
            "</div></div>"
            "<div class='cfg-card'>"
            "<div class='cfg-card-hdr'><div class='cfg-card-title'>Unit converter</div></div>"
            "<div class='cfg-card-body'>"
            "<div class='form-grid-4'>"
            "<div class='form-group'><label class='form-label' for='calc-btc'>BTC</label><input type='text' id='calc-btc' inputmode='decimal' autocomplete='off' value='1.0' oninput='calcFromBtc(this.value)'></div>"
            "<div class='form-group'><label class='form-label' for='calc-sats'>Satoshis</label><input type='text' id='calc-sats' inputmode='numeric' autocomplete='off' value='100,000,000' oninput='calcFromSats(this.value)'></div>"
            "<div class='form-group'><label class='form-label' for='calc-bits'>Bits (&mu;BTC)</label><input type='text' id='calc-bits' inputmode='numeric' autocomplete='off' value='1,000,000' oninput='calcFromBits(this.value)'></div>"
            "<div class='form-group'><label class='form-label' for='calc-mbtc'>mBTC</label><input type='text' id='calc-mbtc' inputmode='decimal' autocomplete='off' value='1,000' oninput='calcFromMbtc(this.value)'></div>"
            "</div>"
            "<span class='form-hint'>1 BTC = 100,000,000 sats. Bitcoin stores every balance as a whole number of sats, which is why the supply tops out at 20,999,999.9769 BTC, not 21 million.</span>"
            "</div></div>"
            "<div class='section-hdr'>How mining works</div>"
            "<details class='edu-acc' name='cyd-faq'>"
            "<summary class='edu-sum'><span>Hashing and the target</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>The Avalanche Effect:</strong> SHA-256 compresses arbitrary data into a deterministic 256-bit (32-byte) digest. Altering a single bit scrambles the output completely. Because preimage recovery is computationally intractable, finding candidate blocks requires exhaustive brute-force search.<br>"
            "&bull; <strong>Double-SHA256 (<code>SHA256d</code>):</strong> Bitcoin executes two sequential SHA-256 rounds (<code>SHA256(SHA256(header))</code>) to prevent length-extension attacks on Merkel-Damg&aring;rd hash constructions.<br>"
            "&bull; <strong>Target Unpacking:</strong> Mining requires <code>hash &lt; target</code>. The 4-byte <code>Bits</code> field unpacks via <code>Target = Mantissa &times; 256<sup>(Exponent - 3)</sup></code>. Base Target<sub>1</sub> (<code>0x1d00ffff</code>) requires an expected <strong>2<sup>32</sup> &approx; 4,294,967,296 hashes</strong> per difficulty unit."
            "</div></details>"
            "<details class='edu-acc' name='cyd-faq'>"
            "<summary class='edu-sum'><span>Why this chip tops out near 1 MH/s</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>The No-Midstate Hardware Constraint:</strong> An 80-byte block header spans two 64-byte SHA-256 blocks, plus a third block for the second hash pass. Unlike modern ASICs or ESP32-S3, the classic ESP32 (D0WD) cannot preload midstates: every nonce must grind all three 64-byte blocks from the standard IV constants ($H_0 \\dots H_7$).<br>"
            "&bull; <strong>228-Cycle Gate Delay:</strong> Three blocks at 64 rounds each plus engine handshakes require <strong>&sim;228 CPU cycles</strong> of hardware gate execution. At 240 MHz, the theoretical silicon speed ceiling is <code>240M / 228 &approx; 1.05 MH/s</code>.<br>"
            "&bull; <strong>247-Cycle Pipelined Assembly:</strong> The tuned assembly mining loop (<code>pipelined_hw_mine_classic_v2.S</code>) executes in 247 cycles per nonce&mdash;achieving <strong>92.3% hardware engine saturation</strong> (yielding <strong>~971.6 KH/s</strong> HW from Core 1). Core 0 contributes <strong>~25 KH/s</strong> in parallel software math for a verified <strong>~996–1,002 KH/s</strong> sustained throughput (Pham et al., 2026, <a href='https://arxiv.org/abs/2602.04415' target='_blank'>arXiv:2602.04415</a>; Pandya, 2024, <a href='https://arxiv.org/abs/2407.08284' target='_blank'>arXiv:2407.08284</a>)."
            "</div></details>"
            "<details class='edu-acc' name='cyd-faq'>"
            "<summary class='edu-sum'><span>How the two cores share the SHA engine</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>The 3-Cycle DPORT Bus Glitch:</strong> On dual-core ESP32, both cores share internal DPORT bus access to cryptographic peripherals. If Core 1 writes to the SHA registers with &lt;3 cycles between writes while Core 0 accesses peripheral memory, the internal bus arbiter silently drops the register write.<br>"
            "&bull; <strong>The Vanity Counter Trap:</strong> When writes are dropped, hardware hashes stale or corrupted data. A naive miner increments its loop counter and claims high speed, but produces zero valid shares.<br>"
            "&bull; <strong>Strict Core Decoupling:</strong> This firmware completely isolates peripherals: Core 1 monopolizes the hardware SHA engine with calibrated cycle-spacing instructions, while Core 0 runs purely in software math (<code>nerd_sha256d_baked</code>) for UI and worker tasks, preventing bus collisions entirely (Banicescu et al., 2024, <a href='https://arxiv.org/abs/2403.03876' target='_blank'>arXiv:2403.03876</a>; Ayyagari, 2019, <a href='https://arxiv.org/abs/1906.00834' target='_blank'>arXiv:1906.00834</a>)."
            "</div></details>"
            "<details class='edu-acc' name='cyd-faq'>"
            "<summary class='edu-sum'><span>Share checks, the 21M cap and confirmations</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>Zero Phantom Counters:</strong> Every candidate block found by hardware is re-verified bit-for-bit in software before pool submission. The firmware self-tests via Known-Answer Test (KAT) at boot and automatically demotes the engine via <code>pl_health</code> if any bit flips, guaranteeing that every reported hash represents genuine thermodynamic Proof-of-Work (Angulo de Lafuente et al., 2026, <a href='https://arxiv.org/abs/2601.12032' target='_blank'>arXiv:2601.12032</a>).<br>"
            "&bull; <strong>Fixed 21M Supply:</strong> Fixed issuance cap of <strong>20,999,999.9769 BTC</strong>. Subsidy halves every 210,000 blocks via bitwise shift (<code>&gt;&gt; 1</code>), reaching zero at block 6,929,999 around <strong>Year 2140</strong>.<br>"
            "&bull; <strong>Satoshi's Gambler's Ruin Finality:</strong> In Section 11 of the 2008 Whitepaper, Satoshi modeled double-spend races against a 10% hash attacker. After <strong>6 confirmations (~1 hour)</strong>, catch-up probability drops exponentially to <strong>P = 0.0002428 (&lt;0.024%)</strong>, establishing 6 confirmations as the worldwide benchmark for irreversible settlement."
            "</div></details>"
            "</section>"); // closes pane-pow
  page += F("</section>"); // closes pane-learn

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
            "<script src='/cyd-twin.js?v=" FW_GIT "'></script>"
            "<script src='/dash.js?v=" FW_GIT "'></script>");
  page += F("<script>const initStats=");
  page += buildStatsJson(d);
  page += F(";updateStats(initStats);setInterval(pollStats,3000);</script></body></html>");

  webCfgServer.send(200, "text/html", page);
}

static void handleSave()
{
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
    next.PoolAddress = webCfgServer.arg("pool");
  if (webCfgServer.hasArg("port"))
    next.PoolPort = webCfgServer.arg("port").toInt();
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
    strncpy(next.PoolPassword, webCfgServer.arg("poolpass").c_str(), sizeof(next.PoolPassword) - 1);
  if (webCfgServer.hasArg("wallet"))
    strncpy(next.BtcWallet, webCfgServer.arg("wallet").c_str(), sizeof(next.BtcWallet) - 1);
  if (webCfgServer.hasArg("tz"))
    next.Timezone = webCfgServer.arg("tz").toInt();
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
                      "<body><div class='card'><h3 style='color:#3fb950;margin-top:0'>Saved</h3><p>" + msg + "</p>"
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
                     "<body><div><div class='sp'></div><h2>Saved</h2><p style='color:#8a96a6'>Orientation changed. Restarting…</p></div>"
                     "<script>function p(){fetch('/',{cache:'no-store'}).then(()=>location.replace('/')).catch(()=>setTimeout(p,1200))};setTimeout(p,5000);</script>"
                     "</body></html>");

  saveStat(); // stats mined since the last periodic save
  delay(1000);
  ESP.restart();
}

static void handleStatus()
{
  const MinerStatus m = getMinerStatus();
  DynamicJsonDocument doc(1536);
  doc["version"] = CURRENT_VERSION;
  doc["fw"] = FW_GIT;
  doc["uptime_s"] = (uint32_t)(esp_timer_get_time() / 1000000ULL); // 64-bit clock: millis() wraps after 49.7 days
  doc["state"] = mMonitor.NerdStatus == NM_hashing ? "mining" : mMonitor.NerdStatus == NM_Connecting ? "connecting" : "starting";
  doc["hashrate_khs"] = round(m.hashrateKHs * 10.0) / 10.0;
  doc["hashrate_avg_khs"] = round(m.avgHashrateKHs * 10.0) / 10.0;
  doc["hw_loop"] = m.hwLoop ? m.hwLoop : "default";
  doc["total_mhashes"] = Mhashes + hashes / 1000000;
  doc["mining_time_s"] = (uint32_t)upTime;
  doc["templates"] = templates;
  doc["best_diff"] = getBestDiff();
  doc["shares_32bit"] = shares;                    // accepted by the pool
  doc["shares_32bit_rejected"] = shares32Rejected; // refused by the pool
  doc["blocks_found"] = valids;
  doc["shares_accepted"] = m.accepted;
  doc["shares_rejected"] = m.rejected;
  JsonObject pool = doc.createNestedObject("pool");
  pool["address"] = activePoolAddress();
  pool["port"] = activePoolPort();
  pool["backup"] = onBackupPool();
  pool["subscribed"] = m.subscribed;
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
  webCfgServer.sendHeader("Access-Control-Allow-Origin", "*");
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
  webCfgServer.sendHeader("Access-Control-Allow-Origin", "*");
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

void webConfigProcess(void)
{
  historyTick(); // also before the server is up, so the graph has the full hour
  if (webCfgStarted)
    webCfgServer.handleClient();
}

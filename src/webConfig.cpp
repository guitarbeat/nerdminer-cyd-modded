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
  json.reserve(1792);
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
  json += ",\"net_diff\":" + (g_networkDifficulty > 0.0 ? String(g_networkDifficulty, 2) : String("100000000000000.00"));
  json += ",\"using_fallback\":" + String(g_usingFallback ? "true" : "false");
  json += ",\"screen\":" + String(getDisplayScreen());
  bool isPort = (Settings.screenRotation == 0 || Settings.screenRotation == 2);
  json += ",\"is_portrait\":" + String(isPort ? "true" : "false");
  json += ",\"rotation\":" + String(Settings.screenRotation);
  json += ",\"market\":" + getMarketJson();
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

  if (scr < 0 || scr > 3)
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
--bg:#0b0d12;--card:#11151c;--card-2:#0d1016;--card-border:#1e2530;--input-bg:#0b0e13;
--text:#e6ebf2;--muted:#8a96a6;--dim:#58636f;--amber:#f7931a;--green:#3fb950;--warn:#d29922;--red:#f85149;
--mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;--r:6px;
}
*{box-sizing:border-box;margin:0;padding:0}
html{-webkit-text-size-adjust:100%}
body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;font-size:14px;padding:max(12px,env(safe-area-inset-top)) max(14px,env(safe-area-inset-right)) 40px max(14px,env(safe-area-inset-left));line-height:1.5;-webkit-font-smoothing:antialiased}
a{color:var(--text)}
code{font-family:var(--mono);font-size:.88em;background:var(--input-bg);border:1px solid var(--card-border);padding:0 4px;border-radius:3px;word-break:break-all}
.container{max-width:1160px;margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:2px 0 12px;border-bottom:1px solid var(--card-border);margin-bottom:14px}
.brand{display:flex;align-items:center;gap:10px;min-width:0}
.logo-box{width:28px;height:28px;border-radius:var(--r);background:var(--amber);display:flex;align-items:center;justify-content:center;color:#000;font-weight:800;font-size:16px;flex-shrink:0}
.brand-title{font-size:15px;font-weight:700}
.brand-subtitle{font-size:12px;color:var(--muted);font-family:var(--mono);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.status-badge{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--card-border);border-radius:var(--r);padding:3px 10px;font-size:12px;font-weight:600;flex-shrink:0}
.status-badge::before{content:'';width:7px;height:7px;border-radius:50%;background:currentColor;color:var(--muted)}
.status-badge:has(.txt-green)::before{color:var(--green)}
.status-badge:has(.txt-amber)::before{color:var(--warn)}
.txt-green{color:var(--green)}
.txt-amber{color:var(--warn)}
.txt-gray{color:var(--muted)}

.diag-strip{display:grid;grid-template-columns:repeat(2,1fr);background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);margin-bottom:16px}
@media(min-width:480px){.diag-strip{grid-template-columns:repeat(3,1fr)}}
@media(min-width:960px){.diag-strip{grid-template-columns:repeat(6,1fr)}}
.diag-item{padding:9px 14px;display:flex;flex-direction:column;justify-content:center;min-width:0}
.diag-label{display:block;font-size:12px;color:var(--muted)}
.diag-val{font-size:15px;font-weight:600;color:var(--text);font-family:var(--mono);font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#st-uptime{font-size:13px}
.diag-sub{font-size:12px;font-weight:400;color:var(--muted)}
.btn-reset-stats{align-self:flex-start;background:transparent;border:1px solid var(--card-border);border-radius:4px;color:var(--muted);padding:2px 9px;font-size:12px;cursor:pointer}
.btn-reset-stats:hover{color:var(--text);border-color:var(--dim)}

.console-layout{display:grid;grid-template-columns:310px 1fr;gap:20px;align-items:start}
@media(max-width:960px){.console-layout{grid-template-columns:1fr}}
@media(min-width:961px){.console-sidebar{position:sticky;top:14px;display:flex;flex-direction:column;gap:12px}}
.console-main{display:flex;flex-direction:column;min-width:0}

.console-tabs{display:flex;gap:2px;border-bottom:1px solid var(--card-border);margin-bottom:14px;overflow-x:auto;scrollbar-width:none}
.console-tabs::-webkit-scrollbar{display:none}
.tab-btn{padding:8px 12px;background:none;border:0;border-bottom:2px solid transparent;margin-bottom:-1px;color:var(--muted);font-size:13px;font-weight:600;cursor:pointer;white-space:nowrap}
.tab-btn:hover{color:var(--text)}
.tab-btn.active{color:var(--text);border-bottom-color:var(--amber)}
.tab-pane{display:none}
.tab-pane.active{display:flex;flex-direction:column;gap:12px}

.telemetry-bento{display:grid;grid-template-columns:repeat(auto-fit,minmax(270px,1fr));gap:12px}
.tele-card{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);padding:14px;display:flex;flex-direction:column;gap:6px}
.tele-card-hdr{display:flex;align-items:center;justify-content:space-between}
.tele-card-title{font-size:13px;font-weight:600;color:var(--muted)}
.tele-hero-val{font-size:22px;font-weight:700;color:var(--text);font-family:var(--mono);font-variant-numeric:tabular-nums;line-height:1.25}
.tele-hero-val.amber{color:var(--amber)}
.tele-hero-sub{font-size:12px;color:var(--muted);margin-bottom:4px}
.unit{font-size:13px;color:var(--muted);font-weight:500;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}
.tele-row{display:flex;justify-content:space-between;align-items:baseline;gap:10px;padding:5px 0;border-top:1px solid var(--card-border);font-size:13px;color:var(--muted)}
.hl-key{color:var(--text);font-weight:600}
.hl-val{color:var(--text);font-family:var(--mono);font-size:12.5px;font-variant-numeric:tabular-nums;text-align:right}

.pool-banner{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);padding:12px 14px;display:flex;flex-direction:column;gap:8px}
.pool-info{font-size:13px;min-width:0}
.pool-btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;border:1px solid var(--card-border);color:var(--text);text-decoration:none;font-weight:600;font-size:13px;padding:7px 12px;border-radius:var(--r)}
.pool-btn:hover{border-color:var(--dim)}

.section-hdr{font-size:13px;font-weight:600;color:var(--muted);margin:10px 0 2px}
details.edu-acc{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);overflow:hidden}
summary.edu-sum{padding:10px 14px;font-size:13px;font-weight:600;color:var(--text);cursor:pointer;list-style:none;display:flex;align-items:center;justify-content:space-between;user-select:none}
summary.edu-sum::-webkit-details-marker{display:none}
summary.edu-sum:hover{background:rgba(255,255,255,0.02)}
summary.edu-sum::after{content:'+';font-size:15px;color:var(--muted)}
details.edu-acc[open] summary.edu-sum::after{content:'\2212'}
.edu-body{padding:10px 14px 14px;border-top:1px solid var(--card-border);font-size:13px;line-height:1.6;color:#c3cbd5}
.edu-body a{color:var(--text)}

input[type=text],input[type=number],input[type=password],select{width:100%;background:var(--input-bg);border:1px solid var(--card-border);border-radius:var(--r);color:var(--text);padding:8px 11px;font-size:14px;font-family:var(--mono);outline:none;transition:border-color .15s;min-height:38px;box-sizing:border-box}
input[type=text]:focus,input[type=number]:focus,input[type=password]:focus,select:focus{border-color:var(--amber)}
input::placeholder{color:var(--dim)}
select{appearance:none;-webkit-appearance:none;cursor:pointer;padding-right:32px;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%238592a3' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center}
.cfg-card{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r)}
.cfg-card-hdr{display:flex;align-items:center;justify-content:space-between;padding:12px 14px 0;gap:10px;flex-wrap:wrap}
.cfg-card-title{font-size:14px;font-weight:600;color:var(--text)}
.cfg-badge{font-size:11px;color:var(--muted);border:1px solid var(--card-border);border-radius:4px;padding:1px 6px}
.cfg-card-body{padding:12px 14px 14px;display:flex;flex-direction:column;gap:12px}
.form-grid-3{display:grid;grid-template-columns:2fr 1fr 1fr;gap:10px}
.form-grid-2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
@media(max-width:540px){.form-grid-3,.form-grid-2{grid-template-columns:1fr}}
.form-group{display:flex;flex-direction:column;gap:4px}
.form-label{display:flex;align-items:center;justify-content:space-between;font-size:13px;font-weight:600;color:var(--text)}
.form-label-sub{font-size:12px;font-weight:400;color:var(--muted)}
.form-sublabel{display:block;font-size:12px;color:var(--muted);margin-bottom:3px}
.form-control{width:100%;background:var(--input-bg);border:1px solid var(--card-border);border-radius:var(--r);color:var(--text);padding:8px 11px;font-size:14px;font-family:var(--mono);box-sizing:border-box;min-height:38px;outline:none;transition:border-color .15s}
.form-control:focus{border-color:var(--amber)}
.form-hint{font-size:12px;color:var(--muted);line-height:1.5}
.form-check{display:flex;align-items:flex-start;gap:9px;font-size:13px;color:var(--text);cursor:pointer;user-select:none;padding:2px 0;line-height:1.45}
.form-check input[type=checkbox]{accent-color:var(--amber);width:16px;height:16px;margin-top:2px;flex-shrink:0;cursor:pointer}
.slider-track-wrap{display:flex;align-items:center;gap:14px}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:4px;background:var(--card-border);border-radius:2px;outline:none;accent-color:var(--amber)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:16px;height:16px;border-radius:50%;background:var(--amber);cursor:pointer}
input[type=range]::-moz-range-thumb{width:16px;height:16px;border-radius:50%;background:var(--amber);cursor:pointer;border:none}
.preset-bar{display:flex;flex-direction:column;gap:8px}
.preset-pills{display:flex;flex-wrap:wrap;gap:6px}
.preset-pill{background:transparent;border:1px solid var(--card-border);color:var(--text);font-size:12px;font-family:inherit;padding:5px 10px;border-radius:4px;cursor:pointer}
.preset-pill:hover{border-color:var(--dim)}
.preset-pill.active{border-color:var(--amber)}
.preset-comment{font-size:12px;line-height:1.45;color:var(--muted)}
.active-pool-pill{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:var(--muted)}
.active-pool-pill::before{content:'';width:6px;height:6px;border-radius:50%;background:var(--green)}
.active-pool-pill.fallback::before{background:var(--warn)}
.theme-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
@media(min-width:520px){.theme-grid{grid-template-columns:repeat(3,1fr)}}
.theme-card{background:var(--input-bg);border:1px solid var(--card-border);border-radius:var(--r);padding:9px 11px;cursor:pointer;user-select:none;position:relative}
.theme-card:hover{border-color:var(--dim)}
.theme-card.active{border-color:var(--amber)}
.theme-card.active::after{content:'\2713';position:absolute;top:8px;right:10px;font-size:12px;color:var(--text)}
.theme-swatch{display:flex;align-items:center;gap:4px;margin-bottom:5px}
.swatch-dot{width:12px;height:12px;border-radius:50%;display:inline-block;box-shadow:0 0 0 1px rgba(255,255,255,.15)}
.theme-card h4{font-size:13px;font-weight:600;color:var(--text)}
.theme-card p{font-size:12px;color:var(--muted);line-height:1.35}
.theme-toast{margin-top:8px;font-size:12px;color:var(--green)}
.actions{margin:16px 0 24px;display:flex;flex-direction:column;gap:8px}
.btn-save{background:var(--amber);color:#000;border:none;border-radius:var(--r);padding:11px 16px;font-size:14px;font-weight:600;cursor:pointer;width:100%}
.btn-save:hover{opacity:.92}
.btn-restart{background:transparent;border:1px solid var(--card-border);border-radius:var(--r);color:var(--muted);padding:9px;font-size:13px;cursor:pointer;width:100%}
.btn-restart:hover{color:var(--red);border-color:rgba(248,81,73,.45)}
.ota-box{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);padding:14px}
.ota-title{font-size:14px;font-weight:600;color:var(--text);margin-bottom:4px}
.ota-desc{font-size:13px;color:var(--muted);line-height:1.5;margin-bottom:10px;max-width:60ch}
.ota-row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
#ota-file{border-radius:var(--r)}
.btn-ota{background:transparent;border:1px solid var(--card-border);border-radius:var(--r);color:var(--text);padding:8px 14px;font-size:13px;font-weight:600;cursor:pointer}
.btn-ota:hover{border-color:var(--dim)}
.ota-progress-box{display:none;margin-top:12px}
.ota-progress-bar{height:4px;border-radius:2px;background:var(--input-bg);overflow:hidden}
.ota-progress-fill{height:100%;width:0%;background:var(--amber);transition:width .1s linear}
.ota-status{font-size:12px;color:var(--muted);margin-top:6px;font-variant-numeric:tabular-nums}
.guide-grid{display:grid;grid-template-columns:1fr;gap:12px}
@media(min-width:480px){.guide-grid{grid-template-columns:repeat(2,1fr)}}
.guide-card{background:var(--card);border:1px solid var(--card-border);border-radius:var(--r);padding:12px 14px;font-size:13px;color:var(--muted);line-height:1.6}
.guide-title{font-size:14px;font-weight:600;color:var(--text);margin-bottom:4px}
.guide-row{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:5px 0;border-top:1px solid var(--card-border);font-size:13px}
.guide-key{color:var(--text)}
.guide-val{color:var(--muted);text-align:right}
#overlay{display:none;position:fixed;inset:0;background:rgba(11,13,18,0.96);z-index:999;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:20px}
#overlay p{color:var(--muted);font-size:13px}
.spinner{width:32px;height:32px;border:3px solid var(--card-border);border-top-color:var(--amber);border-radius:50%;animation:spin 1s linear infinite;margin-bottom:14px}
@keyframes spin{to{transform:rotate(360deg)}}
button:focus-visible,a:focus-visible,.theme-card:focus-visible,summary:focus-visible{outline:2px solid var(--amber);outline-offset:2px}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
)rawcss";

static const char DASH_JS[] PROGMEM = R"rawjs(
function switchTab(tId){
document.querySelectorAll('.tab-btn').forEach(b=>b.classList.toggle('active',b.id==='tbtn-'+tId));
document.querySelectorAll('.tab-pane').forEach(p=>p.classList.toggle('active',p.id==='pane-'+tId));
try{localStorage.setItem('cyd_tab',tId);}catch(e){}
}
function choosePreset(h,p,pass,note){document.getElementById('f-pool').value=h;document.getElementById('f-port').value=p;document.getElementById('f-pass').value=pass;const n=document.getElementById('preset-note');if(n)n.textContent=note;if(window.event&&window.event.target){document.querySelectorAll('.preset-pill').forEach(b=>b.classList.remove('active'));window.event.target.classList.add('active');}}
function setPool(h,p,pass){choosePreset(h,p,pass,'Switched pool to '+h+':'+p);}
function updateBright(v){document.getElementById('b-val').textContent=v+' ('+Math.round(v*100/255)+'%)';}
function applyTz(v){if(v!=='')document.getElementById('f-tz').value=v;}
function selectTheme(id,col){
document.getElementById('f-theme').value=id;
document.querySelectorAll('.theme-card').forEach(c=>c.classList.remove('active'));
const el=document.getElementById('thm-'+id);
if(el)el.classList.add('active');
if(col){document.documentElement.style.setProperty('--amber',col);}
fetch('/api/theme?id='+id,{method:'POST'}).then(r=>r.json()).then(d=>{
const t=document.getElementById('thm-toast');
if(t){t.textContent='Theme applied';t.style.display='block';setTimeout(()=>{t.style.display='none';},2500);}
}).catch(e=>{});}
function toggleInvert(inv){
fetch('/api/invert?val='+(inv?'1':'0'),{method:'POST'}).then(r=>r.json()).then(d=>{
const t=document.getElementById('thm-toast');
if(t){t.textContent='Inversion updated';t.style.display='block';setTimeout(()=>{t.style.display='none';},2500);}
}).catch(e=>{});}
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
const r=document.querySelector("select[name='rot']");
const initR=this.dataset.initRot;
if(r&&r.value!==initR){
setTimeout(pollReboot,100);
}
});
function rebootDevice(){if(confirm('Reboot the miner now?')){fetch('/restart',{method:'POST'}).then(pollReboot).catch(pollReboot);}}
function uploadOta(){
const fi=document.getElementById('ota-file');
if(!fi||!fi.files||!fi.files.length){alert('Please select a firmware .bin file first');return;}
const f=fi.files[0];
if(!f.name.endsWith('.bin')){if(!confirm('File does not end in .bin. Are you sure this is a valid firmware file?'))return;}
const pBox=document.getElementById('ota-progress-box'),pFill=document.getElementById('ota-progress-fill'),pStat=document.getElementById('ota-status'),btn=document.getElementById('btn-ota-start');
if(pBox)pBox.style.display='block';
if(btn)btn.disabled=true;
if(pStat)pStat.textContent='Uploading '+f.name+' ('+Math.round(f.size/1024)+' KB)...';
const fd=new FormData();
fd.append('firmware',f,f.name);
const xhr=new XMLHttpRequest();
xhr.open('POST','/update',true);
xhr.upload.onprogress=function(e){if(e.lengthComputable&&pFill){const pct=Math.round((e.loaded/e.total)*100);pFill.style.width=pct+'%';if(pStat)pStat.textContent='Flashing: '+pct+'% ('+Math.round(e.loaded/1024)+' / '+Math.round(e.total/1024)+' KB)';}};
xhr.onload=function(){
if(xhr.status===200){
if(pStat)pStat.textContent='Update successful! Rebooting miner...';
setTimeout(pollReboot,500);
}else{
if(btn)btn.disabled=false;
if(pStat)pStat.textContent='Update failed: '+(xhr.responseText||'HTTP '+xhr.status);
alert('Firmware update failed: '+(xhr.responseText||'Unknown error'));
}
};
xhr.onerror=function(){
if(btn)btn.disabled=false;
if(pStat)pStat.textContent='Upload error / connection lost';
alert('Network error during upload');
};
xhr.send(fd);
}
function fmtNum(n){if(isNaN(n)||n<=0)return'0';if(n>=1e12)return(n/1e12).toFixed(2)+' trillion';if(n>=1e9)return(n/1e9).toFixed(1)+' billion';if(n>=1e6)return(n/1e6).toFixed(1)+' million';return Math.round(n).toLocaleString();}
function fmtUptime(u){if(!u)return'--';const p=u.trim().split(/\s+/);let d=0,t=u;if(p.length===2){d=parseInt(p[0])||0;t=p[1];}const c=t.split(':');if(c.length===3){const hrs=parseInt(c[0])||0,mins=parseInt(c[1])||0;return(d>0?d+'d ':'')+(hrs>0?hrs+'h ':'')+mins+'m';}return u;}
function resetSession(){
if(confirm('Reset current session shares and session best difficulty? (Lifetime total hashes in flash remain preserved)')){
fetch('/api/reset-session',{method:'POST'}).then(r=>r.json()).then(()=>{pollStats();}).catch(()=>{});}}
function updateStats(d){
const hr=parseFloat(d.hashrate)||0,hps=Math.round(hr*1000),hpd=hr*1000*86400;
const eTopHr=document.getElementById('st-top-hr');if(eTopHr&&d.hashrate)eTopHr.textContent=d.hashrate;
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
const netDiff=(d.net_diff&&d.net_diff>0)?Number(d.net_diff):100e12;
const eNetdiff=document.getElementById('dyn-netdiff');if(eNetdiff)eNetdiff.textContent=fmtNum(netDiff);
const eNetpct=document.getElementById('dyn-netpct');if(eNetpct){if(diff>0){const pct=(diff/netDiff)*100;eNetpct.textContent=pct.toExponential(2)+'% (~'+pct.toFixed(8)+'%)';}else eNetpct.textContent='0%';}
const targetHashes=netDiff*4294967296,dayOdds=hpd>0?(targetHashes/hpd):0,yrOdds=dayOdds/365.25;
const eDayOdds=document.getElementById('dyn-dayodds');if(eDayOdds)eDayOdds.textContent=dayOdds>0?('1 in '+fmtNum(dayOdds)):'--';
const eYrOdds=document.getElementById('dyn-yrodds');if(eYrOdds)eYrOdds.textContent=yrOdds>0?('1 in '+fmtNum(yrOdds)):'--';
const eAvgWait=document.getElementById('dyn-avgwait');if(eAvgWait)eAvgWait.textContent=yrOdds>0?(fmtNum(yrOdds)+' years'):'--';
const netHps=700e18,eNetpow=document.getElementById('dyn-netpow');if(eNetpow){const netSharePct=(hps/netHps)*100;eNetpow.textContent=netSharePct.toExponential(1)+'%';}
if(d.temp)document.getElementById('st-temp').textContent=d.temp;
if(d.uptime){const u=document.getElementById('st-uptime');if(u){const m=d.uptime.match(/^(\d+)\s+(\d{2}:\d{2}:\d{2})$/);u.textContent=m?(parseInt(m[1])>0?m[1]+'d '+m[2]:m[2]):d.uptime;}}
const ePing=document.getElementById('st-ping');if(ePing&&d.ping!==undefined)ePing.textContent=d.ping;
if(d.free_heap)document.getElementById('st-heap').textContent=Math.round(d.free_heap/1024)+' KB';
if(d.rssi!==undefined){const q=Math.min(Math.max(2*(d.rssi+100),0),100);const w=document.getElementById('st-wifi'),r=document.getElementById('st-rssi');if(w)w.textContent=q+'%';if(r)r.textContent=d.rssi;}
const txt=document.getElementById('st-txt');if(txt){
if(d.status==='hashing'){txt.className='txt-green';txt.textContent=d.using_fallback?'Hashing (fallback)':'Hashing';}
else if(d.status==='connecting'){txt.className='txt-amber';txt.textContent='Connecting';}
else{txt.className='txt-gray';txt.textContent='Idle';}}
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
try{const lastT=localStorage.getItem('cyd_tab');if(lastT&&document.getElementById('tbtn-'+lastT)){switchTab(lastT);}}catch(e){}
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
            "<title>NerdMiner</title>"
            "<link rel='stylesheet' href='/dash.css?v=" FW_GIT "'>"
            "<link rel='stylesheet' href='/cyd-twin.css?v=" FW_GIT "'></head><body>"
            "<div class='container'>"
            "<header>"
            "<div class='brand'>"
            "<div class='logo-box' id='site-logo'>&#8383;</div>"
            "<div><div class='brand-title'>NerdMiner CYD</div><div class='brand-subtitle'>v1.6.3 (" FW_GIT ") &bull; ");

  page += htmlEscape(WiFi.localIP().toString()) + F(" &bull; ") + htmlEscape(WiFi.SSID()) + F("</div></div></div>"
            "<div class='status-badge'>"
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
            "<div class='diag-item'><span class='diag-label'>Hashrate</span><span class='diag-val' style='color:var(--amber);'><span id='st-top-hr'>");
  page += d.currentHashRate.length() ? d.currentHashRate : String("--");
  page += F("</span> <span class='diag-sub'>KH/s</span></span></div>"
            "<div class='diag-item'><span class='diag-label'>Temp</span><span class='diag-val'><span id='st-temp'>");
  page += d.temp.length() ? d.temp : String("--");
  page += F("</span>&deg;C</span></div>"
            "<div class='diag-item'><span class='diag-label'>Ping</span><span class='diag-val'><span id='st-ping'>");
  page += String(g_stratumPing) + F("</span> ms</span></div>"
            "<div class='diag-item'><span class='diag-label'>Wi-Fi</span><span class='diag-val'><span id='st-wifi'>");
  page += String(getWifiQuality(WiFi.RSSI())) + F("%</span> <span class='diag-sub'>(<span id='st-rssi'>");
  page += String(WiFi.RSSI()) + F("</span> dBm)</span></span></div>"
            "<div class='diag-item'><span class='diag-label'>Free heap</span><span class='diag-val' id='st-heap'>");
  page += String(ESP.getFreeHeap() / 1024) + F(" KB</span></div>"
            "<div class='diag-item'><span class='diag-label'>Session</span><button type='button' class='btn-reset-stats' onclick='resetSession()'>Reset</button></div>"
            "</div>"
            "<div class='console-layout'>"
            "<aside class='console-sidebar'>");
  appendCydTwinHtml(page);
  if (isPublicPool && cleanWallet.length() > 0)
  {
    page += F("<div class='pool-banner'>"
              "<div class='pool-info'>"
              "<strong>Mining on Public-Pool.io</strong><br>"
              "<span style='color:var(--muted)'>Worker: ");
    page += (worker.length() ? htmlEscape(worker) : F("(default)")) + F("</span></div>"
              "<a href='https://web.public-pool.io/#/app/");
    page += htmlEscape(cleanWallet) + F("' target='_blank' rel='noopener' class='pool-btn'>"
              "View worker on public-pool.io &#8599;</a></div>");
  }
  page += F("</aside>"
            "<main class='console-main'>"
            "<nav class='console-tabs' role='tablist'>"
            "<button type='button' class='tab-btn active' id='tbtn-telemetry' onclick=\"switchTab('telemetry')\">Overview</button>"
            "<button type='button' class='tab-btn' id='tbtn-stratum' onclick=\"switchTab('stratum')\">Pool &amp; wallet</button>"
            "<button type='button' class='tab-btn' id='tbtn-display' onclick=\"switchTab('display')\">Display</button>"
            "<button type='button' class='tab-btn' id='tbtn-pow' onclick=\"switchTab('pow')\">Proof of work</button>"
            "<button type='button' class='tab-btn' id='tbtn-system' onclick=\"switchTab('system')\">System</button>"
            "</nav>");

  page += "<form method='POST' action='/save' id='cfg-form' data-init-rot='" + String(Settings.screenRotation) + "'>";

  // Tab 1: Live Telemetry Bento
  page += F("<section class='tab-pane active' id='pane-telemetry'>"
            "<div class='telemetry-bento'>"
            "<div class='tele-card'>"
            "<div class='tele-card-hdr'>"
            "<div class='tele-card-title'>Hashrate</div>"
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
            "<div class='tele-row'><span>Uptime</span><span class='hl-val' id='dyn-uptime'>");
  page += (d.timeMining.length() ? d.timeMining : F("--"));
  page += F("</span></div>"
            "</div>"
            "<div class='tele-card'>"
            "<div class='tele-card-hdr'>"
            "<div class='tele-card-title'>Shares</div>"
            "</div>"
            "<div class='tele-hero-val'><span id='dyn-shares'>");
  page += (d.completedShares.length() ? d.completedShares : F("0"));
  page += F("</span> <span class='unit'>shares</span></div>"
            "<div class='tele-hero-sub'>About 1 per <span id='dyn-hpshare'>--</span> (<span id='dyn-tpshare'>--</span>)</div>"
            "<div class='tele-row'><span>Best difficulty (session)</span><span class='hl-val'><span id='dyn-diff'>");
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
            "<div class='tele-row'><span>Best vs. network</span><span class='hl-val'><span id='dyn-netpct'>--</span> of <span id='dyn-netdiff'>100T</span></span></div>"
            "<div class='tele-row'><span>Daily odds</span><span class='hl-val'><span id='dyn-dayodds'>--</span> / day</span></div>"
            "<div class='tele-row'><span>Yearly odds</span><span class='hl-val'><span id='dyn-yrodds'>--</span> / yr (avg <span id='dyn-avgwait'>--</span>)</span></div>"
            "</div>"
            "<div class='tele-card'>"
            "<div class='tele-card-hdr'>"
            "<div class='tele-card-title'>Efficiency</div>"
            "</div>"
            "<div class='tele-hero-val'>~1.0 MH/s <span class='unit'>at ~1.2 W</span></div>"
            "<div class='tele-hero-sub'>Under 1 kWh per month</div>"
            "<div class='tele-row'><span>Share of network</span><span class='hl-val'><span id='dyn-netpow'>--</span> of 700 EH/s</span></div>"
            "<div class='tele-row'><span>Chip</span><span class='hl-val'>ESP32 LX6, 240 MHz, HW SHA</span></div>"
            "</div>"
            "</div>"
            "</section>");

  // Tab 2: Stratum Pool & Wallet
  page += F("<section class='tab-pane' id='pane-stratum'>");

  // Card 1: Solo Pool & Stratum Connection
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Pools</div>"
            "</div>"
            "<div class='cfg-card-body'>"
            "<div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
            "<span class='form-hint'>Stratum V1. Switches to the fallback after 3 failed connections.</span>"
            "<span class='active-pool-pill");
  page += (onBackupPool() ? F(" fallback'>Using fallback") : F("'>Using primary"));
  page += F("</span>"
            "</div>"
            "<div class='form-group'>"
            "<span class='form-label'>Primary</span>"
            "<div class='form-grid-3'>"
            "<div>"
            "<label class='form-sublabel' for='f-pool'>Host</label>"
            "<input type='text' id='f-pool' class='form-control' name='pool' value='");
  page += htmlEscape(Settings.PoolAddress);
  page += F("' placeholder='public-pool.io' required>"
            "</div>"
            "<div>"
            "<label class='form-sublabel' for='f-port'>Port</label>"
            "<input type='number' id='f-port' class='form-control' name='port' value='");
  page += String(Settings.PoolPort);
  page += F("' placeholder='3333' required>"
            "</div>"
            "<div>"
            "<label class='form-sublabel' for='f-pass'>Password</label>"
            "<input type='text' id='f-pass' class='form-control' name='poolpass' value='");
  page += htmlEscape(String(Settings.PoolPassword));
  page += F("' placeholder='x'>"
            "</div>"
            "</div>"
            "</div>"
            "<div class='form-group'>"
            "<span class='form-label'>Fallback <span class='form-label-sub'>optional</span></span>"
            "<div class='form-grid-3'>"
            "<div>"
            "<label class='form-sublabel' for='f-fbpool'>Host</label>"
            "<input type='text' id='f-fbpool' class='form-control' name='fbpool' value='");
  page += htmlEscape(Settings.FallbackPoolAddress);
  page += F("' placeholder='pool.nerdminers.org'>"
            "</div>"
            "<div>"
            "<label class='form-sublabel' for='f-fbport'>Port</label>"
            "<input type='number' id='f-fbport' class='form-control' name='fbport' value='");
  page += String(Settings.FallbackPoolPort);
  page += F("' placeholder='3333'>"
            "</div>"
            "<div>"
            "<label class='form-sublabel' for='f-fbpass'>Password</label>"
            "<input type='text' id='f-fbpass' class='form-control' name='fbpass' value='");
  page += htmlEscape(String(Settings.FallbackPoolPassword));
  page += F("' placeholder='x'>"
            "</div>"
            "</div>"
            "</div>"
            "<div class='preset-bar'>"
            "<span class='form-label'>Presets</span>"
            "<div class='preset-pills'>"
            "<button type='button' class='preset-pill active' onclick='choosePreset(\"public-pool.io\",3333,\"x\",\"public-pool.io:3333 is the recommended port.\")'>public-pool.io:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"pool.nerdminers.org\",3333,\"x\",\"pool.nerdminers.org is a solo pool for low-hashrate miners.\")'>pool.nerdminers.org:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"pool.solomining.de\",3333,\"x\",\"pool.solomining.de is a European solo pool.\")'>pool.solomining.de:3333</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"public-pool.io\",21496,\"x\",\"Legacy port. Use 3333 if connections drop.\")'>public-pool.io:21496 (legacy)</button>"
            "</div>"
            "<div id='preset-note' class='preset-comment'>Presets fill in the primary pool. Save to apply.</div>"
            "</div>"
            "</div></div>");

  // Card 2: Payout Address & Worker Identity
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Payout address</div>"
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
            "</div></div>"
            "</section>"
            "<section class='tab-pane' id='pane-display'>");

  // Card 3: Display, Orientation & Illumination
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
            "<div class='slider-track-wrap' style='min-height:42px;'>"
            "<input type='range' name='brightness' id='b-slider' min='10' max='255' value='");
  page += String(Settings.Brightness) + F("' oninput='updateBright(this.value)'>"
            "</div>"
            "<label class='form-check'>"
            "<input type='checkbox' name='invert' id='cb-invert' onchange='toggleInvert(this.checked)'");
  if (Settings.invertColors) page += F(" checked");
  page += F("> <span>Invert colors</span></label>"
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
            "<div id='thm-toast' class='theme-toast' style='display:none'>Theme applied</div>"
            "</div>");
#ifdef HAS_BACKLIGHT_CONTROL
  page += F("<div style='border-top:1px solid var(--card-border);padding-top:14px;display:flex;flex-direction:column;gap:12px;'>"
            "<div class='form-label'>Sleep and night dimming</div>"
            "<div class='form-grid-3'>"
            "<div>"
            "<label class='form-sublabel' for='f-screenoff'>Sleep after (min)</label>"
            "<input type='number' id='f-screenoff' class='form-control' name='screenoff' min='0' max='1440' value='");
  page += String(Settings.ScreenTimeout);
  page += F("' placeholder='0'>"
            "<span class='form-hint'>0 = never. Touch or BOOT wakes it.</span>"
            "</div>"
            "<div>"
            "<label class='form-sublabel' for='f-nbright'>Night brightness (1–255)</label>"
            "<input type='number' id='f-nbright' class='form-control' name='nightbright' min='1' max='255' value='");
  page += String(Settings.NightBrightness);
  page += F("'>"
            "</div>"
            "<div>"
            "<label class='form-sublabel' for='f-nfade'>Fade (min)</label>"
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
            "<div>"
            "<label class='form-sublabel'>City</label>"
            "<select onchange='pickCity(this.value)' class='form-control'>"
            "<option value=''>Pick nearest city…</option>");
  for (unsigned i = 0; i < sizeof(kCities) / sizeof(kCities[0]); i++)
  {
    page += "<option value='" + String(kCities[i].latitude, 2) + "," + String(kCities[i].longitude, 2) + "'>" + String(kCities[i].name) + "</option>";
  }
  page += F("</select></div>"
            "<div>"
            "<label class='form-sublabel' for='f-lat'>Latitude (&deg;)</label>"
            "<input type='text' id='f-lat' name='lat' class='form-control' value='");
  page += String(Settings.Latitude, 2);
  page += F("'></div>"
            "<div>"
            "<label class='form-sublabel' for='f-lon'>Longitude (&deg;)</label>"
            "<input type='text' id='f-lon' name='lon' class='form-control' value='");
  page += String(Settings.Longitude, 2);
  page += F("'></div>"
            "</div>"
            "<span class='form-hint'>Status: ");
  page += nightDimStatus();
  page += F("</span></div>");
#endif
  page += F("</div></div>");

#ifdef HAS_RGB_LED
  // Card 5: Hardware RGB Status LED
  page += F("<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Rear RGB LED</div>"
            "</div>"
            "<div class='cfg-card-body'>"
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
            "<input type='color' id='f-ledcolor' name='ledcolor' class='form-control' style='height:42px;padding:4px;cursor:pointer;' value='");
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
            "</div></div>");
#endif

  page += F("</section>"); // closes pane-display

  // Tab 4: PoW Workshop & Architecture
  page += F("<section class='tab-pane' id='pane-pow'>"
            "<div class='cfg-card'>"
            "<div class='cfg-card-hdr'>"
            "<div class='cfg-card-title'>Proof-of-work simulator</div>"
            "</div>"
            "<div class='cfg-card-body'>"
            "<p style='font-size:13px;color:var(--muted);margin-bottom:8px;'>"
            "The 80-byte header your miner hashes about a million times a second. Step through a real block, or try a sandbox target."
            "</p>"
            "<div style='display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap;'>"
            "<button type='button' class='preset-pill active' id='sim-mode-hist' onclick='simSetMode(true)'>Block #286,819</button>"
            "<button type='button' class='preset-pill' id='sim-mode-rand' onclick='simSetMode(false)'>Sandbox</button>"
            "</div>"
            "<div id='sim-shirriff-box' style='display:flex;align-items:center;gap:6px;flex-wrap:wrap;margin-bottom:10px;'>"
            "<span style='font-size:11px;color:var(--muted);'>Jump to nonce:</span>"
            "<button type='button' class='preset-pill' onclick='simJump(0)' style='font-size:11px;padding:3px 8px;'>0 (0 zeros)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(39)' style='font-size:11px;padding:3px 8px;'>39 (1 zero)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(990)' style='font-size:11px;padding:3px 8px;'>990 (3 zeros)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(52117)' style='font-size:11px;padding:3px 8px;'>52,117 (4 zeros)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(856192320)' style='font-size:11px;padding:3px 8px;'>856,192,320 (Approach)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(856192328)' style='font-size:11px;padding:3px 8px;'>856,192,328 (Solved Block)</button>"
            "</div>"
            "<div style='background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:12px;margin-bottom:12px;font-family:monospace;font-size:12px;'>"
            "<div style='color:var(--muted);margin-bottom:6px;'><strong id='sim-hdr-title'>Block header #286,819 (80 bytes)</strong></div>"
            "<div style='background:rgba(0,0,0,0.3);border:1px solid var(--card-border);border-radius:4px;padding:8px;font-size:11px;color:var(--muted);margin-bottom:8px;word-break:break-all;'>"
            "<span style='color:#79c0ff' id='sim-f-ver'>[Ver 4B: 02000000]</span> "
            "<span style='color:#7ee787' id='sim-f-prev'>[Prev 32B: 00000000...9717]</span> "
            "<span style='color:#d2a8ff' id='sim-f-mrkl'>[Merkle 32B: 871714...978a]</span> "
            "<span style='color:#ffa657' id='sim-f-time'>[Time 4B: 53059335]</span> "
            "<span style='color:#ff7b72' id='sim-f-bits'>[Bits 4B: 19015f53]</span> "
            "<span style='color:var(--amber);font-weight:700;'>[Nonce 4B: <span id='sim-nonce-hex'>40750833</span>]</span>"
            "</div>"
            "<div style='display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin:6px 0;'>"
            "<div>Nonce <span id='sim-nonce' style='font-size:14px;font-weight:700;'>856,192,320 (0x33087540)</span></div>"
            "<div id='sim-zeros-box' style='display:none;align-items:center;gap:6px;'>"
            "<label style='font-size:11px;color:var(--muted);margin:0;'>Leading zeros</label>"
            "<select id='sim-diff' style='padding:2px 6px;font-size:11px;width:auto;' onchange='simUpdateTarget()'>"
            "<option value='3'>3</option>"
            "<option value='4' selected>4</option>"
            "<option value='5'>5</option>"
            "<option value='6'>6</option>"
            "</select>"
            "</div></div>"
            "<div style='margin-top:6px;word-break:break-all;'>"
            "<div style='color:var(--muted);font-size:11px;'>Target</div>"
            "<div id='sim-target' style='font-size:11px;font-weight:600;margin-top:2px;'>00000000000000015f5300000000000000000000000000000000000000000000</div>"
            "<div id='sim-diff-formula' style='font-size:11px;color:var(--muted);margin-top:2px;'>Difficulty = Target<sub>1</sub> / Target = <strong>3,129,573,174.52</strong> (Expected: ~1.34 &times; 10<sup>19</sup> hashes)</div>"
            "</div>"
            "<div style='margin-top:6px;word-break:break-all;'>"
            "<div style='color:var(--muted);font-size:11px;'>Hash (SHA256d, byte-reversed)</div>"
            "<div id='sim-hash' style='font-size:12px;font-weight:700;color:var(--muted);margin-top:2px;'>Press \"Next nonce\" to hash</div>"
            "</div>"
            "<div id='sim-result' style='margin-top:8px;font-size:12px;font-weight:700;color:var(--muted);'>Ready. Solves at nonce 856,192,328.</div>"
            "</div>"
            "<div style='display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px;'>"
            "<button type='button' class='preset-pill active' onclick='simStep()' style='padding:6px 14px;font-size:12px;'>Next nonce</button>"
            "<button type='button' class='preset-pill' id='sim-auto-btn' onclick='simToggleAuto()' style='padding:6px 14px;font-size:12px;'>Auto (10 H/s)</button>"
            "<button type='button' class='preset-pill' onclick='simReset()' style='padding:6px 10px;font-size:12px;'>Reset</button>"
            "</div>"
            "<div style='border-top:1px solid var(--card-border);padding-top:12px;'>"
            "<p style='font-size:13px;font-weight:600;color:var(--text);margin-bottom:8px;'>Unit converter</p>"
            "<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin-bottom:8px;'>"
            "<div style='background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:6px 8px;'>"
            "<label style='font-size:10px;color:var(--muted);'>Bitcoin (BTC)</label>"
            "<input type='text' id='calc-btc' inputmode='decimal' autocomplete='off' value='1.0' oninput='calcFromBtc(this.value)' style='padding:4px;font-size:12px;'>"
            "</div>"
            "<div style='background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:6px 8px;'>"
            "<label style='font-size:10px;color:var(--muted);'>Satoshis (sats)</label>"
            "<input type='text' id='calc-sats' inputmode='numeric' autocomplete='off' value='100,000,000' oninput='calcFromSats(this.value)' style='padding:4px;font-size:12px;'>"
            "</div>"
            "<div style='background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:6px 8px;'>"
            "<label style='font-size:10px;color:var(--muted);'>Bits (&mu;BTC)</label>"
            "<input type='text' id='calc-bits' inputmode='numeric' autocomplete='off' value='1,000,000' oninput='calcFromBits(this.value)' style='padding:4px;font-size:12px;'>"
            "</div>"
            "<div style='background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:6px 8px;'>"
            "<label style='font-size:10px;color:var(--muted);'>Millibitcoins (mBTC)</label>"
            "<input type='text' id='calc-mbtc' inputmode='decimal' autocomplete='off' value='1,000' oninput='calcFromMbtc(this.value)' style='padding:4px;font-size:12px;'>"
            "</div>"
            "</div>"
            "<div style='font-size:11px;color:var(--muted);line-height:1.4;'>"
            "1 BTC = 100M Satoshis (<code>1 sat = 10<sup>-8</sup> BTC</code>). Bitcoin Core stores all balances as 64-bit integer sats. Integer truncation of 33 subsidy halvings yields exactly <strong>20,999,999.9769 BTC</strong>."
            "</div></div>"
            "</div></div>"
            "<div class='section-hdr'>Background</div>"
            "<details class='edu-acc' name='cyd-faq' open>"
            "<summary class='edu-sum'><span>1. Cryptographic Foundations &amp; The 80-Byte Candidate</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>The Avalanche Effect:</strong> SHA-256 compresses arbitrary data into a deterministic 256-bit (32-byte) digest. Altering a single bit scrambles the output completely. Because preimage recovery is computationally intractable, finding candidate blocks requires exhaustive brute-force search.<br>"
            "&bull; <strong>Double-SHA256 (<code>SHA256d</code>):</strong> Bitcoin executes two sequential SHA-256 rounds (<code>SHA256(SHA256(header))</code>) to prevent length-extension attacks on Merkel-Damg&aring;rd hash constructions.<br>"
            "&bull; <strong>Target Unpacking:</strong> Mining requires <code>hash &lt; target</code>. The 4-byte <code>Bits</code> field unpacks via <code>Target = Mantissa &times; 256<sup>(Exponent - 3)</sup></code>. Base Target<sub>1</sub> (<code>0x1d00ffff</code>) requires an expected <strong>2<sup>32</sup> &approx; 4,294,967,296 hashes</strong> per difficulty unit."
            "</div></details>"
            "<details class='edu-acc' name='cyd-faq'>"
            "<summary class='edu-sum'><span>2. Silicon Limits &amp; The 1.05 MH/s Hardware Ceiling</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>The No-Midstate Hardware Constraint:</strong> An 80-byte block header spans two 64-byte SHA-256 blocks, plus a third block for the second hash pass. Unlike modern ASICs or ESP32-S3, the classic ESP32 (D0WD) cannot preload midstates: every nonce must grind all three 64-byte blocks from the standard IV constants ($H_0 \\dots H_7$).<br>"
            "&bull; <strong>228-Cycle Gate Delay:</strong> Three blocks at 64 rounds each plus engine handshakes require <strong>&sim;228 CPU cycles</strong> of hardware gate execution. At 240 MHz, the theoretical silicon speed ceiling is <code>240M / 228 &approx; 1.05 MH/s</code>.<br>"
            "&bull; <strong>247-Cycle Pipelined Assembly:</strong> The tuned assembly mining loop (<code>pipelined_hw_mine_classic_v2.S</code>) executes in 247 cycles per nonce&mdash;achieving <strong>92.3% hardware engine saturation</strong> (yielding <strong>~971.6 KH/s</strong> HW from Core 1). Core 0 contributes <strong>~25 KH/s</strong> in parallel software math for a verified <strong>~996–1,002 KH/s</strong> sustained throughput (Pham et al., 2026, <a href='https://arxiv.org/abs/2602.04415' target='_blank' style='color:var(--text);'>arXiv:2602.04415</a>; Pandya, 2024, <a href='https://arxiv.org/abs/2407.08284' target='_blank' style='color:var(--text);'>arXiv:2407.08284</a>)."
            "</div></details>"
            "<details class='edu-acc' name='cyd-faq'>"
            "<summary class='edu-sum'><span>3. Multi-Core Bus Contention &amp; Hardware Isolation</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>The 3-Cycle DPORT Bus Glitch:</strong> On dual-core ESP32, both cores share internal DPORT bus access to cryptographic peripherals. If Core 1 writes to the SHA registers with &lt;3 cycles between writes while Core 0 accesses peripheral memory, the internal bus arbiter silently drops the register write.<br>"
            "&bull; <strong>The Vanity Counter Trap:</strong> When writes are dropped, hardware hashes stale or corrupted data. A naive miner increments its loop counter and claims high speed, but produces zero valid shares.<br>"
            "&bull; <strong>Strict Core Decoupling:</strong> This firmware completely isolates peripherals: Core 1 monopolizes the hardware SHA engine with calibrated cycle-spacing instructions, while Core 0 runs purely in software math (<code>nerd_sha256d_baked</code>) for UI and worker tasks, preventing bus collisions entirely (Banicescu et al., 2024, <a href='https://arxiv.org/abs/2403.03876' target='_blank' style='color:var(--text);'>arXiv:2403.03876</a>; Ayyagari, 2019, <a href='https://arxiv.org/abs/1906.00834' target='_blank' style='color:var(--text);'>arXiv:1906.00834</a>)."
            "</div></details>"
            "<details class='edu-acc' name='cyd-faq'>"
            "<summary class='edu-sum'><span>4. Verifiable PoW, 21M Economics &amp; Satoshi's Whitepaper</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>Zero Phantom Counters:</strong> Every candidate block found by hardware is re-verified bit-for-bit in software before pool submission. The firmware self-tests via Known-Answer Test (KAT) at boot and automatically demotes the engine via <code>pl_health</code> if any bit flips, guaranteeing that every reported hash represents genuine thermodynamic Proof-of-Work (Angulo de Lafuente et al., 2026, <a href='https://arxiv.org/abs/2601.12032' target='_blank' style='color:var(--text);'>arXiv:2601.12032</a>).<br>"
            "&bull; <strong>Fixed 21M Supply:</strong> Fixed issuance cap of <strong>20,999,999.9769 BTC</strong>. Subsidy halves every 210,000 blocks via bitwise shift (<code>&gt;&gt; 1</code>), reaching zero at block 6,929,999 around <strong>Year 2140</strong>.<br>"
            "&bull; <strong>Satoshi's Gambler's Ruin Finality:</strong> In Section 11 of the 2008 Whitepaper, Satoshi modeled double-spend races against a 10% hash attacker. After <strong>6 confirmations (~1 hour)</strong>, catch-up probability drops exponentially to <strong>P = 0.0002428 (&lt;0.024%)</strong>, establishing 6 confirmations as the worldwide benchmark for irreversible settlement."
            "</div></details>"
            "</section>"); // closes pane-pow

  // Tab 5: System & OTA
  page += F("<section class='tab-pane' id='pane-system'>");

  // Card 4: System, Clock & Data Feeds
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
            "<div class='ota-box'>"
            "<div class='ota-title'>Firmware update</div>"
            "<div class='ota-desc'>Upload a <code>firmware.bin</code> over Wi-Fi. The miner flashes it and reboots.</div>"
            "<div class='ota-row'>"
            "<input type='file' id='ota-file' accept='.bin' style='flex:1;min-width:200px;font-size:13px;color:var(--text);background:var(--input-bg);padding:9px;border:1px solid var(--card-border);border-radius:var(--r);'>"
            "<button type='button' class='btn-ota' id='btn-ota-start' onclick='uploadOta()'>Upload and flash</button>"
            "</div>"
            "<div class='ota-progress-box' id='ota-progress-box'>"
            "<div class='ota-progress-bar'><div class='ota-progress-fill' id='ota-progress-fill'></div></div>"
            "<div class='ota-status' id='ota-status'>Ready</div>"
            "</div>"
            "</div>"
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
            "</section>"); // closes pane-system

  page += F("<div class='actions'>"
            "<button type='submit' class='btn-save'>Save settings</button>"
            "<button type='button' class='btn-restart' onclick='rebootDevice()'>Reboot without saving</button>"
            "</div>"
            "</form>"
            "</main>"
            "</div>"  // closes console-layout
            "</div>"  // closes container
            "<div id='overlay'>"
            "<div class='spinner'></div>"
            "<h3 style='margin-bottom:6px'>Restarting</h3>"
            "<p>Reconnecting in <span id='cd'>8</span> s</p>"
            "</div>"
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
                      "<style>body{background:#0b0e14;color:#f0f6fc;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}"
                      ".card{background:#161b22;padding:24px;border-radius:8px;border:1px solid #30363d;max-width:360px}</style></head>"
                      "<body><div class='card'><h3 style='color:#3fb950;margin-top:0'>Saved</h3><p>" + msg + "</p>"
                      "<p><a href='/' style='color:#e6ebf2'>Back to dashboard</a></p></div></body></html>");
    return;
  }

  webCfgServer.send(200, "text/html",
                     "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                     "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                     "<title>Restarting...</title>"
                     "<style>body{background:#0b0e14;color:#f0f6fc;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}"
                     ".sp{width:40px;height:40px;border:3px solid #30363d;border-top-color:#f7931a;border-radius:50%;animation:s 1s linear infinite;margin:0 auto 16px}"
                     "@keyframes s{to{transform:rotate(360deg)}}</style></head>"
                     "<body><div><div class='sp'></div><h2>Saved</h2><p style='color:#8b949e'>Orientation changed. Restarting…</p></div>"
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

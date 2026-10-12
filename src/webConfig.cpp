#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "webConfig.h"
#include "wManager.h"
#include "drivers/storage/storage.h"
#include "currency.h"
#include "monitor.h"
#include "drivers/displays/display.h"
#include "mining.h"

extern TSettings Settings;
extern monitor_data mMonitor;
extern pool_data pData;
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

static void handleApiStats()
{
  mining_data d = getMiningData(millis());
  String json;
  json.reserve(576);
  json += "{";
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
  json += ",\"mhashes\":" + (d.totalMHashes.length() ? d.totalMHashes : String("0"));
  json += ",\"uptime\":\"" + d.timeMining + "\"";
  json += ",\"temp\":\"" + d.temp + "\"";
  json += ",\"blocks\":" + (d.valids.length() ? d.valids : String("0"));
  json += ",\"time\":\"" + d.currentTime + "\"";
  json += ",\"ssid\":\"" + htmlEscape(WiFi.SSID()) + "\"";
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += ",\"theme\":" + String(Settings.displayTheme);
  json += ",\"ping\":" + String(g_stratumPing);
  json += ",\"net_diff\":" + (g_networkDifficulty > 0.0 ? String(g_networkDifficulty, 2) : String("100000000000000.00"));
  json += ",\"using_fallback\":" + String(g_usingFallback ? "true" : "false");
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

static void handleRestart()
{
  webCfgServer.send(200, "application/json", "{\"status\":\"restarting\"}");
  delay(600);
  ESP.restart();
}

static void handleRoot()
{
  mining_data d = getMiningData(millis());
  String fullWallet = String(Settings.BtcWallet);
  String cleanWallet = getCleanWallet(fullWallet);
  String worker = getWorkerName(fullWallet);
  bool isPublicPool = Settings.PoolAddress.indexOf("public-pool") >= 0;

  String page;
  page.reserve(48000);

  page += F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>NerdMiner Dashboard &amp; Settings</title>"
            "<style>"
            ":root{"
            "--bg:#0b0e14;--card:#151b23;--card-border:#30363d;--input-bg:#0d1117;"
            "--text:#f0f6fc;--muted:#8b949e;--amber:#f7931a;--green:#22c55e;--warn:#eab308;--red:#ef4444;"
            "}"
            "* {box-sizing:border-box;margin:0;padding:0}"
            "body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:16px 12px 48px;line-height:1.5}"
            ".container{max-width:640px;margin:0 auto}"
            "header{display:flex;align-items:center;justify-content:space-between;padding-bottom:14px;border-bottom:1px solid var(--card-border);margin-bottom:18px}"
            ".brand{display:flex;align-items:center;gap:10px}"
            ".logo-box{width:34px;height:34px;background:var(--amber);border-radius:8px;display:flex;align-items:center;justify-content:center;color:#000;font-weight:900;font-size:20px;transition:background .3s}"
            ".brand-title{font-size:18px;font-weight:800;letter-spacing:-0.5px}"
            ".brand-subtitle{font-size:12px;color:var(--muted)}"
            ".status-badge{display:flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--card-border);padding:6px 12px;border-radius:20px;font-size:12px;font-weight:700}"
            ".dot{width:8px;height:8px;border-radius:50%;display:inline-block}"
            ".dot-green{background:var(--green);box-shadow:0 0 8px var(--green)}"
            ".dot-amber{background:var(--warn);box-shadow:0 0 8px var(--warn)}"
            ".dot-gray{background:var(--muted)}"
            ".fact-banner{background:linear-gradient(135deg,rgba(247,147,26,0.12),rgba(34,197,94,0.08));border:1px solid rgba(247,147,26,0.35);border-radius:8px;padding:12px 14px;margin-bottom:14px;display:flex;align-items:flex-start;gap:10px;font-size:13px;line-height:1.5;color:#e6edf3}"
            ".fact-icon{font-size:18px;line-height:1;flex-shrink:0}"
            ".learn-card{background:var(--card);border:1px solid var(--card-border);border-radius:8px;padding:18px 20px;margin-bottom:14px}"
            ".learn-hdr{display:flex;align-items:center;justify-content:space-between;padding-bottom:12px;margin-bottom:16px;border-bottom:1px solid rgba(255,255,255,0.06)}"
            ".learn-title{font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:0.8px;color:var(--amber)}"
            ".learn-sec{margin-bottom:16px}"
            ".learn-sec:last-child{margin-bottom:0}"
            ".learn-h3{font-size:13.5px;font-weight:700;color:var(--amber);margin-bottom:6px;display:flex;align-items:center;gap:6px}"
            ".learn-txt{font-size:13px;line-height:1.6;color:#c9d1d9}"
            ".fields-grid{display:grid;grid-template-columns:repeat(1,1fr);gap:8px;margin:10px 0}"
            "@media(min-width:480px){.fields-grid{grid-template-columns:repeat(2,1fr)}}"
            "@media(min-width:640px){.fields-grid{grid-template-columns:repeat(3,1fr)}}"
            ".field-box{background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:9px 11px}"
            ".field-name{font-size:12px;font-weight:700;color:var(--text);display:flex;align-items:center;justify-content:space-between}"
            ".field-bytes{font-size:10px;font-weight:700;color:var(--amber);background:rgba(247,147,26,0.12);padding:1px 6px;border-radius:4px;border:1px solid rgba(247,147,26,0.25)}"
            ".field-desc{font-size:11px;color:var(--muted);margin-top:3px;line-height:1.35}"
            ".field-sum{background:rgba(255,255,255,0.02);border:1px dashed var(--card-border);border-radius:6px;padding:7px 10px;font-size:11.5px;color:var(--text);margin-top:6px;text-align:center}"
            ".story-card{background:var(--card);border:1px solid var(--card-border);border-radius:8px;padding:18px 20px;margin-bottom:14px}"
            ".story-hdr{display:flex;align-items:center;justify-content:space-between;padding-bottom:12px;margin-bottom:16px;border-bottom:1px solid rgba(255,255,255,0.06)}"
            ".story-title{font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:0.8px;color:var(--muted)}"
            ".story-tag{font-size:11px;font-weight:600;color:var(--muted);background:var(--input-bg);padding:2px 8px;border-radius:4px;border:1px solid var(--card-border)}"
            ".story-flow{display:flex;flex-direction:column;gap:14px}"
            ".story-p{display:flex;gap:12px;align-items:flex-start;font-size:13.5px;line-height:1.6;color:#c9d1d9}"
            ".story-num{font-size:11px;font-weight:800;color:var(--muted);background:var(--input-bg);border:1px solid var(--card-border);width:22px;height:22px;border-radius:50%;display:flex;align-items:center;justify-content:center;flex-shrink:0;margin-top:2px}"
            ".story-text{flex:1}"
            ".hl-key{color:var(--amber);font-weight:700}"
            ".hl-val{color:var(--text);font-weight:700;font-feature-settings:'tnum'}"
            ".diag-strip{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-bottom:18px}"
            "@media(min-width:480px){.diag-strip{grid-template-columns:repeat(3,1fr)}}"
            "@media(min-width:620px){.diag-strip{grid-template-columns:repeat(6,1fr)}}"
            ".diag-item{background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:10px 12px;display:flex;flex-direction:column;justify-content:center}"
            ".diag-label{display:block;font-size:10px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:0.5px;margin-bottom:2px}"
            ".diag-val{font-size:13px;font-weight:700;color:var(--text);font-feature-settings:'tnum'}"
            ".diag-sub{font-size:11px;font-weight:400;color:var(--muted)}"
            ".btn-reset-stats{background:transparent;border:1px solid var(--card-border);color:var(--muted);border-radius:4px;padding:4px 8px;font-size:11px;font-weight:600;cursor:pointer;transition:all .2s;margin-top:2px;text-align:center}"
            ".btn-reset-stats:hover{border-color:var(--amber);color:var(--amber);background:rgba(247,147,26,0.08)}"
            ".pool-banner{background:linear-gradient(135deg,rgba(247,147,26,0.1),rgba(34,197,94,0.06));border:1px solid rgba(247,147,26,0.3);border-radius:8px;padding:14px;margin-bottom:18px;display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px}"
            ".pool-info{font-size:13px}"
            ".pool-btn{display:inline-flex;align-items:center;gap:6px;background:var(--amber);color:#000;text-decoration:none;font-weight:700;font-size:12px;padding:8px 14px;border-radius:6px;transition:opacity .2s}"
            ".pool-btn:hover{opacity:.9}"
            ".card{background:var(--card);border:1px solid var(--card-border);border-radius:8px;padding:18px;margin-bottom:18px}"
            ".card-title{font-size:14px;font-weight:700;text-transform:uppercase;letter-spacing:0.5px;color:var(--amber);margin-bottom:14px;display:flex;align-items:center;gap:8px}"
            ".field{margin-bottom:14px}"
            ".field:last-child{margin-bottom:0}"
            "label{display:block;font-size:13px;font-weight:600;color:var(--text);margin-bottom:5px}"
            ".help{font-size:12px;color:var(--muted);margin-top:4px}"
            "input[type=text],input[type=number],select{width:100%;background:var(--input-bg);border:1px solid var(--card-border);color:var(--text);padding:9px 12px;border-radius:6px;font-size:14px;outline:none;transition:border-color .2s}"
            "input[type=text]:focus,input[type=number]:focus,select:focus{border-color:var(--amber);box-shadow:0 0 0 2px rgba(247,147,26,0.2)}"
            ".cfg-story{background:var(--card);border:1px solid var(--card-border);border-radius:8px;padding:20px;margin-bottom:18px}"
            ".cfg-story-hdr{display:flex;align-items:center;justify-content:space-between;padding-bottom:12px;margin-bottom:16px;border-bottom:1px solid rgba(255,255,255,0.06)}"
            ".cfg-title{font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:0.8px;color:var(--muted)}"
            ".cfg-tag{font-size:11px;font-weight:600;color:var(--muted);background:var(--input-bg);padding:2px 8px;border-radius:4px;border:1px solid var(--card-border)}"
            ".cfg-flow{display:flex;flex-direction:column;gap:18px}"
            ".cfg-p{font-size:14px;line-height:2.0;color:#c9d1d9}"
            ".blank-input{display:inline-block;background:transparent;border:none;border-bottom:2px solid var(--card-border);color:var(--amber);font-size:14px;font-weight:700;padding:2px 6px;margin:0 4px;outline:none;transition:border-color .2s,background .2s;font-family:inherit;vertical-align:baseline}"
            ".blank-input:focus{border-bottom-color:var(--amber);background:rgba(247,147,26,0.08)}"
            ".blank-input::placeholder{color:var(--muted);font-weight:400}"
            ".blank-select{display:inline-block;background:transparent;border:none;border-bottom:2px solid var(--card-border);color:var(--amber);font-size:14px;font-weight:700;padding:2px 6px;margin:0 4px;outline:none;cursor:pointer;transition:border-color .2s;font-family:inherit;vertical-align:baseline}"
            ".blank-select:focus{border-bottom-color:var(--amber);background:rgba(247,147,26,0.08)}"
            ".blank-select option{background:#151b23;color:var(--text)}"
            ".cfg-help{font-size:12px;color:var(--muted);line-height:1.45;margin-top:6px;display:block}"
            ".preset-pills{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 6px}"
            ".preset-pill{background:#21262d;border:1px solid var(--card-border);color:var(--text);font-size:11px;font-weight:600;padding:4px 10px;border-radius:12px;cursor:pointer;transition:all .2s}"
            ".preset-pill:hover,.preset-pill.active{background:rgba(247,147,26,0.15);border-color:var(--amber);color:var(--amber)}"
            ".preset-comment{font-size:12px;color:var(--muted);background:rgba(0,0,0,0.25);border-left:3px solid var(--amber);padding:6px 10px;border-radius:0 4px 4px 0;margin-top:6px}"
            ".presets{display:flex;flex-wrap:wrap;gap:6px;margin-top:6px;margin-bottom:12px}"
            ".preset-btn{background:#21262d;border:1px solid var(--card-border);color:var(--text);font-size:11px;font-weight:600;padding:4px 9px;border-radius:4px;cursor:pointer;transition:all .2s}"
            ".preset-btn:hover{background:#30363d;border-color:var(--amber)}"
            ".theme-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:6px}"
            "@media(min-width:520px){.theme-grid{grid-template-columns:repeat(3,1fr)}}"
            ".theme-card{background:var(--input-bg);border:1px solid var(--card-border);border-radius:8px;padding:12px;cursor:pointer;transition:all .2s ease;user-select:none;position:relative}"
            ".theme-card:hover{border-color:var(--amber);background:#161b22}"
            ".theme-card.active{border-color:var(--amber);background:rgba(247,147,26,0.08);box-shadow:0 0 0 1px var(--amber)}"
            ".theme-swatch{display:flex;align-items:center;gap:6px;margin-bottom:8px}"
            ".swatch-dot{width:14px;height:14px;border-radius:50%;display:inline-block;border:1px solid rgba(255,255,255,0.2)}"
            ".theme-card h4{font-size:13px;font-weight:700;color:var(--text);margin-bottom:2px}"
            ".theme-card p{font-size:11px;color:var(--muted);line-height:1.3}"
            ".theme-toast{margin-top:10px;padding:8px 12px;background:rgba(34,197,94,0.15);border:1px solid rgba(34,197,94,0.3);color:var(--green);border-radius:6px;font-size:12px;font-weight:600;text-align:center}"
            ".slider-row{display:flex;align-items:center;gap:12px}"
            "input[type=range]{flex:1;accent-color:var(--amber)}"
            ".slider-val{font-size:13px;font-weight:700;min-width:70px;text-align:right;color:var(--text)}"
            ".checkbox-label{display:flex;align-items:center;gap:10px;cursor:pointer;font-weight:500;font-size:13px}"
            ".checkbox-label input[type=checkbox]{accent-color:var(--amber);width:16px;height:16px}"
            ".actions{margin-top:22px;display:flex;flex-direction:column;gap:10px}"
            ".btn-save{background:var(--amber);color:#000;border:none;border-radius:6px;padding:12px 18px;font-size:15px;font-weight:800;cursor:pointer;width:100%;transition:opacity .2s}"
            ".btn-save:hover{opacity:.9}"
            ".btn-restart{background:transparent;border:1px solid #da3633;color:#f85149;border-radius:6px;padding:9px;font-size:13px;font-weight:600;cursor:pointer;width:100%;transition:background .2s}"
            ".btn-restart:hover{background:rgba(218,54,51,0.1)}"
            "#overlay{display:none;position:fixed;inset:0;background:rgba(11,14,20,0.95);z-index:999;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:20px}"
            ".spinner{width:40px;height:40px;border:3px solid var(--card-border);border-top-color:var(--amber);border-radius:50%;animation:spin 1s linear infinite;margin-bottom:16px}"
            "@keyframes spin{to{transform:rotate(360deg)}}"
            "</style></head><body>"
            "<div class='container'>"
            "<header>"
            "<div class='brand'>"
            "<div class='logo-box' id='site-logo'>&#8383;</div>"
            "<div><div class='brand-title'>NerdMiner CYD</div><div class='brand-subtitle'>v1.6.3 &bull; ");

  page += htmlEscape(WiFi.localIP().toString()) + F(" &bull; ") + htmlEscape(WiFi.SSID()) + F("</div></div></div>"
            "<div class='status-badge'>"
            "<span id='st-dot' class='dot ");

  bool isHashing = (mMonitor.NerdStatus == NM_hashing || (WiFi.status() == WL_CONNECTED && d.currentHashRate.toFloat() > 0));
  if (isHashing)
    page += F("dot-green'></span><span id='st-txt'>HASHING</span>");
  else if (mMonitor.NerdStatus == NM_Connecting)
    page += F("dot-amber'></span><span id='st-txt'>CONNECTING</span>");
  else
    page += F("dot-gray'></span><span id='st-txt'>IDLE</span>");

  page += F("</div></header>"
            "<div class='fact-banner'>"
            "<div class='fact-icon'>💡</div>"
            "<div class='fact-text'>"
            "<strong>Historical Perspective:</strong> In 2009 (Satoshi era), 3 hours of CPU mining on an ordinary home computer could earn ~100 BTC (worth over <strong>$6.8M USD</strong> today) with only <strong>8 leading zeros</strong>. Today, Bitcoin requires <strong>18 leading zeros</strong> and over 800 EH/s worldwide. That is why solo mining on this ESP32 is an educational lottery ticket!"
            "</div></div>"
            "<div class='story-card'>"
            "<div class='story-hdr'>"
            "<span class='story-title'>Live Miner Explanation</span>"
            "<span class='story-tag'>Solo PoW Lottery</span>"
            "</div>"
            "<div class='story-flow'>"
            "<div class='story-p'><span class='story-num'>1</span><div class='story-text'>"
            "Your CYD builds an 80-byte block header (previous block hash, transaction Merkle root, timestamp, and target bits). The <strong class='hl-key'>nonce (number used once)</strong> is the <em>only</em> dial it turns. It is currently making about <strong class='hl-val'><span id='dyn-hps'>");
  float hr = d.currentHashRate.toFloat();
  unsigned long hps = (unsigned long)(hr * 1000.0f);
  page += (hps > 0 ? String(hps) : F("0"));
  page += F("</span> guesses every second</strong> through SHA-256 (a one-way cryptographic function where every +1 totally scrambles the hash). This guessing speed is called the <strong class='hl-key'>hashrate</strong>, measured in <strong class='hl-val'>KH/s (thousands of hashes per second)</strong>. At this pace, it tests approximately <strong class='hl-val'><span id='dyn-hpd'>--</span></strong> possible solutions every day.</div></div>"
            "<div class='story-p'><span class='story-num'>2</span><div class='story-text'>"
            "In <strong class='hl-val'><span id='dyn-uptime'>");
  page += (d.timeMining.length() ? d.timeMining : F("--"));
  page += F("</span></strong> since booting, this miner has completed approximately <strong class='hl-val'><span id='dyn-totwork'>");
  uint64_t mhashes = (uint64_t)d.totalMHashes.toInt();
  if (mhashes >= 1000) page += String((float)mhashes / 1000.0f, 1) + F(" billion");
  else page += String((unsigned long)mhashes) + F(" million");
  page += F(" hash attempts</strong>.</div></div>"
            "<div class='story-p'><span class='story-num'>3</span><div class='story-text'>"
            "So far, your miner has submitted <strong class='hl-key'><span id='dyn-shares'>");
  page += (d.completedShares.length() ? d.completedShares : F("0"));
  page += F("</span> shares</strong> (averaging roughly one share every <strong class='hl-val'><span id='dyn-hpshare'>--</span></strong>, or about <strong class='hl-val'><span id='dyn-tpshare'>--</span></strong>). A <strong class='hl-key'>share</strong> is proof that the miner is actually doing the work: it found a result difficult enough for the pool to accept and verify, even though it was not difficult enough to solve a real Bitcoin block.</div></div>"
            "<div class='story-p'><span class='story-num'>4</span><div class='story-text'>"
            "Your best result so far reached a <strong class='hl-key'>difficulty of <span id='dyn-diff'>");
  page += (d.bestDiff.length() ? d.bestDiff : F("0.000"));
  page += F("</span></strong>. Only about 1 in every <strong class='hl-val'><span id='dyn-rarity'>--</span></strong> random hashes would be expected to score this well or better. This is a statistical measure of rarity&mdash;higher is better.</div></div>"
            "<div class='story-p'><span class='story-num'>5</span><div class='story-text'>"
            "Bitcoin's test condition is <code style='color:var(--amber);background:var(--input-bg);padding:2px 6px;border-radius:4px;'>hash &lt; target</code>. Because the target is astronomically small, winning hashes naturally begin with <strong class='hl-val'>18+ leading zeros</strong> (roughly <strong class='hl-val'><span id='dyn-netdiff'>100 trillion</span></strong> network difficulty). Your best result reached about <strong class='hl-val'><span id='dyn-netpct'>--</span></strong> of the target difficulty required to solve a block.</div></div>"
            "<div class='story-p'><span class='story-num'>6</span><div class='story-text'>"
            "At your current speed, the estimated chance of finding a Bitcoin block is about <strong class='hl-val'><span id='dyn-dayodds'>--</span> per day</strong> (or <strong class='hl-val'><span id='dyn-yrodds'>--</span> per year</strong>). The statistical average wait time would be roughly <strong class='hl-val'><span id='dyn-avgwait'>--</span></strong>. You could theoretically solve a block on your very next guess&mdash;or never.</div></div>"
            "<div class='story-p'><span class='story-num'>7</span><div class='story-text'>"
            "If your miner solves a block, there is no sharing with a pool: you claim the full current <strong class='hl-key'>block reward of 3.125 BTC</strong> plus transaction fees (worth over <strong class='hl-val'>$200,000+ USD</strong>), paid directly to your configured wallet.</div></div>"
            "<div class='story-p'><span class='story-num'>8</span><div class='story-text'>"
            "This CYD represents roughly <strong class='hl-val'><span id='dyn-netpow'>--</span></strong> of Bitcoin's worldwide computing power (~700 EH/s). At ~1.2 W of typical board power draw, it uses less than 1 kWh per month and costs just pennies to run.</div></div>"
            "</div></div>"
            "<div class='diag-strip'>"
            "<div class='diag-item'><span class='diag-label'>Core Temp</span><span class='diag-val'><span id='st-temp'>");
  page += d.temp.length() ? d.temp : String("--");
  page += F("</span>&deg;C</span></div>"
            "<div class='diag-item'><span class='diag-label'>Uptime</span><span class='diag-val' id='st-uptime'>");
  page += d.timeMining.length() ? d.timeMining : String("--");
  page += F("</span></div>"
            "<div class='diag-item'><span class='diag-label'>Stratum Ping</span><span class='diag-val'><span id='st-ping'>");
  page += String(g_stratumPing) + F("</span> ms</span></div>"
            "<div class='diag-item'><span class='diag-label'>Wi-Fi Signal</span><span class='diag-val'><span id='st-wifi'>");
  page += String(getWifiQuality(WiFi.RSSI())) + F("%</span> <span class='diag-sub'>(<span id='st-rssi'>");
  page += String(WiFi.RSSI()) + F("</span> dBm)</span></span></div>"
            "<div class='diag-item'><span class='diag-label'>Free Heap</span><span class='diag-val' id='st-heap'>");
  page += String(ESP.getFreeHeap() / 1024) + F(" KB</span></div>"
            "<div class='diag-item'><span class='diag-label'>Session</span><button type='button' class='btn-reset-stats' onclick='resetSession()'>&#8634; Reset Stats</button></div>"
            "</div>"
            "<div class='learn-card'>"
            "<div class='learn-hdr'>"
            "<span class='learn-title'>📖 LEARN.TXT &bull; How Bitcoin Mining Actually Works</span>"
            "<span class='story-tag'>Educational Architecture</span>"
            "</div>"
            "<div class='learn-sec'>"
            "<div class='learn-h3'>What even is a \"hash\"?</div>"
            "<div class='learn-txt'>"
            "SHA-256 is a mathematical function that takes any input&mdash;a word, a transaction, or a block header&mdash;and squishes it into a fixed-size <strong>256-bit (32-byte) fingerprint</strong>. The same input always gives the exact same fingerprint. But change just one bit or letter anywhere, and the entire fingerprint changes completely and unpredictably&mdash;there is no \"getting warmer\" (the <strong>avalanche effect</strong>). You cannot work backwards from a fingerprint to deduce the input; your only real option is to guess inputs and check. Bitcoin actually hashes <strong>twice in a row</strong> (<code>SHA-256(SHA-256(header))</code>, called <strong>SHA256d</strong>) to defend against length-extension attacks."
            "</div></div>"
            "<div class='learn-sec'>"
            "<div class='learn-h3'>The 80 bytes you're actually hashing</div>"
            "<div class='learn-txt'>"
            "Every Bitcoin block has a small \"header\"&mdash;<strong>just 80 bytes</strong>&mdash;and that is the <em>only</em> thing a miner hashes over and over. It is strictly composed of six fields:"
            "<div class='fields-grid'>"
            "<div class='field-box'><div class='field-name'><span>1. Version</span><span class='field-bytes'>4 Bytes</span></div><div class='field-desc'>Signals consensus rules and soft-fork features this block supports.</div></div>"
            "<div class='field-box'><div class='field-name'><span>2. Previous Hash</span><span class='field-bytes'>32 Bytes</span></div><div class='field-desc'>Chains every block into unbroken history. Tamper with any past block, and every fingerprint after it breaks!</div></div>"
            "<div class='field-box'><div class='field-name'><span>3. Merkle Root</span><span class='field-bytes'>32 Bytes</span></div><div class='field-desc'>Single master fingerprint representing all transactions. Change 1 satoshi in any tx, the Merkle root changes.</div></div>"
            "<div class='field-box'><div class='field-name'><span>4. Time</span><span class='field-bytes'>4 Bytes</span></div><div class='field-desc'>UNIX timestamp recorded roughly when the block was assembled.</div></div>"
            "<div class='field-box'><div class='field-name'><span>5. Bits</span><span class='field-bytes'>4 Bytes</span></div><div class='field-desc'>Compact scientific notation for the target number (the finish line) without sending all 32 bytes.</div></div>"
            "<div class='field-box'><div class='field-name'><span>6. Nonce (Dial)</span><span class='field-bytes'>4 Bytes</span></div><div class='field-desc'>The knob to turn! A 4-byte counter (0 to 4.29 billion). This is the <em>only</em> field your CYD increments!</div></div>"
            "</div>"
            "<div class='field-sum'>Header Formula: 4 + 32 + 32 + 4 + 4 + 4 = <strong>80 Bytes Total</strong> (76 bytes fixed, Nonce dial turns)</div>"
            "</div></div>"
            "<div class='learn-sec'>"
            "<div class='learn-h3'>The target: your finish line</div>"
            "<div class='learn-txt'>"
            "The Bitcoin network agrees on a \"target\"&mdash;an enormous 256-bit number, but still smaller than the biggest possible 256-bit number. To win, your 80-byte header's double-SHA256 hash must fall numerically below that target (<code>hash &lt; target</code>). Thanks to the avalanche effect, there is no way to steer or nudge a hash lower. Every attempt is an <strong>independent roll of a 256-bit die</strong>: you win if you roll under the target. Because the target is astronomically small, winning hashes naturally begin with <strong>18+ leading zeros</strong>."
            "</div></div></div>"
            "<div class='card' id='pow-sim-card'>"
            "<div class='card-title'>"
            "<span>⚡ Interactive Proof-of-Work Simulator</span>"
            "<span class='story-tag' style='margin-left:auto;'>Browser SHA256d</span>"
            "</div>"
            "<p style='font-size:13px;color:var(--muted);margin-bottom:12px;'>"
            "Experience the exact 80-byte double-SHA256 loop your CYD runs millions of times. Turn the Nonce dial to see how each attempt completely randomizes the 256-bit hash:"
            "</p>"
            "<div style='background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:12px;margin-bottom:12px;font-family:monospace;font-size:12px;'>"
            "<div style='color:var(--muted);margin-bottom:6px;'><strong>80-Byte Candidate Block Header:</strong></div>"
            "<div style='background:rgba(0,0,0,0.3);border:1px solid var(--card-border);border-radius:4px;padding:8px;font-size:11px;color:var(--muted);margin-bottom:8px;word-break:break-all;'>"
            "<span style='color:#79c0ff'>[Ver 4B: 02000000]</span> "
            "<span style='color:#7ee787'>[Prev 32B: 00000000...a1b2]</span> "
            "<span style='color:#d2a8ff'>[Merkle 32B: 4a5e1e...c3d4]</span> "
            "<span style='color:#ffa657'>[Time 4B: 65b821a0]</span> "
            "<span style='color:#ff7b72'>[Bits 4B: 1704e6c3]</span> "
            "<span style='color:var(--amber);font-weight:700;'>[Nonce 4B: <span id='sim-nonce-hex'>00000000</span>]</span>"
            "</div>"
            "<div style='display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin:6px 0;'>"
            "<div><strong>Nonce Dial:</strong> <span id='sim-nonce' style='color:var(--amber);font-size:14px;font-weight:700;'>0 (0x00000000)</span></div>"
            "<div style='display:flex;align-items:center;gap:6px;'>"
            "<label style='font-size:11px;color:var(--muted);margin:0;'>Target Zeros:</label>"
            "<select id='sim-diff' style='padding:2px 6px;font-size:11px;width:auto;'>"
            "<option value='3'>3 Zeros (Very Easy)</option>"
            "<option value='4' selected>4 Zeros (Easy)</option>"
            "<option value='5'>5 Zeros (Medium)</option>"
            "<option value='6'>6 Zeros (Hard)</option>"
            "</select>"
            "</div></div>"
            "<div style='margin-top:6px;word-break:break-all;'>"
            "<div style='color:var(--muted);font-size:11px;'>Resulting 256-Bit Hash (SHA256d):</div>"
            "<div id='sim-hash' style='font-size:12px;font-weight:700;color:var(--muted);margin-top:2px;'>Click 'Test Next Nonce' to hash</div>"
            "</div>"
            "<div id='sim-result' style='margin-top:8px;font-size:12px;font-weight:700;color:var(--muted);'>Status: Ready to test</div>"
            "</div>"
            "<div style='display:flex;gap:8px;flex-wrap:wrap;'>"
            "<button type='button' class='preset-pill active' onclick='simStep()' style='padding:6px 14px;font-size:12px;'>Test Next Nonce (+1)</button>"
            "<button type='button' class='preset-pill' id='sim-auto-btn' onclick='simToggleAuto()' style='padding:6px 14px;font-size:12px;'>&#9654; Auto Mine (10 H/s)</button>"
            "<button type='button' class='preset-pill' onclick='simReset()' style='padding:6px 10px;font-size:12px;'>Reset</button>"
            "</div></div>");

  if (isPublicPool && cleanWallet.length() > 0)
  {
    page += F("<div class='pool-banner'>"
              "<div class='pool-info'>"
              "<strong>Mining on Public-Pool.io</strong><br>"
              "<span style='color:var(--muted)'>Worker: ");
    page += (worker.length() ? htmlEscape(worker) : F("(default)")) + F("</span></div>"
              "<a href='https://web.public-pool.io/#/app/");
    page += htmlEscape(cleanWallet) + F("' target='_blank' rel='noopener' class='pool-btn'>"
              "View Worker on Public-Pool.io &#8599;</a></div>");
  }

  page += F("<form method='POST' action='/save' id='cfg-form'>"
            "<div class='cfg-story'>"
            "<div class='cfg-story-hdr'>"
            "<span class='cfg-title'>Miner Configuration Guide</span>"
            "<span class='cfg-tag'>Interactive Setup</span>"
            "</div>"
            "<div class='cfg-flow'>"
            "<!-- 1. Solo Pool & Stratum -->"
            "<div class='cfg-p'>"
            "Connect this CYD miner to the solo pool at <input type='text' id='f-pool' class='blank-input' style='min-width:150px' name='pool' value='");
  page += htmlEscape(Settings.PoolAddress);
  page += F("' required> on Stratum port <input type='number' id='f-port' class='blank-input' style='width:68px' name='port' value='");
  page += String(Settings.PoolPort);
  page += F("' required> using pool password <input type='text' id='f-pass' class='blank-input' style='width:50px' name='poolpass' value='");
  page += htmlEscape(String(Settings.PoolPassword));
  page += F("'>. If the primary pool is unreachable after 3 attempts, automatically fail over to fallback pool <input type='text' id='f-fbpool' class='blank-input' style='min-width:140px' name='fbpool' value='");
  page += htmlEscape(Settings.FallbackPoolAddress);
  page += F("'> on port <input type='number' id='f-fbport' class='blank-input' style='width:68px' name='fbport' value='");
  page += String(Settings.FallbackPoolPort);
  page += F("'>."
            "<div class='preset-pills'>"
            "<span style='font-size:11px;color:var(--muted);align-self:center;'>Quick Solo Presets:</span>"
            "<button type='button' class='preset-pill active' onclick='choosePreset(\"public-pool.io\",3333,\"x\",\"Public-Pool.io (port 3333) is the official recommended Stratum V1 port for ESP32 miners.\")'>Public-Pool (3333 &bull; Recommended)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"pool.nerdminers.org\",3333,\"x\",\"NerdMiners.org (port 3333) is an ESP32-dedicated solo pool optimized for low-hashrate devices.\")'>NerdMiners.org (3333)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"pool.solomining.de\",3333,\"x\",\"SoloMining.de (port 3333) is a low-latency European solo mining pool.\")'>SoloMining.de (3333)</button>"
            "<button type='button' class='preset-pill' onclick='choosePreset(\"public-pool.io\",21496,\"x\",\"Port 21496 is the legacy port; if you experience dropped connections or 0 KH/s, switch back to 3333.\")'>Public-Pool (21496 &bull; Legacy)</button>"
            "</div>"
            "<div id='preset-note' class='preset-comment'>Public-Pool.io on port 3333 is the official standard for unencrypted Stratum V1, ensuring stable shares for low-hashrate hardware.</div>"
            "</div>"
            "<!-- 2. Payout Address & Worker -->"
            "<div class='cfg-p'>"
            "If this miner finds a winning Bitcoin block, have the pool deposit the entire 3.125 BTC block reward directly into my on-chain Bitcoin address:"
            "<br>"
            "<input type='text' name='wallet' id='f-wallet' class='blank-input' style='width:100%;max-width:440px;margin-top:6px;' value='");
  page += htmlEscape(String(Settings.BtcWallet));
  page += F("' required>"
            "<span class='cfg-help'>💡 <strong>Worker naming:</strong> To give your miner a friendly nickname on the pool dashboard, append <code>.worker_name</code> directly to your address (e.g. <code>");
  page += (cleanWallet.length() ? htmlEscape(cleanWallet) : F("bc1q...")) + F(".cyd</code>). Because solo mining sends payouts directly on-chain, only you hold the keys.</span>"
            "</div>"
            "<!-- 3. Screen Orientation, Brightness & Themes -->"
            "<div class='cfg-p'>"
            "Display information on the CYD screen in <select name='rot' class='blank-select'>");
  int curRot = Settings.screenRotation;
  page += "<option value='0'" + String(curRot == 0 ? " selected" : "") + ">Portrait &bull; USB at bottom (Default)</option>";
  page += "<option value='1'" + String(curRot == 1 ? " selected" : "") + ">Landscape &bull; USB on right</option>";
  page += "<option value='2'" + String(curRot == 2 ? " selected" : "") + ">Portrait Inverted &bull; USB at top</option>";
  page += "<option value='3'" + String(curRot == 3 ? " selected" : "") + ">Landscape Inverted &bull; USB on left</option>";
  page += F("</select> orientation at <span id='b-val' style='color:var(--amber);font-weight:700'>");
  page += String(Settings.Brightness) + " (" + String((Settings.Brightness * 100) / 255) + F("%)</span> screen brightness:"
            "<div class='slider-row' style='margin:10px 0;'>"
            "<input type='range' name='brightness' id='b-slider' min='10' max='255' value='");
  page += String(Settings.Brightness) + F("' oninput='updateBright(this.value)'>"
            "<label class='checkbox-label' style='margin-left:14px;white-space:nowrap;'>"
            "<input type='checkbox' name='invert'");
  if (Settings.invertColors) page += F(" checked");
  page += F("> Invert colors</label></div>"
            "<div style='margin-top:8px;'>"
            "Style the screen with your chosen color palette (tap any card to switch instantly):"
            "<div class='theme-grid' style='margin-top:10px;'>");
  int curTheme = Settings.displayTheme;
  page += F("<div class='theme-card");
  if (curTheme == 0) page += F(" active");
  page += F("' id='thm-0' onclick='selectTheme(0,\"#f7931a\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#f7931a'></span><span class='swatch-dot' style='background:#10141c'></span></div>"
            "<h4>Instrument Amber</h4><p>Matte black &amp; Bitcoin amber instrument (Default)</p></div>"
            "<div class='theme-card");
  if (curTheme == 1) page += F(" active");
  page += F("' id='thm-1' onclick='selectTheme(1,\"#22c55e\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#22c55e'></span><span class='swatch-dot' style='background:#08140a'></span></div>"
            "<h4>Matrix Green</h4><p>Retro phosphor terminal CRT aesthetic</p></div>"
            "<div class='theme-card");
  if (curTheme == 2) page += F(" active");
  page += F("' id='thm-2' onclick='selectTheme(2,\"#f59e0b\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#f59e0b'></span><span class='swatch-dot' style='background:#181208'></span></div>"
            "<h4>Cyber Amber</h4><p>Vintage 1980s monochrome CRT terminal</p></div>"
            "<div class='theme-card");
  if (curTheme == 3) page += F(" active");
  page += F("' id='thm-3' onclick='selectTheme(3,\"#06b6d4\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#06b6d4'></span><span class='swatch-dot' style='background:#ec4899'></span></div>"
            "<h4>Arcade Neon</h4><p>Vibrant cyan &amp; magenta classic mod colors</p></div>"
            "<div class='theme-card");
  if (curTheme == 4) page += F(" active");
  page += F("' id='thm-4' onclick='selectTheme(4,\"#38bdf8\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#38bdf8'></span><span class='swatch-dot' style='background:#111827'></span></div>"
            "<h4>Ice Blue</h4><p>Cool midnight slate &amp; ice blue precision</p></div>"
            "<div class='theme-card");
  if (curTheme == 5) page += F(" active");
  page += F("' id='thm-5' onclick='selectTheme(5,\"#ffffff\")'>"
            "<div class='theme-swatch'><span class='swatch-dot' style='background:#ffffff'></span><span class='swatch-dot' style='background:#181818'></span></div>"
            "<h4>Monochrome</h4><p>Stark pure white on void black (E-Ink style)</p></div>"
            "</div>"
            "<input type='hidden' id='f-theme' name='theme' value='");
  page += String(curTheme) + F("'>"
            "<div id='thm-toast' class='theme-toast' style='display:none'>&#10003; Live theme applied instantly to CYD screen!</div>"
            "</div></div>"
            "<!-- 4. Timezone, Currency & Stats Persistence -->"
            "<div class='cfg-p'>"
            "Set the on-screen clock to <select id='tz-select' onchange='applyTz(this.value)' class='blank-select'>"
            "<option value=''>Preset Timezones...</option>"
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
            "</select> (UTC offset <input type='number' name='tz' id='f-tz' class='blank-input' style='width:52px' min='-12' max='14' value='");
  page += String(Settings.Timezone) + F("'>) and quote live Bitcoin market prices in <select name='currency' class='blank-select'>");
  for (int i = 0; i < kCurrencyCount; i++)
  {
    page += "<option value='" + String(kCurrencies[i].code) + "'";
    if (Settings.Currency.equalsIgnoreCase(kCurrencies[i].code))
      page += " selected";
    page += ">" + String(kCurrencies[i].label) + "</option>";
  }
  page += F("</select>."
            "<div style='margin-top:10px;'>"
            "<label class='checkbox-label'>"
            "<input type='checkbox' name='savestats'");
  if (Settings.saveStats) page += F(" checked");
  page += F("> <span><strong>Remember lifetime work:</strong> Persist total hashes and submitted share count across power cycles in ESP32 flash memory.</span>"
            "</label></div></div>"
            "</div></div>"
            "<div class='card' style='background:rgba(21,27,35,0.6);border:1px solid rgba(255,255,255,0.06);'>"
            "<div class='card-title' style='color:var(--muted);font-size:12px;'>Device Touch &amp; Physical Button Guide</div>"
            "<div style='font-size:12px;color:var(--muted);display:grid;grid-template-columns:1fr;gap:10px;'>"
            "<div style='background:#0d1117;padding:12px;border-radius:6px;border:1px solid var(--card-border)'>"
            "<strong style='color:var(--text);display:block;margin-bottom:6px;'>&#128070; CYD Touch Zones</strong>"
            "<div>&bull; <strong>Tap Wordmark:</strong> Cycle accent color</div>"
            "<div>&bull; <strong>Tap Top-Right:</strong> Toggle screen backlight ON / OFF</div>"
            "<div>&bull; <strong>Tap Screen Center:</strong> Cycle views (MINE &rarr; MARKET &rarr; CLOCK)</div>"
            "</div>"
            "<div style='background:#0d1117;padding:12px;border-radius:6px;border:1px solid var(--card-border)'>"
            "<strong style='color:var(--text);display:block;margin-bottom:6px;'>&#128296; Physical BOOT Button</strong>"
            "<div>&bull; <strong>1 Click:</strong> Next screen view</div>"
            "<div>&bull; <strong>Double Click:</strong> Rotate orientation (0&deg;, 90&deg;, 180&deg;, 270&deg;)</div>"
            "<div>&bull; <strong>Triple Click:</strong> Toggle backlight ON / OFF</div>"
            "<div>&bull; <strong>Hold 5s:</strong> Reset stored configuration and reboot</div>"
            "</div></div></div>"
            "<div class='actions'>"
            "<button type='submit' class='btn-save'>Save &amp; Restart NerdMiner</button>"
            "<button type='button' class='btn-restart' onclick='rebootDevice()'>Reboot Miner (without saving)</button>"
            "</div>"
            "</form></div>"
            "<div id='overlay'>"
            "<div class='spinner'></div>"
            "<h3 style='margin-bottom:8px'>Restarting NerdMiner</h3>"
            "<p style='color:var(--muted);font-size:14px;max-width:320px'>Reconnecting in <span id='cd'>8</span> seconds...</p>"
            "</div>"
            "<script>"
            "function choosePreset(h,p,pass,note){document.getElementById('f-pool').value=h;document.getElementById('f-port').value=p;document.getElementById('f-pass').value=pass;const n=document.getElementById('preset-note');if(n)n.textContent=note;if(window.event&&window.event.target){document.querySelectorAll('.preset-pill').forEach(b=>b.classList.remove('active'));window.event.target.classList.add('active');}}"
            "function setPool(h,p,pass){choosePreset(h,p,pass,'Switched pool to '+h+':'+p);}"
            "function updateBright(v){document.getElementById('b-val').textContent=v+' ('+Math.round(v*100/255)+'%)';}"
            "function applyTz(v){if(v!=='')document.getElementById('f-tz').value=v;}"
            "function selectTheme(id,col){"
            "document.getElementById('f-theme').value=id;"
            "document.querySelectorAll('.theme-card').forEach(c=>c.classList.remove('active'));"
            "const el=document.getElementById('thm-'+id);"
            "if(el)el.classList.add('active');"
            "if(col){document.documentElement.style.setProperty('--amber',col);}"
            "fetch('/api/theme?id='+id,{method:'POST'}).then(r=>r.json()).then(d=>{"
            "const t=document.getElementById('thm-toast');"
            "if(t){t.style.display='block';setTimeout(()=>{t.style.display='none';},2500);}"
            "}).catch(e=>{});}"
            "function pollReboot(){let s=8;document.getElementById('overlay').style.display='flex';"
            "const t=setInterval(()=>{s--;if(s>=0)document.getElementById('cd').textContent=s;"
            "if(s<=0){clearInterval(t);checkBack();}},1000);}"
            "function checkBack(){fetch('/',{cache:'no-store'}).then(()=>location.replace('/')).catch(()=>setTimeout(checkBack,1000));}"
            "document.getElementById('cfg-form').addEventListener('submit',()=>setTimeout(pollReboot,100));"
            "function rebootDevice(){if(confirm('Reboot the miner now?')){fetch('/restart',{method:'POST'}).then(pollReboot).catch(pollReboot);}}"
            "function fmtNum(n){if(isNaN(n)||n<=0)return'0';if(n>=1e12)return(n/1e12).toFixed(2)+' trillion';if(n>=1e9)return(n/1e9).toFixed(1)+' billion';if(n>=1e6)return(n/1e6).toFixed(1)+' million';return Math.round(n).toLocaleString();}"
            "function fmtUptime(u){if(!u)return'--';const p=u.trim().split(/\\s+/);let d=0,t=u;if(p.length===2){d=parseInt(p[0])||0;t=p[1];}const c=t.split(':');if(c.length===3){const hrs=parseInt(c[0])||0,mins=parseInt(c[1])||0;return(d>0?d+'d ':'')+(hrs>0?hrs+'h ':'')+mins+'m';}return u;}"
            "function resetSession(){"
            "if(confirm('Reset current session shares and session best difficulty? (Lifetime total hashes in flash remain preserved)')){"
            "fetch('/api/reset-session',{method:'POST'}).then(r=>r.json()).then(()=>{pollStats();}).catch(()=>{});}}"
            "function updateStats(d){"
            "const hr=parseFloat(d.hashrate)||0,hps=Math.round(hr*1000),hpd=hr*1000*86400;"
            "const mhashes=Number(d.mhashes)||0,totHashes=mhashes*1e6,shares=parseInt(d.shares)||0,diff=parseFloat(d.best_diff)||0;"
            "const eHps=document.getElementById('dyn-hps');if(eHps)eHps.textContent=hps.toLocaleString();"
            "const eHpd=document.getElementById('dyn-hpd');if(eHpd)eHpd.textContent=fmtNum(hpd);"
            "const eUp=document.getElementById('dyn-uptime');if(eUp)eUp.textContent=fmtUptime(d.uptime);"
            "const eTot=document.getElementById('dyn-totwork');if(eTot)eTot.textContent=fmtNum(totHashes);"
            "const eSh=document.getElementById('dyn-shares');if(eSh)eSh.textContent=shares.toLocaleString();"
            "const hPerShare=shares>0?(totHashes/shares):0;"
            "const eHpshare=document.getElementById('dyn-hpshare');if(eHpshare)eHpshare.textContent=fmtNum(hPerShare)+' hashes';"
            "const tPerShareSec=(hps>0&&hPerShare>0)?(hPerShare/hps):0;"
            "const eTpshare=document.getElementById('dyn-tpshare');if(eTpshare){if(tPerShareSec>=3600)eTpshare.textContent=(tPerShareSec/3600).toFixed(1)+' hours';else if(tPerShareSec>=60)eTpshare.textContent=Math.round(tPerShareSec/60)+' minutes';else eTpshare.textContent=Math.round(tPerShareSec)+' seconds';}"
            "const eDiff=document.getElementById('dyn-diff');if(eDiff)eDiff.textContent=diff>0?diff.toFixed(3):'0.000';"
            "const rarity=diff*4294967296;"
            "const eRar=document.getElementById('dyn-rarity');if(eRar)eRar.textContent=diff>0?fmtNum(rarity):'N/A';"
            "const netDiff=(d.net_diff&&d.net_diff>0)?Number(d.net_diff):100e12;"
            "const eNetdiff=document.getElementById('dyn-netdiff');if(eNetdiff)eNetdiff.textContent=fmtNum(netDiff);"
            "const eNetpct=document.getElementById('dyn-netpct');if(eNetpct){if(diff>0){const pct=(diff/netDiff)*100;eNetpct.textContent=pct.toExponential(2)+'% (~'+pct.toFixed(8)+'%)';}else eNetpct.textContent='0%';}"
            "const targetHashes=netDiff*4294967296,dayOdds=hpd>0?(targetHashes/hpd):0,yrOdds=dayOdds/365.25;"
            "const eDayOdds=document.getElementById('dyn-dayodds');if(eDayOdds)eDayOdds.textContent=dayOdds>0?('1 in '+fmtNum(dayOdds)):'--';"
            "const eYrOdds=document.getElementById('dyn-yrodds');if(eYrOdds)eYrOdds.textContent=yrOdds>0?('1 in '+fmtNum(yrOdds)):'--';"
            "const eAvgWait=document.getElementById('dyn-avgwait');if(eAvgWait)eAvgWait.textContent=yrOdds>0?(fmtNum(yrOdds)+' years'):'--';"
            "const netHps=700e18,eNetpow=document.getElementById('dyn-netpow');if(eNetpow){const netSharePct=(hps/netHps)*100;eNetpow.textContent=netSharePct.toExponential(1)+'%';}"
            "if(d.temp)document.getElementById('st-temp').textContent=d.temp;"
            "if(d.uptime)document.getElementById('st-uptime').textContent=d.uptime;"
            "const ePing=document.getElementById('st-ping');if(ePing&&d.ping!==undefined)ePing.textContent=d.ping;"
            "if(d.free_heap)document.getElementById('st-heap').textContent=Math.round(d.free_heap/1024)+' KB';"
            "if(d.rssi!==undefined){const q=Math.min(Math.max(2*(d.rssi+100),0),100);const w=document.getElementById('st-wifi'),r=document.getElementById('st-rssi');if(w)w.textContent=q+'%';if(r)r.textContent=d.rssi;}"
            "const dot=document.getElementById('st-dot'),txt=document.getElementById('st-txt');"
            "if(d.status==='hashing'){dot.className='dot dot-green';txt.textContent=d.using_fallback?'HASHING (FALLBACK)':'HASHING';}"
            "else if(d.status==='connecting'){dot.className='dot dot-amber';txt.textContent='CONNECTING';}"
            "else{dot.className='dot dot-gray';txt.textContent='IDLE';}"
            "let simN=0,simAutoTimer=null;"
            "function toHex32(n){return(n>>>0).toString(16).padStart(8,'0');}"
            "async function sha256Hex(str){"
            "try{"
            "const buf=new TextEncoder().encode(str);"
            "const h1=await crypto.subtle.digest('SHA-256',buf);"
            "const h2=await crypto.subtle.digest('SHA-256',h1);"
            "const arr=Array.from(new Uint8Array(h2));"
            "return arr.map(b=>b.toString(16).padStart(2,'0')).join('');"
            "}catch(e){return'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';}"
            "}"
            "async function simStep(){"
            "simN++;"
            "const hex=toHex32(simN);"
            "const eN=document.getElementById('sim-nonce');if(eN)eN.textContent=simN.toLocaleString()+' (0x'+hex+')';"
            "const eNHex=document.getElementById('sim-nonce-hex');if(eNHex)eNHex.textContent=hex;"
            "const targetZeros=parseInt(document.getElementById('sim-diff').value)||4;"
            "const targetPrefix='0'.repeat(targetZeros);"
            "const hash=await sha256Hex('020000000000000000000000000213a48e4a5e1e4baab89f3a325165b821a01704e6c3'+hex);"
            "const eHash=document.getElementById('sim-hash');"
            "const eRes=document.getElementById('sim-result');"
            "if(hash.startsWith(targetPrefix)){"
            "if(eHash)eHash.innerHTML='<span style=\"color:var(--green);font-weight:800;\">'+hash.substring(0,targetZeros)+'</span>'+hash.substring(targetZeros);"
            "if(eRes)eRes.innerHTML='<span style=\"color:var(--green);\">&#10003; WINNER! Hash &lt; Target ('+targetZeros+' leading zeros found)!</span>';"
            "if(simAutoTimer)simToggleAuto();"
            "}else{"
            "if(eHash)eHash.innerHTML='<span style=\"color:var(--red);\">'+hash.substring(0,4)+'</span>'+hash.substring(4);"
            "if(eRes)eRes.innerHTML='<span style=\"color:var(--red);\">&#10005; TOO LARGE (Die roll missed target of '+targetZeros+' zeros. No getting warmer!)</span>';"
            "}"
            "}"
            "function simToggleAuto(){"
            "const btn=document.getElementById('sim-auto-btn');"
            "if(simAutoTimer){"
            "clearInterval(simAutoTimer);simAutoTimer=null;"
            "if(btn)btn.textContent='&#9654; Auto Mine (10 H/s)';"
            "}else{"
            "simAutoTimer=setInterval(simStep,100);"
            "if(btn)btn.textContent='&#9208; Pause Mining';"
            "}"
            "}"
            "function simReset(){"
            "if(simAutoTimer)simToggleAuto();"
            "simN=0;"
            "const eN=document.getElementById('sim-nonce');if(eN)eN.textContent='0 (0x00000000)';"
            "const eNHex=document.getElementById('sim-nonce-hex');if(eNHex)eNHex.textContent='00000000';"
            "const eH=document.getElementById('sim-hash');if(eH)eH.textContent='Click \"Test Next Nonce\" to hash';"
            "const eR=document.getElementById('sim-result');if(eR)eR.textContent='Status: Ready to test';"
            "}"
            "function pollStats(){fetch('/api/stats').then(r=>r.json()).then(updateStats).catch(()=>{});}"
            "pollStats();setInterval(pollStats,3000);"
            "</script></body></html>");

  webCfgServer.send(200, "text/html", page);
}

static void handleSave()
{
  if (webCfgServer.hasArg("pool"))
    Settings.PoolAddress = webCfgServer.arg("pool");
  if (webCfgServer.hasArg("port"))
    Settings.PoolPort = webCfgServer.arg("port").toInt();
  if (webCfgServer.hasArg("fbpool"))
    Settings.FallbackPoolAddress = webCfgServer.arg("fbpool");
  if (webCfgServer.hasArg("fbport"))
    Settings.FallbackPoolPort = webCfgServer.arg("fbport").toInt();
  if (webCfgServer.hasArg("poolpass"))
    strncpy(Settings.PoolPassword, webCfgServer.arg("poolpass").c_str(), sizeof(Settings.PoolPassword) - 1);
  if (webCfgServer.hasArg("wallet"))
    strncpy(Settings.BtcWallet, webCfgServer.arg("wallet").c_str(), sizeof(Settings.BtcWallet) - 1);
  if (webCfgServer.hasArg("tz"))
    Settings.Timezone = webCfgServer.arg("tz").toInt();
  if (webCfgServer.hasArg("currency"))
    Settings.Currency = currencyFor(webCfgServer.arg("currency")).code;
  Settings.saveStats = webCfgServer.hasArg("savestats");

  if (webCfgServer.hasArg("theme"))
  {
    Settings.displayTheme = webCfgServer.arg("theme").toInt();
    setDisplayTheme(Settings.displayTheme);
  }

#if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
  Settings.invertColors = webCfgServer.hasArg("invert");
  if (webCfgServer.hasArg("rot"))
  {
    Settings.screenRotation = webCfgServer.arg("rot").toInt();
    Settings.flipDisplay = (Settings.screenRotation == 3);
  }
  else if (webCfgServer.hasArg("flip"))
  {
    Settings.flipDisplay = true;
    Settings.screenRotation = 3;
  }
  else
  {
    Settings.flipDisplay = false;
    Settings.screenRotation = 1;
  }
  if (webCfgServer.hasArg("brightness"))
    Settings.Brightness = webCfgServer.arg("brightness").toInt();
#endif
#if defined(ES3C35P) || defined(ESP32_4IN_ST7796)
  Settings.invertColors = webCfgServer.hasArg("invert");
  Settings.flipDisplay = webCfgServer.hasArg("flip");
  if (webCfgServer.hasArg("brightness"))
    Settings.Brightness = webCfgServer.arg("brightness").toInt();
#endif

  saveSettingsToFlash();

  webCfgServer.send(200, "text/html",
                     "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                     "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                     "<title>Restarting...</title>"
                     "<style>body{background:#0b0e14;color:#f0f6fc;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}"
                     ".sp{width:40px;height:40px;border:3px solid #30363d;border-top-color:#f7931a;border-radius:50%;animation:s 1s linear infinite;margin:0 auto 16px}"
                     "@keyframes s{to{transform:rotate(360deg)}}</style></head>"
                     "<body><div><div class='sp'></div><h2>Saved Settings</h2><p style='color:#8b949e'>Restarting miner... Returning shortly.</p></div>"
                     "<script>function p(){fetch('/',{cache:'no-store'}).then(()=>location.replace('/')).catch(()=>setTimeout(p,1200))};setTimeout(p,5000);</script>"
                     "</body></html>");

  delay(800);
  ESP.restart();
}

void setup_webConfig(void)
{
  if (webCfgStarted)
    return;

  webCfgServer.on("/", HTTP_GET, handleRoot);
  webCfgServer.on("/save", HTTP_POST, handleSave);
  webCfgServer.on("/api/stats", HTTP_GET, handleApiStats);
  webCfgServer.on("/api/reset-session", HTTP_POST, handleResetSession);
  webCfgServer.on("/api/theme", HTTP_GET, handleGetTheme);
  webCfgServer.on("/api/theme", HTTP_POST, handleSetTheme);
  webCfgServer.on("/restart", HTTP_POST, handleRestart);
  webCfgServer.onNotFound([]()
  {
    webCfgServer.sendHeader("Location", "/");
    webCfgServer.send(302, "text/plain", "");
  });
  webCfgServer.begin();
  webCfgStarted = true;

  Serial.print("[WEBCFG] Settings & dashboard available at http://");
  Serial.println(WiFi.localIP());
}

void webConfigProcess(void)
{
  if (webCfgStarted)
    webCfgServer.handleClient();
}

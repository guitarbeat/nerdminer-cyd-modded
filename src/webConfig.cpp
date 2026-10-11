#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "webConfig.h"
#include "wManager.h"
#include "drivers/storage/storage.h"
#include "currency.h"
#include "monitor.h"
#include "drivers/displays/display.h"

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
  json.reserve(512);
  json += "{";
  json += "\"status\":";
  if (mMonitor.NerdStatus == NM_hashing)
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
  json += "}";
  webCfgServer.send(200, "application/json", json);
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
  page.reserve(15000);

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
            ".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-bottom:14px}"
            "@media(min-width:520px){.grid{grid-template-columns:repeat(4,1fr)}}"
            ".stat-tile{background:var(--card);border:1px solid var(--card-border);border-radius:8px;padding:12px 14px;position:relative}"
            ".stat-label-wrap{display:flex;align-items:center;justify-content:space-between;margin-bottom:2px}"
            ".stat-label{font-size:11px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:0.5px}"
            ".has-tip{position:relative;cursor:help;display:inline-flex;align-items:center}"
            ".tip-icon{font-size:9px;background:var(--card-border);color:var(--muted);border-radius:50%;width:14px;height:14px;display:inline-flex;align-items:center;justify-content:center;font-weight:bold}"
            ".tip-box{position:absolute;bottom:125%;left:50%;transform:translateX(-50%) translateY(4px);background:#1c2128;border:1px solid var(--card-border);color:var(--text);font-size:11px;font-weight:500;padding:6px 10px;border-radius:6px;white-space:normal;width:190px;text-align:center;line-height:1.3;box-shadow:0 4px 12px rgba(0,0,0,0.5);opacity:0;visibility:hidden;transition:all .15s ease;pointer-events:none;z-index:100}"
            ".has-tip:hover .tip-box,.has-tip:focus .tip-box{opacity:1;visibility:visible;transform:translateX(-50%) translateY(0)}"
            ".stat-val{font-size:20px;font-weight:800;color:var(--text);font-feature-settings:'tnum'}"
            ".stat-sub{font-size:11px;color:var(--muted);margin-top:2px}"
            ".sub-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-bottom:18px}"
            ".pow-card{background:var(--card);border:1px solid var(--card-border);border-radius:8px;margin-bottom:18px;overflow:hidden}"
            ".pow-header{display:flex;align-items:center;justify-content:space-between;padding:14px 16px;cursor:pointer;background:linear-gradient(90deg,rgba(247,147,26,0.06),transparent);user-select:none}"
            ".pow-header:hover{background:linear-gradient(90deg,rgba(247,147,26,0.1),transparent)}"
            ".pow-icon{width:28px;height:28px;background:rgba(247,147,26,0.15);color:var(--amber);border-radius:6px;display:flex;align-items:center;justify-content:center;font-size:14px;font-weight:bold}"
            ".chevron{font-size:12px;color:var(--muted);transition:transform .2s}"
            ".pow-card.open .chevron{transform:rotate(180deg)}"
            ".pow-content{display:none;padding:0 16px 16px;border-top:1px solid var(--card-border)}"
            ".pow-card.open .pow-content{display:block}"
            ".pow-grid{display:grid;grid-template-columns:1fr;gap:12px;margin-top:14px}"
            "@media(min-width:520px){.pow-grid{grid-template-columns:repeat(2,1fr)}}"
            ".pow-item{background:#0d1117;border:1px solid var(--card-border);border-radius:6px;padding:12px}"
            ".pow-item-hdr{display:flex;align-items:center;gap:8px;margin-bottom:6px}"
            ".pow-badge{font-size:9px;font-weight:800;padding:2px 6px;border-radius:4px;text-transform:uppercase;letter-spacing:0.5px}"
            ".badge-hash{background:rgba(247,147,26,0.15);color:var(--amber)}"
            ".badge-share{background:rgba(34,197,94,0.15);color:var(--green)}"
            ".badge-diff{background:rgba(59,130,246,0.15);color:#60a5fa}"
            ".badge-jackpot{background:rgba(234,179,8,0.15);color:var(--warn)}"
            ".pow-item h4{font-size:13px;font-weight:700;color:var(--text)}"
            ".pow-item p{font-size:12px;color:var(--muted);line-height:1.45}"
            ".pow-footer{margin-top:14px;padding:10px 12px;background:#0d1117;border-left:3px solid var(--amber);border-radius:0 6px 6px 0;font-size:12px;color:var(--muted);line-height:1.4}"
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

  if (mMonitor.NerdStatus == NM_hashing)
    page += F("dot-green'></span><span id='st-txt'>HASHING</span>");
  else if (mMonitor.NerdStatus == NM_Connecting)
    page += F("dot-amber'></span><span id='st-txt'>CONNECTING</span>");
  else
    page += F("dot-gray'></span><span id='st-txt'>IDLE</span>");

  page += F("</div></header>"
            "<div class='grid'>"
            "<div class='stat-tile'>"
            "<div class='stat-label-wrap'>"
            "<span class='stat-label'>Hashrate</span>"
            "<span class='has-tip'><span class='tip-icon'>?</span><span class='tip-box'>Computing ~1M SHA-256 double-hash guesses/sec across 32-bit nonces.</span></span>"
            "</div>"
            "<div class='stat-val' id='st-hash'>");
  page += d.currentHashRate.length() ? d.currentHashRate : String("0");
  page += F(" <span style='font-size:12px;font-weight:600;color:var(--muted)'>KH/s</span></div>"
            "<div class='stat-sub'>~1,000,000 guesses/s</div></div>"
            "<div class='stat-tile'>"
            "<div class='stat-label-wrap'>"
            "<span class='stat-label'>Shares</span>"
            "<span class='has-tip'><span class='tip-icon'>?</span><span class='tip-box'>'Accounting units' proving active mathematical work to the pool.</span></span>"
            "</div>"
            "<div class='stat-val' id='st-shares'>");
  page += d.completedShares.length() ? d.completedShares : String("0");
  page += F("</div><div class='stat-sub'>Accepted by pool</div></div>"
            "<div class='stat-tile'>"
            "<div class='stat-label-wrap'>"
            "<span class='stat-label'>Best Diff</span>"
            "<span class='has-tip'><span class='tip-icon'>?</span><span class='tip-box'>Closest hash to a block so far (extra leading zeroes).</span></span>"
            "</div>"
            "<div class='stat-val' id='st-diff'>");
  page += d.bestDiff.length() ? d.bestDiff : String("0.0000");
  page += F("</div><div class='stat-sub'>Personal record</div></div>"
            "<div class='stat-tile'>"
            "<div class='stat-label-wrap'>"
            "<span class='stat-label'>Block Reward</span>"
            "<span class='has-tip'><span class='tip-icon'>?</span><span class='tip-box'>Full block subsidy + fees awarded if your chip solves a valid block!</span></span>"
            "</div>"
            "<div class='stat-val' style='color:var(--amber)'>3.125 BTC</div>"
            "<div class='stat-sub'>~$200,000+ USD</div></div>"
            "</div>"
            "<div class='sub-grid'>"
            "<div class='stat-tile'><div class='stat-label'>Core Temp &bull; Uptime</div>"
            "<div class='stat-val' id='st-temp'>");
  page += d.temp.length() ? d.temp : String("--");
  page += F("&deg;C</div><div class='stat-sub'>Uptime: <span id='st-uptime'>");
  page += d.timeMining.length() ? d.timeMining : String("--");
  page += F("</span></div></div>"
            "<div class='stat-tile'><div class='stat-label'>Wi-Fi &bull; Free Heap</div>"
            "<div class='stat-val'>");
  page += String(getWifiQuality(WiFi.RSSI())) + F("%</div>"
            "<div class='stat-sub'>");
  page += String(WiFi.RSSI()) + F(" dBm &bull; <span id='st-heap'>");
  page += String(ESP.getFreeHeap() / 1024) + F(" KB free</span></div></div></div>");

  // Proof of Work Anatomy Card
  page += F("<div class='pow-card open' id='pow-widget'>"
            "<div class='pow-header' onclick='togglePow()'>"
            "<div style='display:flex;align-items:center;gap:10px'>"
            "<div class='pow-icon'>&#9889;</div>"
            "<div>"
            "<div style='font-size:14px;font-weight:700;color:var(--text)'>Proof of Work Anatomy: Demystifying the Numbers</div>"
            "<div style='font-size:12px;color:var(--muted)'>What your ESP32 is actually doing under the hood &bull; Click to collapse</div>"
            "</div>"
            "</div>"
            "<span class='chevron'>&#9662;</span>"
            "</div>"
            "<div class='pow-content'>"
            "<div class='pow-grid'>"
            "<div class='pow-item'>"
            "<div class='pow-item-hdr'><span class='pow-badge badge-hash'>HASHING</span><h4>Hashrate &amp; Nonces</h4></div>"
            "<p>The ESP32 runs nearly <strong>1 million SHA-256 double-hash guesses every second</strong> (~985 KH/s). It iterates through 32-bit integer values called <em>nonces</em>, altering the block header to find an output below the network target.</p>"
            "</div>"
            "<div class='pow-item'>"
            "<div class='pow-item-hdr'><span class='pow-badge badge-share'>ACCOUNTING</span><h4>Shares Submitted</h4></div>"
            "<p>Because finding a full block is rare, pools assign easier difficulty targets. When your chip hits a hash matching that lower target, it sends a <em>share</em>—an accounting unit proving your miner is actively expending computational energy.</p>"
            "</div>"
            "<div class='pow-item'>"
            "<div class='pow-item-hdr'><span class='pow-badge badge-diff'>PRECISION</span><h4>Best Difficulty</h4></div>"
            "<p>Measures the closest your ESP32 has gotten to landing a valid Bitcoin block. A higher difficulty means more leading zeroes in the resulting hash. A score like <code>1.2M</code> means beating a 1-in-1.2-million difficulty target on a single guess!</p>"
            "</div>"
            "<div class='pow-item'>"
            "<div class='pow-item-hdr'><span class='pow-badge badge-jackpot'>JACKPOT</span><h4>Target Block Reward (3.125 BTC)</h4></div>"
            "<p>If your ESP32 solves a hash that meets global network difficulty, your candidate block is broadcast to the network. On a solo pool like Public-Pool, you win the entire <strong>3.125 BTC subsidy</strong> (~$200,000+ USD) plus all transaction fees directly!</p>"
            "</div>"
            "</div>"
            "<div class='pow-footer'>"
            "<strong>Satoshi CPU Era Context:</strong> In 2009, Satoshi Nakamoto mined the Genesis block on an ordinary processor. The NerdMiner is a standalone homage to that 2009 CPU era—running the exact same SHA-256 mathematics as a decentralized lottery ticket."
            "</div>"
            "</div>"
            "</div>");

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
            "<div class='card'>"
            "<div class='card-title'>Stratum Pool Settings</div>"
            "<div class='presets'>"
            "<span style='font-size:11px;color:var(--muted);align-self:center;margin-right:2px;'>Low-Diff Presets:</span>"
            "<button type='button' class='preset-btn' onclick='setPool(\"public-pool.io\",21496,\"x\")'>Public-Pool (21496)</button>"
            "<button type='button' class='preset-btn' onclick='setPool(\"public-pool.io\",3333,\"x\")'>Public-Pool (3333)</button>"
            "<button type='button' class='preset-btn' onclick='setPool(\"pool.nerdminers.org\",3333,\"x\")'>NerdMiners.org</button>"
            "<button type='button' class='preset-btn' onclick='setPool(\"pool.solomining.de\",3333,\"x\")'>SoloMining.de</button>"
            "</div>"
            "<div class='field'><label>Pool Host / URL</label>"
            "<input type='text' id='f-pool' name='pool' value='");
  page += htmlEscape(Settings.PoolAddress) + F("' required></div>"
            "<div class='field'><label>Pool Port</label>"
            "<input type='number' id='f-port' name='port' value='");
  page += String(Settings.PoolPort) + F("' required>"
            "<div class='help'>Note: Always use low-difficulty solo pools (diff &lt; 1). Standard vardiff pools will not record low-diff shares.</div></div>"
            "<div class='field'><label>Pool Password (optional)</label>"
            "<input type='text' id='f-pass' name='poolpass' value='");
  page += htmlEscape(String(Settings.PoolPassword)) + F("'>"
            "<div class='help'>Default is usually 'x' or empty.</div></div>"
            "</div>"
            "<div class='card'>"
            "<div class='card-title'>Bitcoin Wallet &amp; Worker</div>"
            "<div class='field'><label>Payout Bitcoin Address</label>"
            "<input type='text' name='wallet' value='");
  page += htmlEscape(String(Settings.BtcWallet)) + F("' required>"
            "<div class='help'>To label this worker on Public-Pool, append <code>.worker_name</code> (e.g. <code>");
  page += (cleanWallet.length() ? htmlEscape(cleanWallet) : F("bc1q...")) + F(".cyd</code>).</div></div>"
            "</div>"
            "<div class='card'>"
            "<div class='card-title'>Device Touch &amp; Button Guide</div>"
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
            "</div>"
            "</div>"
            "</div>"
            "<div class='card'>"
            "<div class='card-title'>CYD Display &amp; Orientation</div>"
            "<div class='field'><label>Screen Orientation</label><select name='rot'>");

  int curRot = Settings.screenRotation;
  page += "<option value='0'" + String(curRot == 0 ? " selected" : "") + ">Portrait &bull; USB at bottom (Default)</option>";
  page += "<option value='1'" + String(curRot == 1 ? " selected" : "") + ">Landscape &bull; USB on right</option>";
  page += "<option value='2'" + String(curRot == 2 ? " selected" : "") + ">Portrait Inverted &bull; USB at top</option>";
  page += "<option value='3'" + String(curRot == 3 ? " selected" : "") + ">Landscape Inverted &bull; USB on left</option>";

  page += F("</select><div class='help'>Can also be toggled anytime by double-clicking the physical BOOT button.</div></div>"
            "<div class='field'><label>Screen Brightness</label>"
            "<div class='slider-row'>"
            "<input type='range' name='brightness' id='b-slider' min='10' max='255' value='");
  page += String(Settings.Brightness) + F("' oninput='updateBright(this.value)'>"
            "<span class='slider-val' id='b-val'>");
  page += String(Settings.Brightness) + " (" + String((Settings.Brightness * 100) / 255) + F("%)</span>"
            "</div></div>"
            "<div class='field'><label class='checkbox-label'>"
            "<input type='checkbox' name='invert'");
  if (Settings.invertColors) page += F(" checked");
  page += F("> Invert display colors</label>"
            "<div class='help'>Enable if the screen colors look inverted or cyan.</div></div>"
            "</div>");

  // Display Themes Selector Card
  int curTheme = Settings.displayTheme;
  page += F("<div class='card'>"
            "<div class='card-title'>Display Theme (Live Instant Switch)</div>"
            "<div class='theme-grid'>"
            "<div class='theme-card");
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
            "</div>");

  page += F("<div class='card'>"
            "<div class='card-title'>Regional &amp; System Preferences</div>"
            "<div class='field'><label>Timezone (UTC Offset)</label>"
            "<div style='display:flex;gap:10px'>"
            "<select id='tz-select' onchange='applyTz(this.value)' style='flex:1'>"
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
            "</select>"
            "<input type='number' name='tz' id='f-tz' style='width:90px' min='-12' max='14' value='");
  page += String(Settings.Timezone) + F("'>"
            "</div></div>"
            "<div class='field'><label>BTC Price Fiat Currency</label><select name='currency'>");

  for (int i = 0; i < kCurrencyCount; i++)
  {
    page += "<option value='" + String(kCurrencies[i].code) + "'";
    if (Settings.Currency.equalsIgnoreCase(kCurrencies[i].code))
      page += " selected";
    page += ">" + String(kCurrencies[i].label) + "</option>";
  }

  page += F("</select></div>"
            "<div class='field'><label class='checkbox-label'>"
            "<input type='checkbox' name='savestats'");
  if (Settings.saveStats) page += F(" checked");
  page += F("> Persist mining statistics to flash</label>"
            "<div class='help'>Retains total hashes and valid share count across reboots.</div></div>"
            "</div>"
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
            "function setPool(h,p,pass){document.getElementById('f-pool').value=h;document.getElementById('f-port').value=p;document.getElementById('f-pass').value=pass;}"
            "function updateBright(v){document.getElementById('b-val').textContent=v+' ('+Math.round(v*100/255)+'%)';}"
            "function applyTz(v){if(v!=='')document.getElementById('f-tz').value=v;}"
            "function togglePow(){const w=document.getElementById('pow-widget');w.classList.toggle('open');}"
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
            "setInterval(()=>{fetch('/api/stats').then(r=>r.json()).then(d=>{"
            "if(d.hashrate)document.getElementById('st-hash').innerHTML=d.hashrate+\" <span style='font-size:12px;font-weight:600;color:var(--muted)'>KH/s</span>\";"
            "if(d.shares!==undefined)document.getElementById('st-shares').textContent=d.shares;"
            "if(d.best_diff)document.getElementById('st-diff').textContent=d.best_diff;"
            "if(d.temp)document.getElementById('st-temp').textContent=d.temp+'\\u00B0C';"
            "if(d.uptime)document.getElementById('st-uptime').textContent=d.uptime;"
            "if(d.free_heap)document.getElementById('st-heap').textContent=Math.round(d.free_heap/1024)+' KB free';"
            "const dot=document.getElementById('st-dot'),txt=document.getElementById('st-txt');"
            "if(d.status==='hashing'){dot.className='dot dot-green';txt.textContent='HASHING';}"
            "else if(d.status==='connecting'){dot.className='dot dot-amber';txt.textContent='CONNECTING';}"
            "else{dot.className='dot dot-gray';txt.textContent='IDLE';}"
            "}).catch(()=>{});},3000);"
            "</script></body></html>");

  webCfgServer.send(200, "text/html", page);
}

static void handleSave()
{
  if (webCfgServer.hasArg("pool"))
    Settings.PoolAddress = webCfgServer.arg("pool");
  if (webCfgServer.hasArg("port"))
    Settings.PoolPort = webCfgServer.arg("port").toInt();
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

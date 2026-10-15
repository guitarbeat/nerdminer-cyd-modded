#include "version.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "webConfig.h"
#include "wManager.h"
#include "drivers/storage/storage.h"
#include "currency.h"
#include "monitor.h"
#include "drivers/displays/display.h"
#include "mining.h"
#include "webCydTwin.h"

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
--bg:#0b0e14;--card:#141a22;--card-border:#252d38;--input-bg:#0f1520;
--text:#eef2f8;--muted:#6e7d8f;--amber:#f7931a;--green:#22c55e;--warn:#d97706;--red:#ef4444;
}
* {box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;font-size:16px;padding:16px 16px 56px;line-height:1.5;-webkit-font-smoothing:antialiased}
.container{max-width:640px;margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;padding-bottom:16px;border-bottom:1px solid var(--card-border);margin-bottom:20px}
.brand{display:flex;align-items:center;gap:12px}
.logo-box{width:36px;height:36px;background:var(--amber);display:flex;align-items:center;justify-content:center;color:#000;font-weight:900;font-size:20px;flex-shrink:0;transition:background .3s}
.brand-title{font-size:17px;font-weight:800;letter-spacing:-0.3px}
.brand-subtitle{font-size:12px;color:var(--muted);margin-top:1px}
.status-badge{display:inline-flex;align-items:center;background:var(--card);padding:5px 10px;font-size:12px;font-weight:700}
.txt-green{color:var(--green)}
.txt-amber{color:var(--warn)}
.txt-gray{color:var(--muted)}
details.edu-acc{background:var(--input-bg);margin-bottom:8px;overflow:hidden}
details.edu-acc[open]{border-left:4px solid var(--amber)}
summary.edu-sum{padding:10px 14px;font-size:14px;font-weight:700;color:var(--text);cursor:pointer;list-style:none;display:flex;align-items:center;justify-content:space-between;user-select:none;transition:background .15s}
summary.edu-sum::-webkit-details-marker{display:none}
summary.edu-sum:hover{background:rgba(247,147,26,0.07)}
summary.edu-sum::after{content:'+';font-size:16px;color:var(--amber);font-weight:700}
details.edu-acc[open] summary.edu-sum::after{content:'\2212'}
.edu-body{padding:12px 14px 16px;border-top:1px solid var(--card-border);font-size:14px;line-height:1.6;color:#c9d1d9}
.fields-grid{display:grid;grid-template-columns:repeat(1,1fr);gap:8px;margin:10px 0}
@media(min-width:480px){.fields-grid{grid-template-columns:repeat(2,1fr)}}
@media(min-width:640px){.fields-grid{grid-template-columns:repeat(3,1fr)}}
.field-box{background:var(--card);padding:10px 12px}
.field-name{font-size:13px;font-weight:700;color:var(--text);display:flex;align-items:center;justify-content:space-between;gap:6px}
.field-bytes{font-size:11px;font-weight:700;color:var(--amber)}
.field-desc{font-size:12px;color:var(--muted);margin-top:3px;line-height:1.4}
.field-sum{background:var(--card);padding:8px 12px;font-size:12px;color:var(--text);margin-top:8px}
.story-flow{display:flex;flex-direction:column;gap:14px}
.story-p{display:flex;gap:12px;align-items:flex-start;font-size:14px;line-height:1.6;color:#c9d1d9}
.story-num{font-size:10px;font-weight:800;color:var(--bg);background:var(--muted);width:20px;height:20px;display:flex;align-items:center;justify-content:center;flex-shrink:0;margin-top:3px}
.story-text{flex:1}
.hl-key{color:var(--amber);font-weight:700}
.hl-val{color:var(--text);font-weight:700;font-feature-settings:'tnum'}
.diag-strip{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-bottom:20px}
@media(min-width:480px){.diag-strip{grid-template-columns:repeat(3,1fr)}}
@media(min-width:620px){.diag-strip{grid-template-columns:repeat(6,1fr)}}
.diag-item{background:var(--input-bg);padding:10px 12px;display:flex;flex-direction:column;justify-content:center}
.diag-label{display:block;font-size:11px;font-weight:600;color:var(--muted);margin-bottom:3px}
.diag-val{font-size:14px;font-weight:700;color:var(--text);font-feature-settings:'tnum';white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#st-uptime{font-size:12px}
.diag-sub{font-size:11px;font-weight:400;color:var(--muted)}
.btn-reset-stats{background:var(--card);border:1px solid var(--card-border);color:var(--muted);padding:4px 8px;font-size:11px;font-weight:600;cursor:pointer;transition:color .15s;margin-top:2px}
.btn-reset-stats:hover{color:var(--amber);border-color:var(--amber)}
.pool-banner{background:var(--card);border-left:4px solid var(--amber);padding:14px;margin-bottom:20px;display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px}
.pool-info{font-size:14px}
.pool-btn{display:inline-flex;align-items:center;gap:6px;background:var(--amber);color:#000;text-decoration:none;font-weight:700;font-size:13px;padding:10px 16px;transition:opacity .15s}
.pool-btn:hover{opacity:.85}
input[type=text],input[type=number],select{width:100%;background:var(--input-bg);border:1px solid var(--card-border);color:var(--text);padding:9px 12px;font-size:15px;outline:none;transition:border-color .15s}
input[type=text]:focus,input[type=number]:focus,select:focus{border-color:var(--amber)}
.cfg-story{background:var(--card);padding:20px;margin-bottom:20px}
.cfg-story-hdr{display:flex;align-items:center;justify-content:space-between;padding-bottom:12px;margin-bottom:16px;border-bottom:1px solid var(--card-border)}
.cfg-title{font-size:13px;font-weight:700;color:var(--muted)}
.cfg-tag{font-size:11px;font-weight:600;color:var(--muted);background:var(--input-bg);padding:2px 8px}
.cfg-flow{display:flex;flex-direction:column;gap:20px}
.cfg-p{font-size:15px;line-height:2.0;color:#c9d1d9}
.blank-input{display:inline-block;background:transparent;border:none;border-bottom:2px solid var(--card-border);color:var(--amber);font-size:15px;font-weight:700;padding:2px 6px;margin:0 4px;outline:none;transition:border-color .15s;font-family:inherit;vertical-align:baseline}
.blank-input:focus{border-bottom-color:var(--amber)}
.blank-input::placeholder{color:var(--muted);font-weight:400}
.blank-select{display:inline-block;background:transparent;border:none;border-bottom:2px solid var(--card-border);color:var(--amber);font-size:15px;font-weight:700;padding:2px 6px;margin:0 4px;outline:none;cursor:pointer;transition:border-color .15s;font-family:inherit;vertical-align:baseline}
.blank-select:focus{border-bottom-color:var(--amber)}
.blank-select option{background:#141a22;color:var(--text)}
.cfg-help{font-size:13px;color:var(--muted);line-height:1.5;margin-top:8px;display:block}
.preset-pills{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 6px}
.preset-pill{background:var(--input-bg);border:1px solid var(--card-border);color:var(--text);font-size:12px;font-weight:600;padding:5px 12px;border-radius:999px;cursor:pointer;transition:all .15s}
.preset-pill:hover,.preset-pill.active{background:rgba(247,147,26,0.12);border-color:var(--amber);color:var(--amber)}
.preset-comment{font-size:13px;color:var(--muted);background:var(--input-bg);border-left:4px solid var(--amber);padding:8px 12px;margin-top:8px}
.theme-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:8px}
@media(min-width:520px){.theme-grid{grid-template-columns:repeat(3,1fr)}}
.theme-card{background:var(--input-bg);border:1px solid var(--card-border);padding:12px;cursor:pointer;transition:border-color .15s;user-select:none;position:relative}
.theme-card:hover{border-color:var(--amber)}
.theme-card.active{border-color:var(--amber);background:rgba(247,147,26,0.06)}
.theme-swatch{display:flex;align-items:center;gap:6px;margin-bottom:8px}
.swatch-dot{width:14px;height:14px;border-radius:50%;display:inline-block}
.theme-card h4{font-size:13px;font-weight:700;color:var(--text);margin-bottom:2px}
.theme-card p{font-size:12px;color:var(--muted);line-height:1.3}
.theme-toast{margin-top:10px;padding:8px 12px;background:rgba(34,197,94,0.1);color:var(--green);font-size:12px;font-weight:600;text-align:center}
.slider-row{display:flex;align-items:center;gap:12px}
input[type=range]{flex:1;accent-color:var(--amber)}
.checkbox-label{display:flex;align-items:center;gap:10px;cursor:pointer;font-weight:500;font-size:14px}
.checkbox-label input[type=checkbox]{accent-color:var(--amber);width:16px;height:16px}
.actions{margin-top:24px;display:flex;flex-direction:column;gap:10px}
.btn-save{background:var(--amber);color:#000;border:none;padding:14px 18px;font-size:15px;font-weight:800;cursor:pointer;width:100%;transition:opacity .15s}
.btn-save:hover{opacity:.9}
.btn-restart{background:transparent;border:1px solid #da3633;color:#f85149;padding:10px;font-size:13px;font-weight:600;cursor:pointer;width:100%;transition:background .15s}
.btn-restart:hover{background:rgba(218,54,51,0.08)}
.ota-box{background:var(--card);padding:18px;margin-bottom:20px;border-left:4px solid var(--amber)}
.ota-title{font-size:14px;font-weight:700;color:var(--text);margin-bottom:6px}
.ota-desc{font-size:13px;color:var(--muted);line-height:1.5;margin-bottom:12px;max-width:56ch}
.ota-row{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
.btn-ota{background:var(--input-bg);border:1px solid var(--card-border);color:var(--text);padding:9px 16px;font-size:13px;font-weight:700;cursor:pointer;transition:all .15s}
.btn-ota:hover{border-color:var(--amber);color:var(--amber)}
.ota-progress-box{display:none;margin-top:12px}
.ota-progress-bar{height:6px;background:var(--input-bg);overflow:hidden}
.ota-progress-fill{height:100%;width:0%;background:var(--amber);transition:width .1s linear}
.ota-status{font-size:12px;color:var(--muted);margin-top:6px}
.guide-grid{display:grid;grid-template-columns:1fr;gap:10px;margin:14px 0 20px}
@media(min-width:480px){.guide-grid{grid-template-columns:repeat(2,1fr)}}
.guide-card{background:var(--input-bg);padding:14px;font-size:13px;color:var(--muted);line-height:1.6}
.guide-title{font-size:13px;font-weight:700;color:var(--text);margin-bottom:6px;display:flex;align-items:center;gap:6px}
#overlay{display:none;position:fixed;inset:0;background:rgba(11,14,20,0.95);z-index:999;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:20px}
.spinner{width:40px;height:40px;border:3px solid var(--card-border);border-top-color:var(--amber);border-radius:50%;animation:spin 1s linear infinite;margin-bottom:16px}
@keyframes spin{to{transform:rotate(360deg)}}
)rawcss";

static const char DASH_JS[] PROGMEM = R"rawjs(
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
if(t){t.style.display='block';setTimeout(()=>{t.style.display='none';},2500);}
}).catch(e=>{});}
function pollReboot(){let s=8;document.getElementById('overlay').style.display='flex';
const t=setInterval(()=>{s--;if(s>=0)document.getElementById('cd').textContent=s;
if(s<=0){clearInterval(t);checkBack();}},1000);}
function checkBack(){fetch('/',{cache:'no-store'}).then(()=>location.replace('/')).catch(()=>setTimeout(checkBack,1000));}
document.getElementById('cfg-form').addEventListener('submit',()=>setTimeout(pollReboot,100));
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
if(d.status==='hashing'){txt.className='txt-green';txt.textContent=d.using_fallback?'HASHING (FALLBACK)':'HASHING';}
else if(d.status==='connecting'){txt.className='txt-amber';txt.textContent='CONNECTING';}
else{txt.className='txt-gray';txt.textContent='IDLE';}}
cydLastStats=d;if(d.screen!==undefined){cydDevScr=d.screen;cydUpdateDevBadge();const af=document.getElementById('cyd-auto-follow');if(af&&af.checked&&cydPreviewScr!==cydDevScr){cydSelectTab(cydDevScr,false);}}cydRender();}
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
if(simIsHist)eRes.innerHTML='<span style="color:var(--green);font-size:12px;">&#10003; REAL BLOCK #286,819 SOLVED! Nonce '+curN.toLocaleString()+' &rarr; Hash &lt; Target (wins 25 BTC)!</span>';
else eRes.innerHTML='<span style="color:var(--green);">&#10003; WINNER! Hash &lt; Target!</span>';
}
if(simAutoTimer)simToggleAuto();
}else{
if(eHash)eHash.innerHTML='<span style="color:var(--red);">'+hash.substring(0,6)+'</span>'+hash.substring(6);
if(eRes)eRes.innerHTML='<span style="color:var(--red);">&#10005; TOO LARGE (hash &ge; target). Die roll missed. No getting warmer!</span>';
}
simN++;
}
function simToggleAuto(){
const btn=document.getElementById('sim-auto-btn');
if(simAutoTimer){
clearInterval(simAutoTimer);simAutoTimer=null;
if(btn)btn.textContent='&#9654; Auto Mine (10 H/s)';
}else{
simAutoTimer=setInterval(simStep,100);
if(btn)btn.textContent='&#9208; Pause Mining';
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
if(hTitle)hTitle.textContent='80-Byte Candidate Block Header (Block #286,819):';
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
if(hTitle)hTitle.textContent='80-Byte Candidate Block Header (Sandbox):';
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
const eH=document.getElementById('sim-hash');if(eH)eH.textContent='Click "Test Next Nonce" to hash';
const eR=document.getElementById('sim-result');if(eR)eR.textContent=simIsHist?'Status: Ready (Solves at Nonce 856,192,328)':'Status: Ready to test';
}
function calcFromBtc(v){const n=parseFloat(v);if(isNaN(n))return;document.getElementById('calc-sats').value=Math.round(n*1e8).toLocaleString();document.getElementById('calc-bits').value=(n*1e6).toLocaleString();document.getElementById('calc-mbtc').value=(n*1e3).toLocaleString();}
function calcFromSats(v){const s=parseFloat(String(v).replace(/,/g,''));if(isNaN(s))return;document.getElementById('calc-btc').value=(s/1e8).toFixed(8);document.getElementById('calc-bits').value=(s/100).toLocaleString();document.getElementById('calc-mbtc').value=(s/1e5).toLocaleString();}
function calcFromBits(v){const bt=parseFloat(String(v).replace(/,/g,''));if(isNaN(bt))return;document.getElementById('calc-btc').value=(bt/1e6).toFixed(8);document.getElementById('calc-sats').value=Math.round(bt*100).toLocaleString();document.getElementById('calc-mbtc').value=(bt/1000).toLocaleString();}
function calcFromMbtc(v){const m=parseFloat(String(v).replace(/,/g,''));if(isNaN(m))return;document.getElementById('calc-btc').value=(m/1000).toFixed(8);document.getElementById('calc-sats').value=Math.round(m*1e5).toLocaleString();document.getElementById('calc-bits').value=(m*1000).toLocaleString();}
function pollStats(){fetch('/api/stats').then(r=>r.json()).then(updateStats).catch(()=>{});}
)rawjs";

static void handleRoot()
{
  mining_data d = getMiningData(0);
  String fullWallet = String(Settings.BtcWallet);
  String cleanWallet = getCleanWallet(fullWallet);
  String worker = getWorkerName(fullWallet);
  bool isPublicPool = Settings.PoolAddress.indexOf("public-pool") >= 0;

  String page;
  page.reserve(24000);

  page += F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>NerdMiner Dashboard &amp; Settings</title>"
            "<link rel='stylesheet' href='/dash.css'>"
            "<link rel='stylesheet' href='/cyd-twin.css'></head><body>"
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
    page += F("txt-green'>HASHING</span>");
  else if (mMonitor.NerdStatus == NM_Connecting)
    page += F("txt-amber'>CONNECTING</span>");
  else
    page += F("txt-gray'>IDLE</span>");

  page += F("</div></header>");
  appendCydTwinHtml(page);
  page += F("<div class='diag-strip'>"
            "<div class='diag-item'><span class='diag-label'>Core Temp</span><span class='diag-val'><span id='st-temp'>");
  page += d.temp.length() ? d.temp : String("--");
  page += F("</span>&deg;C</span></div>"
            "<div class='diag-item'><span class='diag-label'>Uptime</span><span class='diag-val' id='st-uptime'>");
  page += formatUptimeWeb(d.timeMining);
  page += F("</span></div>"
            "<div class='diag-item'><span class='diag-label'>Stratum Ping</span><span class='diag-val'><span id='st-ping'>");
  page += String(g_stratumPing) + F("</span> ms</span></div>"
            "<div class='diag-item'><span class='diag-label'>Wi-Fi Signal</span><span class='diag-val'><span id='st-wifi'>");
  page += String(getWifiQuality(WiFi.RSSI())) + F("%</span> <span class='diag-sub'>(<span id='st-rssi'>");
  page += String(WiFi.RSSI()) + F("</span> dBm)</span></span></div>"
            "<div class='diag-item'><span class='diag-label'>Free Heap</span><span class='diag-val' id='st-heap'>");
  page += String(ESP.getFreeHeap() / 1024) + F(" KB</span></div>"
            "<div class='diag-item'><span class='diag-label'>Session</span><button type='button' class='btn-reset-stats' onclick='resetSession()'>&#8634; Reset Stats</button></div>"
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
  page += F("</select>. Read block, fee and difficulty stats from mempool host <input type='text' name='mempool' class='blank-input' style='width:190px' maxlength='80' placeholder='" DEFAULT_MEMPOOLHOST "' value='");
  page += htmlEscape(Settings.MempoolHost) + F("'> (use your own node, e.g. <code>http://umbrel.local:3006</code>)."
            "<div style='margin-top:10px;'>"
            "<label class='checkbox-label'>"
            "<input type='checkbox' name='savestats'");
  if (Settings.saveStats) page += F(" checked");
  page += F("> <span><strong>Remember lifetime work:</strong> Persist total hashes and submitted share count across power cycles in ESP32 flash memory.</span>"
            "</label></div></div>"
            "</div></div>"
            "<div class='actions'>"
            "<button type='submit' class='btn-save'>Save &amp; Restart NerdMiner</button>"
            "<button type='button' class='btn-restart' onclick='rebootDevice()'>Reboot Miner (without saving)</button>"
            "</div>"
            "</form>"
            "<div class='ota-box'>"
            "<div class='ota-title'>Firmware Update Over-the-Air</div>"
            "<div class='ota-desc'>Flash new firmware (.bin) directly over Wi-Fi without connecting a USB cable. The miner will write to flash and automatically reboot.</div>"
            "<div class='ota-row'>"
            "<input type='file' id='ota-file' accept='.bin' style='flex:1;min-width:200px;font-size:13px;color:var(--text);background:var(--input-bg);padding:8px;border:1px solid var(--card-border);'>"
            "<button type='button' class='btn-ota' id='btn-ota-start' onclick='uploadOta()'>Upload &amp; Flash</button>"
            "</div>"
            "<div class='ota-progress-box' id='ota-progress-box'>"
            "<div class='ota-progress-bar'><div class='ota-progress-fill' id='ota-progress-fill'></div></div>"
            "<div class='ota-status' id='ota-status'>Ready</div>"
            "</div>"
            "</div>"
            "<div class='guide-grid'>"
            "<div class='guide-card'>"
            "<div class='guide-title'>&#128070; CYD Touch Zones</div>"
            "<div>&bull; <strong>Tap Wordmark:</strong> Cycle accent color</div>"
            "<div>&bull; <strong>Tap Top-Right:</strong> Toggle screen backlight ON / OFF</div>"
            "<div>&bull; <strong>Tap Screen Center:</strong> Cycle views (MINE &rarr; MARKET &rarr; CLOCK &rarr; PROTOCOL)</div>"
            "</div>"
            "<div class='guide-card'>"
            "<div class='guide-title'>&#128296; Physical BOOT Button</div>"
            "<div>&bull; <strong>1 Click:</strong> Next screen view</div>"
            "<div>&bull; <strong>Double Click:</strong> Rotate orientation (0&deg;, 90&deg;, 180&deg;, 270&deg;)</div>"
            "<div>&bull; <strong>Triple Click:</strong> Toggle backlight ON / OFF</div>"
            "<div>&bull; <strong>Hold 5s:</strong> Reset stored configuration and reboot</div>"
            "</div></div>"
            "<details class='edu-acc'>"
            "<summary class='edu-sum'><span>📖 Live Telemetry Breakdown &amp; PoW Mathematics</span></summary>"
            "<div class='edu-body'>"
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
            "</div></div></details>"
            "<details class='edu-acc'>"
            "<summary class='edu-sum'><span>⚡ Interactive Tools &amp; PoW Simulator</span></summary>"
            "<div class='edu-body'>"
            "<p style='font-size:13px;color:var(--muted);margin-bottom:12px;'>"
            "Experience the exact 80-byte double-SHA256 loop your CYD runs millions of times. Test individual nonces to see how SHA256d scrambles the hash, or solve a real historical Bitcoin block:"
            "</p>"
            "<div style='display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap;'>"
            "<button type='button' class='preset-pill active' id='sim-mode-hist' onclick='simSetMode(true)'>Preset: Historical Block #286,819 (Solves at Nonce 856,192,328!)</button>"
            "<button type='button' class='preset-pill' id='sim-mode-rand' onclick='simSetMode(false)'>Preset: Sandbox (Adjust Leading Zeros)</button>"
            "</div>"
            "<div id='sim-shirriff-box' style='display:flex;align-items:center;gap:6px;flex-wrap:wrap;margin-bottom:10px;'>"
            "<span style='font-size:11px;color:var(--muted);'>Ken Shirriff Nonce Stepping Stones:</span>"
            "<button type='button' class='preset-pill' onclick='simJump(0)' style='font-size:11px;padding:3px 8px;'>0 (0 zeros)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(39)' style='font-size:11px;padding:3px 8px;'>39 (1 zero)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(990)' style='font-size:11px;padding:3px 8px;'>990 (3 zeros)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(52117)' style='font-size:11px;padding:3px 8px;'>52,117 (4 zeros)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(856192320)' style='font-size:11px;padding:3px 8px;'>856,192,320 (Countdown)</button>"
            "<button type='button' class='preset-pill' onclick='simJump(856192328)' style='font-size:11px;padding:3px 8px;'>856,192,328 (WINNER!)</button>"
            "</div>"
            "<div style='background:var(--input-bg);border:1px solid var(--card-border);border-radius:6px;padding:12px;margin-bottom:12px;font-family:monospace;font-size:12px;'>"
            "<div style='color:var(--muted);margin-bottom:6px;'><strong id='sim-hdr-title'>80-Byte Candidate Block Header (Block #286,819):</strong></div>"
            "<div style='background:rgba(0,0,0,0.3);border:1px solid var(--card-border);border-radius:4px;padding:8px;font-size:11px;color:var(--muted);margin-bottom:8px;word-break:break-all;'>"
            "<span style='color:#79c0ff' id='sim-f-ver'>[Ver 4B: 02000000]</span> "
            "<span style='color:#7ee787' id='sim-f-prev'>[Prev 32B: 00000000...9717]</span> "
            "<span style='color:#d2a8ff' id='sim-f-mrkl'>[Merkle 32B: 871714...978a]</span> "
            "<span style='color:#ffa657' id='sim-f-time'>[Time 4B: 53059335]</span> "
            "<span style='color:#ff7b72' id='sim-f-bits'>[Bits 4B: 19015f53]</span> "
            "<span style='color:var(--amber);font-weight:700;'>[Nonce 4B: <span id='sim-nonce-hex'>40750833</span>]</span>"
            "</div>"
            "<div style='display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin:6px 0;'>"
            "<div><strong>Nonce Dial:</strong> <span id='sim-nonce' style='color:var(--amber);font-size:14px;font-weight:700;'>856,192,320 (0x33087540)</span></div>"
            "<div id='sim-zeros-box' style='display:none;align-items:center;gap:6px;'>"
            "<label style='font-size:11px;color:var(--muted);margin:0;'>Target Zeros:</label>"
            "<select id='sim-diff' style='padding:2px 6px;font-size:11px;width:auto;' onchange='simUpdateTarget()'>"
            "<option value='3'>3 Zeros (Very Easy)</option>"
            "<option value='4' selected>4 Zeros (Easy)</option>"
            "<option value='5'>5 Zeros (Medium)</option>"
            "<option value='6'>6 Zeros (Hard)</option>"
            "</select>"
            "</div></div>"
            "<div style='margin-top:6px;word-break:break-all;'>"
            "<div style='color:var(--muted);font-size:11px;'>256-Bit Mining Target &amp; Difficulty:</div>"
            "<div id='sim-target' style='font-size:11px;font-weight:600;color:var(--amber);margin-top:2px;'>00000000000000015f5300000000000000000000000000000000000000000000</div>"
            "<div id='sim-diff-formula' style='font-size:11px;color:var(--muted);margin-top:2px;'>Difficulty = Target<sub>1</sub> / Target = <strong>3,129,573,174.52</strong> (Expected: ~1.34 &times; 10<sup>19</sup> hashes)</div>"
            "</div>"
            "<div style='margin-top:6px;word-break:break-all;'>"
            "<div style='color:var(--muted);font-size:11px;'>Resulting 256-Bit Hash (Double-SHA256, byte-reversed):</div>"
            "<div id='sim-hash' style='font-size:12px;font-weight:700;color:var(--muted);margin-top:2px;'>Click 'Test Next Nonce' to hash</div>"
            "</div>"
            "<div id='sim-result' style='margin-top:8px;font-size:12px;font-weight:700;color:var(--muted);'>Status: Ready to test (Nonce 856,192,320 &rarr; Solves at 856,192,328)</div>"
            "</div>"
            "<div style='display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px;'>"
            "<button type='button' class='preset-pill active' onclick='simStep()' style='padding:6px 14px;font-size:12px;'>Test Next Nonce (+1)</button>"
            "<button type='button' class='preset-pill' id='sim-auto-btn' onclick='simToggleAuto()' style='padding:6px 14px;font-size:12px;'>&#9654; Auto Mine (10 H/s)</button>"
            "<button type='button' class='preset-pill' onclick='simReset()' style='padding:6px 10px;font-size:12px;'>Reset</button>"
            "</div>"
            "<div style='border-top:1px solid var(--card-border);padding-top:12px;'>"
            "<p style='font-size:12px;font-weight:700;color:var(--text);margin-bottom:8px;'>🪙 Bitcoin Supply &amp; Satoshis Converter</p>"
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
            "</div></details>"
            "<details class='edu-acc'>"
            "<summary class='edu-sum'><span>📚 Bitcoin Mining Architecture Reference</span></summary>"
            "<div class='edu-body'>"
            "<details class='edu-acc' name='edu-faq'>"
            "<summary class='edu-sum'><span>1. What is a \"Hash\" &amp; SHA256d?</span></summary>"
            "<div class='edu-body'>"
            "SHA-256 is a cryptographic function that takes any input&mdash;a transaction, block header, or message&mdash;and squishes it into a deterministic <strong>256-bit (32-byte) fingerprint</strong>. Change even a single bit or timestamp, and the entire digest completely scrambles with no predictable pattern (the <strong>avalanche effect</strong>). Because you cannot reverse or deduce inputs from outputs, finding valid hashes requires brute-force guess-and-check. Bitcoin computes SHA-256 <strong>twice consecutively</strong> (<code>SHA256(SHA256d)</code>) to defend against length-extension vulnerabilities."
            "</div></details>"
            "<details class='edu-acc' name='edu-faq'>"
            "<summary class='edu-sum'><span>2. The 80 Bytes You're Hashing (Header Fields)</span></summary>"
            "<div class='edu-body'>"
            "Miners never hash full transactions directly&mdash;they hash a concise <strong>80-byte block header</strong> composed strictly of six fields:"
            "<div class='fields-grid'>"
            "<div class='field-box'><div class='field-name'><span>1. Version</span><span class='field-bytes'>4 Bytes</span></div><div class='field-desc'>Consensus rules &amp; active soft forks.</div></div>"
            "<div class='field-box'><div class='field-name'><span>2. Previous Hash</span><span class='field-bytes'>32 Bytes</span></div><div class='field-desc'>Chains to past block; tamper-evident history.</div></div>"
            "<div class='field-box'><div class='field-name'><span>3. Merkle Root</span><span class='field-bytes'>32 Bytes</span></div><div class='field-desc'>Cryptographic fingerprint summarizing all transactions.</div></div>"
            "<div class='field-box'><div class='field-name'><span>4. Time</span><span class='field-bytes'>4 Bytes</span></div><div class='field-desc'>UNIX timestamp when block was assembled.</div></div>"
            "<div class='field-box'><div class='field-name'><span>5. Bits</span><span class='field-bytes'>4 Bytes</span></div><div class='field-desc'>Compact scientific notation encoding the target threshold.</div></div>"
            "<div class='field-box'><div class='field-name'><span>6. Nonce (Dial)</span><span class='field-bytes'>4 Bytes</span></div><div class='field-desc'>The dial your miner increments from 0 to 4.29 billion!</div></div>"
            "</div>"
            "<div class='field-sum'>Header Formula: 4 + 32 + 32 + 4 + 4 + 4 = <strong>80 Bytes Total</strong> (Satoshi Whitepaper Sec 7: 80B &times; 52,596 blks/yr = <strong>4.2 MB/year</strong> for SPV light clients!)</div>"
            "</div></details>"
            "<details class='edu-acc' name='edu-faq'>"
            "<summary class='edu-sum'><span>3. The Mining Target &amp; Difficulty Formula</span></summary>"
            "<div class='edu-body'>"
            "To solve a block, the double-SHA256 hash must be numerically below the consensus target: <code>hash &lt; target</code>. Every guess is an independent roll of a 256-bit die; because the target is astronomically small, winning hashes start with <strong>18+ leading zeros</strong>.<br><br>"
            "&bull; <strong>Difficulty Formula:</strong> <code>Difficulty = Target<sub>1</sub> / Current_Target</code>.<br>"
            "&bull; Satoshi set base Target<sub>1</sub> = <code>0x00000000FFFF0000...</code> (Bits <code>0x1d00ffff</code>).<br>"
            "&bull; Because <code>Target<sub>1</sub> / 2<sup>256</sup> &approx; 1 / 2<sup>32</sup></code>, difficulty 1 requires an expected <strong>2<sup>32</sup> = 4,294,967,296 hashes</strong>!<br>"
            "&bull; Any difficulty <em>D</em> requires an expected <code>D &times; 2<sup>32</sup></code> attempts. Average block time is <code>Time = (D &times; 2<sup>32</sup>) / Hashrate</code>.<br>"
            "&bull; <strong>How Bits Unpacks:</strong> <code>Target = Mantissa &times; 256<sup>(Exponent - 3)</sup></code>. All header fields are in <strong>little-endian</strong>; digests are byte-reversed into big-endian for zero display."
            "</div></details>"
            "<details class='edu-acc' name='edu-faq'>"
            "<summary class='edu-sum'><span>4. 21 Million Cap, Lost Coins &amp; Satoshis</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>Mathematical Cap:</strong> Fixed issuance cap of <strong>20,999,999.9769 BTC</strong>. Subsidy halves every 210,000 blocks via bitwise shift (<code>&gt;&gt; 1</code>), dropping to zero at block 6,929,999 around <strong>Year 2140</strong>.<br>"
            "&bull; <strong>What happens when coins are lost?</strong> As Satoshi Nakamoto explained: <em>&quot;Lost coins only make everyone else's coins worth slightly more. Think of it as a donation to everyone.&quot;</em> Lost private keys permanently remove coins from supply, creating natural deflation.<br>"
            "&bull; <strong>Why deflation does not break trade:</strong> 1 Bitcoin = <strong>100,000,000 Satoshis</strong>. Internally, Bitcoin Core tracks all balances as 64-bit integers (<code>int64_t</code>), never floats. Even if 99% of coins were lost, global commerce continues smoothly in sub-satoshi units.<br>"
            "&bull; <strong>Post-2140 Economics:</strong> Mining continues every 10 minutes indefinitely, funded 100% by user transaction fees."
            "</div></details>"
            "<details class='edu-acc' name='edu-faq'>"
            "<summary class='edu-sum'><span>5. Satoshi's 2008 Whitepaper &amp; The 6-Confirmation Rule</span></summary>"
            "<div class='edu-body'>"
            "&bull; <strong>Chain of Signatures:</strong> In the 2008 Whitepaper, Satoshi defined an electronic coin as a chain of digital signatures and solved double-spending via distributed Proof-of-Work timestamps.<br>"
            "&bull; <strong>Gold Miner Analogy (Section 6):</strong> <em>&quot;The steady addition of a constant amount of new coins is analogous to gold miners expending resources to add gold to circulation. In our case, it is CPU time and electricity that is expended.&quot;</em> Miners find it strictly more profitable to protect the network than undermine it.<br>"
            "&bull; <strong>Gambler's Ruin Math (Section 11):</strong> Satoshi modeled double-spend races against a 10% hashpower attacker (<code>q = 0.1</code>). After <strong>z = 6 confirmations (~1 hour)</strong>, catch-up probability drops exponentially to <strong>P = 0.0002428 (&lt;0.024%)</strong>, establishing 6 confirmations as the worldwide benchmark for finality."
            "</div></details>"
            "</div></details>"
            "</div>"
            "<div id='overlay'>"
            "<div class='spinner'></div>"
            "<h3 style='margin-bottom:8px'>Restarting NerdMiner</h3>"
            "<p style='color:var(--muted);font-size:14px;max-width:320px'>Reconnecting in <span id='cd'>8</span> seconds...</p>"
            "</div>"
            "<script src='/cyd-twin.js'></script>"
            "<script src='/dash.js'></script>");
  page += F("<script>const initStats=");
  page += buildStatsJson(d);
  page += F(";updateStats(initStats);setInterval(pollStats,3000);</script></body></html>");

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
  if (webCfgServer.hasArg("mempool"))
  {
    String mh = webCfgServer.arg("mempool");
    mh.trim();
    Settings.MempoolHost = mh.length() ? mh : String(DEFAULT_MEMPOOLHOST);
  }
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
  webCfgServer.on("/save", HTTP_POST, handleSave);
  webCfgServer.on("/api/stats", HTTP_GET, handleApiStats);
  webCfgServer.on("/api/reset-session", HTTP_POST, handleResetSession);
  webCfgServer.on("/api/theme", HTTP_GET, handleGetTheme);
  webCfgServer.on("/api/theme", HTTP_POST, handleSetTheme);
  webCfgServer.on("/api/screen", HTTP_GET, handleGetScreen);
  webCfgServer.on("/api/screen", HTTP_POST, handleSetScreen);
  webCfgServer.on("/restart", HTTP_POST, handleRestart);
  webCfgServer.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
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

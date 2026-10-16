#ifndef WEB_CYD_TWIN_H
#define WEB_CYD_TWIN_H

#include <Arduino.h>

static const char CYD_TWIN_STYLE[] PROGMEM = R"rawcss(
:root{
  --cyd-accent:#f7931a;
  --cyd-accent-glow:rgba(247,147,26,0.18);
  --cyd-card-bg:#0d1117;
  --cyd-bezel-bg:radial-gradient(ellipse at 50% -10%,#18202d 0%,#090d13 75%);
  --cyd-border:#1f2937;
  --cyd-border-highlight:#2e3c4e;
  --cyd-text:#f0f6fc;
  --cyd-muted:#8b949e;
  --cyd-green:#22c55e;
  --cyd-amber:#f59e0b;
  --cyd-red:#ef4444;
  --cyd-mono:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;
  --cyd-sans:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  color-scheme:dark;
  accent-color:var(--cyd-accent);
}
.cyd-preview-card{
  background:var(--cyd-card-bg);
  border:1px solid var(--cyd-border);
  border-radius:10px;
  margin-bottom:20px;
  overflow:hidden;
  box-shadow:0 10px 30px rgba(0,0,0,0.5),0 0 0 1px rgba(255,255,255,0.03);
}
.cyd-preview-hdr{
  display:flex;
  justify-content:space-between;
  align-items:center;
  padding:12px 16px;
  background:#090d14;
  border-bottom:1px solid var(--cyd-border);
  flex-wrap:wrap;
  gap:10px;
}
.cyd-title-wrap{
  display:flex;
  align-items:center;
  gap:10px;
}
.cyd-title-icon{
  width:18px;
  height:18px;
  color:var(--cyd-accent);
  display:inline-flex;
  align-items:center;
  justify-content:center;
}
.cyd-preview-title{
  font-family:var(--cyd-sans);
  font-size:12px;
  font-weight:800;
  color:var(--cyd-text);
  text-transform:uppercase;
  letter-spacing:1.5px;
  display:flex;
  align-items:center;
  gap:8px;
}
.cyd-title-tag{
  font-family:var(--cyd-mono);
  font-size:9px;
  font-weight:700;
  color:var(--cyd-accent);
  background:var(--cyd-accent-glow);
  border:1px solid rgba(247,147,26,0.3);
  padding:1px 6px;
  border-radius:4px;
  letter-spacing:1px;
}
.cyd-hdr-right{
  display:flex;
  align-items:center;
  gap:14px;
  font-size:12px;
  color:var(--cyd-muted);
  flex-wrap:wrap;
}
.cyd-toggle{
  display:inline-flex;
  align-items:center;
  gap:6px;
  cursor:pointer;
  user-select:none;
  font-size:12px;
  font-weight:600;
  color:var(--cyd-muted);
  transition:color .15s;
}
.cyd-toggle:hover{color:var(--cyd-text);}
.cyd-toggle input[type=checkbox]{
  accent-color:var(--cyd-accent);
  width:14px;
  height:14px;
  cursor:pointer;
}
.cyd-badge{
  display:inline-flex;
  align-items:center;
  gap:6px;
  background:rgba(34,197,94,0.08);
  color:var(--cyd-green);
  padding:3px 10px;
  border-radius:999px;
  font-size:11px;
  font-weight:700;
  letter-spacing:.6px;
  border:1px solid rgba(34,197,94,0.25);
  font-family:var(--cyd-mono);
  box-shadow:0 0 12px rgba(34,197,94,0.1);
}
.cyd-pulse-dot{
  width:7px;
  height:7px;
  border-radius:50%;
  background:var(--cyd-green);
  display:inline-block;
  box-shadow:0 0 6px var(--cyd-green);
  transition:all .2s;
  flex-shrink:0;
}
.cyd-pulse-dot.loading{
  background:var(--cyd-accent);
  box-shadow:0 0 8px var(--cyd-accent);
  animation:cydBlink .8s infinite alternate;
}
.cyd-pulse-dot.offline{
  background:var(--cyd-red);
  box-shadow:0 0 8px var(--cyd-red);
}
@keyframes cydBlink{
  from{opacity:.35;transform:scale(.85);}
  to{opacity:1;transform:scale(1.15);}
}
.cyd-bezel{
  background:var(--cyd-bezel-bg);
  padding:18px 16px 16px;
  display:flex;
  flex-direction:column;
  align-items:center;
  position:relative;
}
.cyd-bezel-lbl{
  font-size:10px;
  letter-spacing:1.5px;
  color:var(--cyd-muted);
  text-transform:uppercase;
  margin-bottom:12px;
  font-weight:700;
  font-family:var(--cyd-mono);
  text-align:center;
  display:flex;
  align-items:center;
  justify-content:center;
  gap:8px;
  flex-wrap:wrap;
}
.cyd-lbl-tag{
  background:#141b24;
  border:1px solid var(--cyd-border);
  color:var(--cyd-text);
  padding:1px 6px;
  border-radius:4px;
}
.cyd-screen-frame{
  width:100%;
  max-width:252px;
  aspect-ratio:3/4;
  margin:0 auto;
  position:relative;
  background:#000;
  border:5px solid #141923;
  border-radius:8px;
  overflow:hidden;
  box-shadow:0 0 0 1px #222b38,0 12px 32px rgba(0,0,0,0.65);
  transition:max-width .25s ease,aspect-ratio .25s ease;
}
.cyd-screen-frame.landscape{
  max-width:380px;
  aspect-ratio:4/3;
}
#cyd-screen-img{
  width:100%;
  height:100%;
  object-fit:fill;
  display:block;
  image-rendering:-webkit-optimize-contrast;
  image-rendering:-moz-crisp-edges;
  image-rendering:pixelated;
  image-rendering:crisp-edges;
}
.cyd-screen-glare{
  position:absolute;
  top:0;
  left:0;
  right:0;
  height:28%;
  background:linear-gradient(180deg,rgba(255,255,255,0.04) 0%,rgba(255,255,255,0) 100%);
  pointer-events:none;
}
.cyd-tabs{
  display:grid;
  grid-template-columns:repeat(4,1fr);
  gap:0;
  margin-top:16px;
  width:100%;
  max-width:440px;
  border:1px solid var(--cyd-border);
  border-radius:6px;
  overflow:hidden;
  background:#090d14;
}
.cyd-tab-btn{
  background:transparent;
  border:none;
  border-left:1px solid var(--cyd-border);
  color:var(--cyd-muted);
  padding:9px 4px;
  font-family:var(--cyd-mono);
  font-size:11px;
  font-weight:700;
  letter-spacing:.5px;
  cursor:pointer;
  transition:all .15s ease;
  display:flex;
  align-items:center;
  justify-content:center;
  gap:6px;
  white-space:nowrap;
}
.cyd-tab-btn:first-child{border-left:none;}
.cyd-tab-btn:hover{
  color:var(--cyd-text);
  background:rgba(255,255,255,0.03);
}
.cyd-tab-btn.active{
  background:var(--cyd-accent-glow);
  color:var(--cyd-accent);
  box-shadow:inset 0 -2px 0 var(--cyd-accent);
}
.cyd-live-dot{
  width:6px;
  height:6px;
  border-radius:50%;
  background:var(--cyd-green);
  display:inline-block;
  box-shadow:0 0 6px var(--cyd-green);
  flex-shrink:0;
}
.cyd-ctrls{
  display:flex;
  gap:8px;
  margin-top:12px;
  justify-content:center;
  align-items:center;
  width:100%;
  max-width:440px;
  flex-wrap:wrap;
}
.cyd-btn-switch{
  flex:2 1 180px;
  background:var(--cyd-accent);
  color:#000;
  border:none;
  border-radius:6px;
  padding:9px 14px;
  font-family:var(--cyd-sans);
  font-size:12px;
  font-weight:800;
  cursor:pointer;
  letter-spacing:.5px;
  transition:transform .1s ease,opacity .15s ease;
}
.cyd-btn-switch:hover{opacity:.92;transform:translateY(-1px);}
.cyd-btn-switch:active{transform:translateY(0);}
.cyd-btn-switch:disabled{opacity:.5;cursor:not-allowed;}
.cyd-btn-nav{
  flex:1 1 65px;
  background:#10151f;
  border:1px solid var(--cyd-border);
  color:var(--cyd-text);
  border-radius:6px;
  padding:9px 12px;
  font-family:var(--cyd-sans);
  font-size:12px;
  font-weight:600;
  cursor:pointer;
  transition:border-color .15s ease,background .15s ease;
  display:inline-flex;
  align-items:center;
  justify-content:center;
  gap:4px;
}
.cyd-btn-nav:hover{
  border-color:var(--cyd-accent);
  background:#161d2b;
}
.cyd-btn-snap{
  font-family:var(--cyd-mono);
  font-size:11px;
}
@media(max-width:480px){
  .cyd-preview-hdr{padding:10px 12px;}
  .cyd-bezel{padding:14px 10px 12px;}
  .cyd-tab-btn{font-size:10px;padding:8px 2px;letter-spacing:0;}
  .cyd-ctrls{gap:6px;}
  .cyd-btn-switch{flex:1 1 100%;order:1;}
  .cyd-btn-nav{order:2;flex:1 1 28%;padding:8px 6px;font-size:11px;}
}
)rawcss";

static const char CYD_TWIN_HTML[] PROGMEM = R"rawhtml(
<div class='cyd-preview-card'>
  <div class='cyd-preview-hdr'>
    <div class='cyd-title-wrap'>
      <div class='cyd-preview-title'>
        <span class='cyd-title-icon'>&#9632;</span>
        <span>Screen Twin</span>
      </div>
      <span class='cyd-title-tag'>HARDWARE SCREENSHOT</span>
    </div>
    <div class='cyd-hdr-right'>
      <label class='cyd-toggle' title='Sync twin tab automatically when device switches screens'>
        <input type='checkbox' id='cyd-auto-follow' checked onchange='cydToggleFollow()'> Follow Device
      </label>
      <label class='cyd-toggle' title='Toggle automatic periodic refresh'>
        <input type='checkbox' id='cyd-auto-refresh' onchange='cydToggleAutoRefresh(this)'> Auto-Refresh
      </label>
      <span id='cyd-device-badge' class='cyd-badge'>
        <span id='cyd-live-pulse' class='cyd-pulse-dot'></span>
        Device: SCR 0 (MINE)
      </span>
    </div>
  </div>
  <div class='cyd-bezel'>
    <div class='cyd-bezel-lbl' id='cyd-bezel-lbl'>
      ESP32-2432S028R &bull; 2.8&quot; 240&times;320 SPI TFT <span class='cyd-lbl-tag'>PORTRAIT</span>
    </div>
    <div class='cyd-screen-frame' id='cyd-screen-frame'>
      <img id='cyd-screen-img'
           src='/api/screenshot'
           fetchpriority='high'
           alt='Live CYD Screen'
           width='240'
           height='320'>
      <div class='cyd-screen-glare'></div>
    </div>
    <div class='cyd-tabs' role='tablist'>
      <button type='button' class='cyd-tab-btn active' id='ctab-0' onclick='cydSelectTab(0)'>
        <span class='cyd-live-dot' title='Active on hardware'></span>0 MINE
      </button>
      <button type='button' class='cyd-tab-btn' id='ctab-1' onclick='cydSelectTab(1)'>1 MARKET</button>
      <button type='button' class='cyd-tab-btn' id='ctab-2' onclick='cydSelectTab(2)'>2 CLOCK</button>
      <button type='button' class='cyd-tab-btn' id='ctab-3' onclick='cydSelectTab(3)'>3 ARCH</button>
    </div>
    <div class='cyd-ctrls'>
      <button type='button' class='cyd-btn-nav' onclick='cydPrevScreen()' title='Previous Screen'>&larr; Prev</button>
      <button type='button' class='cyd-btn-switch' id='cyd-switch-btn' onclick='cydSwitchPhysical()'>Switch Device to Screen 0</button>
      <button type='button' class='cyd-btn-nav' onclick='cydNextScreen()' title='Next Screen'>Next &rarr;</button>
      <button type='button' class='cyd-btn-nav cyd-btn-snap' id='cyd-snap-btn' onclick='cydSnapshot()' title='Force Snapshot Refresh'>&#8635; Snapshot</button>
      <button type='button' class='cyd-btn-nav' id='cyd-orient-btn' onclick='cydToggleOrientation()' title='Toggle Orientation Preview'>Portrait (240&times;320)</button>
    </div>
  </div>
</div>
)rawhtml";

static const char CYD_TWIN_JS[] PROGMEM = R"rawjs(
let cydPreviewScr = 0;
let cydDevScr = 0;
let cydLastStats = null;
let cydAutoFollow = true;
let cydIsPortrait = true;
let cydAutoRefresh = false;
let cydRefreshTimer = null;
let cydIsLoading = false;
let cydPendingRefresh = false;

const CYD_SCREEN_NAMES = ['MINE', 'MARKET', 'CLOCK', 'ARCH'];

function cydGetScreenUrl(screenIdx) {
  const s = (typeof screenIdx === 'number') ? screenIdx : cydPreviewScr;
  return '/api/screenshot?screen=' + s + '&t=' + Date.now();
}

function cydFetchScreenshot(force) {
  if (cydIsLoading && !force) {
    cydPendingRefresh = true;
    return;
  }
  const img = document.getElementById('cyd-screen-img');
  if (!img) return;

  cydIsLoading = true;
  const pulse = document.getElementById('cyd-live-pulse');
  if (pulse) pulse.classList.add('loading');

  const next = new Image();
  const targetUrl = cydGetScreenUrl(cydPreviewScr);

  next.onload = function() {
    img.src = next.src;
    cydIsLoading = false;
    if (pulse) {
      pulse.classList.remove('loading');
      pulse.classList.remove('offline');
    }
    if (cydPendingRefresh) {
      cydPendingRefresh = false;
      cydFetchScreenshot(false);
    }
  };

  next.onerror = function() {
    cydIsLoading = false;
    if (pulse) {
      pulse.classList.remove('loading');
      pulse.classList.add('offline');
    }
    cydPendingRefresh = false;
  };

  next.src = targetUrl;
}

function cydScheduleRefresh() {
  if (cydRefreshTimer) clearTimeout(cydRefreshTimer);
  if (!cydAutoRefresh) return;
  cydRefreshTimer = setTimeout(function() {
    cydFetchScreenshot(false);
    cydScheduleRefresh();
  }, 2500);
}

function cydToggleAutoRefresh(cb) {
  cydAutoRefresh = cb ? cb.checked : !cydAutoRefresh;
  const el = document.getElementById('cyd-auto-refresh');
  if (el) el.checked = cydAutoRefresh;
  if (cydAutoRefresh) {
    cydFetchScreenshot(true);
    cydScheduleRefresh();
  } else if (cydRefreshTimer) {
    clearTimeout(cydRefreshTimer);
  }
}

function cydToggleFollow() {
  const af = document.getElementById('cyd-auto-follow');
  cydAutoFollow = af ? af.checked : true;
  if (cydAutoFollow && cydPreviewScr !== cydDevScr) {
    cydSelectTab(cydDevScr, false);
  }
}

function cydSnapshot() {
  cydFetchScreenshot(true);
}

function cydSelectTab(idx, isUserClick) {
  if (isUserClick === undefined) isUserClick = true;
  cydPreviewScr = (typeof idx === 'number' && idx >= 0 && idx < 4) ? idx : 0;
  if (isUserClick) {
    const af = document.getElementById('cyd-auto-follow');
    if (af && af.checked && idx !== cydDevScr) {
      af.checked = false;
      cydAutoFollow = false;
    }
  }
  for (let i = 0; i < 4; i++) {
    const el = document.getElementById('ctab-' + i);
    if (el) {
      if (i === cydPreviewScr) el.classList.add('active');
      else el.classList.remove('active');
    }
  }
  const btn = document.getElementById('cyd-switch-btn');
  if (btn) btn.textContent = 'Switch Device to Screen ' + cydPreviewScr;
  cydFetchScreenshot(true);
}

function cydPrevScreen() {
  cydSelectTab((cydPreviewScr + 3) % 4, true);
}

function cydNextScreen() {
  cydSelectTab((cydPreviewScr + 1) % 4, true);
}

function cydSwitchPhysical() {
  const btn = document.getElementById('cyd-switch-btn');
  if (btn) btn.disabled = true;
  fetch('/api/screen?id=' + cydPreviewScr, { method: 'POST' })
    .then(r => r.json())
    .then(data => {
      if (data && data.screen !== undefined) {
        cydDevScr = data.screen;
        cydUpdateDevBadge();
      }
    })
    .catch(() => {})
    .finally(() => {
      if (btn) btn.disabled = false;
      setTimeout(() => cydFetchScreenshot(true), 300);
    });
}

function cydUpdateDevBadge() {
  const b = document.getElementById('cyd-device-badge');
  const scrName = CYD_SCREEN_NAMES[cydDevScr] || 'SCR ' + cydDevScr;
  if (b) {
    b.innerHTML = "<span id='cyd-live-pulse' class='cyd-pulse-dot'></span>Device: SCR " +
                  cydDevScr + ' (' + scrName + ')';
  }
  for (let i = 0; i < 4; i++) {
    const el = document.getElementById('ctab-' + i);
    if (el) {
      const dot = el.querySelector('.cyd-live-dot');
      if (i === cydDevScr) {
        if (!dot) {
          const s = document.createElement('span');
          s.className = 'cyd-live-dot';
          s.title = 'Active on hardware';
          el.prepend(s);
        }
      } else if (dot) {
        dot.remove();
      }
    }
  }
}

function cydToggleOrientation() {
  const af = document.getElementById('cyd-auto-follow');
  if (af) {
    af.checked = false;
    cydAutoFollow = false;
  }
  cydIsPortrait = !cydIsPortrait;
  cydRender();
  cydFetchScreenshot(true);
}

function cydRender() {
  const frame = document.getElementById('cyd-screen-frame');
  const img = document.getElementById('cyd-screen-img');
  if (!frame || !img) return;

  const d = cydLastStats || {};
  const afEl = document.getElementById('cyd-auto-follow');
  cydAutoFollow = afEl ? afEl.checked : true;

  if (d.is_portrait !== undefined && cydAutoFollow) {
    cydIsPortrait = !!d.is_portrait;
  }

  const lbl = document.getElementById('cyd-bezel-lbl');
  const btnOrient = document.getElementById('cyd-orient-btn');

  if (cydIsPortrait) {
    frame.classList.remove('landscape');
    img.width = 240;
    img.height = 320;
    if (lbl) lbl.innerHTML = 'ESP32-2432S028R &bull; 2.8&quot; 240&times;320 SPI TFT <span class="cyd-lbl-tag">PORTRAIT</span>';
    if (btnOrient) btnOrient.textContent = 'Portrait (240×320)';
  } else {
    frame.classList.add('landscape');
    img.width = 320;
    img.height = 240;
    if (lbl) lbl.innerHTML = 'ESP32-2432S028R &bull; 2.8&quot; 320&times;240 SPI TFT <span class="cyd-lbl-tag">LANDSCAPE</span>';
    if (btnOrient) btnOrient.textContent = 'Landscape (320×240)';
  }
}

// Initial bootstrap
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', function() {
    cydUpdateDevBadge();
    cydRender();
    cydScheduleRefresh();
  });
} else {
  cydUpdateDevBadge();
  cydRender();
  cydScheduleRefresh();
}
)rawjs";

static inline void appendCydTwinStyle(String &page) { page += FPSTR(CYD_TWIN_STYLE); }
static inline void appendCydTwinHtml(String &page) { page += FPSTR(CYD_TWIN_HTML); }
static inline void appendCydTwinScript(String &page) { page += FPSTR(CYD_TWIN_JS); }

#endif // WEB_CYD_TWIN_H

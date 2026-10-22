#ifndef WEB_CYD_TWIN_H
#define WEB_CYD_TWIN_H

#include <Arduino.h>

static const char CYD_TWIN_STYLE[] PROGMEM = R"rawcss(
:root{
  --cyd-accent:var(--amber,#f7931a);
  --cyd-accent-soft:rgba(247,147,26,0.10);
  --cyd-card-bg:var(--card,#11151c);
  --cyd-bezel-bg:var(--card,#11151c);
  --cyd-border:var(--card-border,#1e2530);
  --cyd-border-highlight:var(--dim,#58636f);
  --cyd-text:var(--text,#e6ebf2);
  --cyd-muted:var(--muted,#8a96a6);
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
  border-radius:var(--r,6px);
  overflow:hidden;
  min-width:0;
}
.cyd-preview-hdr{
  display:flex;
  justify-content:space-between;
  align-items:center;
  padding:12px 14px 0;
  flex-wrap:wrap;
  gap:10px;
}
.cyd-title-wrap{
  display:flex;
  align-items:center;
  gap:4px 10px;
  flex-wrap:wrap;
  min-width:0;
}
.cyd-preview-title{
  font-family:var(--cyd-sans);
  font-size:14px;
  font-weight:600;
  color:var(--cyd-text);
  display:flex;
  align-items:center;
  gap:8px;
}
.cyd-hdr-right{
  display:flex;
  align-items:center;
  gap:14px;
  font-size:12px;
  color:var(--cyd-muted);
  flex-wrap:wrap;
}
.cyd-badge{
  display:inline-flex;
  align-items:center;
  gap:6px;
  color:var(--cyd-muted);
  font-size:12px;
  font-weight:500;
}
.cyd-pulse-dot{
  width:7px;
  height:7px;
  border-radius:50%;
  background:var(--cyd-green);
  display:inline-block;
  transition:all .2s;
  flex-shrink:0;
}
.cyd-pulse-dot.loading{
  background:var(--cyd-accent);
  animation:cydBlink .8s infinite alternate;
}
.cyd-pulse-dot.offline{
  background:var(--cyd-red);
}
@keyframes cydBlink{
  from{opacity:.35;transform:scale(.85);}
  to{opacity:1;transform:scale(1.15);}
}
.cyd-bezel{
  background:var(--cyd-bezel-bg);
  padding:12px 14px 14px;
  display:flex;
  flex-direction:column;
  align-items:center;
  position:relative;
}
.cyd-bezel-lbl{
  font-size:12px;
  color:var(--cyd-muted);
  margin-bottom:8px;
  text-align:center;
  display:flex;
  align-items:center;
  justify-content:center;
  gap:8px;
  flex-wrap:wrap;
}
.cyd-lbl-tag{color:var(--cyd-muted);}
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
  box-shadow:0 0 0 1px #222b38;
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
.cyd-tabs{
  display:grid;
  grid-template-columns:repeat(3,minmax(0,1fr));
  gap:0;
  margin-top:10px;
  width:100%;
  max-width:440px;
  border-bottom:1px solid var(--cyd-border);
}
.cyd-tab-btn{
  background:transparent;
  border:none;
  border-bottom:2px solid transparent;
  margin-bottom:-1px;
  color:var(--cyd-muted);
  padding:8px 4px;
  font-family:var(--cyd-sans);
  font-size:12px;
  font-weight:500;
  cursor:pointer;
  transition:all .15s ease;
  display:flex;
  align-items:center;
  justify-content:center;
  gap:6px;
  white-space:nowrap;
}
.cyd-tab-btn:hover{color:var(--cyd-text);}
.cyd-tab-btn.active{
  color:var(--cyd-text);
  border-bottom-color:var(--cyd-accent);
}
.cyd-live-dot{
  width:6px;
  height:6px;
  border-radius:50%;
  background:var(--cyd-green);
  display:inline-block;
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
  background:transparent;
  color:var(--cyd-text);
  border:1px solid var(--cyd-border);
  border-radius:var(--r,6px);
  padding:8px 12px;
  min-height:38px;
  font-family:var(--cyd-sans);
  font-size:12px;
  font-weight:600;
  cursor:pointer;
  transition:border-color .15s ease;
}
.cyd-btn-switch:hover{border-color:var(--cyd-border-highlight);}
.cyd-btn-switch:disabled{opacity:.6;cursor:wait;}
.cyd-btn-nav{
  flex:1 1 65px;
  background:transparent;
  border:1px solid var(--cyd-border);
  color:var(--cyd-text);
  border-radius:var(--r,6px);
  padding:8px 12px;
  min-height:38px;
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
.cyd-btn-nav:hover{border-color:var(--cyd-border-highlight);}
.cyd-btn-snap{flex:0 0 auto;}
.cyd-shot-cap{margin-top:8px;min-height:16px;font-size:12px;color:var(--cyd-muted);text-align:center;}
.cyd-shot-cap.err{color:var(--cyd-red);}
.cyd-btn-snap:disabled{opacity:.6;cursor:wait;}
@media(max-width:480px){
  .cyd-preview-hdr{padding:10px 12px 0;}
  .cyd-bezel{padding:10px 10px 12px;}
  .cyd-tab-btn{font-size:11px;padding:8px 2px;}
  .cyd-ctrls{gap:6px;}
  .cyd-btn-switch{flex:1 1 100%;order:1;}
  .cyd-btn-nav{order:2;flex:1 1 28%;padding:8px 6px;font-size:11px;}
}
)rawcss";

static const char CYD_TWIN_HTML[] PROGMEM = R"rawhtml(
<div class='cyd-preview-card'>
  <div class='cyd-preview-hdr'>
    <div class='cyd-title-wrap'>
      <div class='cyd-preview-title'>Live screen mirror</div>
      <span id='cyd-device-badge' class='cyd-badge'>
        <span id='cyd-live-pulse' class='cyd-pulse-dot'></span>On device: Mine
      </span>
    </div>
    <div class='cyd-hdr-right'>
      <button type='button' class='cyd-btn-nav cyd-btn-snap' id='cyd-snap-btn' onclick='cydSnapshot()' title='Grab the current device screen'>Take screenshot</button>
    </div>
  </div>
  <div class='cyd-bezel'>
    <div class='cyd-bezel-lbl' id='cyd-bezel-lbl'>
      240&times;320 <span class='cyd-lbl-tag'>&middot; portrait</span>
    </div>
    <div class='cyd-screen-frame' id='cyd-screen-frame'>
      <img id='cyd-screen-img'
           src='/api/screenshot'
           fetchpriority='high'
           alt='Device screen'
           width='240'
           height='320'>
    </div>
    <div class='cyd-shot-cap'><span id='cyd-shot-ts'></span></div>
    <div class='cyd-tabs' role='tablist'>
      <button type='button' class='cyd-tab-btn active' id='ctab-0' onclick='cydSelectTab(0)'>
        <span class='cyd-live-dot' title='On the device now'></span>Mine
      </button>
      <button type='button' class='cyd-tab-btn' id='ctab-1' onclick='cydSelectTab(1)'>Odds</button>
      <button type='button' class='cyd-tab-btn' id='ctab-2' onclick='cydSelectTab(2)'>World</button>
    </div>
    <div class='cyd-ctrls'>
      <button type='button' class='cyd-btn-nav' onclick='cydPrevScreen()' title='Previous screen'>&larr; Prev</button>
      <button type='button' class='cyd-btn-switch' id='cyd-switch-btn' onclick='cydSwitchPhysical()'>Show Mine on device</button>
      <button type='button' class='cyd-btn-nav' onclick='cydNextScreen()' title='Next screen'>Next &rarr;</button>
      <button type='button' class='cyd-btn-nav' id='cyd-orient-btn' onclick='cydToggleOrientation()' title='Preview is portrait; click to rotate'>Rotate preview</button>
    </div>
  </div>
</div>
)rawhtml";

static const char CYD_TWIN_JS[] PROGMEM = R"rawjs(
let cydPreviewScr = 0;
let cydDevScr = 0;
let cydLastStats = null;
let cydIsPortrait = true;
let cydOrientManual = false; // set once the user picks an orientation by hand
let cydTabSynced = false;    // first stats: line the tabs up with the device
let cydIsLoading = false;
let cydPendingRefresh = false;

const CYD_SCREEN_NAMES = ['Mine', 'Odds', 'World'];
const CYD_SCREEN_COUNT = CYD_SCREEN_NAMES.length;

function cydGetScreenUrl(screenIdx) {
  const s = (typeof screenIdx === 'number') ? screenIdx : cydPreviewScr;
  return '/api/screenshot?screen=' + s + '&t=' + Date.now();
}

const CYD_SNAP_LABEL = 'Take screenshot';
let cydErrTimer = null;

function cydToast(msg, isErr) {
  if (typeof showToast === 'function') showToast(msg, isErr);
}

function cydSetSnapBtn(busy) {
  const btn = document.getElementById('cyd-snap-btn');
  if (!btn) return;
  btn.disabled = busy;
  btn.textContent = busy ? 'Capturing\u2026' : CYD_SNAP_LABEL;
}

function cydSetShotCaption(ok) {
  const ts = document.getElementById('cyd-shot-ts');
  if (!ts) return;
  const cap = ts.parentNode;
  if (cydErrTimer) { clearTimeout(cydErrTimer); cydErrTimer = null; }
  if (ok) {
    ts.textContent = 'Updated ' + new Date().toLocaleTimeString([], { hour12: false });
    if (cap) cap.classList.remove('err');
  } else {
    const prev = ts.textContent;
    ts.textContent = 'Capture failed';
    cydToast('Screenshot failed. Try again.', true);
    if (cap) cap.classList.add('err');
    cydErrTimer = setTimeout(function() {
      ts.textContent = (prev && prev !== 'Capture failed') ? prev : '';
      if (cap) cap.classList.remove('err');
      cydErrTimer = null;
    }, 4000);
  }
}

// One-shot fetch of the device screenshot. Called on button press, tab
// change, and after switching the device's screen -- never on a timer.
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
  cydSetSnapBtn(true);

  const next = new Image();
  const targetUrl = cydGetScreenUrl(cydPreviewScr);

  next.onload = function() {
    img.src = next.src;
    cydIsLoading = false;
    cydSetSnapBtn(false);
    cydSetShotCaption(true);
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
    cydSetSnapBtn(false);
    cydSetShotCaption(false);
    if (pulse) {
      pulse.classList.remove('loading');
      pulse.classList.add('offline');
    }
    cydPendingRefresh = false;
  };

  next.src = targetUrl;
}

// "Take screenshot" button
function cydSnapshot() {
  cydFetchScreenshot(true);
}

function cydHighlightTab() {
  for (let i = 0; i < CYD_SCREEN_COUNT; i++) {
    const el = document.getElementById('ctab-' + i);
    if (el) {
      if (i === cydPreviewScr) el.classList.add('active');
      else el.classList.remove('active');
    }
  }
  const btn = document.getElementById('cyd-switch-btn');
  if (btn && !btn.disabled) btn.textContent = 'Show ' + (CYD_SCREEN_NAMES[cydPreviewScr] || ('screen ' + cydPreviewScr)) + ' on device';
}

function cydSelectTab(idx, fetchNow) {
  if (fetchNow === undefined) fetchNow = true;
  cydPreviewScr = (typeof idx === 'number' && idx >= 0 && idx < CYD_SCREEN_COUNT) ? idx : 0;
  cydHighlightTab();
  if (fetchNow) cydFetchScreenshot(true);
}

// Called with each stats poll. Only the first one moves the tabs (to the
// screen the device is on); the page-load <img> already shows that screen,
// so no extra fetch.
function cydSyncInitialTab() {
  if (cydTabSynced) return;
  cydTabSynced = true;
  if (cydPreviewScr !== cydDevScr) cydSelectTab(cydDevScr, false);
}

function cydPrevScreen() {
  cydSelectTab((cydPreviewScr + CYD_SCREEN_COUNT - 1) % CYD_SCREEN_COUNT);
}

function cydNextScreen() {
  cydSelectTab((cydPreviewScr + 1) % CYD_SCREEN_COUNT);
}

function cydSwitchPhysical() {
  const btn = document.getElementById('cyd-switch-btn');
  if (btn) { btn.disabled = true; btn.textContent = 'Switching\u2026'; }
  fetch('/api/screen?id=' + cydPreviewScr, { method: 'POST' })
    .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
    .then(data => {
      if (data && data.screen !== undefined) {
        cydDevScr = data.screen;
        cydUpdateDevBadge();
        cydToast('Device now showing ' + (CYD_SCREEN_NAMES[cydDevScr] || ('screen ' + cydDevScr)));
      }
    })
    .catch(() => { cydToast('Could not switch the device screen', true); })
    .finally(() => {
      if (btn) btn.disabled = false;
      cydHighlightTab();
      setTimeout(() => cydFetchScreenshot(true), 300);
    });
}

function cydUpdateDevBadge() {
  const b = document.getElementById('cyd-device-badge');
  const scrName = CYD_SCREEN_NAMES[cydDevScr];
  const label = scrName ? scrName : 'Screen ' + cydDevScr;
  if (b) {
    const pulse = document.getElementById('cyd-live-pulse');
    const cls = pulse ? pulse.className : 'cyd-pulse-dot';
    b.innerHTML = "<span id='cyd-live-pulse' class='" + cls + "'></span>On device: " + label;
  }
  for (let i = 0; i < CYD_SCREEN_COUNT; i++) {
    const el = document.getElementById('ctab-' + i);
    if (el) {
      const dot = el.querySelector('.cyd-live-dot');
      if (i === cydDevScr) {
        if (!dot) {
          const s = document.createElement('span');
          s.className = 'cyd-live-dot';
          s.title = 'On the device now';
          el.prepend(s);
        }
      } else if (dot) {
        dot.remove();
      }
    }
  }
}

function cydToggleOrientation() {
  cydOrientManual = true;
  cydIsPortrait = !cydIsPortrait;
  cydRender();
  cydFetchScreenshot(true);
}

function cydRender() {
  const frame = document.getElementById('cyd-screen-frame');
  const img = document.getElementById('cyd-screen-img');
  if (!frame || !img) return;

  const d = cydLastStats || {};
  if (d.is_portrait !== undefined && !cydOrientManual) {
    cydIsPortrait = !!d.is_portrait;
  }

  const lbl = document.getElementById('cyd-bezel-lbl');
  const btnOrient = document.getElementById('cyd-orient-btn');

  if (cydIsPortrait) {
    frame.classList.remove('landscape');
    img.width = 240;
    img.height = 320;
    if (lbl) lbl.innerHTML = '240&times;320 <span class="cyd-lbl-tag">&middot; portrait</span>';
    if (btnOrient) btnOrient.title = 'Preview is portrait; click to rotate';
  } else {
    frame.classList.add('landscape');
    img.width = 320;
    img.height = 240;
    if (lbl) lbl.innerHTML = '320&times;240 <span class="cyd-lbl-tag">&middot; landscape</span>';
    if (btnOrient) btnOrient.title = 'Preview is landscape; click to rotate';
  }
}

function cydInit() {
  cydUpdateDevBadge();
  cydRender();
  const img = document.getElementById('cyd-screen-img');
  if (img) {
    if (img.complete && img.naturalWidth > 0) cydSetShotCaption(true);
    else img.addEventListener('load', function() { cydSetShotCaption(true); }, { once: true });
  }
}

// Initial bootstrap: the <img> loads one screenshot; nothing polls after that.
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', cydInit);
} else {
  cydInit();
}
)rawjs";

static inline void appendCydTwinStyle(String &page) { page += FPSTR(CYD_TWIN_STYLE); }
static inline void appendCydTwinHtml(String &page) { page += FPSTR(CYD_TWIN_HTML); }
static inline void appendCydTwinScript(String &page) { page += FPSTR(CYD_TWIN_JS); }

#endif // WEB_CYD_TWIN_H

#ifndef WEB_CYD_TWIN_H
#define WEB_CYD_TWIN_H

#include <Arduino.h>

static const char CYD_TWIN_STYLE[] PROGMEM = R"rawcss(
.cyd-preview-card{background:transparent;border:none;padding:0;margin-bottom:18px}
.cyd-preview-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;flex-wrap:wrap;gap:8px}
.cyd-preview-title{font-size:13px;font-weight:700;display:flex;align-items:center;gap:8px;color:var(--muted);text-transform:uppercase;letter-spacing:0.5px}
.cyd-bezel{background:#12161f;border:1px solid var(--card-border);border-radius:10px;padding:12px;display:flex;flex-direction:column;align-items:center;position:relative}
.cyd-bezel-lbl{font-size:10px;letter-spacing:1px;color:#8b949e;text-transform:uppercase;margin-bottom:6px;font-weight:600}
#cyd-canvas{width:100%;max-width:480px;height:auto;aspect-ratio:4/3;background:#0b0e14;border:1px solid #30363d;border-radius:4px;image-rendering:pixelated}
.cyd-tabs{display:flex;gap:6px;margin-top:12px;flex-wrap:wrap;justify-content:center;width:100%}
.cyd-tab-btn{background:var(--input-bg);border:1px solid var(--card-border);color:var(--muted);border-radius:6px;padding:5px 10px;font-size:11px;font-weight:600;cursor:pointer;transition:all .15s;display:flex;align-items:center;gap:6px}
.cyd-tab-btn:hover{color:#fff;border-color:var(--amber)}
.cyd-tab-btn.active{background:rgba(247,147,26,0.12);color:var(--amber);border-color:var(--amber)}
.cyd-live-dot{width:6px;height:6px;border-radius:50%;background:#3fb950;display:inline-block;box-shadow:0 0 6px #3fb950}
.cyd-ctrls{display:flex;gap:8px;margin-top:10px;justify-content:space-between;align-items:center;width:100%;max-width:480px;flex-wrap:wrap}
.cyd-btn-switch{background:var(--amber);color:#000;border:none;border-radius:6px;padding:7px 12px;font-size:11px;font-weight:700;cursor:pointer;transition:opacity .15s}
.cyd-btn-switch:hover{opacity:.9}
.cyd-btn-nav{background:var(--input-bg);border:1px solid var(--card-border);color:#fff;border-radius:6px;padding:7px 10px;font-size:11px;cursor:pointer}
)rawcss";

static const char CYD_TWIN_HTML[] PROGMEM = R"rawhtml(
<div class='cyd-preview-card'>
<div class='cyd-preview-hdr'>
<div class='cyd-preview-title'><span>📺 Hardware Screen Twin</span></div>
<div style='display:flex;align-items:center;gap:10px;font-size:12px;color:var(--muted);'>
<label style='display:flex;align-items:center;gap:4px;cursor:pointer;'><input type='checkbox' id='cyd-auto-follow' checked> Follow Device</label>
<span id='cyd-device-badge' style='background:rgba(63,185,80,0.15);color:#3fb950;padding:2px 8px;border-radius:12px;font-weight:600;border:1px solid rgba(63,185,80,0.3);'>Device: SCR 0 (MINE)</span>
</div></div>
<div class='cyd-bezel'>
<div class='cyd-bezel-lbl'>ESP32-2432S024 &bull; 2.4&quot; 320&times;240 SPI TFT</div>
<canvas id='cyd-canvas' width='320' height='240'></canvas>
<div class='cyd-ctrls'>
<button type='button' class='cyd-btn-nav' onclick='cydPrevScreen()'>&larr; Prev</button>
<button type='button' class='cyd-btn-switch' id='cyd-switch-btn' onclick='cydSwitchPhysical()'>📺 Switch Device to Screen 0</button>
<button type='button' class='cyd-btn-nav' onclick='cydNextScreen()'>Next &rarr;</button>
</div>
<div class='cyd-tabs'>
<button type='button' class='cyd-tab-btn active' id='ctab-0' onclick='cydSelectTab(0)'><span class='cyd-live-dot'></span>0 MINE</button>
<button type='button' class='cyd-tab-btn' id='ctab-1' onclick='cydSelectTab(1)'>1 MARKET</button>
<button type='button' class='cyd-tab-btn' id='ctab-2' onclick='cydSelectTab(2)'>2 CLOCK</button>
<button type='button' class='cyd-tab-btn' id='ctab-3' onclick='cydSelectTab(3)'>3 PROTOCOL</button>
</div></div></div>
)rawhtml";

static const char CYD_TWIN_JS[] PROGMEM = R"rawjs(
let cydPreviewScr=0,cydDevScr=0,cydLastStats=null,cydAutoFollow=true;
const CYD_PALETTES=[
{acc:'#f7931a',meter:'#f7931a',track:'#221608',text:'#f0f6fc',dim:'#afb9c8',panel:'#161b22',edge:'#30363d',bg:'#0b0e14',good:'#3fb950',warn:'#d29922',bad:'#f85149',gold:'#ffd700'},
{acc:'#00ff66',meter:'#00ff66',track:'#002810',text:'#e6ffed',dim:'#78eba0',panel:'#0d1b12',edge:'#1b4329',bg:'#040d07',good:'#00ff66',warn:'#d29922',bad:'#f85149',gold:'#ffd700'},
{acc:'#ffaa00',meter:'#ffaa00',track:'#2a1b00',text:'#fff4e5',dim:'#f5be64',panel:'#1a1408',edge:'#3d2b0d',bg:'#0a0702',good:'#3fb950',warn:'#ffaa00',bad:'#f85149',gold:'#ffd700'},
{acc:'#ff007f',meter:'#00f0ff',track:'#1f0033',text:'#ffffff',dim:'#d7afff',panel:'#150a21',edge:'#381559',bg:'#0a0312',good:'#00f0ff',warn:'#ffbf00',bad:'#ff007f',gold:'#ffd700'},
{acc:'#58a6ff',meter:'#58a6ff',track:'#091b33',text:'#f0f6fc',dim:'#a0e1ff',panel:'#0d1829',edge:'#1f3b64',bg:'#040914',good:'#58a6ff',warn:'#d29922',bad:'#f85149',gold:'#ffd700'},
{acc:'#e6edf3',meter:'#e6edf3',track:'#21262d',text:'#f0f6fc',dim:'#c3c3c3',panel:'#161b22',edge:'#30363d',bg:'#0b0e14',good:'#e6edf3',warn:'#d29922',bad:'#f85149',gold:'#ffd700'}
];
function cydGetPalette(th){const idx=(typeof th==='number'&&th>=0&&th<CYD_PALETTES.length)?th:0;return CYD_PALETTES[idx];}
function cydSelectTab(idx,isUserClick=true){
cydPreviewScr=idx;
if(isUserClick){const af=document.getElementById('cyd-auto-follow');if(af&&af.checked&&idx!==cydDevScr)af.checked=false;}
for(let i=0;i<4;i++){const el=document.getElementById('ctab-'+i);if(el)el.className=(i===idx)?'cyd-tab-btn active':'cyd-tab-btn';}
const btn=document.getElementById('cyd-switch-btn');if(btn)btn.textContent='📺 Switch Device to Screen '+idx;
cydRender();
}
function cydPrevScreen(){cydSelectTab((cydPreviewScr+3)%4,true);}
function cydNextScreen(){cydSelectTab((cydPreviewScr+1)%4,true);}
function cydSwitchPhysical(){
fetch('/api/screen?id='+cydPreviewScr,{method:'POST'}).then(r=>r.json()).then(data=>{cydDevScr=data.screen;cydUpdateDevBadge();}).catch(()=>{});
}
function cydUpdateDevBadge(){
const b=document.getElementById('cyd-device-badge');const names=['MINE','MARKET','CLOCK','PROTOCOL'];
if(b)b.textContent='Device: SCR '+cydDevScr+' ('+(names[cydDevScr]||'')+')';
for(let i=0;i<4;i++){
const el=document.getElementById('ctab-'+i);
if(el){
const dot=el.querySelector('.cyd-live-dot');
if(i===cydDevScr){if(!dot){const s=document.createElement('span');s.className='cyd-live-dot';el.prepend(s);}}
else if(dot){dot.remove();}
}}
}
function cydDrawHeader(ctx,p,d){
ctx.fillStyle=p.bg;ctx.fillRect(0,0,320,26);
const screenNames=['MINE','MARKET','CLOCK','PROTOCOL'];
const title=screenNames[cydPreviewScr]||'MINE';
ctx.fillStyle=p.text;ctx.font='bold 12px sans-serif';ctx.fillText(title,10,16);
const tw=ctx.measureText(title).width;
const isHash=d&&(d.status==='hashing');
const stCol=isHash?p.good:(d&&d.status==='connecting'?p.warn:p.dim);
const stTxt=isHash?'HASHING':(d&&d.status==='connecting'?'LINKING':'SETUP');
ctx.fillStyle=stCol;ctx.beginPath();ctx.arc(tw+20,13,3,0,Math.PI*2);ctx.fill();
ctx.font='9px sans-serif';ctx.fillText(stTxt,tw+27,16);
const dotW=7,dotH=3,gap=2,startX=160;
for(let i=0;i<4;i++){
ctx.fillStyle=(i===cydPreviewScr)?p.meter:p.track;
ctx.fillRect(startX+i*(dotW+gap),12,dotW,dotH);
}
const tm=(d&&d.time)?d.time:'12:00:00';
ctx.fillStyle=p.text;ctx.font='11px sans-serif';ctx.textAlign='right';ctx.fillText(tm,314,16);ctx.textAlign='left';
ctx.strokeStyle=p.edge;ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(10,26);ctx.lineTo(314,26);ctx.stroke();
}
function cydDrawPanel(ctx,p,x,y,w,h,r=6){
ctx.fillStyle=p.panel;ctx.strokeStyle=p.edge;ctx.lineWidth=1;
ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fill();ctx.stroke();
}
function cydDrawTile(ctx,p,x,y,w,h,lbl,val,valCol){
cydDrawPanel(ctx,p,x,y,w,h,4);
ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText(lbl,x+6,y+13);
ctx.fillStyle=valCol||p.text;ctx.font='bold 14px sans-serif';ctx.fillText(val,x+6,y+27);
}
function cydDrawStrip(ctx,p,cells){
cydDrawPanel(ctx,p,8,198,304,38,6);
ctx.strokeStyle=p.edge;
for(let i=1;i<4;i++){ctx.beginPath();ctx.moveTo(8+i*76,204);ctx.lineTo(8+i*76,230);ctx.stroke();}
for(let i=0;i<4;i++){
const c=cells[i]||{l:'',v:''};const cx=14+i*76;
ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText(c.l,cx,211);
ctx.fillStyle=p.text;ctx.font='bold 14px sans-serif';ctx.fillText(c.v,cx,228);
}
}
function cydDrawMiniArc(ctx,p,cx,cy,r,ri,val,warnAt,critAt,invert=false){
const startAng=150*(Math.PI/180),sweep=240*(Math.PI/180);
ctx.lineWidth=r-ri;ctx.lineCap='butt';
ctx.strokeStyle=p.track;ctx.beginPath();ctx.arc(cx,cy,(r+ri)/2,startAng,startAng+sweep);ctx.stroke();
let col=p.good;
if(!invert)col=(val>=critAt)?p.bad:(val>=warnAt?p.warn:p.good);
else col=(val<=(1-critAt))?p.bad:(val<=(1-warnAt)?p.warn:p.good);
if(val>0.01){
ctx.strokeStyle=col;ctx.beginPath();ctx.arc(cx,cy,(r+ri)/2,startAng,startAng+sweep*Math.min(val,1));ctx.stroke();
}
}
function cydRender(){
const cvs=document.getElementById('cyd-canvas');if(!cvs)return;
const ctx=cvs.getContext('2d');const d=cydLastStats||{};const p=cydGetPalette(d.theme);
ctx.fillStyle=p.bg;ctx.fillRect(0,0,320,240);
cydDrawHeader(ctx,p,d);
if(cydPreviewScr===0){
const hr=parseFloat(d.hashrate)||0;
const gCx=92,gCy=110,gR=76,gRi=66,segs=30;
const valFrac=Math.min(hr/1200,1.0),filledSegs=Math.round(valFrac*segs);
for(let i=0;i<segs;i++){
const a0=(45+i*9)*(Math.PI/180),a1=(45+i*9+7)*(Math.PI/180);
let col=p.track;
if(i<filledSegs){col=(i>=27)?p.bad:(i>=24?p.warn:p.meter);}
ctx.lineWidth=gR-gRi;ctx.strokeStyle=col;ctx.beginPath();ctx.arc(gCx,gCy,(gR+gRi)/2,a0,a1);ctx.stroke();
}
ctx.textAlign='center';ctx.fillStyle=p.text;ctx.font='bold 22px sans-serif';
ctx.fillText(hr>0?hr.toFixed(1):'0.0',gCx,gCy+4);
ctx.font='9px sans-serif';ctx.fillStyle=p.dim;ctx.fillText('KH/s',gCx,gCy+18);ctx.textAlign='left';
cydDrawTile(ctx,p,196,32,116,36,'SHARES',(d.shares||0).toLocaleString(),p.text);
cydDrawTile(ctx,p,196,72,116,36,'BEST DIFF',d.best_diff||'0.000',p.acc);
cydDrawTile(ctx,p,196,112,116,36,'TEMPLATES','4,210',p.text);
cydDrawTile(ctx,p,196,152,116,36,'BLOCKS FOUND',String(d.blocks||0),p.good);
cydDrawStrip(ctx,p,[{l:'WORKERS',v:'1'},{l:'POOL RATE',v:hr>0?(hr.toFixed(0)+' KH/s'):'--'},{l:'POOL BEST',v:d.best_diff||'--'},{l:'UPTIME',v:d.uptime||'--'}]);
}else if(cydPreviewScr===1){
cydDrawPanel(ctx,p,8,30,304,52,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('BTC / USDT • BINANCE',16,42);
ctx.fillStyle=p.good;ctx.font='bold 9px sans-serif';ctx.fillText('+3.62% 24h',140,42);
ctx.fillStyle=p.gold;ctx.font='bold 22px sans-serif';ctx.fillText('$68,820',16,68);
ctx.fillStyle=p.text;ctx.font='bold 14px sans-serif';ctx.textAlign='right';ctx.fillText('#884,120',302,52);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('12 sat/vB fee',302,68);ctx.textAlign='left';
cydDrawPanel(ctx,p,8,86,304,68,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('BTC/USDT 1H CANDLES',14,98);
ctx.fillStyle=p.gold;ctx.font='8px sans-serif';ctx.textAlign='right';ctx.fillText('H: 69.1K',302,98);
ctx.fillStyle=p.dim;ctx.textAlign='left';ctx.fillText('L: 66.0K',14,148);
const cX=14,cY=102,cW=292,cH=44,nC=22;
for(let i=0;i<nC;i++){
const isBull=i%3!==0;const cCol=isBull?p.good:p.bad;const step=cW/nC;const cpx=cX+i*step+step/2;
const bh=Math.max(4,Math.abs(Math.sin(i*0.4)*26));const by=cY+cH/2-bh/2;
ctx.strokeStyle=cCol;ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(cpx,by-4);ctx.lineTo(cpx,by+bh+4);ctx.stroke();
ctx.fillStyle=cCol;ctx.fillRect(cpx-3,by,6,bh);
}
ctx.strokeStyle=p.gold;ctx.lineWidth=1.5;ctx.beginPath();
for(let i=0;i<nC;i++){
const step=cW/nC;const cpx=cX+i*step+step/2;const my=cY+cH/2-Math.sin(i*0.35)*10;
if(i===0)ctx.moveTo(cpx,my);else ctx.lineTo(cpx,my);
}
ctx.stroke();
cydDrawPanel(ctx,p,8,158,304,36,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('HALVING #5 PROGRESS',14,170);
ctx.fillStyle=p.meter;ctx.font='bold 9px sans-serif';ctx.fillText('78%',110,170);
ctx.fillStyle=p.text;ctx.textAlign='right';ctx.fillText('42,100 blk left',302,170);ctx.textAlign='left';
ctx.fillStyle=p.track;ctx.fillRect(14,176,292,6);ctx.fillStyle=p.meter;ctx.fillRect(14,176,292*0.78,6);
cydDrawStrip(ctx,p,[{l:'PAIR',v:'BTC/USDT'},{l:'INTERVAL',v:'1 HOUR'},{l:'POOL',v:'public-pool'},{l:'TIME',v:d.time||'--'}]);
}else if(cydPreviewScr===2){
cydDrawPanel(ctx,p,8,30,188,56,6);
ctx.fillStyle=p.text;ctx.font='bold 36px monospace';
const tm2=(d.time||'12:00').substring(0,5);
ctx.fillText(tm2,16,70);
ctx.font='9px sans-serif';ctx.fillStyle=p.dim;ctx.fillText('SUN 12 OCT 2026',120,68);
cydDrawTile(ctx,p,202,30,110,56,'BEST DIFF',d.best_diff||'0.000',p.acc);
cydDrawPanel(ctx,p,8,92,304,98,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('ROLLING 48-SLOT HASHRATE HISTORY (8 MIN)',16,104);
const barsX=16,barsY=112,barW=4,barGap=2;
for(let i=0;i<46;i++){
const bh=Math.max(4,Math.sin((i+5)*0.4)*20+35);
ctx.fillStyle=p.meter;ctx.fillRect(barsX+i*(barW+barGap),barsY+68-bh,barW,bh);
}
cydDrawStrip(ctx,p,[{l:'WORKERS',v:'1'},{l:'POOL RATE',v:d.hashrate?(parseFloat(d.hashrate).toFixed(0)+' KH/s'):'--'},{l:'UPTIME',v:d.uptime||'--'},{l:'TEMP',v:(d.temp||'--')+'°C'}]);
}else if(cydPreviewScr===3){
cydDrawPanel(ctx,p,8,30,304,48,6);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText('BLOCK REWARD JACKPOT',16,42);
ctx.fillStyle=p.gold;ctx.font='bold 22px sans-serif';ctx.fillText('3.125 BTC',16,64);
ctx.font='10px sans-serif';ctx.fillStyle=p.dim;ctx.fillText('+ fees (~$300,000+ USD)',126,64);
ctx.fillStyle=p.good;ctx.fillText('SOLO: 100% TO YOU',220,54);

cydDrawPanel(ctx,p,8,82,304,52,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('80-BYTE CANDIDATE HEADER',14,92);
ctx.fillStyle=p.text;ctx.font='8px monospace';
ctx.fillText('VER 4B [02000000]   PREV 32B [00000000...9717]',14,105);
ctx.fillText('MERKLE 32B [8717...978a]  TIME 4B [53059335]',14,116);
ctx.fillStyle=p.acc;
ctx.fillText('BITS 4B [19015f53]  NONCE 4B [DIAL: 0x330776]',14,127);

const pipeY=138;const stages=['Header 80B','SHA256d','Target Zeros','H < Target'];
for(let i=0;i<4;i++){
const px=8+i*78;cydDrawPanel(ctx,p,px,pipeY,72,28,4);
ctx.fillStyle=(i===2)?p.meter:((i===3)?p.good:p.dim);
ctx.font='bold 8px sans-serif';ctx.textAlign='center';ctx.fillText(stages[i],px+36,pipeY+18);ctx.textAlign='left';
}
cydDrawTile(ctx,p,8,170,148,24,'GLOBAL TARGET',d.net_diff?(parseFloat(d.net_diff)/1e12).toFixed(2)+' T':'132.72 T',p.text);
cydDrawTile(ctx,p,164,170,148,24,'HARD CAP SUPPLY','20,999,999.9769 BTC',p.gold);
cydDrawStrip(ctx,p,[{l:'WORKERS',v:'1'},{l:'POOL RATE',v:d.hashrate?(parseFloat(d.hashrate).toFixed(0)+' KH/s'):'--'},{l:'POOL BEST',v:d.best_diff||'0.000'},{l:'ODDS',v:'1/70M'}]);
}
}
)rawjs";

static inline void appendCydTwinStyle(String &page) { page += FPSTR(CYD_TWIN_STYLE); }
static inline void appendCydTwinHtml(String &page) { page += FPSTR(CYD_TWIN_HTML); }
static inline void appendCydTwinScript(String &page) { page += FPSTR(CYD_TWIN_JS); }

#endif // WEB_CYD_TWIN_H

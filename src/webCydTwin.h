#ifndef WEB_CYD_TWIN_H
#define WEB_CYD_TWIN_H

#include <Arduino.h>

static const char CYD_TWIN_STYLE[] PROGMEM = R"rawcss(
.cyd-preview-card{background:transparent;border:none;padding:0;margin-bottom:18px}
.cyd-preview-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;flex-wrap:wrap;gap:8px}
.cyd-preview-title{font-size:13px;font-weight:700;display:flex;align-items:center;gap:8px;color:var(--muted);text-transform:uppercase;letter-spacing:0.5px}
.cyd-bezel{background:#12161f;border:1px solid var(--card-border);border-radius:10px;padding:14px;display:flex;flex-direction:column;align-items:center;position:relative}
.cyd-bezel-lbl{font-size:10px;letter-spacing:1px;color:#8b949e;text-transform:uppercase;margin-bottom:10px;font-weight:600}
#cyd-canvas{width:100%;max-width:270px;height:auto;aspect-ratio:3/4;background:#0b0e14;border:1px solid #30363d;border-radius:6px;image-rendering:pixelated;box-shadow:0 6px 18px rgba(0,0,0,0.4);transition:max-width .2s,aspect-ratio .2s}
#cyd-canvas.landscape{max-width:440px;aspect-ratio:4/3}
.cyd-tabs{display:flex;gap:6px;margin-top:12px;flex-wrap:wrap;justify-content:center;width:100%}
.cyd-tab-btn{background:var(--input-bg);border:1px solid var(--card-border);color:var(--muted);border-radius:6px;padding:5px 10px;font-size:11px;font-weight:600;cursor:pointer;transition:all .15s;display:flex;align-items:center;gap:6px}
.cyd-tab-btn:hover{color:#fff;border-color:var(--amber)}
.cyd-tab-btn.active{background:rgba(247,147,26,0.12);color:var(--amber);border-color:var(--amber)}
.cyd-live-dot{width:6px;height:6px;border-radius:50%;background:#3fb950;display:inline-block;box-shadow:0 0 6px #3fb950}
.cyd-ctrls{display:flex;gap:8px;margin-top:12px;justify-content:center;align-items:center;width:100%;max-width:480px;flex-wrap:wrap}
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
<div class='cyd-bezel-lbl' id='cyd-bezel-lbl'>ESP32-2432S024 &bull; 2.4&quot; 240&times;320 SPI TFT (PORTRAIT)</div>
<canvas id='cyd-canvas' width='240' height='320'></canvas>
<div class='cyd-ctrls'>
<button type='button' class='cyd-btn-nav' onclick='cydPrevScreen()'>&larr; Prev</button>
<button type='button' class='cyd-btn-switch' id='cyd-switch-btn' onclick='cydSwitchPhysical()'>📺 Switch Device to Screen 0</button>
<button type='button' class='cyd-btn-nav' onclick='cydNextScreen()'>Next &rarr;</button>
<button type='button' class='cyd-btn-nav' id='cyd-orient-btn' onclick='cydToggleOrientation()'>🔄 Portrait</button>
</div>
<div class='cyd-tabs'>
<button type='button' class='cyd-tab-btn active' id='ctab-0' onclick='cydSelectTab(0)'><span class='cyd-live-dot'></span>0 MINE</button>
<button type='button' class='cyd-tab-btn' id='ctab-1' onclick='cydSelectTab(1)'>1 MARKET</button>
<button type='button' class='cyd-tab-btn' id='ctab-2' onclick='cydSelectTab(2)'>2 CLOCK</button>
<button type='button' class='cyd-tab-btn' id='ctab-3' onclick='cydSelectTab(3)'>3 PROTOCOL</button>
</div></div></div>
)rawhtml";

static const char CYD_TWIN_JS[] PROGMEM = R"rawjs(
let cydPreviewScr=0,cydDevScr=0,cydLastStats=null,cydAutoFollow=true,cydIsPortrait=true;
const CYD_PALETTES=[
{acc:'#f7931a',meter:'#f7931a',track:'#221608',text:'#f0f6fc',dim:'#afb9c8',panel:'#161b22',edge:'#30363d',bg:'#0b0e14',good:'#3fb950',warn:'#d29922',bad:'#f85149',gold:'#ffd700'},
{acc:'#00ff66',meter:'#00ff66',track:'#002810',text:'#e6ffed',dim:'#78eba0',panel:'#0d1b12',edge:'#1b4329',bg:'#040d07',good:'#00ff66',warn:'#d29922',bad:'#f85149',gold:'#ffd700'},
{acc:'#ffaa00',meter:'#ffaa00',track:'#2a1b00',text:'#fff4e5',dim:'#f5be64',panel:'#1a1408',edge:'#3d2b0d',bg:'#0a0702',good:'#3fb950',warn:'#ffaa00',bad:'#f85149',gold:'#ffd700'},
{acc:'#ff007f',meter:'#00f0ff',track:'#1f0033',text:'#ffffff',dim:'#d7afff',panel:'#150a21',edge:'#381559',bg:'#0a0312',good:'#00f0ff',warn:'#ffbf00',bad:'#ff007f',gold:'#ffd700'},
{acc:'#58a6ff',meter:'#58a6ff',track:'#091b33',text:'#f0f6fc',dim:'#a0e1ff',panel:'#0d1829',edge:'#1f3b64',bg:'#040914',good:'#58a6ff',warn:'#d29922',bad:'#f85149',gold:'#ffd700'},
{acc:'#e6edf3',meter:'#e6edf3',track:'#21262d',text:'#f0f6fc',dim:'#c3c3c3',panel:'#161b22',edge:'#30363d',bg:'#0b0e14',good:'#e6edf3',warn:'#d29922',bad:'#f85149',gold:'#ffd700'}
];

function cydGetPalette(th){const idx=(typeof th==='number'&&th>=0&&th<CYD_PALETTES.length)?th:0;return CYD_PALETTES[idx];}
function cydToggleOrientation(){cydIsPortrait=!cydIsPortrait;cydRender();}
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
function cydDrawPanel(ctx,p,x,y,w,h,r=6){
ctx.fillStyle=p.panel;ctx.strokeStyle=p.edge;ctx.lineWidth=1;
ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fill();ctx.stroke();
}
function cydDrawTile(ctx,p,x,y,w,h,lbl,val,valCol){
cydDrawPanel(ctx,p,x,y,w,h,4);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText(lbl,x+6,y+13);
ctx.fillStyle=valCol||p.text;ctx.font='bold 13px sans-serif';ctx.fillText(val,x+6,y+28);
}
function cydDrawCoin(ctx,p,cx,cy,r){
ctx.fillStyle=p.gold;ctx.beginPath();ctx.arc(cx,cy,r,0,Math.PI*2);ctx.fill();
ctx.strokeStyle='#feaa00';ctx.lineWidth=2;ctx.stroke();
ctx.fillStyle='#994d00';ctx.beginPath();ctx.arc(cx,cy,r-3,0,Math.PI*2);ctx.fill();
ctx.fillStyle='#fff';ctx.font='bold '+(r+2)+'px sans-serif';ctx.textAlign='center';ctx.textBaseline='middle';
ctx.fillText('₿',cx,cy+1);ctx.textAlign='left';ctx.textBaseline='alphabetic';
}
function cydDrawArrow(ctx,p,x,y,s,col){
ctx.fillStyle=col||p.acc;ctx.beginPath();ctx.moveTo(x,y-s);ctx.lineTo(x+s*1.2,y);ctx.lineTo(x,y+s);ctx.closePath();ctx.fill();
}

function cydDrawHeaderPortrait(ctx,p,d){
ctx.fillStyle=p.bg;ctx.fillRect(0,0,240,26);
const screenNames=['MINE','MARKET','CLOCK','PROTOCOL'];
const title=screenNames[cydPreviewScr]||'MINE';
ctx.fillStyle=p.text;ctx.font='bold 11px sans-serif';ctx.fillText(title,8,16);
const tw=ctx.measureText(title).width;
const isHash=d&&(d.status==='hashing');
const stCol=isHash?p.good:(d&&d.status==='connecting'?p.warn:p.dim);
const stTxt=isHash?'HASHING':(d&&d.status==='connecting'?'LINKING':'SETUP');
ctx.fillStyle=stCol;ctx.font='bold 9px sans-serif';ctx.fillText(stTxt,tw+14,16);
const dotW=6,dotH=3,gap=2,startX=142;
for(let i=0;i<4;i++){
ctx.fillStyle=(i===cydPreviewScr)?p.meter:p.track;
ctx.fillRect(startX+i*(dotW+gap),12,dotW,dotH);
}
const tm=(d&&d.time)?d.time:'12:00:00';
ctx.fillStyle=p.text;ctx.font='10px sans-serif';ctx.textAlign='right';ctx.fillText(tm,234,16);ctx.textAlign='left';
ctx.strokeStyle=p.edge;ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(6,26);ctx.lineTo(234,26);ctx.stroke();
}

function cydFmtK(pre,v){return pre+(v>=10000?'$'+(v/1000).toFixed(1)+'K':'$'+Math.round(v));}
function cydPoolLbl(h){h=h||'';const i=h.lastIndexOf('.');if(i>0)h=h.substring(0,i);if(h.startsWith('pool.'))h=h.substring(5);return (h.substring(0,9))||'--';}
function cydMkt(d){const m=(d&&d.market)||{};const pre=m.prefix||'$';const rem=m.rem||0;
return {price:m.price?pre+Number(m.price).toLocaleString('en-US'):'--',chg:Number(m.chg||0),block:m.block?Number(m.block).toLocaleString('en-US'):'--',
fee:m.fee?m.fee+' sat/vB':'--',diff:m.diff||'--',net:m.net?m.net+' EH/s':'--',rem:rem,pct:rem?Math.floor((210000-rem)*100/210000):0,
pool:cydPoolLbl(m.pool),iv:m.iv||'1H',k:Array.isArray(m.k)?m.k:[],time:(d&&d.time?d.time:'--:--').substring(0,5)};}
/* Mirrors drawCandlestickChart() in esp23_2432s028r.cpp; k rows are [o,h,l,c] */
function cydDrawCandles(ctx,p,x,y,w,h,k){
ctx.fillStyle=p.panel;ctx.fillRect(x,y,w,h);ctx.textBaseline='top';
ctx.font='9px sans-serif';ctx.fillStyle=p.dim;ctx.textAlign='left';ctx.fillText('BTC/USDT 1H',x+4,y+2);
ctx.fillStyle=p.gold;ctx.textAlign='center';ctx.fillText('MA7',x+w/2,y+2);
const pX=x+4,pY=y+14,pW=w-8,pH=h-26;ctx.strokeStyle=p.track;ctx.lineWidth=1;
for(let i=0;i<=3;i++){const gy=Math.round(pY+pH*i/3)+0.5;ctx.beginPath();ctx.moveTo(pX,gy);ctx.lineTo(pX+pW,gy);ctx.stroke();}
const n=Math.min(k.length,24);
if(!n){ctx.fillStyle=p.dim;ctx.fillText('WAITING FOR MARKET DATA',x+w/2,pY+pH/2-5);ctx.textAlign='left';ctx.textBaseline='alphabetic';return;}
let lo=k[0][2],hi=k[0][1];for(let i=1;i<n;i++){lo=Math.min(lo,k[i][2]);hi=Math.max(hi,k[i][1]);}
const rLo=lo,rHi=hi;let pad=Math.max((hi-lo)*0.05,1);lo-=pad;hi+=pad;const rng=hi-lo;
const py=v=>Math.min(pY+pH,Math.max(pY,Math.round(pY+pH-((v-lo)/rng)*pH)));
const step=pW/n,cw=Math.max(2,Math.floor(step)-2);const ma=[];
for(let i=0;i<n;i++){const c=k[i];let s=0,m=0;for(let j=Math.max(0,i-6);j<=i;j++,m++)s+=k[j][3];ma.push(s/m);
const col=c[3]>=c[0]?p.good:p.bad;const cx=Math.floor(pX+i*step+step/2);
ctx.fillStyle=col;const yh=py(c[1]),yl=py(c[2]);ctx.fillRect(cx,yh,1,Math.max(1,yl-yh));
const yo=py(c[0]),yc=py(c[3]);ctx.fillRect(cx-Math.floor(cw/2),Math.min(yo,yc),cw,Math.max(1,Math.abs(yc-yo)));}
ctx.strokeStyle=p.gold;ctx.lineWidth=1;ctx.beginPath();
for(let i=0;i<n;i++){const cx=Math.floor(pX+i*step+step/2)+0.5;if(i)ctx.lineTo(cx,py(ma[i]));else ctx.moveTo(cx,py(ma[i]));}ctx.stroke();
ctx.fillStyle=p.good;ctx.textAlign='right';ctx.fillText(cydFmtK('H ',rHi),x+w-4,y+2);
ctx.fillStyle=p.text;ctx.fillText(cydFmtK('',k[n-1][3]),x+w-4,y+h-11);
ctx.fillStyle=p.bad;ctx.textAlign='left';ctx.fillText(cydFmtK('L ',rLo),x+4,y+h-11);
ctx.textBaseline='alphabetic';}
function cydDrawStripPortrait(ctx,p,cells){
cydDrawPanel(ctx,p,6,266,228,48,8);
ctx.strokeStyle=p.edge;
for(let i=1;i<4;i++){ctx.beginPath();ctx.moveTo(6+i*57,272);ctx.lineTo(6+i*57,308);ctx.stroke();}
for(let i=0;i<4;i++){
const c=cells[i]||{l:'',v:''};const cx=9+i*57;
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText(c.l,cx,280);
ctx.fillStyle=p.text;ctx.font='bold 12px sans-serif';ctx.fillText(c.v,cx,298);
}
}

function cydRenderPortrait(ctx,p,d){
cydDrawHeaderPortrait(ctx,p,d);
const hr=parseFloat(d.hashrate)||0;
if(cydPreviewScr===0){
const gCx=120,gCy=104,gR=70,gRi=60,segs=30;
const valFrac=Math.min(hr/1200,1.0),filledSegs=Math.round(valFrac*segs);
const baseOffset=90*(Math.PI/180);
for(let i=0;i<segs;i++){
const a0=baseOffset+(45+i*9)*(Math.PI/180),a1=baseOffset+(45+i*9+7)*(Math.PI/180);
let col=(i>=27)?'#3a1010':p.track;
if(i<filledSegs){col=(i>=27)?p.bad:(i>=24?p.warn:p.meter);}
ctx.lineWidth=gR-gRi;ctx.strokeStyle=col;ctx.beginPath();ctx.arc(gCx,gCy,(gR+gRi)/2,a0,a1);ctx.stroke();
}
ctx.textAlign='center';ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('HASHRATE',gCx,gCy-16);
ctx.fillStyle=p.text;ctx.font='bold 24px sans-serif';ctx.fillText(hr>0?hr.toFixed(1):'0.0',gCx,gCy+8);
ctx.font='10px sans-serif';ctx.fillStyle=p.dim;ctx.fillText('KH/s',gCx,gCy+22);
const mhVal=d.mhashes?Number(d.mhashes).toLocaleString():'143,274';
ctx.font='10px sans-serif';ctx.fillStyle=p.dim;ctx.fillText(mhVal+' MH TOTAL',gCx,160);ctx.textAlign='left';

cydDrawTile(ctx,p,6,178,112,38,'SHARES',(d.shares||0).toLocaleString(),p.text);
cydDrawTile(ctx,p,122,178,112,38,'BEST DIFF',d.best_diff||'0.000',p.acc);
cydDrawTile(ctx,p,6,222,112,38,'TEMPLATES','4,210',p.text);
cydDrawTile(ctx,p,122,222,112,38,'BLOCKS FOUND',String(d.blocks||0),p.good);

cydDrawStripPortrait(ctx,p,[
{l:'WORKERS',v:'1'},
{l:'POOL',v:hr>0?(hr.toFixed(0)+' KH/s'):'--'},
{l:'BEST',v:d.best_diff||'--'},
{l:'UPTIME',v:d.uptime?(d.uptime.trim().split(/\s+/)[0]+'d'):'--'}
]);
}else if(cydPreviewScr===1){
const m=cydMkt(d);const up=m.chg>=0;
cydDrawPanel(ctx,p,6,30,228,54,8);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText('BTC / USDT',14,43);
ctx.fillStyle=p.gold;ctx.font='bold 22px sans-serif';ctx.fillText(m.price,14,74);
ctx.textAlign='right';ctx.fillStyle=up?p.good:p.bad;ctx.font='bold 9px sans-serif';ctx.fillText((up?'+':'')+m.chg.toFixed(2)+'% 24h',224,53);
ctx.fillStyle=p.text;ctx.font='bold 11px sans-serif';ctx.fillText(m.block,224,67);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText(m.fee+' fee',224,79);ctx.textAlign='left';
cydDrawPanel(ctx,p,6,88,228,118,8);
cydDrawCandles(ctx,p,10,92,220,110,m.k);
cydDrawPanel(ctx,p,6,210,228,50,8);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText('HALVING #5 PROGRESS',14,224);
ctx.fillStyle=p.meter;ctx.fillText(m.pct+'%',128,224);
ctx.textAlign='right';ctx.fillStyle=p.text;ctx.fillText(m.rem.toLocaleString('en-US')+' blk left',224,224);ctx.textAlign='left';
ctx.fillStyle=p.track;ctx.fillRect(14,226,212,6);ctx.fillStyle=p.meter;ctx.fillRect(14,226,212*m.pct/100,6);
ctx.fillStyle=p.gold;ctx.font='9px sans-serif';ctx.fillText('Diff: '+m.diff,14,251);
ctx.textAlign='right';ctx.fillStyle=p.good;ctx.fillText('Net: '+m.net,224,251);ctx.textAlign='left';
cydDrawStripPortrait(ctx,p,[{l:'PAIR',v:'BTC/USDT'},{l:'INTERVAL',v:m.iv},{l:'POOL',v:m.pool},{l:'TIME',v:m.time}]);
}else if(cydPreviewScr===2){
cydDrawPanel(ctx,p,6,30,228,82,8);
ctx.textAlign='center';ctx.fillStyle=p.text;ctx.font='bold 42px monospace';
const tm2=(d&&d.time?d.time:'12:00').substring(0,5);
ctx.fillText(tm2,120,74);
ctx.font='10px sans-serif';ctx.fillStyle=p.dim;ctx.fillText('SUN 12 OCT 2026',120,98);ctx.textAlign='left';

cydDrawTile(ctx,p,6,118,112,44,'BTC / USD','$68,420',p.text);
cydDrawTile(ctx,p,122,118,112,44,'BLOCK','#884,120',p.text);
cydDrawTile(ctx,p,6,168,228,42,'HASHRATE KH/s',(hr>0?hr.toFixed(1):'850.4'),p.good);

cydDrawPanel(ctx,p,6,214,228,46,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('ROLLING 36-SLOT HASHRATE HISTORY',12,225);
ctx.strokeStyle=p.track;ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(12,255);ctx.lineTo(228,255);ctx.stroke();

const barsX=14,barW=4,barGap=2;
for(let i=0;i<35;i++){
const bh=Math.max(3,Math.sin((i+3)*0.45)*8+14);
ctx.fillStyle=p.meter;ctx.fillRect(barsX+i*(barW+barGap),255-bh,barW,bh);
}

cydDrawStripPortrait(ctx,p,[
{l:'WORKERS',v:'1'},
{l:'POOL',v:hr>0?(hr.toFixed(0)+'K'):'--'},
{l:'UPTIME',v:d.uptime?(d.uptime.trim().split(/\s+/)[0]+'d'):'--'},
{l:'TEMP',v:(d.temp||'52')+'°C'}
]);
}else if(cydPreviewScr===3){
cydDrawPanel(ctx,p,6,30,228,54,8);
cydDrawCoin(ctx,p,30,57,18);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText('SOLO BLOCK REWARD',58,42);
ctx.fillStyle=p.gold;ctx.font='bold 20px sans-serif';ctx.fillText('3.125 BTC',58,62);
ctx.fillStyle='rgba(34,197,94,0.15)';ctx.strokeStyle=p.good;ctx.lineWidth=1;
ctx.beginPath();ctx.roundRect(58,68,124,12,3);ctx.fill();ctx.stroke();
ctx.fillStyle=p.good;ctx.font='bold 8px sans-serif';ctx.fillText('★ 100% TO YOUR WALLET',62,77);

cydDrawPanel(ctx,p,6,88,228,58,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('80-BYTE CANDIDATE HEADER',12,98);
const bx=12,by=104;
ctx.fillStyle='#38bdf8';ctx.fillRect(bx,by,12,16);ctx.fillStyle='#fff';ctx.font='7px sans-serif';ctx.fillText('V4',bx+1,by+11);
ctx.fillStyle='#64748b';ctx.fillRect(bx+13,by,84,16);ctx.fillStyle='#fff';ctx.fillText('PREV HASH 32B',bx+16,by+11);
ctx.fillStyle='#a855f7';ctx.fillRect(bx+98,by,84,16);ctx.fillStyle='#fff';ctx.fillText('MERKLE ROOT 32B',bx+102,by+11);
ctx.fillStyle='#eab308';ctx.fillRect(bx+183,by,10,16);ctx.fillStyle='#000';ctx.fillText('T',bx+185,by+11);
ctx.fillStyle='#ef4444';ctx.fillRect(bx+194,by,10,16);ctx.fillStyle='#fff';ctx.fillText('B',bx+196,by+11);
ctx.fillStyle=p.gold;ctx.fillRect(bx+205,by,11,16);ctx.fillStyle='#000';ctx.fillText('N',bx+207,by+11);

cydDrawPanel(ctx,p,12,124,216,18,3);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('76B Template  +',16,136);
ctx.fillStyle=p.gold;ctx.font='bold 9px sans-serif';ctx.fillText('NONCE DIAL: 0x330776',105,136);

cydDrawPanel(ctx,p,6,148,228,50,6);
const py=154;
cydDrawPanel(ctx,p,10,py,42,38,4);
ctx.fillStyle='#38bdf8';ctx.font='bold 9px sans-serif';ctx.textAlign='center';ctx.fillText('80B',31,py+14);
ctx.fillStyle=p.dim;ctx.font='7px sans-serif';ctx.fillText('HEADER',31,py+28);
cydDrawArrow(ctx,p,55,py+19,4,p.meter);

cydDrawPanel(ctx,p,64,py,42,38,4);
ctx.fillStyle=p.meter;ctx.font='bold 8px sans-serif';ctx.fillText('SHA256',85,py+14);
ctx.fillStyle=p.dim;ctx.font='7px sans-serif';ctx.fillText('DOUBLE',85,py+28);
cydDrawArrow(ctx,p,109,py+19,4,p.meter);

cydDrawPanel(ctx,p,118,py,42,38,4);
ctx.fillStyle=p.gold;ctx.font='bold 8px sans-serif';ctx.fillText('TARGET',139,py+14);
ctx.fillStyle=p.dim;ctx.font='7px sans-serif';ctx.fillText('18+ 0s',139,py+28);
cydDrawArrow(ctx,p,163,py+19,4,p.meter);

cydDrawPanel(ctx,p,172,py,46,38,4);
ctx.fillStyle=p.good;ctx.font='bold 8px sans-serif';ctx.fillText('H < TGT',195,py+14);
ctx.fillText('WINNER',195,py+28);ctx.textAlign='left';

const nd=(d&&d.net_diff)?(Number(d.net_diff)/1e12).toFixed(0)+'+ T':'132+ T';
cydDrawTile(ctx,p,6,202,112,58,'GLOBAL TARGET',nd,p.text);
cydDrawPanel(ctx,p,122,202,112,58,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('HARD CAP SUPPLY',128,214);
ctx.fillStyle=p.gold;ctx.font='bold 13px sans-serif';ctx.fillText('20,999,999',128,230);
ctx.fillStyle=p.track;ctx.fillRect(128,238,100,4);ctx.fillStyle=p.gold;ctx.fillRect(128,238,94,4);
ctx.fillStyle=p.dim;ctx.font='7px sans-serif';ctx.fillText('94.3% Mined • 21M Cap',128,252);

cydDrawStripPortrait(ctx,p,[
{l:'WORKERS',v:'1'},
{l:'POOL',v:hr>0?(hr.toFixed(0)+' KH/s'):'--'},
{l:'BEST',v:d.best_diff||'--'},
{l:'ODDS',v:'1/70M'}
]);
}
}

function cydDrawHeaderLandscape(ctx,p,d){
ctx.fillStyle=p.bg;ctx.fillRect(0,0,320,26);
const screenNames=['MINE','MARKET','CLOCK','PROTOCOL'];
const title=screenNames[cydPreviewScr]||'MINE';
ctx.fillStyle=p.text;ctx.font='bold 12px sans-serif';ctx.fillText(title,10,16);
const tw=ctx.measureText(title).width;
const isHash=d&&(d.status==='hashing');
const stCol=isHash?p.good:(d&&d.status==='connecting'?p.warn:p.dim);
const stTxt=isHash?'HASHING':(d&&d.status==='connecting'?'LINKING':'SETUP');
ctx.fillStyle=stCol;ctx.font='bold 9px sans-serif';ctx.fillText(stTxt,tw+14,16);
const dotW=7,dotH=3,gap=2,startX=160;
for(let i=0;i<4;i++){
ctx.fillStyle=(i===cydPreviewScr)?p.meter:p.track;
ctx.fillRect(startX+i*(dotW+gap),12,dotW,dotH);
}
const tm=(d&&d.time)?d.time:'12:00:00';
ctx.fillStyle=p.text;ctx.font='11px sans-serif';ctx.textAlign='right';ctx.fillText(tm,314,16);ctx.textAlign='left';
ctx.strokeStyle=p.edge;ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(10,26);ctx.lineTo(314,26);ctx.stroke();
}

function cydDrawStripLandscape(ctx,p,cells){
cydDrawPanel(ctx,p,8,198,304,38,6);
ctx.strokeStyle=p.edge;
for(let i=1;i<4;i++){ctx.beginPath();ctx.moveTo(8+i*76,204);ctx.lineTo(8+i*76,230);ctx.stroke();}
for(let i=0;i<4;i++){
const c=cells[i]||{l:'',v:''};const cx=14+i*76;
ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText(c.l,cx,211);
ctx.fillStyle=p.text;ctx.font='bold 14px sans-serif';ctx.fillText(c.v,cx,228);
}
}

function cydRenderLandscape(ctx,p,d){
cydDrawHeaderLandscape(ctx,p,d);
const hr=parseFloat(d.hashrate)||0;
if(cydPreviewScr===0){
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
cydDrawStripLandscape(ctx,p,[{l:'WORKERS',v:'1'},{l:'POOL RATE',v:hr>0?(hr.toFixed(0)+' KH/s'):'--'},{l:'POOL BEST',v:d.best_diff||'--'},{l:'UPTIME',v:d.uptime||'--'}]);
}else if(cydPreviewScr===1){
const m=cydMkt(d);const up=m.chg>=0;
cydDrawPanel(ctx,p,8,30,304,52,8);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText('BTC / USDT',16,43);
ctx.textAlign='right';ctx.fillText('LATEST BLOCK',300,43);ctx.textAlign='left';
ctx.fillStyle=p.gold;ctx.font='bold 22px sans-serif';ctx.fillText(m.price,18,72);
ctx.fillStyle=up?p.good:p.bad;ctx.font='bold 10px sans-serif';ctx.fillText((up?'+':'')+m.chg.toFixed(2)+'%',134,62);
ctx.textAlign='right';ctx.fillStyle=p.text;ctx.font='bold 13px sans-serif';ctx.fillText(m.block,302,60);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText(m.fee+' fee',302,74);ctx.textAlign='left';
cydDrawPanel(ctx,p,8,86,304,68,6);
cydDrawCandles(ctx,p,12,89,296,62,m.k);
cydDrawPanel(ctx,p,8,158,304,36,6);
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText('HALVING #5',18,171);
ctx.fillStyle=p.meter;ctx.fillText(m.pct+'%',88,171);
ctx.textAlign='right';ctx.fillStyle=p.text;ctx.fillText('Diff: '+m.diff+' | Net: '+m.net,300,171);ctx.textAlign='left';
ctx.fillStyle=p.track;ctx.fillRect(18,175,284,6);ctx.fillStyle=p.meter;ctx.fillRect(18,175,284*m.pct/100,6);
cydDrawStripLandscape(ctx,p,[{l:'PAIR',v:'BTC/USDT'},{l:'INTERVAL',v:m.iv},{l:'POOL',v:m.pool},{l:'TIME',v:m.time}]);
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
cydDrawStripLandscape(ctx,p,[{l:'WORKERS',v:'1'},{l:'POOL RATE',v:d.hashrate?(parseFloat(d.hashrate).toFixed(0)+' KH/s'):'--'},{l:'UPTIME',v:d.uptime||'--'},{l:'TEMP',v:(d.temp||'--')+'°C'}]);
}else if(cydPreviewScr===3){
cydDrawPanel(ctx,p,8,30,304,48,8);
cydDrawCoin(ctx,p,28,54,16);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('SOLO BLOCK REWARD',52,42);
ctx.fillStyle=p.gold;ctx.font='bold 22px sans-serif';ctx.fillText('3.125 BTC',52,65);
ctx.font='9px sans-serif';ctx.fillStyle=p.dim;ctx.fillText('+ fees (~$250,000+ USD)',156,64);
ctx.fillStyle='rgba(34,197,94,0.15)';ctx.beginPath();ctx.roundRect(230,38,76,16,4);ctx.fill();
ctx.fillStyle=p.good;ctx.font='bold 8px sans-serif';ctx.textAlign='center';ctx.fillText('100% TO YOU',268,49);ctx.textAlign='left';

cydDrawPanel(ctx,p,8,82,304,50,6);
const py=88;
const stages=[
{t:'80B HEADER',s:'76B + Nonce',c:p.blue||'#38bdf8'},
{t:'2x SHA-256',s:'Double Hash',c:p.acc},
{t:'TARGET',s:'18+ Zeros',c:p.gold},
{t:'H < TARGET',s:'Jackpot Win!',c:p.good}
];
for(let i=0;i<4;i++){
const px=14+i*(60+14);
cydDrawPanel(ctx,p,px,py,60,38,4);
ctx.fillStyle=stages[i].c;ctx.font='bold 8px sans-serif';ctx.textAlign='center';ctx.fillText(stages[i].t,px+30,py+14);
ctx.fillStyle=p.dim;ctx.font='7px sans-serif';ctx.fillText(stages[i].s,px+30,py+28);ctx.textAlign='left';
if(i<3)cydDrawArrow(ctx,p,px+60+3,py+19,4,p.acc);
}

cydDrawTile(ctx,p,8,136,148,54,'GLOBAL TARGET',d.net_diff?(parseFloat(d.net_diff)/1e12).toFixed(2)+' T':'132.72 T',p.text);
cydDrawPanel(ctx,p,164,136,148,54,6);
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('HARD CAP SUPPLY',172,148);
ctx.fillStyle=p.gold;ctx.font='bold 15px sans-serif';ctx.fillText('20,999,999 BTC',172,166);
ctx.fillStyle=p.track;ctx.fillRect(172,172,132,4);
ctx.fillStyle=p.gold;ctx.fillRect(172,172,132*0.943,4);
ctx.fillStyle=p.dim;ctx.font='7px sans-serif';ctx.fillText('94.3% Mined • 21M Cap',172,184);

cydDrawStripLandscape(ctx,p,[{l:'WORKERS',v:'1'},{l:'POOL RATE',v:d.hashrate?(parseFloat(d.hashrate).toFixed(0)+' KH/s'):'--'},{l:'POOL BEST',v:d.best_diff||'0.000'},{l:'ODDS',v:'1/70M'}]);
}
}

function cydRender(){
const cvs=document.getElementById('cyd-canvas');if(!cvs)return;
const d=cydLastStats||{};
if(d.is_portrait!==undefined&&cydAutoFollow&&d.screen===undefined){
cydIsPortrait=d.is_portrait;
}
const lbl=document.getElementById('cyd-bezel-lbl');
const btnOrient=document.getElementById('cyd-orient-btn');
if(cydIsPortrait){
cvs.width=240;cvs.height=320;
cvs.classList.remove('landscape');
if(lbl)lbl.innerHTML='ESP32-2432S024 &bull; 2.4&quot; 240&times;320 SPI TFT (PORTRAIT)';
if(btnOrient)btnOrient.textContent='🔄 Portrait (240×320)';
}else{
cvs.width=320;cvs.height=240;
cvs.classList.add('landscape');
if(lbl)lbl.innerHTML='ESP32-2432S024 &bull; 2.4&quot; 320&times;240 SPI TFT (LANDSCAPE)';
if(btnOrient)btnOrient.textContent='🔄 Landscape (320×240)';
}
const ctx=cvs.getContext('2d');const p=cydGetPalette(d.theme);
ctx.fillStyle=p.bg;ctx.fillRect(0,0,cvs.width,cvs.height);
if(cydIsPortrait)cydRenderPortrait(ctx,p,d);
else cydRenderLandscape(ctx,p,d);
}
)rawjs";

static inline void appendCydTwinStyle(String &page) { page += FPSTR(CYD_TWIN_STYLE); }
static inline void appendCydTwinHtml(String &page) { page += FPSTR(CYD_TWIN_HTML); }
static inline void appendCydTwinScript(String &page) { page += FPSTR(CYD_TWIN_JS); }

#endif // WEB_CYD_TWIN_H

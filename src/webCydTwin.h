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

function cydPoolLbl(h){h=h||'';const i=h.lastIndexOf('.');if(i>0)h=h.substring(0,i);if(h.startsWith('pool.'))h=h.substring(5);return (h.substring(0,9))||'--';}
function cydMkt(d){const m=(d&&d.market)||{};const pre=m.prefix||'$';const rem=m.rem||0;
return {price:m.price?pre+Number(m.price).toLocaleString('en-US'):'--',cur:(m.cur||'USD').toUpperCase(),block:m.block?Number(m.block).toLocaleString('en-US'):'--',
fee:m.fee?m.fee+' sat/vB':'--',diff:m.diff||'--',net:m.net?m.net+' EH/s':'--',rem:rem,pct:rem?Math.floor((210000-rem)*100/210000):0,
pool:cydPoolLbl(m.pool),time:(d&&d.time?d.time:'--:--').substring(0,5)};}
function cydDrawStrip2(ctx,p,cells,ls){
if(ls)cydDrawPanel(ctx,p,8,198,304,38,6);else cydDrawPanel(ctx,p,6,266,228,48,8);
const mx=ls?160:120;ctx.strokeStyle=p.edge;ctx.beginPath();ctx.moveTo(mx+.5,ls?204:272);ctx.lineTo(mx+.5,ls?230:308);ctx.stroke();
ctx.textAlign='center';
for(let i=0;i<2;i++){const c=cells[i]||{l:'',v:''};const cx=ls?(84+i*152):(63+i*114);
ctx.fillStyle=p.dim;ctx.font=(ls?10:9)+'px sans-serif';ctx.fillText(c.l,cx,ls?211:282);
ctx.fillStyle=p.text;ctx.font='bold '+(ls?14:15)+'px sans-serif';ctx.fillText(c.v,cx,ls?228:302);}
ctx.textAlign='left';
}

function cydHashLog2Q8FromDiff(d){if(!(d>0))return 0xFF00;const log2True=223.999978314;let exp=0;let m=d;while(m>=1){m*=0.5;exp++;}while(m>0&&m<0.5){m*=2;exp--;}const u=2*m;const z=u-1;const log2m=-1+(z-0.5*z*z+(z*z*z)/3)/0.69314718056;let log2h=log2True-(exp+log2m);if(log2h<0)log2h=0;if(log2h>255.996)log2h=255.996;return (log2h*256)|0;}
function cydDiffQ8FromHashQ8(h){return (224<<8)-h;}
function cydSynthSamples(n){const out=[];for(let i=0;i<n;i++){const bits=255-((Math.random()*3)|0)-((Math.random()<0.1)?((Math.random()*12)|0):0);const frac=(Math.random()*255)|0;out.push((bits<<8)|frac);}return out;}
function cydParseDiff(v){if(typeof v==='number')return v;const m=String(v||'').trim().match(/^([0-9.]+)\s*([KMGTPE]?)/i);if(!m)return 0;const k={'':1,K:1e3,M:1e6,G:1e9,T:1e12,P:1e15,E:1e18}[m[2].toUpperCase()]||1;return parseFloat(m[1])*k;}
function cydProtoYDiff(dq8,topQ8,botQ8,plotY,plotH){const span=topQ8-botQ8;if(span<=0)return plotY+(plotH>>1);let v=topQ8-dq8;if(v<0)v=0;if(v>span)v=span;return plotY+2+(((v*(plotH-5))/span)|0);}
function cydDrawProtoX(ctx,x,y,col){ctx.strokeStyle=col;ctx.lineWidth=1.5;ctx.beginPath();ctx.moveTo(x-2.5,y-2.5);ctx.lineTo(x+2.5,y+2.5);ctx.moveTo(x-2.5,y+2.5);ctx.lineTo(x+2.5,y-2.5);ctx.stroke();}
function cydFmtDiffShort(n){n=Number(n)||0;if(n>=1e12)return (n/1e12>=100?(n/1e12).toFixed(0):(n/1e12).toFixed(1))+'T';if(n>=1e9)return (n/1e9).toFixed(1)+'G';if(n>=1e6)return (n/1e6).toFixed(1)+'M';if(n>=1e3)return (n/1e3).toFixed(1)+'K';if(n>=1)return n.toFixed(1);if(n>0)return n.toExponential(1);return '--';}
function cydDrawProtocol(ctx,p,d,ls){
const W=ls?320:240,H=ls?240:320;const plotX=ls?8:6,plotY=28,plotW=W-(ls?16:12);const footH=ls?44:52,footY=H-footH-4,plotH=footY-plotY-4;
const hr=parseFloat(d.hashrate)||0;const netDiff=(d.net_diff&&d.net_diff>0)?Number(d.net_diff):133e12;
let samples=Array.isArray(d.hash_samples)?d.hash_samples.slice():null;
if(!samples||!samples.length)samples=cydSynthSamples(40);
const best=(d.best_diff_raw>0)?Number(d.best_diff_raw):cydParseDiff(d.best_diff);
const tgtQ8=cydDiffQ8FromHashQ8(cydHashLog2Q8FromDiff(netDiff));
let bestQ8=best>0?cydDiffQ8FromHashQ8(cydHashLog2Q8FromDiff(best)):null;
const topQ8=tgtQ8,botQ8=0;const n=samples.length;
let bestI=-1,maxQ=-1e9;for(let i=0;i<n;i++){const q=cydDiffQ8FromHashQ8(samples[i]);if(q>maxQ){maxQ=q;bestI=i;}}
if(bestQ8==null||maxQ>bestQ8)bestQ8=maxQ;const bestIsSample=bestI>=0&&maxQ>=bestQ8-8;
ctx.font='8px sans-serif';const G=Math.ceil(ctx.measureText('1G').width)+8,sx0=G+10,sxSpan=plotW-sx0-14;
const yTop=plotY+2,yBot=plotY+plotH-16;
const Y=q=>{const span=topQ8-botQ8;let v=topQ8-q;if(v<0)v=0;if(v>span)v=span;return yTop+Math.round(v*(yBot-yTop)/span);};
cydDrawPanel(ctx,p,plotX,plotY,plotW,plotH,6);
const ticks=['1K','1M','1G'];ctx.textAlign='right';
for(let t=0;t<3;t++){const q=((t+1)*10)<<8;if(q>=topQ8-(4<<8))break;const ty=Y(q);ctx.fillStyle=p.edge||'#30363d';for(let x=plotX+G+2;x<plotX+plotW-4;x+=4)ctx.fillRect(x,ty,1,1);ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText(ticks[t],plotX+G-4,ty+3);}
ctx.strokeStyle=p.edge||'#334155';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(plotX+G+0.5,yTop);ctx.lineTo(plotX+G+0.5,yBot);ctx.lineTo(plotX+plotW-4,yBot+0.5);ctx.stroke();
ctx.fillStyle=p.dim;ctx.fillText('<1',plotX+G-4,yBot+3);
const warn=p.warn||'#f59e0b';const netLab=(d.market&&d.market.diff)?d.market.diff:cydFmtDiffShort(netDiff);
ctx.fillStyle=warn;ctx.fillRect(plotX+G,yTop,plotW-G-4,2);ctx.fillText(netLab,plotX+G-4,yTop+6);
ctx.font='bold 8px sans-serif';ctx.fillText('BLOCK THRESHOLD \u00b7 '+netLab+' diff',plotX+plotW-6,yTop+12);
ctx.fillStyle=p.dim;ctx.font='7px sans-serif';ctx.fillText('hash < target \u2192 valid block',plotX+plotW-6,yTop+22);
ctx.textAlign='left';ctx.font='8px sans-serif';ctx.fillText('RARER \u2191',plotX+G+4,yTop+12);
const xAt=i=>plotX+sx0+((i*sxSpan)/Math.max(1,n-1));
for(let i=0;i<n;i++){if(bestIsSample&&i===bestI)continue;cydDrawProtoX(ctx,xAt(i),Y(cydDiffQ8FromHashQ8(samples[i])),p.bad||'#f87171');}
if(n>0){ctx.strokeStyle=p.dim;ctx.lineWidth=1;ctx.beginPath();ctx.arc(xAt(n-1),Y(cydDiffQ8FromHashQ8(samples[n-1])),5,0,Math.PI*2);ctx.stroke();}
if(bestQ8!=null){const bx=bestIsSample?xAt(bestI):plotX+sx0,by=Y(bestQ8);ctx.fillStyle=p.meter;ctx.beginPath();ctx.arc(bx,by,4,0,Math.PI*2);ctx.fill();ctx.strokeStyle=warn;ctx.beginPath();ctx.arc(bx,by,5,0,Math.PI*2);ctx.stroke();
const bv=bestIsSample?Math.pow(2,maxQ/256):best;const blab='\u2605 BEST '+((!bestIsSample&&d.best_diff)?d.best_diff:cydFmtDiffShort(bv));ctx.font='bold 9px sans-serif';const lw=ctx.measureText(blab).width;const right=bx+8+lw<plotX+plotW-4;ctx.textAlign=right?'left':'right';ctx.fillText(blab,right?bx+8:bx-8,by>yTop+30?by-6:by+14);ctx.textAlign='left';}
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.textAlign='center';ctx.fillText('recent samples \u00b7 newest \u2192',plotX+G+(plotW-G)/2,plotY+plotH-4);ctx.textAlign='left';
cydDrawPanel(ctx,p,plotX,footY,plotW,footH,6);
ctx.fillStyle=p.meter;ctx.beginPath();ctx.moveTo(plotX+10,footY+(ls?22:24));ctx.lineTo(plotX+18,footY+(ls?8:10));ctx.lineTo(plotX+15,footY+(ls?16:18));ctx.fill();ctx.beginPath();ctx.moveTo(plotX+14,footY+(ls?14:16));ctx.lineTo(plotX+22,footY+(ls?14:16));ctx.lineTo(plotX+16,footY+(ls?28:30));ctx.fill();
ctx.fillStyle=p.meter;ctx.font='bold 20px sans-serif';ctx.fillText((hr>0?hr.toFixed(0):'--')+' KH/s',plotX+28,footY+(ls?26:21));
ctx.fillStyle=p.text;const attT='\u2248 '+(hr>0?(hr*1000).toFixed(0):'--')+' attempts/sec';if(ls){ctx.font='11px sans-serif';ctx.textAlign='right';ctx.fillText(attT,plotX+plotW-8,footY+24);ctx.textAlign='left';}else{ctx.font='10px sans-serif';ctx.fillText(attT,plotX+28,footY+35);}
ctx.fillStyle=p.dim;ctx.font='8px sans-serif';ctx.fillText('LIVE SAMPLE \u00b7 MINING UNINTERRUPTED',plotX+28,footY+(ls?36:47));
}

function cydMktCells(d){const hr=d&&d.hashrate?parseFloat(d.hashrate):0;return [{l:'HASHRATE',v:hr.toFixed(0)+' KH/s'},{l:'SHARES',v:(parseInt((d&&d.shares)||0)||0).toLocaleString('en-US')}];}
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
const m=cydMkt(d);
cydDrawPanel(ctx,p,6,30,228,70,8);
ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('BTC / '+m.cur,14,44);

ctx.textAlign='center';ctx.fillStyle=p.gold;ctx.font='bold 30px sans-serif';ctx.fillText(m.price,120,88);
cydDrawPanel(ctx,p,6,106,228,56,8);
ctx.strokeStyle=p.edge;ctx.beginPath();ctx.moveTo(120.5,114);ctx.lineTo(120.5,154);ctx.stroke();
ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText('BLOCK HEIGHT',63,121);ctx.fillText('FEE RATE',177,121);
ctx.fillStyle=p.text;ctx.font='bold 18px sans-serif';ctx.fillText(m.block,63,149);
ctx.font='bold 14px sans-serif';ctx.fillText(m.fee,177,147);
cydDrawPanel(ctx,p,6,168,228,92,8);
ctx.textAlign='left';ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('HALVING #5 PROGRESS',14,182);
ctx.textAlign='right';ctx.fillStyle=p.meter;ctx.font='bold 12px sans-serif';ctx.fillText(m.pct+'%',226,184);
ctx.fillStyle=p.track;ctx.fillRect(14,191,212,8);ctx.fillStyle=p.meter;ctx.fillRect(14,191,212*m.pct/100,8);
ctx.textAlign='center';ctx.fillStyle=p.text;ctx.font='9px sans-serif';ctx.fillText(m.rem.toLocaleString('en-US')+' blocks left',120,212);
ctx.strokeStyle=p.edge;ctx.beginPath();ctx.moveTo(14,220.5);ctx.lineTo(226,220.5);ctx.moveTo(120.5,226);ctx.lineTo(120.5,254);ctx.stroke();
ctx.fillStyle=p.dim;ctx.fillText('DIFFICULTY',63,233);ctx.fillText('NETWORK',177,233);
ctx.font='bold 13px sans-serif';ctx.fillStyle=p.gold;ctx.fillText(m.diff,63,250);ctx.fillStyle=p.good;ctx.fillText(m.net,177,250);ctx.textAlign='left';
cydDrawStrip2(ctx,p,cydMktCells(d),false);
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
cydDrawProtocol(ctx,p,d,false);
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
const m=cydMkt(d);
cydDrawPanel(ctx,p,8,30,304,60,8);
ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('BTC / '+m.cur,16,44);
ctx.textAlign='right';ctx.fillText('LATEST BLOCK',300,44);ctx.textAlign='left';
ctx.fillStyle=p.gold;ctx.font='bold 28px sans-serif';ctx.fillText(m.price,16,80);
const pw=ctx.measureText(m.price).width;

ctx.textAlign='right';ctx.fillStyle=p.text;ctx.font='bold 16px sans-serif';ctx.fillText(m.block,300,64);
ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText(m.fee+' fee',300,82);ctx.textAlign='left';
cydDrawPanel(ctx,p,8,96,304,96,8);
ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('HALVING #5 PROGRESS',16,110);
ctx.textAlign='right';ctx.fillStyle=p.meter;ctx.font='bold 12px sans-serif';ctx.fillText(m.pct+'%',304,112);
ctx.fillStyle=p.track;ctx.fillRect(16,119,288,8);ctx.fillStyle=p.meter;ctx.fillRect(16,119,288*m.pct/100,8);
ctx.textAlign='center';ctx.fillStyle=p.text;ctx.font='9px sans-serif';ctx.fillText(m.rem.toLocaleString('en-US')+' blocks left',160,140);
ctx.strokeStyle=p.edge;ctx.beginPath();ctx.moveTo(16,146.5);ctx.lineTo(304,146.5);ctx.moveTo(160.5,152);ctx.lineTo(160.5,186);ctx.stroke();
ctx.fillStyle=p.dim;ctx.fillText('DIFFICULTY',84,160);ctx.fillText('NETWORK HASHRATE',236,160);
ctx.font='bold 15px sans-serif';ctx.fillStyle=p.gold;ctx.fillText(m.diff,84,181);ctx.fillStyle=p.good;ctx.fillText(m.net,236,181);ctx.textAlign='left';
cydDrawStrip2(ctx,p,cydMktCells(d),true);
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
cydDrawProtocol(ctx,p,d,true);
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

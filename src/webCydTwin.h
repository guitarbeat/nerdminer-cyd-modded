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
if(cydPreviewScr===1){ctx.fillStyle=p.dim;ctx.fillText('|',tw+12,16);}ctx.fillStyle=stCol;ctx.font='bold 9px sans-serif';ctx.fillText(stTxt,tw+(cydPreviewScr===1?20:14),16);
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
function cydMkt(d){const m=(d&&d.market)||{};const pre=m.prefix||'$';const rem=m.rem||0;const blk=m.block||0;
return {price:m.price?pre+Number(m.price).toLocaleString('en-US'):'--',cur:(m.cur||'USD').toUpperCase(),block:blk?Number(blk).toLocaleString('en-US'):'--',height:blk,
age:(m.age!==undefined&&m.age>=0)?m.age:-1,fee:m.fee||0,fhour:m.fhour||0,ffast:m.ffast||0,diff:m.diff||'--',net:m.net?m.net+' EH/s':'--',rem:rem,
pct:rem?Math.floor((210000-rem)*100/210000):0,eta:m.hdate||''};}
function cydFeeClass(f,p){if(!(f>0))return null;if(f<=4)return ['LOW',p.good];if(f<=20)return ['NORMAL',p.meter];return ['BUSY',p.bad];}
function cydFmtReward(r){return String(parseFloat(r.toFixed(4)));}
function cydDrawMarket(ctx,p,d,ls){
const m=cydMkt(d);const g=ls?{hx:8,hy:30,hw:304,hh:56,ry:90,rh:44,cw:150,fy:138,fh:40,vy:182,vh:54}:{hx:6,hy:30,hw:228,hh:74,ry:108,rh:50,cw:112,fy:162,fh:56,vy:222,vh:94};
const R=g.hx+g.hw;const ag=m.age>=0?(m.age<1?'just now':m.age+'m ago'):'';const bl='BLOCK #'+m.block;
cydDrawPanel(ctx,p,g.hx,g.hy,g.hw,g.hh,6);ctx.textAlign='left';ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('BTC / '+m.cur,g.hx+10,g.hy+15);
if(!ls){ctx.textAlign='center';ctx.fillStyle=p.gold;ctx.font='bold '+(m.price.length>9?26:30)+'px sans-serif';ctx.fillText(m.price,120,g.hy+50);
ctx.font='10px sans-serif';const lw=ctx.measureText(bl).width,aw=ag?ctx.measureText(ag).width:0;const x0=120-(lw+(ag?14+aw:0))/2;ctx.textAlign='left';ctx.fillStyle=p.text;ctx.fillText(bl,x0,g.hy+66);
if(ag){ctx.fillStyle=p.dim;ctx.fillRect(x0+lw+6,g.hy+62,2,2);ctx.fillText(ag,x0+lw+14,g.hy+66);}}
else{ctx.fillStyle=p.gold;ctx.font='bold 28px sans-serif';ctx.fillText(m.price,g.hx+8,g.hy+46);ctx.textAlign='right';ctx.fillStyle=p.text;ctx.font='bold 14px sans-serif';ctx.fillText(bl,R-8,g.hy+24);
if(ag){ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText(ag,R-8,g.hy+42);}ctx.textAlign='left';}
const cards=[['NETWORK',m.net,p.good,g.hx],['DIFFICULTY',m.diff,p.gold,R-g.cw]];
for(const c of cards){cydDrawPanel(ctx,p,c[3],g.ry,g.cw,g.rh,6);ctx.textAlign='center';ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText(c[0],c[3]+g.cw/2,g.ry+(ls?14:16));ctx.fillStyle=c[2];ctx.font='bold '+(ls?17:18)+'px sans-serif';ctx.fillText(c[1],c[3]+g.cw/2,g.ry+(ls?37:40));}
ctx.textAlign='left';cydDrawPanel(ctx,p,g.hx,g.fy,g.hw,g.fh,6);ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('HALF-HOUR FEE',g.hx+8,g.fy+(ls?15:16));
const fv=m.fee>0?String(m.fee):'--';const vs=ls?18:22;const vx=ls?g.hx+132:g.hx+8,vy=ls?g.fy+28:g.fy+42;ctx.fillStyle=p.text;ctx.font='bold '+vs+'px sans-serif';ctx.fillText(fv,vx,vy);const fw=ctx.measureText(fv).width;ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('sat/vB',vx+fw+4,vy);
const cls=cydFeeClass(m.fee,p);if(cls){ctx.font='bold 9px sans-serif';const pw=ctx.measureText(cls[0]).width+12,ph=14,px=R-pw-8,py=ls?g.fy+12:g.fy+6;ctx.fillStyle=cls[1];ctx.beginPath();ctx.roundRect(px,py,pw,ph,7);ctx.fill();ctx.fillStyle=p.bg;ctx.textAlign='center';ctx.fillText(cls[0],px+pw/2,py+10);ctx.textAlign='left';}
if(m.fhour>0&&m.ffast>0&&m.fhour!==m.ffast){const rg='range '+Math.min(m.fhour,m.ffast)+'-'+Math.max(m.fhour,m.ffast)+' sat/vB';ctx.fillStyle=p.dim;ctx.font='9px sans-serif';if(ls){ctx.fillText(rg,g.hx+8,g.fy+33);}else{ctx.textAlign='right';ctx.fillText(rg,R-8,g.fy+48);ctx.textAlign='left';}}
cydDrawPanel(ctx,p,g.hx,g.vy,g.hw,g.vh,6);const era=m.height>0?Math.floor(m.height/210000):4;const r0=50/Math.pow(2,era);
const rw=cydFmtReward(r0)+' \u2192 ',rw2=cydFmtReward(r0/2)+' BTC';const rem=(m.rem>0?m.rem.toLocaleString('en-US'):'--')+' blocks left';const bx=g.hx+8,bw=g.hw-16;
const reward=(xr,y,sz)=>{ctx.font='bold '+sz+'px sans-serif';const w2=ctx.measureText(rw2).width;ctx.textAlign='right';ctx.fillStyle=p.gold;ctx.fillText(rw2,xr,y);ctx.fillStyle=p.dim;ctx.fillText(rw,xr-w2,y);ctx.textAlign='left';};
if(!ls){ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('NEXT HALVING',bx,g.vy+16);ctx.textAlign='right';ctx.fillStyle=p.meter;ctx.font='bold 16px sans-serif';ctx.fillText(m.pct+'%',R-8,g.vy+18);ctx.textAlign='left';
ctx.fillStyle=p.track;ctx.beginPath();ctx.roundRect(bx,g.vy+26,bw,10,5);ctx.fill();if(m.pct>0){ctx.fillStyle=p.meter;ctx.beginPath();ctx.roundRect(bx,g.vy+26,bw*m.pct/100,10,5);ctx.fill();}
ctx.fillStyle=p.text;ctx.font='11px sans-serif';ctx.fillText(rem,bx,g.vy+55);if(m.eta){ctx.textAlign='right';ctx.fillStyle=p.dim;ctx.fillText(m.eta,R-8,g.vy+55);ctx.textAlign='left';}
ctx.strokeStyle=p.edge;ctx.beginPath();ctx.moveTo(bx,g.vy+62.5);ctx.lineTo(bx+bw,g.vy+62.5);ctx.stroke();ctx.fillStyle=p.dim;ctx.font='9px sans-serif';ctx.fillText('BLOCK REWARD',bx,g.vy+79);reward(R-8,g.vy+80,12);}
else{ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText('NEXT HALVING',bx,g.vy+15);const hw=ctx.measureText('NEXT HALVING').width;ctx.fillStyle=p.meter;ctx.font='bold 13px sans-serif';ctx.fillText(m.pct+'%',bx+hw+8,g.vy+16);reward(R-8,g.vy+15,11);
ctx.fillStyle=p.track;ctx.beginPath();ctx.roundRect(bx,g.vy+22,bw,8,4);ctx.fill();if(m.pct>0){ctx.fillStyle=p.meter;ctx.beginPath();ctx.roundRect(bx,g.vy+22,bw*m.pct/100,8,4);ctx.fill();}
ctx.fillStyle=p.text;ctx.font='10px sans-serif';ctx.fillText(rem,bx,g.vy+45);if(m.eta){ctx.textAlign='right';ctx.fillStyle=p.dim;ctx.fillText(m.eta,R-8,g.vy+45);ctx.textAlign='left';}}
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

function cydHumanHashes(mh){let h=(Number(mh)||0)*1e6;const u=['','K','M','B','T','Q'];let i=0;while(h>=1000&&i<5){h/=1000;i++;}return i?(h>=100?h.toFixed(1):h.toFixed(2))+u[i]:h.toFixed(0);}
function cydUpShort(s){const m=String(s||'').match(/^(\d+)\s+(\d+):(\d+)/);if(!m)return '--';const d=+m[1],h=+m[2],mi=+m[3];return d>0?d+'d':(h>0?h+'h':mi+'m');}
function cydStar(ctx,cx,cy,r,col){ctx.fillStyle=col;ctx.beginPath();for(let i=0;i<10;i++){const a=-Math.PI/2+i*Math.PI/5,rr=i%2?r*0.42:r;ctx.lineTo(cx+rr*Math.cos(a),cy+rr*Math.sin(a));}ctx.closePath();ctx.fill();}
function cydMineCard(ctx,p,x,y,w,h,lbl,val,col,star){cydDrawPanel(ctx,p,x,y,w,h,4);ctx.font='10px sans-serif';const lw=ctx.measureText(lbl).width+(star?12:0);let lx=x+(w-lw)/2;if(star){cydStar(ctx,lx+4,y+10,5,p.gold);lx+=12;}
ctx.fillStyle=p.dim;ctx.textAlign='left';ctx.fillText(lbl,lx,y+14);ctx.textAlign='center';ctx.fillStyle=col;ctx.font='bold '+(h>=60?22:18)+'px sans-serif';ctx.fillText(val,x+w/2,y+(h>=60?50:35));ctx.textAlign='left';}
function cydDrawMine(ctx,p,d,ls){
const hr=parseFloat(d.hashrate)||0;const cx=ls?100:120,cy=ls?116:128,R=ls?86:98,Ri=ls?74:84,segs=30;
const filled=Math.round(Math.min(hr/1200,1)*segs);const base=Math.PI/2;
for(let i=0;i<segs;i++){const a0=base+(45+i*9)*Math.PI/180,a1=base+(45+i*9+7)*Math.PI/180;let col=(i>=27)?'#3a1010':p.track;if(i<filled)col=(i>=27)?p.bad:(i>=24?p.warn:p.meter);
ctx.lineWidth=R-Ri;ctx.strokeStyle=col;ctx.beginPath();ctx.arc(cx,cy,(R+Ri)/2,a0,a1);ctx.stroke();}
ctx.textAlign='center';ctx.fillStyle=p.dim;ctx.font=(ls?10:11)+'px sans-serif';ctx.fillText('HASHRATE',cx,cy-(ls?30:33));
ctx.fillStyle=p.text;ctx.font='bold '+(ls?32:38)+'px sans-serif';ctx.fillText(hr>0?hr.toFixed(1):'0.0',cx,cy+(ls?6:6));
ctx.fillStyle=p.meter;ctx.font=(ls?12:13)+'px sans-serif';ctx.fillText('KH/s',cx,cy+(ls?24:28));
ctx.fillStyle=p.dim;ctx.font=(ls?8:10)+'px sans-serif';ctx.fillText(cydHumanHashes(d.mhashes)+' hashes attempted',cx,cy+(ls?48:56));ctx.textAlign='left';
const sh=(parseInt(d.shares)||0).toLocaleString('en-US'),best=d.best_diff||'--';
if(ls){cydMineCard(ctx,p,198,34,114,76,'SHARES',sh,p.text,false);cydMineCard(ctx,p,198,116,114,76,'BEST',best,p.gold,true);}
else{cydMineCard(ctx,p,6,222,112,42,'SHARES',sh,p.text,false);cydMineCard(ctx,p,122,222,112,42,'BEST',best,p.gold,true);}
const sx=ls?8:6,sy=ls?198:270,sw=ls?304:228,shh=ls?38:44;cydDrawPanel(ctx,p,sx,sy,sw,shh,6);
const wk=(d.workers!==undefined)?+d.workers:1,bl=parseInt(d.blocks)||0;
const cells=[[String(wk),wk===1?'worker':'workers'],[cydUpShort(d.uptime),'uptime'],[String(bl),bl===1?'block':'blocks']];
ctx.strokeStyle=p.edge;ctx.lineWidth=1;for(let i=1;i<3;i++){const x=Math.round(sx+sw*i/3)+.5;ctx.beginPath();ctx.moveTo(x,sy+8);ctx.lineTo(x,sy+shh-8);ctx.stroke();}
for(let i=0;i<3;i++){const ccx=sx+sw*(i+0.5)/3,by=sy+shh/2+6;ctx.font='bold '+(ls?15:16)+'px sans-serif';const wv=ctx.measureText(cells[i][0]).width;ctx.font='10px sans-serif';const wl=ctx.measureText(cells[i][1]).width;const x0=ccx-(wv+4+wl)/2;
ctx.fillStyle=p.text;ctx.font='bold '+(ls?15:16)+'px sans-serif';ctx.fillText(cells[i][0],x0,by);ctx.fillStyle=p.dim;ctx.font='10px sans-serif';ctx.fillText(cells[i][1],x0+wv+4,by);}
}
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
cydDrawMine(ctx,p,d,false);
}else if(cydPreviewScr===1){
cydDrawMarket(ctx,p,d,false);
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
if(cydPreviewScr===1){ctx.fillStyle=p.dim;ctx.fillText('|',tw+12,16);}ctx.fillStyle=stCol;ctx.font='bold 9px sans-serif';ctx.fillText(stTxt,tw+(cydPreviewScr===1?20:14),16);
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
cydDrawMine(ctx,p,d,true);
}else if(cydPreviewScr===1){
cydDrawMarket(ctx,p,d,true);
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

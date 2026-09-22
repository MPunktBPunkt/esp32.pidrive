#pragma once
#include <Arduino.h>

const char PAGE_MAIN[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Cache-Control" content="no-store">
<title>esp32.pidrive</title>
<style>
:root{--bg:#0f1419;--panel:#1a222c;--line:#2c3847;--ink:#e8eef4;--muted:#8b9bb0;--acc:#3d9cfd;--ok:#3dd68c;--warn:#f5a524;--bad:#f76c6c}
*{box-sizing:border-box;margin:0;padding:0}
body{font:14px/1.45 system-ui,sans-serif;background:var(--bg);color:var(--ink);min-height:100vh}
.shell{max-width:920px;margin:0 auto;padding:16px 14px 40px}
h1{font-size:1.35rem;font-weight:700;letter-spacing:-.02em}
.sub{color:var(--muted);font-size:12px;margin-top:4px}
.bar{display:flex;flex-wrap:wrap;gap:8px;margin:14px 0 8px}
.chip{border:1px solid var(--line);background:var(--panel);padding:4px 10px;border-radius:999px;font-size:11px;color:var(--muted)}
.chip b{color:var(--ink)}
.chip.on{border-color:rgba(61,214,140,.45);color:var(--ok)}
.chip.off{opacity:.55}
.chip.warn{border-color:rgba(245,165,36,.45);color:var(--warn)}
nav{display:flex;gap:2px;border-bottom:1px solid var(--line);margin:12px 0 16px;overflow:auto}
.tab{appearance:none;border:0;background:0;color:var(--muted);padding:10px 12px;cursor:pointer;font:inherit;border-bottom:2px solid transparent;white-space:nowrap}
.tab.active{color:var(--acc);border-bottom-color:var(--acc)}
.pane{display:none}.pane.active{display:block}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:12px}
.panel h3{font-size:11px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin-bottom:10px}
.ports{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:14px}
@media(max-width:640px){.ports{grid-template-columns:1fr}}
.port{border:1px solid var(--line);border-radius:12px;padding:16px 14px;background:#0c1016;transition:border-color .2s,box-shadow .2s}
.port.up{border-color:rgba(61,214,140,.55);box-shadow:inset 0 0 0 1px rgba(61,214,140,.12)}
.port.sus{border-color:rgba(245,165,36,.5)}
.port.quiet{border-color:rgba(61,156,253,.45);box-shadow:inset 0 0 0 1px rgba(61,156,253,.12)}
.port .topline{display:flex;align-items:center;gap:10px}
.port .dot{width:16px;height:16px;border-radius:50%;background:#3a4555;flex-shrink:0;transition:background .2s,box-shadow .2s}
.port.up .dot{background:var(--ok);box-shadow:0 0 14px rgba(61,214,140,.6)}
.port.sus .dot{background:var(--warn);box-shadow:0 0 14px rgba(245,165,36,.45)}
.port.quiet .dot{background:var(--acc);box-shadow:0 0 14px rgba(61,156,253,.45)}
.port .plabel{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}
.port .pname{font-size:1.05rem;font-weight:700;letter-spacing:-.02em}
.port .pstate{margin-top:10px;font-size:1.35rem;font-weight:700;letter-spacing:-.02em}
.port.up .pstate{color:var(--ok)}
.port.sus .pstate{color:var(--warn)}
.port.quiet .pstate{color:var(--acc)}
.port .phint{margin-top:6px;font-size:12px;color:var(--muted);line-height:1.4}
.port .pmeta{margin-top:10px;font-family:ui-monospace,monospace;font-size:11px;color:var(--muted)}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{padding:8px 6px;border-bottom:1px solid var(--line);text-align:left}
th{color:var(--muted);font-size:10px;text-transform:uppercase}
tr.play td{color:var(--ok)}
.btn{appearance:none;border:1px solid var(--line);background:#243040;color:var(--ink);padding:8px 12px;border-radius:8px;cursor:pointer;font:inherit;margin:4px 6px 0 0}
.btn:hover{border-color:var(--acc);color:var(--acc)}
.btn-a{background:rgba(61,156,253,.15);border-color:rgba(61,156,253,.4);color:var(--acc)}
label{display:block;font-size:11px;color:var(--muted);margin:10px 0 4px;text-transform:uppercase}
input{width:100%;background:#0c1016;border:1px solid var(--line);color:var(--ink);padding:8px;border-radius:6px;font:inherit}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
@media(max-width:640px){.row{grid-template-columns:1fr}}
.ev{font-family:ui-monospace,monospace;font-size:12px;padding:6px 0;border-bottom:1px solid var(--line)}
.ev .c{color:var(--acc)}.ev .d{color:var(--muted)}
.drop{border:1px dashed var(--line);border-radius:10px;padding:24px;text-align:center;color:var(--muted);cursor:pointer}
.drop:hover{border-color:var(--acc);color:var(--ink)}
.meta{color:var(--muted);font-size:12px;margin-top:8px}
.metric{font-family:ui-monospace,monospace;font-size:13px;padding:6px 0;border-bottom:1px solid var(--line);display:flex;justify-content:space-between;gap:12px}
.metric b{color:var(--ok)}
.apbox{background:#0c1016;border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin-bottom:12px;font-size:13px}
.apbox code{color:var(--acc)}
.cover-wrap{display:flex;gap:14px;align-items:flex-start;margin:8px 0 4px;flex-wrap:wrap}
.cover-art{width:160px;height:160px;object-fit:cover;border-radius:8px;border:1px solid var(--line);background:#0c1016;display:none}
.cover-art.on{display:block}
.cover-art.lg{width:220px;height:220px}
.cover-empty{width:160px;height:160px;border-radius:8px;border:1px dashed var(--line);color:var(--muted);display:flex;align-items:center;justify-content:center;font-size:12px;text-align:center;padding:8px}
.cover-empty.lg{width:220px;height:220px}
.cover-empty.hide{display:none}
.cover-meta{flex:1;min-width:160px;font-size:13px;color:var(--muted)}
.cover-meta b{color:var(--ink)}
.remote-actions{display:flex;flex-wrap:wrap;gap:8px;margin:10px 0}
.btn-bad{background:rgba(247,108,108,.12);border-color:rgba(247,108,108,.45);color:var(--bad)}
.now-title{font-size:1.25rem;font-weight:700;letter-spacing:-.02em;margin:4px 0}
</style></head><body>
<div class="shell">
  <h1>esp32.pidrive</h1>
  <p class="sub">USB-MSC Auto-Test · SoftAP WebUI · ohne Pi</p>
  <div class="bar" id="bar">
    <span class="chip" id="c-ver">v-</span>
    <span class="chip off" id="c-otg">AUTO</span>
    <span class="chip off" id="c-uart">PI</span>
    <span class="chip off" id="c-msc">MSC</span>
    <span class="chip" id="c-play">PLAY -</span>
    <span class="chip off" id="c-pump">PUMP</span>
    <span class="chip" id="c-lat">LAT -</span>
    <span class="chip" id="c-ap">AP -</span>
  </div>
  <nav>
    <button class="tab active" data-t="remote">Remote</button>
    <button class="tab" data-t="car">Auto-Test</button>
    <button class="tab" data-t="menu">Menü</button>
    <button class="tab" data-t="events">Events</button>
    <button class="tab" data-t="config">Config</button>
    <button class="tab" data-t="ota">OTA</button>
  </nav>

  <section class="pane active" id="p-remote">
    <div class="panel">
      <h3>Fernbedienung</h3>
      <div class="now-title" id="remote-title">—</div>
      <div class="cover-wrap">
        <img id="remote-art" class="cover-art lg" alt="Cover" width="220" height="220">
        <div id="remote-empty" class="cover-empty lg">kein Cover</div>
        <div class="cover-meta">
          <div>Quelle <b id="remote-src">—</b></div>
          <div style="margin-top:6px">Datei <code id="remote-path">—</code></div>
          <div style="margin-top:6px">ID3 <b id="remote-id3">—</b></div>
          <div class="meta" style="margin-top:8px">Handy am SoftAP · steuert PiDrive über PUMP</div>
        </div>
      </div>
      <div class="remote-actions">
        <button class="btn btn-a" id="btn-root">Home / Presets</button>
        <button class="btn" id="btn-fav">Favoriten</button>
        <button class="btn btn-bad" id="btn-stop">Stop</button>
        <button class="btn" id="btn-remote-listen">Hören</button>
      </div>
      <h3>Aktuelle Slots <span id="remote-menu-meta"></span></h3>
      <table><thead><tr><th>#</th><th>Name</th><th></th></tr></thead>
      <tbody id="remote-menu-body"><tr><td colspan="3">lädt…</td></tr></tbody></table>
      <p class="meta">Max. <b>4 Slots</b> wie am USB-Stick — mit <b>Mehr…</b> blättern. Nach Öffnen kurz warten, bis die Liste aktualisiert.</p>
    </div>
  </section>

  <section class="pane" id="p-car">
    <div class="panel">
      <h3>USB-Anschlüsse</h3>
      <div class="ports">
        <div class="port" id="port-otg">
          <div class="topline"><span class="dot"></span><div><div class="plabel">OTG · Auto</div><div class="pname">Car Host</div></div></div>
          <div class="pstate" id="otg-state">—</div>
          <div class="phint">TinyUSB Mount am nativen USB. Event bei Stecken/Trennen.</div>
          <div class="pmeta" id="otg-meta"></div>
        </div>
        <div class="port" id="port-uart">
          <div class="topline"><span class="dot"></span><div><div class="plabel">UART · Pi / PC</div><div class="pname">Serial Bridge</div></div></div>
          <div class="pstate" id="uart-state">—</div>
          <div class="phint">Kein Plug-Sensor. <b>AKTIV</b>=Traffic · <b>STILL</b>=hatte RX, Kabel ok · <b>KEIN TRAFFIC</b>=noch nie. Später PUMP-Hello.</div>
          <div class="pmeta" id="uart-meta"></div>
        </div>
      </div>
      <h3>SoftAP Zugang</h3>
      <div class="apbox" id="ap-box">lädt…</div>
      <h3>Aktuelles Menü <span id="car-menu-meta"></span></h3>
      <table><thead><tr><th>#</th><th>Name</th><th>Art</th><th></th></tr></thead>
      <tbody id="car-menu-body"><tr><td colspan="4">lädt…</td></tr></tbody></table>
      <p class="meta" id="car-menu-hint">USB-Stick zeigt max. <b>4 Dateien</b> (MSC-Slots). Mehr Einträge → <b>Mehr…</b> tippen (Soft-Paging). SoftAP spiegelt dieselben 4 Slots.</p>
      <h3>Cover / Now Playing</h3>
      <div class="cover-wrap">
        <img id="cover-art" class="cover-art" alt="Cover" width="160" height="160">
        <div id="cover-empty" class="cover-empty">kein APIC<br>im sticky ID3</div>
        <div class="cover-meta">
          <div>Titel <b id="cover-title">—</b></div>
          <div style="margin-top:6px">ID3 <b id="cover-id3">—</b></div>
          <div style="margin-top:6px">Quelle <b id="cover-src">—</b></div>
          <div style="margin-top:6px">Aktuell <code id="cover-path">—</code></div>
          <div style="margin-top:6px">Ersetzen mit <code id="cover-replace">—</code></div>
          <div style="margin-top:4px;font-size:11px;word-break:break-all">Kandidaten <span id="cover-try">—</span></div>
          <div class="meta" style="margin-top:8px">Repo-Ordner <code>assets/usb-msc-covers/</code> · Default <code>default.jpg</code> wenn kein Station-Cover.</div>
        </div>
      </div>
      <h3>Live-Audio <span id="listen-meta">—</span></h3>
      <audio id="listen-audio" controls preload="none" style="width:100%;margin:6px 0"></audio>
      <button class="btn btn-a" id="btn-listen">Stream hören</button>
      <button class="btn" id="btn-listen-stop">Stop</button>
      <p class="meta">Hört den PUMP-MP3-Puffer im Browser (`/api/lab/listen`). Station zuerst per Play aktivieren · PUMP ●.</p>
      <h3>MSC Timing / Metriken</h3>
      <div id="metrics"></div>
      <h3>LBA Read-Trace (Host)</h3>
      <table><thead><tr><th>ms</th><th>LBA</th><th>n</th><th>kind</th><th>tag</th></tr></thead>
      <tbody id="trace-body"></tbody></table>
      <p class="meta">PC-Test: Prefetch ≠ Play. Play braucht Start nah am Dateianfang + genug sequentielle Bytes (Config <code>playMinSeqBytes</code>, Default 6000). Events: <code>msc.prefetch</code>, <code>play.reject</code>, <code>play.guess</code>, <code>msc.write</code>.</p>
    </div>
  </section>

  <section class="pane" id="p-menu">
    <div class="panel">
      <h3>PiDrive-Menü <span id="menu-meta"></span></h3>
      <table><thead><tr><th>Pfad</th><th>Name</th><th>UID</th><th></th></tr></thead>
      <tbody id="menu-body"><tr><td colspan="4">lädt…</td></tr></tbody></table>
      <div class="meta" id="menu-hint">Lebt über UART-PUMP. Folder = Öffnen · Station/Action = Play. Max. 4 FAT-Slots.</div>
      <button class="btn" id="btn-refresh">Refresh</button>
    </div>
  </section>

  <section class="pane" id="p-events">
    <div class="panel">
      <h3>Events</h3>
      <div id="ev-list"></div>
      <button class="btn" id="btn-ev-clear">Clear</button>
    </div>
  </section>

  <section class="pane" id="p-config">
    <div class="panel">
      <h3>Config</h3>
      <div class="row">
        <div><label>Gerätename</label><input id="cfg-name"></div>
        <div><label>SoftAP Pass (≥8)</label><input id="cfg-appass"></div>
      </div>
      <div class="row">
        <div><label>SoftAP (0/1)</label><input id="cfg-softap" type="number" min="0" max="1"></div>
        <div><label>STA / WiFiManager (0/1)</label><input id="cfg-sta" type="number" min="0" max="1"></div>
      </div>
      <div class="row">
        <div><label>Hub Host</label><input id="cfg-host"></div>
        <div><label>Hub Port</label><input id="cfg-port" type="number"></div>
      </div>
      <div class="row">
        <div><label>Hub aktiv (0/1)</label><input id="cfg-hub" type="number" min="0" max="1"></div>
        <div><label>Lab-Mode (0/1)</label><input id="cfg-lab" type="number" min="0" max="1"></div>
      </div>
      <div class="row">
        <div><label>Buffer Ziel (ms)</label><input id="cfg-buf" type="number"></div>
        <div></div>
      </div>
      <h3>Play-Detection (BMW A/B)</h3>
      <div class="row">
        <div><label>Plug-Fenster (ms)</label><input id="cfg-play-plug" type="number" min="0" step="100"></div>
        <div><label>Min. Seq-Bytes</label><input id="cfg-play-seq" type="number" min="512" step="512"></div>
      </div>
      <div class="row">
        <div><label>Head-LBA-Slop</label><input id="cfg-play-head" type="number" min="0" max="64"></div>
        <div><label>Cooldown (ms)</label><input id="cfg-play-cd" type="number" min="0" step="100"></div>
      </div>
      <div class="row">
        <div><label>Prefetch-LBA-Slop</label><input id="cfg-play-pf" type="number" min="0" max="32"></div>
        <div></div>
      </div>
      <button class="btn btn-a" id="btn-save">Speichern</button>
      <button class="btn" id="btn-restart">Neustart</button>
      <p class="meta">Car-Default: SoftAP an, STA aus. Play-Detection sofort aktiv (kein Reboot). Events: <code>play.reject</code> / <code>play.guess</code>.</p>
    </div>
  </section>

  <section class="pane" id="p-ota">
    <div class="panel">
      <h3>OTA Upload</h3>
      <div class="drop" id="drop">Firmware .bin hierher oder klicken</div>
      <input type="file" id="ota-file" accept=".bin" hidden>
      <div class="meta" id="ota-msg">Über SoftAP möglich — auch ohne Hub.</div>
    </div>
  </section>
</div>
<script>
const $=s=>document.querySelector(s);
let since=0;
let lastMenuRev=-1;
let menuBusy=false;
let lastCoverKey='';
function tabs(){
  document.querySelectorAll('.tab').forEach(t=>t.onclick=()=>{
    document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.pane').forEach(x=>x.classList.remove('active'));
    t.classList.add('active');
    $('#p-'+t.dataset.t).classList.add('active');
    if(t.dataset.t==='menu') refreshMenu(true);
  });
}
async function j(url,opt){
  const r=await fetch(url,opt);
  if(!r.ok) throw new Error(url+' '+r.status);
  return r.json();
}
function chip(el,on,warn){
  if(!el) return;
  el.classList.toggle('on',!!on && !warn);
  el.classList.toggle('off',!on);
  el.classList.toggle('warn',!!warn);
}
function actLabel(kind){
  if(kind==='folder') return 'Öffnen';
  if(kind==='info') return 'Info';
  if(kind==='station'||kind==='action') return 'Play';
  return 'Aktivieren';
}
function bindPlayButtons(root){
  if(!root) return;
  root.querySelectorAll('[data-uid]').forEach(b=>{
    b.onclick=async()=>{
      b.disabled=true;
      const prevRev=lastMenuRev;
      try{
        await j('/api/lab/play',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({uid:b.dataset.uid})});
        // Menü kann 200–800 ms brauchen — warten bis rev wechselt oder Timeout
        for(let i=0;i<12;i++){
          await refreshMenu(true);
          if(lastMenuRev!==prevRev) break;
          await new Promise(r=>setTimeout(r,120));
        }
        await refreshStatus(); await refreshEvents();
      }catch(e){
        alert('Play fehlgeschlagen: '+(e.message||e));
      }finally{ b.disabled=false; }
    };
  });
}
function renderMenuRows(items, mode){
  if(!items.length) return '<tr><td colspan="4">kein Menü — Bridge/PUMP prüfen</td></tr>';
  return items.map((it,i)=>{
    const btn=`<button class="btn" data-uid="${it.uid}">${actLabel(it.kind)}</button>`;
    if(mode==='remote'){
      return `<tr class="${it.playing?'play':''}"><td>${i+1}</td><td>${it.name||'?'}</td><td>${btn}</td></tr>`;
    }
    if(mode==='car'){
      return `<tr class="${it.playing?'play':''}"><td>${i+1}</td><td>${it.name||'?'}</td><td>${it.kind||''}</td><td>${btn}</td></tr>`;
    }
    return `<tr class="${it.playing?'play':''}"><td>${it.path||''}</td><td>${it.name||'?'}</td><td style="font-size:11px;word-break:break-all">${it.uid||''}</td><td>${btn}</td></tr>`;
  }).join('');
}
async function refreshMenu(force){
  if(menuBusy) return;
  menuBusy=true;
  try{
    const m=await j('/api/menu');
    const menu=m.menu||{};
    const items=menu.items||[];
    const rev=menu.rev!=null?menu.rev:0;
    if(!force && rev===lastMenuRev) return;
    lastMenuRev=rev;
    const metaTxt='('+items.length+(rev?' · rev '+rev:'')+')';
    const meta=$('#menu-meta'); if(meta) meta.textContent=metaTxt;
    const cmeta=$('#car-menu-meta'); if(cmeta) cmeta.textContent=metaTxt;
    const rmeta=$('#remote-menu-meta'); if(rmeta) rmeta.textContent=metaTxt;
    const body=$('#menu-body');
    if(body){ body.innerHTML=renderMenuRows(items,'full'); bindPlayButtons(body); }
    const cbody=$('#car-menu-body');
    if(cbody){ cbody.innerHTML=renderMenuRows(items,'car'); bindPlayButtons(cbody); }
    const rbody=$('#remote-menu-body');
    if(rbody){ rbody.innerHTML=renderMenuRows(items,'remote'); bindPlayButtons(rbody); }
  }catch(e){
    const body=$('#menu-body');
    if(body && !body.dataset.ok) body.innerHTML='<tr><td colspan="4">Menü-Fehler: '+(e.message||e)+'</td></tr>';
  }finally{ menuBusy=false; }
}
function fmtMs(v){return (v===undefined||v===null||v===0)?'—':v+' ms'}
function fmtAgo(ms){
  if(ms===undefined||ms===null) return '—';
  if(ms<1000) return ms+' ms';
  if(ms<60000) return Math.round(ms/1000)+' s';
  return Math.round(ms/60000)+' min';
}
function setPort(el, state, stateSel){
  if(!el) return;
  el.classList.toggle('up', state==='up');
  el.classList.toggle('sus', state==='sus');
  el.classList.toggle('quiet', state==='quiet');
  const st=$(stateSel);
  if(st){
    st.textContent = state==='up'?'AKTIV'
      :(state==='sus'?'SUSPEND'
      :(state==='quiet'?'STILL':'KEIN TRAFFIC'));
  }
}
function fillAp(s){
  const el=$('#ap-box');
  if(!el) return;
  el.innerHTML=
    'SSID <code>'+(s.softApSsid||'?')+'</code><br>'+
    'Pass <code>'+(s.softApPass||'?')+'</code><br>'+
    'URL <code>http://'+(s.softApIp||'192.168.4.1')+'/</code>'+
    (s.ip?'<br>STA <code>'+s.ip+'</code>':'');
}
function setCoverVisible(has){
  const img=$('#cover-art'), empty=$('#cover-empty');
  if(img) img.classList.toggle('on', !!has);
  if(empty) empty.classList.toggle('hide', !!has);
  const rimg=$('#remote-art'), rempty=$('#remote-empty');
  if(rimg) rimg.classList.toggle('on', !!has);
  if(rempty) rempty.classList.toggle('hide', !!has);
}
function updateCover(s){
  const st=s.stream||{};
  const id3=st.id3Len||0;
  const uid=st.uid||s.playingUid||'';
  const cov=s.cover||{};
  const titleEl=$('#cover-title');
  const id3El=$('#cover-id3');
  const bytesEl=$('#cover-bytes');
  const srcEl=$('#cover-src');
  const pathEl=$('#cover-path');
  const replEl=$('#cover-replace');
  const tryEl=$('#cover-try');
  const rTitle=$('#remote-title');
  const rSrc=$('#remote-src');
  const rPath=$('#remote-path');
  const rId3=$('#remote-id3');
  if(titleEl) titleEl.textContent=s.playingName||st.uid||'—';
  if(rTitle) rTitle.textContent=s.playingName||st.uid||'—';
  if(id3El) id3El.textContent=id3? (id3+' B') : '—';
  if(rId3) rId3.textContent=id3? (id3+' B') : '—';
  const map={file:'Datei',default:'Default',embedded:'MP3-APIC',generated:'Text',status:'Status',none:'—',error:'Fehler'};
  if(srcEl){
    srcEl.textContent=(map[cov.src]||cov.src||'—')+(cov.src?(' ('+cov.src+')'):'');
  }
  if(rSrc) rSrc.textContent=(map[cov.src]||cov.src||'—');
  if(pathEl) pathEl.textContent=cov.path||'—';
  if(rPath) rPath.textContent=cov.path||'—';
  // preferred replace target = first candidate or path for file override
  let preferred='stations/<menu_id>.jpg';
  const tries=(cov.try||'').split('|').filter(Boolean);
  if(tries.length) preferred=tries[0];
  else if(cov.path && cov.src==='file') preferred=cov.path;
  else if(cov.path && cov.src==='default') preferred=tries[0]||'stations/<menu_id>.jpg';
  if(replEl) replEl.textContent='assets/usb-msc-covers/'+preferred;
  if(tryEl) tryEl.textContent=tries.length?tries.map(t=>'assets/usb-msc-covers/'+t).join(' · '):'—';
  const key=uid+'|'+id3+'|'+(cov.src||'')+'|'+(cov.path||'');
  if(!id3){
    lastCoverKey='';
    setCoverVisible(false);
    if(bytesEl) bytesEl.textContent='—';
    const img=$('#cover-art');
    if(img){ img.removeAttribute('src'); }
    const rimg=$('#remote-art');
    if(rimg){ rimg.removeAttribute('src'); }
    return;
  }
  if(key===lastCoverKey) return;
  lastCoverKey=key;
  const url='/api/lab/cover?u='+encodeURIComponent(uid)+'&id3='+id3+'&t='+Date.now();
  const apply=(img)=>{
    if(!img) return;
    img.onload=()=>{ setCoverVisible(true); if(bytesEl) bytesEl.textContent='ok'; };
    img.onerror=()=>{ setCoverVisible(false); if(bytesEl) bytesEl.textContent='kein APIC'; };
    img.src=url;
  };
  apply($('#cover-art'));
  apply($('#remote-art'));
}
async function refreshStatus(){
  try{
    const s=await j('/api/status');
    fillAp(s);  // zuerst — SoftAP nie „lädt…“ hängen lassen
    $('#c-ver').innerHTML='v<b>'+s.version+'</b>';
    const otgUp=!!s.otgUp, otgSus=!!s.otgSuspended;
    const uartState=(s.uart&&s.uart.state)||(s.uartUp?'up':'idle');
    const uartUp=uartState==='up';
    const cO=$('#c-otg');
    if(cO){ cO.textContent='AUTO '+(otgUp?(otgSus?'◐':'●'):'○'); chip(cO, otgUp, otgUp&&otgSus); }
    const cU=$('#c-uart');
    if(cU){
      cU.textContent='PI '+(uartState==='up'?'●':(uartState==='quiet'?'◐':'○'));
      chip(cU, uartState==='up', uartState==='quiet');
    }
    const m=$('#c-msc'); if(m){ m.textContent='MSC '+(s.mscReady?'●':'○'); chip(m,s.mscReady); }
    const cPlay=$('#c-play'); if(cPlay) cPlay.innerHTML='PLAY <b>'+(s.playingName||'-')+'</b>';
    const cPump=$('#c-pump');
    if(cPump){ cPump.textContent='PUMP '+(s.pumpUp?'●':'○'); chip(cPump, !!s.pumpUp); }
    const lm=$('#listen-meta');
    if(lm){
      const st=s.stream||{};
      lm.textContent=st.active?('● '+(s.playingName||st.uid||'live')+' · '+(st.size||0)+' B'):'○ kein Stream';
    }
    updateCover(s);
    const lat=(s.msc&&s.msc.msPlugToPlayGuess)||0;
    const cLat=$('#c-lat'); if(cLat) cLat.innerHTML='LAT <b>'+(lat?lat+'ms':'-')+'</b>';
    const cAp=$('#c-ap'); if(cAp) cAp.innerHTML='AP <b>'+(s.softApIp||'-')+'</b>';

    const otg=(s.ports&&s.ports.otg)||{};
    const uart=(s.ports&&s.ports.uart)||{};
    const otgState=otgUp?(otgSus?'sus':'up'):'down';
    setPort($('#port-otg'), otgState, '#otg-state');
    const otgSt=$('#otg-state');
    if(otgSt) otgSt.textContent=otgUp?(otgSus?'SUSPEND':'VERBUNDEN'):'GETRENNT';
    const om=$('#otg-meta');
    if(om) om.textContent=
      'seit '+fmtAgo(otg.msSinceChange)+
      ' · up '+((s.msc&&s.msc.plugCount)||otg.plugCount||0)+
      ' · down '+((s.msc&&s.msc.unplugCount)||otg.unplugCount||0)+
      (otgUp?' · mounted '+fmtAgo((s.msc&&s.msc.msSincePlug)||0):'');
    const uartUi=uartState==='up'?'up':(uartState==='quiet'?'quiet':'down');
    setPort($('#port-uart'), uartUi, '#uart-state');
    const um=$('#uart-meta');
    if(um) um.textContent=
      'state '+uartState+
      ' · seit '+fmtAgo(uart.msSinceChange)+
      ' · lastRx '+fmtAgo((s.uart&&s.uart.msSinceRx)||0)+
      ' · rx '+(uart.rxBytes||(s.uart&&s.uart.rxBytes)||0)+' B';
    const mm=s.msc||{};
    const metrics=$('#metrics');
    if(metrics) metrics.innerHTML=[
      ['OTG (Auto)', otgUp?(otgSus?'suspend':'up'):'down'],
      ['UART (Pi)', uartState],
      ['PUMP', s.pumpUp?'up':'down'],
      ['MSC ready', s.mscReady?'yes':'no'],
      ['Reads / Writes', (mm.readCount||0)+' / '+(mm.writeCount||0)],
      ['Bytes R (meta/file)', (mm.bytesRead||0)+' ('+(mm.bytesMeta||0)+'/'+(mm.bytesFile||0)+')'],
      ['Last LBA', mm.lastReadLba||0],
      ['Prefetch hits', mm.prefetchHits||0],
      ['Plug → first read', fmtMs(mm.msPlugToFirstRead)],
      ['Plug → play guess', fmtMs(mm.msPlugToPlayGuess)],
      ['Playing', (s.playingName||'-')+' ('+(s.playingUid||'-')+')'],
      ['PUMP / Menü', (s.pumpUp?'up':'down')+' · '+(s.menuCount||0)+' slots · rev '+(s.menuRev||0)],
      ['Heap', s.freeHeap],
      ['Uptime', s.uptime],
      ['LED', s.led||'-']
    ].map(([k,v])=>`<div class="metric"><span>${k}</span><b>${v}</b></div>`).join('');
    const tb=$('#trace-body');
    if(tb){
      const tr=Array.isArray(s.mscTrace)?s.mscTrace:[];
      tb.innerHTML=tr.slice().reverse().slice(0,24).map(r=>
        `<tr><td>${r.ms}</td><td>${r.lba}</td><td>${r.n}</td><td>${r.kind}</td><td>${r.tag||''}</td></tr>`
      ).join('')||'<tr><td colspan="5">noch keine Reads</td></tr>';
    }
    const rev=s.menuRev!=null?s.menuRev:lastMenuRev;
    if(rev!==lastMenuRev) await refreshMenu(true);
  }catch(err){
    const el=$('#ap-box');
    if(el) el.innerHTML='Status-Fehler: <code>'+String(err.message||err)+'</code> — Hard-Reload versuchen';
  }
}
async function refreshEvents(){
  try{
    const e=await j('/api/events?since='+since);
    const list=$('#ev-list'); if(!list) return;
    (e.events||[]).forEach(ev=>{
      since=Math.max(since,ev.seq);
      const d=document.createElement('div');
      d.className='ev';
      d.innerHTML=`<span class="c">${ev.code}</span> <span class="d">${ev.detail||''}</span> <span class="d">@${ev.ms}</span>`;
      list.prepend(d);
    });
  }catch(e){}
}
async function loadConfig(){
  try{
    const c=(await j('/api/config')).config;
    $('#cfg-name').value=c.deviceName||'';
    $('#cfg-appass').value=c.softApPass||'';
    $('#cfg-softap').value=c.enableSoftAp?1:0;
    $('#cfg-sta').value=c.enableSta?1:0;
    $('#cfg-host').value=c.hubHost||'';
    $('#cfg-port').value=c.hubPort||8093;
    $('#cfg-buf').value=c.bufferTargetMs||5000;
    $('#cfg-hub').value=c.enableHub?1:0;
    $('#cfg-lab').value=c.labMode?1:0;
    $('#cfg-play-plug').value=c.playPlugWindowMs??2500;
    $('#cfg-play-seq').value=c.playMinSeqBytes??6000;
    $('#cfg-play-head').value=c.playHeadLbaSlop??12;
    $('#cfg-play-cd').value=c.playCooldownMs??5000;
    $('#cfg-play-pf').value=c.playPrefetchLbaSlop??2;
  }catch(e){}
}
async function saveConfig(){
  const body={
    deviceName:$('#cfg-name').value,
    softApPass:$('#cfg-appass').value,
    enableSoftAp:+$('#cfg-softap').value===1,
    enableSta:+$('#cfg-sta').value===1,
    hubHost:$('#cfg-host').value,
    hubPort:+$('#cfg-port').value,
    bufferTargetMs:+$('#cfg-buf').value,
    enableHub:+$('#cfg-hub').value===1,
    labMode:+$('#cfg-lab').value===1,
    playPlugWindowMs:+$('#cfg-play-plug').value,
    playMinSeqBytes:+$('#cfg-play-seq').value,
    playHeadLbaSlop:+$('#cfg-play-head').value,
    playCooldownMs:+$('#cfg-play-cd').value,
    playPrefetchLbaSlop:+$('#cfg-play-pf').value
  };
  await j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  await refreshEvents();
}
function setupOta(){
  const drop=$('#drop'), file=$('#ota-file');
  if(!drop||!file) return;
  drop.onclick=()=>file.click();
  drop.ondragover=e=>{e.preventDefault();};
  drop.ondrop=e=>{e.preventDefault(); if(e.dataTransfer.files[0]) upload(e.dataTransfer.files[0]);};
  file.onchange=()=>{ if(file.files[0]) upload(file.files[0]); };
}
async function upload(f){
  $('#ota-msg').textContent='Upload '+f.name+'…';
  const fd=new FormData(); fd.append('firmware', f);
  const r=await fetch('/ota-upload',{method:'POST',body:fd});
  $('#ota-msg').textContent=r.ok?'OK — Neustart':'Fehler '+r.status;
}
tabs();
$('#btn-refresh').onclick=async()=>{await refreshMenu(true); await refreshStatus();};
async function labPlayUid(uid){
  await j('/api/lab/play',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({uid})});
  await refreshMenu(true); await refreshStatus(); await refreshEvents();
}
const btnRoot=$('#btn-root'); if(btnRoot) btnRoot.onclick=()=>labPlayUid('pump:root');
const btnFav=$('#btn-fav'); if(btnFav) btnFav.onclick=()=>labPlayUid('pump:favoriten');
const btnStop=$('#btn-stop'); if(btnStop) btnStop.onclick=async()=>{
  try{ await j('/api/lab/stop',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'}); }
  catch(e){ try{ await labPlayUid('pump:stop'); }catch(_){ alert('Stop: '+(e.message||e)); } }
  await refreshStatus(); await refreshEvents();
};
$('#btn-listen').onclick=()=>{
  const a=$('#listen-audio'); if(!a) return;
  a.src='/api/lab/listen?t='+Date.now();
  a.play().catch(err=>alert('Audio: '+(err.message||err)+' — zuerst Station Play, Puffer füllen lassen'));
};
const btnRL=$('#btn-remote-listen');
if(btnRL) btnRL.onclick=()=>{
  const a=$('#listen-audio'); if(!a) return;
  a.src='/api/lab/listen?t='+Date.now();
  a.play().catch(err=>alert('Audio: '+(err.message||err)));
};
$('#btn-listen-stop').onclick=()=>{
  const a=$('#listen-audio'); if(!a) return;
  a.pause(); a.removeAttribute('src'); a.load();
};
$('#btn-ev-clear').onclick=async()=>{await fetch('/api/events',{method:'DELETE'}); $('#ev-list').innerHTML=''; since=0; await refreshEvents();};
$('#btn-save').onclick=()=>saveConfig();
$('#btn-restart').onclick=()=>fetch('/api/restart',{method:'POST'});
setupOta();
(async()=>{
  // sequentiell — ESP-WebServer mag keine parallelen Requests
  await refreshStatus();
  await refreshMenu(true);
  await refreshEvents();
  await loadConfig();
  setInterval(async()=>{
    await refreshStatus();
    await refreshEvents();
  },1000);
})();
</script>
</body></html>
)HTML";

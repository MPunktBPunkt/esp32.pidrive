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
.port .topline{display:flex;align-items:center;gap:10px}
.port .dot{width:16px;height:16px;border-radius:50%;background:#3a4555;flex-shrink:0;transition:background .2s,box-shadow .2s}
.port.up .dot{background:var(--ok);box-shadow:0 0 14px rgba(61,214,140,.6)}
.port.sus .dot{background:var(--warn);box-shadow:0 0 14px rgba(245,165,36,.45)}
.port .plabel{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}
.port .pname{font-size:1.05rem;font-weight:700;letter-spacing:-.02em}
.port .pstate{margin-top:10px;font-size:1.35rem;font-weight:700;letter-spacing:-.02em}
.port.up .pstate{color:var(--ok)}
.port.sus .pstate{color:var(--warn)}
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
    <span class="chip" id="c-lat">LAT -</span>
    <span class="chip" id="c-ap">AP -</span>
  </div>
  <nav>
    <button class="tab active" data-t="car">Auto-Test</button>
    <button class="tab" data-t="menu">Menü</button>
    <button class="tab" data-t="events">Events</button>
    <button class="tab" data-t="config">Config</button>
    <button class="tab" data-t="ota">OTA</button>
  </nav>

  <section class="pane active" id="p-car">
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
          <div class="phint">Kein Plug-Sensor am Bridge-Chip — Status = Seriellaktivität (später PUMP).</div>
          <div class="pmeta" id="uart-meta"></div>
        </div>
      </div>
      <h3>SoftAP Zugang</h3>
      <div class="apbox" id="ap-box">lädt…</div>
      <h3>MSC Timing / Metriken</h3>
      <div id="metrics"></div>
      <h3>LBA Read-Trace (Host)</h3>
      <table><thead><tr><th>ms</th><th>LBA</th><th>n</th><th>kind</th><th>tag</th></tr></thead>
      <tbody id="trace-body"></tbody></table>
      <p class="meta">PC-Test: Prefetch ≠ Play. Play braucht Start nah am Dateianfang + ≥8 KiB sequentiell. Events: <code>msc.prefetch</code>, <code>play.guess</code>, <code>msc.write</code>.</p>
    </div>
  </section>

  <section class="pane" id="p-menu">
    <div class="panel">
      <h3>Virtuelles FAT <span id="menu-meta"></span></h3>
      <table><thead><tr><th>Pfad</th><th>Name</th><th>UID</th><th></th></tr></thead>
      <tbody id="menu-body"></tbody></table>
      <div class="meta">FAT12-Demo. Play-Guess: Start nahe Dateianfang + ≥8 KiB sequentiell (Prefetch wird gefiltert).</div>
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
      <button class="btn btn-a" id="btn-save">Speichern</button>
      <button class="btn" id="btn-restart">Neustart</button>
      <p class="meta">Car-Default: SoftAP an, STA aus. STA nur für Hub/Home-WLAN einschalten.</p>
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
function tabs(){
  document.querySelectorAll('.tab').forEach(t=>t.onclick=()=>{
    document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.pane').forEach(x=>x.classList.remove('active'));
    t.classList.add('active');
    $('#p-'+t.dataset.t).classList.add('active');
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
  el.classList.toggle('warn',!!warn);
  el.classList.toggle('off',!on && !warn);
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
  const st=$(stateSel);
  if(st) st.textContent = state==='up'?'VERBUNDEN':(state==='sus'?'SUSPEND':'GETRENNT');
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
async function refreshStatus(){
  try{
    const s=await j('/api/status');
    fillAp(s);  // zuerst — SoftAP nie „lädt…“ hängen lassen
    $('#c-ver').innerHTML='v<b>'+s.version+'</b>';
    const otgUp=!!s.otgUp, otgSus=!!s.otgSuspended, uartUp=!!s.uartUp;
    const cO=$('#c-otg');
    if(cO){ cO.textContent='AUTO '+(otgUp?(otgSus?'◐':'●'):'○'); chip(cO, otgUp, otgUp&&otgSus); }
    const cU=$('#c-uart');
    if(cU){ cU.textContent='PI '+(uartUp?'●':'○'); chip(cU, uartUp); }
    const m=$('#c-msc'); if(m){ m.textContent='MSC '+(s.mscReady?'●':'○'); chip(m,s.mscReady); }
    const cPlay=$('#c-play'); if(cPlay) cPlay.innerHTML='PLAY <b>'+(s.playingName||'-')+'</b>';
    const lat=(s.msc&&s.msc.msPlugToPlayGuess)||0;
    const cLat=$('#c-lat'); if(cLat) cLat.innerHTML='LAT <b>'+(lat?lat+'ms':'-')+'</b>';
    const cAp=$('#c-ap'); if(cAp) cAp.innerHTML='AP <b>'+(s.softApIp||'-')+'</b>';

    const otg=(s.ports&&s.ports.otg)||{};
    const uart=(s.ports&&s.ports.uart)||{};
    const otgState=otgUp?(otgSus?'sus':'up'):'down';
    setPort($('#port-otg'), otgState, '#otg-state');
    const om=$('#otg-meta');
    if(om) om.textContent=
      'seit '+fmtAgo(otg.msSinceChange)+
      ' · up '+((s.msc&&s.msc.plugCount)||otg.plugCount||0)+
      ' · down '+((s.msc&&s.msc.unplugCount)||otg.unplugCount||0)+
      (otgUp?' · mounted '+fmtAgo((s.msc&&s.msc.msSincePlug)||0):'');
    setPort($('#port-uart'), uartUp?'up':'down', '#uart-state');
    const um=$('#uart-meta');
    if(um) um.textContent=
      'seit '+fmtAgo(uart.msSinceChange)+
      ' · rx '+(uart.rxBytes||0)+' B · sense serial-activity';

    const mm=s.msc||{};
    const metrics=$('#metrics');
    if(metrics) metrics.innerHTML=[
      ['OTG (Auto)', otgUp?(otgSus?'suspend':'up'):'down'],
      ['UART (Pi)', uartUp?'activity':'idle'],
      ['MSC ready', s.mscReady?'yes':'no'],
      ['Reads / Writes', (mm.readCount||0)+' / '+(mm.writeCount||0)],
      ['Bytes R (meta/file)', (mm.bytesRead||0)+' ('+(mm.bytesMeta||0)+'/'+(mm.bytesFile||0)+')'],
      ['Last LBA', mm.lastReadLba||0],
      ['Prefetch hits', mm.prefetchHits||0],
      ['Plug → first read', fmtMs(mm.msPlugToFirstRead)],
      ['Plug → play guess', fmtMs(mm.msPlugToPlayGuess)],
      ['Playing', (s.playingName||'-')+' ('+(s.playingUid||'-')+')'],
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
  }catch(err){
    const el=$('#ap-box');
    if(el) el.innerHTML='Status-Fehler: <code>'+String(err.message||err)+'</code> — Hard-Reload versuchen';
  }
}
async function refreshMenu(){
  try{
    const m=await j('/api/menu');
    const items=(m.menu&&m.menu.items)||[];
    const meta=$('#menu-meta'); if(meta) meta.textContent='('+items.length+')';
    const body=$('#menu-body'); if(!body) return;
    body.innerHTML=items.map(it=>`<tr class="${it.playing?'play':''}">
      <td>${it.path}</td><td>${it.name}</td><td>${it.uid}</td>
      <td>${it.kind==='station'||it.kind==='action'?`<button class="btn" data-uid="${it.uid}">Play</button>`:''}</td></tr>`).join('');
    body.querySelectorAll('.btn').forEach(b=>b.onclick=async()=>{
      await j('/api/lab/play',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({uid:b.dataset.uid})});
      await refreshMenu(); await refreshStatus(); await refreshEvents();
    });
  }catch(e){}
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
    labMode:+$('#cfg-lab').value===1
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
$('#btn-refresh').onclick=async()=>{await refreshMenu(); await refreshStatus();};
$('#btn-ev-clear').onclick=async()=>{await fetch('/api/events',{method:'DELETE'}); $('#ev-list').innerHTML=''; since=0; await refreshEvents();};
$('#btn-save').onclick=()=>saveConfig();
$('#btn-restart').onclick=()=>fetch('/api/restart',{method:'POST'});
setupOta();
(async()=>{
  // sequentiell — ESP-WebServer mag keine parallelen Requests
  await refreshStatus();
  await refreshMenu();
  await refreshEvents();
  await loadConfig();
  setInterval(async()=>{ await refreshStatus(); await refreshEvents(); },1000);
})();
</script>
</body></html>
)HTML";

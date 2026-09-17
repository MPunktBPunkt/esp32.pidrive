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
nav{display:flex;gap:2px;border-bottom:1px solid var(--line);margin:12px 0 16px;overflow:auto}
.tab{appearance:none;border:0;background:0;color:var(--muted);padding:10px 12px;cursor:pointer;font:inherit;border-bottom:2px solid transparent;white-space:nowrap}
.tab.active{color:var(--acc);border-bottom-color:var(--acc)}
.pane{display:none}.pane.active{display:block}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:12px}
.panel h3{font-size:11px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin-bottom:10px}
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
</style></head><body>
<div class="shell">
  <h1>esp32.pidrive</h1>
  <p class="sub">USB-MSC-Gadget für PiDrive · Diagnose-WebUI</p>
  <div class="bar" id="bar">
    <span class="chip" id="c-ver">v-</span>
    <span class="chip off" id="c-usb">USB</span>
    <span class="chip off" id="c-pump">PUMP</span>
    <span class="chip" id="c-buf">BUF -</span>
    <span class="chip" id="c-play">PLAY -</span>
    <span class="chip" id="c-ip">IP -</span>
  </div>
  <nav>
    <button class="tab active" data-t="menu">Menü</button>
    <button class="tab" data-t="events">Events</button>
    <button class="tab" data-t="config">Config</button>
    <button class="tab" data-t="ota">OTA</button>
  </nav>

  <section class="pane active" id="p-menu">
    <div class="panel">
      <h3>Virtuelles FAT <span id="menu-meta"></span></h3>
      <table><thead><tr><th>Pfad</th><th>Name</th><th>UID</th><th></th></tr></thead>
      <tbody id="menu-body"></tbody></table>
      <div class="meta">V0.1: Demo-Menü. Echtes MSC folgt. Lab: Simulate play / USB-Toggle.</div>
      <button class="btn" id="btn-usb">USB lab-toggle</button>
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
        <div><label>Hub Host</label><input id="cfg-host"></div>
      </div>
      <div class="row">
        <div><label>Hub Port</label><input id="cfg-port" type="number"></div>
        <div><label>Buffer Ziel (ms)</label><input id="cfg-buf" type="number"></div>
      </div>
      <div class="row">
        <div><label>Hub aktiv (0/1)</label><input id="cfg-hub" type="number" min="0" max="1"></div>
        <div><label>Lab-Mode (0/1)</label><input id="cfg-lab" type="number" min="0" max="1"></div>
      </div>
      <button class="btn btn-a" id="btn-save">Speichern</button>
      <button class="btn" id="btn-restart">Neustart</button>
    </div>
  </section>

  <section class="pane" id="p-ota">
    <div class="panel">
      <h3>OTA Upload</h3>
      <div class="drop" id="drop">Firmware .bin hierher oder klicken</div>
      <input type="file" id="ota-file" accept=".bin" hidden>
      <div class="meta" id="ota-msg">Auch via esp-hub OTA-Push (Carport).</div>
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
async function j(url,opt){const r=await fetch(url,opt);return r.json()}
function chip(el,on){el.classList.toggle('on',!!on);el.classList.toggle('off',!on)}
async function refreshStatus(){
  const s=await j('/api/status');
  $('#c-ver').innerHTML='v<b>'+s.version+'</b>';
  const u=$('#c-usb'); u.textContent='USB '+(s.usbEnumerated?'●':'○'); chip(u,s.usbEnumerated);
  const p=$('#c-pump'); p.textContent='PUMP '+(s.pumpUp?'●':'○'); chip(p,s.pumpUp);
  $('#c-buf').innerHTML='BUF <b>'+s.bufferMs+'</b>/'+s.bufferTargetMs+'ms';
  $('#c-play').innerHTML='PLAY <b>'+(s.playingName||'-')+'</b>';
  $('#c-ip').innerHTML='IP <b>'+s.ip+'</b>';
}
async function refreshMenu(){
  const m=await j('/api/menu');
  const items=m.menu.items||[];
  $('#menu-meta').textContent='('+items.length+')';
  $('#menu-body').innerHTML=items.map(it=>`<tr class="${it.playing?'play':''}">
    <td>${it.path}</td><td>${it.name}</td><td>${it.uid}</td>
    <td>${it.kind==='station'||it.kind==='action'?`<button class="btn" data-uid="${it.uid}">Play</button>`:''}</td></tr>`).join('');
  document.querySelectorAll('#menu-body .btn').forEach(b=>b.onclick=async()=>{
    await j('/api/lab/play',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({uid:b.dataset.uid})});
    refreshMenu(); refreshStatus(); refreshEvents();
  });
}
async function refreshEvents(){
  const e=await j('/api/events?since='+since);
  (e.events||[]).forEach(ev=>{
    since=Math.max(since,ev.seq);
    const d=document.createElement('div');
    d.className='ev';
    d.innerHTML=`<span class="c">${ev.code}</span> <span class="d">${ev.detail||''}</span> <span class="d">@${ev.ms}</span>`;
    $('#ev-list').prepend(d);
  });
}
async function loadConfig(){
  const c=(await j('/api/config')).config;
  $('#cfg-name').value=c.deviceName||'';
  $('#cfg-host').value=c.hubHost||'';
  $('#cfg-port').value=c.hubPort||8093;
  $('#cfg-buf').value=c.bufferTargetMs||5000;
  $('#cfg-hub').value=c.enableHub?1:0;
  $('#cfg-lab').value=c.labMode?1:0;
}
async function saveConfig(){
  const body={
    deviceName:$('#cfg-name').value,
    hubHost:$('#cfg-host').value,
    hubPort:+$('#cfg-port').value,
    bufferTargetMs:+$('#cfg-buf').value,
    enableHub:+$('#cfg-hub').value===1,
    labMode:+$('#cfg-lab').value===1
  };
  await j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  refreshEvents();
}
function setupOta(){
  const drop=$('#drop'), file=$('#ota-file');
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
$('#btn-refresh').onclick=()=>{refreshMenu();refreshStatus()};
$('#btn-usb').onclick=async()=>{await j('/api/lab/usb-toggle',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});refreshStatus();refreshEvents()};
$('#btn-ev-clear').onclick=async()=>{await fetch('/api/events',{method:'DELETE'}); $('#ev-list').innerHTML=''; since=0; refreshEvents()};
$('#btn-save').onclick=saveConfig;
$('#btn-restart').onclick=()=>fetch('/api/restart',{method:'POST'});
setupOta();
refreshStatus(); refreshMenu(); refreshEvents(); loadConfig();
setInterval(()=>{refreshStatus(); refreshEvents()},2000);
</script>
</body></html>
)HTML";

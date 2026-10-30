#pragma once

// Trang web dieu khien Radio (nhung thang trong firmware, phuc vu o http://<IP>/)
static const char kRadioPage[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Eri Radio</title>
<style>
body{margin:0;background:#0b0f14;color:#e6edf3;font:15px system-ui,sans-serif}
.w{max-width:560px;margin:auto;padding:16px}
h1{font-size:13px;letter-spacing:.2em;color:#58a6ff;margin:0 0 14px}
.c{background:#131a22;border:1px solid #243040;border-radius:12px;padding:14px;margin-bottom:14px}
#n{font-size:20px;font-weight:700}
#t{color:#8b9bb0;margin-top:4px;min-height:1.2em}
#m{font-size:12px;color:#58a6ff;margin-top:6px}
input[type=range]{width:100%}
.r{display:flex;gap:6px;margin:6px 0}
.r input{flex:1;min-width:0;background:#0b0f14;color:inherit;border:1px solid #243040;border-radius:8px;padding:8px;font:inherit}
.r input.n{flex:0 0 32%}
.r.on input{border-color:#58a6ff}
button{background:#1f6feb;color:#fff;border:0;border-radius:8px;padding:8px 12px;font:inherit}
button.g{background:#243040}
.b{margin-top:10px;display:flex;gap:8px}
</style></head><body><div class=w><h1>ERI RADIO</h1>
<div class=c><div id=n>-</div><div id=t></div><div id=m></div></div>
<div class=c>Volume <b id=vv></b><input id=v type=range min=0 max=100 step=5></div>
<div class=c><div id=l></div>
<div class=b><button class=g onclick=add()>+ Add</button><button onclick=save()>Save list</button><button class=g onclick=rst()>Reset</button></div></div>
</div><script>
const $=s=>document.getElementById(s);
const q=async(p,o)=>(await fetch(p,o)).text();
function row(n,u){
 const d=document.createElement('div');d.className='r';
 d.innerHTML='<button class=g>&#9654;</button><input class=n maxlength=23 placeholder=Name><input maxlength=159 placeholder=http://...><button class=g>&#10005;</button>';
 const i=d.querySelectorAll('input'),b=d.querySelectorAll('button');
 i[0].value=n;i[1].value=u;
 b[0].onclick=()=>{q('/api/play?i='+[...$('l').children].indexOf(d),{method:'POST'}).then(poll)};
 b[1].onclick=()=>d.remove();
 $('l').appendChild(d)}
async function load(){
 $('l').innerHTML='';
 (await q('/api/list')).split('\n').forEach(s=>{const k=s.indexOf('|');if(k>0)row(s.slice(0,k),s.slice(k+1))})}
function add(){row('','')}
async function save(){
 const t=[...$('l').children].map(d=>{const i=d.querySelectorAll('input');
  return i[0].value.replace(/[|\n]/g,' ')+'|'+i[1].value.trim()}).join('\n');
 const r=await fetch('/api/list',{method:'POST',body:t});
 alert(r.ok?'Saved':'Error: '+await r.text());load();poll()}
async function rst(){if(confirm('Restore the built-in list?')){await q('/api/reset',{method:'POST'});load();poll()}}
let vt;
$('v').oninput=e=>{$('vv').textContent=e.target.value;clearTimeout(vt);
 vt=setTimeout(()=>q('/api/vol?v='+e.target.value,{method:'POST'}),150)};
async function poll(){try{
 const s=JSON.parse(await q('/api/state'));
 $('n').textContent=s.n;$('t').textContent=s.t;$('m').textContent=(s.m||s.p)+(s.b>=0?' \u00b7 Battery '+s.b+'% ('+(s.mv/1000).toFixed(2)+'V)':'');
 if(document.activeElement!==$('v')){$('v').value=s.v;$('vv').textContent=s.v}
 [...$('l').children].forEach((d,k)=>d.classList.toggle('on',k==s.i))}catch(e){}}
q('/api/time?t='+Math.floor(Date.now()/1000-new Date().getTimezoneOffset()*60),{method:'POST'});
load();poll();setInterval(poll,2000);
</script></body></html>)HTML";

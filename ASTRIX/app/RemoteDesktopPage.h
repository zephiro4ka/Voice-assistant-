#pragma once
// Страница Telegram Mini App: пульт с вкладками «Главная», «Удалёнка», «Быстрый запуск», «Настройки».

inline constexpr const char* kRemoteDesktopPage = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<title>ASTRIX</title>
<script src="https://telegram.org/js/telegram-web-app.js"></script>
<style>
:root{--a:#6b4fd8;--bg:#0d0b14;--c:#1a1530;--c2:#272042;--t:#eee9f7;--m:#a79cc0}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;user-select:none;-webkit-user-select:none}
html,body{margin:0;height:100%;background:var(--bg);color:var(--t);font:15px system-ui,-apple-system,Segoe UI,Roboto,sans-serif;overflow:hidden;overscroll-behavior:none}
#app{position:fixed;inset:0;display:flex;flex-direction:column}
#pages{flex:1;min-height:0;position:relative}
.page{position:absolute;inset:0;display:none;overflow-y:auto;padding:16px 14px 20px;-webkit-overflow-scrolling:touch}
.page.on{display:block;animation:in .18s ease-out}
@keyframes in{from{opacity:0;transform:translateY(6px)}}
#nav{display:flex;background:#15112a;padding:6px 4px calc(6px + env(safe-area-inset-bottom));border-top:1px solid #ffffff10}
#nav button{flex:1;display:flex;flex-direction:column;gap:2px;align-items:center;justify-content:center;height:52px;min-width:0;padding:0;background:none;color:var(--m);font-size:11px;font-weight:600;border-radius:12px}
#nav button i{font-style:normal;font-size:20px}
#nav button.on{color:#fff;background:color-mix(in srgb,var(--a) 35%,transparent)}
button{flex:none;min-width:44px;height:42px;padding:0 12px;border:0;border-radius:12px;background:var(--c2);color:var(--t);font:inherit;font-weight:600;white-space:nowrap}
button.main{background:var(--a)}button.danger{background:#7a2840}button.on{background:var(--a)}
h2{margin:0;font-size:22px}.head{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;min-height:42px}
.card{background:var(--c);border-radius:18px;padding:14px;margin-bottom:12px}
#status .row{display:flex;justify-content:space-between;font-size:13px;color:var(--m);margin:7px 0 3px}
.meter{height:6px;border-radius:9px;background:#ffffff14;overflow:hidden}.meter b{display:block;height:100%;background:var(--a);border-radius:9px;transition:width .4s}
#track{margin-top:10px;font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.grid{display:grid;gap:10px}
.tile{position:relative;height:auto;min-width:0;white-space:normal;aspect-ratio:1/.86;border:1px solid #ffffff0f;display:flex;flex-direction:column;gap:6px;align-items:center;justify-content:center;padding:6px;border-radius:18px;background:var(--c);font-size:12.5px;line-height:1.15;text-align:center;font-weight:600;transition:transform .1s}
.tile:active{transform:scale(.94)}.tile i{font-style:normal;font-size:28px}.tile span{overflow:hidden;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical}
.tile.add{background:none;border:2px dashed #ffffff26;color:var(--m)}
.edit .tile:not(.add){outline:2px solid color-mix(in srgb,var(--a) 70%,transparent)}
.empty{color:var(--m);text-align:center;padding:32px 16px;line-height:1.45}
#p-remote{padding:0;overflow:hidden}
#remote{position:absolute;inset:0;display:flex;flex-direction:column;background:var(--bg)}
#remote.rot{position:fixed;inset:auto;left:100%;top:0;transform-origin:0 0;transform:rotate(90deg);z-index:20}
#stage{position:relative;flex:1;min-height:0;overflow:hidden;touch-action:none;background:radial-gradient(circle at 50% 30%,#1c1630,#0d0b14 70%)}
#screen{position:absolute;left:0;top:0;transform-origin:0 0;display:none;pointer-events:none}
#notice{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:12px;padding:32px;text-align:center;color:#b9aed0;line-height:1.4}
#spin{width:34px;height:34px;border:3px solid #6b4fd833;border-top-color:#9a7bff;border-radius:50%;animation:s 1s linear infinite}
@keyframes s{to{transform:rotate(360deg)}}
#net{position:absolute;right:8px;top:8px;padding:3px 9px;border-radius:99px;background:#17122acc;font-size:12px;color:#bcaedb;pointer-events:none}
#bar,#keys{display:flex;gap:6px;overflow-x:auto;scrollbar-width:none;touch-action:pan-x}
#bar{padding:6px;background:#15112a}#bar::-webkit-scrollbar,#keys::-webkit-scrollbar{display:none}
#panel{display:none;flex-direction:column;gap:6px;padding:6px;background:#15112a}#panel.open{display:flex}
#row{display:flex;gap:6px}input,select,textarea{width:100%;min-width:0;padding:10px 12px;border:1px solid #39305a;border-radius:12px;background:#0f0c1d;color:#fff;font:inherit;outline:none;user-select:text;-webkit-user-select:text}
#sheet{position:fixed;inset:0;z-index:40;display:none;align-items:flex-end;background:#0009}#sheet.on{display:flex}
#sbody{width:100%;max-height:88%;overflow-y:auto;background:#1a1530;border-radius:22px 22px 0 0;padding:18px 16px calc(18px + env(safe-area-inset-bottom));animation:up .2s ease-out}
@keyframes up{from{transform:translateY(40px);opacity:0}}
#sbody h3{margin:0 0 12px}#sbody label{display:block;margin:10px 0 5px;color:var(--m);font-size:13px}#sbody .acts{display:flex;gap:8px;margin-top:16px;flex-wrap:wrap}#sbody .acts button{flex:1}
#sbody img{width:100%;border-radius:12px;display:block}.answer{white-space:pre-wrap;user-select:text;-webkit-user-select:text;line-height:1.4;margin-top:12px}
#toast{position:fixed;left:50%;top:14px;transform:translate(-50%,-70px);z-index:50;max-width:88%;padding:10px 16px;border-radius:14px;background:#2b2345;box-shadow:0 8px 28px #0008;transition:transform .22s;font-weight:600;font-size:14px;text-align:center}#toast.on{transform:translate(-50%,0)}
.opt{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:44px}.opt+.opt{border-top:1px solid #ffffff0d}.opt small{display:block;color:var(--m);font-weight:400;margin-top:2px}
.sw{width:50px;height:30px;min-width:0;padding:0;border-radius:99px;position:relative;background:#39305a}.sw:after{content:"";position:absolute;left:3px;top:3px;width:24px;height:24px;border-radius:50%;background:#fff;transition:left .15s}.sw.on{background:var(--a)}.sw.on:after{left:23px}
.dots{display:flex;gap:8px;flex-wrap:wrap}.dot{width:34px;height:34px;min-width:0;padding:0;border-radius:50%}.dot.on{outline:3px solid #fff}
</style></head><body>
<div id="app">
 <div id="pages">
  <section class="page" id="p-home"><div class="head"><h2>Главная</h2><button id="eh">✎</button></div><div class="card" id="status"></div><div class="grid" id="tiles"></div></section>
  <section class="page" id="p-remote"><div id="remote">
   <div id="stage"><img id="screen" alt=""><div id="notice"><div id="spin"></div><div id="msg">Подключаюсь…</div></div><div id="net" hidden></div></div>
   <div id="panel"><div id="row"><input id="text" placeholder="Текст для компьютера" autocomplete="off"><button id="send" class="main">Ввести</button></div>
    <div id="keys"><button data-k="enter">Enter</button><button data-k="back">⌫</button><button data-k="del">Del</button><button data-k="tab">Tab</button><button data-k="esc">Esc</button><button data-k="copy">Ctrl+C</button><button data-k="paste">Ctrl+V</button></div></div>
   <div id="bar"><button id="rot">⟳</button><button id="full" hidden>⛶</button><button id="kb">⌨️</button><button id="up">▲</button><button id="down">▼</button><button id="lockm" hidden>🎯</button>
    <button data-k="alttab">Alt+Tab</button><button data-k="win">Пуск</button><button data-k="desktop">Win+D</button><button id="mon" hidden>Экран 1</button><button id="hd">HD</button><span id="pad" hidden style="align-self:center;padding:0 6px">🎮</span></div>
  </div></section>
  <section class="page" id="p-launch"><div class="head"><h2>Быстрый запуск</h2><button id="el">✎</button></div><div class="grid" id="apps"></div></section>
  <section class="page" id="p-set"><div class="head"><h2>Настройки</h2></div><div id="opts"></div></section>
 </div>
 <div id="nav"><button data-p="home"><i>🏠</i>Главная</button><button data-p="remote"><i>🖥</i>Удалёнка</button><button data-p="launch"><i>🚀</i>Быстрый запуск</button><button data-p="set"><i>⚙️</i>Настройки</button></div>
</div>
<div id="sheet"><div id="sbody"></div></div><div id="toast"></div>
<script>
)HTML"
R"HTML(const $=id=>document.getElementById(id),tg=window.Telegram&&Telegram.WebApp,sleep=ms=>new Promise(r=>setTimeout(r,ms));
function el(tag,props,...kids){const e=document.createElement(tag);for(const k in props||{}){if(k==='style')e.style.cssText=props[k];else e[k]=props[k]}kids.forEach(c=>e.append(c));return e}
let session='',cfg={home:[],apps:[],settings:{}},catalog=null,page='home',editHome=false,editApps=false,toastT=0;
const ACT={vol_up:['🔊','Громче'],vol_down:['🔉','Тише'],mute:['🔇','Без звука'],play:['⏯','Пауза'],next:['⏭','Дальше'],prev:['⏮','Назад'],ask:['📸','Ответ с экрана'],lock:['🔒','Блокировка'],standby:['🌙','Экран выкл'],wake:['☀️','Экран вкл'],shutdown:['⏻','Выключить'],restart:['🔄','Перезагрузка'],cancel_power:['✋','Отменить выключение'],launch:['🚀','Запустить программу'],link:['🌐','Открыть ссылку'],scenario:['🎬','Сценарий'],ai_profile:['🧠','Профиль нейросети'],say:['🗣','Сказать вслух'],command:['💬','Своя команда']};
const ARG={ai_profile:'Профиль: 0 Стандарт, 1 Быстро, 2 Умно, 3 Креативно, 4 Свой',launch:'Программа',link:'Адрес',scenario:'Сценарий',say:'Текст',command:'Как будто сказала Ане'};
const COLORS=['','#6b4fd8','#2f6fdb','#16937a','#b5762a','#7a2840','#a8418f'];
function toast(t){$('toast').textContent=t;$('toast').classList.add('on');clearTimeout(toastT);toastT=setTimeout(()=>$('toast').classList.remove('on'),2200)}
function haptic(){if(cfg.settings.haptic)try{tg.HapticFeedback.impactOccurred('light')}catch(e){}}
async function auth(){const r=await fetch('/auth',{method:'POST',body:tg.initData});if(!r.ok)throw 0;const d=await r.json();session=d.session;monitors=d.monitors||1;$('mon').hidden=monitors<2}
async function call(path,opt={}){opt.headers={'X-Session':session,...(opt.headers||{})};let r=await fetch(path,opt);
 if(r.status===401){try{await auth()}catch(e){fatal('Сеанс устарел. Закрой пульт и открой его заново.');throw e}opt.headers['X-Session']=session;r=await fetch(path,opt)}return r}
async function api(path,body){const r=await call(path,body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{});const d=await r.json().catch(()=>({}));if(!r.ok)throw new Error(d.error||'Ошибка');return d}
function fatal(t){document.body.replaceChildren(el('div',{style:'height:100%;display:flex;align-items:center;justify-content:center;padding:32px;text-align:center;color:#b9aed0;line-height:1.45'},t))}
async function save(part){try{cfg=await api('/api/config',part);applyCfg()}catch(e){toast(e.message)}}
function applyCfg(){document.documentElement.style.setProperty('--a',cfg.settings.accent||'#6b4fd8');$('hd').classList.toggle('on',!!cfg.settings.hd);drawHome();drawApps();drawSet()}
function go(p){if(page==='remote'&&p!=='remote')stopStream();page=p;document.querySelectorAll('.page').forEach(s=>s.classList.toggle('on',s.id==='p-'+p));document.querySelectorAll('#nav button').forEach(b=>b.classList.toggle('on',b.dataset.p===p));if(p==='remote'){startStream();setTimeout(fit,40)}if(p==='set')loadAi()}
document.querySelectorAll('#nav button').forEach(b=>b.onclick=()=>{haptic();go(b.dataset.p)});
function sheet(...kids){$('sbody').replaceChildren(...kids);$('sheet').classList.add('on')}
function closeSheet(){$('sheet').classList.remove('on')}
$('sheet').onclick=e=>{if(e.target===$('sheet'))closeSheet()};
function confirmSheet(title,yes,run){sheet(el('h3',{},title),el('div',{className:'acts'},el('button',{onclick:closeSheet},'Отмена'),el('button',{className:'danger',onclick:()=>{closeSheet();run()}},yes)))}
async function act(a,arg){haptic();try{const d=await api('/api/action',{a,arg:arg||''});if(d.text)toast(d.text);refresh()}catch(e){toast(e.message)}}
function run(t){if(t.a==='ask')return askScreen();if(t.a==='shutdown')return confirmSheet('Выключить компьютер?','Выключить',()=>act('shutdown'));if(t.a==='restart')return confirmSheet('Перезагрузить компьютер?','Перезагрузить',()=>act('restart'));act(t.a,t.arg)}
function tile(t,click){const b=el('button',{className:'tile',onclick:click},el('i',{},t.icon||'⭐'),el('span',{},t.label||''));if(t.color)b.style.background=t.color;return b}
function grid(id){const g=$(id);g.style.gridTemplateColumns=`repeat(${cfg.settings.cols||3},1fr)`;return g}
function drawHome(){const g=grid('tiles');g.classList.toggle('edit',editHome);$('eh').textContent=editHome?'Готово':'✎';$('eh').classList.toggle('main',editHome);$('status').style.display=cfg.settings.status===false?'none':'';
 g.replaceChildren(...cfg.home.map((t,i)=>tile(t,()=>editHome?editTile(i):run(t))));if(editHome)g.append(el('button',{className:'tile add',onclick:()=>editTile(-1)},el('i',{},'＋'),el('span',{},'Добавить')))}
$('eh').onclick=()=>{editHome=!editHome;drawHome()};$('el').onclick=()=>{editApps=!editApps;drawApps()};
)HTML"
R"HTML(async function cat(){if(!catalog)catalog=await api('/api/catalog').catch(()=>({programs:[],scenarios:[]}));return catalog}
function picker(list,value){const d=el('div',{}),i=el('input',{value:value||'',placeholder:'Начни вводить название'}),box=el('div',{style:'display:flex;flex-wrap:wrap;gap:6px;margin-top:8px;max-height:150px;overflow:auto'});
 const draw=()=>{const q=i.value.toLowerCase();box.replaceChildren(...list.filter(n=>n.toLowerCase().includes(q)).slice(0,30).map(n=>el('button',{style:'height:34px;font-size:13px',onclick:()=>{i.value=n;draw()}},n)))};i.oninput=draw;draw();d.append(i,box);d.val=()=>i.value;return d}
function colors(value){let cur=value||'';const d=el('div',{className:'dots'});const draw=()=>d.replaceChildren(...COLORS.map(c=>{const b=el('button',{className:'dot'+(c===cur?' on':''),onclick:()=>{cur=c;draw()}});b.style.background=c||'#1a1530';return b}));draw();d.val=()=>cur;return d}
async function editTile(i){const t=i<0?{a:'vol_up',label:'',icon:'',color:'',arg:''}:{...cfg.home[i]},c=await cat();
 const sel=el('select',{},...Object.keys(ACT).map(k=>el('option',{value:k,selected:k===t.a},ACT[k][1]))),label=el('input',{value:t.label,placeholder:'Как подписать'}),icon=el('input',{value:t.icon,placeholder:'Эмодзи'}),col=colors(t.color),argBox=el('div',{});let arg={val:()=>t.arg||''};
 const drawArg=()=>{const a=sel.value;argBox.replaceChildren();if(!ARG[a]){arg={val:()=>''};return}const w=a==='launch'?picker(c.programs,t.arg):a==='scenario'?picker(c.scenarios,t.arg):el('input',{value:t.arg||''});if(w.val)arg=w;else arg={val:()=>w.value};argBox.append(el('label',{},ARG[a]),w)};
 sel.onchange=()=>{const k=ACT[sel.value];icon.value=k[0];if(k[1])label.value=k[1];drawArg()};drawArg();
 const move=d=>{const h=[...cfg.home],j=i+d;if(j<0||j>=h.length)return;[h[i],h[j]]=[h[j],h[i]];closeSheet();save({home:h})};
 sheet(el('h3',{},i<0?'Новая кнопка':'Кнопка'),el('label',{},'Действие'),sel,argBox,el('label',{},'Подпись'),label,el('label',{},'Значок'),icon,el('label',{},'Цвет'),col,
  el('div',{className:'acts'},...(i<0?[]:[el('button',{onclick:()=>move(-1)},'←'),el('button',{onclick:()=>move(1)},'→'),el('button',{className:'danger',onclick:()=>{const h=[...cfg.home];h.splice(i,1);closeSheet();save({home:h})}},'Удалить')]),
   el('button',{className:'main',onclick:()=>{const a=sel.value,n={a,label:label.value||ACT[a][1],icon:icon.value||ACT[a][0],color:col.val(),arg:arg.val()},h=[...cfg.home];if(i<0)h.push(n);else h[i]=n;closeSheet();save({home:h})}},'Сохранить')))}
function drawApps(){const g=grid('apps');g.classList.toggle('edit',editApps);$('el').textContent=editApps?'Готово':'✎';$('el').classList.toggle('main',editApps);
 const list=cfg.apps.map((t,i)=>tile(t,()=>editApps?editApp(i):act(t.a,t.arg)));g.replaceChildren(...list);if(editApps||!cfg.apps.length)g.append(el('button',{className:'tile add',onclick:()=>editApp(-1)},el('i',{},'＋'),el('span',{},'Добавить')))}
async function editApp(i){const t=i<0?{a:'launch',label:'',icon:'🎮',color:'',arg:''}:{...cfg.apps[i]},c=await cat();
 const kind=el('select',{},el('option',{value:'launch',selected:t.a==='launch'},'Программа или игра'),el('option',{value:'link',selected:t.a==='link'},'Сайт или видео')),label=el('input',{value:t.label,placeholder:'Как подписать'}),icon=el('input',{value:t.icon,placeholder:'Эмодзи'}),col=colors(t.color),box=el('div',{});let target;
 const draw=()=>{if(kind.value==='launch'){target=picker(c.programs,t.a==='launch'?t.arg:'');box.replaceChildren(el('label',{},'Программа'),target)}else{const w=el('input',{value:t.a==='link'?t.arg:'',placeholder:'https://…'});target={val:()=>w.value};box.replaceChildren(el('label',{},'Адрес'),w)}};kind.onchange=draw;draw();
 const move=d=>{const h=[...cfg.apps],j=i+d;if(j<0||j>=h.length)return;[h[i],h[j]]=[h[j],h[i]];closeSheet();save({apps:h})};
 sheet(el('h3',{},i<0?'Добавить в запуск':'Запуск'),el('label',{},'Что это'),kind,box,el('label',{},'Подпись'),label,el('label',{},'Значок'),icon,el('label',{},'Цвет'),col,
  el('div',{className:'acts'},...(i<0?[]:[el('button',{onclick:()=>move(-1)},'←'),el('button',{onclick:()=>move(1)},'→'),el('button',{className:'danger',onclick:()=>{const h=[...cfg.apps];h.splice(i,1);closeSheet();save({apps:h})}},'Удалить')]),
   el('button',{className:'main',onclick:()=>{const v=target.val().trim();if(!v)return toast('Укажи, что запускать');const n={a:kind.value,label:label.value||v,icon:icon.value||'🚀',color:col.val(),arg:v},h=[...cfg.apps];if(i<0)h.push(n);else h[i]=n;closeSheet();save({apps:h})}},'Сохранить')))}
async function askScreen(){haptic();sheet(el('h3',{},'Смотрю на экран…'));let r;try{r=await call('/api/shot')}catch(e){return}if(!r.ok){const d=await r.json().catch(()=>({}));closeSheet();return toast(d.error||'Не получилось')}
 const u=URL.createObjectURL(await r.blob()),task=el('textarea',{rows:3,placeholder:'Что сделать с этим снимком?'}),out=el('div',{className:'answer'}),go2=el('button',{className:'main'},'Ответить');
 go2.onclick=async()=>{if(!task.value.trim())return toast('Напиши задачу');go2.disabled=true;out.textContent='Думаю… 🧠 Это может занять 1–3 минуты.';try{await api('/api/ask',{task:task.value});for(;;){await sleep(1500);const d=await api('/api/ask');if(d.state==='done'){out.textContent=d.answer;break}}}catch(e){out.textContent=e.message}go2.disabled=false};
 sheet(el('h3',{},'Ответ с экрана'),el('img',{src:u}),el('label',{},'Задача'),task,el('div',{className:'acts'},el('button',{onclick:closeSheet},'Закрыть'),go2),out)}
function meter(name,v,extra){return [el('div',{className:'row'},el('span',{},name),el('span',{},Math.round(v)+'%'+(extra||''))),el('div',{className:'meter'},el('b',{style:`width:${Math.max(0,Math.min(100,v))}%`}))]}
async function refresh(){if(page!=='home'||document.hidden||cfg.settings.status===false||!session)return;try{const s=await api('/api/status');const k=[];if(s.ready){k.push(...meter('Процессор',s.cpu,s.cpuTemp>0?` · ${Math.round(s.cpuTemp)}°`:''));if(s.gpu>=0)k.push(...meter('Видеокарта',s.gpu,s.gpuTemp>0?` · ${Math.round(s.gpuTemp)}°`:''));k.push(...meter('Память',s.ram))}k.push(...meter(s.muted?'Громкость (без звука)':'Громкость',s.volume));if(s.track)k.push(el('div',{id:'track'},(s.playing?'▶️ ':'⏸ ')+s.track));if(s.locked)k.push(el('div',{id:'track'},'🔒 Компьютер заблокирован'));$('status').replaceChildren(...k)}catch(e){}}
setInterval(refresh,3000);
function opt(title,sub,ctl){return el('div',{className:'opt'},el('div',{},title,sub?el('small',{},sub):''),ctl)}
function sw(key){const b=el('button',{className:'sw'+(cfg.settings[key]?' on':''),onclick:()=>save({settings:{[key]:!cfg.settings[key]}})});return b}
let aiState=null;async function loadAi(){try{aiState=await api('/api/ai');drawSet()}catch(e){}}
async function setAi(i){await act('ai_profile',String(i));loadAi()}
function drawSet(){const s=cfg.settings,acc=el('div',{className:'dots'},...COLORS.slice(1).map(c=>{const b=el('button',{className:'dot'+(c===s.accent?' on':''),onclick:()=>save({settings:{accent:c}})});b.style.background=c;return b}));
 const cols=el('div',{style:'display:flex;gap:6px'},...[2,3,4].map(n=>el('button',{className:n==s.cols?'on':'',onclick:()=>save({settings:{cols:n}})},String(n))));
 const sens=el('input',{type:'range',min:5,max:40,value:s.sens||18,style:'width:130px;padding:0',onchange:e=>save({settings:{sens:+e.target.value}})});
 $('opts').replaceChildren(el('div',{className:'card'},el('div',{style:'margin-bottom:10px'},'Цвет'),acc,el('div',{style:'height:10px'}),opt('Кнопок в ряду','',cols),opt('Состояние компьютера','Блок сверху на главной',sw('status')),opt('Отклик вибрацией','',sw('haptic'))),
  el('div',{className:'card'},opt('Высокое качество','Чётче, но меньше кадров',sw('hd')),opt('Джойстик','Управляет клавиатурой и мышью',sw('pad')),opt('Чувствительность стика','',sens)),
  el('div',{className:'card'},el('div',{style:'margin-bottom:10px'},'Профиль нейросети'),el('div',{style:'display:flex;gap:6px;flex-wrap:wrap'},...(aiState?aiState.names.map((n,i)=>el('button',{className:i===aiState.profile?'on':'',onclick:()=>setAi(i)},n)):[]))),
  el('div',{className:'card'},opt('Главная по умолчанию','Вернуть исходные кнопки',el('button',{onclick:()=>confirmSheet('Вернуть кнопки главной?','Вернуть',()=>save({reset:true}))},'Сбросить'))));
 if(alive)send({t:'hd',v:!!s.hd})}
)HTML"
R"HTML(const stage=$('stage'),img=$('screen'),remote=$('remote');
let alive=false,monitor=0,monitors=1,scale=1,tx=0,ty=0,fitW=0,fitH=0,url='',frames=0,busy=false,nextBlob=null,ws=null,rotated=false,wsUsable=true;
function notice(text,spin){$('msg').textContent=text;$('spin').style.display=spin?'':'none';$('notice').style.display=text?'flex':'none'}
function wsOpen(){return ws&&ws.readyState===1&&wsReady}let wsReady=false;
function send(e){if(wsOpen())ws.send(JSON.stringify(e));else if(session&&alive)call('/input',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(e)}).catch(()=>{})}
function apply(){const W=stage.clientWidth,H=stage.clientHeight,mx=Math.max(0,fitW*scale-W),my=Math.max(0,fitH*scale-H);tx=mx?Math.min(0,Math.max(-mx,tx)):(W-fitW*scale)/2;ty=my?Math.min(0,Math.max(-my,ty)):(H-fitH*scale)/2;img.style.transform=`translate(${tx}px,${ty}px) scale(${scale})`}
function fit(){if(rotated){remote.style.width=innerHeight+'px';remote.style.height=innerWidth+'px'}if(!img.naturalWidth)return;const k=Math.min(stage.clientWidth/img.naturalWidth,stage.clientHeight/img.naturalHeight);fitW=img.naturalWidth*k;fitH=img.naturalHeight*k;img.style.width=fitW+'px';img.style.height=fitH+'px';apply()}
addEventListener('resize',()=>setTimeout(fit,50));
function show(b){if(busy){nextBlob=b;return}busy=true;const u=URL.createObjectURL(b);img.dataset.next=u;img.src=u;frames++}
img.onload=()=>{const first=img.style.display!=='block';img.style.display='block';if(first)fit();notice('');if(url)URL.revokeObjectURL(url);url=img.dataset.next;busy=false;if(nextBlob){const b=nextBlob;nextBlob=null;show(b)}};img.onerror=()=>{busy=false};
function locked(){img.style.display='none';notice('🔒 Компьютер заблокирован. Войди в Windows за компьютером.')}
async function loop(){while(alive&&!wsOpen()){if(document.hidden){await sleep(500);continue}try{const r=await call('/frame');if(r.status===200){show(await r.blob());await sleep(25)}else if(r.status===204)await sleep(100);else if(r.status===423){locked();await sleep(2000)}else await sleep(800)}catch(e){notice('Связь пропала, переподключаюсь…',true);await sleep(1000)}}}
function connect(){if(!alive)return;wsReady=false;const s=new WebSocket('wss://'+location.host+'/stream');ws=s;s.binaryType='blob';let ok=false;
 s.onopen=()=>s.send(session);
 s.onmessage=e=>{if(typeof e.data==='string'){if(e.data==='ok'){ok=true;wsReady=true;send({t:'hd',v:!!cfg.settings.hd})}else if(e.data==='locked')locked();else if(e.data==='denied')auth().then(()=>{}).catch(()=>fatal('Сеанс устарел. Закрой пульт и открой его заново.'));return}if(document.hidden)return;s.send('a');show(e.data)};
 s.onclose=()=>{if(!alive||ws!==s)return;wsReady=false;if(!ok&&!wsUsable)return;if(!ok){wsUsable=false;loop();return}notice('Связь пропала, переподключаюсь…',true);setTimeout(connect,1000)}}
function startStream(){if(alive)return;alive=true;wsUsable=true;if(img.style.display!=='block')notice('Подключаюсь…',true);connect();padLoop()}
function stopStream(){alive=false;wsReady=false;if(ws){const s=ws;ws=null;s.close()}$('net').hidden=true;if(rotated)toggleRot()}
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&wsOpen())ws.send('r')});
setInterval(()=>{const n=$('net');n.hidden=!alive||document.hidden;n.textContent=frames+' к/с';frames=0},1000);
function toStage(e){return rotated?{x:e.clientY,y:innerWidth-e.clientX}:{x:e.clientX,y:e.clientY}}
function point(e){const r=img.getBoundingClientRect();let x,y;if(rotated){x=(e.clientY-r.top)/r.height;y=1-(e.clientX-r.left)/r.width}else{x=(e.clientX-r.left)/r.width;y=(e.clientY-r.top)/r.height}return{x:Math.min(1,Math.max(0,x)),y:Math.min(1,Math.max(0,y))}}
function toggleRot(){rotated=!rotated;remote.classList.toggle('rot',rotated);$('rot').classList.toggle('on',rotated);$('nav').style.display=rotated?'none':'';if(!rotated){remote.style.width=remote.style.height=''}scale=1;setTimeout(fit,40)}
$('rot').onclick=toggleRot;
if(tg&&tg.isVersionAtLeast&&tg.isVersionAtLeast('8.0')&&tg.requestFullscreen){$('full').hidden=false;$('full').onclick=()=>{tg.isFullscreen?tg.exitFullscreen():tg.requestFullscreen();setTimeout(fit,300)}}
const pts=new Map();let one=null,two=null,lastMove=0,mouseSeen=false;
function endOne(){if(one&&one.timer)clearTimeout(one.timer);one=null}
function sawMouse(){if(!mouseSeen){mouseSeen=true;if(stage.requestPointerLock)$('lockm').hidden=false}}
const pl=()=>document.pointerLockElement===stage;
$('lockm').onclick=()=>{stage.requestPointerLock();toast('Мышь захвачена для игр. Выход — Esc')};
stage.addEventListener('pointerdown',e=>{if(!alive)return;if(e.pointerType==='mouse'){sawMouse();send(pl()?{t:'btn',b:e.button,d:1}:{t:'down',b:e.button,...point(e)});return}
 stage.setPointerCapture(e.pointerId);pts.set(e.pointerId,e);
 if(pts.size===1){one={sx:e.clientX,sy:e.clientY,p:point(e),drag:false,timer:setTimeout(()=>{if(one&&!one.drag){send({t:'click',b:2,...one.p});haptic();one.done=true}},520)}}
 else if(pts.size===2){if(one&&one.drag)send({t:'up',...point(e)});endOne();const[a,b]=[...pts.values()].map(toStage);two={d:Math.hypot(a.x-b.x,a.y-b.y),x:(a.x+b.x)/2,y:(a.y+b.y)/2}}});
stage.addEventListener('pointermove',e=>{if(!alive)return;if(e.pointerType==='mouse'){sawMouse();if(pl())send({t:'rel',x:e.movementX,y:e.movementY});else if(Date.now()-lastMove>16){lastMove=Date.now();send({t:'move',...point(e)})}return}
 if(!pts.has(e.pointerId))return;pts.set(e.pointerId,e);
 if(pts.size===2&&two){const[a,b]=[...pts.values()].map(toStage),d=Math.hypot(a.x-b.x,a.y-b.y),x=(a.x+b.x)/2,y=(a.y+b.y)/2,old=scale,R=stage.getBoundingClientRect(),ox0=rotated?R.top:R.left,oy0=rotated?innerWidth-R.right:R.top;scale=Math.min(6,Math.max(1,scale*d/two.d));const k=scale/old,ox=two.x-ox0,oy=two.y-oy0;tx=ox-(ox-tx)*k+x-two.x;ty=oy-(oy-ty)*k+y-two.y;two={d,x,y};apply();return}
 if(one&&!one.done&&pts.size===1){if(!one.drag&&Math.hypot(e.clientX-one.sx,e.clientY-one.sy)>9){one.drag=true;clearTimeout(one.timer);send({t:'down',...one.p})}if(one.drag&&Date.now()-lastMove>30){lastMove=Date.now();send({t:'move',...point(e)})}}});
function lift(e){if(e.pointerType==='mouse'){if(e.type==='pointerup')send(pl()?{t:'btn',b:e.button,d:0}:{t:'up',b:e.button,...point(e)});return}if(!pts.has(e.pointerId))return;pts.delete(e.pointerId);if(pts.size<2)two=null;if(one){if(one.drag)send({t:'up',...point(e)});else if(!one.done&&e.type==='pointerup'){send({t:'click',...one.p});haptic()}endOne()}}
stage.addEventListener('pointerup',lift);stage.addEventListener('pointercancel',lift);stage.addEventListener('contextmenu',e=>e.preventDefault());
stage.addEventListener('wheel',e=>{e.preventDefault();send({t:'scroll',d:e.deltaY>0?-1:1})},{passive:false});
function hold(id,d){const b=$(id);let t=0;const go=()=>send({t:'scroll',d});b.onpointerdown=()=>{go();t=setInterval(go,180)};b.onpointerup=b.onpointerleave=b.onpointercancel=()=>clearInterval(t)}hold('up',2);hold('down',-2);
document.querySelectorAll('[data-k]').forEach(b=>b.onclick=()=>{send({t:'key',k:b.dataset.k});haptic()});
$('kb').onclick=()=>{const o=$('panel').classList.toggle('open');$('kb').classList.toggle('on',o);if(o)$('text').focus();setTimeout(fit,60)};
function typeIt(){const v=$('text').value;if(v){send({t:'text',s:v});$('text').value=''}}$('send').onclick=typeIt;
const tgtText=e=>/^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName);
addEventListener('keydown',e=>{if(!alive||tgtText(e))return;if(e.key==='Enter'&&e.target===$('text'))return;e.preventDefault();if(!e.repeat&&e.code)send({t:'vk',c:e.code,d:1})});
addEventListener('keyup',e=>{if(!alive||tgtText(e))return;e.preventDefault();if(e.code)send({t:'vk',c:e.code,d:0})});
$('text').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();typeIt()}});
$('hd').onclick=()=>save({settings:{hd:!cfg.settings.hd}});
$('mon').onclick=()=>{monitor=(monitor+1)%monitors;$('mon').textContent='Экран '+(monitor+1);send({t:'mon'});scale=1;setTimeout(fit,300)};
const PAD={0:'Space',1:'ControlLeft',2:'KeyR',3:'KeyF',4:'KeyQ',5:'KeyE',8:'Tab',9:'Escape',10:'ShiftLeft',11:'KeyV',12:'ArrowUp',13:'ArrowDown',14:'ArrowLeft',15:'ArrowRight'},padDown={};
function padSet(k,on,msg){if(!!padDown[k]===on)return;padDown[k]=on;send(msg(on?1:0))}
function padLoop(){if(!alive){for(const k in padDown)if(padDown[k]){padDown[k]=false;if(k.startsWith('btn'))send({t:'btn',b:+k.slice(3),d:0});else send({t:'vk',c:k,d:0})}return}requestAnimationFrame(padLoop);if(cfg.settings.pad===false||!navigator.getGamepads||document.hidden)return;const p=[...navigator.getGamepads()].find(g=>g&&g.connected);$('pad').hidden=!p;if(!p)return;
 for(const i in PAD)if(p.buttons[i]){const c=PAD[i];padSet(c,p.buttons[i].pressed,d=>({t:'vk',c,d}))}
 const lt=p.buttons[6],rt=p.buttons[7];if(lt)padSet('btn2',lt.value>.4,d=>({t:'btn',b:2,d}));if(rt)padSet('btn0',rt.value>.4,d=>({t:'btn',b:0,d}));
 const ax=p.axes,dz=.18,T=.45;[['KeyD',ax[0]>T],['KeyA',ax[0]<-T],['KeyS',ax[1]>T],['KeyW',ax[1]<-T]].forEach(([c,on])=>padSet(c,on,d=>({t:'vk',c,d})));
 const rx=Math.abs(ax[2])>dz?ax[2]:0,ry=Math.abs(ax[3])>dz?ax[3]:0;if(rx||ry){const s=cfg.settings.sens||18;send({t:'rel',x:Math.round(rx*Math.abs(rx)*s),y:Math.round(ry*Math.abs(ry)*s)})}}
(async()=>{if(tg){tg.ready();tg.expand();try{tg.disableVerticalSwipes();tg.setHeaderColor('#15112a');tg.setBackgroundColor('#0d0b14')}catch(e){}}
 if(!tg||!tg.initData)return fatal('Открой пульт через меню своего Telegram-бота.');
 try{await auth();cfg=await api('/api/config');applyCfg();go('home');refresh()}catch(e){fatal('⛔ Нет доступа или нет связи с компьютером.')}})();
</script></body></html>)HTML";

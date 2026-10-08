/* VitaRPS5 XMB mock: screens, input and deep links. Keyboard drives it like the Vita, clicks act as touch.
   Physical keys: arrows D-pad, Enter Cross, Esc Circle, T Triangle, S Square, Q/E L/R, Space Start, Z Select.
   Logical actions (ok, back) swap when Circle Button Confirm is on, on every screen. */
const S={
 screen:'home',cat:0,sel:[0,0,0,0],opts:false,os:0,flt:'',noConsoles:false,
 pg:{kind:'settings',g:0,focus:'r',row:{}},
 ct:{slot:0,view:'summary',page:0,sh:0,cur:0,pick:[],hold:false},
 pin:{d:Array(8).fill(null),cur:0,ci:4},
 conn:{ci:0,flow:[0,4,7],cur:0,play:false,all:false},
 rec:{attempt:2},
 pop:null,toast:null,kb:null,unst:false,
 psn:'auth',pconn:'wifi',qr:true,logout:false,exitStart:0,zoom:1
};
let stageTimer=null,toastTimer=null,logoutTimer=null;
const STAGES=[['Waking console','Sending wake signal'],['Authenticating with PSN','Validating account tokens'],['Fetching internet consoles','Loading remote-play capable devices'],['Creating PSN session','Creating cloud-assisted session'],['Preparing Remote Play','Negotiating session'],['Punching control channel','Establishing control tunnel'],['Punching data channel','Finalizing media tunnel'],['Starting stream','Launching video pipeline']];
const connTitle=(flow,st)=>flow.includes(1)?'Starting Internet Remote Play':st===0?'Waking Console':'Starting Remote Play';
const visCons=()=>S.noConsoles?[]:CONSOLES.filter(c=>(!S.flt||c.name.toLowerCase().includes(S.flt.toLowerCase()))&&(!SET('paired').v||c.reg));
const hhmm=()=>{const d=new Date();return String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0');};
const slide=(el,x,y,extra='')=>{el.style.transform=`translate3d(${x}px,${y}px,0) ${extra}`;};

/* ---------------- chrome: layers, top bar, hint row ---------------- */
const LAYERS=['home','page','ctrl','pin','conn','strm'];
function showOnly(id){
 LAYERS.forEach(n=>$('#ly-'+n).style.display=n===id?'block':'none');
 const pageish=id==='page'||id==='ctrl'||id==='pin'||id==='conn';
 $('#rib').style.opacity=id==='strm'?0:1;
 $('.vig').style.opacity=id==='home'?1:0;
 $('.wash').style.opacity=pageish?1:0;
 $('#top').style.display=id==='strm'?'none':'flex';
 $('#hintbar').style.display=id==='strm'?'none':'block';
}
function paintTop(){
 const ban=S.screen==='home'&&S.cat===0&&CONSOLES.some(c=>c.cool);
 $('#top').innerHTML=`<img src="assets/Vita_RPS5_Logo.png" alt="VitaRPS5"><span class="slot">${ban?bannerPill('Console entered sleep mode'):''}</span><span class="r">${ban?'':`<span style="display:flex;gap:8px;align-items:center">${ico('wifi',24)}</span><span style="display:flex;gap:8px;align-items:center">${ico('battery',24)}86%</span>`}<span class="clk">${hhmm()}</span></span>`;
}
const unstExtra=()=>S.unst&&SET('net').v?unstPill():'';
function paintHints(items){ $('#hintbar').innerHTML=hintRow(items,unstExtra()); fitHints(); }
/* hint row collapse rule: a right slot of 200 px is reserved when the alert pill shows; if the hints no longer fit, items flagged low are dropped from the right until they do */
function fitHints(){
 const hl=$('#hintbar .hl');if(!hl)return;
 const low=[...hl.children].filter(c=>c.dataset.low);
 while(hl.scrollWidth>hl.clientWidth&&low.length){low.pop().remove();}
}

/* ---------------- toast, keyboard stand-in, popups ---------------- */
function toast(text,tone='',icon=''){
 S.toast={text,tone,icon};paintToast();
 clearTimeout(toastTimer);toastTimer=setTimeout(()=>{S.toast=null;paintToast();},3000);
}
const paintToast=()=>{$('#toastLayer').innerHTML=S.toast?toastHTML(S.toast):'';};
function popup(p){S.pop=Object.assign({size:'s',sel:0},p);paintPop();}
function closePop(){S.pop=null;paintPop();}
function paintPop(){
 const p=S.pop;
 $('#popLayer').innerHTML=p?popupHTML(p):'';
 $('#popLayer').style.pointerEvents=p?'auto':'none';
 if(p)paintHints(popHints(p));
 else refreshHints();
}
function popHints(p){
 const cl=p.cancelLabel||'Cancel';
 if(p.buttons){
  if(p.buttons.length===1)return [['confirm',p.buttons[0].label,{key:'ok'}]];
  const f=p.buttons[p.sel].label;
  return f===cl?[['confirm',f,{key:'ok'}]]:[['confirm',f,{key:'ok'}],['cancel',cl,{key:'back'}]];
 }
 return [['confirm',p.okLabel||'Select',{key:'ok'}],['cancel',cl,{key:'back'}]];
}
function popKey(k){
 const p=S.pop,n=(p.rows||p.buttons).length;
 if(k==='ok')return popPress(p.sel);
 if(k==='back'){closePop();p.onCancel&&p.onCancel();return;}
 if(p.grid){if(k==='left'&&p.sel%3>0)p.sel--;else if(k==='right'&&p.sel%3<2&&p.sel<n-1)p.sel++;else if(k==='up'&&p.sel>=3)p.sel-=3;else if(k==='down'&&p.sel+3<n)p.sel+=3;}
 else if(p.buttons){if(k==='left'&&p.sel>0)p.sel--;else if(k==='right'&&p.sel<n-1)p.sel++;}
 else {if(k==='up'&&p.sel>0)p.sel--;else if(k==='down'&&p.sel<n-1)p.sel++;}
 paintPop();
}
function popPress(i){
 const p=S.pop,it=(p.rows||p.buttons)[i];if(it.dis)return;
 closePop();(p.onSel?p.onSel(i):it.fn&&it.fn());
}
function keyboard(o){S.kb=o;paintKb();}
function paintKb(){
 const k=S.kb;
 $('#kbLayer').innerHTML=k?`<div class="kbd" data-do="kbok"><div class="cap">System keyboard</div><h3>${k.title}</h3><div class="fld">${k.text||''}<span class="cursor" style="height:16px;margin-left:2px"></span></div><div class="keys">The Vita system keyboard opens here</div></div>`:'';
 if(k)paintHints([['confirm','Done',{key:'ok'}],['cancel','Cancel',{key:'back'}]]);else refreshHints();
}
function kbKey(k){const o=S.kb;if(k==='ok'){S.kb=null;paintKb();o.done&&o.done();}else if(k==='back'){S.kb=null;paintKb();}}

/* ---------------- HOME (C01 CategoryBar, C02 XmbList, C04 DetailPanel, C05 OptionsColumn) ---------------- */
const CATS=[['Consoles','assets/icon_play.png'],['Settings','assets/icon_settings.png'],['Controller','icons/controller.svg'],['Profile','icons/profile.svg']];
function items(ci){
 if(ci===0){const v=visCons();return v.map(c=>({k:'console',c,t:c.name}));}
 if(ci===1)return GROUPS.map((n,gi)=>({k:'grp',gi,t:n,s:SETTINGS.filter(s=>s.g===gi).length+(SETTINGS.filter(s=>s.g===gi).length>1?' settings':' setting'),img:'assets/icon_settings.png'}));
 if(ci===2)return PNAME.map((p,i)=>({k:'preset',i,t:p,s:PDESC[i],img:'icons/controller.svg'}));
 return [{k:'pf',gi:0,t:'Account',s:'PSN Account ID',img:'icons/profile.svg'},{k:'pf',gi:1,t:'Connection',s:'Network, console and stream',img:'icons/profile.svg'},{k:'pf',gi:2,t:'PlayStation Network',s:PSN_STATES[psnState()],img:'icons/profile.svg'}];
}
let cache=[];
const psnState=()=>SET('psnmode').v?S.psn:'disabled';
const selItem=()=>cache[S.sel[S.cat]];
/* status hints wrap to at most 2 lines in the 288 px text column; every copy-deck hint fits (checked by measuring) */
const mctx=document.createElement('canvas').getContext('2d');
function hintLines(t){mctx.font='400 16px Roboto, sans-serif';return Math.min(3,Math.ceil(mctx.measureText(t).width/288));}
const hintH=t=>24*hintLines(t);
function buildCats(){$('#cats').innerHTML=CATS.map((c,i)=>`<div class="cat" data-do="cat" data-arg="${i}" role="tab" aria-label="${c[0]}"><div class="ci"><img src="${c[1]}" alt=""></div><div class="cl">${c[0]}</div></div>`).join('');}
function itemIcon(it){return it.k==='console'?ring(it.c,56):`<img class="nav" src="${it.img}" alt="">`;}
function buildList(anim){
 cache=items(S.cat);
 S.sel[S.cat]=Math.max(0,Math.min(S.sel[S.cat],cache.length-1));
  if(S.cat===0&&!cache.length){
  $('#list').innerHTML='';
  $('#empty').innerHTML=(CONSOLES.length&&!S.noConsoles)?'No consoles match filter':`<span class="sp"></span>Searching for consoles...`;
 } else $('#empty').innerHTML='';
 $('#list').innerHTML=cache.map((it,i)=>{
  let sub=it.s||'',hint='';
  if(it.k==='console'){const k=kindOf(it.c),K=KIND[k];sub=`${sdot(k)}${K.t}${k==='psn'?' <span class="net">&middot; Internet</span>':''}`;
   hint=it.c.hint?HINT[it.c.hint]:'';}
  return `<div class="xi ${it.c&&kindOf(it.c)==='cool'?'dimmed':''}" data-do="item" data-arg="${i}"><div class="itc" style="--i:${anim?Math.min(i,6):0}"><div class="ic">${itemIcon(it)}</div><div class="tx"><div class="n">${it.t}</div><div class="s">${sub}</div>${hint?`<div class="h">${hint}</div>`:''}</div></div></div>`;
 }).join('');
 layout();
}
function layout(){
 $$('.cat').forEach((el,i)=>{
  const d=i-S.cat,x=d===0?256:d>0?256+128+(d-1)*112:256-112*(-d);
  slide(el,x,72,d<0?'scale(.75)':'');el.classList.toggle('on',d===0);
 });
 const sel=S.sel[S.cat],rows=$$('#list .xi');
 const hOf=(i,f)=>(f?88:56)+(cache[i]&&cache[i].c&&cache[i].c.hint?hintH(HINT[cache[i].c.hint]):0);
 let y=192;
 rows.forEach((el,i)=>{
  let top;
  if(i<sel)top=192-80*(sel-i);
  else if(i===sel)top=192;
  else{top=y;}
  if(i===sel)y=192+hOf(i,true)+24;else if(i>sel){y=top+hOf(i,false)+24;}
  slide(el,0,top);el.style.opacity=i<sel?0:'';el.style.pointerEvents=i<sel?'none':'auto';
  el.classList.toggle('sel',i===sel);
 });
 const showFlt=S.cat===0&&(CONSOLES.length>4||S.flt);
 $('#flt').innerHTML=showFlt?(S.flt?`<span data-do="kbflt">Filter: &ldquo;${S.flt}&rdquo; (${visCons().length} found)</span><span class="x" data-do="fltclr">${ico('close',16)}</span>`:`<span data-do="kbflt" style="display:flex;align-items:center;gap:8px">${gl('start')}Filter</span>`):'';
 paintDetail();refreshHints();
}
function kv(a,b,col=''){return `<div class="kv"><span>${a}</span><b ${col?`style="color:${col}"`:''}>${b}</b></div>`;}
function paintDetail(){
 const it=selItem(),P=$('#detail');let h='';
 if(!it){P.innerHTML='';return;}
 if(it.k==='console'){const c=it.c,k=kindOf(c),K=KIND[k];
  h=`${typeLogo(c)}<h4>${c.name}</h4><p class="st">${K.t?sdot(k)+K.t:sdot(k)+'Not reachable'}</p>${kv('Address',c.ip||'Unknown')}${kv('Route',routeOf(c))}${kv('Pairing',c.reg?'Paired':'Unpaired')}`;
 } else if(it.k==='grp'){const rs=SETTINGS.filter(s=>s.g===it.gi);h=`<h4>${it.t}</h4><p class="st" style="height:16px"></p>${rs.map(s=>kv(s.label.replace(' (artifacts) (Experimental)',' (Experimental)'),setVal(s))).join('')}`;}
 else if(it.k==='preset'){const m=MAPS[it.i];h=`<h4>${it.t}</h4><p class="st">${PDESC[it.i]}</p>${kv('L1',m.L1)}${kv('R1',m.R1)}${kv('Front touch',nz(m.front)+' zones')}${kv('Rear touch',nz(m.back)+' zones')}`;}
 else {const rs=pageRows('profile',it.gi);h=`<h4>${it.t}</h4><p class="st" style="height:16px"></p>${rs.slice(0,4).map(r=>kv(r.label,r.plain||'')).join('')}`;}
 P.innerHTML=h;P.style.animation='none';void P.offsetWidth;P.style.animation='';
}
function consoleVerb(c){const k=kindOf(c);return k==='unpaired'?'Pair':k==='standby'?'Wake':k==='cool'?'Please wait':'Connect';}
function optsList(){
 const it=selItem();if(!it||it.k!=='console')return [];const c=it.c,k=kindOf(c);
 if(k==='unpaired')return [{l:'Pair',f:()=>startPin(c)},{l:'Change icon',f:()=>iconPicker(c)}];
 const L=[{l:k==='standby'?'Wake and connect':'Connect',f:()=>connect(c),dis:k==='cool'}];
 if(bothRoutes(c))L.push({l:'Connect via',f:()=>viaPop(c),dis:k==='cool'});
 L.push({l:'Re-pair',f:()=>repairPop(c)},{l:'Change icon',f:()=>iconPicker(c)});
 return L;
}
function paintOpts(){
 const it=selItem(),L=optsList();
 $('#opts').innerHTML=it&&it.k==='console'?`<h4>${it.c.name}</h4><div class="sub">Options</div>`+L.map((o,i)=>`<div class="oi ${i===S.os&&S.opts?'sel':''} ${o.dis?'dis':''}" data-do="oi" data-arg="${i}">${o.l}</div>`).join(''):'';
 $('#opts').classList.toggle('open',S.opts);$('#ly-home').classList.toggle('oo',S.opts);$('#screen').classList.toggle('opts-open',S.opts);
 refreshHints();
}
function homeHints(){
 if(S.opts)return [['confirm','Select',{key:'ok'}],['cancel','Back',{key:'back'}]];
 const it=selItem(),L=[];
 if(S.cat===0){
  if(it&&it.k==='console')L.push(['confirm',consoleVerb(it.c),{key:'ok',dim:kindOf(it.c)==='cool'}],['tri','Options',{key:'tri'}]);
  if(CONSOLES.length>4||S.flt)L.push(['start',S.flt?'Clear filter':'Filter',{key:'start'}]);
 } else L.push(['confirm','Open',{key:'ok'}]);
 L.push(['LR','Category',{low:1}]);
 return L;
}
function openHome(anim=true){S.screen='home';showOnly('home');S.opts=false;paintTop();buildList(anim);paintOpts();}
function iconPicker(c){
 popup({size:'m',grid:true,icon:'',title:'Change icon',sub:c.name,sel:Math.max(0,ROOMS.findIndex(r=>r[0]===c.room)),okLabel:'Choose',
  rows:ROOMS.map(r=>({img:'icons/'+r[0]+'.svg',label:r[1],cur:r[0]===c.room})),onSel:i=>{c.room=ROOMS[i][0];buildList(false);}});
}
function viaPop(c){
 popup({size:'s',title:'Connect via',sub:c.name,okLabel:'Connect',rows:[{label:'Local Network',sub:c.ip},{label:'Internet',sub:'PSN'}],onSel:i=>connect(c,i===0?'lan':'net')});
}
function repairPop(c){
 popup({size:'s',icon:'lock',title:'Re-pair '+c.name+'?',body:'You will need to enter a new 8-digit PIN from the console.',sel:0,okLabel:'Select',buttons:[{label:'Cancel'},{label:'Re-pair'}],onSel:i=>{if(i===1)startPin(c);}});
}
function resultPop(r){
 popup(Object.assign({size:'s',sel:0,okLabel:'OK',cancelLabel:'Close'},r));
}
const pairOk=c=>resultPop({tone:'ok',icon:'check',title:'Console paired',body:`${c.name} is paired. You can connect to it now.`,buttons:[{label:'OK'}],onSel:()=>{c.reg=true;CONSOLES.sort((a,b)=>b.reg-a.reg);S.sel[0]=CONSOLES.indexOf(c);openHome(false);}});
const PAIR_FAIL={pin:'did not accept the PIN. Check the code on the console and try again.',unreachable:'could not be reached. Check that it is on and on the same network.',timeout:'did not answer in time. Open Link Device on the console again and retry.'};
const pairFail=(c,why='pin')=>resultPop({tone:'err',icon:'warn',title:'Pairing failed',body:`${c.name} ${PAIR_FAIL[why]}`,sel:1,buttons:[{label:'Close'},{label:'Try again'}],onSel:i=>{if(i===1)startPin(c);else openHome(false);}});
const connFail=(c,why)=>resultPop({tone:'err',icon:'warn',title:'Could not connect',body:`${c.name}: ${why}`,sel:1,buttons:[{label:'Close'},{label:'Try again'}],onSel:i=>{if(i===1)connect(c);}});

/* ---------------- PAGES: Settings and Profile (C07 PageShell, C08 SettingRow) ---------------- */
const PG={settings:{title:'Settings',icon:'assets/icon_settings.png',groups:GROUPS},profile:{title:'Profile',icon:'icons/profile.svg',groups:['Account','Connection','PlayStation Network']}};
const ACCT='dGhlX3BsYXllcl9vbmU=';
/* Profile > Connection by situation. Network Type, Console, Console IP (when an address is known) and Quality follow today's code; Status uses the Home list wording */
const PCONN={wifi:{net:'Local Wi-Fi',con:'Living Room',ip:'192.168.1.20',st:'Ready'},standby:{net:'Local Wi-Fi',con:'Den',ip:'192.168.1.31',st:'Standby'},psn:{net:'PSN Internet',con:'Office',ip:null,st:'Ready'},manual:{net:'Manual Host',con:'Manual host',ip:'192.168.1.77',st:'Ready'},unavail:{net:'Unavailable',con:'Dorm',ip:null,st:'Unavailable'},unreg:{net:'Local Wi-Fi',con:'Bedroom',ip:'192.168.1.44',st:'Unpaired'},none:{net:'Unavailable',con:'Not selected',ip:null,st:'None'}};
function pageRows(kind,g){
 if(kind==='settings')return SETTINGS.filter(s=>s.g===g).map(s=>({label:s.label,type:s.type,s,desc:s.desc,plain:setVal(s)}));
 if(g===0)return [{label:'Account ID',type:'info',val:`<span class="mono">${ACCT}</span>`,plain:ACCT.slice(0,10)+'...'},{label:'Refresh Account ID',type:'action',desc:'Read the Account ID again from the system profile.',act:'acct',plain:''}];
 if(g===1){
  const c=PCONN[S.pconn],r=[{label:'Network Type',type:'info',val:c.net,plain:c.net},{label:'Console',type:'info',val:c.con,plain:c.con}];
  if(c.ip)r.push({label:'Console IP',type:'info',val:`<span class="mono">${c.ip}</span>`,plain:c.ip});
  r.push({label:'Status',type:'info',val:c.st,plain:c.st});
  r.push({label:'Quality',type:'info',val:SET('quality').opts[SET('quality').v],plain:SET('quality').opts[SET('quality').v]});
  return r;
 }
 const ps=psnState(),st={label:'PSN Auth',type:'info',val:PSN_STATES[ps],plain:PSN_STATES[ps],tone:ps==='auth'?'':ps==='disabled'?'':'' ,cls:ps==='auth'?'on':ps==='none'||ps==='expired'||ps==='error'?'bad':ps==='refresh'?'mid':''};
 st.val=`<span class="${st.cls||''}" style="display:inline-flex;align-items:center;gap:8px">${st.cls==='bad'?ico('warn',20):''}${st.val}</span>`;st.tone=st.cls==='bad'?'err':'';
 if(ps==='disabled'){st.desc='Enable PSN internet mode in Settings';}
 if(ps==='disabled')return [st,{label:'Log in',type:'action',dis:true,desc:'Enable PSN internet mode in Settings',act:'login'}];
 if(ps==='auth')return [st,{label:'Refresh hosts',type:'action',desc:'Reload your internet-capable consoles.',act:'hosts'},{label:S.logout?`Press ${inl('confirm')} again to confirm log out`:'Log out',type:'action',tone:S.logout?'warn':'',desc:'Remove the saved PSN login from this Vita.',act:'logout'}];
 if(ps==='refresh')return [st];
 if(ps==='await')return [{label:'PSN Auth',type:'info',val:PSN_STATES.await}];
 return [st,{label:'Log in',type:'action',desc:'Sign in with your phone. Needed for internet Remote Play.',act:'login'}];
}
const curRow=()=>{const P=S.pg,rs=pageRows(P.kind,P.g),k=P.kind+P.g;return {rs,i:Math.min(P.row[k]||0,rs.length-1),k};};
function rowVal(r,i){
 if(r.type==='toggle')return toggle(r.s.v);
 if(r.type==='choice')return choice(r.s.opts[r.s.v],i);
 if(r.type==='info')return r.val||'';
 return r.dis?'':ico('next',20);
}
function paintPage(full){
 const P=S.pg,cfg=PG[P.kind],{rs,i:s,k}=curRow();P.row[k]=s;
 const login=P.kind==='profile'&&P.g===2&&psnState()==='await';
 $('#ly-page').innerHTML=`<div class="ptitle"><img src="${cfg.icon}" alt=""><h1>${cfg.title}</h1></div><div class="prule"></div>${P.kind==='profile'?`<div class="ident"><span class="av"><img src="icons/profile.svg" alt=""></span><span class="it"><b>${ACCT}</b><i>PlayStation Network</i></span></div>`:''}
 <div id="groups">${cfg.groups.map((n,i)=>`<div class="grp ${i===P.g?'on':''} ${i===P.g&&P.focus==='g'?'act':''}" data-do="pgg" data-arg="${i}">${n}</div>`).join('')}</div>
 ${login?loginHTML():`<div id="pane"><div id="rows" ${full?'':'style="animation:none"'}>${rs.map((r,i)=>srow({i,label:r.label,val:rowVal(r,i),sel:i===s,act:i===s&&P.focus==='r',dis:r.dis,tone:r.tone})).join('')}</div></div>
 <div id="pdesc">${rs[s].desc||''}</div>`}
 ${rs.length>6&&!login?`<div class="scr"><i style="top:${Math.max(0,s-5)/rs.length*100}%;height:${6/rs.length*100}%"></i></div>`:''}`;
 if(!login)$('#rows').style.transform=`translateY(${-Math.max(0,s-5)*48}px)`;
 refreshHints();
}
function pageHints(){
 const P=S.pg,{rs,i}=curRow(),r=rs[i];
 if(P.kind==='profile'&&P.g===2&&psnState()==='await')return [['confirm','Enter code'],['start','QR'],['select','Browser'],['sq','Cancel login']];
 if(P.focus==='g')return [['confirm','Open',{key:'ok'}],['cancel','Back',{key:'back'}]];
 const L=[];
 if(r.type==='toggle')L.push(['confirm','Toggle',{key:'ok'}]);
 else if(r.type==='choice')L.push(['confirm','Next',{key:'ok'}],['dpadh','Change']);
 else if(r.type==='action')L.push(['confirm',r.act==='login'?'Log in':r.act==='logout'?(S.logout?'Confirm log out':'Log out'):'Refresh',{key:'ok',dim:r.dis}]);
 L.push(['LR','Group'],['cancel','Back',{key:'back'}]);
 return L;
}
function loginHTML(){
 const SHORT_URL='my.account.sony.com/sso/ca/authorize'; /* display only; the full authorize URL stays in memory for the QR and the browser */
 const qr=S.qr?`<div class="qr" data-do="qrtog"><svg viewBox="0 0 25 25" width="160" height="160" shape-rendering="crispEdges">${qrSvg()}</svg></div>`:`<div class="qr hid" data-do="qrtog">QR hidden</div>`;
 return `<div class="lgn"><h3>Phone Login Assist</h3><div class="cols">${qr}<div class="st"><div>1&nbsp; Press ${inl('start')} to show or hide the QR code</div><div>2&nbsp; Scan the QR code with your phone and sign in</div><div>3&nbsp; Press ${inl('confirm')} and paste the redirect URL or code</div><div>4&nbsp; ${inl('select')} opens the Vita browser instead</div></div></div>
 <div class="st" style="margin-top:16px;display:grid;grid-template-columns:auto 1fr;gap:0 16px"><span style="color:var(--text-3)">Code</span><b>Paste redirect URL/code</b><span style="color:var(--text-3)">URL</span><b style="color:var(--text-2)">${SHORT_URL}</b></div></div>`;
}
function qrSvg(){let s='';const f=(x,y)=>`<rect x="${x}" y="${y}" width="7" height="7" fill="#0a0a0a"/><rect x="${x+1}" y="${y+1}" width="5" height="5" fill="#fafafa"/><rect x="${x+2}" y="${y+2}" width="3" height="3" fill="#0a0a0a"/>`;s+=f(0,0)+f(18,0)+f(0,18);let r=7;for(let y=0;y<25;y++)for(let x=0;x<25;x++){if((x<8&&y<8)||(x>16&&y<8)||(x<8&&y>16))continue;r=(r*9301+49297)%233280;if(r%3===0)s+=`<rect x="${x}" y="${y}" width="1" height="1" fill="#0a0a0a"/>`;}return s;}
function openPage(kind,g,focus='r'){
 S.pg={kind,g:g||0,focus,row:S.pg.kind===kind?S.pg.row:{}};S.screen='page';showOnly('page');paintTop();paintPage(true);
}
function pageAct(r){
 if(r.type==='toggle'||r.type==='choice')return pageChange(1);
 if(r.type!=='action'||r.dis){if(r.dis)toast('PSN internet mode is disabled in Settings','err','warn');return;}
 if(r.act==='acct')return toast('Account ID refreshed from system profile','ok','check');
 if(r.act==='hosts')return toast('PSN internet host list refreshed','ok','check');
 if(r.act==='login'){S.psn='await';S.qr=true;paintPage(true);return toast(`Scan QR on phone, then press ${inl('confirm')} to paste the full redirect URL`);}
 if(r.act==='logout'){
  if(!S.logout){S.logout=true;clearTimeout(logoutTimer);logoutTimer=setTimeout(()=>{S.logout=false;if(S.screen==='page')paintPage(false);},3000);paintPage(false);}
  else{S.logout=false;clearTimeout(logoutTimer);S.psn='none';paintPage(true);toast('PSN login removed','ok','check');}
 }
}
function pageChange(d){
 const {rs,i}=curRow(),r=rs[i];if(r.type==='choice'){const s=r.s;s.v=(s.v+d+s.opts.length)%s.opts.length;}else if(r.type==='toggle'){r.s.v=!r.s.v;if(r.s.id==='cc')refreshHints();}else return;
 paintPage(false);
}

/* ---------------- CONTROLLER (C20 diagram + callout, C21 zone grid) ---------------- */
const FRONT={x:164,y:176,w:630},BACK={x:170,y:176,w:620};
const ZF={x:120,y:144,w:720},ZB={x:140,y:152,w:680};
const frontRect=b=>{const s=b.w/874;return{x:b.x+178*s,y:b.y+30*s,w:526*s,h:298*s};};
const backRect=b=>{const s=b.w/720;return{x:b.x+139*s,y:b.y+45*s,w:444*s,h:188*s};};
function zones(r,arr,mode){
 let h='';const cw=r.w/6,ch=r.h/3;
 for(let i=0;i<18;i++){const rr=Math.floor(i/6),cc=i%6,o=arr[i];
  h+=`<div class="cell ${o!=='None'?'m':''} ${mode&&S.ct.pick.includes(i)?'pick':''} ${mode&&S.ct.cur===i?'cur':''}" data-do="cell" data-arg="${i}" style="left:${r.x+cc*cw}px;top:${r.y+rr*ch}px;width:${cw}px;height:${ch}px">${sh(o)}</div>`;}
 return h;
}
function paintCtrl(){
 const C=S.ct,m=MAPS[C.slot],v=C.view;
 const title=v==='summary'?'Controller':v==='front'?'Front Touch':'Rear Touch';
 let h=`<div class="ptitle"><img src="icons/controller.svg" alt=""><h1>${title}</h1>${v!=='summary'?`<span class="sub">${PNAME[C.slot]}</span>`:''}</div><div class="prule"></div>`;
 if(v==='summary')h+=`<div class="pright"><span class="chev" data-do="preset" data-arg="-1">${ico('back',16)}</span><span style="min-width:96px;text-align:center">${PNAME[C.slot]}</span><span class="chev" data-do="preset" data-arg="1">${ico('next',16)}</span></div>`;
 if(v==='summary'&&C.page===0){
  const H=FRONT.w*396/874;
  h+=`<div class="dg" data-do="dg" style="left:${FRONT.x}px;top:${FRONT.y}px;width:${FRONT.w}px;height:${H}px"><img src="assets/controller_front.png" alt="Vita front"></div>`;
  [['L1',m.L1,0],['R1',m.R1,1]].forEach(([n,o,i])=>{
   const txt=`${n} &rarr; ${o}`,w=i?136:136,x=i?896-w:64,py=FRONT.y+.1*H,px=FRONT.x+(i?.9:.1)*FRONT.w;
   h+=`<div class="callout ${C.sh===i?'on':''}" data-do="callout" data-arg="${i}" style="left:${x}px;top:136px;width:${w}px;justify-content:${i?'flex-end':'flex-start'}">${txt}</div>`;
   const x1=i?x+w*.5:x+w*.5,y1=168,dx=px-x1,dy=py-y1,len=Math.hypot(dx,dy),ang=Math.atan2(dy,dx);
   h+=`<div class="cl-line" style="left:${x1}px;top:${y1}px;width:${len}px;transform:rotate(${ang}rad)"></div><div class="cl-dot" style="left:${px-3}px;top:${py-3}px"></div>`;
  });
  h+=`<div class="cfoot" style="left:48px">${PDESC[C.slot]}</div><div class="cfoot" style="right:48px">Page 1/2 &middot; Buttons</div>`;
 } else if(v==='summary'){
  const H=BACK.w*327/720;
  h+=`<div class="dg back" data-do="dg" style="left:${BACK.x}px;top:${BACK.y}px;width:${BACK.w}px;height:${H}px"><img src="assets/controller_back_clean.png" alt="Vita back"></div>`+zones(backRect(BACK),m.back,false);
  h+=`<div class="cfoot" style="left:48px">${PDESC[C.slot]}</div><div class="cfoot" style="right:48px">Page 2/2 &middot; Back Touch &middot; <b>${nz(m.back)}</b> zones</div>`;
 } else if(v==='front'){
  const H=ZF.w*396/874;
  h+=`<div class="dg" style="left:${ZF.x}px;top:${ZF.y}px;width:${ZF.w}px;height:${H}px"><img src="assets/controller_front.png" alt=""></div>`+zones(frontRect(ZF),m.front,true);
  h+=`<div class="cfoot" style="left:48px">${C.pick.length>1?C.pick.length+' Zones Selected':'Zone '+zoneName(C.cur)}</div>`;
 } else {
  const H=ZB.w*327/720;
  h+=`<div class="dg back" style="left:${ZB.x}px;top:${ZB.y}px;width:${ZB.w}px;height:${H}px"><img src="assets/controller_back_clean.png" alt=""></div>`+zones(backRect(ZB),m.back,true);
  h+=`<div class="cfoot" style="left:48px">${C.pick.length>1?C.pick.length+' Zones Selected':'Zone '+zoneName(C.cur)}</div>`;
 }
 $('#ly-ctrl').innerHTML=h;refreshHints();
}
function ctrlHints(){
 const C=S.ct;
 if(C.view==='summary'){
  const L=[['dpadh','Preset',{low:1}],['LR','Page']];
  if(C.page===0)L.push(['dpadv','L1 / R1'],['confirm','Shoulder']);
  L.push(['tri','Zones'],['sq','Clear',{low:1}],['cancel','Back']);return L;
 }
 return [['dpad','Move'],['confirm','Assign'],['tri','Whole surface'],['sq','Clear'],['cancel','Back']];
}
function openCtrl(view='summary',page=0){S.ct.view=view;S.ct.page=page;S.ct.cur=0;S.ct.pick=[];S.screen='ctrl';showOnly('ctrl');paintTop();paintCtrl();}
function mapPop(title,sub,cur,apply){
 popup({size:'l',title,sub,sel:Math.max(0,OUTS.indexOf(cur)),okLabel:'Assign',cancelLabel:'Cancel',rows:OUTS.map(o=>({label:o,cur:o===cur})),onSel:i=>{apply(OUTS[i]);S.ct.pick=[];paintCtrl();}});
}
function zonePop(idxs){
 const C=S.ct,arr=C.view==='front'?MAPS[C.slot].front:MAPS[C.slot].back,side=C.view==='front'?'Front':'Rear';
 const sub=idxs.length>1?idxs.length+' Zones Selected':side+' '+zoneName(idxs[0]);
 const same=idxs.every(i=>arr[i]===arr[idxs[0]]);
 mapPop(side+' Touch Mapping',sub+(idxs.length>1?` &middot; ${same?arr[idxs[0]]:'Mixed'}`:''),same?arr[idxs[0]]:null,o=>idxs.forEach(i=>arr[i]=o));
}
function shoulderPop(which){
 const m=MAPS[S.ct.slot];mapPop('Shoulder Mapping',which==='L1'?'Left Shoulder (L1)':'Right Shoulder (R1)',m[which],o=>{m[which]=o;});
}
function fullPop(){
 const C=S.ct,key=C.view==='front'?'front':'back',m=MAPS[C.slot];
 const arr=m[key],same=arr.every(o=>o===arr[0]);
 mapPop(C.view==='front'?'Front Touch Mapping':'Rear Touch Mapping',(C.view==='front'?'Full Front Touch':'Full Rear Touch')+` &middot; ${same?arr[0]:'Mixed'}`,same?arr[0]:null,o=>{m[key]=Array(18).fill(o);});
}

/* ---------------- PIN (C17 PinField) ---------------- */
function startPin(c){S.pin={d:Array(8).fill(null),cur:0,ci:CONSOLES.indexOf(c),zone:'d',b:2};S.screen='pin';showOnly('pin');paintTop();paintPin();}
/* one focus at a time: zone d = digit boxes, zone b = the three buttons. Right on the last digit (when all 8 are filled) or Down moves to the buttons (Register when ready); Up returns to the last digit. */
function paintPin(){
 const P=S.pin,c=CONSOLES[P.ci],full=P.d.every(x=>x!==null),zb=P.zone==='b';
 $('#ly-pin').innerHTML=`<div class="ptitle">${ico('lock',32)}<h1>${c.model} Console Registration</h1><span class="sub">${c.name}${c.ip?` (${c.ip})`:''}</span></div><div class="prule"></div>
 <div class="pmsg" style="top:160px">Enter the 8-digit session PIN displayed on your ${c.model}:</div>
 <div class="pinwrap">${P.d.map((d,i)=>`<div class="pind ${i===P.cur&&!zb?'sel':''}" data-do="pdig" data-arg="${i}"><span class="cv" data-do="pup" data-arg="${i}">${ico('up',20)}</span><span class="bx ${d===null&&(i!==P.cur||zb)?'empty0':''}">${d!==null?d:(i===P.cur&&!zb?'<span class="cursor"></span>':'')}</span><span class="cv" data-do="pdn" data-arg="${i}">${ico('down',20)}</span></div>`).join('')}</div>
 <div class="pbtns">${tbtn('Clear digit',{do:'pclr',sel:zb&&P.b===0})}${tbtn('Cancel',{do:'pcancel',sel:zb&&P.b===1})}${tbtn('Register',{do:'pok',dis:!full,sel:zb&&P.b===2})}</div>`;
 refreshHints();
}
function pinHints(){
 const P=S.pin,full=P.d.every(x=>x!==null);
 if(P.zone==='b'){const f=['Clear digit','Cancel','Register'][P.b];return [['dpadh','Button'],['dpadv','Digits'],['confirm',f,{key:'ok',dim:P.b===2&&!full}],['cancel','Cancel',{key:'back'}]];}
 return [['dpadh','Digit'],['dpadv','Change'],['sq','Clear digit',{low:1}],['confirm','Register',{key:'ok',dim:!full}],['cancel','Cancel',{key:'back'}]];
}
function pinRegister(){
 const P=S.pin,c=CONSOLES[P.ci];
 /* mock trigger only: first digit 0 = PIN rejected, 9 = timeout, anything else = paired. The build needs real results, see SPEC 3.3 */
 const d=P.d[0];if(d===0)pairFail(c,'pin');else if(d===9)pairFail(c,'timeout');else pairOk(c);
}
function pinKey(k){
 const P=S.pin,full=P.d.every(x=>x!==null);
 if(P.zone==='b'){
  if(k==='left'&&P.b>0)P.b--;else if(k==='right'&&P.b<2)P.b++;
  else if(k==='up'){P.zone='d';P.cur=7;}
  else if(k==='back'){openHome(false);return;}
  else if(k==='ok'){if(P.b===0){P.zone='d';P.d[P.cur]=null;}else if(P.b===1){openHome(false);return;}else if(full){pinRegister();return;}}
  return paintPin();
 }
 if(k==='left'&&P.cur>0)P.cur--;
 else if(k==='right'){if(P.cur<7)P.cur++;else if(full){P.zone='b';P.b=2;}}
 else if(k==='up'||k==='down'){const d=P.d[P.cur];P.d[P.cur]=d===null?(k==='up'?0:9):(d+(k==='up'?1:9))%10;}
 else if(k==='sq')P.d[P.cur]=null;
 else if(k==='back'){openHome(false);return;}
 else if(k==='ok'){if(full){pinRegister();return;}}
 else if(k==='start'){}
 paintPin();
}

/* ---------------- CONNECTING and RECONNECTING (C16 ProgressSteps) ---------------- */
function connect(c,route){
 const k=kindOf(c);
 if(k==='unpaired')return startPin(c);
 if(k==='cool')return;
 if(k==='unavail')return connFail(c,'Console disconnected. Please wait a few moments and try again.');
 const net=route==='net'||(route!=='lan'&&!c.disc);
 const flow=net?[1,2,3,4,5,6,7]:k==='standby'?[0,4,7]:[4,7];
 S.conn={ci:CONSOLES.indexOf(c),flow,cur:0,play:true,all:false};openConn();
}
function openConn(){
 S.screen='conn';showOnly('conn');paintTop();paintConn();
 clearTimeout(stageTimer);
 if(S.conn.play){const run=()=>{stageTimer=setTimeout(()=>{S.conn.cur++;if(S.conn.cur<S.conn.flow.length){paintConn();run();}else{S.conn.cur=S.conn.flow.length-1;S.unst=false;openStream();}},1000);};run();}
}
function paintConn(){
 const C=S.conn,c=CONSOLES[C.ci],st=C.flow[Math.min(C.cur,C.flow.length-1)],list=C.flow.map(i=>STAGES[i]);
 const via=C.flow.includes(1)?'via Internet':'via Local Network';
 $('#ly-conn').innerHTML=`<div class="ptitle">${ico(C.flow.includes(1)?'globe':st===0?'moon':'lan',32)}<h1>${connTitle(C.flow,st)}</h1></div><div class="prule"></div>
 <div class="wk"><div class="art"><div class="halo"></div><div class="spin"></div>${ring(c,128)}</div><div class="who"><div class="tlw">${typeLogo(c,32)}</div><h2>${c.name}</h2><p>${via}</p></div>
 <div class="steps">${stepsHTML(list,Math.min(C.cur,list.length-1))}</div><div style="position:absolute;right:48px;top:440px">${tbtn('Cancel',{do:'ccancel'})}</div></div>`;
 refreshHints();
}
function openReconnect(){
 S.screen='conn';showOnly('conn');paintTop();
 $('#ly-conn').innerHTML=`<div class="ptitle">${ico('wifi',32)}<h1>Optimizing Stream</h1></div><div class="prule"></div>
 <div class="wk"><div class="art"><div class="halo"></div><div class="spin"></div></div><div class="rc">Recovering from packet loss<b>Retrying at 1.80 Mbps</b><small>Attempt ${S.rec.attempt}<br>Please wait...</small></div></div>`;
 refreshHints();
}

/* ---------------- STREAM (C19 Pill, C25 StatsPanel) ---------------- */
function openStream(){
 S.screen='strm';showOnly('strm');S.exitStart=Date.now();
 $('#ly-strm').innerHTML=`<div class="scene"><div class="drift"></div></div><div class="hud"><div class="exit" id="exitPill"></div><div class="panel" id="statPanel"></div><div class="unst" id="unstB"></div></div>`;
 paintHud();
}
function paintHud(){
 if(S.screen!=='strm')return;
 const e=$('#exitPill'),p=$('#statPanel'),u=$('#unstB');if(!e)return;
 const el=Date.now()-S.exitStart,show=SET('exit').v;
 e.innerHTML=show?pill(`Back to menu: Hold ${inl('L')}&nbsp;+&nbsp;${inl('R')}&nbsp;+&nbsp;${inl('start')}`):'';
 e.classList.toggle('gone',el>5000);
 const lat=S.unst?112:38,fps=S.unst?'41 / 60':(SET('fps').v?'59 / 60':'30 / 30');
 p.style.display=SET('lat').v?'block':'none';
 p.innerHTML=`<div class="t">Stream Stats</div><div class="r">Latency<b>${lat} ms</b></div><div class="r">FPS<b>${fps}</b></div>`;
 u.innerHTML=S.unst&&SET('net').v?unstPill():'';
}
setInterval(()=>paintHud(),1000);

/* ---------------- input ---------------- */
function refreshHints(){
 if(S.pop||S.kb)return;
 const sc=S.screen;
 if(sc==='home')return paintHints(homeHints());
 if(sc==='page')return paintHints(pageHints());
 if(sc==='ctrl')return paintHints(ctrlHints());
 if(sc==='pin')return paintHints(pinHints());
 if(sc==='conn')return paintHints(S.conn&&$('.rc')?[]:[['cancel','Cancel',{key:'back'}]]);
}
function key(k){
 if(S.kb)return kbKey(k);
 if(S.pop)return popKey(k);
 const sc=S.screen;
 if(sc==='conn'){if(k==='back'&&!$('.rc')){clearTimeout(stageTimer);openHome(false);}return;}
 if(sc==='strm'){if(k==='back'){openHome(false);}return;}
 if(sc==='pin')return pinKey(k);
 if(sc==='page')return pageKey(k);
 if(sc==='ctrl')return ctrlKey(k);
 homeKey(k);
}
function homeKey(k){
 const n=cache.length,c=S.cat;
 if(S.opts){const L=optsList();
  if(k==='up'&&S.os>0)S.os--;else if(k==='down'&&S.os<L.length-1)S.os++;
  else if(k==='back'||k==='tri')S.opts=false;
  else if(k==='ok'){const o=L[S.os];if(o.dis)return;S.opts=false;paintOpts();o.f();return;}
  return paintOpts();}
 if(k==='up'&&S.sel[c]>0){S.sel[c]--;layout();}
 else if(k==='down'&&S.sel[c]<n-1){S.sel[c]++;layout();}
 else if((k==='left'||k==='L')&&c>0){S.cat--;buildList(true);}
 else if((k==='right'||k==='R')&&c<CATS.length-1){S.cat++;buildList(true);}
 else if(k==='start'&&c===0&&(CONSOLES.length>4||S.flt)){if(S.flt){S.flt='';buildList(true);}else openFilter();}
 else if(k==='tri'&&c===0&&selItem()&&selItem().k==='console'){S.opts=true;S.os=0;paintOpts();}
 else if(k==='ok'){const it=selItem();if(!it)return;
  if(it.k==='console')connect(it.c);
  else if(it.k==='grp')openPage('settings',it.gi,'r');
  else if(it.k==='pf')openPage('profile',it.gi,'r');
  else if(it.k==='preset'){S.ct.slot=it.i;openCtrl();}}
}
function openFilter(){keyboard({title:'Filter Consoles',text:'',done:()=>{S.flt='den';S.sel[0]=0;buildList(true);}});}
function pageKey(k){
 const P=S.pg,cfg=PG[P.kind],{rs,i:s,k:kk}=curRow(),login=P.kind==='profile'&&P.g===2&&psnState()==='await';
 if(login){
  if(k==='ok')return keyboard({title:'Paste full redirect URL',text:'https://remoteplay.dl.playstation.net/remoteplay/redirect?code=v3.AbC123',done:()=>{S.psn='auth';paintPage(true);toast('PSN login complete','ok','check');}});
  if(k==='start'){S.qr=!S.qr;paintPage(false);return toast(S.qr?'QR shown. Scan it with your phone.':'QR hidden. Press Start to show it again.');}
  if(k==='select')return toast('Opened browser fallback. Phone QR is still recommended.');
  if(k==='sq'){S.psn='none';paintPage(true);return toast('PSN login canceled');}
  if(k==='back'){P.focus='g';S.psn='none';paintPage(true);}
  return;
 }
 if(k==='L'){P.g=(P.g+cfg.groups.length-1)%cfg.groups.length;paintPage(true);return;}
 if(k==='R'){P.g=(P.g+1)%cfg.groups.length;paintPage(true);return;}
 if(P.focus==='g'){
  if(k==='up'&&P.g>0){P.g--;paintPage(true);}
  else if(k==='down'&&P.g<cfg.groups.length-1){P.g++;paintPage(true);}
  else if(k==='right'||k==='ok'){P.focus='r';paintPage(false);}
  else if(k==='back')returnHome();
  return;
 }
 if(k==='up'&&s>0){P.row[kk]--;paintPage(false);}
 else if(k==='down'&&s<rs.length-1){P.row[kk]++;paintPage(false);}
 else if(k==='left'){if(rs[s].type==='choice')pageChange(-1);else{P.focus='g';paintPage(false);}}
 else if(k==='right')pageChange(1);
 else if(k==='back'){P.focus='g';paintPage(false);}
 else if(k==='ok')pageAct(rs[s]);
}
function returnHome(){S.cat=S.pg.kind==='settings'?1:3;S.sel[S.cat]=S.pg.g;openHome(false);}
function ctrlKey(k){
 const C=S.ct,m=MAPS[C.slot];
 if(C.view==='summary'){
  if(k==='back')return openHome(false);
  if(k==='left'||k==='right'){C.slot=(C.slot+(k==='right'?1:2))%3;return paintCtrl();}
  if(k==='L'||k==='R'){C.page=C.page?0:1;return paintCtrl();}
  if((k==='up'||k==='down')&&C.page===0){C.sh=C.sh?0:1;return paintCtrl();}
  if(k==='ok'){if(C.page===0)return shoulderPop(C.sh?'R1':'L1');C.view='back';C.cur=0;return paintCtrl();}
  if(k==='tri'){C.view=C.page===0?'front':'back';C.cur=0;C.pick=[];return paintCtrl();}
  if(k==='sq'){m[C.page?'back':'front']=Array(18).fill('None');return paintCtrl();}
  return;
 }
 if(k==='back'){const was=C.view;C.view='summary';C.page=was==='back'?1:0;C.pick=[];return paintCtrl();}
 const mv=(dr,dc)=>{const r=Math.floor(C.cur/6)+dr,c=C.cur%6+dc;if(r<0||r>2||c<0||c>5)return;C.cur=r*6+c;if(C.hold&&!C.pick.includes(C.cur))C.pick.push(C.cur);paintCtrl();};
 if(k==='left')mv(0,-1);else if(k==='right')mv(0,1);else if(k==='up')mv(-1,0);else if(k==='down')mv(1,0);
 else if(k==='tri')fullPop();
 else if(k==='sq'){m[C.view==='front'?'front':'back']=Array(18).fill('None');paintCtrl();}
}
const PHYS={ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left',ArrowRight:'right',Enter:'cross',Escape:'circle',t:'tri',T:'tri',q:'L',Q:'L',e:'R',E:'R',s:'sq',S:'sq',' ':'start',z:'select',Z:'select'};
const logical=p=>p==='cross'?(SET('cc').v?'back':'ok'):p==='circle'?(SET('cc').v?'ok':'back'):p;
const down=new Set();let exitT=null;
addEventListener('keydown',e=>{
 if(e.target&&['SELECT','INPUT'].includes(e.target.tagName))return;
 const p=PHYS[e.key];if(!p)return;e.preventDefault();
 if(e.repeat)return;
 down.add(p);
 if(S.screen==='strm'&&down.has('L')&&down.has('R')&&down.has('start')){clearTimeout(exitT);exitT=setTimeout(()=>{if(down.has('L')&&down.has('R')&&down.has('start'))openHome(false);},1000);}
 const k=logical(p);
 if(k==='ok'&&S.screen==='ctrl'&&S.ct.view!=='summary'&&!S.pop&&!S.kb){S.ct.hold=true;S.ct.pick=[S.ct.cur];paintCtrl();return;}
 key(k);
});
addEventListener('keyup',e=>{
 const p=PHYS[e.key];if(!p)return;down.delete(p);
 const k=logical(p);
 if(k==='ok'&&S.ct.hold){S.ct.hold=false;if(S.screen==='ctrl'&&!S.pop){const pk=S.ct.pick.length?S.ct.pick.slice():[S.ct.cur];zonePop(pk);}}
});

/* ---------------- touch (clicks) ---------------- */
function act(a,arg,el){
 const i=+arg;
 if(a==='btn')return key(arg);
 if(a==='cat'){S.cat=i;buildList(true);}
 else if(a==='item'){if(S.sel[S.cat]!==i){S.sel[S.cat]=i;layout();}else key('ok');}
 else if(a==='oi'){S.os=i;key('ok');}
 else if(a==='kbflt')openFilter();
 else if(a==='fltclr'){S.flt='';buildList(true);}
 else if(a==='pgg'){S.pg.g=i;S.pg.focus='g';paintPage(true);}
 else if(a==='row'){const {rs,k}=curRow();S.pg.focus='r';S.pg.row[k]=i;if(rs[i].type==='info')paintPage(false);else{paintPage(false);pageAct(rs[i]);}}
 else if(a==='dec'||a==='inc'){const {k}=curRow();S.pg.focus='r';S.pg.row[k]=i;pageChange(a==='inc'?1:-1);}
 else if(a==='preset'){S.ct.slot=(S.ct.slot+i+3)%3;paintCtrl();}
 else if(a==='callout'){S.ct.sh=i;shoulderPop(i?'R1':'L1');}
 else if(a==='dg'){S.ct.view=S.ct.page===0?'front':'back';S.ct.cur=0;S.ct.pick=[];paintCtrl();}
 else if(a==='cell'){if(S.ct.view==='summary'){S.ct.view='back';S.ct.page=1;paintCtrl();}else{S.ct.cur=i;zonePop([i]);}}
 else if(a==='qrtog')key('start');
 else if(a==='pdig'){S.pin.cur=i;S.pin.zone='d';paintPin();}
 else if(a==='pup'){S.pin.cur=i;S.pin.zone='d';pinKey('up');}
 else if(a==='pdn'){S.pin.cur=i;S.pin.zone='d';pinKey('down');}
 else if(a==='pclr'){S.pin.zone='d';pinKey('sq');}
 else if(a==='pcancel')pinKey('back');
 else if(a==='pok'){if(S.pin.d.every(x=>x!==null))pinRegister();}
 else if(a==='ccancel'){clearTimeout(stageTimer);openHome(false);}
 else if(a==='pop'){S.pop.sel=i;popPress(i);}
 else if(a==='scrim')key('back');
 else if(a==='kbok')key('ok');
}
/* touch swipe: vertical on the list, page pane and list popups; horizontal on the category row. One step per 56 px (list, categories) or 48 px (rows); focus follows the finger like the D-pad. */
let sw=null,suppressUntil=0;
const zoomNow=()=>parseFloat(getComputedStyle($('#wrap')).getPropertyValue('--z'))||1;
document.addEventListener('pointerdown',e=>{
 if(S.kb||!$('#stage').contains(e.target))return;
 let t=null;
 if(S.pop){if(e.target.closest('.lp')&&S.pop.rows&&!S.pop.grid)t=['v',48];}
 else if(S.screen==='home'&&!S.opts){if(e.target.closest('#listvp'))t=['v',56];else if(e.target.closest('#catstrip,.cat'))t=['h',56];}
 else if(S.screen==='page'&&e.target.closest('#pane'))t=['v',48];
 if(t)sw={dir:t[0],size:t[1],x:e.clientX,y:e.clientY,done:0,moved:false};
});
document.addEventListener('pointermove',e=>{
 if(!sw)return;
 const d=(sw.dir==='v'?e.clientY-sw.y:e.clientX-sw.x)/zoomNow();
 if(Math.abs(d)>8)sw.moved=true;
 if(!sw.moved)return;
 if(S.screen==='page'&&!S.pop)S.pg.focus='r';
 const steps=Math.trunc(-d/sw.size);
 while(steps>sw.done){key(sw.dir==='v'?'down':'right');sw.done++;}
 while(steps<sw.done){key(sw.dir==='v'?'up':'left');sw.done--;}
});
document.addEventListener('pointerup',()=>{if(sw&&sw.moved)suppressUntil=Date.now()+80;sw=null;});
document.addEventListener('click',e=>{
 if(Date.now()<suppressUntil){e.stopPropagation();return;}
 if(!$('#stage').contains(e.target))return;
 if(S.opts&&!S.pop&&!e.target.closest('#opts')&&!e.target.closest('[data-do=btn]')){S.opts=false;paintOpts();e.stopPropagation();return;}
 const p=e.target.closest('[data-pop]');if(p&&S.pop){S.pop.sel=+p.dataset.pop;popPress(+p.dataset.pop);return;}
 const el=e.target.closest('[data-do]');if(el){
  if(S.pop&&!['pop','scrim'].includes(el.dataset.do)&&el.dataset.do!=='btn')return;
  act(el.dataset.do,el.dataset.arg,el);
 }
});
/* touch: drag across zones paints a multi-selection; release assigns (finger paint, backtrack removes) */
let paint=null;
document.addEventListener('pointerdown',e=>{const c=e.target.closest&&e.target.closest('.cell');if(c&&S.screen==='ctrl'&&S.ct.view!=='summary'&&!S.pop){paint=[+c.dataset.arg];S.ct.pick=paint.slice();S.ct.cur=paint[0];e.preventDefault();}});
document.addEventListener('pointermove',e=>{if(!paint)return;const el=document.elementFromPoint(e.clientX,e.clientY),c=el&&el.closest&&el.closest('.cell');if(!c)return;const i=+c.dataset.arg;if(paint[paint.length-2]===i)paint.pop();else if(!paint.includes(i))paint.push(i);S.ct.pick=paint.slice();S.ct.cur=i;paintCtrl();});
document.addEventListener('pointerup',()=>{if(!paint)return;const pk=paint;paint=null;suppressUntil=Date.now()+80;S.ct.cur=pk[pk.length-1];zonePop(pk);});

/* ---------------- mock chrome: deep links, toggles, device frame ---------------- */
const JUMPS=[
 ['Home','home','Consoles: Ready'],['Home','consoles-standby','Console on standby'],['Home','consoles-psn','Internet-only console'],['Home','consoles-unavailable','Console not reachable'],['Home','consoles-unpaired','Console unpaired'],['Home','consoles-cooldown','Console in cooldown + banner'],['Home','hints','Per-console status hints'],['Home','home-unstable','Network Unstable on a menu'],['Home','empty-searching','Empty: Searching'],['Home','empty-nomatch','Empty: No match'],['Home','filter','Filter active'],['Home','keyboard','System keyboard (filter)'],['Home','options','Options column'],['Home','options-unpaired','Options, unpaired console'],['Home','icon-picker','Change icon'],['Home','connect-via','Connect via'],['Home','repair','Re-pair confirm'],
 ['XMB categories','xmb-settings','Settings category'],['XMB categories','xmb-controller','Controller category'],['XMB categories','xmb-profile','Profile category'],
 ['Pairing','pin','PIN entry (empty)'],['Pairing','pin-partial','PIN entry (partly filled)'],['Pairing','pin-full','PIN entry (ready to register)'],['Pairing','result-paired','Result: paired'],['Pairing','result-pair-failed','Result: PIN not accepted'],['Pairing','result-pair-timeout','Result: pairing timed out'],['Pairing','result-pair-unreachable','Result: console unreachable'],['Pairing','result-connect-failed','Result: could not connect'],
 ['Connecting','waking','Waking Console'],['Connecting','connecting','Starting Remote Play'],['Connecting','connecting-internet','Starting Internet Remote Play'],['Connecting','waking-all','All 8 stages (reference)'],['Connecting','reconnecting','Reconnecting'],
 ['Stream','stream','Stream overlay'],['Stream','stream-stats','Stream with stats'],['Stream','unstable','Network unstable'],['Stream','stream-quiet','Overlay after hint faded'],
 ['Settings','settings','Video'],['Settings','settings-network','Network'],['Settings','settings-display','Display'],['Settings','settings-controls','Controls'],['Settings','settings-advanced','Advanced'],['Settings','settings-circle','Circle confirm on (glyphs swap)'],
 ['Profile','profile','Account'],['Profile','profile-connection','Connection: Local Wi-Fi'],['Profile','profile-connection-standby','Connection: console on standby'],['Profile','profile-connection-psn','Connection: PSN Internet'],['Profile','profile-connection-unavailable','Connection: Unavailable'],['Profile','profile-connection-unpaired','Connection: unpaired console'],['Profile','profile-connection-none','Connection: no console'],['Profile','profile-psn','PlayStation Network (signed in)'],['Profile','profile-psn-disabled','PSN: Disabled'],['Profile','profile-psn-none','PSN: Not authenticated'],['Profile','profile-psn-expired','PSN: Token expired'],['Profile','profile-psn-refresh','PSN: Refreshing token'],['Profile','profile-psn-error','PSN: error text'],['Profile','profile-login','Phone login assist'],['Profile','profile-login-hidden','Phone login, QR hidden'],['Profile','profile-logout','Log out, second press'],['Profile','toast-account','Toast: Account ID refreshed'],['Profile','toast-login-complete','Toast: PSN login complete'],['Profile','keyboard-paste','System keyboard (paste URL)'],
 ['Controller','controller','Summary page 1'],['Controller','controller-back','Summary page 2'],['Controller','controller-front','Front touch zones'],['Controller','controller-rear','Rear touch zones'],['Controller','controller-multi','Multi-select zones'],['Controller','controller-full','Whole front surface'],['Controller','mapping-popup','Mapping popup, one zone'],['Controller','mapping-multi','Mapping popup, several zones'],['Controller','mapping-shoulder','Mapping popup, L1']
];
const DL={};
const C0=(c,i)=>{S.cat=c;S.sel[c]=i||0;openHome(false);};
const ORIG=CONSOLES.map(c=>Object.assign({},c));
const mockConsole=()=>{CONSOLES.length=0;ORIG.forEach(o=>CONSOLES.push(Object.assign({},o)));S.flt='';S.noConsoles=false;};
Object.assign(DL,{
 home:()=>{mockConsole();C0(0,0);},
 'consoles-standby':()=>{mockConsole();C0(0,1);},
 'consoles-psn':()=>{mockConsole();C0(0,2);},
 'consoles-unavailable':()=>{mockConsole();C0(0,3);},
 'consoles-unpaired':()=>{mockConsole();C0(0,4);},
 'consoles-cooldown':()=>{mockConsole();CONSOLES[1].cool=true;C0(0,1);},
 hints:()=>{mockConsole();CONSOLES[0].hint='busy';CONSOLES[1].hint='wake';CONSOLES[2].hint='psn';C0(0,0);},
 'home-unstable':()=>{mockConsole();S.unst=true;$('#bUn').classList.add('on');$('#bUn').textContent='Network unstable: on';C0(0,0);},
 'empty-searching':()=>{mockConsole();S.noConsoles=true;C0(0,0);},
 'empty-nomatch':()=>{mockConsole();S.flt='xyz';C0(0,0);},
 filter:()=>{mockConsole();S.flt='den';C0(0,0);},
 keyboard:()=>{mockConsole();C0(0,0);openFilter();},
 options:()=>{mockConsole();C0(0,0);S.opts=true;S.os=0;paintOpts();},
 'options-unpaired':()=>{mockConsole();C0(0,4);S.opts=true;S.os=0;paintOpts();},
 'icon-picker':()=>{mockConsole();C0(0,0);iconPicker(CONSOLES[0]);},
 'connect-via':()=>{mockConsole();C0(0,0);viaPop(CONSOLES[0]);},
 repair:()=>{mockConsole();C0(0,0);repairPop(CONSOLES[0]);},
 'xmb-settings':()=>C0(1,0),'xmb-controller':()=>C0(2,0),'xmb-profile':()=>C0(3,2),
 pin:()=>{mockConsole();startPin(CONSOLES[4]);},
 'pin-partial':()=>{mockConsole();startPin(CONSOLES[4]);S.pin.d=[4,8,2,null,null,null,null,null];S.pin.cur=3;paintPin();},
 'pin-full':()=>{mockConsole();startPin(CONSOLES[4]);S.pin.d=[4,8,2,7,1,9,3,6];S.pin.cur=7;S.pin.zone='b';S.pin.b=2;paintPin();},
 'result-paired':()=>{mockConsole();startPin(CONSOLES[4]);S.pin.d=[4,8,2,7,1,9,3,6];S.pin.cur=7;paintPin();pairOk(CONSOLES[4]);},
 'result-pair-timeout':()=>{mockConsole();startPin(CONSOLES[4]);S.pin.d=[9,8,2,7,1,9,3,6];S.pin.cur=7;S.pin.zone='b';paintPin();pairFail(CONSOLES[4],'timeout');},
 'result-pair-unreachable':()=>{mockConsole();startPin(CONSOLES[4]);S.pin.d=[4,8,2,7,1,9,3,6];S.pin.cur=7;S.pin.zone='b';paintPin();pairFail(CONSOLES[4],'unreachable');},
 'result-pair-failed':()=>{mockConsole();startPin(CONSOLES[4]);S.pin.d=[0,8,2,7,1,9,3,6];S.pin.cur=7;paintPin();pairFail(CONSOLES[4]);},
 'result-connect-failed':()=>{mockConsole();C0(0,3);connFail(CONSOLES[3],'Console disconnected. Please wait a few moments and try again.');},
 waking:()=>{S.conn={ci:1,flow:[0,4,7],cur:0,play:false};openConn();},
 connecting:()=>{S.conn={ci:0,flow:[4,7],cur:0,play:false};openConn();},
 'connecting-internet':()=>{S.conn={ci:2,flow:[1,2,3,4,5,6,7],cur:2,play:false};openConn();},
 'waking-all':()=>{S.conn={ci:1,flow:[0,1,2,3,4,5,6,7],cur:3,play:false};openConn();},
 reconnecting:()=>openReconnect(),
 stream:()=>{S.unst=false;SET('lat').v=false;openStream();},
 'stream-stats':()=>{S.unst=false;SET('lat').v=true;openStream();},
 unstable:()=>{S.unst=true;SET('lat').v=true;openStream();$('#bUn').classList.add('on');$('#bUn').textContent='Network unstable: on';},
 'stream-quiet':()=>{S.unst=false;SET('lat').v=false;openStream();S.exitStart=Date.now()-6000;paintHud();},
 settings:()=>openPage('settings',0,'r'),'settings-network':()=>openPage('settings',1,'r'),'settings-display':()=>openPage('settings',2,'r'),'settings-controls':()=>openPage('settings',3,'r'),'settings-advanced':()=>openPage('settings',4,'r'),
 'settings-circle':()=>{SET('cc').v=true;openPage('settings',3,'r');},
 profile:()=>{S.psn='auth';openPage('profile',0,'r');},
 'profile-connection':()=>{S.pconn='wifi';openPage('profile',1,'r');},
 'profile-connection-standby':()=>{S.pconn='standby';openPage('profile',1,'r');},
 'profile-connection-psn':()=>{S.pconn='psn';openPage('profile',1,'r');},
 'profile-connection-unavailable':()=>{S.pconn='unavail';openPage('profile',1,'r');},
 'profile-connection-unpaired':()=>{S.pconn='unreg';openPage('profile',1,'r');},
 'profile-connection-none':()=>{S.pconn='none';openPage('profile',1,'r');},
 'profile-psn':()=>{S.psn='auth';SET('psnmode').v=true;openPage('profile',2,'r');},
 'profile-psn-disabled':()=>{SET('psnmode').v=false;openPage('profile',2,'r');},
 'profile-psn-none':()=>{SET('psnmode').v=true;S.psn='none';openPage('profile',2,'r');},
 'profile-psn-expired':()=>{SET('psnmode').v=true;S.psn='expired';openPage('profile',2,'r');},
 'profile-psn-refresh':()=>{SET('psnmode').v=true;S.psn='refresh';openPage('profile',2,'r');},
 'profile-psn-error':()=>{SET('psnmode').v=true;S.psn='error';openPage('profile',2,'r');},
 'profile-login':()=>{SET('psnmode').v=true;S.psn='await';S.qr=true;openPage('profile',2,'r');},
 'profile-login-hidden':()=>{SET('psnmode').v=true;S.psn='await';S.qr=false;openPage('profile',2,'r');},
 'profile-logout':()=>{SET('psnmode').v=true;S.psn='auth';S.logout=true;openPage('profile',2,'r');S.pg.row['profile2']=2;paintPage(false);},
 'toast-account':()=>{S.psn='auth';openPage('profile',0,'r');S.pg.row['profile0']=1;paintPage(false);toast('Account ID refreshed from system profile','ok','check');},
 'toast-login-complete':()=>{SET('psnmode').v=true;S.psn='auth';openPage('profile',2,'r');toast('PSN login complete','ok','check');},
 'keyboard-paste':()=>{SET('psnmode').v=true;S.psn='await';openPage('profile',2,'r');keyboard({title:'Paste full redirect URL',text:'https://remoteplay.dl.playstation.net/remoteplay/redirect?code=v3.AbC123',done:()=>{S.psn='auth';paintPage(true);toast('PSN login complete','ok','check');}});},
 controller:()=>{S.ct.slot=0;S.ct.sh=0;openCtrl();},
 'controller-back':()=>{S.ct.slot=0;openCtrl('summary',1);},
 'controller-front':()=>{S.ct.slot=0;openCtrl('front');S.ct.cur=7;paintCtrl();},
 'controller-rear':()=>{S.ct.slot=0;openCtrl('back');S.ct.cur=2;paintCtrl();},
 'controller-multi':()=>{S.ct.slot=0;openCtrl('front');S.ct.hold=true;S.ct.pick=[7,8,9,10];S.ct.cur=10;paintCtrl();S.ct.hold=false;},
 'controller-full':()=>{S.ct.slot=0;openCtrl('front');S.ct.cur=0;paintCtrl();fullPop();},
 'mapping-popup':()=>{S.ct.slot=0;openCtrl('front');S.ct.cur=7;paintCtrl();zonePop([7]);},
 'mapping-multi':()=>{S.ct.slot=0;openCtrl('front');S.ct.pick=[7,8,9,10];S.ct.cur=10;paintCtrl();zonePop([7,8,9,10]);},
 'mapping-shoulder':()=>{S.ct.slot=0;openCtrl();shoulderPop('L1');}
});
DL['consoles-unregistered']=DL['consoles-unpaired'];DL.error=DL['result-connect-failed'];DL.register=DL.pin;DL.chooser=DL['connect-via'];
function jump(id){
 closePop();S.kb=null;paintKb();S.toast=null;paintToast();clearTimeout(stageTimer);
 SET('cc').v=id==='settings-circle';$('#bCc').textContent='Confirm: '+(SET('cc').v?'Circle':'Cross');
 if(id!=='home-unstable'&&id!=='unstable'){S.unst=false;$('#bUn').classList.remove('on');$('#bUn').textContent='Network unstable: off';}
 if(!id.startsWith('profile')&&!id.startsWith('toast')&&!id.startsWith('keyboard-paste')){S.logout=false;}
 const f=DL[id]||DL.home;f();$('#jump').value=DL[id]?id:'home';
}
function init(){
 const skel=`<canvas id="rib" width="960" height="544"></canvas><div class="vig"></div><div class="wash" style="opacity:0"></div>
 <div class="layer" id="ly-home"><div id="catstrip" style="position:absolute;left:0;top:64px;width:960px;height:112px"></div><div id="cats"></div><div id="flt"></div><div id="listvp"><div id="list"></div></div><div id="empty" class="empty"></div><div id="detail"></div><div id="opts"></div></div>
 <div class="layer" id="ly-page" style="display:none"></div><div class="layer" id="ly-ctrl" style="display:none"></div><div class="layer" id="ly-pin" style="display:none"></div><div class="layer" id="ly-conn" style="display:none"></div><div class="layer" id="ly-strm" style="display:none"></div>
 <div class="topbar" id="top"></div><div id="hintbar"></div><div id="popLayer" style="position:absolute;inset:0;pointer-events:none;z-index:30"></div><div id="toastLayer"></div><div id="kbLayer"></div>`;
 $('#screen').innerHTML=skel;$('#screen').style.cssText='position:absolute;inset:0';
 $('#empty').className='empty';
 cv=$('#rib');g=cv.getContext('2d');loop(0);
 buildCats();
 const sel=$('#jump');let grp='';
 sel.innerHTML=JUMPS.map(j=>{const o=(j[0]!==grp?(grp?'</optgroup>':'')+`<optgroup label="${j[0]}">`:'')+`<option value="${j[1]}">${j[2]}</option>`;grp=j[0];return o;}).join('')+'</optgroup>';
 sel.addEventListener('change',()=>{location.hash=sel.value;sel.blur();});
 $('#bCc').addEventListener('click',e=>{SET('cc').v=!SET('cc').v;e.target.textContent='Confirm: '+(SET('cc').v?'Circle':'Cross');e.target.classList.toggle('on',SET('cc').v);refreshHints();if(S.screen==='page')paintPage(false);});
 $('#bUn').addEventListener('click',e=>{S.unst=!S.unst;e.target.classList.toggle('on',S.unst);e.target.textContent='Network unstable: '+(S.unst?'on':'off');refreshHints();paintHud();});
 $('#bZoom').addEventListener('click',e=>{S.zoom=S.zoom===1?2:1;fit();e.target.classList.toggle('on',S.zoom===2);});
 $('#bDev').addEventListener('click',e=>{const on=$('#wrap').classList.toggle('dev');e.target.classList.toggle('on',on);e.target.setAttribute('aria-pressed',on);e.target.textContent=on?'Plain screen':'View on device';fit();});
 addEventListener('resize',fit);fit();
 setInterval(()=>$$('.clk').forEach(n=>n.textContent=hhmm()),5000);
 $('#legend').innerHTML=`<span><kbd>Arrows</kbd>D-pad</span><span><kbd>Enter</kbd>Cross</span><span><kbd>Esc</kbd>Circle</span><span><kbd>T</kbd>Triangle</span><span><kbd>S</kbd>Square</span><span><kbd>Q</kbd><kbd>E</kbd>L / R</span><span><kbd>Space</kbd>Start</span><span><kbd>Z</kbd>Select</span><span><kbd>Click</kbd>touch (drag paints zones)</span><span>Stream: hold <kbd>Q</kbd><kbd>E</kbd><kbd>Space</kbd> or <kbd>Esc</kbd></span><span>Keys are the physical buttons: with Circle Button Confirm on, Esc confirms and Enter goes back.</span>`;
}
function fit(){const dev=$('#wrap').classList.contains('dev'),w=dev?1260:960,f=Math.min(1,(innerWidth-32)/w);$('#wrap').style.setProperty('--z',+(S.zoom*f).toFixed(3));}
init();
function route(){const h=(location.hash||'#home').slice(1);jump(h);['#stage','#screen','.devScale','#wrap'].forEach(s=>{const e=$(s);if(e){e.scrollTop=0;e.scrollLeft=0;}});scrollTo(0,0);}
addEventListener('hashchange',route);
if(location.hash)setTimeout(route,60);else jump('home');
document.addEventListener('dragstart',e=>e.preventDefault());

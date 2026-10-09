/* VitaRPS5 XMB mock: reusable components. Each returns markup for one component of SPEC.md section 2.
   No colour or size literal lives here; classes and tokens do the work. */

/* flat stroke icons, 24 grid, 2 px round stroke, no fills (used in rows, popups and the top bar) */
const ICONS={
 back:'<path d="M15 5l-7 7 7 7"/>',next:'<path d="M9 5l7 7-7 7"/>',up:'<path d="M5 15l7-7 7 7"/>',down:'<path d="M5 9l7 7 7-7"/>',
 check:'<path d="M5 12.5l4.5 4.5L19 7.5"/>',close:'<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>',
 warn:'<path d="M12 3.5L2.5 20.5h19z"/><path d="M12 10v4.5M12 17.5h.01"/>',
 lock:'<rect x="5" y="11" width="14" height="10"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
 globe:'<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c3.2 3 3.2 15 0 18M12 3c-3.2 3-3.2 15 0 18"/>',
 moon:'<path d="M20 14.5A8.5 8.5 0 0 1 9.5 4 8.5 8.5 0 1 0 20 14.5z"/>',
 clock:'<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3.5 2"/>',
 wifi:'<path d="M2.5 9a14.5 14.5 0 0 1 19 0M5.5 12.5a10 10 0 0 1 13 0M8.6 16a5.5 5.5 0 0 1 6.8 0M12 19.5h.01"/>',
 battery:'<rect x="2" y="7" width="17" height="10"/><path d="M21.5 10.5v3M5.5 10.5v3M8.5 10.5v3M11.5 10.5v3"/>',
 lan:'<rect x="9" y="3" width="6" height="5"/><rect x="3" y="16" width="6" height="5"/><rect x="15" y="16" width="6" height="5"/><path d="M12 8v4M6 16v-4h12v4"/>'
};
const ico=(n,s=24)=>`<svg class="ico" width="${s}" height="${s}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[n]||''}</svg>`;

/* button glyphs: the app's own four PNGs plus baked flat glyphs for D-pad, shoulders, Start, Select */
const PNG={ex:'symbol_ex',circle:'symbol_circle',sq:'symbol_square',tri:'symbol_triangle'};
const S1=(b)=>`<svg class="g" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#fff" stroke-width="1.5" stroke-linejoin="round">${b}</svg>`;
const pillG=(t,w)=>`<svg class="g" viewBox="0 0 ${w} 24" width="${w}" height="24"><rect x="1" y="4" width="${w-2}" height="16" rx="8" fill="none" stroke="#fff" stroke-width="1.5"/><text x="${w/2}" y="16.5" text-anchor="middle" font-size="${t.length>2?9:12}" font-family="Roboto,sans-serif" font-weight="500" fill="#fff" stroke="none">${t}</text></svg>`;
const GLY={
 dpad:S1('<path d="M9 3h6v6h6v6h-6v6H9v-6H3V9h6z"/>'),
 dpadh:S1('<path d="M9 7 3 12l6 5zM15 7l6 5-6 5z" fill="#fff"/>'),
 dpadu:S1('<path d="M7 15l5-6 5 6z" fill="#fff"/>'),
 dpadv:S1('<path d="M7 9l5-6 5 6zM7 15l5 6 5-6z" fill="#fff"/>'),
 L:pillG('L',32),R:pillG('R',32),
 LR:`<span style="display:flex;gap:4px">${pillG('L',32)}${pillG('R',32)}</span>`,
 start:pillG('START',48),select:pillG('SELECT',56),
 tap:S1('<circle cx="12" cy="10" r="3"/><path d="M12 13v8M9 21h6" stroke-linecap="round"/>')
};
/* confirm and cancel resolve through the Circle Button Confirm setting (spec: swap on every screen) */
const gl=(k)=>{
 if(k==='confirm')k=SET('cc').v?'circle':'ex';
 if(k==='cancel')k=SET('cc').v?'ex':'circle';
 return PNG[k]?`<img class="g" src="assets/${PNG[k]}.png" alt="${k}">`:(GLY[k]||'');
};
const inl=(k)=>{ // inline glyph inside running text
 if(k==='confirm')k=SET('cc').v?'circle':'ex';
 if(k==='cancel')k=SET('cc').v?'ex':'circle';
 return PNG[k]?`<img class="inl" src="assets/${PNG[k]}.png" alt="${k}">`:`<span class="gs">${GLY[k]||''}</span>`;
};

/* C06 HintRow. items: [glyph, label, {key, dim}] ; key makes the hint tappable like the button */
const hintRow=(items,extra='')=>`<div class="hintrow"><div class="hl">${items.map(i=>`<span class="h ${i[2]&&i[2].dim?'dim':''}" ${i[2]&&i[2].low?'data-low="1"':''} ${i[2]&&i[2].key?`data-do="btn" data-arg="${i[2].key}"`:''}>${gl(i[0])}${i[1]}</span>`).join('')}</div>${extra?`<div class="hr">${extra}</div>`:''}</div>`;

/* C19 Pill */
const pill=(html,cls='')=>`<span class="pill ${cls}">${html}</span>`;
const unstPill=()=>pill('<i class="d"></i>Network Unstable','unst');

/* C03 StatusRing */
function ring(c,size){ /* Connecting art only: status ring around the room icon, no badge */
 const k=kindOf(c);
 return `<span class="ring ${k}" style="--sz:${size}px"><img class="rm" src="icons/${c.room}.svg" alt="" style="width:${Math.round(size*.5)}px"></span>`;
}
/* console-type logo, bare and white (PS5_logo.png / ps4.png cropped to the wordmark), height h */
function typeLogo(c,h=48){
 if(c.model==='PS5'){const w=Math.round(176*h/48),sc=w/132;return `<span class="tl" style="width:${w}px;height:${h}px;background:url(assets/PS5_logo.png) 0 ${-(8*sc).toFixed(1)}px/${w}px auto no-repeat" role="img" aria-label="PS5"></span>`;}
 const w=Math.round(200*h/48);return `<span class="tl" style="width:${w}px;height:${h}px;background:url(assets/ps4.png) 0 ${-(38*w/100).toFixed(1)}px/${w}px ${w}px no-repeat" role="img" aria-label="PS4"></span>`;
}
const sdot=(k)=>`<span class="sdot" style="--dc:${{ready:'var(--ok)',standby:'var(--warn)',psn:'var(--ok)',unpaired:'var(--idle)',unavail:'var(--text-3)',error:'var(--err)',retry:'var(--warn)',cool:'var(--warn)'}[k]}"></span>`;

/* C09 Toggle, C10 ChoiceValue, C08 SettingRow */
const toggle=v=>`<span class="tgl"><span class="sw ${v?'on':''}"></span><span style="width:24px">${v?'On':'Off'}</span></span>`;
const choice=(txt,i)=>`<span class="chc"><span class="ar" data-do="dec" data-arg="${i}">${ico('back',16)}</span><span class="v">${txt}</span><span class="ar" data-do="inc" data-arg="${i}">${ico('next',16)}</span></span>`;
const srow=(o)=>`<div class="srow ${o.sel?'sel':''} ${o.act?'act':''} ${o.dis?'dis':''} ${o.tone||''}" data-do="row" data-arg="${o.i}"><span>${o.label}</span><span class="val">${o.val||''}</span></div>`;

/* C22 TextButton */
const tbtn=(label,o={})=>`<button class="tb ${o.sm?'sm':''} ${o.sel?'sel':''} ${o.dis?'dis':''}" ${o.do?`data-do="${o.do}" data-arg="${o.arg??''}"`:''}>${o.icon||''}${label}</button>`;

/* C11 PopupShell + C12 ListPopup + C13 Confirm + C14 Result. p: {size,tone,icon,title,sub,body,rows,grid,buttons,sel} */
function popupHTML(p){
 if(p.kind==='pair'){
  const m=p.rows.length-1,vis=4;
  if(p.sel<m){if(p.sel<p.off)p.off=p.sel;else if(p.sel>=p.off+vis)p.off=p.sel-vis+1;}
  p.off=Math.max(0,Math.min(Math.max(0,m+(p.note&&p.rows[0]&&p.rows[0].filter?1:0)-vis),p.off));
  const rowH=(r,i)=>r.filter?`<div class="lr fl ${i===p.sel?'sel':''}" data-pop="${i}"><img src="icons/search.svg" width="24" height="24" alt="">${S.pf?`Filter: "${S.pf}"`:'Filter...'}${S.pf?`<span class="fr"><small>${p.count}</small><button class="tb sm" data-pfclr="1">Clear</button></span>`:''}</div>`
   :`<div class="lr ${i===p.sel?'sel':''}" data-pop="${i}">${r.label}<small>${r.sub}</small></div>`;
  const body=p.rows.slice(0,m).map((r,i)=>rowH(r,i));
  if(p.note)body.splice(p.rows[0]&&p.rows[0].filter?1:0,0,`<div class="lr note">${p.note}</div>`);
  const total=body.length;
  const ipr=p.rows[m];
  const inner=`<h3>${p.title}</h3><div class="body pbody">${p.body}</div><div class="phead"><span>${p.head}</span>${p.spin?'<span class="sp"></span>':''}<span class="cnt">${p.count&&!S.pf?p.count:''}</span></div>
  <div class="lp l4"><div style="transform:translateY(${-p.off*48}px);transition:transform var(--d1) var(--ease)">${body.join('')}</div>${total>vis?`<div class="lscr"><i style="top:${p.off/total*100}%;height:${vis/total*100}%"></i></div>`:''}</div>
  <div class="lr pinrow ${m===p.sel?'sel':''}" data-pop="${m}">${ipr.ip?'Enter IP address':''}<span class="ck">${ico('next',20)}</span></div>`;
  return `<div class="scrimlay" data-do="scrim"></div><div class="popup ${p.size}" role="dialog" aria-label="${p.title}">${inner}</div>`;
 }
 let inner=`<h3>${p.icon?ico(p.icon,32):''}${p.title}</h3>${p.sub?`<div class="psub">${p.sub}</div>`:''}`;
 if(p.body)inner+=`<div class="body">${p.body}</div>`;
 if(p.grid){
  inner+=`<div class="lgrid">${p.rows.map((r,i)=>`<div class="lg ${i===p.sel?'sel':''} ${r.cur?'cur':''}" data-pop="${i}"><span class="mk">${r.cur?ico('check',16):''}</span><img src="${r.img}" alt="">${r.label}</div>`).join('')}</div>`;
 } else if(p.rows){
  const vis=p.size==='l'?6:p.rows.length,n=p.rows.length,off=Math.max(0,Math.min(n-vis,p.sel-Math.floor(vis/2)+1))*48;
  inner+=`<div class="lp ${p.size==='l'?'l6':''}"><div style="transform:translateY(${-off}px);transition:transform var(--d1) var(--ease)">${p.rows.map((r,i)=>`<div class="lr ${i===p.sel?'sel':''} ${r.dis?'dis':''}" data-pop="${i}">${r.img?`<img src="${r.img}" width="32" height="32" alt="">`:''}${r.label}${r.sub?`<small>${r.sub}</small>`:''}${r.cur?`<span class="ck">${ico('check',20)}</span>`:''}</div>`).join('')}</div>${n>vis?`<div class="lscr"><i style="top:${(off/48)/n*100}%;height:${vis/n*100}%"></i></div>`:''}</div>`;
 }
 if(p.buttons)inner+=`<div class="bbar">${p.buttons.map((b,i)=>tbtn(b.label,{sel:i===p.sel,dis:b.dis,do:'pop',arg:i})).join('')}</div>`;
 return `<div class="scrimlay" data-do="scrim"></div><div class="popup ${p.size} ${p.tone||''}" role="dialog" aria-label="${p.title}">${inner}</div>`;
}

/* C15 Toast */
const toastHTML=t=>`<div class="toast ${t.tone||''}">${t.icon?ico(t.icon,24):''}${t.text}</div>`;

/* C16 ProgressSteps */
const stepsHTML=(list,cur)=>list.map((s,i)=>`<div class="stp ${i===cur?'cur':''}"><span class="n">${i<cur?'<img src="assets/ellipse_green.png" width="12" height="12" alt="">':i===cur?'<span class="sp"></span>':`<span class="num">${i+1}</span>`}</span><span>${s[0]}<small>${s[1]}</small></span></div>`).join('');

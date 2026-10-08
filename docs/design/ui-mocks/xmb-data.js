/* VitaRPS5 XMB mock: data and copy. Labels, values, defaults and messages come from the app inventory
   (see SPEC.md section 5, copy deck). New or reworded copy is marked in SPEC.md. */
const $=(s,r=document)=>r.querySelector(s), $$=(s,r=document)=>[...r.querySelectorAll(s)];

/* ---------- consoles ---------- */
const ROOMS=[['tv','TV'],['sofa','Living room'],['bed','Bedroom'],['bunk','Dorm'],['desk','Office'],['house','Another place']];
/* flags: reg paired, disc found on the local network now, awake, net = valid PSN token + internet route, cool = cooldown */
const CONSOLES=[
 {name:'Living Room',model:'PS5',ip:'192.168.1.20',reg:true,disc:true,awake:true,net:true,room:'sofa'},
 {name:'Den',model:'PS4',ip:'192.168.1.31',reg:true,disc:true,awake:false,net:false,room:'tv'},
 {name:'Office',model:'PS5',ip:'',reg:true,disc:false,awake:true,net:true,room:'desk'},
 {name:'Dorm',model:'PS5',ip:'',reg:true,disc:false,awake:false,net:false,room:'bunk'},
 {name:'Bedroom',model:'PS5',ip:'192.168.1.44',reg:false,disc:true,awake:true,net:false,room:'bed'}
];
const kindOf=c=>c.cool?'cool':c.hint?HINT[c.hint].k:!c.reg?(c.disc?'unpaired':'unavail'):c.disc?(c.awake?'ready':'standby'):c.net?'psn':'unavail';
const KIND={
 ready:{t:'Ready',col:'var(--ok)',dot:'green'},
 standby:{t:'Standby',col:'var(--warn)',dot:'yellow'},
 unpaired:{t:'Unpaired',col:'var(--idle)',dot:'idle'},
 psn:{t:'Ready',col:'var(--ok)',dot:'green'},
 unavail:{t:'Unavailable',col:'var(--text-3)',dot:'grey'},
 error:{t:'Error',col:'var(--err)',dot:'red'},
 retry:{t:'Retrying',col:'var(--warn)',dot:'yellow'},
 cool:{t:'Please wait...',col:'var(--warn)',dot:'yellow'}
};
const BADGE={ready:'check',standby:'moon',unpaired:'lock',psn:'globe',unavail:'',error:'warn',retry:'clock',cool:'clock'};
const routeOf=c=>c.disc&&c.net?'Local Network + Internet':c.disc?'Local Network':c.net?'Internet':'Not reachable';
const bothRoutes=c=>c.reg&&c.disc&&c.net;
/* Console detail messages. Each has a status kind: error (red, needs the user) or retry (amber, the app is retrying or waiting). The row shows only the status label; the message shows in the info panel. */
const HINT={
 wake:{k:'error',t:'Wake signal failed. Check pairing and network.'},
 busy:{k:'retry',t:'Console busy - retrying in 3s...'},
 psn:{k:'error',t:'PSN session expired. Re-authenticate in Profile.'}
};
const bannerPill=r=>`<span class="pill warn ban"><span>Streaming stopped:</span><span class="rsn">${r}</span><span>- Please wait a few moments</span></span>`;

/* ---------- settings (inventory: all 16 minus Show Navigation Labels) ---------- */
const GROUPS=['Video','Network','Display','Controls','Advanced'];
const LAT=['Ultra Low (~1.2 Mbps)','Low (~1.8 Mbps)','Balanced (~2.6 Mbps)','High (~3.2 Mbps)','Max (~3.8 Mbps)'];
const SETTINGS=[
 {id:'quality',g:0,label:'Quality Preset',type:'choice',opts:['360p','540p'],v:1,desc:'Video resolution requested from the console.'},
 {id:'latency',g:0,label:'Latency Mode',type:'choice',opts:LAT,v:2,desc:'Sets the target bitrate. Higher looks better but needs a stronger connection.'},
 {id:'fps',g:0,label:'FPS Target',type:'choice',opts:['30 FPS','60 FPS'],v:0,desc:'Frame rate requested from the console.'},
 {id:'f30',g:0,label:'Force 30 FPS Output',type:'toggle',v:false,desc:'Output video at 30 FPS.'},
 {id:'fill',g:0,label:'Fill Screen',type:'toggle',v:false,desc:'Stretch the video to fill the whole screen.'},
 {id:'disc',g:1,label:'Auto Discovery',type:'toggle',v:true,desc:'Find consoles on your network automatically. Takes effect the next time the app starts.'},
 {id:'psnmode',g:1,label:'Enable PSN Internet Mode',type:'toggle',v:true,desc:'Connect to your consoles over the internet with your PSN account.'},
 {id:'paired',g:1,label:'Show Only Paired',type:'toggle',v:false,desc:'Hide consoles that are not paired.'},
 {id:'lat',g:2,label:'Show Latency',type:'toggle',v:false,desc:'Show latency and frame rate in the stream overlay.'},
 {id:'net',g:2,label:'Show Network Alerts',type:'toggle',v:true,desc:'Show a badge when the connection becomes unstable.'},
 {id:'exit',g:2,label:'Show Exit Shortcut Hint',type:'toggle',v:true,desc:'Show how to leave the stream when it starts.'},
 {id:'blur',g:2,label:'Background Blur',type:'choice',opts:['None','Soft','Strong','Dark'],v:0,desc:'Blur the background waves behind menus. Strong and Dark are softer and calmer.'},
 {id:'cc',g:3,label:'Circle Button Confirm',type:'toggle',v:false,desc:'Use Circle to confirm and Cross to go back, on every screen.'},
 {id:'clamp',g:4,label:'Clamp Soft Restart Bitrate',type:'toggle',v:true,desc:'Limit the bitrate when the stream restarts after packet loss.'},
 {id:'motion',g:4,label:'Motion during loss (artifacts) (Experimental)',type:'toggle',v:false,desc:'Keep motion going while packets are lost. May show visual artifacts.'},
 {id:'log',g:4,label:'Enable Logging',type:'toggle',v:false,desc:'Write diagnostic logs on the Vita for troubleshooting.'}
];
SETTINGS.forEach(s=>s.d=s.v);
const SET=id=>SETTINGS.find(s=>s.id===id);
/* the mock starts with PSN Internet Mode on so internet screens can be shown; the app default is off */
SET('psnmode').d=false;
const setVal=s=>s.type==='choice'?s.opts[s.v]:(s.v?'On':'Off');

/* ---------- PSN auth states (Profile) ---------- */
const PSN_STATES={
 disabled:'Disabled',auth:'Authenticated',refresh:'Refreshing token',await:'Awaiting browser sign-in',
 expired:'Token expired',none:'Not authenticated',error:'Login failed: invalid redirect URL'
};

/* ---------- controller ---------- */
const OUTS=['Options','Share','Touchpad','L1','L2','L3','R1','R2','R3','PS','None'];
const SHORT={Options:'OPT',Share:'SHR',Touchpad:'TP',None:''};
const sh=o=>SHORT[o]!==undefined?SHORT[o]:o;
const mk=(f,b,L1='L1',R1='R1')=>{const fa=Array(18).fill('None'),ba=Array(18).fill('None');Object.entries(f).forEach(([i,o])=>fa[i]=o);Object.entries(b).forEach(([i,o])=>ba[i]=o);return{L1,R1,front:fa,back:ba};};
const seed=()=>{const f={},b={};for(let i=0;i<18;i++){f[i]='Touchpad';b[i]=(i%6<3)?'L2':'R2';}return mk(f,b);};
const MAPS=[seed(),mk({},{0:'L3',5:'R3',6:'L2',11:'R2'},'L2','R2'),seed()];
const PNAME=['Custom 1','Custom 2','Custom 3'],PDESC=['Your first custom mapping','Your second custom mapping','Your third custom mapping'];
const zoneName=i=>String.fromCharCode(65+i%6)+(Math.floor(i/6)+1);
const nz=a=>a.filter(o=>o!=='None').length;

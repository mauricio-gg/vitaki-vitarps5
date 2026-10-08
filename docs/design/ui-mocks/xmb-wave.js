/* Wave background (C27). Five ribbons, time-of-day palette, dust; CPU vertex update at 30 Hz on the Vita. */
let cv,g,reduce=matchMedia('(prefers-reduced-motion: reduce)').matches;
const TOD={night:{a:'#04081a',m:'#0b1634',b:'#16275a',r:[[90,130,255],[60,200,230],[150,110,255]],h:'rgba(70,110,230,.3)'},dawn:{a:'#150d2c',m:'#321a4a',b:'#6a2d5c',r:[[255,140,170],[255,190,110],[190,130,255]],h:'rgba(255,150,130,.35)'},day:{a:'#06204a',m:'#0f4585',b:'#245f9c',r:[[150,205,255],[100,180,240],[190,220,255]],h:'rgba(160,210,255,.28)'},dusk:{a:'#1a0c1c',m:'#4a2018',b:'#a8502a',r:[[255,170,90],[255,120,70],[255,200,140]],h:'rgba(255,150,80,.4)'}};
const todNow=()=>{if(S.tod!=='auto')return S.tod;const h=new Date().getHours();return h<5?'night':h<9?'dawn':h<17?'day':h<21?'dusk':'night';};
const DUST=Array.from({length:36},(_,i)=>{const r=(i*9301+49297)%233280/233280,q=(i*7919+1013)%233280/233280;return{x:r*960,y:q*544,s:.5+((i*37)%10)/12,p:i*1.3};});
function paintRibbons(t){
 const P=TOD[todNow()];
 const gr=g.createLinearGradient(0,0,0,544);gr.addColorStop(0,P.a);gr.addColorStop(.55,P.m);gr.addColorStop(1,P.b);
 g.globalCompositeOperation='source-over';g.fillStyle=gr;g.fillRect(0,0,960,544);
 const hz=g.createRadialGradient(560,520,10,560,520,520);hz.addColorStop(0,P.h);hz.addColorStop(1,'rgba(0,0,0,0)');g.fillStyle=hz;g.fillRect(0,0,960,544);
 g.globalCompositeOperation='lighter';
 for(let i=0;i<5;i++){
  const k=i/4,c=P.r[i%3],amp=40+k*60,sp=(.00014+k*.00022),base=310+i*28-k*20,al=.07+k*.15;
  const top=[],bot=[],mid=[];
  for(let x=-30;x<=990;x+=15){
   const yc=base+Math.sin(x*.0048+t*sp*1.3+i*1.7)*amp+Math.sin(x*.011-t*sp+i)*amp*.28;
   const th=34+k*34+Math.sin(x*.0038+i+t*.0002)*(16+k*20);
   top.push([x,yc-th/2]);bot.push([x,yc+th/2]);mid.push([x,yc+Math.sin(x*.02+t*.0005+i)*th*.1]);
  }
  g.beginPath();top.forEach((p,j)=>j?g.lineTo(p[0],p[1]):g.moveTo(p[0],p[1]));[...bot].reverse().forEach(p=>g.lineTo(p[0],p[1]));g.closePath();
  const lg=g.createLinearGradient(0,0,960,0);lg.addColorStop(0,`rgba(${c},0)`);lg.addColorStop(.3,`rgba(${c},${al})`);lg.addColorStop(.7,`rgba(${c},${al*.9})`);lg.addColorStop(1,`rgba(${c},0)`);
  g.fillStyle=lg;g.fill();
  g.beginPath();mid.forEach((p,j)=>j?g.lineTo(p[0],p[1]):g.moveTo(p[0],p[1]));
  g.lineWidth=1+k;g.strokeStyle=`rgba(255,255,255,${.05+k*.16})`;g.stroke();
 }
 DUST.forEach(d=>{const tw=.3+.3*Math.sin(t*.0012+d.p);g.fillStyle=`rgba(255,255,255,${tw*.5})`;g.beginPath();g.arc((d.x+t*.004*d.s)%960,d.y+Math.sin(t*.0004+d.p)*6,d.s,0,6.3);g.fill();});
}
function loop(t){paintRibbons(t);if(!reduce)setTimeout(()=>requestAnimationFrame(loop),33);}

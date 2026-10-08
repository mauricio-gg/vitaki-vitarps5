/* Wave background (C27). Fixed palette from tokens (--wave-*), five ribbons, 36 dust points; CPU vertex update at 30 Hz on the Vita. */
let cv,g,reduce=matchMedia('(prefers-reduced-motion: reduce)').matches;
const tok=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const rgb=h=>[1,3,5].map(i=>parseInt(h.slice(i,i+2),16));
let P=null;const palette=()=>P||(P={a:tok('--wave-top'),m:tok('--wave-mid'),b:tok('--wave-bot'),r:[tok('--wave-r1'),tok('--wave-r2'),tok('--wave-r3')].map(rgb),h:tok('--wave-horizon')});
const DUST=Array.from({length:36},(_,i)=>{const r=(i*9301+49297)%233280/233280,q=(i*7919+1013)%233280/233280;return{x:r*960,y:q*544,s:.5+((i*37)%10)/12,p:i*1.3};});
function paintRibbons(t){
 const P=palette();
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
/* Background Blur setting (None / Soft / Strong / Dark). The wave is rendered into a small render target and drawn upscaled with bilinear filtering; the upscale is the blur (same on the Vita).
   Soft = 240x136 (1/4). Strong and Dark = 60x34 (1/16). None = today's full-resolution wave. */
const mkc=d=>{const c=document.createElement('canvas');c.width=960/d;c.height=544/d;return c;};
const cv4=mkc(4),cv16=mkc(16);
function viaTarget(t,target,div){const sg=target.getContext('2d'),main=g;g=sg;sg.setTransform(1/div,0,0,1/div,0,0);paintRibbons(t);g=main;return target;}
function blit(src){g.globalCompositeOperation='source-over';g.imageSmoothingEnabled=true;g.imageSmoothingQuality='high';g.drawImage(src,0,0,960,544);}
function paintWave(t){
 const m=S.glass;
 if(m==='soft')blit(viaTarget(t,cv4,4));
 else if(m==='strong'||m==='dark')blit(viaTarget(t,cv16,16));
 else paintRibbons(t);
}
function loop(t){if(!window.__freeze)paintWave(t);if(!reduce)setTimeout(()=>requestAnimationFrame(loop),33);}

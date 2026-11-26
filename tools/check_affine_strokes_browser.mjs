// Optional pixel qualification against a dedicated headless Chrome; no npm dependencies.
// Serve the SDK root on OPXY_PREVIEW_PORT and launch Chrome on OPXY_CHROME_PORT.
import assert from 'node:assert/strict';
const chrome=Number(process.env.OPXY_CHROME_PORT||9339),port=Number(process.env.OPXY_PREVIEW_PORT||8939);
const endpoint=`http://127.0.0.1:${chrome}`;
const policy=(await fetch(`http://127.0.0.1:${port}/`)).headers.get('Content-Security-Policy');
assert(policy?.includes("img-src 'self' data:"));
const target=await(await fetch(`${endpoint}/json/new?about:blank`,{method:'PUT'})).json();
const socket=new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
let serial=0;const pending=new Map();
socket.onmessage=({data})=>{const value=JSON.parse(data);if(value.id){const p=pending.get(value.id);pending.delete(value.id);clearTimeout(p.timer);value.error?p.reject(value.error):p.resolve(value.result);}};
function command(method,params={}){return new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>reject(Error(method+' timeout')),15000);pending.set(id,{resolve,reject,timer});socket.send(JSON.stringify({id,method,params}));});}
try{
 await command('Page.enable');await command('Page.navigate',{url:`http://127.0.0.1:${port}/`});
 await new Promise(resolve=>setTimeout(resolve,300));
 const result=await command('Runtime.evaluate',{awaitPromise:true,returnByValue:true,expression:`(async()=>{
 const {SvgDisplay}=await import('/sdk/browser/display.mjs');
 const asset='<script>throw Error("never execute")</script><path id="a" onclick="throw Error(1)" d="M10 40 L40 40" fill="none" stroke="#ffffff" stroke-width="1.3"/><path id="b" d="M10 40 L40 40" fill="none" stroke="#ffffff" stroke-width="1.3"/><path id="c" d="M0 200 L10 210 Z" fill="#ffffff" fill-opacity="0.5" stroke="#ffffff" stroke-opacity="0.25"/>';
 const renderer=new SvgDisplay(asset,{nodes:['a','b','c'],texts:[],palette:[],paths:0,points:0,affine:1,fullCanvas:1});
 const frame=new Int32Array(renderer.frameWords);
 frame.set([3072,0,0,512,0,0,0,0],renderer.nodesAt);
 frame.set([512,0,0,3072,0,0,0,0],renderer.nodesAt+8);
 frame.set([1024,0,0,1024,0,0,0,0],renderer.nodesAt+16);
 renderer.apply(frame);const drawing=renderer.svg();
 const payload=new DOMParser().parseFromString(drawing,'image/svg+xml').documentElement.getAttribute('href');
 const original=new DOMParser().parseFromString(decodeURIComponent(payload.split(',').slice(1).join(',')),'image/svg+xml');
 const checks=[];
 for(const scale of [1,2]){
  const svg='<svg xmlns="http://www.w3.org/2000/svg" width="'+(480*scale)+'" height="'+(220*scale)+'" viewBox="0 0 480 220">'+drawing+'</svg>';
  const image=new Image();image.src='data:image/svg+xml,'+encodeURIComponent(svg);await image.decode();
  const canvas=document.createElement('canvas');canvas.width=480*scale;canvas.height=220*scale;
  const ctx=canvas.getContext('2d');ctx.drawImage(image,0,0);
  const coverage=(x,y)=>{const pixels=ctx.getImageData(x,y-10,1,21).data;let sum=0;for(let i=3;i<pixels.length;i+=4)sum+=pixels[i]/255;return sum;};
  checks.push({scale,widths:[coverage(60*scale,20*scale),coverage(10*scale,120*scale)]});
 }
 if(original.querySelector('script')||original.querySelector('[onclick]'))throw Error('Unsafe copied asset');
 const alpha=original.querySelector('#c');
 if(alpha.getAttribute('fill-opacity')!=='0.5'||alpha.getAttribute('stroke-opacity')!=='0.25')throw Error('Authored paint opacity lost');
 return {checks,drawingPrefix:drawing.slice(0,60),source:Array.from(original.querySelectorAll('path')).map(n=>({d:n.getAttribute('d'),matrix:n.getAttribute('transform'),effect:n.getAttribute('vector-effect')}))};
})()`});
 if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
 const value=result.result.value;
 assert.equal(value.source.length,3);
 assert(value.source.slice(0,2).every(n=>n.d==='M10 40 L40 40'&&n.effect==='non-scaling-stroke'));
 for(const sample of value.checks)for(const width of sample.widths)assert(Math.abs(width-1.3*sample.scale)<0.12,JSON.stringify(value));
 console.log(JSON.stringify({result:'PASS',...value},null,2));
}finally{socket.close();await fetch(`${endpoint}/json/close/${target.id}`);}

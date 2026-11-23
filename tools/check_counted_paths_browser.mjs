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
try {
 await command('Page.enable');await command('Page.navigate',{url:`http://127.0.0.1:${port}/`});
 await new Promise(resolve=>setTimeout(resolve,300));
 const result=await command('Runtime.evaluate',{awaitPromise:true,returnByValue:true,expression:`(async()=>{
 const {PreviewDisplay,SvgDisplay}=await import('/sdk/browser/display.mjs');
 const {shownControls}=await import('/sdk/browser/controls.mjs');
 const manifest=await(await fetch('/preview/manifest.json')).json();
 const module=await WebAssembly.compile(await(await fetch('/preview/engine.wasm')).arrayBuffer());
 const display=new PreviewDisplay(module),renderer=new SvgDisplay(await(await fetch('/preview/panel.svg')).text(),manifest.graphics);
 const result=[],glyphs=[];
 for(let i=0;i<3;++i){renderer.apply(display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0));
  result.push(renderer.paths.map(path=>(path.getAttribute('d').match(/M/g)||[]).length));
  if(renderer.glyphCount)glyphs.push(renderer.nodes.slice(4,6).map(node=>node.getAttribute('d')));
  const drawing=renderer.svg();const image=new Image();image.src='data:image/svg+xml,'+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220">'+drawing+'</svg>');await image.decode();
 }
 return {counts:result,glyphs,metadata:manifest.graphics,frameBytes:display.frame.byteLength};
})()`});
 if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
 const value=result.result.value;assert.deepEqual(value.counts,[[81,61,49,41],[1,1,1,1],[63,63,63,63]]);
 assert.equal(value.metadata.countedPaths,1);assert.equal(value.frameBytes,value.metadata.glyphs?2920:2852);
 if(value.metadata.glyphs)assert.deepEqual(value.glyphs,[['M3 4 L5 6','M7 8 L9 10'],['M0 0 L1 2',''],['M3 4 L5 6','M7 8 L9 10']]);
 console.log(JSON.stringify({result:'PASS',...value},null,2));
}finally{socket.close();await fetch(`${endpoint}/json/close/${target.id}`);}

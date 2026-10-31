import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay,SvgDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const display=new PreviewDisplay(new WebAssembly.Module(readFileSync(process.argv[2])));
const nodes=Array.from({length:4},()=>({attributes:{},setAttribute(k,v){this.attributes[k]=v;}}));
const renderer=Object.assign(Object.create(SvgDisplay.prototype),{
 points:82,counted:true,quadratic:false,cubic:false,paths:nodes,nodes,texts:[],keys:Array(4).fill(0),applied:Array(4).fill(''),
 shown:[],palette:[],fillable:Array(4).fill(false),strokeable:Array(4).fill(true),rgba:true,affine:true,openPaths:0,segmentPaths:15,
 nodesAt:664,nodeWords:8,textsAt:696,headerAt:705,frameWords:713,
});
for(const counts of [[82,62,50,42],[2,2,2,2],[64,64,64,64]]){
 const frame=display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0);renderer.apply(frame);
 for(let p=0;p<4;++p){const outline=nodes[p].attributes.d;
  assert.equal((outline.match(/M/g)||[]).length,counts[p]-1);
  assert.equal((outline.match(/L/g)||[]).length,counts[p]-1);
  assert(!outline.includes('Z'));assert(outline.startsWith('M0.0009765625 '));
 }
}
const invalid=new Int32Array(display.frame);for(let p=0;p<4;++p)invalid[p*166+1]=82;
assert.throws(()=>renderer.apply(invalid),/budget/);
console.log('PASS: exact82/62/50/42 counts, shrink/grow,256 active cap, no padded segments.');

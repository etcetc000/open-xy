import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay,SvgDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const display=new PreviewDisplay(new WebAssembly.Module(readFileSync(process.argv[2])));
const nodes=Array.from({length:8},()=>({attributes:{stroke:'#112233'},setAttribute(k,v){this.attributes[k]=v;}}));
const renderer=Object.assign(Object.create(SvgDisplay.prototype),{
  points:0,commands:16,quadratic:true,cubic:false,paths:nodes,nodes,texts:[],keys:Array(8).fill(0),applied:Array(8).fill(''),
  shown:[],palette:[],fillable:Array(8).fill(false),strokeable:Array(8).fill(true),rgba:true,affine:true,openPaths:0,segmentPaths:0,
  nodesAt:656,nodeWords:8,textsAt:720,headerAt:729,frameWords:737,
});
for(const count of [15,10,16]){
  const frame=display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0);
  renderer.apply(frame);
  const outline=nodes[0].attributes.d;
  assert.equal((outline.match(/Q/g)||[]).length,count-2);
  assert.equal((outline.match(/M/g)||[]).length,1);
  assert.equal((outline.match(/L/g)||[]).length,1);
  assert(outline.startsWith('M0.0029296875 300.00390625 Q1.0009765625 200.001953125 2.0029296875 300.00390625'));
  assert(!outline.includes('C')&&!outline.includes('Z'));
}
assert.equal(nodes[0].attributes.stroke,'#0c2238');
assert.equal(nodes[0].attributes['vector-effect'],'non-scaling-stroke');
console.log('PASS: compiled M/L/Q frame transitions preserve exact Q10 and remove stale segments.');

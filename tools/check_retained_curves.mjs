import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay,SvgDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const display=new PreviewDisplay(new WebAssembly.Module(readFileSync(process.argv[2])));
const frame=display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0);
const nodes=Array.from({length:17},()=>({attributes:{stroke:'#12ABEF'},setAttribute(k,v){this.attributes[k]=v;}}));
const renderer=Object.assign(Object.create(SvgDisplay.prototype),{
  points:4,paths:nodes,nodes,texts:[],keys:Array(17).fill(0),applied:Array(17).fill(''),shown:[],palette:[],fillable:Array(17).fill(false),
  affine:true,cubic:true,openPaths:0,segmentPaths:0,nodesAt:153,nodeWords:8,textsAt:289,headerAt:298,frameWords:306,
});
renderer.apply(frame);
assert.equal(nodes[0].attributes.d,'M100.0009765625 200.001953125 C100.0009765625 200.001953125 300.0029296875 400.00390625 500.0048828125 600.005859375');
assert.equal(nodes[16].attributes.stroke,'#12ABEF');
assert.equal(nodes[0].attributes.transform,'matrix(1 0 0 1 0 0)');
renderer.cubic=false; renderer.openPaths=1; renderer.keys.fill(0); renderer.apply(frame);
assert(!nodes[0].attributes.d.endsWith(' Z')); assert(nodes[1].attributes.d.endsWith(' Z'));
renderer.segmentPaths=1;renderer.openPaths=0;renderer.keys.fill(0);renderer.apply(frame);
assert.equal((nodes[0].attributes.d.match(/M/g)||[]).length,3);
assert.equal((nodes[0].attributes.d.match(/L/g)||[]).length,3);
assert(!nodes[0].attributes.d.endsWith(' Z'));
console.log('PASS: compiled cubic frame, exact Q10 endpoints/controlpoints, open/closed/separate topology, immutable paint.');

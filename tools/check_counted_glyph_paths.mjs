import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay,SvgDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const display=new PreviewDisplay(new WebAssembly.Module(readFileSync(process.argv[2])));
const nodes=Array.from({length:6},()=>({attributes:{},writes:0,setAttribute(k,v){this.attributes[k]=v;++this.writes;}}));
const renderer=Object.assign(Object.create(SvgDisplay.prototype),{
 points:80,counted:true,quadratic:false,cubic:false,paths:nodes.slice(0,4),nodes,texts:[],keys:Array(4).fill(0),applied:Array(6).fill(''),
 shown:[],palette:[],fillable:Array(6).fill(false),strokeable:Array(6).fill(true),rgba:true,affine:true,openPaths:0,segmentPaths:15,
 extended:true,stride:162,concealed:[],authoredVisibility:[],
 nodesAt:648,nodeWords:8,textsAt:696,headerAt:705,frameWords:714,glyphsAt:713,glyphCount:1,glyphStart:4,glyphDefinitions:2,
 glyphOutlines:['M0 0 L1 2','','M3 4 L5 6','M7 8 L9 10'],glyphApplied:[-1],
});
let frame=display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0);renderer.apply(frame);
assert.equal(nodes[4].attributes.d,'M3 4 L5 6');assert.equal((nodes[0].attributes.d.match(/M/g)||[]).length,79);
const invalid=new Int32Array(frame);invalid[713]=0;for(let p=0;p<4;++p)invalid[p*162+1]=80;
const writes=nodes.map(n=>n.writes);assert.throws(()=>renderer.apply(invalid),/256 points/);assert.deepEqual(nodes.map(n=>n.writes),writes);
frame=display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0);renderer.apply(frame);
assert.equal(nodes[4].attributes.d,'M0 0 L1 2');assert.equal(nodes[5].attributes.d,'');assert.equal((nodes[0].attributes.d.match(/M/g)||[]).length,1);
console.log('PASS: counted geometry and glyph selector tail coexist; invalid counts reject before any glyph mutation.');

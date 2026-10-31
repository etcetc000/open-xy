import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay, SvgDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const module = new WebAssembly.Module(readFileSync(process.argv[2]));
const display = new PreviewDisplay(module);
const frame = display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0,0,false,null,null);
const nodes = Array.from({length:2}, (_,i) => ({attributes:{stroke:'#12AbEf','stroke-width':String(i+2),fill:i?'#334455':'none'},
 setAttribute(name,value){this.attributes[name]=value;}}));
const renderer=Object.assign(Object.create(SvgDisplay.prototype),{
 points:0,paths:[],nodes,texts:[],keys:[],applied:['',''],shown:[],palette:[],fillable:[false,true],strokeable:[true,true],
 affine:true,rgba:true,nodesAt:1,nodeWords:8,textsAt:17,headerAt:26,frameWords:34,
});
renderer.apply(frame);
assert.equal(nodes[0].attributes.stroke,'#12abef');
assert.equal(nodes[0].attributes['stroke-opacity'],String(128/255));
assert.equal(nodes[0].attributes.fill,'none');
assert.equal(nodes[1].attributes.stroke,'#ed4321');
assert.equal(nodes[1].attributes.fill,'#ed4321');
assert.equal(nodes[1].attributes['stroke-opacity'],'0');
assert.equal(nodes[1].attributes['fill-opacity'],'0');
assert.equal(nodes[0].attributes['stroke-width'],'2');
assert.equal(nodes[1].attributes['stroke-width'],'3');
console.log('PASS: real WASM RGBA, alpha extremes, original stroke widths and disabled fill.');

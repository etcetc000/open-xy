import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay,SvgDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const display=new PreviewDisplay(new WebAssembly.Module(readFileSync(process.argv[2])));
const nodes=Array.from({length:4},()=>({attributes:{},writes:0,setAttribute(k,v){this.attributes[k]=v;++this.writes;}}));
const r=Object.assign(Object.create(SvgDisplay.prototype),{points:0,paths:[],nodes,texts:[],keys:[],applied:Array(4).fill(''),shown:[],palette:[],fillable:Array(4).fill(false),strokeable:Array(4).fill(true),rgba:false,affine:true,openPaths:0,segmentPaths:0,
  nodesAt:1,nodeWords:8,textsAt:33,headerAt:42,frameWords:52,glyphsAt:50,glyphCount:2,glyphStart:0,glyphDefinitions:2,
  glyphOutlines:['M0 0 L1 2 Z','M2 3 L4 5','M3 4 C5 6 7 8 9 10 Z',''],glyphApplied:[-1,-1]});
let frame=display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0);
r.apply(frame);assert.equal(nodes[0].attributes.d,r.glyphOutlines[2]);assert.equal(nodes[1].attributes.d,'');
const writes=nodes.map(n=>n.writes);r.apply(frame);assert.deepEqual(nodes.map(n=>n.writes),writes);
frame=display.tick(shownControls(Array(8).fill(0),[0,1,2,3]),17,0);r.apply(frame);
assert.equal(nodes[0].attributes.d,r.glyphOutlines[0]);assert.equal(nodes[1].attributes.d,r.glyphOutlines[1]);assert.equal(nodes[3].attributes.d,'');
const before=nodes.map(n=>n.writes);frame[50]=1;frame[51]=2;assert.throws(()=>r.apply(frame),/glyph selector/);assert.deepEqual(nodes.map(n=>n.writes),before);
console.log('PASS: compiled glyph selectors swap original paths, clear absent parts and reject whole invalid updates.');

// Explicit DOM boundary double exercises the real constructor/source parser.
class Element {
  constructor(name, attrs={}, children=[]){this.localName=name;this.attrs={...attrs};this.children=children;this.textContent='';}
  get attributes(){return Object.entries(this.attrs).map(([name,value])=>({name,value}));}
  get id(){return this.attrs.id;}
  getAttribute(name){return this.attrs[name]??null;}
  setAttribute(name,value){this.attrs[name]=value;}
  append(child){this.children.push(child);}
}
const scaffold='M0,0 '+Array.from({length:15},(_,i)=>`L${i+1},0`).join(' ');
let raw;
function artwork(){
  const paths=['M0 0 L1 2 Z','M2 3 L4 5','M3.0009 4 C5 6 7 8 9 10 Z','M0 0'];
  const sources=paths.map((d,i)=>new Element('path',{id:`font_glyph_${i>>1}_${i&1}`,d,fill:'none',stroke:'#ffffff','stroke-width':'1.3'}));
  const owned=Array.from({length:4},(_,i)=>new Element('path',{id:`font_node_${i}`,d:scaffold,fill:'none',stroke:'#ffffff','stroke-width':'1.3'}));
  raw=new Element('svg',{},[new Element('g',{transform:'translate(1024,1024)'},sources),...owned]);
  return sources;
}
globalThis.DOMParser=class{parseFromString(){return {querySelector(){return null;},documentElement:raw};}};
globalThis.document={createElementNS(ns,name){return new Element(name);}};
const binding={nodes:Array.from({length:4},(_,i)=>`font_node_${i}`),texts:[],palette:[],paths:0,points:0,affine:1,
  glyphs:2,glyphDefinitions:2,glyphStart:0,glyphSources:Array.from({length:4},(_,i)=>`font_glyph_${i>>1}_${i&1}`)};
let sources=artwork();const actual=new SvgDisplay('<g/>',binding);
assert.equal(actual.glyphOutlines[2],'M3 4 C5 6 7 8 9 10 Z');
sources[2].setAttribute('d','M0 0 L999 999');
frame[50]=1;frame[51]=0;actual.apply(frame);assert.equal(actual.nodes[0].getAttribute('d'),'M3 4 C5 6 7 8 9 10 Z');
assert.equal(actual.nodes[1].getAttribute('d'),'');
for(const bad of ['M0 0 Q1 2 3 4','M0..0 1','M0 0 L9000 0','M0 0 Z L1 2']){
  sources=artwork();sources[0].setAttribute('d',bad);assert.throws(()=>new SvgDisplay('<g/>',binding));
}
artwork();raw.children[1].setAttribute('d','M0 0 '+Array(15).fill('L0 0').join(' '));assert.throws(()=>new SvgDisplay('<g/>',binding),/scaffold/);
console.log('PASS: actual browser constructor snapshots bounded Q10 outlines and rejects malformed sources/scaffolds.');

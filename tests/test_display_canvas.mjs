import test from 'node:test';
import assert from 'node:assert/strict';
import {createControls} from '../sdk/browser/controls.mjs';
import {renderScreen} from '../harness/screen.mjs';
const controls=createControls({id:'probe',name:'Probe',parameters:Array.from({length:4},(_,i)=>({
 id:`p${i}`,label:`P${i}`,default:0.5,mapping:{kind:'linear',minimum:0,maximum:1},display:'percent',
})),resources:{display:{fullCanvas:1}},presets:[]});
test('harness renders normalized controls without a resources field',()=>{
 assert.equal(controls.definition.resources,undefined);
 const svg=renderScreen(controls.defaults(),'instrument',null,'<g id="instrument"/>',controls);
 assert.match(svg,/<g id="instrument"/);
 assert.match(svg,/p0/);
});
test('harness full canvas is an explicit manifest capability before preview loading',()=>{
 const svg=renderScreen(controls.defaults(),'instrument',null,'<g id="instrument"/>',controls,[0,1,2,3],undefined,true);
 assert.match(svg,/<g id="instrument"/);
 assert.doesNotMatch(svg,/p0/);
});

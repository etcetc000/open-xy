import {decode64, requireThat, sha256} from './core.mjs';
import * as Retained from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';

// Cache only frozen frames/SVGs: the temporary WASM instance is released after
// one default-state render, and comparison changes never tick machine code.
export function createSnapshotService({compile = bytes => WebAssembly.compile(bytes),
  PreviewDisplay = Retained.PreviewDisplay, SvgDisplay = Retained.SvgDisplay, headerSvg = Retained.headerSvg} = {}) {
  const machineSnapshots = new WeakMap();
  function render(machine, artwork) {
    let cached = machineSnapshots.get(machine);
    if (!cached) {
      const frame = (async () => {
        const wasm = decode64(machine.preview.wasm);
        requireThat(await sha256(wasm) === machine.preview.sha256, 'Preview module hash mismatch');
        const display = new PreviewDisplay(await compile(wasm));
        const parameters = Object.assign(Array(8).fill(0), machine.preview.defaults);
        const frame = display.tick(shownControls(parameters, [0, 1, 2, 3]), 0, 0, 0, true,
          {held_notes:0, note_off_serial:0}).slice();
        return {frame, parameters};
      })();
      cached = {frame, artwork:new Map()}; machineSnapshots.set(machine, cached);
    }
    if (!cached.artwork.has(artwork)) {
      cached.artwork.set(artwork, cached.frame.then(({frame, parameters}) => {
        const renderer = new SvgDisplay(artwork, machine.preview.graphics);
        renderer.apply(frame);
        const header = headerSvg(machine.controls.slice(0, 4).map(title => title.toLowerCase()),
          [0, 1, 2, 3].map(slot => renderer.header[slot] || String(Math.round(parameters[slot] / 0x7fffffff * 100))));
        return '<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220">' +
          '<rect width="480" height="220" fill="#000000"/>' + header + renderer.svg() + '</svg>';
      }));
    }
    return cached.artwork.get(artwork);
  }
  return {render};
}

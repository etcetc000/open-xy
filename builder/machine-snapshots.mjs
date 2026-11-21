import {decode64, kitChoices, kitHeader, kitKey, kitMix, kitModel, kitWord, requireThat, sha256} from './core.mjs';
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
        const shown = [0, 1, 2, 3], kit = machine.preview.kit;
        if (kit) {
          // A drum kit: its first key as the kit sets it up, the key a new track selects.
          const key = kitKey(display.api, kit, 53), names = kitModel(display.api, kit, key.model);
          const parameters = key.controls.map(kitWord);
          const frame = display.tick(shownControls(parameters, shown), 0, 0, 0, true,
            {held_notes:0, note_off_serial:0}, key.model, 0, kitChoices(kit, key.model), null, kitMix(kit), 0).slice();
          const [titles, values] = kitHeader(names, key.controls, shown);
          return {frame, titles, values};
        }
        const parameters = Object.assign(Array(8).fill(0), machine.preview.defaults);
        // A synth's Shift row (SDK 1.13) at its defaults, not held.
        const row = machine.preview.shift ? [0, 0, [0, 0, 0, 0], null, {pan:0, level:0}, 0,
          {held:0, line:0, values:machine.preview.shift}] : [];
        const frame = display.tick(shownControls(parameters, shown), 0, 0, 0, true,
          {held_notes:0, note_off_serial:0}, ...row).slice();
        return {frame, titles:machine.controls.slice(0, 4).map(title => title.toLowerCase()),
          values:shown.map(slot => String(Math.round(parameters[slot] / 0x7fffffff * 100)))};
      })();
      cached = {frame, artwork:new Map()}; machineSnapshots.set(machine, cached);
    }
    if (!cached.artwork.has(artwork)) {
      cached.artwork.set(artwork, cached.frame.then(({frame, titles, values}) => {
        const renderer = new SvgDisplay(artwork, machine.preview.graphics);
        renderer.apply(frame);
        // The machine's header text, or the stock header's value.
        const header = headerSvg(titles, values.map((value, slot) => renderer.header[slot] || value));
        return '<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220">' +
          '<rect width="480" height="220" fill="#000000"/>' + header + renderer.svg() + '</svg>';
      }));
    }
    return cached.artwork.get(artwork);
  }
  return {render};
}

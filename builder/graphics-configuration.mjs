import {graphicsForMachine} from './core.mjs';

const valid = mode => mode === 'grayscale' || mode === 'colorful';

// Appearance choices outlive the current selection; only selected custom screens
// contribute to the checkbox, summary and packaged overrides.
export function createGraphicsConfiguration() {
  let graphics = 'colorful';
  const graphicsOverrides = {};
  const modeFor = machine => graphicsForMachine(machine, {graphics, graphicsOverrides});
  return {
    modeFor,
    summary(machines) {
      const custom = machines.filter(machine => machine.artwork);
      return {enabled:custom.some(machine => modeFor(machine) === 'grayscale'),
        customized:custom.filter(machine => Object.hasOwn(graphicsOverrides, machine.id)).length};
    },
    setMachine(machine, mode) {
      if (!machine.artwork || !valid(mode)) return false;
      const before = graphicsOverrides[machine.id];
      if (mode === graphics) delete graphicsOverrides[machine.id]; else graphicsOverrides[machine.id] = mode;
      return before !== graphicsOverrides[machine.id];
    },
    setAll(machines, mode) {
      const custom = machines.filter(machine => machine.artwork);
      if (!valid(mode) || !custom.length) return false;
      let changed = graphics !== mode;
      graphics = mode;
      for (const machine of custom) {
        if (Object.hasOwn(graphicsOverrides, machine.id)) { delete graphicsOverrides[machine.id]; changed = true; }
      }
      return changed;
    },
    settings(machines) {
      const overrides = Object.fromEntries(machines.filter(machine => machine.artwork && Object.hasOwn(graphicsOverrides, machine.id))
        .map(machine => [machine.id, graphicsOverrides[machine.id]]));
      return {graphics, ...(Object.keys(overrides).length ? {graphicsOverrides:overrides} : {})};
    },
  };
}

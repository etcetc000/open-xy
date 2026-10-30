import { headerSvg } from '../sdk/browser/display.mjs';

// The device page, 480 x 220 as on the OP-XY: the stock four-control header
// under the machine's own drawing, which can cover it, and above the
// harness's waveform view. Each of the four positions shows the parameter
// shown[i] (its own or an alternate); header holds the display's own text for
// a position, or '' for the formatted value.
export function renderScreen(values, view, samples = null, graphics = '', controls, shown = [0, 1, 2, 3], header = ['', '', '', '']) {
  const selected = controls;
  const title = selected.definition.name;
  let drawing;
  if (view === 'waveform') {
    const points = Array.from({ length: 160 }, (_, i) => {
      const sample = samples ? samples[Math.floor(i * samples.length / 160)] : 0;
      return `${24 + i * 432 / 159},${120 - sample * 160}`;
    }).join(' ');
    drawing = `<path d="M24 120H456" stroke="#454949"/><polyline points="${points}" fill="none" stroke="white" stroke-width="1.5"/>`;
  } else {
    // All instrument geometry, paint and animation arrive from shared C++.
    drawing = graphics || '<text x="240" y="120" text-anchor="middle" font-size="12" fill="#e9eeee" font-family="monospace">Build and load the C++ preview to see the instrument.</text>';
  }
  const parameters = shown.map(slot => selected.parameters[slot]);
  const titles = parameters.map(p => p.label.toLowerCase());
  const shownValues = parameters.map((p, i) => header[i] || selected.displayValue(p.id, values));
  return `<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220" role="img" aria-label="${title} ${view} screen">
    <rect width="480" height="220" fill="#000000"/>
    ${view === 'waveform' ? drawing + headerSvg(titles, shownValues) : headerSvg(titles, shownValues) + drawing}
    </svg>`;
}

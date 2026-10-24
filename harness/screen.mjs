// This drawing is an original screen concept, not a device UI emulator.
// Each of the four positions shows the parameter shown[i] (its own or an alternate).
// header holds the display's own text for each position, or '' for the value.
export function renderScreen(values, view, samples = null, graphics = '', controls, shown = [0, 1, 2, 3], header = ['', '', '', '']) {
  const selected = controls;
  const title = selected.definition.name;
  let drawing;
  if (view === 'waveform') {
    const points = Array.from({ length: 160 }, (_, i) => {
      const sample = samples ? samples[Math.floor(i * samples.length / 160)] : 0;
      return `${24 + i * 432 / 159},${101 - sample * 160}`;
    }).join(' ');
    drawing = `<path d="M24 101H456" stroke="#454949"/><polyline points="${points}" fill="none" stroke="white" stroke-width="1.5"/>`;
  } else {
    // Only the browser page's header/footer placement differs from stock.
    // All instrument geometry, paint and animation arrive from shared C++.
    // The device screen (480 x 220, header above y = 20) fits between the rules.
    drawing = graphics ? `<g transform="translate(60 20) scale(.75)">${graphics}</g>`
      : '<text x="240" y="110" text-anchor="middle" font-size="12">Build and load the C++ preview to see the instrument.</text>';
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" width="480" height="224" viewBox="0 0 480 224" role="img" aria-label="${title} ${view} screen concept">
    <rect width="480" height="224" fill="#111515"/>
    <g font-family="monospace" fill="#e9eeee"><text x="20" y="29" font-size="19" font-weight="bold">${title}</text>
    <text x="460" y="28" text-anchor="end" font-size="10">${view === 'routing' ? 'SIGNAL / 01' : 'SCOPE / 02'}</text>
    <path d="M20 39H460M20 170H460" stroke="#454949"/>
    ${drawing}
    ${shown.map(slot => selected.parameters[slot]).map((p, i) => `<text x="${20 + i * 115}" y="190" font-size="10" fill="#a0aaa7">${p.label.toUpperCase()}</text><text x="${20 + i * 115}" y="212" font-size="16">${header[i] || selected.displayValue(p.id, values)}</text>`).join('')}
    </g></svg>`;
}

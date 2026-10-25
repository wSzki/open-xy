// This drawing is an original screen concept, not a device UI emulator.
// Each of the four positions shows the parameter shown[i] (its own or an alternate).
export function renderScreen(values, view, samples = null, graphics = '', controls, segmented = false, shown = [0, 1, 2, 3]) {
  const selected = controls;
  const rings = selected.definition.id === 'rings';
  const title = selected.definition.name;
  let drawing;
  if (view === 'waveform') {
    const points = Array.from({ length: 160 }, (_, i) => {
      const sample = samples ? samples[Math.floor(i * samples.length / 160)] : 0;
      return `${24 + i * 432 / 159},${(rings ? 128 : 101) - sample * (rings ? 88 : 160)}`;
    }).join(' ');
    drawing = rings
      ? `<path d="M24 128H456" stroke="#454949"/><polyline points="${points}" fill="none" stroke="white" stroke-width="1.5"/>`
      : `<path d="M24 101H456" stroke="#454949"/><polyline points="${points}" fill="none" stroke="white" stroke-width="1.5"/>`;
  } else {
    // Only the browser page's header/footer placement differs from stock.
    // All instrument geometry, paint and animation arrive from shared C++.
    // Native y=120 maps to y=122, the centre of the visible 20–224 body.
    const transform = rings ? 'translate(0 16.4) scale(1 .88)'
      : segmented ? 'translate(60 20) scale(.75)' : 'translate(0 -15)';
    drawing = graphics ? `<g transform="${transform}">${graphics}</g>`
      : '<text x="240" y="110" text-anchor="middle" font-size="12">Build and load the C++ preview to see the instrument.</text>';
  }
  // Rings shows the stock stepped header: four titles above four values on a
  // 20 px greyscale bar, light text on the dark half and dark on the light half.
  const steps = ['#000000','#323232','#4B4B4B','#646464','#7D7D7D','#969696','#AFAFAF','#CDCDCD']
    .map((fill, i) => `<rect x="${i * 60}" width="60" height="20" fill="${fill}"/>`).join('');
  const header = shown.map(slot => selected.parameters[slot]).map((p, i) => {
    const ink = i < 2 ? '#CBCBCB' : '#000000';
    return `<text x="${5 + i * 120}" y="10" font-size="9" fill="${ink}">${p.label.toUpperCase()}</text>` +
      `<text x="${62 + i * 120}" y="17.4" font-size="15" fill="${ink}">${selected.displayValue(p.id, values)}</text>`;
  }).join('');
  return `<svg xmlns="http://www.w3.org/2000/svg" width="480" height="224" viewBox="0 0 480 224" role="img" aria-label="${title} ${view} screen concept">
    <rect width="480" height="224" fill="#111515"/>
    ${rings ? `<g font-family="monospace">${steps}${header}</g><defs><clipPath id="rings-body-clip"><rect x="0" y="24" width="480" height="200"/></clipPath></defs>`
      : `<g font-family="monospace" fill="#e9eeee"><text x="20" y="29" font-size="19" font-weight="bold">${title}</text>
    <text x="460" y="28" text-anchor="end" font-size="10">${view === 'routing' ? 'SIGNAL / 01' : 'SCOPE / 02'}</text>
    <path d="M20 39H460M20 170H460" stroke="#454949"/>`}
    ${rings ? `<g font-family="monospace" clip-path="url(#rings-body-clip)">${drawing}</g>`
      : `${drawing}
    ${shown.map(slot => selected.parameters[slot]).map((p, i) => `<text x="${20 + i * 115}" y="190" font-size="10" fill="#a0aaa7">${p.label.toUpperCase()}</text><text x="${20 + i * 115}" y="212" font-size="16">${selected.displayValue(p.id, values)}</text>`).join('')}</g>`}
    </svg>`;
}

// Original instructional drawings, based on the OP-XY hardware layout:
// https://teenage.engineering/guides/op-xy/layout
// Menu illustrations show the option to look for, never a live device readout.
const ns = 'http://www.w3.org/2000/svg';
const paths = {
  firmware: 'M10 3h15l10 10v31H10z M25 3v11h10 M16 24h13 M16 31h13',
  machines: 'M5 7h12v12H5z M26 7h12v12H26z M5 28h12v12H5z M26 28h12v12H26z',
  settings: 'M18 4h8l1 6 5 3 6-2 4 7-5 4v5l5 4-4 7-6-2-5 3-1 6h-8l-1-6-5-3-6 2-4-7 5-4v-5l-5-4 4-7 6 2 5-3z M29 25a7 7 0 1 0-14 0 7 7 0 0 0 14 0',
  controls: 'M8 10h30 M8 24h30 M8 38h30 M17 5v10 M29 19v10 M20 33v10',
  install: 'M22 3v26 M13 20l9 9 9-9 M6 33v10h32V33',
  synth: 'M2 24c8-27 13 27 21 0s13 27 21 0',
  effect: 'M3 34h10V24h10V14h10v20h10',
  sample: 'M35 14a16 16 0 1 0 0 20 M35 4v11H24',
};
// Transcribed from the user's four bootloader photographs. The variable build
// stamp is omitted; these drawings describe menus, not detected device state.
const FACTORY = ['1. factory reset', '2. mtp file transfer (mac)', '3. mtp file transfer (windows)', '4. reset vol calibration', '5. write os to boot partition', '6. reset', '', '8. back'];
const bootMenus = {
  boot: {title: 'TE BOOT', rows: ['1. upload firmware', '3. dev menu', '5. format drive', '', '7. factory reset', '8. system menu']},
  system: {title: 'system menu', rows: ['1. function test', '2. reset volume pot calibration', '', '8. back']},
  factory: {title: 'factory menu', rows: FACTORY},
  // Writing the OS adds status lines below the factory menu.
  writing: {title: 'factory menu', rows: [...FACTORY, 'looking for /os.ldr']},
  written: {title: 'factory menu', rows: [...FACTORY, 'looking for /os.ldr', 'firmware written ok!']},
  mtp: {title: 'mtp mode active', rows: ['', '2. exit']},
};
// `highlight` outlines a status line, as `selected` outlines a numbered option.
function screenMarkup(name, selected, devMenu = true, highlight = null) {
  const model = bootMenus[name];
  let markup = '<rect class="lcd-background" width="480" height="222"/>';
  markup += `<text class="lcd-text lcd-title" x="12" y="22">${model.title}</text>`;
  model.rows.forEach((line, index) => {
    if (name === 'boot' && line.startsWith('3.') && !devMenu) return;
    const y = 50 + index * 17;
    const active = (selected && line.startsWith(selected + '.')) || (highlight && line === highlight);
    if (active) markup += `<rect class="lcd-highlight instruction-pulse" x="7" y="${y - 13}" width="466" height="17" rx="2"/>`;
    markup += `<text class="lcd-text${active ? ' lcd-selected' : ''}" x="12" y="${y}">${line}</text>`;
  });
  return markup;
}
function svg(markup, viewBox, label) {
  const el = document.createElementNS(ns, 'svg');
  el.setAttribute('viewBox', viewBox);
  if (label) { el.setAttribute('role', 'img'); el.setAttribute('aria-label', label); }
  else el.setAttribute('aria-hidden', 'true');
  el.innerHTML = markup; // Only authored drawing constants enter this helper.
  return el;
}
// A machine may supply its own glyph path; otherwise its kind's glyph is used.
export function icon(kind, path = '') {
  return svg(`<path d="${path || paths[kind] || paths.firmware}" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>`, '0 0 46 48');
}
// Restart only the presentation; navigation and focus never wait on animation.
export function reveal(container) {
  container.classList.remove('step-enter');
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  void container.offsetWidth;
  container.classList.add('step-enter');
}
export function device({com = false, track = null, screen = null, cable = false, devMenu = true} = {}) {
  const key = (x, y, text = '', active = false, fill = '') => `<g class="device-key${active ? ' is-lit' : ''}">${active ? `<rect class="key-halo instruction-pulse" x="${x - 3}" y="${y - 3}" width="48" height="48" rx="8"/>` : ''}<rect class="key-face" x="${x}" y="${y}" width="42" height="42" rx="5"${fill ? ` style="fill:${fill}"` : ''}/><circle cx="${x + 21}" cy="${y + 21}" r="13"/><text x="${x + 21}" y="${y + 25}">${text}</text></g>`;
  let drawing = '<rect class="device-body" x="40" y="100" width="820" height="294" rx="16"/>';
  // Speaker grille, volume, project/tempo keys and the 480:222 screen.
  drawing += '<rect class="device-inset" x="53" y="113" width="86" height="86" rx="5"/>';
  for (let row = 0; row < 9; row++) for (let col = 0; col < 9; col++) {
    if ((col - 4) ** 2 + (row - 4) ** 2 < 22) drawing += `<circle class="speaker-hole" cx="${64 + col * 8}" cy="${124 + row * 8}" r="1.3"/>`;
  }
  drawing += '<circle class="device-knob" cx="162" cy="134" r="16"/><circle class="knob-ring" cx="162" cy="134" r="11"/>';
  drawing += key(141, 157, '◇') + key(185, 157, '♩');
  drawing += '<rect class="device-screen" x="229" y="113" width="174" height="86" rx="4"/>';
  if (screen) drawing += `<svg x="234" y="118" width="164" height="76" viewBox="0 0 480 222">${screenMarkup(screen, track, devMenu)}</svg>`;
  for (let i = 0; i < 4; i++) {
    const x = 449 + i * 88;
    drawing += `<circle class="device-knob" cx="${x}" cy="157" r="25" style="fill:${['#343434', '#696969', '#a1a1a1', '#dfdfdf'][i]}"/><circle class="knob-ring" cx="${x}" cy="157" r="17"/><path d="M${x} 132v9" stroke="#111" stroke-width="2"/>`;
  }
  drawing += key(757, 113, '≋') + key(757, 157, 'com', com);
  // Four modes, four modules, eight tracks, then PLAYERS.
  for (let i = 0; i < 17; i++) drawing += key(53 + i * 44, 201, i < 4 ? ['∿','↔','▤','☷'][i] : i < 8 ? String(i - 3) : i < 16 ? String(i - 7) : '···', track !== null && i === track + 7);
  // Sixteen sequencer keys fade from dark to light on the actual instrument.
  for (let i = 0; i < 17; i++) drawing += key(53 + i * 44, 245, i === 16 ? 'bar' : '', false, i === 16 ? '' : `rgb(${40 + i * 7},${40 + i * 7},${40 + i * 7})`);
  const blackKeys = new Set([1,2,3,5,6,8,9,10,12,13]);
  let number = 0;
  for (let i = 0; i < 17; i++) {
    if (i < 3) drawing += key(53 + i * 44, 289, ['●','▶','■'][i]);
    else if (blackKeys.has(i - 3)) drawing += key(53 + i * 44, 289, String(++number % 10));
    drawing += key(53 + i * 44, 333, i < 3 ? ['−','+','shift'][i] : '');
  }
  drawing += '<rect class="device-edge" x="98" y="394" width="91" height="3"/>';
  if (com) drawing += '<g class="instruction-cue"><path class="callout" d="M778 157V53H660"/><circle class="callout-dot" cx="778" cy="157" r="3"/><text class="callout-label" x="650" y="48" text-anchor="end">Hold COM</text><text class="callout-note" x="650" y="70" text-anchor="end">while switching on</text></g>';
  if (track !== null) {
    const x = 74 + (track + 7) * 44;
    drawing += `<path class="callout" d="M${x} 201V63H${x - 85}"/><circle class="callout-dot" cx="${x}" cy="201" r="3"/><text class="callout-label" x="${x - 95}" y="68" text-anchor="end">Track ${track}</text>`;
  }
  if (cable) drawing += '<path class="usb-cable" d="M866 302h25v-74h-20"/><rect class="usb-plug" x="856" y="296" width="10" height="12" rx="3"/><text class="callout-note" x="890" y="210" text-anchor="end">USB</text>';
  return svg(drawing, '0 0 910 430', 'Device controls: speaker and volume at left, screen, four encoders, sequencer and two-octave keyboard' + (com ? '. Hold COM at the top right while switching on.' : track !== null ? `. Track ${track} is highlighted below the encoders.` : cable ? '. Connect USB to the computer.' : '.'));
}
function element(tag, text, className) {
  const el = document.createElement(tag);
  if (text) el.textContent = text;
  if (className) el.className = className;
  return el;
}
function bootGuide(name, track, caption, {com = false, devMenu = true, highlight = null} = {}) {
  const figure = element('figure', '', 'boot-guide');
  const closeup = element('div', '', 'lcd-closeup');
  const rows = bootMenus[name].rows.filter(line => line && (name !== 'boot' || devMenu || !line.startsWith('3.')));
  closeup.append(svg(screenMarkup(name, track, devMenu, highlight), '0 0 480 222', bootMenus[name].title + '. ' + rows.join('. ')));
  const hardware = element('div', '', 'boot-hardware');
  hardware.append(device({screen: name, track, com, devMenu}));
  const hint = element('figcaption');
  if (track !== null || com) hint.append(element('kbd', com ? 'com' : String(track)));
  hint.append(element('span', caption));
  figure.append(hardware, closeup, hint);
  return figure;
}
function tree(title, files) {
  const section = element('div', '', 'copy-tree');
  section.append(icon('firmware'), element('h3', title));
  for (const file of files) section.append(element('div', file, 'copy-file'));
  return section;
}
export function fileMap(install, files, direct = false) {
  const figure = element('figure', '', 'copy-guide');
  const arrow = element('span', '→', 'copy-arrow'); arrow.setAttribute('aria-hidden', 'true');
  figure.append(tree(install ? (direct ? 'This build' : 'Extracted ZIP') : 'OP–XY', files), arrow,
    tree(install ? 'OP–XY' : 'This computer', install ? files : files.map(file => file.split('/').at(-1))));
  return figure;
}
export function bundle(files) {
  return tree('Your build', files);
}
export function guide(stage, {install, computer, files, disabling = false}) {
  if (stage === 'boot') {
    const figure = element('figure', '', 'device-guide-figure');
    const caption = element('figcaption');
    caption.className = 'instruction-cue';
    caption.append(element('kbd', 'com'), element('span', 'Hold COM while switching on'));
    figure.append(device({com: true}), caption);
    return figure;
  }
  if (['menu', 'menu-help'].includes(stage)) return bootGuide('boot', 3, 'Look for “3. dev menu”. It may be missing on your device.');
  if (stage === 'confirm') return bootGuide('boot', null, 'Check your device’s screen for “3. dev menu”.');
  // Steps that press a track show only the current screen, with that track lit.
  const press = (screen, track, options) => bootGuide(screen, track, `Press track ${track}.`, options);
  if (stage === 'refresh-open') return press('boot', 8, {devMenu: disabling});
  if (stage === 'refresh-return') return press('system', 8);
  if (stage === 'open-dev-menu') return press('boot', 3);
  if (stage === 'start-transfer') {
    const key = computer === 'mac' ? 2 : 3;
    return press('factory', key);
  }
  if (stage === 'close-field-kit') return bootGuide('mtp', null, 'Your OP–XY stays in file transfer mode.');
  if (stage === 'transfer') return bootGuide('mtp', null, 'Keep file transfer open until copying finishes.');
  if (stage === 'exit-transfer') return press('mtp', 2);
  if (stage === 'write-os') return press('factory', 5);
  if (stage === 'write-result') return bootGuide('written', null, 'Once “firmware written ok!” appears, it’s safe to power cycle.', {highlight: 'firmware written ok!'});
  if (stage === 'write-help') return bootGuide('writing', null, '“looking for /os.ldr” alone doesn’t confirm a completed write.', {highlight: 'looking for /os.ldr'});
  return device({cable: ['enable', 'computer', 'missing-os'].includes(stage)});
}

// Illustrative machine screens, authored here rather than taken from a catalog
// machine. Keep their paint in SVG so the installation converter can process it.
export function graphicsExampleArtwork() {
  const text = (x, y, value, fill = '#b8c4d0', size = 10) =>
    `<text x="${x}" y="${y}" fill="${fill}" font-family="sans-serif" font-size="${size}">${value}</text>`;
  const header = (names, values) => Array.from({length:4}, (_, i) => {
    const x = i * 120, ink = i < 2 ? '#dddddd' : '#111111';
    return `<rect x="${x}" width="120" height="20" fill="${['#242424','#555555','#aaaaaa','#dddddd'][i]}"/>` +
      text(x + 5, 10, names[i], ink, 9) + text(x + 65, 17, values[i], ink, 18);
  }).join('');
  const wave = (id, y, phase, color) => {
    const points = Array.from({length:81}, (_, i) => {
      const x = 22 + i * 4;
      return `${i ? 'L' : 'M'}${x} ${(y + Math.sin(i / 80 * Math.PI * 6 + phase) * 22).toFixed(2)}`;
    }).join(' ');
    return `<path id="${id}" d="${points}" fill="none" stroke="${color}" stroke-width="2.5"/>`;
  };
  // The same three authored tone roles identify traces, labels and meters.
  const waveformTones = {a:'#f0f0f0', b:'#999999', mix:'#c8c8c8'};
  const spectrum = `<g><rect id="waveform_background" width="480" height="220" fill="#000000"/>` +
    header(['shape','detune','blend','level'], ['sine','7','42','80']) +
    text(22, 43, 'TWIN OSCILLATOR', '#c8c8c8', 11) +
    [77, 111, 145, 179].map((y, i) => `<path id="waveform_grid_${i}" d="M22 ${y} H342" fill="none" stroke="#303030" stroke-width="1"/>`).join('') +
    wave('waveform_osc_a', 96, 0, waveformTones.a) + wave('waveform_osc_b', 153, 1.3, waveformTones.b) +
    text(7, 100, 'A', waveformTones.a, 11) + text(7, 157, 'B', waveformTones.b, 11) +
    ['a', 'b', 'mix'].map((role, i) => {
      const x = 370 + i * 29, height = [74, 116, 91][i];
      return `<rect id="waveform_meter_${role}" x="${x}" y="${181 - height}" width="18" height="${height}" fill="${waveformTones[role]}"/>` +
        text(x + (role === 'mix' ? -1 : 6), 195, role.toUpperCase(), waveformTones[role], 11);
    }).join('') +
    text(22, 213, 'OSC A', waveformTones.a, 11) + text(108, 213, 'OSC B', waveformTones.b, 11) + text(370, 213, 'OUTPUT', '#c8c8c8', 11) + '</g>';
  const heights = [40, 64, 91, 113, 54, 78, 102, 67, 119, 49, 85, 38, 105, 72, 94, 56];
  // Four semantic groups: distinct hues AND brightness, with neutral labels and
  // tracks so decoration does not consume the converter's hue-separation budget.
  const sequenceColors = ['#3b8b78','#79a9e0','#d4b7ee','#ffd3bf'];
  const sequence = `<g><rect width="480" height="220" fill="#000000"/>` +
    header(['rate','length','swing','level'], ['1/16','16','12','75']) +
    text(20, 44, 'STEP SEQUENCER', '#c8c8c8', 11) + text(363, 44, 'PATTERN 01', '#c8c8c8') +
    sequenceColors.map((_, i) => text(20 + i * 112, 62, 'GROUP ' + 'ABCD'[i], '#c8c8c8', 11)).join('') +
    heights.map((originalHeight, i) => {
      const x = 20 + i * 28, height = Math.round(originalHeight * .88), color = sequenceColors[Math.floor(i / 4)];
      return `<rect id="sequence_track_${i + 1}" x="${x}" y="72" width="19" height="111" fill="#101010"/>` +
        `<rect id="sequence_step_${i + 1}" x="${x}" y="${183 - height}" width="19" height="${height}" fill="${color}"/>` +
        text(x + 3, 201, String(i + 1).padStart(2, '0'), '#c8c8c8', 11);
    }).join('') + '<rect id="sequence_current_step" x="101" y="69" width="25" height="117" fill="none" stroke="#ffffff" stroke-width="1.5"/></g>';
  return [{name:'Waveform', artwork:spectrum}, {name:'Step sequence', artwork:sequence}];
}

export function graphicsExamples(convert) {
  let position = 50, animation = null, destination = 50;
  const examples = graphicsExampleArtwork().map(({name, artwork}) => {
    const figure = element('figure', '', 'comparison-figure');
    const grayscale = convert(artwork, 'grayscale');
    const caption = element('figcaption', name + (grayscale === artwork ? ' · grayscale by design' : ''));
    const frame = element('div', '', 'comparison-screen');
    const before = element('div', '', 'comparison-layer');
    const after = element('div', '', 'comparison-layer comparison-after');
    before.append(svg(artwork, '0 0 480 220'));
    after.append(svg(grayscale, '0 0 480 220'));
    const divider = element('div', '', 'comparison-divider');
    divider.setAttribute('aria-hidden', 'true');
    const handle = element('span', '', 'comparison-handle');
    handle.append(svg('<path d="M7 5 2 10l5 5m6-10 5 5-5 5" fill="none" stroke="#ffffff" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>', '0 0 20 20'));
    divider.append(handle);
    const range = element('input', '', 'comparison-range');
    range.type = 'range'; range.min = '0'; range.max = '100'; range.step = '1'; range.value = '50';
    range.setAttribute('aria-label', name + ' color and grayscale comparison');
    const render = value => {
      range.value = String(Math.round(value));
      frame.style.setProperty('--split', value + '%');
      range.setAttribute('aria-valuetext', `${range.value}% original color, ${100 - Number(range.value)}% grayscale`);
    };
    // Either control takes over the shared position and animation immediately.
    range.onpointerdown = stop;
    range.oninput = () => { stop(); update(Number(range.value)); };
    render(position);
    frame.append(before, after, divider, range);
    figure.append(caption, frame);
    return {element:figure, render};
  });
  function update(value) {
    position = Math.min(100, Math.max(0, value));
    examples.forEach(example => example.render(position));
  }
  function stop() {
    if (animation !== null) cancelAnimationFrame(animation);
    animation = null;
  }
  function move(target) {
    if (animation !== null && destination === target) return;
    stop(); destination = target;
    if (position === target || matchMedia('(prefers-reduced-motion: reduce)').matches) {
      update(target); return;
    }
    const from = position, started = performance.now();
    const tick = now => {
      const progress = Math.min(1, Math.max(0, (now - started) / 420));
      const eased = progress === 1 ? 1 : 1 - 2 ** (-10 * progress);
      update(from + (target - from) * eased);
      animation = progress < 1 ? requestAnimationFrame(tick) : null;
    };
    animation = requestAnimationFrame(tick);
  }
  return {
    elements:examples.map(example => example.element),
    setGraphics(mode) {
      move(mode === 'grayscale' ? 0 : 100);
    },
  };
}

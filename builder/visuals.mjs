// Original instructional drawings, based on the OP-XY hardware layout:
// https://teenage.engineering/guides/op-xy/layout
// Menu illustrations show the option to look for, never a live device readout.
const ns = 'http://www.w3.org/2000/svg';
const paths = {
  firmware: 'M10 3h15l10 10v31H10z M25 3v11h10 M16 24h13 M16 31h13',
  machines: 'M5 7h12v12H5z M26 7h12v12H26z M5 28h12v12H5z M26 28h12v12H26z',
  install: 'M22 3v26 M13 20l9 9 9-9 M6 33v10h32V33',
  synth: 'M2 24c8-27 13 27 21 0s13 27 21 0',
  effect: 'M3 34h10V24h10V14h10v20h10',
  sample: 'M35 14a16 16 0 1 0 0 20 M35 4v11H24',
  step: 'M5 34h8V22h8v12h8V14h8v20h4 M5 42h36',
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

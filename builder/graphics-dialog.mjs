import {selectArtwork} from '../sdk/browser/graphics.mjs';

// Owns card DOM, async stale-result guards and object URLs; appearance rules and
// snapshot rendering are supplied by the caller.
export function createGraphicsDialog({elements, configuration, snapshots, onChange = () => {}, beforeOpen = () => {},
  document = globalThis.document, urlApi = globalThis.URL}) {
  const $ = id => elements[id];
  const node = (tag, text, className) => {
    const element = document.createElement(tag);
    if (text !== undefined) element.textContent = text;
    if (className) element.className = className;
    return element;
  };
  let rendered = Promise.resolve();
  function createGraphicsOverride(machine, refresh) {
    const label = node('label', undefined, 'machine-graphics-control');
    label.append(node('span', 'Screen appearance'));
    const select = node('select'); select.setAttribute('aria-label', machine.name + ' screen appearance');
    for (const [value, text] of [['grayscale', 'Enforce grayscale'], ['colorful', 'Keep original design']]) {
      const option = node('option', text); option.value = value; select.append(option);
    }
    select.value = configuration.modeFor(machine);
    select.onchange = () => {
      if (!configuration.setMachine(machine, select.value)) return;
      onChange(machine); refresh();
    };
    label.append(select); return label;
  }
  let selectedPreviewRevision = 0, disposeSelectedPreviews = () => {};
  function clearSelectedPreviews() {
    ++selectedPreviewRevision;
    disposeSelectedPreviews();
    disposeSelectedPreviews = () => {};
    $('selected-previews-list').replaceChildren();
  }
  $('selected-previews').addEventListener('close', clearSelectedPreviews);
  $('close-selected-previews').onclick = () => $('selected-previews').close();
  const open = ({machines, screenTemplate}) => {
    if (!machines.length) return;
    beforeOpen(); clearSelectedPreviews();
    const dialog = $('selected-previews'), ticket = selectedPreviewRevision;
    $('selected-previews-count').textContent = `${machines.length} ${machines.length === 1 ? 'machine' : 'machines'} selected`;
    const updates = [], controls = [], urls = new Set();
    const allGrayscale = $('graphics-all-grayscale'), allOriginal = $('graphics-all-original');
    const updatePreviews = () => {
      rendered = Promise.allSettled(updates.map(update => update()));
    };
    const applyAll = mode => {
      if (!controls.length) return;
      if (configuration.setAll(machines, mode)) onChange();
      for (const {select} of controls) select.value = mode;
      updatePreviews();
    };
    allGrayscale.disabled = allOriginal.disabled = !machines.some(machine => machine.artwork);
    allGrayscale.onclick = () => applyAll('grayscale');
    allOriginal.onclick = () => applyAll('colorful');
    updatePreviews();
    disposeSelectedPreviews = () => {
      urls.forEach(url => urlApi.revokeObjectURL(url));
      allGrayscale.onclick = allOriginal.onclick = null;
    };
    dialog.showModal();
    for (const m of machines) {
      const figure = node('figure', undefined, 'selected-machine-preview');
      const screen = node('div', undefined, 'selected-machine-screen');
      const caption = node('figcaption', m.artwork ? '' : 'Stock screen · kept unchanged');
      figure.append(node('h3', m.name), screen, caption);
      $('selected-previews-list').append(figure);
      if (!m.artwork) {
        screen.append(node('p', 'This machine uses a stock screen. A browser preview is not available.'));
        continue;
      }
      const control = createGraphicsOverride(m, updatePreviews);
      figure.append(control); controls.push({machine:m, select:control.children[1]});
      let staticUrl, currentSvg, request = 0;
      const update = () => {
        const current = ++request;
        const active = () => ticket === selectedPreviewRevision && current === request;
        const unavailable = error => {
          if (!active()) return;
          currentSvg = undefined;
          screen.replaceChildren(node('p', 'Preview unavailable: ' + error.message));
          caption.textContent = 'Check the machine artwork before installing.';
        };
        const show = svg => {
          if (!active() || currentSvg === svg) return;
          currentSvg = svg;
          if (staticUrl) { urlApi.revokeObjectURL(staticUrl); urls.delete(staticUrl); }
          staticUrl = urlApi.createObjectURL(new Blob([svg], {type:'image/svg+xml'})); urls.add(staticUrl);
          const image = node('img'); image.src = staticUrl; image.alt = m.name + ' display preview'; screen.replaceChildren(image);
        };
        try {
          const effective = configuration.modeFor(m);
          const artwork = selectArtwork(m, effective);
          caption.textContent = effective === 'colorful' ? 'Original design' : artwork === m.artwork
            ? 'Grayscale by design · kept intact' : m.grayscaleArtwork !== undefined
              ? 'Grayscale design provided by the developer.' : 'Automatic grayscale conversion';
          if (m.preview) {
            if (!staticUrl) screen.replaceChildren(node('p', 'Rendering default UI…'));
            return snapshots.render(m, artwork).then(show).catch(unavailable);
          } else {
            caption.textContent += ' · Static artwork; rendered snapshot unavailable';
            show(screenTemplate.replace('</svg>', artwork + '</svg>'));
          }
        } catch (error) { unavailable(error); }
      };
      updates.push(update);
    }
    updatePreviews();
    return rendered;
  };
  return {open, clear:clearSelectedPreviews, settled:() => rendered,
    dispose() {
      clearSelectedPreviews();
      $('selected-previews').removeEventListener('close', clearSelectedPreviews);
      $('close-selected-previews').onclick = null;
    }};
}

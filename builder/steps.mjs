// The step component keyboard: the OP–XY's fourteen white keys, each a stock step
// component, with the ten black keys that choose its setting. Step machines from
// the list are dragged (or picked, then placed) onto the components the profile
// lets them replace (core.mjs stepSlots); every other key stays stock.
// Original drawing after the OP–XY layout: https://teenage.engineering/guides/op-xy/step-components

// Stock components in white-key order as [profile slot, name the guide gives it].
export const STOCK_KEYS = [['pulse', 'pulse'], ['hold', 'hold'], ['multiply', 'multiply'], ['velocity', 'velocity'],
  ['ramp_up', 'ramp up'], ['ramp_down', 'ramp down'], ['random', 'random'], ['portamento', 'portamento'],
  ['bend', 'bend'], ['tonality', 'tonality'], ['jump', 'jump'], ['skip_param', 'skip param'],
  ['skip_component', 'skip comp'], ['skip_trig', 'skip trig']];
const keyName = slot => STOCK_KEYS.find(([id]) => id === slot)?.[1] ?? slot;
// Black keys over white-key columns, numbered 1..9 then 0: setting numbers 0..9.
const BLACK_COLUMNS = [1, 2, 3, 5, 6, 8, 9, 10, 12, 13];

function element(tag, text, className) {
  const el = document.createElement(tag);
  if (text !== undefined) el.textContent = text;
  if (className) el.className = className;
  return el;
}

// kit: the opened kit; slots: replaceable slot names; placed: slot -> machine for this
// selection; armed: a machine picked to place by keyboard or tap. Calls place(id, slot),
// remove(id) and arm(id) as the person acts.
export function render(container, {kit, slots, placed, armed, place, remove, arm}) {
  const machines = kit.machines.filter(m => m.kind === 'step');
  container.hidden = !machines.length;
  if (!machines.length) return;
  const palette = container.querySelector('.step-palette'), keyboard = container.querySelector('.step-keyboard');
  const detail = container.querySelector('.step-detail');
  const blacks = element('div', undefined, 'step-blacks');
  blacks.setAttribute('aria-hidden', 'true');
  BLACK_COLUMNS.forEach((column, setting) => {
    const key = element('span', undefined, 'step-black');
    key.style.gridColumn = String(column + 1);
    key.append(element('span', String((setting + 1) % 10), 'step-black-number'), element('span', '', 'step-black-label'));
    blacks.append(key);
  });
  // The black keys show a component's ten settings as its labels read on the device.
  const labels = m => blacks.querySelectorAll('.step-black-label').forEach((label, setting) => {
    label.textContent = m ? m.settings[setting].join(' ') : '';
  });
  palette.replaceChildren(...machines.map(m => {
    const item = element('li'), chip = element('button', m.name, 'step-chip');
    chip.type = 'button'; chip.draggable = true; chip.dataset.id = m.id;
    const where = Object.entries(placed).find(([, owner]) => owner.id === m.id)?.[0];
    chip.setAttribute('aria-pressed', String(armed === m.id));
    chip.title = where ? `Replaces ${keyName(where)}` : 'Drag onto a highlighted key';
    if (where) chip.append(element('span', '→ ' + keyName(where), 'step-chip-where'));
    chip.ondragstart = e => { e.dataTransfer.setData('text/plain', m.id); e.dataTransfer.effectAllowed = 'move'; };
    chip.onclick = () => arm(armed === m.id ? null : m.id);
    chip.onfocus = chip.onmouseenter = () => labels(m);
    item.append(chip);
    return item;
  }));
  const whites = element('div', undefined, 'step-whites');
  STOCK_KEYS.forEach(([slot, name]) => {
    const replaceable = slots.includes(slot), owner = placed[slot];
    const key = element('button', undefined, 'step-white' + (replaceable ? ' replaceable' : '') + (owner ? ' placed' : ''));
    key.type = 'button'; if (slot) key.dataset.slot = slot;
    key.disabled = !replaceable;
    key.append(element('span', name, 'step-stock'), element('span', owner ? owner.name : replaceable ? 'stock' : '', 'step-owner'));
    key.setAttribute('aria-label', !replaceable ? `${name}: stock only` :
      owner ? `${name}: replaced by ${owner.name}. Press to restore stock.` :
      armed ? `${name}: place here` : `${name}: stock. Pick a step component first.`);
    if (replaceable) {
      key.onclick = () => {
        if (armed) place(armed, slot);
        else if (owner) remove(owner.id);
      };
      key.ondragover = e => { e.preventDefault(); key.dataset.drag = 'true'; };
      key.ondragleave = () => { key.dataset.drag = 'false'; };
      key.ondrop = e => {
        e.preventDefault(); key.dataset.drag = 'false';
        const id = e.dataTransfer.getData('text/plain');
        if (machines.some(m => m.id === id)) place(id, slot);
      };
      key.onfocus = key.onmouseenter = () => labels(owner);
    }
    whites.append(key);
  });
  keyboard.replaceChildren(blacks, whites);
  labels(armed ? machines.find(m => m.id === armed) : Object.values(placed)[0]);
  detail.textContent = armed ? 'Choose a highlighted key for ' + machines.find(m => m.id === armed).name + '.' :
    'Drag a step component onto a highlighted key, or pick it and then the key. Press a replaced key to restore stock. ' +
    'On the OP–XY the key keeps its printed icon; its black keys choose the settings shown.';
}

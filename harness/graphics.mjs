// Add the authoring control without owning the workbench's HTML, themes or layout.
export function createGraphicsSelector(onChange) {
  const toolbar = document.querySelector('.screen-toolbar');
  const label = document.createElement('label');
  label.className = 'zoom-label graphics-label';
  label.htmlFor = 'graphics';
  const select = document.createElement('select');
  select.id = 'graphics'; select.disabled = true;
  select.setAttribute('aria-label', 'Machine graphics');
  for (const [value, text] of [['colorful', 'Colorful'], ['grayscale', 'Grayscale']]) {
    const option = document.createElement('option');
    option.value = value; option.textContent = text;
    select.append(option);
  }
  select.value = 'colorful';
  select.addEventListener('change', () => onChange(select.value));
  label.append('Graphics ', select);
  toolbar.insertBefore(label, toolbar.querySelector('.zoom-label'));
  const style = document.createElement('link');
  style.rel = 'stylesheet'; style.href = '/harness/graphics.css';
  document.head.append(style);
  return select;
}

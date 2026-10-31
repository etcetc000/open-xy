import './style.css';
import {Audition} from './audition.mjs';

try {
  const response = await fetch(import.meta.env.BASE_URL + 'kit.json', {cache:'no-store'});
  if (!response.ok) throw new Error('Machine collection unavailable');
  const kit = await response.json();
  document.getElementById('kit-data').textContent = JSON.stringify(kit);
} catch (error) {
  console.error('open-xy: configure OPEN_XY_KIT with a verified export for a complete preview.', error);
}
try {
  const {createBuilder} = await import('./app.mjs');
  await createBuilder({audition:Audition});
} catch (error) {
  console.error('open-xy failed to load.', error);
  const notice = document.createElement('div');
  notice.className = 'load-error';
  notice.setAttribute('role', 'alert');
  const message = document.createElement('p');
  message.textContent = 'The builder could not load. Reload the page to try again.';
  const reload = document.createElement('button');
  reload.type = 'button';
  reload.className = 'secondary';
  reload.textContent = 'Reload page';
  reload.onclick = () => location.reload();
  notice.append(message, reload);
  // Keep acknowledgement required; never expose a partially initialized app.
  document.getElementById('browser-gate').append(notice);
}

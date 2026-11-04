// Load before the stylesheet so a saved appearance is applied before first paint.
(() => {
  const key = 'opxy.workbench.theme.v1';
  const root = document.documentElement;
  try {
    if (localStorage.getItem(key) === 'light') root.dataset.theme = 'light';
  } catch {
    // The default remains usable when browser storage is unavailable.
  }
  document.addEventListener('DOMContentLoaded', () => {
    const selector = document.getElementById('theme');
    selector.value = root.dataset.theme;
    selector.addEventListener('change', () => {
      root.dataset.theme = selector.value;
      try { localStorage.setItem(key, selector.value); } catch { /* Appearance still changes for this visit. */ }
    });
  });
})();

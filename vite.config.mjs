import {existsSync, readFileSync, readdirSync, realpathSync} from 'node:fs';
import {basename, dirname, posix, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {defineConfig, loadEnv, normalizePath} from 'vite';
import {openKit} from './builder/core.mjs';

const root = dirname(fileURLToPath(import.meta.url));
const browserRoot = resolve(root, 'builder');
const renderers = ['controls.mjs', 'display.mjs', 'engine.mjs', 'processor.mjs'].map(name => resolve(root, 'sdk/browser', name));
const browserFiles = ['index.html', 'entry.mjs', 'style.css', 'app.mjs', 'core.mjs', 'browser.mjs', 'wizard.mjs', 'ferret.mjs', 'mtp.mjs', 'visuals.mjs', 'audition.mjs'];
// Vite's /@fs/ URLs: C:/x becomes /@fs/C:/x and /x becomes /@fs/x.
const fsUrl = path => posix.join('/@fs/', normalizePath(path));
const fsFile = url => { const path = url.slice(5); return /^[A-Za-z]:\//.test(path) ? path : '/' + path; };
const kitKeys = new Set(['format','version','profile','profileSource','definitions','fixed','linked','machines','extensions','objects','notices','stockCatalog','screenTemplate']);

// Check both the supplied path and its physical destination, including a new
// directory below an existing symlink/junction. Worktrees have a .git file.
export function outsideCheckout(path, label) {
  const absolute = resolve(path);
  let existing = absolute;
  const suffix = [];
  while (!existsSync(existing)) {
    suffix.unshift(basename(existing));
    const parent = dirname(existing);
    if (parent === existing) throw new Error(`Cannot resolve ${label}: ${path}`);
    existing = parent;
  }
  const physical = resolve(realpathSync(existing), ...suffix);
  for (const candidate of [absolute, physical]) {
    for (let directory = candidate; ; directory = dirname(directory)) {
      if (existsSync(resolve(directory, '.git'))) {
        throw new Error(`${label} must be outside every Git checkout (including ignored directories): ${path}`);
      }
      if (dirname(directory) === directory) break;
    }
  }
  return physical;
}

export async function readCollection(path) {
  outsideCheckout(path, 'Machine collection');
  const text = readFileSync(path, 'utf8');
  const serialized = path.endsWith('.html')
    ? text.match(/<script id="kit-data" type="application\/json">([\s\S]*?)<\/script>/)?.[1] : text;
  if (!serialized) throw new Error('Choose a completed builder HTML or kit.json export.');
  const kit = JSON.parse(serialized);
  if (Object.keys(kit).some(key => !kitKeys.has(key))) throw new Error('Unexpected collection fields; refusing to serve unrelated data.');
  await openKit(kit);
  return JSON.stringify(kit);
}

export default defineConfig(async ({command, mode}) => {
  const env = loadEnv(mode, root, 'OPEN_XY_');
  const configured = process.env.OPEN_XY_KIT ?? env.OPEN_XY_KIT;
  const output = process.env.OPEN_XY_OUT_DIR ?? env.OPEN_XY_OUT_DIR;
  const kitPath = configured ? resolve(root, configured) : null;
  if (mode === 'release' && !kitPath) throw new Error('OPEN_XY_KIT must identify a verified collection for a release build.');
  let kit = kitPath ? await readCollection(kitPath) : null;
  let releaseOutput;
  const clientDir = resolve(root, 'node_modules/vite/dist/client');
  const allowedFs = [...renderers, resolve(clientDir, 'env.mjs')];
  const paths = new Set(['/', '/index.html', '/kit.json', '/@vite/client', '/@vite/env',
    ...browserFiles.map(name => '/' + name),
    ...allowedFs.map(fsUrl)]);
  return {
    root: browserRoot,
    publicDir: false,
    base: './',
    server: {
      host:'127.0.0.1', port:8918, strictPort:true,
      allowedHosts:['localhost'], cors:false,
      fs:{strict:true, allow:[browserRoot, ...renderers, clientDir]},
    },
    preview: {host:'127.0.0.1', port:8919, strictPort:true},
    build: {outDir:resolve(root, output || 'dist/open-xy'), emptyOutDir:!kit, target:'es2022', sourcemap:false},
    plugins: [{
      name:'open-xy-browser-only',
      configResolved(config) {
        if (kit && config.command === 'build') {
          // Use the final merged configuration so --outDir and API overrides
          // cannot bypass the storage boundary.
          releaseOutput = resolve(config.root, config.build.outDir);
          outsideCheckout(releaseOutput, 'Collection-containing site output');
          if (config.build.emptyOutDir) throw new Error('Collection builds must not empty an existing output directory.');
        }
      },
      buildStart() {
        if (kit && command === 'build') {
          const directory = outsideCheckout(releaseOutput, 'Collection-containing site output');
          if (existsSync(directory) && readdirSync(directory).length) {
            throw new Error('Choose a fresh, empty output directory for a collection-containing site.');
          }
        }
      },
      transformIndexHtml: {
        order:'pre',
        handler(html) {
          const connect = command === 'serve' ? "'self' ws://127.0.0.1:8918 ws://localhost:8918" : "'self'";
          return html.replace(/<meta http-equiv="Content-Security-Policy"[^>]*>/,
            `<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'self' blob: 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src data: blob:; connect-src ${connect}; base-uri 'none'; form-action 'none'">`)
            .replace('<!-- kit -->','<script id="kit-data" type="application/json"></script>')
            .replace('<!-- app -->','<script type="module" src="/entry.mjs"></script>');
        },
      },
      configureServer(server) {
        // Vite serves an exact browser-source allowlist, never the repository,
        // configured artifact, firmware inputs, .env files, or arbitrary @fs paths.
        server.middlewares.use((req,res,next) => {
          let path;
          try { path = decodeURIComponent(new URL(req.url, 'http://127.0.0.1').pathname); }
          catch { res.writeHead(400); res.end(); return; }
          if (!['GET','HEAD'].includes(req.method)) { res.writeHead(405); res.end(); return; }
          if (!paths.has(path)) { res.writeHead(404); res.end(); return; }
          // Reject junctions/symlinks for authored files as well.
          const file = browserFiles.includes(path.slice(1)) ? resolve(browserRoot,path.slice(1))
            : path.startsWith('/@fs/') ? fsFile(path) : null;
          try {
            if (file && normalizePath(realpathSync(file)) !== normalizePath(file)) { res.writeHead(404); res.end(); return; }
          } catch { res.writeHead(404); res.end(); return; }
          res.setHeader('Cache-Control','no-store'); res.setHeader('X-Content-Type-Options','nosniff');
          if (path === '/kit.json') {
            if (!kit) { res.writeHead(404); res.end(); return; }
            res.setHeader('Content-Type','application/json'); res.end(req.method === 'HEAD' ? undefined : kit); return;
          }
          next();
        });
        if (kitPath) {
          server.watcher.add(kitPath);
          server.watcher.on('change', async path => {
            if (resolve(path) !== kitPath) return;
            try { kit = await readCollection(kitPath); }
            catch (error) { kit = null; server.config.logger.error(error.message); }
            server.ws.send({type:'full-reload'});
          });
        } else server.config.logger.info('UI-only mode: set OPEN_XY_KIT in .env.local to use a verified machine collection.');
      },
      generateBundle() {
        this.emitFile({type:'asset', fileName:'_headers', source:[
          '/*',
          '  X-Content-Type-Options: nosniff',
          '  Referrer-Policy: no-referrer',
          '  X-Frame-Options: DENY',
          '  Permissions-Policy: midi=(self), usb=(self)',
          '/',
          '  Cache-Control: no-cache',
          '/kit.json',
          '  Cache-Control: no-cache',
          '/assets/*',
          '  Cache-Control: public, max-age=31536000, immutable',
          '',
        ].join('\n')});
        if (kit) this.emitFile({type:'asset',fileName:'kit.json',source:kit});
      },
    }],
  };
});

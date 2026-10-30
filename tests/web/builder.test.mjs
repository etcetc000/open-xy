import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createServer, build} from 'vite';
import {mkdir, mkdtemp, readFile, rm, symlink, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {outsideCheckout} from '../../vite.config.mjs';

// CI never consumes a developer's local collection or private inputs.
process.env.OPEN_XY_KIT = '';
process.env.OPEN_XY_OUT_DIR = '';
const configFile = fileURLToPath(new URL('../../vite.config.mjs', import.meta.url));
test('dev server serves browser sources and rejects private paths and writes', async () => {
  const server = await createServer({configFile,mode:'test',logLevel:'silent',server:{port:0}});
  try {
    await server.listen();
    const url = `http://127.0.0.1:${server.httpServer.address().port}`;
    const page = await fetch(url);
    assert.equal(page.status,200);
    const html = await page.text();
    assert.match(html,/@vite\/client/);
    assert.match(html,/src="\/entry.mjs"/);
    for (const path of ['/entry.mjs','/style.css','/app.mjs','/browser.mjs','/wizard.mjs','/ferret.mjs','/mtp.mjs','/visuals.mjs','/audition.mjs']) {
      const response = await fetch(url+path); assert.equal(response.status,200,path);
    }
    // Vite's client and the shared renderers load through /@fs/ on every platform.
    const clientEnv = fileURLToPath(new URL('../../node_modules/vite/dist/client/env.mjs', import.meta.url));
    const shared = ['display.mjs','controls.mjs'].map(name =>
      fileURLToPath(new URL('../../sdk/browser/' + name, import.meta.url)));
    for (const file of [clientEnv, ...shared]) {
      const path = '/@fs/' + file.replaceAll('\\', '/').replace(/^\//, '');
      assert.equal((await fetch(url + path)).status, 200, path);
    }
    for (const path of ['/kit.json','/.env.local','/local/inputs/os.ldr','/builder/../local/inputs/os.ldr',
      '/%2e%2e/local/inputs/os.ldr','/@fs/C:/Windows/win.ini','/@fs/etc/passwd','/@fs//etc/passwd','/vite.config.mjs','/__open-in-editor?file=README.md']) {
      const response = await fetch(url+path); assert.equal(response.status,404,path);
    }
    assert.equal((await fetch(url+'/style.css',{method:'POST',body:'x'})).status,405);
  } finally { await server.close(); }
});
test('public UI build works without a collection; release build requires one', async () => {
  const directory = await mkdtemp(join(tmpdir(),'open-xy-web-'));
  try {
    await build({configFile,mode:'test',logLevel:'silent',build:{outDir:directory}});
    const html = await readFile(join(directory,'index.html'),'utf8');
    assert.doesNotMatch(html,/@vite\/client|ws:\/\//);
    assert.match(html,/assets\//);
    const headers = await readFile(join(directory,'_headers'),'utf8');
    assert.match(headers,/Permissions-Policy: midi=\(self\), usb=\(self\)/);
    assert.match(headers,/\/kit\.json\n  Cache-Control: no-cache/);
    await assert.rejects(readFile(join(directory,'kit.json')),/ENOENT/);
    await assert.rejects(build({configFile,mode:'release',logLevel:'silent',build:{outDir:directory}}),/OPEN_XY_KIT/);
  } finally { await rm(directory,{recursive:true,force:true}); }
});

test('collection builds require fresh external output, including config overrides', async () => {
  const directory = await mkdtemp(join(tmpdir(),'open-xy-release-'));
  const profileSource = await readFile(new URL('../../profiles/opxy-1.2.1.json',import.meta.url));
  // Public profile plus authored metadata; no firmware, stock catalog or tools.
  const screenTemplate = await readFile(new URL('../../sdk/platform/opxy-1.2.1/screen.svg',import.meta.url),'utf8');
  const collection = {format:'opxy-browser-kit/1',version:'0.0.2',profile:JSON.parse(profileSource),
    profileSource:profileSource.toString('base64'),objects:[],stockCatalog:'',screenTemplate,
    fixed:{fixture:{engine:34,page:82}},
    machines:[{id:'fixture',kind:'synth',identity:{engine:34,page:82},origin:'personal'}]};
  const source = join(directory,'kit.json');
  const external = join(directory,'site');
  try {
    await writeFile(source,JSON.stringify(collection));
    process.env.OPEN_XY_KIT = fileURLToPath(new URL('../../local/rejected-kit.json',import.meta.url));
    await assert.rejects(createServer({configFile,mode:'test',logLevel:'silent'}),/Machine collection must be outside/);
    process.env.OPEN_XY_KIT = source;
    await assert.rejects(build({configFile,mode:'release',logLevel:'silent'}),/outside every Git checkout/);
    process.env.OPEN_XY_OUT_DIR = external;
    const inside = fileURLToPath(new URL('../../dist/rejected-release',import.meta.url));
    await assert.rejects(build({configFile,mode:'release',logLevel:'silent',build:{outDir:inside}}),/outside every Git checkout/);
    await assert.rejects(readFile(join(inside,'kit.json')),/ENOENT/);
    await assert.rejects(build({configFile,mode:'release',logLevel:'silent',build:{emptyOutDir:true}}),/must not empty/);
    await build({configFile,mode:'release',logLevel:'silent'});
    assert.deepEqual(JSON.parse(await readFile(join(external,'kit.json'),'utf8')),collection);
    await assert.rejects(build({configFile,mode:'release',logLevel:'silent'}),/fresh, empty output/);
    assert.deepEqual(JSON.parse(await readFile(join(external,'kit.json'),'utf8')),collection);
    // Development remains possible without configuring a build destination.
    process.env.OPEN_XY_OUT_DIR = '';
    const server = await createServer({configFile,mode:'test',logLevel:'silent',server:{port:0}});
    try {
      await server.listen();
      const response = await fetch(`http://127.0.0.1:${server.httpServer.address().port}/kit.json`);
      assert.deepEqual(await response.json(),collection);
    } finally { await server.close(); }
    const deployOutput = join(directory,'deploy-site');
    const deploy = spawnSync(process.execPath,
      ['tools/deploy_cloudflare.mjs','--dry-run','--outdir',join(directory,'wrangler')], {
        cwd:fileURLToPath(new URL('../../',import.meta.url)), encoding:'utf8',
        windowsHide:true, timeout:120000,
        env:{...process.env, OPEN_XY_KIT:source, OPEN_XY_OUT_DIR:deployOutput,
          WRANGLER_SEND_METRICS:'false'},
      });
    assert.equal(deploy.status,0,deploy.stdout + deploy.stderr);
    assert.match(deploy.stdout,/dry.run/i);
    assert.deepEqual(JSON.parse(await readFile(join(deployOutput,'kit.json'),'utf8')),collection);
  } finally {
    process.env.OPEN_XY_KIT = '';
    process.env.OPEN_XY_OUT_DIR = '';
    await rm(directory,{recursive:true,force:true});
  }
});

test('storage boundary recognizes other checkouts, worktrees and junctions', async () => {
  const directory = await mkdtemp(join(tmpdir(),'open-xy-boundary-'));
  try {
    for (const kind of ['checkout','worktree']) {
      const checkout = join(directory,kind);
      await mkdir(checkout);
      if (kind === 'checkout') await mkdir(join(checkout,'.git'));
      else await writeFile(join(checkout,'.git'),'gitdir: ../checkout/.git/worktrees/test');
      assert.throws(()=>outsideCheckout(join(checkout,'ignored','new','site'),'Output'),/outside every Git checkout/);
      const redirect = join(directory,`${kind}-link`);
      await symlink(checkout,redirect,'junction');
      assert.throws(()=>outsideCheckout(join(redirect,'new','site'),'Output'),/outside every Git checkout/);
    }
    const external = join(directory,'external','new');
    assert.equal(outsideCheckout(external,'Output'),external);
  } finally { await rm(directory,{recursive:true,force:true}); }
});

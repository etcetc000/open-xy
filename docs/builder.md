# Open-XY browser builder

Open-XY builds OP-XY firmware in the browser. It runs as a website or from a
single `opxy-builder.html` file, including offline from `file://`. Firmware never
leaves the browser.

## Using the builder

1. Use a desktop browser exposing both Web MIDI and WebUSB, such as Chrome, Edge
   or Opera. Compatible browsers go straight to the risk notice; acknowledge it
   on every load. Unsupported browsers see a centered blocking message with no
   Continue button. Developer preview links still display the guide without
   enabling device access; returning to the builder restores the risk notice
   or browser block as appropriate.
2. Choose Mac or Windows. **Help me get the OS file** starts the guided setup;
   **I already have the OS file** opens the file picker. At the dev menu check,
   **No, it’s missing** is the first option and leads to enabling the menu.
   Choose the OS: the decrypted OP-XY OS 1.2.1 file `os.ldr`, or an Open-XY
   build to change. Only OS 1.2.1 is supported; the `.tfw` update files teenage
   engineering hosts are a different, encrypted format. The stock file is in a
   device capture at `/upgrade/firmware/blackfin/bin/os.ldr`; a previously
   installed Open-XY build is at `/os.ldr`. On a Mac with WebUSB in a compatible browser,
   follow the OS file guide and select **Read original OS**. The guide confirms OS 1.2.1, opens
   Mac file transfer mode and asks you to quit Field Kit and OpenMTP. It reads
   the original firmware path, checks the exact stock hash and loads the OS
   directly into the builder, with an optional backup download. It does not change device files or fall back to
   the patched root OS. Keep file transfer open to resume installation later.
   Windows uses the manual copy guide; Mac users
   can also choose **Copy it myself instead**.
   Both transfer guides show a notice for the rare case where a factory
   reset or initialization leaves `OPXY/upgrade/firmware/blackfin/bin/os.ldr`
   missing. **The OS file is missing · Help me recover it** shows how to exit
   file transfer, then **Disable dev menu** connects and sends the disable command
   in one click. The next page links to the official OS 1.2.1 download and explains
   how to power cycle, check that the menu is gone, and reinstall. After reinstalling,
   **Try again** goes straight to enabling the dev menu for the same computer.
3. Select machines. Synth rows have a playable preview of their C++ sound and
   display code. Sample engines with authored drawing pages have an interactive,
   silent screen preview; loading and playing samples runs on the OP-XY.
   Each row shows its code, data and state size against the
   memory meter. Machines from the [catalog](catalog.md) are marked
   **Community** and start unselected; their details name the maintainer and
   link to the machine's code at the commit the catalog pins. Open-XY's own
   example machines from the catalog are labelled as Open-XY machines. A
   [personal kit](#export-a-kit)'s own machines are marked **Yours**.
4. Follow the installation guide using the remembered computer choice. Mac
   browsers with WebUSB send the files directly; Windows downloads a ZIP to
   copy with File Explorer. The OS write step on the device is still manual.

The ZIP contains:

| File | Install |
| --- | --- |
| `os.ldr` | Device root, replacing the existing file |
| `content/op1_factory.json` | Replaces the menu file |
| `content/display/*.svg` | New screens for the selected machines; keep the stock files |
| `content/presets/<machine>/*.json` | Each selected instrument's init preset, `default.json`, and its own [presets](api.md#presets), in the instrument's folder |
| `layout.json`, hashes | Keep `layout.json`: it holds your machines' numbers, and loading it into the builder next time keeps them for your projects |

Every build shows its release at boot, `OPEN 0.0.2-1.2.1` for 0.0.2, the version in
[`VERSION`](../VERSION) that the builder also shows under its name and records in
`build.json`. Timestretch starts without a sample;
choose one on its sample page. Ring Mod is a send effect and has no preset.

The builder accepts the stock OS 1.2.1 file or any Open-XY OS made from it,
including one from an earlier release: it rebuilds the stock image by undoing
the reviewed edits and accepts it only if it matches OS 1.2.1 exactly. An exact
output of the same collection also restores its machine selection; for any
other, choose the machines again (the layout still keeps their numbers).
Encrypted `.tfw`, unknown patches and damaged files are rejected. Projects,
presets and samples on the device are not touched.

### Dev menu

The MIDI helper, adapted from [Ferret](https://github.com/bagofwaffles/ferret)
(MIT, Bag of Waffles; see `builder/ferret.mjs` and `builder/ferret.LICENSE`),
enables or disables the dev menu after an explicit click and browser MIDI
permission. Each enable or disable button connects and sends the requested
command in one action; failures and cancellation leave the action available
on the same page. It sets or removes only the `bootloader.dev` flag; it never
transfers firmware. After enabling, separate illustrated steps guide you through
pressing track 8 to open the system menu, then track 8 to return, before asking
whether the dev menu appears. After installation, **Disable dev menu** sends the command directly
before restarting; if already restarted, return to te-boot first. MIDI
needs a desktop browser and a secure context; otherwise the helper links to
hosted Ferret. The separate WebUSB MTP client reads or sends files only after
an explicit click and USB permission, and releases the connection afterwards.

## Capacity and machine numbers

The firmware reserves **2 MiB** of the application heap: 2040 KiB that every
selected machine shares for code, data and state, and 8 KiB for relocated stock
tables. The memory meter shows each machine's share; see
[memory and limits](api.md#memory-and-limits). CPU headroom is not measured.

Projects save each track's machine by number, so a machine keeps its numbers
in your firmware for good ([layouts](api.md#identity)). The builder remembers
your layout in this browser and writes it to `layout.json` in every download;
load that file under the machine list to build from another browser or
computer. A layout from a build for OS 1.2.0 still loads. Numbers given to a machine you drop stay held, so its projects never
open another machine. Adding a machine to the collection means adding it to
the native build; the builder is not a plugin loader and does not raise the
voice limit.

## Web development

The app is in `builder/`. Use Node 22.12+:

```sh
npm ci
npm run dev        # http://127.0.0.1:8918/
npm run test:web
```

Open `http://127.0.0.1:8918/#preview/machines` to browse and audition the machine
collection without uploading an OS file. Append a machine ID, for example
`#preview/machines/fm2op`, to open its details directly. Installation stays disabled;
the **OS** button returns to the normal builder entry. A configured collection is
required for machine previews.

Installation screens have direct preview links, such as
`http://127.0.0.1:8918/#preview/mac/transfer`, `#preview/mac/exit-transfer`
and `#preview/mac/write-os`. The original-OS flow uses
`#preview/source/mac/close-field-kit` and `#preview/source/mac/transfer`.
Use `#preview/source/mac/missing-os` to preview the recovery side path.
Replace `mac` with `windows` to see the manual
copy route. These developer links open the selected step immediately, skipping
the disclaimer and requiring no OS file or build. Device actions are disabled in previews;
**Preview next step** continues past the send screen. These URLs are developer
tools and are not linked from the installation interface. **Back to builder** shows the usual disclaimer
if it has not been acknowledged; opening a preview does not acknowledge it.

The dev server serves only the browser source allowlist and the configured
collection; do not replace it with a server rooted at the repository. Previews
and bundles need a collection: point the ignored `.env.local` at a verified
export from [Export a kit](#export-a-kit):

```dotenv
OPEN_XY_KIT=C:/Users/YOUR_USER/AppData/Local/OPXY-SDK/private/builds/browser-kit-001/opxy-builder.html
OPEN_XY_OUT_DIR=C:/Users/YOUR_USER/AppData/Local/OPXY-SDK/private/sites/browser-kit-001
```

`OPEN_XY_KIT` may also name an exported `kit.json`. Collections contain stock
menu content, so they and site output must live outside every checkout; the
server and build reject checkout-local paths, including through symlinks,
junctions and other worktrees. UI changes need no native rebuild; machine source
changes need a new export.

`npm run build:release` writes the site to `OPEN_XY_OUT_DIR` (or `--outDir`),
which must be a fresh directory; `npm run preview` serves it at
http://127.0.0.1:8919/. Asset URLs are relative, so the site can be hosted at a
domain root or a subpath; serve it over HTTPS for MIDI. CI runs `npm run build`
with no collection as a UI check and never consumes private inputs.

### Cloudflare deployment

`wrangler.jsonc` names the site `open-xy` and binds `open-xy.com`. The site is
static assets only, with no Worker script. The `beta` environment is a separate
`open-xy-beta` site at `beta.open-xy.com`, deployed from `staging`; production
is deployed from `main`. Merge work, and catalog changes, into `staging`,
release beta, then open a pull request from `staging` to `main` and release
production.

On the release host, after `npx wrangler login`, one command releases the
catalog:

```powershell
python sdk.py release --stock-os <stock-1.2.1>/os.ldr --out <builds>/release-001 --deploy beta
```

It fetches every published [catalog](catalog.md) machine at its pinned commit
(confirming its tag), exports the release kit from them
([Export a kit](#export-a-kit), with release verification), runs the
builder's web tests, builds the site around the kit and deploys it. It deploys
beta only from `staging` and production only from `main`, and only when the
checkout is exactly that branch on `origin`: no local edits, nothing unpushed or
behind. With `--deploy`, `--dry-run` does everything except publish; without
`--deploy` it stops after the kit. `--toolchain` and `--compiler` name the
pinned tools when they are not in `local/toolchain`.
`--out` must be a new folder outside every checkout; the kit and the site are
written inside it.

The deploy rebuilds in release mode, fails without a valid collection, and
uploads only the release directory. `npm run deploy:beta` and
`npm run deploy:cloudflare` remain for deploying an existing kit, with
`.env.local` set as above and a fresh `OPEN_XY_OUT_DIR`.

To add a custom domain, make its parent an
active Cloudflare zone, then use **Workers & Pages → open-xy → Settings →
Domains & Routes → Add → Custom Domain**; see the
[custom-domain guide](https://developers.cloudflare.com/workers/configuration/routing/custom-domains/).

## Export a kit

With the [pinned native tools](build.md) and Clang++ for wasm32:

```powershell
python sdk.py kit --preview-cxx clang++ --stock-os "$env:LOCALAPPDATA/OPXY-SDK/private/stock-1.2.1/os.ldr" --out "$env:LOCALAPPDATA/OPXY-SDK/private/builds/browser-kit-001"
```

`--stock-content` defaults to the repository's copy of `op1_factory.json`. The export runs the
full native build and its checks, then packages the native objects in link
order with each machine's `machine.json` and definition. The HTML carries the
authenticated stock menu and the SDK screen template; firmware stays
user-supplied. It is named only after every check passes.

A published kit comes from the release host, after
[release verification](build.md#release-verification). It holds the
[catalog](catalog.md)'s machines fetched at their pinned commits
(`--collection local/catalog/collection.json`), each with its repository, tag,
commit and maintainer. For your own machines,
add `--personal`: it runs on any pinned host, Windows or Apple-silicon macOS,
and skips only that verification. Open the resulting `opxy-builder.html`, or
serve it as in [web development](#web-development), to select machines,
preview them and use the installation guide with your device. It accepts your
stock OS and builds of its own collection.

The browser links the normalized ELF32 Blackfin objects itself, supporting the
LUIMM16, HUIMM16, PCREL24, PCREL24_JUMP_L and BYTE4 relocations. Unsupported
sections, COMMON or weak symbols, unresolved imports, overlapping relocations,
range overflow, constructors and memory overflow are errors. A deselected
machine's startup and catalog callbacks resolve to `kit_absent`, which declines:
startup skips it and its menu name is left to stock. Its audio code and state
are omitted.

The export checks that:

- selecting every machine reproduces the native `os.ldr` byte for byte;
- every non-empty selection matches GNU ld, including symbols and BSS;
- browser output matches the Python writer, with correct catalog, artwork and ZIP;
- every output is recognized and its selection restored, and corruption inside
  or outside the allowed edits is rejected.

Restoring a build removes the appended module record, restores the profile's
edit intervals and FIRST record, checks the stock SHA-256, and accepts the file
only if relinking its selection reproduces it exactly. Uploaded code is never
executed or reused, so only outputs of the same kit are recognized.

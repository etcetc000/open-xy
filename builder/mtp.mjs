// MTP over WebUSB for an OP–XY in "mtp file transfer (mac)" mode. Sends files
// to paths below the device's top level, or reads existing files. The only
// deletion is a same-named file the caller agreed to replace; folders are never
// replaced or deleted. Windows binds its own MTP driver, so WebUSB cannot open
// the device there; Windows keeps the File Explorer steps.

const VENDOR = 0x2367, VENDOR_CLASS = 0xff, ROOT = 0xffffffff;
const COMMAND = 1, DATA = 2, RESPONSE = 3;
const OK = 0x2001, SESSION_ALREADY_OPEN = 0x201e;
const OPEN_SESSION = 0x1002, CLOSE_SESSION = 0x1003, STORAGE_IDS = 0x1004, OBJECT_HANDLES = 0x1007,
  OBJECT_INFO = 0x1008, GET_OBJECT = 0x1009, DELETE_OBJECT = 0x100b, SEND_OBJECT_INFO = 0x100c, SEND_OBJECT = 0x100d;
const UNDEFINED_FORMAT = 0x3000, FOLDER_FORMAT = 0x3001;
// A multiple of every bulk packet size, so only the final transfer can be short.
const CHUNK = 1 << 20;
// WebUSB transfers never time out on their own; a silent device fails instead.
const TIMEOUT = 20000;
const refusals = {
  0x200c: 'Your OP–XY is full. Free some space, then try again.',
  0x200d: 'Your OP–XY’s storage is read-only.',
  0x200f: 'Your OP–XY refused access to that file.',
};

// The browser can't name the app holding the device; these are the ones that do.
export const BUSY = 'Another app is using your OP–XY. Quit Field Kit, OpenMTP or Android File Transfer, then try again.';
const STOPPED = 'Your OP–XY stopped responding. Reconnect it, then try again.';
const DISCONNECTED = 'Your OP–XY disconnected. Reconnect it, then try again.';
const timed = (promise, ms = TIMEOUT) => {
  let timer;
  return Promise.race([promise, new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(STOPPED)), ms); })])
    .finally(() => clearTimeout(timer));
};
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
export const supported = () => globalThis.isSecureContext && !!globalThis.navigator?.usb;

function concat(...parts) {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let at = 0; for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
}
const words = values => {
  const out = new Uint8Array(values.length * 4), view = new DataView(out.buffer);
  values.forEach((value, i) => view.setUint32(i * 4, value, true)); return out;
};
const view = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
export function container(type, code, transaction, payload = new Uint8Array(0)) {
  const header = new Uint8Array(12), v = view(header);
  v.setUint32(0, 12 + payload.length, true); v.setUint16(4, type, true);
  v.setUint16(6, code, true); v.setUint32(8, transaction, true);
  return concat(header, payload);
}
// PTP strings: a UTF-16 code-unit count including the terminator, then UTF-16LE.
export function ptpString(value) {
  const units = value ? value + '\0' : '';
  if (units.length > 255) throw new Error('That file name is too long for your OP–XY.');
  const out = new Uint8Array(1 + units.length * 2), v = view(out);
  out[0] = units.length;
  for (let i = 0; i < units.length; i++) v.setUint16(1 + i * 2, units.charCodeAt(i), true);
  return out;
}
export function readString(bytes, at) {
  const v = view(bytes); let text = '';
  for (let i = 0; i < bytes[at]; i++) text += String.fromCharCode(v.getUint16(at + 1 + i * 2, true));
  return text.replace(/\0$/, '');
}
// ObjectInfo dataset; the filename follows the fixed fields. A folder is a
// generic association (type 1) with no data.
export function objectInfo(storage, name, size, parent = ROOT, folder = false) {
  const fixed = new Uint8Array(52), v = view(fixed);
  v.setUint32(0, storage, true); v.setUint16(4, folder ? FOLDER_FORMAT : UNDEFINED_FORMAT, true);
  v.setUint32(8, size, true); v.setUint32(38, parent, true);
  if (folder) v.setUint16(42, 1, true);
  return concat(fixed, ptpString(name), ptpString(''), ptpString(''), ptpString(''));
}
const readParams = bytes => Array.from({length: Math.floor(bytes.length / 4)}, (_, i) => view(bytes).getUint32(i * 4, true));
function readWords(bytes) {
  if (!bytes || bytes.length < 4) throw new Error('Unexpected reply from your OP–XY.');
  const v = view(bytes), count = v.getUint32(0, true);
  if (bytes.length < 4 + count * 4) throw new Error('Unexpected reply from your OP–XY.');
  return Array.from({length: count}, (_, i) => v.getUint32(4 + i * 4, true));
}
// A relative path of plain names, such as content/display/macroosc.svg.
function segments(path) {
  const names = String(path).split('/');
  for (const name of names) {
    if (!name || name === '.' || name === '..' || /[\\\0]/.test(name)) throw new Error(`${path} isn’t a file path your OP–XY accepts.`);
    ptpString(name);
  }
  return names;
}

export class Device {
  #device; #number; #in; #out; #packet; #transaction = 0; #current; #storage; #running = false; #broken = false; #folders; #listings;
  static async request(usb = navigator.usb) {
    return new Device(await usb.requestDevice({filters: [{vendorId: VENDOR, classCode: VENDOR_CLASS}]}));
  }
  constructor(device) { this.#device = device; }
  async open() {
    const device = this.#device;
    try { await timed(device.open(), 5000); }
    catch (error) { throw new Error(error.name === 'NotFoundError' ? DISCONNECTED : error.message === STOPPED ? STOPPED : BUSY); }
    if (!device.configuration) await timed(device.selectConfiguration(1), 5000);
    const found = device.configuration.interfaces.find(i => {
      const a = i.alternate;
      return a.interfaceClass === VENDOR_CLASS && ['in', 'out'].every(d => a.endpoints.some(e => e.type === 'bulk' && e.direction === d));
    });
    if (!found) {
      await device.close().catch(() => {});
      throw new Error('Your OP–XY isn’t in Mac file transfer mode. On the factory menu, press track 2.');
    }
    try { await timed(device.claimInterface(found.interfaceNumber), 5000); }
    catch { await device.close().catch(() => {}); throw new Error(BUSY); }
    this.#number = found.interfaceNumber;
    const endpoint = d => found.alternate.endpoints.find(e => e.type === 'bulk' && e.direction === d);
    this.#in = endpoint('in').endpointNumber; this.#out = endpoint('out').endpointNumber;
    this.#packet = endpoint('out').packetSize;
    // Halts are cleared only when an endpoint actually stalls: clearing a healthy
    // endpoint resets the host's data toggle, and a device that keeps its own
    // then drops the next packet, so a second session would never get going.
    this.#transaction = 0; this.#broken = false;
    const opened = await this.#run(OPEN_SESSION, [1]);
    if (opened.code !== SESSION_ALREADY_OPEN) this.#expect(opened, 'open file transfer');
    const [storage] = readWords(this.#expect(await this.#run(STORAGE_IDS), 'find its storage').payload);
    if (storage === undefined) throw new Error('Your OP–XY has no storage available.');
    this.#storage = storage;
    this.#folders = new Map([['', ROOT]]);
    this.#listings = new Map();
  }
  // Names in one folder, mapped to their object handles. Listed once per
  // session and kept current as this client adds and deletes objects.
  async #children(parent) {
    if (this.#listings.has(parent)) return this.#listings.get(parent);
    const {payload} = this.#expect(await this.#run(OBJECT_HANDLES, [this.#storage, 0, parent]), 'list its files');
    const files = new Map();
    for (const handle of readWords(payload)) {
      const info = this.#expect(await this.#run(OBJECT_INFO, [handle]), 'list its files').payload;
      if (!info || info.length < 53 || 53 + info[52] * 2 > info.length) throw new Error('Unexpected file information from your OP–XY.');
      const name = readString(info, 52);
      if (files.has(name)) throw new Error(`Your OP–XY has more than one file named ${name}.`);
      files.set(name, {handle, size: view(info).getUint32(8, true), folder: view(info).getUint16(4, true) === FOLDER_FORMAT});
    }
    this.#listings.set(parent, files);
    return files;
  }
  // CLI access uses the same session and protocol as the browser installer.
  async list(path = '') {
    let parent = ROOT;
    for (const name of path ? segments(path) : []) {
      const folder = (await this.#children(parent)).get(name);
      if (!folder?.folder) throw new Error(`Your OP–XY does not have the folder ${path}.`);
      parent = folder.handle;
    }
    this.#listings.delete(parent);
    return [...await this.#children(parent)].map(([name, entry]) => ({name, ...entry}));
  }
  async mkdir(path) { await this.#folder(segments(path), () => {}); }
  async remove(path) {
    const names = segments(path), name = names.pop();
    let parent = ROOT;
    for (const part of names) {
      const folder = (await this.#children(parent)).get(part);
      if (!folder?.folder) throw new Error(`Your OP–XY does not have ${path}.`);
      parent = folder.handle;
    }
    const siblings = await this.#children(parent), file = siblings.get(name);
    if (!file || file.folder) throw new Error(`Refusing to delete a missing file or folder: ${path}.`);
    this.#expect(await this.#run(DELETE_OBJECT, [file.handle, 0]), 'delete ' + path);
    siblings.delete(name);
  }
  // Resolve an existing path without creating folders or changing device files.
  async read(path, {maxBytes = 16 * 1024 ** 2, onProgress = () => {}} = {}) {
    const names = segments(path);
    let parent = ROOT, file;
    for (const [index, name] of names.entries()) {
      file = (await this.#children(parent)).get(name);
      if (!file) throw new Error(`Your OP–XY does not have ${path}. Copy the OS manually instead.`);
      if (file.folder !== (index < names.length - 1)) throw new Error(`Unexpected file or folder at ${path}.`);
      parent = file.handle;
    }
    if (!Number.isSafeInteger(maxBytes) || maxBytes < 1 || file.size > maxBytes) throw new Error('That file is too large to read.');
    const {payload} = this.#expect(await this.#run(GET_OBJECT, [file.handle], null, () => {},
      {maxBytes, onProgress}), 'read ' + path);
    if (!payload || payload.length !== file.size) throw new Error('The OS file transfer was incomplete. Try again.');
    onProgress(1);
    return payload;
  }
  // The handle of a folder path, creating missing folders on the way.
  async #folder(names, onStep) {
    let parent = ROOT, path = '';
    for (const name of names) {
      path = path ? path + '/' + name : name;
      if (this.#folders.has(path)) { parent = this.#folders.get(path); continue; }
      const siblings = await this.#children(parent), existing = siblings.get(name);
      if (existing && !existing.folder) throw new Error(`Your OP–XY has a file named ${path} where a folder is needed.`);
      const handle = existing?.handle ?? this.#expect(await this.#run(SEND_OBJECT_INFO, [this.#storage, parent],
        objectInfo(this.#storage, name, 0, parent, true)), 'create the folder ' + path).params[2];
      if (!existing) {
        siblings.set(name, {handle, folder: true}); this.#listings.set(handle, new Map());
        onStep('folder', path);
      }
      parent = handle;
      this.#folders.set(path, parent);
    }
    return parent;
  }
  // Sends one file, such as os.ldr or content/op1_factory.json. Returns false
  // when a same-named file exists and replace() declines. onStep reports
  // ('folder', path) for each folder created and ('replace', path) before a
  // replacement.
  async send(path, bytes, {replace = async () => false, onProgress = () => {}, onStep = () => {}} = {}) {
    const names = segments(path), name = names.pop();
    if (bytes.length > 0xffffffff - 12) throw new Error('That file is too large for your OP–XY.');
    const parent = await this.#folder(names, onStep);
    const siblings = await this.#children(parent), existing = siblings.get(name);
    if (existing?.folder) throw new Error(`Your OP–XY already has a folder named ${path}.`);
    if (existing) {
      if (!await replace(path)) return false;
      onStep('replace', path);
      this.#expect(await this.#run(DELETE_OBJECT, [existing.handle, 0]), 'replace ' + path);
      siblings.delete(name);
    }
    const {params} = this.#expect(await this.#run(SEND_OBJECT_INFO, [this.#storage, parent],
      objectInfo(this.#storage, name, bytes.length, parent)), 'accept ' + path);
    this.#expect(await this.#run(SEND_OBJECT, [], bytes, onProgress), 'save ' + path);
    siblings.set(name, {handle: params[2], size: bytes.length, folder: false});
    return true;
  }
  // Safe to call at any time. A pending transfer is cancelled with the MTP class
  // Cancel Request so the device isn't left waiting for the rest of a file;
  // after any failure the session is abandoned rather than closed normally.
  async close() {
    const device = this.#device;
    if (!device.opened) return;
    if (this.#running || this.#broken) await this.#cancel(this.#running);
    else if (this.#storage !== undefined) await timed(this.#run(CLOSE_SESSION), 2000).catch(() => {});
    this.#storage = undefined;
    if (this.#number !== undefined) await device.releaseInterface(this.#number).catch(() => {});
    await device.close().catch(() => {});
  }
  // Cancel Request (0x64) for a pending transaction, then Get Device Status
  // (0x67) until the device reports OK, as the still-image class requires
  // before the next command. Endpoints the device stalled for the cancel are
  // cleared by the next session's first transfer.
  async #cancel(pending) {
    const setup = request => ({requestType: 'class', recipient: 'interface', request, value: 0, index: this.#number});
    if (pending) {
      const request = new Uint8Array(6);
      view(request).setUint16(0, 0x4001, true); view(request).setUint32(2, this.#current, true);
      await timed(this.#device.controlTransferOut(setup(0x64), request), 2000).catch(() => {});
    }
    for (let attempt = 0; attempt < 20; attempt++) {
      const status = await timed(this.#device.controlTransferIn(setup(0x67), 64), 2000).catch(() => null);
      if (!status || status.status !== 'ok' || status.data.byteLength < 4 || status.data.getUint16(2, true) === OK) break;
      await pause(100);
    }
  }
  #expect(result, action) {
    if (result.code !== OK) {
      throw new Error(refusals[result.code] ?? `Your OP–XY couldn’t ${action} (MTP error 0x${result.code.toString(16)}).`);
    }
    return result;
  }
  async #run(code, params = [], data = null, onProgress = () => {}, receiveOptions = {}) {
    const id = this.#current = this.#transaction++;
    this.#running = true;
    try {
      await this.#send(container(COMMAND, code, id, words(params)));
      if (data) await this.#send(container(DATA, code, id, data), onProgress);
      let reply = await this.#receive(receiveOptions), payload = null;
      if (reply.type === DATA) {
        if (reply.id !== id || reply.code !== code) throw new Error('Unexpected data from your OP–XY.');
        payload = reply.payload; reply = await this.#receive();
      }
      if (reply.type !== RESPONSE || reply.id !== id) throw new Error('Unexpected reply from your OP–XY.');
      return {code: reply.code, payload, params: readParams(reply.payload)};
    } catch (error) {
      this.#broken = true;
      throw error.name === 'NotFoundError' ? new Error(DISCONNECTED) : error;
    } finally { this.#running = false; }
  }
  // One bulk transfer. A stalled endpoint is cleared and the transfer retried
  // once; that's the only place halts are cleared (see open()).
  async #transfer(direction, start) {
    for (let attempt = 0; ; attempt++) {
      const result = await timed(start());
      if (result.status === 'ok') return result;
      if (result.status !== 'stall' || attempt) throw new Error(STOPPED);
      await timed(this.#device.clearHalt(direction, direction === 'out' ? this.#out : this.#in), 2000);
    }
  }
  async #send(bytes, onProgress = () => {}) {
    for (let at = 0; at < bytes.length; at += CHUNK) {
      await this.#transfer('out', () => this.#device.transferOut(this.#out, bytes.subarray(at, at + CHUNK)));
      onProgress(Math.min(1, (at + CHUNK) / bytes.length));
    }
    // A transfer ending exactly on a packet boundary needs a zero-length packet.
    if (bytes.length % this.#packet === 0) await this.#transfer('out', () => this.#device.transferOut(this.#out, new Uint8Array(0)));
  }
  async #read() {
    const result = await this.#transfer('in', () => this.#device.transferIn(this.#in, CHUNK));
    return new Uint8Array(result.data.buffer, result.data.byteOffset, result.data.byteLength);
  }
  async #receive({maxBytes = 16 * 1024 ** 2, onProgress = () => {}} = {}) {
    let first;
    do first = await this.#read(); while (!first.length);  // skip a trailing zero-length packet
    if (first.length < 12) throw new Error('Unexpected reply from your OP–XY.');
    const v = view(first), length = v.getUint32(0, true), parts = [first];
    if (length < 12 || length > maxBytes + 12 || first.length > length) throw new Error('Unexpected transfer size from your OP–XY.');
    const progress = have => { if (v.getUint16(4, true) === DATA) onProgress(length === 12 ? 1 : (have - 12) / (length - 12)); };
    progress(first.length);
    for (let have = first.length; have < length;) {
      const more = await this.#read();
      if (!more.length) throw new Error('Unexpected reply from your OP–XY.');
      parts.push(more); have += more.length;
      if (have > length) throw new Error('Unexpected transfer size from your OP–XY.');
      progress(have);
    }
    const whole = concat(...parts).subarray(0, length);
    return {type: v.getUint16(4, true), code: v.getUint16(6, true), id: v.getUint32(8, true), payload: whole.subarray(12)};
  }
}

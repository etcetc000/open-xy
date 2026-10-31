// A JSON-lines adapter, not another MTP implementation. Python device.push owns
// planning, backups, byte-for-byte verification and rollback.
import {createInterface} from 'node:readline';
import {pathToFileURL} from 'node:url';
import {Device} from '../builder/mtp.mjs';

export function xyDevice(devices) {
  const matches = devices.filter(d => d.vendorId === 0x2367 && d.productId === 0x0021);
  if (matches.length !== 1) throw new Error('Expected one OP-XY in MTP file transfer (mac) mode. Close OpenMTP and Field Kit.');
  return matches[0];
}

export class Session {
  constructor(device) { this.device = device; this.paths = new Map([['root', '']]); }
  path(id) {
    if (!this.paths.has(id)) throw new Error('Unknown device object');
    return this.paths.get(id);
  }
  async dispatch(request) {
    const d = this.device;
    switch (request.op) {
      case 'open': await d.open(); return null;
      case 'close': await d.close(); return null;
      case 'list': {
        const parent = this.path(request.parent);
        return (await d.list(parent)).map(entry => {
          const path = parent ? parent + '/' + entry.name : entry.name;
          this.paths.set(entry.handle, path);
          return {id: entry.handle, name: entry.name, size: entry.size, folder: entry.folder};
        });
      }
      case 'read': return Buffer.from(await d.read(this.path(request.id))).toString('base64');
      case 'delete': await d.remove(this.path(request.id)); return null;
      case 'mkdir': {
        const parent = this.path(request.parent);
        await d.mkdir(parent ? parent + '/' + request.name : request.name); return null;
      }
      case 'write': {
        const parent = this.path(request.parent), path = parent ? parent + '/' + request.name : request.name;
        if (!await d.send(path, Buffer.from(request.data, 'base64')))
          throw new Error('A file appeared before writing: ' + path);
        return null;
      }
      default: throw new Error('Unknown device operation');
    }
  }
}

async function main() {
  let session;
  const input = createInterface({input: process.stdin, crlfDelay: Infinity});
  try {
    for await (const line of input) {
      let request;
      try {
        request = JSON.parse(line);
        if (!session) {
          if (request.op !== 'open') throw new Error('Open the device first');
          let WebUSB;
          try { ({WebUSB} = await import('usb')); }
          catch { throw new Error('Mac device transfer needs the optional USB dependency: run npm ci in the SDK checkout.'); }
          const device = xyDevice(await new WebUSB({allowAllDevices: true}).getDevices());
          session = new Session(new Device(device));
        }
        process.stdout.write(JSON.stringify({seq: request.seq, result: await session.dispatch(request)}) + '\n');
        if (request.op === 'close') break;
      } catch (error) {
        process.stdout.write(JSON.stringify({seq: request?.seq, error: error.message}) + '\n');
      }
    }
  } finally { await session?.device.close(); }
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) await main();

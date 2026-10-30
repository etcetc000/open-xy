"""Run the installed build's synthetic machine benchmarks over USB MIDI. Requires python-rtmidi.

Save the project and stop playback first: stock diagnostic setup changes temporary
engine/playback state. --package must describe the firmware actually installed.
No firmware is installed by this command.
"""
import argparse, datetime, hashlib, json, re, time
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tooling.benchmark import decode
from tooling.formats import require, sha
TABLE = bytes.fromhex('ead496a8122c6e507f41033d87b9fbc5a59bd9e75d63211f300e4c72c8f6b48a744a08368cb2f0cee1df9da31927655b3b054779c3fdbf81ae90d2ec56682a14b38dcff14b75370926185a64dee0a29cfcc280be043a78466957152b91afedd32d13516fd5eba997b886c4fa407e3c02625c1e209aa4e6d8f7c98bb50f31734d5866241aa09edce2cdf3b18f350b497717296b55efd193ad82bcfec07a440638c6f8ba843e00427c536d2f11ab95d7e989b7f5cb714f0d331c22605ee4da98a6013f7d43f9c785bb94aae8d66c52102e4e70320cb688caf4dbe5a799231d5f619fa1e3dd67591b250a347648f2cc8eb0d0eeac922816546a457b3907bd83c1ff')
HEAD = bytes.fromhex('f00020762166')
VERIFIED_BENCHMARK_ROUTE = 0x21

def crc(b):
    c = 0
    for x in b:
        c = TABLE[c ^ x]
    return c

def cobs(b):
    out = bytearray()
    part = bytearray()
    for x in b:
        if not x:
            out.append(len(part) + 1)
            out.extend(part)
            part.clear()
        else:
            part.append(x)
            if len(part) == 254:
                out.append(255)
                out.extend(part)
                part.clear()
    out.append(len(part) + 1)
    out.extend(part)
    return bytes(out)

def uncobs(b):
    out = bytearray()
    i = 0
    while i < len(b):
        n = b[i]
        i += 1
        if not n or i + n - 1 > len(b):
            raise ValueError('bad COBS')
        out.extend(b[i:i + n - 1])
        i += n - 1
        if n < 255 and i < len(b):
            out.append(0)
    return bytes(out)

def pack(b):
    out = bytearray()
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum((x >> 7 << j for j, x in enumerate(g))))
        out.extend((x & 127 for x in g))
    return bytes(out)

def unpack(b):
    if any((x > 127 for x in b)):
        raise ValueError('non7bit')
    out = bytearray()
    for i in range(0, len(b), 8):
        mask = b[i]
        g = b[i + 1:i + 8]
        if mask >> len(g):
            raise ValueError('invalid mask')
        out.extend((x | (mask >> j & 1) << 7 for j, x in enumerate(g)))
    return bytes(out)

def frame(route, cmd, seq=0):
    b = bytes([route, seq, cmd, 0])
    return HEAD + pack(cobs(b + bytes([crc(b)])) + b'\x00') + b'\xf7'

def selftest():
    for b in [b'', b'\x00', b'a\x00b', bytes(range(256)), bytes([255]) * 520]:
        if uncobs(cobs(b)) != b or unpack(pack(b)) != b:
            raise ValueError('codec roundtrip')
    if cobs(b'\x00') != b'\x01\x01' or cobs(b'abc') != b'\x04abc':
        raise ValueError('COBS vector')
    for b in [b'\x00', b'\x03x']:
        try:
            uncobs(b)
        except ValueError:
            pass
        else:
            raise ValueError('malformed accepted')
    if len(frame(0x21, 9)) > 64:
        raise ValueError('frame bounds')

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['selftest', 'probe', 'benchmark', 'report'])
    p.add_argument('--out', type=Path)
    p.add_argument('--package', type=Path)
    p.add_argument('--seconds', type=float, help='how long to wait for reports (default: 60, or about 3 per expected run)')
    p.add_argument('--greet', action='store_true')
    p.add_argument('--installed', action='store_true', help='confirm --package is installed and unsaved work is saved')
    p.add_argument('--capture', type=Path, action='append', default=[], help='report: capture.json from each independent run')
    p.add_argument('--machine', help='report: machine ID in the package')
    p.add_argument('--report-url', help='report: optional public HTTPS location where you will publish report.md')
    a = p.parse_args(argv)
    a.route = VERIFIED_BENCHMARK_ROUTE
    selftest()
    if a.mode == 'selftest':
        print('PASS: codec vectors/roundtrip/rejection; no device access')
        return
    if a.mode == 'report':
        require(a.package and a.out and a.machine, 'report needs --package, --out and --machine')
        from tooling.profile_report import create
        print(create(a.package, a.capture, a.machine, a.report_url, a.out))
        return
    if not a.out or a.out.exists():
        raise ValueError('fresh output required')
    expected = None
    package_identity = None
    if a.mode == 'benchmark':
        require(a.package is not None, '--package is required for machine attribution')
        from tooling.profile_report import package_data
        _manifest, report, package_identity = package_data(a.package)
        expected = report['benchmark_cases']
        require(a.installed, 'Confirm this exact package is installed and save work first: --installed')
    # Each stock-workload run takes about 2.2 s (1,500 blocks); a sweep sends many.
    if a.seconds is None:
        a.seconds = max(60, 3 * len(expected) + 15) if expected else 60
    # A batch sweep can run for hours; its frames and traffic grow with its reports.
    if not 1 <= a.seconds <= (12 * 3600 if expected else 60):
        raise ValueError('timeout bounds')
    reports_expected = len(expected) if expected else 0
    # Each report also brings stock's text lines; leave room for many per report.
    frame_budget = max(10000, 25 * reports_expected + 1000)
    traffic_budget = max(2 * 1024 * 1024, 4096 * (reports_expected + 500))
    a.out.mkdir(parents=True)
    record = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'mode': a.mode, 'route': a.route, 'collector_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'expected_cases': expected, 'transmissions': [], 'frames': [], 'errors': [], 'identity': None, 'complete': False}
    record['package'] = package_identity
    try:
        import rtmidi
    except ImportError:
        raise SystemExit('Install the optional collector dependency: python -m pip install python-rtmidi')
    incoming = rtmidi.MidiIn(queue_size_limit=8192)
    outgoing = rtmidi.MidiOut()
    pending = bytearray()
    total = 0
    received_reports = 0
    ordinal = 0
    raw = (a.out / 'traffic.jsonl').open('x', encoding='utf-8')

    def save(item):
        raw.write(json.dumps(item) + '\n')
        raw.flush()

    def receive(seconds, reports=None):
        """Read for `seconds`, or until `reports` benchmark reports have arrived."""
        nonlocal total, received_reports
        end = time.monotonic() + seconds
        stopping = False
        while time.monotonic() < end:
            if reports is not None and received_reports >= reports and not stopping:
                # Listen several runs longer, so an extra report is still refused.
                end, stopping = min(end, time.monotonic() + 10), True
            got = incoming.get_message()
            if got is None:
                time.sleep(0.002)
                continue
            b = bytes(got[0])
            total += len(b)
            if total > traffic_budget:
                raise ValueError('traffic limit')
            save({'direction': 'rx', 'time': time.monotonic(), 'hex': b.hex()})
            if not b.startswith(HEAD) or not b.endswith(b'\xf7'):
                continue
            try:
                pending.extend(unpack(b[len(HEAD):-1]))
            except ValueError as e:
                record['errors'].append(str(e))
                continue
            if len(pending) > 8192:
                raise ValueError('unframed receive limit')
            while 0 in pending:
                split = pending.index(0)
                chunk = bytes(pending[:split])
                del pending[:split + 1]
                if not chunk:
                    continue
                try:
                    f = uncobs(chunk)
                    if len(f) < 5 or len(f) != f[3] + 5 or crc(f[:-1]) != f[-1]:
                        raise ValueError('bad frame length/CRC')
                except ValueError as e:
                    record['errors'].append(str(e))
                    continue
                item = {'route': f[0], 'seq': f[1], 'command': f[2], 'payload_hex': f[4:-1].hex(), 'text': f[4:-1].decode('utf-8', 'replace')}
                record['frames'].append(item)
                if item['command'] == 0x94 and item['route'] == 0x12:  # decode()'s reports only
                    received_reports += 1
                if len(record['frames']) > frame_budget:
                    raise ValueError('frame budget')
                text = item['text'].replace('\x00', '').strip()
                if f[2] == 10:
                    fields = dict((x.split(':', 1) for x in text.split(';') if ':' in x))
                    record['identity'] = fields
                    print('IDENTITY ' + json.dumps({k: v for k, v in fields.items() if k in ['mode', 'product', 'os_version', 'sw_version', 'bl_version']}), flush=True)
                elif re.search('PRODUCER|FILTER|RENDER|COMPRESSOR|LIMITER|\\[\\d+/\\d+\\]|max:|avg:', text):
                    print(text, flush=True)

    def send(command, route=None):
        nonlocal ordinal
        b = frame(a.route if route is None else route, command, ordinal)
        ordinal = ordinal + 1 & 255
        save({'direction': 'tx', 'time': time.monotonic(), 'hex': b.hex(), 'command': command})
        record['transmissions'].append({'command': command, 'hex': b.hex()})
        outgoing.send_message(list(b))
    try:
        ins = incoming.get_ports()
        outs = outgoing.get_ports()
        pat = re.compile('OP[\\s_-]*XY', re.I)
        ii = [i for i, n in enumerate(ins) if pat.search(n)]
        oo = [i for i, n in enumerate(outs) if pat.search(n)]
        if len(ii) != 1 or len(oo) != 1:
            raise RuntimeError('Expected unique OP-XY MIDI input/output; ' + json.dumps({'inputs': ins, 'outputs': outs}))
        record['ports'] = {'input': ins[ii[0]], 'output': outs[oo[0]]}
        incoming.ignore_types(sysex=False, timing=True, active_sense=True)
        incoming.open_port(ii[0])
        outgoing.open_port(oo[0])
        if a.greet:
            send(5, 0x21)
            receive(1)
        send(9)
        receive(3)
        ident = record['identity'] or {}
        if a.mode == 'benchmark':
            if VERIFIED_BENCHMARK_ROUTE != a.route:
                raise RuntimeError('Benchmark endpoint routing unverified; command not sent')
            if ident.get('mode') != 'normal' or ident.get('product') not in ('OP-XY', 'OP-1') or ident.get('os_version') != '1.2.1':
                raise RuntimeError('Normal OS identity not confirmed; benchmark not sent')
            received_reports = 0  # only this benchmark's reports end the listen early
            send(0xc8)
            receive(a.seconds, len(expected) if expected else None)
        else:
            receive(max(0, a.seconds - 3))
        if pending:
            record['errors'].append('Truncated diagnostic frame')
        record['complete'] = True
    finally:
        for r in [outgoing, incoming]:
            try:
                r.close_port()
                r.delete()
            except Exception as e:
                record['errors'].append('cleanup: ' + str(e))
        raw.close()
        (a.out / 'capture.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
        (a.out / 'messages.txt').write_text('\n'.join((f['text'] for f in record['frames'])), encoding='utf-8')
    if expected is not None:
        decoded = decode(record, expected)
        results = {'package': package_identity, 'engines': decoded, 'normalization': '100 * encoded average / 65535; stock 580352-cycle block budget', 'workload': 'Versioned workloads in docs/profiling.md; synth: stock note 60/midpoint, then in a sweep build each control through its positions; effect/sample: isolated default-setting callback fixtures', 'limitations': ['Synthetic test, not live project utilization or maximum polyphony.', 'Results assigned by build order and exact packet count; device firmware hash is not attested.', 'Machine settings affect cost; a sweep moves one control at a time, then combines each control at its heaviest position; other combinations and presets are not run.', 'Graphics are excluded. Effect/sample callback tests exclude stock mixing, preparation and allocation.', 'Whole voice includes oscillator; do not add the two.', 'A maximum of 65535 is clipped; insufficient voice activity is flagged.']}
        (a.out / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
        from tooling.cpu_measurement import recommendation
        from tooling.benchmark import case_label, combined_label, combined_positions
        lines = ['| Machine | Scope | Setting | Mean block budget |', '| --- | --- | --- | ---: |']
        heaviest, swept = {}, {}
        for item in decoded:
            metrics = item['metrics']
            suffix = ' (insufficient activity)' if item['insufficient_voice_activity'] else ''
            case = item['machine']
            scope = 'whole voice' if case.get('kind', 'synth') == 'synth' else case['kind'] + ' callback'
            metric = metrics[case.get('metric', 5)]
            value = metric['average_budget_percent']
            setting = case.get('setting')
            if setting:
                swept.setdefault(case['name'], []).append((setting, metric['encoded_average']))
            label = (combined_label(combined_positions(swept.get(case['name'], []))) if case.get('combined')
                     else case_label(case))
            lines.append(f"| {case['name']}{suffix} | {scope} | {label} | {value:.3f}% |")
            # A silent run says nothing about cost; a saturated one is the heaviest of all.
            # As profile report: a swept setting counts if it timed anything; others must sound.
            sounded = metric['encoded_average'] > 0 and (setting or not item['insufficient_voice_activity'])
            if case.get('source') == 'sdk' and sounded and value > heaviest.get(case['name'], (0, ''))[0]:
                heaviest[case['name']] = (value, label + (' (saturated: at or above the whole budget)'
                                                          if metric['encoded_average'] >= 65535 else ''))
        lines.append('')
        for name, (value, label) in heaviest.items():
            warning = recommendation(value, 'at ' + label)
            lines.append(f'- {name}: heaviest {value:.3f}% at {label} in this run.' +
                         (f' **{warning[0].upper() + warning[1:]}.**' if warning else ''))
        lines += ['', 'Workload: ' + results['workload'] + '.', '', 'Normalization: ' + results['normalization'] + '.',
                  '', *('- ' + item for item in results['limitations']), '',
                  'Firmware SHA-256: `' + package_identity['firmware_sha256'] + '`']
        (a.out / 'results.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print('\n'.join(lines))
    print(json.dumps({'frames': len(record['frames']), 'identity': bool(record['identity']), 'commands': [x['command'] for x in record['transmissions']], 'errors': record['errors'], 'saved': str(a.out)}))
if __name__ == '__main__':
    main()

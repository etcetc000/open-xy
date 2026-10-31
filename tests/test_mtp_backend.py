import base64
import subprocess
import sys
import unittest

from tooling.mtp import Device


@unittest.skipUnless(sys.platform == 'darwin', 'Mac backend uses POSIX pipes and signal masks')
class MacBackendTests(unittest.TestCase):
    def session(self, body):
        device = Device()
        device.sequence = 0
        device.process = subprocess.Popen([sys.executable, '-u', '-c', body], stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        self.addCleanup(device.__exit__, None, None, None)
        return device

    def test_chunked_large_read_and_error_keeps_rpc_aligned(self):
        device = self.session('''import json,sys,base64
for line in sys.stdin:
 q=json.loads(line)
 if q['op']=='read': out={'seq':q['seq'],'result':base64.b64encode(b'x'*3000000).decode()}
 elif q['op']=='bad': out={'seq':q['seq'],'error':'refused'}
 else: out={'seq':q['seq'],'result':None}
 sys.stdout.write(json.dumps(out)+'\\n');sys.stdout.flush()
 if q['op']=='close': break
''')
        self.assertEqual(len(base64.b64decode(device.rpc('read'))), 3000000)
        with self.assertRaisesRegex(ValueError, 'refused'):
            device.rpc('bad')
        self.assertIsNone(device.rpc('close'))

    def test_wrong_sequence_is_rejected(self):
        device = self.session('import sys,json;sys.stdin.readline();print(json.dumps({"seq":999,"result":None}))')
        with self.assertRaisesRegex(ValueError, 'sequence'):
            device.rpc('open')

    def test_exited_adapter_is_reported(self):
        device = self.session('import sys;sys.stdin.readline()')
        with self.assertRaisesRegex(ValueError, 'exited'):
            device.rpc('open')

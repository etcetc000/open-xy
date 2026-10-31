"""macOS device backend using the browser's MTP module through Node WebUSB."""
import base64
from dataclasses import dataclass
import json
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import time

from .formats import require


@dataclass
class Entry:
    id: int
    name: str
    size: int
    folder: bool


class Device:
    def __enter__(self):
        node = shutil.which('node')
        require(node, 'Mac device transfer needs Node 22.12 or later and npm ci in the SDK checkout.')
        self.process = subprocess.Popen([node, str(Path(__file__).with_name('mtp_cli.mjs'))],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        start_new_session=True)
        self.sequence = 0
        try:
            self.rpc('open')
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, *_):
        try:
            if self.process.poll() is None:
                try:
                    self.rpc('close', timeout=5)
                except Exception:
                    pass
        finally:
            if self.process.poll() is None:
                self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                stream.close()

    def rpc(self, operation, timeout=60, **arguments):
        # Finish the bounded request before Ctrl+C reaches the transaction's
        # rollback handler; otherwise an unread reply would desynchronize it.
        previous = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT})
        try:
            return self._rpc(operation, timeout, **arguments)
        finally:
            signal.pthread_sigmask(signal.SIG_SETMASK, previous)

    def _rpc(self, operation, timeout, **arguments):
        self.sequence += 1
        payload = dict(seq=self.sequence, op=operation, **arguments)
        self.process.stdin.write((json.dumps(payload) + '\n').encode())
        self.process.stdin.flush()
        end, data = time.monotonic() + timeout, bytearray()
        while not data.endswith(b'\n'):
            remaining = end - time.monotonic()
            require(remaining > 0 and select.select([self.process.stdout], [], [], remaining)[0],
                    'The Mac USB adapter stopped responding; do not flash an incomplete transfer.')
            chunk = os.read(self.process.stdout.fileno(), 65536)
            require(chunk, 'The Mac USB adapter exited unexpectedly.')
            data.extend(chunk)
            require(len(data) <= 24 * 1024 * 1024, 'Unexpected USB adapter response size.')
        reply = json.loads(data)
        require(reply.get('seq') == self.sequence, 'Unexpected USB adapter response sequence.')
        require('error' not in reply, reply.get('error', 'USB adapter error'))
        return reply['result']

    def root(self):
        return 'root'

    def list(self, parent):
        return [Entry(**entry) for entry in self.rpc('list', parent=parent)]

    def read(self, entry):
        return base64.b64decode(self.rpc('read', id=entry.id), validate=True)

    def delete(self, entry):
        self.rpc('delete', id=entry.id)

    def create_folder(self, parent, name):
        self.rpc('mkdir', parent=parent, name=name)

    def create_file(self, parent, name, data, like=None):
        self.rpc('write', parent=parent, name=name, data=base64.b64encode(data).decode('ascii'))

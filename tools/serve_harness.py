"""Read-only localhost server. Exposes preview sources only, never repository inputs."""
import argparse
import socket
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tooling.preview import preview_bundle

ALLOWED = {
    '/sdk/browser/controls.mjs': 'sdk/browser/controls.mjs',
    '/sdk/browser/engine.mjs': 'sdk/browser/engine.mjs',
    '/sdk/browser/processor.mjs': 'sdk/browser/processor.mjs',
    '/sdk/browser/display.mjs': 'sdk/browser/display.mjs',
    '/sdk/browser/graphics.mjs': 'sdk/browser/graphics.mjs',
    '/harness/graphics.mjs': 'harness/graphics.mjs',
    '/harness/graphics.css': 'harness/graphics.css',
    '/': 'harness/index.html',
    '/harness/': 'harness/index.html',
    **{f'/harness/{name}': f'harness/{name}' for name in
       ('index.html', 'style.css', 'theme.mjs', 'knobs.mjs', 'audio-activation.mjs', 'audio-output.mjs', 'app.mjs', 'screen.mjs', 'processor.mjs')},
}
MIME = {'.html': 'text/html', '.css': 'text/css', '.mjs': 'text/javascript'}

def resolve_public_path(url):
    name = ALLOWED.get(unquote(urlsplit(url).path))
    if name is None:
        return None
    path = ROOT / name
    # Reject symlinks/junctions even when their target happens to be inside the repo.
    if path.resolve() != path.absolute() or not path.is_file():
        return None
    return path

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.respond(False)

    def do_HEAD(self):
        self.respond(True)

    def respond(self, head):
        artifact = self.server.preview.get(unquote(urlsplit(self.path).path))
        if artifact:
            content_type, data = artifact
        else:
            path = resolve_public_path(self.path)
            if path is None:
                self.send_error(404)
                return
            content_type, data = MIME[path.suffix] + '; charset=utf-8', path.read_bytes()
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        self.end_headers()
        if not head:
            self.wfile.write(data)

class PreviewServer(ThreadingHTTPServer):
    allow_reuse_address = False
    preview = {}

    def server_bind(self):
        if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8917)
    parser.add_argument('--preview', type=Path, help='Fresh C++ preview built by sdk.py preview')
    args = parser.parse_args()
    bundle = preview_bundle(args.preview) if args.preview else {}
    try:
        server = PreviewServer(('127.0.0.1', args.port), Handler)
    except OSError as error:
        parser.exit(1, f'Port {args.port} is unavailable ({error.strerror or error}). '
                       f'Stop the other server or pass --port {args.port + 20}.\n')
    server.preview = bundle
    print(f'OP-XY workbench: http://127.0.0.1:{server.server_port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

# Serves payload/ at a capped rate, with a first-byte delay, like a slow phone link.
import http.server, os, sys, time
RATE = int(os.environ.get('RATE', 1_000_000)); DELAY = float(os.environ.get('DELAY', 1.5))
ROOT = os.path.join(os.environ['UPD_WORK'], 'payload')
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        p = os.path.join(ROOT, os.path.basename(self.path))
        if not os.path.isfile(p): self.send_response(404); self.end_headers(); return
        b = open(p, 'rb').read(); time.sleep(DELAY)
        self.send_response(200); self.send_header('Content-Length', str(len(b))); self.end_headers()
        for i in range(0, len(b), 16384):
            self.wfile.write(b[i:i+16384]); time.sleep(16384 / RATE)
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()

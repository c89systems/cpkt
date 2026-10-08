"""Serve fixed archive bytes and record requests for native cache tests."""
import argparse
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("work", type=Path)
a = p.parse_args()
payload = (a.work / "fixture.tar.gz").read_bytes()


class Origin(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        with (a.work / "requests").open("a") as log:
            log.write(self.command + " " + self.path + "\n")
        if self.command != "GET" or self.path != "/fixture.tar.gz":
            self.send_error(503)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


with HTTPServer(("127.0.0.1", 0), Origin) as server:
    (a.work / "port").write_text(str(server.server_port))
    server.serve_forever()

#!/usr/bin/env python3
"""Serve the small HTTP response fixture for compiled libcurl tests."""

import argparse
import http.server
import socketserver
import time
from pathlib import Path


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, _format, *_args):
        pass

    def do_GET(self):
        if self.path == "/slow":
            first, last = b"slow-start\n", b"slow-end\n"
            self.send_response(200)
            self.send_header("Content-Length", str(len(first) + len(last)))
            self.end_headers()
            self.wfile.write(first)
            self.wfile.flush()
            time.sleep(2)
            self.wfile.write(last)
        elif self.path == "/fast":
            body = b"ok"
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)


class LoopbackHTTPServer(http.server.ThreadingHTTPServer):
    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.server_address[1]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-file", required=True, type=Path)
    args = parser.parse_args(argv)
    server = LoopbackHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    server.block_on_close = False
    try:
        args.port_file.write_text(str(server.server_address[1]) + "\n")
        server.serve_forever()
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

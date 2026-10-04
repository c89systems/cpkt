#!/usr/bin/env python3
"""Exercise bundled libcurl's multi socket API against a local hostname."""

import argparse
import http.server
import socketserver
import subprocess
import threading
import time


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
    parser.add_argument("--runner", action="append", default=[],
                        help="one configured emulator argument; repeat for each argument")
    parser.add_argument("clients", nargs="+")
    args = parser.parse_args(argv)
    print("multi socket harness: binding loopback server", flush=True)
    server = LoopbackHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    server.block_on_close = False
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        port = str(server.server_address[1])
        print(f"multi socket harness: listening on {port}", flush=True)
        for client in args.clients:
            print(f"multi socket harness: starting {client}", flush=True)
            subprocess.run(args.runner + [client, "--multi", port], check=True, timeout=15)
            print(f"multi socket harness: completed {client}", flush=True)
    finally:
        print("multi socket harness: shutting down", flush=True)
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    main()

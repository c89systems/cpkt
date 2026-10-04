#!/usr/bin/env python3
"""Verify the curl test server can bind without reverse DNS."""

import sys
import subprocess
import unittest
from unittest.mock import MagicMock, call, patch


class LoopbackServerTest(unittest.TestCase):
    def test_native_and_emulated_clients(self):
        sys.dont_write_bytecode = True
        from curl_async_dns_server import main

        for prefix in ([], ["qemu fixture", "-L", "sysroot with spaces"]):
            with self.subTest(prefix=prefix):
                server = MagicMock()
                server.server_address = ("127.0.0.1", 12345)
                args = ["--runner=" + word for word in prefix] + ["client one", "client two"]
                with patch("curl_async_dns_server.LoopbackHTTPServer", return_value=server), \
                        patch("curl_async_dns_server.subprocess.run") as run:
                    main(args)
                self.assertEqual(run.call_args_list, [
                    call(prefix + [client, "--multi", "12345"], check=True, timeout=15)
                    for client in ["client one", "client two"]])
                server.shutdown.assert_called_once()
                server.server_close.assert_called_once()

    def test_client_failure_stops_and_closes_server(self):
        sys.dont_write_bytecode = True
        from curl_async_dns_server import main

        server = MagicMock()
        server.server_address = ("127.0.0.1", 12345)
        with patch("curl_async_dns_server.LoopbackHTTPServer", return_value=server), \
                patch("curl_async_dns_server.subprocess.run",
                      side_effect=subprocess.CalledProcessError(1, "client")) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                main(["client one", "client two"])
        self.assertEqual(run.call_count, 1)
        server.shutdown.assert_called_once()
        server.server_close.assert_called_once()

    def test_bind_without_reverse_dns(self):
        sys.dont_write_bytecode = True
        from curl_async_dns_server import Handler, LoopbackHTTPServer

        with patch("socket.getfqdn", side_effect=RuntimeError("reverse DNS is unavailable")):
            server = LoopbackHTTPServer(("127.0.0.1", 0), Handler)
        try:
            self.assertEqual(server.server_name, "localhost")
            self.assertEqual(server.server_port, server.server_address[1])
        finally:
            server.server_close()


if __name__ == "__main__":
    unittest.main()

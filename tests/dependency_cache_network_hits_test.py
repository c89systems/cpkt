"""Prove digest hits make no HTTP requests across names and fresh source roots."""

import hashlib
import http.server
import io
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading

repo = pathlib.Path(sys.argv[1]).resolve()
scratch = repo / "build"
scratch.mkdir(exist_ok=True)
payload = io.BytesIO()
with tarfile.open(fileobj=payload, mode="w:gz") as archive:
    for name in ("include/pslog.h", "lib/libpslog.a"):
        data = b"cache fixture\n"
        member = tarfile.TarInfo("libpslog-0.11.0-fixture/" + name)
        member.size = len(data)
        archive.addfile(member, io.BytesIO(data))
data = payload.getvalue()
digest = hashlib.sha256(data).hexdigest()
requests = []


class Origin(http.server.BaseHTTPRequestHandler):
    def do_HEAD(self):
        requests.append("HEAD " + self.path)
        self.send_error(503, "cache hit attempted network access")

    def do_GET(self):
        requests.append(self.path)
        if self.path != "/fixture.tar.gz":
            self.send_error(503, "cache hit attempted network access")
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_):
        pass


def configure(source, body, env, cache=None):
    source.mkdir(parents=True, exist_ok=True)
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.21)\n"
        "project(cache_fixture NONE)\n" + body
    )
    command = ["cmake", "-S", str(source), "-B", str(source / "build")]
    if cache is not None:
        command.append(f"-DCPKT_DEPENDENCY_CACHE={cache}")
    result = subprocess.run(command, env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def check_toolchain_caches(work):
    # Exercise the actual provisioning branches. Synthetic metadata selects
    # small pinned bytes; extraction fails deliberately after cache acquisition.
    # A downloader invocation fails earlier and leaves an observable marker.
    tool_scripts = (
        (repo / "scripts", "cpkt-toolchains.sh", "bootlin"),
        (repo / "scripts", "cpkt-toolchains.sh", "mig"),
        (repo / "scripts", "cpkt-aflpp.sh", "afl"),
        (repo / "skills/pkt-systems-cmake-lifecycle/scripts", "cpkt-toolchains.sh", "bootlin"),
        (repo / "skills/pkt-systems-cmake-lifecycle/scripts", "cpkt-toolchains.sh", "mig"),
        (repo / "skills/pkt-systems-cmake-lifecycle/scripts", "cpkt-aflpp.sh", "skill-afl"),
    )
    for index, (directory, name, kind) in enumerate(tool_scripts):
        fixture = work / f"toolchain-{index}"
        scripts = fixture / "scripts"
        scripts.mkdir(parents=True)
        contents = (directory / name).read_text()
        # Remove only CLI dispatch to call the real provisioning functions.
        contents, dispatch = contents.rsplit('\ncase "${1:-}" in', 1)
        assert dispatch.strip().endswith("esac")
        (scripts / "resolver.sh").write_text(contents)
        shutil.copyfile(directory / "cpkt-archive-cache.sh", scripts / "cpkt-archive-cache.sh")
        binaries = fixture / "bin"
        binaries.mkdir()
        for executable, body in (
            ("curl", 'printf request >> "$CPKT_TEST_REQUEST_LOG"\nexit 91\n'),
            ("wget", 'printf request >> "$CPKT_TEST_REQUEST_LOG"\nexit 91\n'),
            ("tar", "exit 73\n"),
            ("uname", 'case "$1" in -m) printf x86_64 ;; *) printf Linux ;; esac\n'),
            ("bison", "exit 0\n"), ("flex", "exit 0\n"),
            ("cc", "exit 0\n"), ("cxx", "exit 0\n"),
        ):
            path = binaries / executable
            path.write_text("#!/bin/sh\n" + body)
            path.chmod(0o755)
        host = fixture / "host"
        (host / "include").mkdir(parents=True)
        (host / "include/gmp.h").touch()
        (host / "bin").mkdir()
        shutil.copyfile(binaries / "cc", host / "bin/fixture-gcc")
        (host / "bin/fixture-gcc").chmod(0o755)
        cache = fixture / "cache"
        archives = cache / "archives"
        archives.mkdir(parents=True)
        (archives / "previous-name.tar.gz").write_bytes(data)
        requested = "AFLplusplus-5.02c.tar.gz" if "afl" in kind else "collection.tar.xz" if kind == "bootlin" else "mig.tar.gz"
        (archives / requested).write_bytes(b"corrupt requested alias")
        driver = (
            'set -euo pipefail\n'
            f'source {shlex.quote(str(scripts / "resolver.sh"))}\n'
            'fixture_root=$1\nfixture_digest=$2\n'
            'cache_root() { printf "%s\\n" "$fixture_root/cache"; }\n'
            'cache() { cache_root; }\n'
            'bootlin_values() { printf "arch|collection|%s|fixture|sysroot|%s/host\\n" "$fixture_digest" "$fixture_root"; }\n'
            'bootlin_description() { printf "cc=%s/bin/cc\\ncxx=%s/bin/cxx\\nroot=%s/host\\n" "$fixture_root" "$fixture_root" "$fixture_root"; }\n'
            'bootlin_ready() { return 1; }\nhost_mig_ready() { return 1; }\n'
            'afl_ready() { return 1; }\nready() { return 1; }\n'
            'archive_sha256=$fixture_digest\n'
        )
        if kind == "bootlin":
            driver += "install_bootlin_locked x86_64-linux-gnu\n"
        elif kind == "mig":
            driver += 'install_host_mig_locked fixture "$fixture_digest" mig.tar.gz "$fixture_root/mig"\n'
        elif kind == "afl":
            driver += 'ensure_locked "$fixture_root/cache"\n'
        else:
            driver += "ensure_locked\n"
        driver_path = scripts / "driver.sh"
        driver_path.write_text(driver)
        marker = fixture / "requests"
        tool_env = dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ["PATH"],
                        CPKT_TEST_REQUEST_LOG=str(marker))
        result = subprocess.run(["bash", str(driver_path), str(fixture), digest],
                                env=tool_env, capture_output=True, text=True)
        assert result.returncode == 73, (kind, result.returncode, result.stdout, result.stderr)
        assert not marker.exists(), (kind, "cache hit invoked a downloader")
        assert hashlib.sha256((archives / requested).read_bytes()).hexdigest() == digest
        # A local publication error must fail, never fall back to downloading.
        (archives / requested).unlink()
        failing_copy = binaries / "cp"
        failing_copy.write_text("#!/bin/sh\nexit 74\n")
        failing_copy.chmod(0o755)
        result = subprocess.run(["bash", str(driver_path), str(fixture), digest],
                                env=tool_env, capture_output=True, text=True)
        assert result.returncode == 1, (kind, result.returncode, result.stderr)
        assert not marker.exists(), (kind, "publication error invoked a downloader")
        assert not (archives / requested).exists()
        assert not list(archives.glob("*.tmp.*")), kind


server = http.server.HTTPServer(("127.0.0.1", 0), Origin)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    with tempfile.TemporaryDirectory(prefix="cache-network-hits-", dir=scratch) as tmp:
        work = pathlib.Path(tmp)
        shared = work / "shared cache"
        env = dict(os.environ, CPKT_DEPENDENCY_CACHE=str(work / "ignored env cache"))
        url = f"http://127.0.0.1:{server.server_port}"
        helper = repo / "cmake/CpktDependencyArchiveCache.cmake"
        prelude = (
            f'include("{helper}")\n'
            "cpkt_initialize_dependency_cache()\n"
            "set(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT 5)\n"
            "set(CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT 5)\n"
            "set(CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT 5)\n"
            "set(CPKT_DEPENDENCY_DOWNLOAD_RETRIES 1)\n"
        )

        def acquire(name, endpoint):
            return prelude + (
                f'cpkt_acquire_dependency_archive(result NAME "{name}" '
                f'SHA256 "{digest}" URLS "{url}/{endpoint}")\n'
                'file(SHA256 "${result}" actual)\n'
                f'if(NOT actual STREQUAL "{digest}")\n'
                'message(FATAL_ERROR "cache returned corrupt bytes")\nendif()\n'
            )

        # A genuine miss downloads once; an explicit CMake cache wins over env.
        configure(work / "original", acquire("original.tar.gz", "fixture.tar.gz"), env, shared)
        assert requests == ["/fixture.tar.gz"], requests
        assert not (work / "ignored env cache").exists()
        shutil.rmtree(work / "original")
        configure(work / "extracted", acquire("renamed.tar.gz", "must-not-fetch"), env, shared)
        assert len(requests) == 1, requests

        # Reject a corrupt requested alias, reusing valid bytes at the same hash.
        entry = shared / "archives/sha256" / digest
        (entry / "renamed.tar.gz").write_bytes(b"corrupt")
        configure(work / "repaired", acquire("renamed.tar.gz", "must-not-fetch"), env, shared)
        assert len(requests) == 1, requests

        # A complete corrupt entry is a miss and must fetch/verify fresh bytes.
        for path in entry.iterdir():
            path.write_bytes(b"corrupt")
        configure(work / "corrupt", acquire("original.tar.gz", "fixture.tar.gz"), env, shared)
        assert requests == ["/fixture.tar.gz", "/fixture.tar.gz"], requests

        # Use the real test-dependency module with tiny, checksum-pinned test
        # inputs. Substitute only fixture digests, origin, and archive layout;
        # retain acquisition/cache selection and imported-target construction.
        modules = work / "modules"
        modules.mkdir()
        shutil.copyfile(helper, modules / helper.name)
        pslog = (repo / "cmake/CpktTestPslog.cmake").read_text()
        pslog = re.sub(r'"[a-f0-9]{64}"', f'"{digest}"', pslog)
        pslog = pslog.replace(
            '"https://github.com/sa6mwa/libpslog/releases/download/v${version}/${name}.tar.gz"',
            f'"{url}/must-not-fetch"',
        )
        pslog = pslog.replace('set(prefix "${root}/${name}")',
                              'set(prefix "${root}/libpslog-0.11.0-fixture")')
        (modules / "CpktTestPslog.cmake").write_text(pslog)
        targets = (
            "x86_64-linux-gnu", "x86_64-linux-musl", "aarch64-linux-gnu",
            "aarch64-linux-musl", "armhf-linux-gnu", "armhf-linux-musl",
            "arm64-apple-darwin",
        )
        for target in targets:
            source = work / "source archives" / target
            body = prelude + (
                f'set(CPKT_TARGET_ID "{target}")\n'
                f'include("{modules}/CpktTestPslog.cmake")\n'
                "cpkt_add_test_pslog()\n"
                f'if(NOT CPKT_DEPENDENCY_CACHE STREQUAL "{shared}")\n'
                'message(FATAL_ERROR "test dependency replaced shared cache")\nendif()\n'
                'get_target_property(location cpkt_test_pslog IMPORTED_LOCATION)\n'
                'if(NOT EXISTS "${location}")\n'
                'message(FATAL_ERROR "test archive was not extracted")\nendif()\n'
            )
            configure(source, body, dict(env, CPKT_DEPENDENCY_CACHE=str(shared)))
            assert not (source / ".cache").exists(), source
            assert len(requests) == 2, (target, requests)

        # XDG resolution also survives changed source/build directories.
        xdg = work / "xdg"
        xdg_shared = xdg / "cpkt/deps"
        shutil.copytree(shared, xdg_shared)
        xdg_env = dict(env, XDG_CACHE_HOME=str(xdg))
        xdg_env.pop("CPKT_DEPENDENCY_CACHE", None)
        configure(work / "xdg-source", acquire("xdg.tar.gz", "must-not-fetch"), xdg_env)
        assert len(requests) == 2, requests

        # A partial unpublished download is never a verified cache hit.
        for path in entry.iterdir():
            path.unlink()
        (entry / ".archive.part-test").write_bytes(data)
        configure(work / "partial", acquire("original.tar.gz", "fixture.tar.gz"), env, shared)
        assert len(requests) == 3, requests
        check_toolchain_caches(work)
finally:
    server.shutdown()
    thread.join()
    server.server_close()

print("verified cache hits made zero HTTP requests across names, all seven test-package targets, Bootlin, MIG, and both AFL++ resolvers")

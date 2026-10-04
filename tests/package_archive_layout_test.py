#!/usr/bin/env python3
"""Reject SDK entries outside the declared archive root before extraction."""

from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import os
import shutil
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from cpkt_operation import operation_fds


if not __debug__:
    raise SystemExit("Archive layout tests require Python assertions enabled")
source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="package-layout-", dir=source / "build") as tmp:
    root = Path(tmp)
    stem = "cpkt-1.2.3-x86_64-linux-gnu"
    archive = root / (stem + ".tar.gz")
    (root / "cpkt-1.2.3-CHECKSUMS").write_text("")
    for entry in ["other-root/", "loose.txt", stem + "/../escape.txt"]:
        with tarfile.open(archive, "w:gz") as output:
            info = tarfile.TarInfo(stem + "/")
            info.type = tarfile.DIRTYPE
            output.addfile(info)
            output.addfile(tarfile.TarInfo(entry))
        result = subprocess.run([
            "bash", str(source / "scripts/run-package-assertions.sh"),
            "-DCPKT_ARCHIVE=" + str(archive),
            "-DCPKT_TARGET_ID=x86_64-linux-gnu", "-DCPKT_BUNDLE_VERSION=1.2.3"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,pass_fds=operation_fds() if 'CPKT_OPERATION_FD' in os.environ else ())
        assert result.returncode != 0, result.stdout
        assert "package archive contains entry outside its root" in result.stdout, result.stdout
    for entry in ["include/pslog.h", "lib/libpslog.a", "lib/libpslog.so.0",
                  "lib/cmake/pslog/pslogConfig.cmake", "lib/pkgconfig/pslog.pc"]:
        with tarfile.open(archive, "w:gz") as output:
            info = tarfile.TarInfo(stem + "/")
            info.type = tarfile.DIRTYPE
            output.addfile(info)
            output.addfile(tarfile.TarInfo(stem + "/" + entry))
        result = subprocess.run([
            "bash", str(source / "scripts/run-package-assertions.sh"),
            "-DCPKT_ARCHIVE=" + str(archive), "-DCPKT_TARGET_ID=x86_64-linux-gnu",
            "-DCPKT_BUNDLE_VERSION=1.2.3"], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,pass_fds=operation_fds() if 'CPKT_OPERATION_FD' in os.environ else ())
        assert result.returncode != 0, result.stdout
        assert "release archive must not contain test-only libpslog" in result.stdout, result.stdout
    verifier = root / 'verifier'
    verifier.mkdir()
    shutil.copytree(source/'scripts', verifier/'scripts')
    shutil.copytree(source/'cmake', verifier/'cmake')
    environment = {k:v for k,v in os.environ.items() if not k.startswith('CPKT_OPERATION_') and k != 'GROUP'}
    archive = root / "cpkt-1.2.3.tar.gz"
    for entry in ["..", "../escape.txt", "cpkt-1.2.3/../escape.txt"]:
        with tarfile.open(archive, "w:gz") as output:
            output.addfile(tarfile.TarInfo(entry))
        result = subprocess.run([
            "bash", str(verifier / "scripts/source-archive-verify.sh"),
            str(archive), "1.2.3"], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,env=environment)
        assert result.returncode != 0, result.stdout
        assert "source archive contains unsafe entry" in result.stdout, result.stdout
print("[test] package archive layout rejection passed")

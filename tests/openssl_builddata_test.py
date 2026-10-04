"""Exercise the pinned OpenSSL build metadata generator and patch idempotence."""

import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile

repo = pathlib.Path(sys.argv[1]).resolve()
source = pathlib.Path(sys.argv[2]).resolve()
scratch = pathlib.Path(sys.argv[3]).resolve()
scratch.mkdir(parents=True, exist_ok=True)


def generate(info, marker, replacements, cwd):
    recipe = []
    collecting = False
    for line in info.read_text().splitlines():
        if line.startswith(marker):
            collecting = True
            line = line.split("=", 1)[1]
        if collecting:
            recipe.append(line.rstrip("\\").strip())
            if not line.endswith("\\"):
                break
    assert recipe, f"missing generator recipe: {marker}"
    args = shlex.split(" ".join(recipe))
    for old, new in replacements.items():
        args = [arg.replace(old, new) for arg in args]
    result = subprocess.run(
        ["perl", *args], cwd=cwd, capture_output=True, text=True, check=True
    )
    assert not result.stderr, f"OpenSSL metadata emitted warnings: {result.stderr}"
    return result.stdout


with tempfile.TemporaryDirectory(prefix="openssl-builddata-", dir=scratch) as tmp:
    work = pathlib.Path(tmp)
    (work / "util").mkdir()
    (work / "exporters").mkdir()
    for name in (
        "build.info", "exporters/build.info", "util/mkinstallvars.pl", "util/mkbuildinf.pl"
    ):
        shutil.copyfile(source / name, work / name)
    command = [
        "cmake",
        f"-DOPENSSL_SOURCE_DIR={work}",
        "-P",
        str(repo / "cmake/patch_openssl_buildinfo.cmake"),
    ]
    subprocess.run(command, check=True)
    patched = {path: path.read_bytes() for path in work.rglob("*") if path.is_file()}
    subprocess.run(command, check=True)
    assert all(path.read_bytes() == data for path, data in patched.items())

    common = {"$(VERSION)": "test-version", "$(LIB_EX_LIBS)": "-ldl -pthread"}
    build_data = generate(
        work / "build.info", "GENERATE[builddata.pm]=",
        {**common, "$(SRCDIR)": "."}, work,
    )
    (work / "builddata.pm").write_text(build_data)
    subprocess.run(
        [
            "perl", "-I.", "-Mbuilddata", "-e",
            "for my $path (@OpenSSL::safe::installdata::LIBDIR, "
            "@OpenSSL::safe::installdata::libdir, "
            "@OpenSSL::safe::installdata::PKGCONFIGDIR, "
            "@OpenSSL::safe::installdata::CMAKECONFIGDIR) "
            "{ die qq(unexpected build directory: $path) if $path ne $ARGV[0]; }",
            str(work),
        ],
        cwd=work,
        check=True,
    )
    install_data = generate(
        work / "exporters/build.info", "GENERATE[../installdata.pm]=",
        {
            **common,
            "$(INSTALLTOP)": "/opt/cpkt",
            "$(LIBDIR)": "lib",
            "$(libdir)": "/opt/cpkt/lib",
            "$(ENGINESDIR)": "/opt/cpkt/lib/engines-3",
            "$(MODULESDIR)": "/opt/cpkt/lib/ossl-modules",
            "$(PKGCONFIGDIR)": "/opt/cpkt/lib/pkgconfig",
            "$(CMAKECONFIGDIR)": "/opt/cpkt/lib/cmake/OpenSSL",
        }, work / "exporters",
    )
    (work / "installdata.pm").write_text(install_data)
    subprocess.run(
        [
            "perl", "-I.", "-Minstalldata", "-e",
            "die 'wrong install libdir' if "
            "$OpenSSL::safe::installdata::LIBDIR[0] ne '/opt/cpkt/lib'; "
            "die 'wrong install pkg-config directory' if "
            "$OpenSSL::safe::installdata::PKGCONFIGDIR[0] ne '/opt/cpkt/lib/pkgconfig'; "
            "die 'wrong install CMake directory' if "
            "$OpenSSL::safe::installdata::CMAKECONFIGDIR[0] ne '/opt/cpkt/lib/cmake/OpenSSL';",
        ], cwd=work, check=True,
    )
    incomplete = subprocess.run(
        ["perl", "util/mkinstallvars.pl", "PREFIX=."],
        cwd=work, capture_output=True, text=True,
    )
    assert "No value given for" in incomplete.stderr, "missing-input warnings were hidden"

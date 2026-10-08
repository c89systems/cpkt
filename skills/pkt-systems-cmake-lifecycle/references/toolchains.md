# CPKT Toolchain Reference

This lifecycle owns C and C++ compiler resolution for pkt.systems C/CMake projects. Linux builds use only complete, pinned compiler collections cached outside the checkout. A Linux target must not select a host-installed GCC, Clang, linker, binutils collection, libc, headers, or C++ runtime.

## Compiler Policy

- Every ordinary Linux build uses the pinned Bootlin GCC collection for its target. Its triple-prefixed `gcc`, `g++`, `ld`, `ar`, `ranlib`, `strip`, `nm`, `objcopy`, `objdump`, `addr2line`, `gdb`, and `readelf`, plus its sysroot libc and headers, are one inseparable collection.
- Do not use `/usr/bin/cc`, `gcc`, `clang`, distro cross compilers, or unpinned compiler paths as a fallback. A cached Bootlin collection is the only Linux default.
- On a Linux host, cross-building `arm64-apple-darwin` uses a local, project-pinned osxcross collection and the separately pinned Linux-host MIG helper. The lifecycle must not download Apple SDKs or Darwin compiler collections.
- Native macOS builds use the selected Xcode/Apple Clang compiler, SDK and tools via `xcrun`; they do not use Linux osxcross wrappers, Linux-host MIG or the Linux Valgrind/AFL++ gates. Follow the same native selection in local and hosted builds; see [github-actions.md](github-actions.md).
- cpkt owns the pinned PureDarwin-derived Linux-host `mig` and `migcom` helpers. The resolver builds them in the shared cache only for Linux-host Darwin cross builds; they are neither workstation package-manager prerequisites nor SDK payloads.
- Native memory checking uses host-provided Valgrind against executables compiled by the selected Bootlin collection. It is a required gate on the native x86_64 Linux host, but it is not an MSan substitute. Never run Valgrind through cross-compilation, an emulator, or QEMU.
- Native fuzzing uses a pinned cached AFL++ release built with Bootlin and its matching x86_64 GCC plugin headers. AFL++ compiler wrappers must delegate to the selected Bootlin `gcc`/`g++`; never use host GCC or Clang. Never run fuzzing through cross-compilation, an emulator, or QEMU.
- LLVM/Clang is installed by the workstation operator outside all cpkt caches and artifacts. Host `clang`/`clang++` support osxcross; `clang-format` and `clangd` are native development tools. None may enter Linux CMake compiler or linker discovery. `clangd` validation is a native development-host editor gate: register and run it only against the native host compile database. Cross-target CTest, package, and release configurations must not invoke it or rely on host `clangd` to emulate a target compiler or sysroot ABI; prove those targets through their selected compiler, supported target runner, and package verification gates.

## Linux Targets

These are the resolver-supported collections, not a required shipment set for
every consumer. cpkt requires all six; other projects select their release targets
under [operability.md](operability.md#lifecycle-spine).

| Target | Pinned Bootlin collection | Compiler prefix | Sysroot |
| --- | --- | --- | --- |
| `x86_64-linux-gnu` | `x86-64--glibc--stable-2026.08-1` | `x86_64-linux` | `x86_64-buildroot-linux-gnu/sysroot` |
| `x86_64-linux-musl` | `x86-64--musl--stable-2026.08-1` | `x86_64-linux` | `x86_64-buildroot-linux-musl/sysroot` |
| `aarch64-linux-gnu` | `aarch64--glibc--stable-2026.08-1` | `aarch64-linux` | `aarch64-buildroot-linux-gnu/sysroot` |
| `aarch64-linux-musl` | `aarch64--musl--stable-2026.08-1` | `aarch64-linux` | `aarch64-buildroot-linux-musl/sysroot` |
| `armhf-linux-gnu` | `armv7-eabihf--glibc--stable-2026.08-1` | `arm-linux` | `arm-buildroot-linux-gnueabihf/sysroot` |
| `armhf-linux-musl` | `armv7-eabihf--musl--stable-2026.08-1` | `arm-linux` | `arm-buildroot-linux-musleabihf/sysroot` |

The resolver pins each tarball SHA-256. Change a Bootlin pin only by updating its archive name, URL architecture, checksum, compiler prefix, and sysroot as one atomic lifecycle change.

## Cache Layout

Default root:

```sh
${CPKT_TOOLCHAIN_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/toolchains}
```

- `archives/` contains verified Bootlin, AFL++, and host MIG source tarballs.
- `roots/` contains extracted immutable compiler collections.
- AFL++ root names include the Bootlin collection and tooling build revision so different lifecycle helper revisions can coexist in the shared cache.
- `locks/` contains per-collection advisory lock files. Provisioning must hold the matching lock from its post-lock readiness check through publication; a waiting process must recheck readiness and never remove a root another process has already published. Use host `flock` for these lifecycle cache locks, with `CPKT_TOOLCHAIN_LOCK_TIMEOUT` (default `600` seconds) as the bounded wait.

The cache survives project cleans and is shared by all downstream pkt.systems projects. Do not create project-local compiler caches.

Explicit provisioning runs in one owned Bash job group, including nested ensure
calls. Cancellation signals that group, waits for teardown, and bounds escalation
before returning failure. Discovery/environment paths do not start this worker.

Before a download, reuse any archive in `archives/` whose verified SHA-256
matches the pin, even if its filename differs. A digest hit must make zero
network requests. Ignore temporary/unpublished files; verify copied bytes before
atomic publication under the requested name. Local copy/publication errors fail
provisioning instead of falling back to downloading the same bytes. Bootlin,
host MIG, and AFL++ share this rule through `scripts/cpkt-archive-cache.sh`.

## Resolver location

Use the activated skill's `scripts/` directory, or the project's explicitly
declared vendored helper directory. In this skill's source repository that is
`skills/pkt-systems-cmake-lifecycle/scripts`; an installed skill normally uses
`${CODEX_HOME:-$HOME/.codex}/skills/pkt-systems-cmake-lifecycle/scripts`.
Resolve it from the actual skill location when a different installation root
is in use. Do not assume a downstream checkout contains either `skills/` or
vendored copies under `scripts/`.

Set a shell variable `lifecycle_scripts_dir` to that resolved directory.
Keep the complete helper set together, including `require-host-bash.sh`, `cpkt-archive-cache.sh`,
`cpkt-toolchains.sh`, `cpkt-aflpp.sh` and `cpkt-afl-runtime.sh`; do not copy a resolver without its
sibling helpers. CMake examples below take the same directory through the
explicit `CPKT_LIFECYCLE_SCRIPTS_DIR` input (for example `-D` on configure).
This local tool-discovery hint must not enter installed/exported metadata.

## Provisioning

Use the lifecycle resolvers directly or vendor their exact content into a downstream repository:

```sh
"$lifecycle_scripts_dir/cpkt-toolchains.sh" ensure all || exit 1
"$lifecycle_scripts_dir/cpkt-toolchains.sh" discover aarch64-linux-gnu || exit 1
resolved_toolchain_env=$("$lifecycle_scripts_dir/cpkt-toolchains.sh" env aarch64-linux-gnu) || exit 1
eval "$resolved_toolchain_env"

"$lifecycle_scripts_dir/cpkt-aflpp.sh" ensure || exit 1
resolved_aflpp_env=$("$lifecycle_scripts_dir/cpkt-aflpp.sh" env) || exit 1
eval "$resolved_aflpp_env"
```

`ensure all` downloads the six Linux Bootlin collections and reports Darwin osxcross status. It never installs an Apple SDK. `discover` reports all resolved paths, including the selected compiler, linker, binutils, sysroot, static GNU C++ runtime archives, and source. `env` emits shell exports only; it does not modify login-shell files.

Capture and check `env` output before evaluating it. `eval "$(resolver env)"`
can return success on failed discovery and leave stale compiler settings active;
never use that shape for resolver invocation. These preparation examples terminate
the calling script/session on failure rather than continuing with stale settings.

Both resolvers' `discover` and `env` are non-provisioning paths. Bootlin discovery
reports `status=missing` without installing; AFL++ discovery/environment fails with
the matching preparation command if Bootlin or AFL++ is absent/incomplete. Only
`ensure` provisions. Selected operations validate existing tools and refuse missing
prerequisites; ordinary unselected builds may ensure collections through their
authorized outer wrapper. Never call `ensure` solely for inventory reporting.

## Development-machine provisioning

Native macOS SDK builds that apply bundled dependency patches require GNU
`patch`. Install Homebrew `gpatch` (`brew install gpatch`) and keep `gpatch` on
`PATH`; the system BSD `patch` does not apply every bundled patch reliably.

These instructions establish the complete Debian/Ubuntu Linux development
workstation baseline used across pkt.systems Go work and C/CMake work. They
are workstation prerequisites, not SDK contents and not release-artifact
dependencies. They deliberately describe the required state rather than
shipping or invoking a machine-provisioning script from this skill. Use the
host package manager with explicit operator authorization where it needs
`sudo`; do not embed package-manager actions in ordinary project builds.

## Host Bash

The lifecycle helpers require host **Bash 4.4 or newer** for nounset-safe empty
arrays, dynamic file descriptors and `mapfile`. This host prerequisite does not
apply to unrelated consumers of the C89 SDK. Helpers reject an unsupported
shell before lifecycle discovery, cache work or native children.

On macOS, install Homebrew Bash and give it PATH precedence in the terminal
that runs Make and scripts (the system `/bin/bash` remains unchanged):

```sh
brew install bash
export PATH="$(brew --prefix bash)/bin:$PATH"
bash "$lifecycle_scripts_dir/require-host-bash.sh"
```

On Linux, include `bash` in the host package baseline (for example
`sudo apt-get install bash`), upgrading it if older than 4.4. The command above
prints the actual selected shell version and path. Installing a formula alone
does not select it; Make, operations and compiler/native launchers must inherit
that PATH. Bash is a host tool, never a cpkt SDK or toolchain cache product.

### Host packages

Refresh apt metadata and install this complete baseline as one transaction:

```sh
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  autoconf automake bash binutils bison bzip2 ca-certificates \
  cmake cpio curl default-jre-headless flex \
  fuse-overlayfs gawk git git-lfs help2man \
  libbz2-dev libcairo2-dev liblzma-dev \
  libssl-dev libtool libxml2-dev libx11-dev make \
  ninja-build patch perl pkg-config podman python-is-python3 \
  python3 python3-pip python3-venv qemu-user ripgrep slirp4netns texinfo \
  uidmap unzip uuid-dev valgrind wget xar xz-utils zip zlib1g-dev
```

This baseline supplies build/Autotools tools, native quality tools, source and
archive utilities, Java-based generators, containers with rootless networking,
QEMU cross-target runners, and headers needed to build osxcross and common
pkt.systems dependencies. Host tools are
workstation support only; they must never enter Linux C/C++ compiler discovery.
Provision the pinned Bootlin collections through the lifecycle resolver before
configuring every pkt.systems Linux C/C++ build.

The host `curl` command is used for downloads. cpkt builds libcurl
from its pinned source and ships its headers and libraries in the SDK; its
workstation baseline does not require host libcurl development files.

`valgrind` is a host package prerequisite. Install LLVM/Clang separately as
described below. Confirm the baseline host command surfaces before treating
the workstation as ready:

```sh
command -v bash git cmake ninja podman qemu-aarch64 valgrind
bash "$lifecycle_scripts_dir/require-host-bash.sh"
cmake --version
```

`cmake` must be at least 3.24 for pkt.systems components that require that
version. On older Ubuntu releases, install a compatible CMake through the
organization-approved host package source before continuing.

### Host LLVM and Clang

Install the latest **stable** upstream LLVM release on the development host,
outside repositories and outside `${CPKT_TOOLCHAIN_CACHE}`. The host installation
must supply `clang`, `clang++`, `clangd`, `clang-format`, and LLVM tools used by
osxcross. Do not make cpkt download, extract, cache, wrap, or package
LLVM/Clang. Distro packages may lag the upstream stable release; the unversioned
apt.llvm.org version 23 packages are a development snapshot as of 2026-09-23.

Before installation, check [LLVM's release page](https://llvm.org/) for the
latest stable release and [its official Linux x86_64 archive](https://github.com/llvm/llvm-project/releases).
As of 2026-09-23 this is `23.1.2`. For that release, the upstream archive is
`LLVM-23.1.2-Linux-X64.tar.xz` and its published SHA-256 is
`b5ed9675149cc837c282e9b6962c276c9fa62863d5b2f91537b60848552995b7`.
Use the matching published checksum or signature when a newer stable release
is selected; never reuse this checksum for another archive.

For the current example, install manually in a host-controlled versioned path:

```sh
LLVM_RELEASE=23.1.2
LLVM_ARCHIVE="LLVM-${LLVM_RELEASE}-Linux-X64.tar.xz"
LLVM_ROOT="$HOME/.local/opt/LLVM-${LLVM_RELEASE}-Linux-X64"
mkdir -p "$HOME/Downloads" "$HOME/.local/opt"
curl -fL "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_RELEASE}/${LLVM_ARCHIVE}" \
  -o "$HOME/Downloads/$LLVM_ARCHIVE" || exit 1
printf '%s  %s\n' \
  b5ed9675149cc837c282e9b6962c276c9fa62863d5b2f91537b60848552995b7 \
  "$HOME/Downloads/$LLVM_ARCHIVE" | sha256sum -c - || exit 1
if [ ! -e "$LLVM_ROOT" ]; then
  tar -xf "$HOME/Downloads/$LLVM_ARCHIVE" -C "$HOME/.local/opt"
fi
export PATH="$LLVM_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$LLVM_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$LLVM_ROOT/bin/clang" --version
"$LLVM_ROOT/bin/clang++" --version
"$LLVM_ROOT/bin/clangd" --version
"$LLVM_ROOT/bin/clang-format" --version
"$LLVM_ROOT/bin/llvm-config" --version
```

Stop if checksum verification fails; do not extract or use that archive. The
selected version must appear in every version output. Check that
`command -v clang` resolves under `LLVM_ROOT`, and check `ld64.lld` too if
using osxcross's `llvm` build flavor. Keep these exports in the shell sessions
that build or invoke osxcross; do not write LLVM paths into cpkt
artifacts or use this host Clang to build Linux targets. Record the host LLVM
version and installation path in workstation records. A newer stable release
requires a new host installation and fresh verification, not an automatic
cpkt cache update.

Install Go independently using the version policy of the Go components being
worked on. This baseline intentionally does not select, pin, or update Go.
Rootless Podman is a host package prerequisite for local container-backed e2e. The lifecycle does not provision a container engine into the cpkt toolchain cache or SDK; follow [podman-kube-e2e.md](podman-kube-e2e.md) for the project service contract.

### Developer identity and local source roots

Perform source checkouts and cross-toolchain builds as the regular developer
account, never as root. Global Git settings are operator-selected preferences,
not automatic lifecycle provisioning. Only if the engineer explicitly requests
these account-wide defaults, use:

```sh
git config --global init.defaultBranch trunk
git config --global log.showSignature true
```

The conventional non-repository locations are:

| Purpose | Conventional location |
| --- | --- |
| osxcross source checkout | `$HOME/src/osxcross` |
| osxcross arm64 collection | `$HOME/.local/cross/osxcross` |
| cpkt immutable toolchain cache | `${CPKT_TOOLCHAIN_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/toolchains}` |

Keep these outside all source repositories. They may be overridden with
workstation-local paths, but never written into source, generated packages, or
release artifacts.

### Darwin osxcross input and setup

This section applies to Linux-host Darwin cross builds. Native macOS uses its
selected Xcode SDK and tools; it does not provision osxcross or Linux-host MIG.

The Apple SDK is proprietary input supplied by the developer. Before Darwin
provisioning, the developer must sign in to Apple Developer Downloads with an
account entitled to obtain Xcode, download the approved Xcode archive manually,
and place it at a local path they control. For the currently used SDK source,
that archive is normally named `Xcode_26.4.1_Apple_silicon.xip`; accept an
equivalent locally extracted Xcode/SDK only when its SDK version is the
project-approved one.

Never put that archive in a repository, a cpkt dependency cache, an
SDK artifact, or a source archive. Do not place Apple credentials in scripts,
environment files, or source. The former workstation helper's authenticated
download pathway is not lifecycle policy: the developer supplies the Xcode
input manually. If it is absent, stop with an actionable prerequisite naming
the expected local archive or extracted SDK path. `xar`, `cpio`, `xz`, `bzip2`,
`libxml2` development headers, OpenSSL development headers, Python, the host
LLVM/Clang installation above, and the normal build tools are required to turn
the approved local Xcode input into osxcross's packaged `MacOSX*.sdk` input.

Provision osxcross as a regular developer user from a project-approved pinned
source revision. Keep its source under `$HOME/src/osxcross`, record the exact
revision and input SDK version in local workstation records, and publish only
the generated collection to `$HOME/.local/cross/osxcross` (or the path named by
`OSXCROSS_ROOT`). A moving `master` checkout is not a reproducible lifecycle
toolchain.

The provisioning sequence is:

1. Clone or fast-forward the approved osxcross revision; do not overwrite a
   non-checkout directory.
2. Use osxcross's SDK packaging helper on the developer-supplied Xcode archive.
   Place the resulting `MacOSX*.sdk.tar.{xz,bz2,gz}` package in the checkout's
   `tarballs/` directory. Reuse a matching existing package when present.
3. Build with the selected host LLVM `bin` first in `PATH`, and set
   `CC="$LLVM_ROOT/bin/clang"` and `CXX="$LLVM_ROOT/bin/clang++"` in that
   shell. The osxcross build defaults to host Clang when these variables are
   unset; its `llvm` build flavor additionally requires `ld64.lld`. Build the
   required architecture only. The current baseline is
   `ENABLE_ARCHS=arm64` and `OSX_VERSION_MIN=11.0`; use the packaged SDK's
   version as `SDK_VERSION`, set `UNATTENDED=1`, and set `TARGET_DIR` to the
   osxcross collection path.
4. Locate the generated `arm64-apple-darwin*-clang`, compile a trivial C
   program, and use `file` to prove the result is a 64-bit arm64 Mach-O
   executable. A compiler executable alone is not sufficient evidence that the
   workstation has a usable Darwin SDK.
5. Retain the developer-supplied Xcode archive by default. Successful packaging
   does not authorize deleting it; remove that input only when the engineer
   explicitly requests its deletion.

The Linux cross-build workstation must not configure a cpkt Darwin build until this
Mach-O smoke check passes. Do not distribute osxcross, the SDK package, the
Xcode archive, or their Apple license material with a cpkt SDK.

After osxcross can produce a Darwin arm64 Mach-O smoke executable, inspect its
installed compiler names. osxcross may install `arm64-apple-darwin25.4-clang`
without an `arm64-apple-darwin25-clang` major version alias. The resolver
selects the newest complete 25.x prefix by default. To pin a particular
installed prefix, set `CPKT_OSXCROSS_HOST` before provisioning and CMake
configuration; the example below shows an optional pin for a 25.4 toolchain.
`bison` and `flex` from the host baseline are required to build MIG. Provision
the host-side helper and verify the complete collection:

```sh
export OSXCROSS_ROOT="${OSXCROSS_ROOT:-$HOME/.local/cross/osxcross}"
ls "$OSXCROSS_ROOT"/bin/arm64-apple-darwin*-clang
# Optional repository pin: export CPKT_OSXCROSS_HOST=arm64-apple-darwin25.4
"$lifecycle_scripts_dir/cpkt-toolchains.sh" ensure arm64-apple-darwin
"$lifecycle_scripts_dir/cpkt-toolchains.sh" discover arm64-apple-darwin
```

`ensure arm64-apple-darwin` does not obtain Apple content. It uses the pinned
x86_64 Linux GNU Bootlin collection to build the pinned PureDarwin-derived MIG
as a static Linux host executable in the shared toolchain cache. `mig` invokes
the local osxcross target compiler to preprocess Darwin definitions. It is
build-only tooling: do not bundle it, its source, or its license in the SDK.
Require `discover` to report `status=ready`, the osxcross compiler/binutils,
`mig`, `migcom`, and the pinned `mig_revision` before configuring a Darwin
build. Run the reported `migcom` path with `-version` and confirm it prints
`cpkt-host-mig`; `file` should identify it as a static x86_64 Linux executable.
Keep the selected `CPKT_OSXCROSS_HOST` in the shell used for Darwin configure,
build, and package commands. A bare `mig` in `PATH` is not the readiness check:
use the resolver-reported pinned paths.

## CMake Setup

Resolve the collection before `project()` through a CMake toolchain file or a
compiler bootstrap module. The outer lifecycle wrapper handles permitted provisioning
before configure; selected operations require explicit prerequisite preparation.
This read-only discovery pattern refuses missing collections and sets every relevant
tool, not only `CMAKE_C_COMPILER`. Repeated toolchain/try-compile evaluation must not
trigger provisioning:

```cmake
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CPKT_LIFECYCLE_SCRIPTS_DIR)
function(project_configure_bootlin_toolchain target_id)
  if(NOT DEFINED CPKT_LIFECYCLE_SCRIPTS_DIR OR
      NOT EXISTS "${CPKT_LIFECYCLE_SCRIPTS_DIR}/cpkt-toolchains.sh")
    message(FATAL_ERROR "Set CPKT_LIFECYCLE_SCRIPTS_DIR to the resolved lifecycle scripts directory")
  endif()
  set(resolver "${CPKT_LIFECYCLE_SCRIPTS_DIR}/cpkt-toolchains.sh")
  execute_process(COMMAND "${resolver}" discover "${target_id}"
    RESULT_VARIABLE result OUTPUT_VARIABLE description ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect the pinned Bootlin toolchain: ${error}")
  endif()
  if(NOT description MATCHES "(^|[\r\n])status=ready([\r\n]|$)")
    message(FATAL_ERROR "Missing pinned Bootlin collection; prepare explicitly with: ${resolver} ensure ${target_id}")
  endif()
  foreach(key cc cxx ld ar ranlib strip nm objcopy objdump addr2line readelf sysroot root)
    string(REGEX MATCH "(^|[\r\n])${key}=([^\r\n]+)" match "${description}")
    if(NOT match)
      message(FATAL_ERROR "Bootlin resolver did not report ${key} for ${target_id}")
    endif()
    set(bootlin_${key} "${CMAKE_MATCH_2}")
  endforeach()
  set(CMAKE_C_COMPILER "${bootlin_cc}" CACHE FILEPATH "" FORCE)
  set(CMAKE_CXX_COMPILER "${bootlin_cxx}" CACHE FILEPATH "" FORCE)
  set(CMAKE_LINKER "${bootlin_ld}" CACHE FILEPATH "" FORCE)
  set(CMAKE_AR "${bootlin_ar}" CACHE FILEPATH "" FORCE)
  set(CMAKE_RANLIB "${bootlin_ranlib}" CACHE FILEPATH "" FORCE)
  set(CMAKE_STRIP "${bootlin_strip}" CACHE FILEPATH "" FORCE)
  set(CMAKE_NM "${bootlin_nm}" CACHE FILEPATH "" FORCE)
  set(CMAKE_OBJCOPY "${bootlin_objcopy}" CACHE FILEPATH "" FORCE)
  set(CMAKE_OBJDUMP "${bootlin_objdump}" CACHE FILEPATH "" FORCE)
  set(CMAKE_ADDR2LINE "${bootlin_addr2line}" CACHE FILEPATH "" FORCE)
  set(CMAKE_READELF "${bootlin_readelf}" CACHE FILEPATH "" FORCE)
  set(CMAKE_SYSROOT "${bootlin_sysroot}" CACHE PATH "" FORCE)
  set(CMAKE_FIND_ROOT_PATH "${bootlin_sysroot}" "${bootlin_root}" CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY CACHE STRING "" FORCE)
endfunction()
```

For a cross target, the enclosing toolchain file must additionally set `CMAKE_SYSTEM_NAME` to `Linux`, set the target processor, and use `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` before calling the function.

For AFL++ fuzzing, first configure the ordinary Bootlin x86_64 collection, then resolve the pinned AFL++ wrapper. The wrapper must export `AFL_CC`/`AFL_CXX` as the matching Bootlin drivers and `AFL_PATH` as the cached helper root before it invokes `afl-gcc-fast` or `afl-g++-fast`. Fuzzing is native x86_64 Linux-only: no cross target, emulator, or QEMU runner is permitted.

The cached runtime wrappers keep GCC's plugin-loading backends on that
collection's libc and C++ runtime without exporting library paths to host children.

An AFL++ CMake toolchain file must call the Bootlin setup before `project()`, then replace only the C/C++ compiler drivers with the resolver-reported wrappers. Keep the linker and all binary utilities from Bootlin:

```cmake
project_configure_bootlin_toolchain(x86_64-linux-gnu)
execute_process(COMMAND "${CPKT_LIFECYCLE_SCRIPTS_DIR}/cpkt-aflpp.sh" discover
  RESULT_VARIABLE result OUTPUT_VARIABLE description ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Unable to inspect prepared AFL++; run ${CPKT_LIFECYCLE_SCRIPTS_DIR}/cpkt-aflpp.sh ensure explicitly: ${error}")
endif()
foreach(key cc cxx helper root)
  string(REGEX MATCH "(^|[\r\n])${key}=([^\r\n]+)" match "${description}")
  if(NOT match)
    message(FATAL_ERROR "AFL++ resolver did not report ${key}")
  endif()
  set(afl_${key} "${CMAKE_MATCH_2}")
endforeach()
set(ENV{AFL_PATH} "${afl_helper}")
set(CMAKE_C_COMPILER "${afl_cc}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${afl_cxx}" CACHE FILEPATH "" FORCE)
```

Assert collection integrity in the downstream bootstrap: the C compiler's reported linker must be inside the selected compiler root, and its libc must be inside the selected sysroot. This prevents an accidental host linker or host libc from entering an otherwise cross-target build.

## C89 CMake Policy

For project-owned C89 targets, do not set `CMAKE_C_STANDARD` or `C_STANDARD` to C90. CMake's C90 mapping does not express this lifecycle's intended compiler invocation. Apply these options directly to each project-owned target, in this order:

```cmake
function(project_configure_c89_target target)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target} PRIVATE
      -std=c89
      -Wall
      -Wextra
      -Wpedantic
      -pedantic-errors)
  endif()
endfunction()
```

Keep this policy off Lua sources and known C99 dependency implementations that are deliberately hidden behind C89 facades. Add project-specific `-Werror` policy separately when required by the lifecycle quality gate.

## C89 Large Text Asset Policy

Do not work around C89's conservative source-line limits by splitting a
production text payload across string arrays and concatenating, appending, or
otherwise joining it at runtime. A shipped document, template, protocol body,
query, generated source fragment, or other large logical text value must be
available to production code as one already-contiguous byte sequence. Production
code must not need an allocation, copy, `strcat`, `snprintf`, loop, or helper to
assemble that logical value.

Use this asset pipeline instead:

1. Store the canonical text or document as a separately tracked file artifact.
   Do not encode the payload as a hand-maintained C string literal.
2. Use a CMake custom command or CMake-script generator to convert that file
   into a build-tree `.inc` file containing only comma-separated `0xNN` byte
   literals with no trailing comma: no declaration, braces, string literals, or
   other wrapper. Make the source asset, generator, and generated `.inc` explicit
   build dependencies of the consuming target. Read the asset as bytes: never
   normalize encoding, line endings, or a final newline. Wrap generated physical
   lines at 72 columns or fewer so the generated source is safely within
   conservative C89 limits.
   Generate to a unique temporary file beside the final output and atomically
   rename it only after success; each asset/configuration needs its own output
   path so a parallel build can never compile a partial or colliding `.inc`.
3. Include that generated file directly in a single project-owned C89 array,
   for example `static const unsigned char asset[] = {` followed by
   `#include "generated/asset.inc"` and `};`. When the public or internal API
   requires a C string, C-string mode is valid only when the canonical payload
   contains no NUL byte and must append exactly one generated trailing `0x00`
   after the byte-exact payload. Otherwise expose the array together with its
   explicit byte length.
4. Keep the generated array's storage lifetime sufficient for every consumer.
   Do not replace it with a deferred reconstruction cache or an accessor that
   materializes a combined string.
5. Reject an empty canonical payload in raw-byte mode: a zero-length array is
   not portable C89. C-string mode may represent an empty payload as the one
   generated `0x00` terminator. Keep generated `.inc` files private to the
   consuming implementation source; do not include them from public headers or
   install them. An exception requires an intentional shipped-artifact contract.
6. When the generated asset is needed to build shipped code, include its
   canonical asset and generator in every source archive and release manifest.
   Keep the build-tree `.inc` out of those payloads; an extracted source archive
   must regenerate it successfully without files from the original checkout.

The generator must preserve every canonical payload byte in order and produce
deterministic output. Test the generated asset against the canonical file for
exact payload length and content, preserve final-newline behavior, enforce the
generated-line limit, and separately test the one-byte generated terminator and
the no-NUL input rule when C-string mode applies. Test raw empty-payload
rejection, empty C-string output, and a clean parallel build to prove the
generator's dependency and atomic-publication contract. Extract the source
archive and prove it regenerates the asset before compiling the consumer.
Generate the same asset in two independent clean build trees and byte-compare
the `.inc` outputs to prove reproducible generated source. Test code may use
split literals or joins to construct fixtures, but that exemption never permits
the production implementation or a shipped deliverable to require reconstruction.

This rule does not prohibit an intentionally segmented product design, such as
a streaming transport whose API deliberately exposes chunks. Such an exception
must be explicit in the API and tests; it is not a line-length workaround.

## Static C++ Runtime Contract

Linux resolver output includes `libstdcxx_a` and `libgcc_a` from the same Bootlin collection. Use them when building SDK metadata for C++-implemented C facades:

- Do not merge GNU runtime archives into facade archives.
- Ship the selected runtime archives in the SDK.
- Make static metadata place facade archives before the selected runtime archives.
- Require a downstream final static link that uses C++ to consume the same runtime closure.

For Darwin, do not apply the GNU runtime archive contract. osxcross/Apple Clang generally uses `libc++`; package metadata must emit target-correct runtime and system flags.

## Verification

Run these checks after changing either resolver or the toolchain policy:

Set `project_root` to the validated current checkout and `skill_tests_dir` to the
activated skill's `tests/` directory. The CTest graph supplies repository-local
scratch under ignored `build/`; fixtures must not default to `/tmp`.
The resolver fixtures apply on Linux; AFL++ requires native x86_64 Linux.
Vendored helper changes need equivalent CTest registration against that copy.

```sh
bash -n "$lifecycle_scripts_dir/cpkt-toolchains.sh"
cmake -S "$skill_tests_dir" -B "$project_root/build/skill-validation" \
  -DSKILL_WORKSPACE_ROOT="$project_root"
ctest --test-dir "$project_root/build/skill-validation" -L resolver \
  --stop-on-failure --output-on-failure --no-tests=error
"$lifecycle_scripts_dir/cpkt-toolchains.sh" discover
```

For a changed pin, also run `ensure` and a configure/build using that target. For AFL++ changes, run `cpkt-aflpp.sh ensure`, compile a small target through the wrapper, and prove `afl-showmap` observes distinct execution paths.

Readiness checks resolve tool and runtime symlinks and require files to remain
inside the selected physical collection; headers/libc remain inside its sysroot.
Test escaping paths and in-collection aliases through discovery and environment
output. Invalid collections cannot report readiness or export compiler settings.

## Local execution with the selected libc

The complete Bootlin collection owns the runtime used to verify target code,
including on a native host. Classify executables by whether they are shipped,
not by build type: release-mode tests and in-tree examples are still local
verification artifacts.

Use one CMake helper to configure the collection ELF interpreter and private
runtime search paths on every non-shipped development executable: the local
CLI, unit/integration tests, compiled helpers, examples, benchmarks, fuzzers,
and instrumentation builds. Apply this to existing targets; do not create a
second test CLI. Native CTest, e2e scripts, and Make example targets execute
these binaries directly. Their project-built children select the same runtime
from their own ELF metadata. Fully static executables need no dynamic loader.
Foreign targets use their matching sysroot and declared runner when execution
is selected; do not apply a native loader path to foreign executables. Never
apply ELF flags to Darwin targets.

Build and inspect a tiny dynamically linked consumer during each Linux target's
standalone preflight, before dependency builds. Execute it for native targets
and cross targets with declared runtime coverage, using the same selected-loader
checks as SDK verification. Cross execution follows the
[runner contract](local-ci.md#cross-target-runner-contract): without that opt-in,
configure/build/link/inspect and report deferred runtime proof; do not require
QEMU. Once selected, missing runner/sysroot/configuration fails clearly.
For musl, its `libc.so` self-alias may report the guest ELF interpreter
path. Accept that alias only when it resolves inside the selected sysroot to the
exact selected loader; reject missing or escaping files, foreign runtimes,
and delivered libraries resolved outside the SDK prefix. Test both ARM widths
and native musl within the declared coverage, executing through QEMU only when
that runtime coverage is selected.

Keep these settings private to executable targets. Account for indirect
runtime dependencies: ELF DT_RUNPATH alone does not propagate to grandchildren;
local-executable DT_RPATH can provide this coverage. Include required compiler,
C++, and instrumentation runtimes where applicable. Check actual resolution;
a loader path or RPATH alone is not evidence of complete host-library exclusion.

Shipped libraries, executable artifacts, installed example sources, and exported
CMake/pkg-config metadata must not acquire collection-cache paths. An install
rule alone does not settle whether a target is shipped: prove that packaging
excludes local executables. Keep shared/static release behavior as defined by
the project and test actual release artifacts separately from local builds.

Temporary SDK verification consumers are also non-shipped executables. Configure
their CMake targets with the same helper; non-CMake/pkg-config verification links
use that helper's compiler/linker settings as well. Apply these settings only to
the generated verification project or its local build flags, never to installed
SDK metadata or example sources. Execute the resulting binaries directly.
Direct ELF interpreter selection preserves normal child exec and
`/proc/self/exe` behavior. Installed-archive verification may invoke its verified
loader explicitly for both resolution inspection and execution to control the
complete dependency search path. Keep this confined to verification; shipped
metadata and ordinary development execution retain direct ELF selection.

Do not export collection or project dependency library paths into native test
or example environments: even a bundled libcurl can contaminate a host child
process. Use private executable search paths instead. Host shells, Python, Git, ripgrep,
CMake, and Valgrind remain host programs. Do not intercept arbitrary execs or
introduce a process broker, container, or namespace solely to select libc.
Native AFL++ and Valgrind must be verified against the chosen target runtime.
Host tools do not establish the correctness of target-library runtime selection.

For Lua modules, the process loading the module owns the runtime. Prefer a local
embedding/interpreter executable built with this same policy. Changing module
linking cannot select its process's libc; host-only Lua tooling can remain host
programs. Installed-SDK consumers must compile with the selected collection and
run with its runtime without exporting local verification flags through the SDK.

Verify actual loaded runtime objects, direct and child execution, self-executable
behavior where used, independent host commands, Lua behavior, shared/static
consumers, and final artifact metadata. Add negative checks for missing or
mismatched runtimes and accidental host resolution. Record coverage limitations;
this is not hermetic execution or proof of older deployment-libc compatibility.
Ensure runtime checks also cover reduced/facade-only configurations; early
configuration returns must not bypass the checks. Execute runnable SDK consumers
as well as building and inspecting them, including direct-package metadata paths.
Run supported development/instrumentation configurations and the actual release
artifact checks. Measure workload changes caused by libc updates rather than
assuming unchanged performance.

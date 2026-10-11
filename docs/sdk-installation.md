# Installing cpkt SDK packages

Use the binary archive matching the exact OS, CPU and libc. Verify its SHA-256
against the release checksum manifest before extraction. Start from an empty
installation directory; do not overlay a previous SDK.

Core is published at <https://github.com/c89systems/cpkt>. Optional providers
are <https://github.com/c89systems/cpktdb> and
<https://github.com/c89systems/cpktmisc>. Their release versions are independent.
An optional package requires the exact core version, target and package ID
recorded in its `share/cpkt/packages/<group>.json` manifest.

Extract each selected archive as its own installation, retaining its archive root:

```sh
mkdir -p sdk
# ARCHIVE paths must refer to previously checksum-verified release bytes.
tar -xzf "$CORE_ARCHIVE" -C sdk
# Extract verified optional archives with their own roots; never overlay providers.
```

CMake package discovery performs native content and prerequisite validation.
Set `CMAKE_PREFIX_PATH` to the selected independent installation prefixes, and
`PKG_CONFIG_PATH` to their `lib/pkgconfig` directories. Cross builds also include
the verified target SDK prefixes in `CMAKE_FIND_ROOT_PATH`. Avoid mixing host
libraries into imported SDK targets. `find_package(CpktCore CONFIG REQUIRED)`
provides the validated `CpktCore_PREFIX`, `CpktCore_VERSION`, `CpktCore_TARGET_ID`
and `CpktCore_PACKAGE_ID`. Downstream packages validate their own payload and
match their prerequisite pin against that discovered core identity.
pkg-config cannot validate
payload identities; the supplied pkg-config example uses CMake to validate the
prefix before linking. Static and shared
facade APIs keep their existing library names and `<cpkt/...>` headers.

Each repository ships its own upstream notices under
`share/doc/cpkt/<group>/third_party/`. cmocka is delivered by core for downstream
tests and is never an implicit production-facade dependency.

Installed examples live under `share/doc/cpkt/<group>/examples/`. Their
`build-pkg-config.sh` scripts accept an output path followed by individual link
flags. The Lua example uses CMake's pkg-config module to parse flags. Set `CC` to the selected target
compiler and `CPKT_SDK_PREFIX` to the validated SDK prefix. For Darwin, pass the SDK library runtime path as one
argument, for example `"-Wl,-rpath,$CPKT_SDK_PREFIX/lib"`; CMake's automatic build
RPATH does not apply to these direct compiler links. These are local example
executables; SDK library install names remain relative.

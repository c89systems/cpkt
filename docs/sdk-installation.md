# Installing cpkt SDK packages

Use the binary archive matching the exact OS, CPU and libc. Verify its SHA-256
against the release checksum manifest before extraction. Start from an empty
installation directory; do not overlay a previous SDK.

Core is published at <https://github.com/c89systems/cpkt>. Optional providers
are <https://github.com/c89systems/cpktdb> and
<https://github.com/c89systems/cpktmisc>. Their release versions are independent.
Provider builds pin an exact core version, target, archive checksum and package ID
for reproducibility. Installed providers declare the core version they require;
the selected core must accept that compatibility baseline and match the target.
Each installation's own package ID and payload hashes remain independently validated.

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
provides the validated `CpktCore_PREFIX`, `CpktCore_VERSION`, `CpktCore_TARGET_ID`,
`CpktCore_PACKAGE_ID` and `CpktCore_COMPATIBLE_SINCE`. Downstream packages validate
their own payload and discover a compatible core independently of their build pin.
pkg-config cannot validate
payload identities; the supplied pkg-config example uses CMake to validate the
prefix before linking. Static and shared
facade APIs keep their existing library names and `<cpkt/...>` headers.

## Core compatibility

Request a compatibility baseline with ordinary CMake discovery:

```cmake
find_package(CpktCore 0.2.0 CONFIG REQUIRED)
# Optional consumer limit: at least 0.2.0, below 0.4.0.
find_package(CpktCore 0.2.0...<0.4.0 CONFIG REQUIRED)
```

Core accepts a baseline from `CpktCore_COMPATIBLE_SINCE` through its installed
version, inclusive. Consumer range bounds and `EXACT` remain enforced. A release
declares its oldest compatible baseline in `cmake/sdk-discovery.cmake`; a breaking
release advances that floor and makes any required library ABI-version changes.
Compatibility is not inferred from matching major or minor release numbers.
Development version `0.0.0` accepts only its own baseline.

For example, core 0.3.0 with floor 0.2.0 accepts a provider requiring 0.2.0.
Core 0.4.0 with floor 0.4.0 rejects that provider. Compatible upgrades preserve
the provider binaries; statically linked applications must relink to incorporate
updated core code. Verify compatibility with existing binaries and static
provider archives before extending the supported span.

Installed provider configs use `find_dependency(CpktCore <baseline> CONFIG REQUIRED)`
and check target identity. Exact core hashes and package IDs record build provenance;
they do not require consumers to select those same bytes. Existing provider archives
that enforce exact core identity require updated provider metadata before accepting
independent upgrades. pkg-config version constraints alone do not enforce core's
compatibility floor; use the provider's CMake validation before linking through
pkg-config, as the supplied examples do.

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

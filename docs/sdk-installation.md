# Installing cpkt SDK packages

Use the binary archive matching the exact OS, CPU and libc. Verify its SHA-256
against the release checksum manifest before extraction. Start from an empty
installation directory; do not overlay a previous SDK.

Core is published at <https://github.com/c89systems/cpkt>. Optional providers
are <https://github.com/c89systems/cpktdb> and
<https://github.com/c89systems/cpktmisc>. Their release versions are independent.
An optional package requires the exact core version, target and package ID
recorded in its `share/cpkt/packages/<group>.json` manifest.

Extract each selected archive into the same fresh prefix with its one root
component stripped. Validate all selected groups before CMake or pkg-config:

```sh
mkdir sdk
# ARCHIVE paths must refer to previously checksum-verified release bytes.
tar -xzf "$CORE_ARCHIVE" --strip-components=1 -C sdk
# If selected, extract the verified matching optional archive into sdk too.
python3 sdk/share/cpkt/validate-sdk.py --prefix sdk --groups core
# For core plus database use --groups core,db; for misc use core,misc.
```

CMake package discovery performs the same content and prerequisite validation.
Use the SDK prefix explicitly, keep all selected packages together, and avoid
mixing host libraries into imported SDK targets. pkg-config cannot validate
payload identities, so run the validator explicitly first. Static and shared
facade APIs keep their existing library names and `<cpkt/...>` headers.

Each repository ships its own upstream notices under
`share/doc/cpkt/<group>/third_party/`. cmocka is delivered by core for downstream
tests and is never an implicit production-facade dependency.

Installed examples live under `share/doc/cpkt/<group>/examples/`. Their
`build-pkg-config.sh` scripts accept an output path followed by individual link
flags. Set `CC` to the selected target compiler and `CPKT_SDK_PREFIX` to the
validated SDK prefix. For Darwin, pass the SDK library runtime path as one
argument, for example `"-Wl,-rpath,$CPKT_SDK_PREFIX/lib"`; CMake's automatic build
RPATH does not apply to these direct compiler links. These are local example
executables; SDK library install names remain relative.

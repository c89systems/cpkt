# cpkt: Core C89 SDK

[cpkt on GitHub](https://github.com/c89systems/cpkt) ships static and
shared libraries with strict C89 public interfaces, CMake packages, pkg-config
metadata, examples and complete third-party license notices.

## Delivered components

- openssl
- zlib
- nghttp2
- libssh2
- curl
- libxml2
- lua
- mqttc
- krb5
- cyrus-sasl
- openldap
- cmocka

Public headers keep the `<cpkt/...>` namespace and existing facade library names.
Shared-library ABI majors retain their existing identities. Repository release
versions are independent of those ABI majors.

See [Lua runtime execution policy](docs/lua-runtime-policy.md) for embedding
limits, coroutine behavior and native module ownership.

The libssh2 C89 facade uses SFTP attribute `flags` to select valid fields:
`SIZE` selects `filesize`, `UIDGID` selects `uid`/`gid`, `PERMISSIONS` selects
`permissions`, and `ACMODTIME` selects `atime`/`mtime`. Setters and `open_ex_r`
read only selected fields. Successful getters preserve flags and zero absent
fields; errors, EAGAIN and directory EOF leave caller attributes unchanged.

## Independent repositories

| Repository | Scope | Prerequisite |
| --- | --- | --- |
| [cpkt](https://github.com/c89systems/cpkt) | Common libraries and cmocka | None |
| [cpktdb](https://github.com/c89systems/cpktdb) | PostgreSQL, SQLite and iODBC | Pinned cpkt SDK |
| [cpktmisc](https://github.com/c89systems/cpktmisc) | OPC UA, PDF, audio and speech | Pinned cpkt SDK |

Database and misc do not depend on each other. Only core maintains `skills/`.
Each producer owns its source, dependency recipes, tests, notices, artifacts and
release cycle. A database or misc change never starts a core producer or runs
core's standalone test suites.

## Development

Host Bash >= 4.4 is required for the lifecycle helpers. On macOS, run
`brew install bash`, then `export PATH="$(brew --prefix bash)/bin:$PATH"`
before Make or scripts. On Linux, install/upgrade the host `bash` package.
Run `bash scripts/require-host-bash.sh` to verify the selected shell.
See [host setup](docs/build-lifecycle.md) for the scope and PATH requirement.

Run `make help` for the authoritative command index. The default local Debug
preset uses the pinned Bootlin GNU toolchain. Every Linux target uses its own
complete pinned compiler collection. LLVM/Clang, clangd, clang-format, Valgrind
and QEMU are host tools; Darwin cross builds require osxcross and its Apple SDK.
The configured concurrency limit remains eight locally and two on native Darwin.

```sh
make debug
make finalize-slice
make test-all
```

`finalize-slice` formats project-owned C and headers, runs the Debug tests and
native clangd checks, then asserts formatting. `format-check` is required after
the final edit and before each commit. `test-all` includes the relevant native
hardening and integration tiers. Cross-target and extracted-package consumers
remain part of the complete release gate.

## Distribution and release

All seven targets are retained: x86_64, aarch64 and armhf Linux with GNU and musl,
and arm64 Darwin. Each release ships:

- `cpkt-<version>-<target>.tar.gz` for each target;
- `cpkt-<version>-arm64-apple-darwin-smoke-test.zip`;
- `cpkt-<version>.tar.gz`, the independently reconstructed source archive;
- `cpkt-<version>-CHECKSUMS`, the exact upload inventory.

`make release` starts clean and runs the complete owning repository's required
gates against the final amended, signed commit and its exact lightweight tag.
A candidate rehearsal or unsigned build cannot satisfy that release gate.
Darwin release artifacts are built and verified locally through osxcross on Linux.
Native macOS GitHub Actions runs are manual, optional diagnostics and never block
release or supply release assets. Never push to trigger CI. Trunk and the release
tag are pushed only after the complete signed, tagged local release gate passes.

Shared dependency archives use
`${CPKT_DEPENDENCY_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps}`;
compiler collections use the corresponding `cpkt/toolchains` root. A verified
SHA-256 cache hit performs no network acquisition. `clean` removes disposable
repository state and preserves both shared caches.

The native build ownership and specialist validation boundaries are documented in [build lifecycle architecture](docs/build-lifecycle.md).

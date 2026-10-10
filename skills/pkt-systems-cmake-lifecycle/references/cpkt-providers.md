# Independent cpkt providers

Apply these contracts only when working on the cpkt SDK provider family or
consuming its packages. They do not define the lifecycle scope or require other
deliveries to use cpkt, split repositories, ship SDKs or adopt this target matrix.

The SDK family has three public repositories with independent release versions:

| Provider | Repository | Own payload | SDK prerequisite |
| --- | --- | --- | --- |
| `cpkt` | `https://github.com/c89systems/cpkt` | Core libraries and cmocka | None |
| `cpktdb` | `https://github.com/c89systems/cpktdb` | PostgreSQL, SQLite and iODBC | Exact pinned published `cpkt` SDK |
| `cpktmisc` | `https://github.com/c89systems/cpktmisc` | OPC UA, PDF, audio and speech | Exact pinned published `cpkt` SDK |

Only `cpkt` owns the authoritative `skills/` sources and the installer for both
lifecycle and review skills. Update those sources when producer, acquisition,
packaging or release contracts change. Review all affected references for
consistency before installing; change installed skills only through an explicitly
requested installation operation.

`GROUP=all` in one provider means that repository's complete payload. It cannot
build, test, clean, commit or release a sibling repository. Optional providers do
not require each other. Their core prerequisite comes from a checked-in exact
release/target/archive-SHA-256/package-ID pin, never a sibling checkout or host
libraries. Resolve imported component versions from the validated core manifest;
copied producer defaults are not the imported dependency selection.

Use each repository's inventory and `make help` for its actual commands. The
family's archive names are `<provider>-<version>-<target>.tar.gz`. Extract each SDK
as an independent installation, preserving its packaged layout and using its own
prefix. Never merge or overlay provider payloads into a shared prefix.
Discover prerequisites with ordinary `find_package`/`find_dependency` through
`CMAKE_PREFIX_PATH`, or pkg-config through `PKG_CONFIG_PATH`; consumers link the
provided imported targets rather than copying prerequisite files.
Optional archives contain only owned payload and identify the exact required core
version, target and package ID. Their own version need not match core. Preserve public
`<cpkt/...>` header names and library ABI identities through repository moves.
See [package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md)
and [packaging.md](packaging.md).

These provider assignments describe this SDK family, not a requirement to split
all downstream projects into three repositories.

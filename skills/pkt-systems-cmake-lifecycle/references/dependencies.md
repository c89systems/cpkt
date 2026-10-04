# Dependencies And Provenance

For initial summaries, explicit dependency questions and exact released
inventories, follow [dependency-reporting.md](dependency-reporting.md). Reporting
is read-only unless the task also authorizes dependency or documentation changes.

## Dependencies

Downstream projects consume external C dependencies as SDK bundles from
`cpkt`. A bundle producer acquires its own pinned upstream sources. Core `cpkt` has no
SDK prerequisite; `cpktdb` and `cpktmisc` import an exactly pinned published
`cpkt` SDK and build only their own upstreams/facades. No producer depends on
itself or acquires a sibling checkout as an SDK. Imported component versions
come from the validated provider manifest, not copied source defaults.
The lifecycle must support the applicable producer or consumer surfaces:

- Host debug dependency root.
- Host release dependency root.
- Cross dependency roots keyed by target ID.
- Separate dependency build roots and install roots.
- A shared verified archive cache plus repository-local disposable dependency state under `.cache/`.
- Clear dependency provenance in build metadata or manifests.
- Explicit failures for missing bundles, unsupported targets, checksum failures, or unavailable network fetches.
- Reuse downloaded SDK bundles and per-target dependency install roots across debug, release, hardening, e2e, fuzz, benchmark, and package builds.
- Bundled SDK mode, host dependency mode, and conservative auto mode when a project benefits from all three.

For implemented composable SDKs, select and validate the required package closure
using [package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md).
For producer reuse, apply its build-identity and completion-evidence rules even
when the project ships one package. Proposed layouts do not select current assets.

## Upgrade compatibility

Apply the repository's supported consumer and ABI boundary to dependency and
compiler-collection upgrades. Compare against the latest published artifacts,
not the previous development commit. Minor/patch version numbers and unchanged
SONAMEs alone do not establish compatibility. Check public ABI, bundled consumer
support, static link closure, package metadata, and deployment runtime requirements.
A newer collection can raise required glibc symbol versions without changing
`libc.so.6`; passing tests with that collection does not prove older-host support.

Proceed with compatible upgrades after verification. A breaking transition needs
a consequence analysis and explicit maintainer approval before changing the
supported contract. Record affected consumers, required rebuilds/deployment
changes, verification coverage, and release communication. Respect an explicitly
facade-only support boundary for embedded backends instead of inventing a support
commitment for their private or upstream interfaces.

## Upstream Components

The usual upstream project owner for pkt.systems C lifecycle dependencies is `github.com/sa6mwa/`. Unless a project explicitly documents another source, fetch dependency archives from each upstream project's GitHub Releases page, not from source checkouts, branch archives, local sibling repositories, package-manager mirrors, or generated artifacts copied between worktrees.

Known upstream components:

| Component | Upstream project | Release archive source |
| --- | --- | --- |
| `lonejson` | `https://github.com/sa6mwa/lonejson` | `https://github.com/sa6mwa/lonejson/releases` |
| `cai` | `https://github.com/sa6mwa/cai` | `https://github.com/sa6mwa/cai/releases` |
| `libpslog` | `https://github.com/sa6mwa/libpslog` | `https://github.com/sa6mwa/libpslog/releases` |
| `cpkt` | `https://github.com/c89systems/cpkt` | `https://github.com/c89systems/cpkt/releases` |
| `cpktdb` | `https://github.com/c89systems/cpktdb` | `https://github.com/c89systems/cpktdb/releases` |
| `cpktmisc` | `https://github.com/c89systems/cpktmisc` | `https://github.com/c89systems/cpktmisc/releases` |
| `lonehash` | `https://github.com/sa6mwa/lonehash` | `https://github.com/sa6mwa/lonehash/releases` |
| `liblockdc` | `https://github.com/sa6mwa/liblockdc` | `https://github.com/sa6mwa/liblockdc/releases` |
| `vectis` | `https://github.com/sa6mwa/vectis` | `https://github.com/sa6mwa/vectis/releases` |
| `libpid0` | `https://github.com/sa6mwa/libpid0` | `https://github.com/sa6mwa/libpid0/releases` |
| `liblql` | `https://github.com/sa6mwa/liblql` | `https://github.com/sa6mwa/liblql/releases` |

## Shared Verified Archive Cache

All external dependency archives use the shared cache below, including pkt.systems SDK bundles, third-party sources, and test-only dependencies:

```sh
${CPKT_DEPENDENCY_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps}
```

This is a sibling of the toolchain cache's default `.../cpkt/toolchains` root. `CPKT_DEPENDENCY_CACHE` may override only the dependency archive cache; it must not redirect toolchains, and `CPKT_TOOLCHAIN_CACHE` must not redirect dependencies.

Expose `CPKT_DEPENDENCY_CACHE` as a CMake `PATH` cache variable. Resolve it once, with an explicit `-DCPKT_DEPENDENCY_CACHE=...` taking precedence over the environment variable, then `XDG_CACHE_HOME`, then `$HOME/.cache`; do not rediscover it independently in each dependency declaration.

Use this layout:

```text
deps/
  archives/sha256/<expected-sha256>/<archive-name>
  locks/<expected-sha256>.lock
```

- The global cache holds verified immutable archives only. Do not put extracted trees, CMake build directories, install prefixes, package-manager state, generated headers, or dependency stamps there.
- Keep ordinary extraction, build, install, and stamp state under the consuming repository's `.cache/`, keyed only by the target ID and the dependency name. Do not put dependency-set IDs, toolchain IDs, version hashes, URLs, compiler metadata, build-option hashes, or other semantic cache IDs in repo-local path names. Explicit instrumented variants require separately owned state and contracts as described in [package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md). These roots are disposable, but deletion stays within the operation's owned scope; borrowed prerequisites are read-only. A later acquisition reuses the verified global archive without network access.
- Key each archive by its required SHA-256. Archive names are for diagnostics only; never accept an archive because its filename, component name, version, or URL happens to match.
- Before every cache reuse, calculate SHA-256 and compare it to the dependency's pinned expected digest. A corrupt entry is not a cache hit.
- A verified digest hit must make zero network requests, including HEAD requests, URL probes, or release-metadata refreshes. Reuse identical verified bytes even when the requested asset name or URL changes; names are diagnostic aliases, not cache identities. Ignore unpublished partial downloads when searching the digest entry.
- Source-archive reconstruction, test registration fixtures, temporary consumers, and fresh configurations retain the resolved shared archive cache. Empty extracted/build/install state does not imply an empty archive cache. Never replace it with `${CMAKE_SOURCE_DIR}/.cache/...` or a temporary directory for real packages; synthetic cache-contract fixtures may use isolated caches under `build/`.
- Forward an explicit cache override to nested configure/test commands instead of independently selecting another cache. Test-only status changes the install/package boundary, not download policy. Verified archives in a former local cache may seed the shared cache through the common acquisition helper before local state is cleaned.
- Serialize writers with a per-digest lock. The lifecycle serializes project operations, but independent downstream repositories can acquire the same shared cache concurrently.
- On a miss or corrupt entry, download to a uniquely named temporary file in the archive's final cache directory, verify the expected SHA-256, then atomically rename it to the final path. Never publish a partial download. Remove only the temporary or corrupt archive entry covered by the held lock.
- If a verified cache entry cannot be reused and the download fails, report the component, URL, expected digest, cache path, and download failure. Do not silently substitute a host package, a differently named file, an unpinned URL, or an unchecked archive.
- `make clean`, `make clean-dist`, `make prerelease`, `make release`, package targets, and normal dependency-clean targets must never remove this global archive cache. A project may offer an explicitly named, opt-in cache-prune command, but it must state that it affects all local pkt.systems projects sharing the cache and must never be a prerequisite of another lifecycle target.
- Never embed this global path in dependency manifests, generated CMake or pkg-config metadata, binaries, scripts, source archives, binary SDKs, or release artifacts.

Implement acquisition behind one project-owned CMake helper, for example `project_acquire_verified_archive()`, rather than letting each `FetchContent`, `ExternalProject`, or custom dependency builder download independently. The helper must accept a component identity, HTTPS URL, expected SHA-256, and output archive path. It must serialize writers with CMake `file(LOCK ...)`, hash an existing archive, download with `file(DOWNLOAD ... TLS_VERIFY ON)` only when needed, hash the temporary file explicitly against the expected digest, and use same-directory `file(RENAME ...)` publication. Do not rely on `EXPECTED_HASH` for this helper: a transfer or hash failure can terminate CMake before the helper removes its temporary path or tries a fallback URL. Consumers then extract or stage the returned archive into their repository-local `.cache/` tree. `FetchContent` and `ExternalProject` may consume that staged local result, but their default build-tree download cache is not the lifecycle cache.

Add executable cache-contract tests that prove: an initial miss downloads and publishes only a verified archive; deleting local extracted dependency state permits an offline cache hit; a corrupt cached archive is rejected and never extracted; concurrent acquisition does not expose a partial archive; `make clean` preserves the global archive; and package/privacy checks reject global-cache paths in released output.

Count requests to a local HTTP origin and assert zero requests on verified digest hits, including changed filenames/URLs and fresh source roots. Exercise real test-dependency acquisition as well as SDK/source acquisition, and run these checks in the early local gate before the expensive release matrix.

Rules for component downloads:

- Pin each release dependency by component name, version, target ID when target-specific, exact GitHub release asset URL, and SHA-256.
- Prefer release assets whose names encode component, version, and target ID. Source code archives generated automatically by GitHub are not SDK bundles unless the project explicitly declares them as supported release artifacts.
- Reuse the same release URL and SHA-256 through every lifecycle surface that consumes the dependency: debug, release, hardening, fuzz, benchmark, e2e, package, and downstream smoke tests.
- Dependency fetchers must fail with an actionable diagnostic when a component has no pinned release URL for the requested target, when the URL is not under the declared upstream release page, when the checksum is missing, or when checksum verification fails.
- Dependency manifests in released SDKs must record the exact release asset URL used for each bundled component, but must not record local cache paths or source checkout paths.

Rules:

- Do not vendor generated dependency installs into release source.
- Shared archive cache reuse is the default. Do not re-download a verified global archive when the requested SHA-256 already exists and verifies.
- Repo-local dependency builds and install roots are disposable local state. Unqualified global clean/release removes that state; selected cleanup/dependency operations remove only owned roots. Normal no-clean entrypoints use explicit stale-component detection for source, patches, output-affecting helpers/options, toolchain and layout. Repair only the stale owned component and affected dependency closure from verified archives; selected consumers fail instead of repairing borrowed prerequisites. Preserve matching unrelated components.
- Model each upstream as an independently buildable component. Keep its extraction/build root at `.cache/deps-build/<target-id>/<component>/`, its install root at `.cache/deps/<target-id>/<component>/install/`, and its contract at `.cache/dependency-contracts/<target-id>/<component>.txt`. Do not make façades, tests, examples, or a single component target depend on a universal all-dependencies target.
- Expose `cpkt_deps_<component>` targets and a Make entrypoint such as `make deps DEPENDENCY=<component> PRESET=<preset>` for a component closure. Direct dependency edges must build only the required transitive closure. Reserve `cpkt_deps_all` for intentional complete-SDK assembly and final release work.
- A component contract includes its recipe, output-affecting helper closure, pinned inputs, relevant options/toolchain state, and direct dependency contract identifiers. A bookkeeping-only change must not invalidate every component. Schema migration may adopt old state only after proving equivalent meaning and validating completed outputs; expected configure-time contracts alone are not success. Reject unknown/incompatible evidence. Later mismatches repair only stale owned components and affected dependents, subject to selected-operation boundaries.
- When dependency rebuilding is disabled, a stale component root must fail with an actionable diagnostic. Never delete caller-owned roots; require the caller to refresh them or remove the override.
- Compiler collection metadata may be recorded for diagnostics and package provenance, but it must not become a repo-local cache path component. For a pinned Bootlin build, diagnostics should include the Bootlin target ID, pinned collection release/root, and sysroot path; GCC version alone and `CMAKE_C_COMPILER_TARGET` are insufficient to describe the selected compiler collection.
- `scripts/deps.sh` refreshes stale owned component roots from verified global archives. A selected invocation must not delete/rebuild borrowed prerequisite roots or provision missing prerequisite tools. Stale local state must not be hidden behind longer path names.
- Do not leak dependency cache paths into package metadata, CMake config files, pkg-config files, binaries, scripts, or release archives.
- Imported CMake targets and pkg-config metadata must expose only the public dependency contract needed by downstream consumers.
- Static SDKs may require downstream consumers to provide dependency include and library roots; encode that clearly in CMake package config, pkg-config metadata, tests, and README examples.
- When a binary SDK bundles static third-party archives, the SDK owns the full static link interface for those archives. Ship relocatable CMake package configs and/or pkg-config files for every bundled library that downstream projects may link directly or transitively.
- Static imported targets must put private system and bundled-library requirements on the dependency target that needs them, not on downstream consumers or aggregate top-level targets. Examples: OpenSSL crypto owns `${CMAKE_DL_LIBS}` where DSO/dlfcn support needs it; libraries that use math own `m` where required; compression users declare zlib; thread users declare `Threads::Threads`.
- Prefer upstream-compatible CMake target names for bundled dependencies, such as `OpenSSL::SSL`, `OpenSSL::Crypto`, `CURL::libcurl`, `ZLIB::ZLIB`, `Libssh2::libssh2`, `nghttp2::nghttp2`, and `LibXml2::LibXml2`. Add project-namespaced aliases only when they clarify a bundle-specific variant without replacing the standard consumer path.
- Pkg-config metadata for static consumers must use `Requires.private` for dependencies that also ship `.pc` files and `Libs.private` for private system libraries or linker flags. Do not make consumers spell out `-ldl`, `-lm`, `-lz`, `-pthread`, framework flags, or similar workaround closures when those are requirements of bundled dependencies.
- Shared SDKs must not require bundled private static archives unless the project deliberately ships them as part of the artifact contract.
- Do not implement bespoke JSON parsers, serializers, tokenizers, compactors, escaping logic, or JSON framing code when the project declares `lonejson` as a dependency. `lonejson` owns all JSON parsing, serialization, validation, streaming, framing, escaping, and fixture normalization surfaces.
- When a project already declares `lonejson` and the task authorizes lifecycle consolidation of its legacy JSON handling, migrate that handling behind the dependency and test preservation of behavior. Do not add lonejson or migrate unrelated code merely because this skill is active.
- If the needed JSON behavior cannot be implemented through the project's declared `lonejson` dependency, stop and flag it to the engineer. Propose a change request for the missing capability instead of bespoke JSON behavior in that consuming project. This rule does not select a dependency for projects that do not declare it.
- Do not implement bespoke parsers, serializers, tokenizers, compactors, escaping logic, or framing code for a data format when the project declares a lifecycle component that owns that format.
- When the project declares the format-owning dependency and the task authorizes lifecycle consolidation, migrate legacy structured-data handling covered by it behind that dependency and test preservation of behavior.
- If the needed structured-data behavior cannot be implemented through the declared lifecycle dependency, stop and flag it to the engineer. Propose a change request for adding the missing capability to the owning dependency instead of implementing bespoke behavior in the consuming project.
- Exceptions to declared format-ownership rules require explicit engineer approval and documentation as a narrow non-parser use case, such as fixed fixture text or protocol examples not parsed/serialized by project code.
- Do not implement a bespoke structured logging subsystem when the project declares a pkt.systems logging dependency. Put logging behind a narrow adapter, keep it optional at the public API boundary, and test that disabling logging removes side effects.
- Logging dependencies must not leak into public headers unless the project deliberately accepts that type as part of the API. When a logger handle is accepted publicly, forward-declare it where possible and document that ownership stays with the caller.
- Host dependency modes must validate ABI-sensitive dependencies, not just headers. Wrong-ABI or partial host installs must fail with actionable diagnostics or fall back to bundled SDK mode when auto mode is explicitly supported.
- Package metadata must record logical dependency identity and ABI requirements, not local cache paths. CMake package config and pkg-config metadata should explain how static consumers supply external dependencies.
- For `cpkt` dependency bundles specifically, package metadata is part of the SDK product surface: every bundled library intended for downstream consumption must have CMake and pkg-config metadata in the released archive, and downstream pkt.systems projects should consume those targets instead of raw archive paths.
- Bundled SDK mode must pin per-target URL and SHA-256 for every dependency archive used by release builds.
- Downloads must use the shared verified archive cache: hash an existing global archive before reuse, retry transient download failures a bounded number of times, hash a temporary download before atomic publication, and extract only into a repository-local target-specific dependency root.
- Auto mode may choose host dependencies only when all required headers, libraries, package metadata, and ABI checks pass. Partial host installs must not shadow a valid bundled SDK configuration.
- Host mode must not require bundled SDK checksums for unsupported host target IDs.
- Preserve or deliberately deprecate existing dependency-mode names when a mature API or declared external-support commitment requires compatibility. For pre-1.0 non-ABI refactors without that commitment, make the clean cutover and do not add aliases unless requested. Published bundle/dependency compatibility and shared-library ABI obligations remain in force regardless of maturity.

Dependency provenance contract:

- When dependency provenance matters, ship a logical dependency manifest under `share/<project>/dependencies.json` inside each binary SDK archive.
- The manifest records dependency name, version, target ID, source system, source URL, SHA-256, archive name, license identifier, bundled/external status, and the logical install role used by the package.
- Manifest paths must be logical or artifact-relative. They must not contain local cache roots, source roots, build roots, temporary directories, or workstation-local paths.
- If a dependency is bundled in the SDK, include its license text or required notices under `share/<project>/licenses/` or `share/doc/<package>/licenses/`, and mark it as bundled in the manifest.
- If a dependency is an external requirement for static consumers, mark it as external in the manifest and ensure CMake/pkg-config metadata and README examples describe how the downstream consumer provides it.
- Package verification must validate the dependency manifest when present, verify it contains no local paths, and verify it agrees with CMake package and pkg-config metadata.
- Installed CMake config files should include helper validation modules needed by downstream consumers, such as ABI probes for dependency SONAME/install-name checks.
- Installed pkg-config files should use relocatable `prefix=${pcfiledir}/...` derivation that works for both `lib` and multiarch libdirs.
- In host dependency mode, build-tree metadata may include host hints needed for local tests. Redistributable binary SDK metadata must not contain local build/cache/source paths. If a host-only artifact deliberately records host paths, it is not a portable SDK and must be named and verified as such.

License and provenance contract:

- Every release artifact must include the project license in the appropriate artifact-local documentation location.
- Binary SDK archives include the project license under `share/doc/<package>/LICENSE`.
- Source archives include the project license at the archive root.
- Lua source packages and source rocks include the project license, version, release manifest, rockspec template or rendered rockspec, Lua sources, Lua C binding/facade code, required public C headers, and any license text required by included source.
- If a C SDK bundles third-party dependency headers, libraries, tools, or data, include required license and notice text for each bundled dependency under `share/<project>/licenses/` or `share/doc/<package>/licenses/`.
- If a dependency is only an external static-consumer requirement, list it as external in the dependency manifest. Do not copy its license into the C SDK unless redistribution or included source requires it.
- Source archives and Lua source packages must be staged from explicit inclusion manifests.
- Package verification must fail when the project license is missing, a bundled dependency license or notice is missing, the dependency manifest marks a dependency as bundled but no corresponding license/notice exists, generated/private files are included, or license/provenance metadata contains local paths.

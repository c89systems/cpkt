# Packaging, Runtime Paths, And Release Artifacts

## Packaging

Release artifacts are built under `dist/`.

Default target IDs:

- `x86_64-linux-gnu`
- `x86_64-linux-musl`
- `aarch64-linux-gnu`
- `aarch64-linux-musl`
- `armhf-linux-gnu`
- `armhf-linux-musl`
- `arm64-apple-darwin`, optional when the Darwin cross toolchain exists

Default single-package binary SDK naming:

```text
dist/<project>-<version>-<target-id>.tar.gz
```

For implemented composable SDK packages, use the repository's declared group
artifact inventory and common prefix instead. Follow
[package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md) for
payload ownership, prerequisite identities, acquisition and combination tests.
The independent cpkt family uses `cpkt-<version>-<target>.tar.gz`,
`cpktdb-<version>-<target>.tar.gz` and `cpktmisc-<version>-<target>.tar.gz`.
Each archive has its own provider/version/target root. Composition maps/strips
those roots into one chosen install prefix without replacing colliding files.
Schema-1 manifests live under `share/cpkt/packages/`; core owns the common
validator and family ownership catalog. Optional manifests bind the exact core
release, target and package ID while retaining their own independent version.
Consult each producer inventory and executable contracts. Implemented naming
does not certify unrun native, runtime or reconstruction gates.

Checksum manifest:

```text
dist/<project>-<version>-CHECKSUMS
```

The checksum manifest is the release upload manifest. It must be SHA-256, must list every artifact intended for the GitHub release, must itself be uploaded to the GitHub release, and must be the only source used to select non-manifest `gh release create` upload arguments.

When Lua artifacts exist, the final complete-release checksum manifest must include the standalone Lua source package, the rendered release rockspec, and the LuaRocks `.src.rock`, in addition to C binary and source archives.

Binary rehearsal uses an explicitly scoped binary manifest under `build/`, not
a partial replacement for `dist/<project>-<version>-CHECKSUMS`. Keep its entries
artifact-relative to `dist/` and give the verifier that base separately. Derive
the expected set from the selected project/version/target binary inventory,
not from an unrestricted glob. Check exact membership, bytes and every binary
layout/privacy/loader/dependency obligation for that scope. Existing identified
source distributions or artifacts for another identity/version remain out of
scope during warm rehearsal; do not delete them or let them satisfy its proof.
Unexpected/missing binary artifacts within the selected scope are failures.
Final clean `make release` assembles all surfaces and replaces the final manifest
only with the complete verified release set. A binary manifest cannot authorize
uploads or publication.

Selected package operations stage artifacts/checksums/evidence only in their owned
workspace under `build/`, leaving `dist/` and complete-release evidence unchanged.
They validate only that scope and its existing prerequisite archives; they do not
regenerate prerequisites. Binary-matrix publication to `dist/` invalidates previous
complete-release evidence before replacing payloads. Final release regenerates the
complete manifest after all required proof. Scoped manifest paths are relative to
their declared artifact base, whether `dist/` or selected owned staging.

Release artifact privacy and relocatability are hard packaging invariants, not cosmetic cleanup. Every artifact that can be uploaded or consumed must be free of workstation-local paths, including binary SDK tarballs, CLI tarballs, source archives, Lua source rocks, rockspecs, checksum manifests, smoke bundles, and nested archives inside package formats.

Optional artifacts:

```text
dist/<project>-<version>.tar.gz
dist/<project>-<version>.h.gz
dist/<project>-<version>-<target-id>-smoke-test.zip
dist/<project>-lua-<version>.tar.gz
dist/<project>-<version>-1.rockspec
dist/<project>-<version>-1.src.rock
```

Binary SDK archive contract:

These defaults describe a project's C SDK. A declared bundle producer also ships
its selected upstream runtime/development payload, including native Lua headers,
libraries or an interpreter when explicitly part of that bundle. The Lua exclusions
below concern downstream facade/source-rock payload and package-manager state;
they do not prohibit an intentionally bundled upstream Lua dependency. A composable
group archive contains only its owned subset; required closure is checked in the
selected installation combination, not by demanding prerequisite files in every
archive.

- The tarball's first and only top-level directory is `<project>-<version>-<target-id>/`.
- Every architecture target uses the same internal directory contract. File formats and suffixes vary by target, but paths do not.
- `include/` contains project public C headers only. Prefer `include/<project>/` for multi-header libraries and `include/<project>.h` only for deliberate single-header public APIs.
- `bin/` contains project-owned executables and CLI tools for that target, when the project ships runtime binaries.
- `lib/` contains project-owned target libraries:
  - static libraries: `lib<project>.a`;
  - Linux shared libraries: `lib<project>.so`, ABI symlinks such as `lib<project>.so.<major>`, and the real SONAME file when shared libraries are shipped;
  - Darwin shared libraries: `lib<project>.dylib` or versioned `.dylib` files when Darwin shared libraries are shipped.
- `lib/pkgconfig/<project>.pc` is present when pkg-config is supported.
- `lib/cmake/<project>/<project>Config.cmake` is present for CMake package consumers.
- `lib/cmake/<project>/<project>ConfigVersion.cmake` is present for CMake package consumers.
- `share/doc/<package>/LICENSE` contains the shipped license text.
- `share/doc/<package>/README.md` contains release-consumer documentation.
- `share/doc/<package>/examples/` contains installed examples only when examples are part of the SDK contract.
- `share/<project>/manifest.txt`, `share/<project>/package-metadata.*`, or `share/<project>/dependencies.*` may contain package metadata or dependency provenance when needed.
- Do not include generated build trees, dependency cache roots, package-manager build directories, local service state, test service volumes, fuzz corpora generated during release, benchmark logs, `.git`, `.env`, or private credentials.
- Do not include Lua runtime files, Lua rockspecs, Lua source modules, Lua package-manager state, or Lua C binding/facade source files in C binary SDK tarballs.
- Do not include benchmark harness internals, fuzz-only helpers, e2e fixtures, local dev-service config, vendored upstream worktrees, migration ledgers, or lifecycle diagnostics in C binary SDK tarballs unless the engineer explicitly defines them as product artifacts.
- Do not include third-party dependency headers or libraries unless the project explicitly ships a bundled SDK artifact. If bundled dependencies are shipped, their layout and license files must be specified and verified as part of the artifact contract.
- Do not include `$HOME`, source repository paths, build directories, dependency cache roots, package-manager temporary paths, parent-relative source provenance, absolute `file://` source URLs, or other workstation-local paths in any shipped file or metadata.

For each target archive, answer these questions in the package verification tests:

- Does `<project>-<version>-<target-id>/` exist as the only root?
- Are all public headers under `include/` and no private headers leaked?
- Are all target libraries under `lib/`, with correct static/shared names and symlinks for the target platform?
- Are all target executables under `bin/`, executable, and built for the archive target?
- Are CMake package files under `lib/cmake/<project>/` relocatable and free of build/cache paths?
- Is pkg-config metadata under `lib/pkgconfig/` relocatable and free of build/cache paths?
- Are all release-consumer metadata files free of `$HOME`, repository paths, build roots, dependency caches, package-manager temporary paths, and absolute local `file://` URLs?
- Are docs, license, examples, and metadata under `share/` only?
- Are downstream Lua facade/source-rock files and package-manager state absent from the C binary SDK, with any intentionally bundled upstream Lua payload matching its declared inventory?
- Are all runtime paths relocatable according to the runtime path invariant?
- Are forbidden generated or private files absent?

Package generation must:

- Clean `dist/` at the start of each required clean `make release` run, not in each per-target packaging recipe. Warm binary rehearsal/package entrypoints preserve identified out-of-scope artifacts and use the explicit binary manifest scope above. Final clean release still rejects stale/unlisted distribution artifacts before publication.
- Build each requested target from the correct preset and dependency root.
- Stage through `cmake --install`.
- Strip project-owned libraries only when the platform's final-byte and signing policy permits it; follow the Darwin rules below.
- For Darwin artifacts, prefer link/install-time metadata over package-time mutation. Build final Mach-O install names, dependency paths, and rpaths correctly before staging whenever the build system can do so.
- Treat absolute build, install, dependency-cache, toolchain, and package-manager paths in generated metadata or binary loader/debug metadata as release-blocking defects.
- Generate pkg-config files with `${pcfiledir}` or another relocatable prefix, never with the staging or install-time `CMAKE_INSTALL_PREFIX`.
- Generate CMake package files without source roots, build roots, dependency cache paths, or machine-local package-manager paths.
- Configure Darwin package builds with target-correct inspection and mutation tools when the toolchain provides them. Use mutation tools only for deliberate pre-finalization repair or when a following signing/verification step is available; do not treat post-package mutation as the normal relocatability mechanism.
- When a repository has `scripts/discover_target_tools.sh`, use its discovered tool values for package generation instead of duplicating tool lookup in packaging scripts.
- Produce deterministic tar/gzip output as far as the toolchain reasonably allows.
- Generate checksums after all artifacts are present.
- Use an explicit artifact manifest or narrow, version-qualified artifact patterns for checksum generation. Do not include build intermediates, package staging directories, or stale artifacts by accident.
- Package artifacts correctly in the first place. Do not rely on a sanitized repack step to hide local paths after generation, and do not rely on Darwin `install_name_tool` as a routine final-package cleanup step.

Final complete-release package verification must:

- Verify checksums.
- Verify `dist/<project>-<version>-CHECKSUMS` is present and is the only active checksum manifest for the release.
- Verify every checksum-listed artifact exists under `dist/`.
- Verify every checksum-listed artifact validates successfully.
- Scan the checksum manifest itself for local paths and absolute local `file://` URLs.
- Verify every release-looking artifact under `dist/` is listed in the checksum manifest unless explicitly ignored by a documented rule.
- Verify no stale release artifact for a different version remains under `dist/`.
- Verify deprecated checksum files such as `SHA256SUMS` are absent unless the project explicitly preserves them as compatibility artifacts.
- Verify `gh release create` arguments are derived from checksum-listed files plus the checksum manifest itself, never from a `dist/` glob.
- Extract every checksum-listed archive and recursively expand nested `.tar.gz`, `.tgz`, `.tar.xz`, `.zip`, `.rock`, and `.src.rock` payloads that are part of the released package format before privacy and relocatability scanning.
- Verify archive layout and single root.
- Verify required headers, libraries, CMake config, pkg-config file, docs, examples, and metadata.
- Verify generated version headers are installed and agree with package metadata.
- Verify forbidden bundled headers and libraries are absent unless explicitly part of the artifact contract.
- Verify install-tree CMake consumers for static and shared imported targets.
- Verify pkg-config consumers when pkg-config metadata is shipped.
- Verify example builds from installed examples when examples are shipped.
- Verify shared-library runpaths use relocatable paths and do not contain build roots.
- Verify each extracted project-owned shared library, facade, plugin, and
  module has exactly its source-controlled public dynamic-export set, using
  target-correct symbol inspection. Also run the negative downstream fixture
  that manually declares a known private sentinel and must fail to link.
  Installed headers being free of private declarations is not sufficient.
- Verify every shipped ELF executable and shared object that has runtime library lookup metadata uses `$ORIGIN`-relative RPATH/RUNPATH only.
- Verify no shipped Mach-O dynamic library or executable contains local build paths in install names or dependency paths; project-owned Darwin install names should be `@rpath`-relative.
- Verify no shipped Mach-O dynamic library, module, or executable contains non-system absolute dependency paths such as `/lib`, `/usr/local`, build roots, dependency cache roots, source roots, temporary directories, or home directories. `/usr/lib` and `/System/Library` are the normal allowed absolute system locations.
- Verify Darwin rpaths are relative loader paths such as `@loader_path` or `@executable_path` when runtime lookup metadata is needed; do not allow absolute non-system Darwin rpaths.
- Verify no shipped runtime loader metadata contains absolute non-system paths. ELF RPATH/RUNPATH and Darwin install names/dependency paths are release artifacts and must be inspected after extraction.
- Scan both text and binary files in extracted artifacts. Verify no hardening runtime, hardening symbols, debug-only paths, generated service state, package-manager build state, credentials, VCS metadata, dependency caches, `$HOME`, repository path, temporary build path, package-manager temporary path, absolute local `file://` URL, or other local path appears in artifacts.
- Verify shipped CMake config files give actionable errors for missing external dependencies and include any installed helper modules they call.
- Verify shipped pkg-config files are relocatable from their installed `lib/pkgconfig` or multiarch path and declare public/private dependencies correctly.
- For SDKs that bundle static dependency archives, verify extracted downstream consumers link through the shipped CMake imported targets and `pkg-config --static` metadata without consumer-supplied private workaround libraries such as `-ldl`, `-lm`, `-lz`, `-pthread`, platform frameworks, or raw transitive archive lists. The test should fail if removing the metadata would still pass because the consumer hard-codes the closure.
- Include negative regression fixtures or generated throwaway artifacts that prove the privacy gate fails on a repository path, `$HOME`, `file://$HOME`, `file://<repo>`, absolute local or non-system RPATH/RUNPATH, Darwin project-owned install names outside `@rpath`, Darwin absolute non-system dependency paths such as `/lib/libfoo.dylib`, and Darwin local dependency paths. These tests must inspect extracted release artifacts, not package staging directories.
- Verify target-tool discovery itself when cross-target artifacts are shipped. Package verification should prove the configured build directory, target ID, and fake or real toolchain resolve to the same target-correct tools used by package generation.

Per-target SDK smoke contract:

- `package-verify` must extract every declared target/group archive in its evidence scope into an owned workspace under `build/` and test the extracted SDK, not only the staging tree. The single-package default is `dist/<project>-<version>-<target-id>.tar.gz`; selected packages use owned staging. Composable SDKs require their declared installation combinations and prerequisite identities.
- Assert the extracted archive has exactly one root: `<project>-<version>-<target-id>/`.
- Assert `include/`, `bin/`, `lib/`, `lib/cmake/`, `lib/pkgconfig/`, and `share/` contents match the binary SDK archive contract.
- Use `file`, compiler target metadata, or target-specific inspection tools to verify shipped libraries and binaries match `<target-id>` when tooling is available.
- Compile a minimal C consumer against extracted public headers.
- Configure and build a minimal CMake consumer with `find_package(<project> CONFIG REQUIRED)`.
- Configure and build a minimal pkg-config consumer when `lib/pkgconfig/<project>.pc` is shipped.
- Link static and shared consumers when both static and shared libraries are shipped.
- For static pkg-config smoke tests, force an actual static link when the target toolchain supports it; otherwise document that the test is only validating metadata expansion, not static linkability.
- Run consumers and shipped CLI binaries when executable on the host or through an explicitly supported runner. Otherwise record compile/link proof and deferred runtime cases; this is local package readiness only. Required release runtime evidence must still pass through the declared native/runner gate; unsupported local execution cannot waive it.
- For CLI binaries, run `--version` or the project equivalent when execution is supported.
- Verify runtime paths after extraction.
- For shipped Darwin artifacts, inspect install names, dependency paths, rpaths, and code-signature load-command presence with the discovered target-correct `otool`. Optional Darwin targets may be skipped before packaging when the toolchain is unavailable, but a mandatory target or explicitly required runtime gate cannot be skipped. A packaged Darwin artifact must not skip Mach-O metadata verification. Run Darwin smoke bundles when the required runtime/toolchain exists. GitHub macOS Actions tests its native build in the same run; its archives are diagnostics, not release assets or proof of executing the local osxcross bytes. Hosted execution of local archives is additional work only when explicitly requested, not an automatic deferred release blocker; see [github-actions.md](github-actions.md).

Runtime path invariant:

- Shipped Linux and other ELF artifacts must never contain absolute RPATH/RUNPATH entries, dependency cache paths, build directories, source directories, temporary directories, or workstation-local paths.
- Prefer no RPATH/RUNPATH when the artifact does not need one.
- When runtime lookup metadata is needed, use `$ORIGIN` or `$ORIGIN/<relative-lib-dir>` so the artifact is relocatable inside the extracted SDK.
- Package verification must inspect every shipped executable and shared object with `readelf -d` or an equivalent tool and fail on absolute paths, local paths, hardening runtime dependencies, or non-relocatable runpaths.
- Discover ELF inspection tools from configured build state before `PATH` when cross targets are involved. Prefer configured values such as `CMAKE_READELF`, then target-prefixed sibling tools next to `CMAKE_C_COMPILER`, then `readelf` or an equivalent tool on `PATH`.
- Do not rely on CMake defaults for this. Set install/build RPATH policy explicitly for shipped targets and test the installed package tree, not only the build tree.

Darwin Mach-O invariant:

- The Darwin equivalent of ELF runtime metadata is Mach-O loader metadata: `LC_ID_DYLIB` for a dylib's own identity, `LC_LOAD_DYLIB` for dependencies, and `LC_RPATH` for runtime search paths. Treat these as release-critical ABI/runtime metadata.
- Shipped Darwin dylibs should have explicit `@rpath` install names that match the intended ABI/version policy. Versioned dylibs should normally keep the ABI-versioned install name rather than being rewritten to an unversioned name during packaging.
- Shipped Darwin dylibs, modules, and executables must not contain local or non-system absolute load paths. Allowed absolute paths are normally `/usr/lib/...` and `/System/Library/...`; paths such as `/lib/...`, `/usr/local/...`, build roots, dependency cache roots, source roots, temporary directories, and home directories are release-blocking defects.
- Prefer no Darwin rpath when an artifact does not need one. When it does, use `@loader_path`, `@loader_path/<relative-dir>`, `@executable_path`, or `@executable_path/<relative-dir>` according to the artifact type and packaged layout.
- Build dependencies for Darwin with clean `LC_ID_DYLIB` values before downstream targets link against them. The Darwin linker records dependency install names, so a bad dependency ID can leak into otherwise clean downstream artifacts.
- Avoid mutating final shipped Mach-O artifacts after link/install. `install_name_tool` and `strip` both mutate Mach-O files, and modern Darwin arm64 outputs may contain `LC_CODE_SIGNATURE`; mutation after signing or linker-emitted ad-hoc signing can leave a stale invalid signature.
- If Mach-O mutation is unavoidable for a shipped final artifact, the only acceptable order is: perform all `install_name_tool` changes, perform all stripping, sign or ad-hoc sign the final bytes when a signing tool is available, then verify the packaged/extracted artifact. If no signing tool is available, do not ship a final artifact whose existing `LC_CODE_SIGNATURE` was invalidated by mutation.
- Do not require Apple `codesign` for Linux/osxcross release hosts. Prefer producing correct Darwin loader metadata at build/link/install time and making packaging verify-only. On native macOS release hosts where `codesign` is available and deliberately part of the lifecycle, package verification should run signature verification after all mutations.
- Release builds should avoid debug metadata through build flags where practical. Do not strip Darwin final artifacts as a routine belt-and-suspenders step unless the package flow can re-sign and verify afterward.
- Package verification must inspect extracted final artifacts, not only build or staging trees, with `otool -D`, `otool -L`, and `otool -l` or equivalent target-correct tools.

Darwin tool discovery:

- Do not assume Darwin inspection tools are on `PATH`.
- The pkt.systems standard osxcross install location is `${OSXCROSS_ROOT:-$HOME/.local/cross/osxcross}`. With `CPKT_OSXCROSS_HOST` unset, resolve the newest complete Darwin 25.x prefix and use its reported tool paths. An explicit value pins an exact installed prefix. Do not invent major-version aliases such as `arm64-apple-darwin25-otool`; the installed tools may only have a `25.4` prefix. Follow [toolchains.md](toolchains.md#darwin-osxcross-input-and-setup).
- Do not assume Darwin linker tools are selected merely because `${host}-clang` is invoked by absolute path. Some osxcross clang wrappers still delegate to the first `ld` found in `PATH`; on Linux that can be `/usr/bin/ld`, which violates the basic cross-compilation invariant. Darwin configure, build, package-smoke, and installed-example link commands must run with `${OSXCROSS_ROOT}/bin` prepended to `PATH`.
- Darwin CMake toolchains must set `CMAKE_LINKER` to the resolver-reported absolute linker for the selected prefix and force executable, shared, and module links through `--ld-path=${CMAKE_LINKER}`. This applies when the pin variable is unset as well. Upstream dependency build systems that bypass CMake must receive equivalent environment, normally `PATH=${OSXCROSS_ROOT}/bin:$PATH` plus `LDFLAGS=--ld-path=${CMAKE_LINKER}` or an upstream-specific linker override. Do not use an absolute path with `-fuse-ld`; Clang reserves that option for linker flavor and deprecates path use.
- Package verification should include a linker-route regression when osxcross is available. It should dry-run or link a minimal Darwin executable and assert the route selects the resolved Darwin linker, not `/usr/bin/ld` or another host linker; cover discovery without a pin and an explicit prefix pin.
- Prefer configured CMake tool state when locating Darwin tools: `CMAKE_C_COMPILER`, `CMAKE_STRIP`, `CMAKE_INSTALL_NAME_TOOL`, `CPKT_OTOOL`, and `CMAKE_OTOOL` when present.
- Discover configured CMake tool state from the active preset build directory, normally by reading `CMakeCache.txt` or using non-mutating CMake cache introspection after configure. Tool lookup scripts should accept an explicit build directory or target ID so they inspect the same configured build that produced the package.
- Prefer a shared `scripts/discover_target_tools.sh` helper for this lookup so package generation, package verification, Darwin smoke bundles, and release privacy verification agree on the selected tools.
- The lookup order for each Darwin tool is:
  1. an explicit project-prefixed override variable, when the repository defines one;
  2. the configured CMake cache value for that tool;
  3. target-prefixed sibling tools next to `CMAKE_C_COMPILER`;
  4. unprefixed sibling tools next to `CMAKE_C_COMPILER`;
  5. `PATH` as the last fallback.
- For osxcross-style toolchains, derive the host prefix from `CPKT_OSXCROSS_HOST`, the configured compiler name, or the configured target host. If the compiler is `/path/to/toolchain/bin/<host>-cc` or `/path/to/toolchain/bin/<host>-clang`, check `/path/to/toolchain/bin/<host>-otool`, `/path/to/toolchain/bin/<host>-install_name_tool`, and `/path/to/toolchain/bin/<host>-strip` before checking unprefixed names.
- Darwin package verification must use the discovered `otool` to inspect install names, dependency paths, rpaths, and code-signature load commands. Darwin package generation must use discovered target-correct `install_name_tool` and `strip` only when mutation is deliberately required; host `/usr/bin/strip` or another non-target strip must not be used on cross-built Mach-O artifacts.
- Validate discovered Darwin tools before trusting them when practical, for example by checking they exist, are executable, and can inspect a generated target Mach-O artifact in the package verification workflow. Validate mutation tools only in workflows that intentionally mutate generated throwaway artifacts or pre-finalization build inputs.
- If a Darwin artifact is shipped and no target-correct `otool` can be found from configured overrides, CMake state, compiler siblings, or `PATH`, package verification must report an `external-tool-unavailable` diagnostic for that target instead of silently skipping Mach-O metadata verification. Missing `install_name_tool` or `strip` is not a blocker for verify-only Darwin packaging, but it is a blocker for any lifecycle step that intentionally mutates Mach-O artifacts.

Release privacy gate:

- `make package-verify` must include the privacy and relocatability gate for every checksum-listed artifact in its declared scope. Complete-release verification covers every final manifest payload; selected/binary verification cannot count as that complete proof.
- Projects may expose `make verify-release-privacy` as a focused alias, but it must not be the only place the privacy gate runs.
- The privacy gate must report the exact artifact and extracted file that leaked local material.
- It must fail on the current repository path, `$HOME`, `file://$HOME`, `file://<repo>`, absolute local or non-system RPATH/RUNPATH, Darwin project-owned install names outside `@rpath`, Darwin absolute non-system dependency paths such as `/lib/...` or `/usr/local/...`, and dependency paths pointing at local build trees.


## Source And Single-Header Artifacts

Source archives must be staged from an explicit manifest, not from an unfiltered repository copy. Include source, public headers, CMake, scripts needed to build from source, tests or smoke tests, examples, docs, license, an injected `VERSION` file for the non-git build path, and release manifest. Exclude generated output.

Source archive staging must write `RELEASE_MANIFEST` into the staged tree. In a git worktree, derive it from tracked, non-ignored files and add only deliberate generated release files such as source-archive `VERSION` and `RELEASE_MANIFEST`. `/VERSION` remains ignored in the repository and must not be treated as tracked release metadata. Outside git, require an existing release manifest instead of copying the whole tree.

Source archive verification must extract the tarball to a generated temporary directory, configure from the extracted tree, build, run the local tests that do not require unavailable external services, and verify the configured version, generated version header, CMake package metadata, pkg-config metadata, and archive `VERSION` agree. When the source archive is produced from a git worktree, verify the archive payload exactly matches the tracked non-ignored release manifest plus deliberate generated release files.

Binary `package-verify` validates already-produced binary artifacts and their checksum manifest; it must never trigger source-archive reconstruction. Keep source extraction, configure, build and test behind `package-source-smoke`. Automatic prerelease/matrix/package verification orchestration runs it only in clean `make release` after binary packages are available. An explicitly requested standalone source-smoke operation is also supported; its proof does not replace either required clean release run.

Source archives may carry release scripts and deterministic fixtures needed to rebuild and test the source package. They must not carry generated dependency archives, local `.env` files, package-manager state, service volumes, VCS metadata, or private review notes unless explicitly part of a public source distribution.

Source archives, source rocks, rockspecs, and single-header artifacts must not embed local source URLs, local checkout paths, build paths, package-manager temporary paths, or user home paths. LuaRocks release rockspecs must not use absolute `file://` URLs.

Lua source packages are source archives for the Lua facade, but they are separate artifacts from the C source archive. Stage them from an explicit Lua manifest, write an injected `VERSION` and `RELEASE_MANIFEST`, package them as `dist/<project>-lua-<version>.tar.gz`, include them in checksums, and verify them directly as well as through any `.src.rock` that embeds them.

Single-header artifacts, when present, must be generated from public header and implementation parts, formatted, version-stamped from the release version, compressed under `dist/`, and verified by decompressing and checking version macros plus a compile smoke test.


## Vendored Upstreams

If a project patches or builds a vendored upstream, put it behind a named lifecycle extension:

```text
vendor/<name>/upstream/
vendor/<name>/patches/
vendor/<name>/patches/series
```

Expose:

- `make vendor-<name>`
- `make vendor-<name>-apply`
- `make vendor-<name>-status`
- `make vendor-<name>-upgrade`
- `make build-<name>`
- `make verify-<name>-patches`

Rules:

- Apply patches only to a clean upstream checkout.
- `status` prints upstream revision, branch, and patch count.
- `upgrade` refuses dirty upstream state.
- Verification clones or copies the upstream to a temporary generated directory, applies the patch series, and builds it there.
- Vendored build output must not leak into release artifacts.

For each independent cpkt provider, `make package-source-smoke` creates its
source archive once and reconstructs that repository's native GNU Release product
from fresh local compiled state, using its effective configured job limit,
generator and shared digest cache. Eight is the family's local default/ceiling;
explicit lower limits are honored and native Darwin uses two. Never discover host
cores or substitute a host compiler or an unrelated cache. Optional reconstruction
acquires its exact pinned published core SDK from the shared verified archive cache;
it never reconstructs core or runs core's standalone suites.

Selected staging/checksums/verification stays under the owning group's build
namespace. Optional staging requires the exact pinned core archive and validated
payload; it cannot repair or replace that dependency implicitly. Each current
provider's binary scope has eight payloads (seven target tarballs and the Darwin
smoke ZIP); release scope adds one source archive. The complete release uploads
those nine payloads plus its checksum manifest. Derive and validate this set from
the owning inventory rather than hardcoding a family-wide aggregate release.

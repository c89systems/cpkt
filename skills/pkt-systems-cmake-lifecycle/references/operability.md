# Operability, Layout, And Command Surfaces

## Host shell prerequisite

For this modern helper collection (nounset-safe empty arrays, dynamic FDs and
mapfile), require host Bash >= 4.4. Use one builtin-only, Bash 3.2-parseable
startup helper before discovery, cache work, native children or mutation.
Reject unsupported shells with the actual version and actionable PATH setup.
Keep `set -u` and exact argv, including zero arguments versus one empty argument.
Test those behaviors, actual selected-shell provenance and stock macOS Bash
rejection on native workers. Do not impose this floor on unrelated SDK consumers.
See [toolchains.md](toolchains.md#host-bash).

## Tool ownership and simplicity

Use the simplest implementation that satisfies the project's observable contracts.
Build logic must be easy to follow and maintain. Prefer established tool features
and a direct command flow over frameworks, nested dispatchers, redundant wrappers
or multiple competing descriptions of the same build graph.

- **Make:** expose the public target vocabulary, simple prerequisites and short
  recipe commands. Keep variables and conditionals straightforward. Do not embed
  elaborate Bash programs, long quoted one-liners, nested loops, traps, heredocs
  or chains of `eval`/`$(shell ...)` in Makefiles. Move procedural work to Bash;
  avoiding Python must not make Makefiles complicated.
- **Bash:** handle procedural workflows, argument validation, command sequencing,
  target-matrix iteration, environment setup and cleanup. Use readable statements,
  arrays and small functions. A Make recipe should normally invoke an existing
  tool or a focused Bash script; avoid wrapper chains that only forward arguments.
- **CMake/CTest:** own configuration, compiler/toolchain selection, targets,
  dependency edges, generated outputs, incremental rebuild decisions, test
  registration/execution and installation. Use CMake scripts for suitable build
  and package operations. Invoke the configured build tool through CMake rather
  than duplicating its graph or freshness logic in another language.

Python3 is an exception, not the default build language. Before introducing or
expanding a Python helper, document its specific task and why the existing
Make/Bash/CMake or standard tools cannot handle it clearly and reliably. A
complex schema generator or structured binary/archive validation may justify
Python; routine subprocess sequencing, simple hashing, a JSON file or preference
for Python does not by itself justify it. Keep justified helpers focused, with
clear inputs, outputs and failure behavior. They must not become a replacement
build system, lifecycle command dispatcher or release orchestrator.

Apply this boundary when bootstrapping or changing lifecycle logic. For an
existing implementation, identify overlapping ownership and remove it within an
authorized refactor; do not silently turn an unrelated task into a whole-pipeline
rewrite. Verify preserved build, reuse, cleanup and release behavior at cutover.

## Agent Operability Contract

This lifecycle exists to make C/CMake repositories human-operable and agent-operable through the same local commands. The operating loop is:

```text
inspect -> plan -> edit -> narrow gate -> repair -> broader gate -> summarize
```

Rules:

- Prefer fast, abundant local feedback. Remote CI/CD is not part of the default lifecycle.
- After code edits, run the narrowest relevant local gate first.
- CMake command/test registration wrappers must preserve argument boundaries,
  including empty native-tool arguments, semicolons and literal quoting. Unquoted
  list forwarding can silently change the invocation. Test observable arguments
  for both direct and delegated commands, including working-directory and
  configuration options. Reject a registered project test whose executable
  target is missing during configuration, before building dependencies.
- Package/build command wrappers must own isolated process groups. On interruption
  or command failure, terminate and escalate that group, including descendants
  whose immediate parent has exited, before returning or releasing operation
  ownership. Keep nested cleanup grace periods shorter than their callers and
  bound pipe draining after escalation. Verify direct TERM/INT/HUP delivery,
  signal-resistant grandchildren, exited leaders, captured output pipes and
  nested wrappers through observable cancellation tests; never kill unrelated
  host processes or infer ownership from a machine-wide process search.
- Persistent service execution must close the actual issued lock/scope descriptor
  numbers, including recovered or narrowed handles, and remove operation bearer
  fields before exec. Verify non-default descriptor numbers, not only FDs 9/10.
- After public API edits, run header, C-only consumer, install-tree, and package checks relevant to the changed surface.
- After package, dependency, release, RPATH/RUNPATH, install-name, or artifact layout edits, run package verification or the closest available local packaging gate.
- After e2e service edits, run `dev-reset`, `dev-up`, and `test-e2e` or the closest project-specific local e2e gate.
- After Lua facade edits, run `lua-rock`, `lua-test`, and any Lua benchmark gate that protects a hot path.
- Do not run `make clean` reflexively between ordinary build, test, debug, Valgrind, e2e, Lua, fuzz, or benchmark targets. Reuse configured builds and cached dependencies for fast iteration unless there is a concrete stale-state reason.
- Diagnose stale state from effective input contracts; changes to versions, source bytes, helpers, toolchains, layout or version-bearing outputs require repair only of affected owned state. A changed URL with identical verified bytes does not itself require rebuilding. Use explicit component/group preparation or cleanup where supported. Reserve global `make clean` for required clean gates, an intentional full reset, or a demonstrated problem that scoped repair cannot resolve; a selected consumer must not clean/rebuild its borrowed prerequisites.
- When the skill already covers a lifecycle-mechanical decision, do not ask for permission. Implement, verify, and report.
- Ask only for product, architecture, ABI/API, release authority, external service, or unsupported-tool decisions.

Execution tiers:

- **Inner loop**: seconds to low minutes; targeted configure/build/test commands for the edited surface.
- **Confidence loop**: normal local verification such as `test-all`, deterministic e2e, Lua tests, fuzz smoke, benchmark gates, and package verification.
- **Release loop**: incremental candidate proof and review, squash to the resolved local release branch, lightweight tag, one final clean tagged build, package verification, push, and GitHub release. Reuse valid evidence during preparation and equivalent same-run work during release; follow [release.md](release.md).

Failure taxonomy:

- `build-graph`: CMake target, preset, dependency graph, or install rule failure.
- `compiler`: compiler error or warning policy failure.
- `test`: unit, fixture, example, or consumer test failure.
- `memory-check`: Valgrind Memcheck or related native memory-check failure.
- `e2e-service`: local service startup, readiness, endpoint, credential, or protocol failure.
- `dependency-acquisition`: SDK bundle download, checksum, unpack, cache, or target-root failure.
- `package-layout`: tarball root, directory contract, missing file, forbidden file, or stale artifact failure.
- `relocatability`: local path, absolute RPATH/RUNPATH, Darwin install-name, pkg-config, or CMake config relocation failure.
- `api-abi-risk`: public API, exported symbol, SONAME/SOVERSION, struct layout, or compatibility risk.
- `release-authority`: unclear version, branch, tag, GitHub release, or engineer approval state.
- `external-tool-unavailable`: optional tool unavailable; skip only when absence is acceptable and documented.

## External tool discovery

- Lifecycle scripts must not assume cross-target inspection or fixup tools are on `PATH`.
- The pkt.systems default osxcross root is `${OSXCROSS_ROOT:-$HOME/.local/cross/osxcross}`. Leave `CPKT_OSXCROSS_HOST` unset for resolver discovery of the newest complete Darwin 25.x prefix; set it only to pin an exact installed prefix. osxcross may provide `arm64-apple-darwin25.4-*` without major-version aliases. Use the resolver-reported tools from the ready collection rather than inventing `arm64-apple-darwin25-*` paths; follow [toolchains.md](toolchains.md#darwin-osxcross-input-and-setup).
- osxcross Darwin compiler drivers may select the host linker when the osxcross `bin` directory is not first on `PATH`. Darwin toolchain files and wrapper scripts must prepend `${OSXCROSS_ROOT}/bin` to `PATH` before configure, compiler-identification, try-compile, dependency configure/build, package smoke, and example/consumer link steps. Do not assume that invoking `${host}-clang` by absolute path is enough to select `${host}-ld`.
- Darwin toolchain files must also set `CMAKE_LINKER` to the resolver-reported absolute `${host}-ld` and inject `--ld-path=${CMAKE_LINKER}` into executable, shared-library, and module linker flags. Apply this with discovery or an explicit prefix pin. Keep the `PATH` fix and the explicit linker-path fix together. Upstream builds that bypass CMake need equivalent selection, normally `PATH=${OSXCROSS_ROOT}/bin:$PATH` and `LDFLAGS=--ld-path=${CMAKE_LINKER}` or an upstream-specific linker override. Do not pass an absolute path through `-fuse-ld`: modern Clang treats that option as a linker-flavor selector and deprecates path use.
- Add an executable regression test for the Darwin linker route when osxcross is available. It should compile or dry-run link a minimal executable, demonstrate the host-linker risk, and prove the lifecycle route chooses the resolver-reported absolute Darwin linker for both automatic discovery and an explicit prefix pin.
- Read target tools from the producing build's configured state, including `CMAKE_C_COMPILER`, `CMAKE_STRIP`, `CMAKE_INSTALL_NAME_TOOL`, `CPKT_OTOOL`, `CMAKE_OTOOL` when present, and `CMAKE_READELF`. Use `CMakeCache.txt` or non-mutating cache introspection. Darwin lookup order is:
  1. an explicit project-prefixed override, when declared;
  2. the configured CMake cache value;
  3. target-prefixed sibling tools next to `CMAKE_C_COMPILER`;
  4. unprefixed compiler sibling tools;
  5. `PATH` as the last fallback.
- For osxcross-style Darwin toolchains, derive `<host>` from `CPKT_OSXCROSS_HOST`, the configured compiler or the selected resolver. Look for `<host>-otool`, `<host>-install_name_tool`, and `<host>-strip` next to `<host>-cc` or `<host>-clang` before unprefixed names. Do not substitute a major-only default prefix.
- Package generation must use discovered target-correct mutation tools, such as `strip` and `install_name_tool`, rather than host tools with the same basename when mutation is deliberately required. For Darwin final artifacts, prefer verify-only packaging with correct link/install-time Mach-O metadata over post-package mutation.
- Package verification must use the discovered target-correct inspection tools, such as `readelf` and `otool`, and report the lookup paths tried when a required tool is unavailable. Darwin verification inspects install names, dependency paths, rpaths and code-signature load commands. Missing target-correct `otool` for a shipped Darwin artifact is an `external-tool-unavailable` failure.
- Validate discovered tools against a generated target artifact when practical. Validate mutation tools only when mutation is required, using throwaway or pre-finalization inputs.
- Absence of an optional inspection tool may skip only that optional inspection and only with an explicit message. Absence of a tool required to prove a release invariant is a verification failure, not a silent pass.
- Repositories that package cross-target artifacts should centralize this logic in one helper instead of reimplementing lookup in package, smoke, and privacy scripts. For these SDK providers, artifact validators use `scripts/configured_build.py` against the producing CMake cache and `scripts/cpkt_darwin_tools.py` for native Apple tools. These are focused validation adapters, not build configuration or preset interpreters. Other projects may expose an equivalent Bash tool query such as `scripts/discover_target_tools.sh`.
- If a project exposes `scripts/discover_target_tools.sh`, it should accept at least a configured build directory or preset-derived build directory, a target ID, and optional project-prefixed overrides. It should print stable `KEY=value` shell assignments or another simple machine-readable format for `CC`, `STRIP`, `INSTALL_NAME_TOOL`, `OTOOL`, `READELF`, and any target host prefix it derived.
- The helper should read configured CMake cache values from the same build directory that produced the artifact being packaged or verified. It must not infer target tools from an unrelated host/debug build.
- Package generation, package verification, Darwin smoke bundle creation, and release privacy verification should consume the same discovered tool values. A mismatch between generation and verification tool discovery is a lifecycle bug. Missing Darwin mutation tools are acceptable only for verify-only package flows that do not mutate final Mach-O artifacts.
- Add tests for the discovery helper with temporary fake toolchain directories. Cover configured CMake cache values, target-prefixed osxcross sibling tools, unprefixed compiler sibling tools, `PATH` fallback, and refusal to select a known host tool for a cross-built Darwin artifact.

## Structured diagnostics

- Major lifecycle scripts should end important failures with a compact diagnostic block in plain key/value text.
- Diagnostics are for humans, agents, and wrappers. Human-readable logs remain authoritative.
- Use this shape when practical:

```text
PKT_DIAGNOSTIC_BEGIN
surface=<make-target-or-script>
phase=<specific-phase>
status=failed
class=<failure-taxonomy-class>
reason=<short-stable-reason>
artifact=<artifact-or-path-when-relevant>
next=<actionable-next-step>
PKT_DIAGNOSTIC_END
```

- Keep diagnostics short. Put long compiler, e2e, package, or benchmark logs before the diagnostic block or in named log files referenced by `artifact` or `next`.
- Do not create unsolicited tracked audit/report artifacts. Human diagnostics stay in command output; machine completion/verification receipts and operation logs required for reuse belong under ignored `build/`. Follow [package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md).


## Lifecycle Spine

Every repository should converge to this spine:

```text
deps -> configure -> build -> test -> hardening -> e2e -> package -> verify -> review -> release
```

This spine describes proof obligations, not a command list to execute afresh
after every edit. Follow [feedback-loop.md](feedback-loop.md) to reuse valid
evidence and run only affected or missing coverage during preparation.

The lifecycle has mandatory surfaces and optional extension surfaces.
Each project chooses its shipped targets and artifact types. Required release
coverage follows that declared shipment set; available toolchains do not select
it. cpkt's bundle target requirements do not apply automatically to consumers.

Mandatory surfaces:

- Public C API and source layout.
- CMake build graph.
- CMake presets.
- Make command surface.
- Dependency acquisition from `cpkt` SDK bundles for consumers, or checksum-pinned upstream inputs for bundle producers; producers do not acquire themselves.
- Fast host tests.
- Native Valgrind memory-check tests.
- Release verification for the declared target and artifact set.
- Binary SDK packaging, when a binary SDK is shipped.
- Checksums for released artifacts.
- Release artifact verification.
- Privacy and host-path scans.
- Relocatable runtime paths for shipped binaries and shared libraries.
- Install-tree downstream consumer tests, when a public SDK is shipped.

Optional extension surfaces, enabled when the project needs them:

- Coverage reports.
- Fuzzing.
- Benchmarks and performance gates.
- Podman Kube-backed e2e.
- External integration tests.
- Lua facade and Lua rock artifacts.
- Single-header artifacts.
- Source archives.
- Vendored upstream patch workflows.
- Darwin smoke bundles.
- Language parity or foreign-runtime benchmark harnesses.


## Repository Layout

Use this layout unless the project has a proven product reason to diverge:

```text
CMakeLists.txt
CMakePresets.json
Makefile
.clang-format
.gitignore
README.md
LICENSE
cmake/
scripts/
include/
src/
tests/
examples/
dist/
build/
.cache/
```

Optional directories:

```text
bench/
fuzz/
lua/
gobencher/
devenv/
vendor/
performance-logs/
perflogs/
```

Rules:

- `Makefile` is the public command surface.
- Container-backed local e2e uses a root `devenv.yaml` or `devenv.yaml.in` and generated state under `build/devenv/`; see [podman-kube-e2e.md](podman-kube-e2e.md).
- `CMakePresets.json` is the build configuration surface.
- `.clang-format` is checked in and starts from `clang-format -style=llvm -dump-config`; project style changes are explicit edits to that file, not hidden formatter defaults.
- `VERSION` is not checked into git for normal repositories. Add `/VERSION` to `.gitignore`; generate or inject it only for source archives and other non-git build contexts.
- `scripts/` holds stateful orchestration and long logic.
- `cmake/` holds CMake modules, toolchains, package scripts, archive assertions, version logic, and config templates.
- `dist/`, `build/`, generated dependency roots, local service state, and package-manager build directories are generated.
- Source archives may include tracked Podman Kube config, release scripts, examples, tests, and fixture descriptors. They must not include generated service state, dependency caches, build trees, package-manager temp trees, credentials, or VCS internals.
- Unqualified `make clean` is the full generated-state reset. It removes reusable build/evidence, `dist/`, generated dependency roots under `.cache/`, and package-manager build state. Preserve the stable operation lock file/inode across owners and waiters and any unresolved owned recovery record, as described in [package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md). Such control state cannot satisfy reusable verification. Selected cleanup removes only owned state. Neither mutates shared archive/toolchain caches.
- `make clean-dist` removes only release artifacts under `dist/`.
- Do not make normal build/test targets depend on `make clean`; fast local CI/CD depends on cache reuse.

Group-owned build layouts and selectors apply only where implemented or explicitly
requested. Keep presets and Make/helper path resolution coherent at cutover; a
generic `build/${presetName}` default does not override declared group isolation.
Follow [package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md).


## Generated Workspaces

- Put repository-local extraction trees, scan workspaces, generated fixtures, probes, and other temporary output under `<repository>/build/`. Never create them beside source files or at the repository root, even briefly. Final artifacts still belong in `dist/`; dependency and toolchain caches retain their documented locations.
- Require a checked-in `/build/` entry in `.gitignore` before creating generated workspaces. An extra ignore pattern for a leaked root-level directory is not a substitute for fixing its producer.
- Resolve the repository root from the script/module location or an explicit validated root, then create a unique workspace below `build/`. Do not use the caller's working directory as a scratch root. In `cmake -P` script mode, `CMAKE_CURRENT_BINARY_DIR` can be the repository root; it does not establish a safe build location.
- Clean owned workspaces on success and on catchable failure/interruption paths. Abrupt termination must leave any residue only under `build/`, recoverable with `make clean`.
- Verify the boundary by invoking helpers from the repository root and another working directory, exercising failed extraction/scanning as well as success. Check that no source/root scratch appeared and that generated workspaces cannot enter git or source-archive manifests. Source archive verification must reject generated scratch, including empty directories.

## CMake Presets

Required configure presets:

- `base`: hidden, Ninja generator, build directory `build/${presetName}`, compile commands on.
- `debug`: host Debug build with tests and examples.
- `debug-lua`, when Lua is supported.
- `valgrind`: native Debug C facade CTests checked by host-provided Valgrind.
- `fuzz`, when fuzzing exists.
- `integration`, when opt-in integration tests exist.
- `<target-id>-release` for each declared release target.

cpkt requires release presets for all six Linux targets listed in
[packaging.md](packaging.md#packaging). Other projects choose their release targets;
Darwin release presets are required when Darwin artifacts are declared.

Optional configure presets:

- `release`: alias or host release preset only when useful.
- `host`: host-native Release or benchmark build when separate from `debug`.
- `coverage`
- `e2e`
- `profile`
- install-tree or shared-check presets when package validation needs a dedicated configuration.

Build presets mirror configure presets. Test presets exist for each executable test configuration. Release presets must set target identity explicitly through project-prefixed variables.

Preset rules:

- The hidden `base` preset pins the default dependency mode. Do not let stale CMake cache state silently change Valgrind, fuzz, integration, or release dependency resolution.
- Keep `valgrind` as an explicit native-host public target; do not silently turn a normal debug build into a memory-check run.
- Release presets set `<P>_DIST_DIR` and `<P>_TARGET_ID` explicitly.
- Cross presets use the selected lifecycle toolchains. An unavailable toolchain for a required release target fails the gate; optional targets may be skipped only under the declared release contract and with an explicit message.
- Add a local test that verifies required presets, required cache variables, and dependency-mode defaults.

Use project-prefixed CMake options:

- `<P>_BUILD_STATIC`
- `<P>_BUILD_SHARED`
- `<P>_BUILD_BINARY`
- `<P>_BUILD_EXAMPLES`
- `<P>_BUILD_TESTS`
- `<P>_BUILD_E2E_TESTS`
- `<P>_BUILD_INTEGRATION_TESTS`
- `<P>_BUILD_BENCHMARKS`
- `<P>_BUILD_FUZZERS`
- `<P>_ENABLE_COVERAGE`
- `<P>_INSTALL`
- `<P>_DIST_DIR`
- `<P>_EXTERNAL_ROOT`
- `<P>_DEPENDENCY_BUILD_ROOT`
- `<P>_TARGET_ID`
- `<P>_TARGET_ARCH`
- `<P>_TARGET_OS`
- `<P>_TARGET_LIBC`
- `<P>_DEPENDENCY_MODE`, when the project supports bundled SDK, host, or auto dependency resolution.
- `<P>_VERSION_OVERRIDE`, for local release-candidate builds only.


## Make Surface

Core targets:

- `make help`
- `make deps-debug`
- `make deps-release`
- `make deps-cross`
- `make build`
- `make build-debug`
- `make build-release`
- `make test`
- `make test-debug`
- `make test-all`
- `make valgrind`
- `make package`
- `make package-checksums`
- `make package-verify`
- `make verify-release-archives`
- `make verify-release-privacy`
- `make release-matrix`
- `make finalize-slice`
- `make prerelease`
- `make prerelease-hardening`
- `make lifecycle-version-contract`
- `make release`
- `make print-release-version`
- `make format`
- `make format-check`
- `make clean`
- `make clean-dist`

Conditional standard targets, required when the surface exists:

- `make build-host`
- `make test-host`
- `make test-cross`
- `make cross-build`
- `make cross-test`
- `make coverage`
- `make test-coverage`
- `make fuzz`
- `make fuzz-smoke`
- `make fuzz-long`
- `make bench`
- `make benchmarks`
- `make bench-check`
- `make bench-gate`
- `make bench-compare`
- `make bench-freeze-baseline`
- `make benchmarks-go`
- `make benchmarks-gobencher`
- `make perf-gate`
- `make test-e2e`
- `make test-integration`
- `make dev-up`
- `make dev-down`
- `make dev-reset`
- `make dev-ps`
- `make dev-logs`
- `make test-install-tree`
- `make example-smoke-local`
- `make example-smoke-live`
- `make prerelease-live`
- `make lua-rock`
- `make lua-env`
- `make lua-test`
- `make release-lua-artifacts`
- `make lua-bench`
- `make lua-bench-gate`
- `make package-single-header`
- `make package-source`
- `make package-source-smoke`
- `make release-darwin-smoke-bundle`
- `make vendor-<name>`
- `make vendor-<name>-apply`
- `make vendor-<name>-status`
- `make vendor-<name>-upgrade`
- `make build-<name>`
- `make verify-<name>-patches`

Make rules:

- `make help` must list every root target intended for humans or agents, including required opt-in environment variables for integration, live, service, and package-manager targets.
- Make exposes the release entrypoint and simple target relationships; readable Bash scripts perform procedural release orchestration. CMake supplies build, test, install and package operations. Keep recipes short under [Tool ownership and simplicity](#tool-ownership-and-simplicity); do not move complex scripting into Make or replace this division with a Python orchestrator. The final public gate remains `make release`.
- `make format` formats project-owned C, headers, examples, tests, and generated single-header inputs with clang-format using the checked-in `.clang-format`.
- `make format-check` is a read-only formatting assertion; run it after the last edit and before committing.
- `make print-release-version` prints exactly the version that packaging/release targets will use.
- `make finalize-slice` is the default pre-commit gate for ordinary implementation slices: format plus the narrow local tests that catch common regressions quickly.
- `make prerelease` is deterministic local verification. It must not require real credentials or live external providers.
- `make prerelease-live` is credentialed or external-provider verification and must refuse to run without an explicit project-prefixed opt-in variable.
- `make prerelease-hardening` is expensive and may combine deterministic, live, long fuzz, benchmark, and release-matrix gates.
- `make release-matrix` builds, tests, packages, checksums, and verifies the release target set without requiring a clean tree. `make release` is the clean final pipeline.
- `make lifecycle-version-contract` is the focused pre-clean release contract for exact lightweight-tag version behavior through release-owned script and Make surfaces. Follow [temporary test-tag ownership](release.md#temporary-test-tag-ownership) for execution, recovery and cleanup.
- `make package-verify` must include privacy verification for all checksum-listed artifacts in its declared selected/binary/release scope. `make verify-release-privacy` may expose the same invariant, but cannot replace package verification. Partial scope cannot satisfy complete-release proof; see [packaging.md](packaging.md).
- `make package-source-smoke` extracts the source archive and proves it can configure, build, test, and resolve the same version without repository metadata.
- `make release` is the final clean release action and gate. Its first recipe command must be `make lifecycle-version-contract`, followed by `make clean`, followed by the shared release proof graph. It must fail on warnings for project-owned and otherwise controllable code using `-Werror` or the platform equivalent, while allowing documented exclusions for upstream dependency warnings outside practical project control.
- Core targets are the default lifecycle vocabulary. Conditional standard targets are not optional once the matching surface exists in the project.

Every target listed in `make help` must work or fail with an actionable missing-prerequisite message.


## Script Surface

Use these script names when the behavior exists:

- `scripts/deps.sh`
- `scripts/build.sh`
- `scripts/build.sh test` (native CMake/CTest workflow)
- `scripts/lifecycle.sh debug`
- `scripts/cross_build.sh`
- `scripts/lifecycle.sh cross-test`
- `scripts/fuzz.sh`
- `scripts/package.sh`
- `scripts/package-verify.sh`
- `scripts/run_linux_release_matrix.sh`
- `scripts/clean.sh`
- `scripts/devenv.sh`
- `scripts/test-e2e.sh`
- `scripts/run_timed.sh`
- `scripts/osxcross_available.sh`
- `scripts/discover_target_tools.sh`
- `scripts/release_version.sh`
- `scripts/stage_release_sources.sh`
- `scripts/test_release_from_source.sh`
- `scripts/verify_release_artifacts.sh`
- `scripts/verify_release_privacy.sh`
- `scripts/build_lua_rock.sh`
- `scripts/render_release_rockspec.sh`
- `scripts/stage_lua_rock_sources.sh`
- `scripts/validate_luarocks.sh`

Project-specific scripts are allowed only behind the standard Make targets.

Script safety contract:

- Use strict shell behavior for lifecycle scripts: fail on errors and unset variables where practical.
- Quote paths and variables.
- Resolve the repository root once and operate relative to it.
- Validate argument count and required files before mutating generated state.
- Trap cleanup for temporary directories, child processes, local daemons, and service state created by the script.
- Destructive cleanup must be limited to known generated directories owned by the selected operation. Borrowed prerequisite roots and unrelated groups are read-only. Global clean preserves the stable operation lock inode and unresolved owned recovery records while removing reusable output/evidence. Container service state belongs under `build/devenv/`; `dev-reset` removes that root only after stopping its pods. Cleanup must not reach tracked config, shared archive/toolchain caches, or unrelated services.
- Scripts that delete or recreate a directory must refuse empty paths, `/`, the repository root, parent directories, home directories, and any path outside the expected generated-state root.
- Never remove source-controlled files, parent directories, home directories, or arbitrary user-provided paths.
- Print actionable errors with the failed surface, phase, and next step. Use the structured diagnostic block for important lifecycle failures.
- Keep procedural orchestration in readable Bash scripts exposed through short Make recipes. Keep CMake build rules in CMake, and justify any focused Python exception under [Tool ownership and simplicity](#tool-ownership-and-simplicity).

For the SDK family, `scripts/build.sh path --group all --preset <preset>` resolves the documented graph profile; native CMake evaluates actual preset variables, toolchains, flags and environment. `CpktTestInventory.cmake` evaluates required registrations in the configured graph. Native CMake install rules own staging, `scripts/archive.sh` owns deterministic GNU tar output, and `docs/build-lifecycle.md` records each specialist Python exception.

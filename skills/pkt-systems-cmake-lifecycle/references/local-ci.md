# Local Build, Test, Quality, Fuzzing, And Benchmarking

For selected groups and producer/test reuse, follow
[package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md).
Required coverage is inventory-derived; narrower operations cannot silently build
or retest prerequisites. Reuse exact same-run proof without conflating Debug/Release,
plain/Memcheck, build-tree/extracted-package or source/final-byte evidence. Existing
matching prerequisite readiness supports development, not persistent skipping of
selected tests or reuse across the two clean release runs.

## Build And Test Gates

Required CTest gates use `--stop-on-failure`, including selected suites,
composition suites and Memcheck. A failed or partial suite revokes readiness.
Verify this behavior with a failing first case and a later observable case that
must remain unexecuted. Synchronize subprocess and lock tests with readiness
and explicit release signals; fixed sleeps must not determine whether a child
is still alive. Bound startup waits and provide cleanup on assertion failure.

Fast local confidence:

1. `make build`
2. `make test`
3. `make valgrind`

Broader local confidence:

1. `make test-all`
2. `make package-verify`

Release confidence:

1. the explicit applicable pre-release gates
2. independent review on the feature branch
3. final clean `make release` package build from the tagged main commit

`make test-all` includes fast host tests, host release tests when useful, cross product tests that can execute locally or through an approved runner, native Valgrind checking, native AFL++ fuzz smoke, deterministic e2e when part of normal confidence, and benchmark gates when performance is a product property. Cross runners never execute Valgrind or fuzzing gates.

Do not add a separate umbrella target as a standard lifecycle target. The exhaustive clean-slate release gate is `make release`; expensive pre-release confidence belongs in `make prerelease-hardening` or another documented rehearsal target that does not replace final release.

Recommended production-loop tiers:

- `make finalize-slice`: formatting, narrow debug checks, and a read-only formatting assertion before committing a small slice. Run it after each implementation iteration and before every commit; any edit after it requires rerunning it or at least `make format` and `make format-check` with the affected checks.
- `make prerelease`: local, deterministic pre-release confidence. Include formatting, debug unit tests, the native Valgrind target, native fuzz smoke, Lua tests, local example smoke, and deterministic local e2e when those surfaces exist.
- `make prerelease-live`: opt-in external-provider or credentialed integration tests. Refuse to run unless a project-prefixed environment variable explicitly enables them.
- `make prerelease-hardening`: the expensive tier. Include `prerelease`, live checks when explicitly enabled, long fuzz runs, benchmark gates when applicable, and the release matrix.
- `make release-matrix`: incrementally build, test, package, checksum, and verify the binary SDK/runtime artifacts for every release target, including their headers, metadata, notices and applicable binary smoke bundles. Source archives, standalone source distributions/source rocks and single-header distributions are assembled and verified by final `make release` after the binary matrix; source reconstruction is not a matrix prerequisite.

These names are preferred over project-specific gate names. Retain compatibility
aliases only for an explicit request or declared external-support commitment;
documentation alone does not prevent a pre-1.0 clean cutover.

## Optional Native macOS Hosted Verification

Follow [github-actions.md](github-actions.md) when the engineer opts in or project
policy records an existing opt-in. The extension includes normal development
branch pushes and execution of the declared workflow; public GitHub availability
alone does not enable it. Keep repository commands as the proof authority and
record exactly which commit, tests, and artifacts were verified. Required native
coverage fails closed. Release branches/tags stay local until the final tagged
`make release` and local artifact verification succeed under release authority.

## Cross-Target Runner Contract

- Each downstream project decides whether to execute cross-target tests under QEMU. QEMU is not an implicit lifecycle requirement merely because a project cross-compiles.
- Once a project opts in to QEMU testing for a release target such as `aarch64` or `armhf`, those target tests are mandatory release coverage. Include them in `release-matrix` and therefore in the shared prerelease/release proof graph; do not leave an existing QEMU test behind an optional or manual-only target.
- A project that opts in must fail `release-matrix` and `release` clearly when the required QEMU user-mode runner, target sysroot, or runner configuration is unavailable. Do not silently skip the selected target tests.
- A project that does not opt in may limit cross-target release coverage to configure, build, link, package, and artifact verification. Native Valgrind and AFL++ gates remain native x86_64 Linux-only and are never QEMU gates.


## Quality Contracts

Add tests that assert observable behavior:

- Public API success and failure paths.
- Preferred public API usage style, including receiver-style handle operations when that is the project convention.
- ABI and exported symbol expectations where ABI is promised.
- Exact dynamic-export allowlists for every project-owned shared library,
  facade, plugin, or module: maintain a source-controlled allowlist per binary
  target, including intentional runtime entry points not declared in a primary
  C header; inspect target-correct defined dynamic symbols and fail on
  unexpected or missing exports. Prove a manually declared private sentinel
  cannot link from a downstream fixture, but treat that as negative evidence
  rather than a substitute for table inspection. Header absence is not an
  export policy. Repeat the assertion against extracted SDK libraries.
- Independently check the dynamic import layer of every project facade,
  plugin, or module. Imports from the project's core library must resolve only
  to documented public-header declarations; private-core imports are forbidden.
  Run the check with target-correct tooling on build outputs and extracted SDK
  artifacts when dynamic imports are retained.
- Header self-sufficiency and C-only consumer builds.
- C89 or project-selected C standard compatibility for installed SDK consumers.
- Optional C++ consumer builds when headers must be C++ compatible.
- Warning-clean builds with strict flags for project-owned code.
- Warning-clean release builds with warnings treated as errors for all project-owned or otherwise controllable code, including facades, wrappers, generated project-owned sources, examples, tests, package smoke consumers, and release verification helpers. Exclude upstream dependency warnings only when they are outside practical project control, and document the boundary in the warning regression test.
- Install-tree downstream consumers through CMake `find_package`.
- Install-tree downstream consumers through pkg-config when pkg-config is shipped.
- Examples built from the source tree and, when shipped, from installed examples.
- Example help/version smoke tests and deterministic local example workflows.
- Version resolution from lightweight `vX.Y.Z` tags in git worktrees, and from injected `VERSION` files only in non-git source archive builds. Include negative checks proving a git worktree with no exact lightweight `vX.Y.Z` tag on `HEAD` resolves to `0.0.0`, not `0.1.0`, a planned next release version, or a repository-local `VERSION` file.
- Lightweight-tag version checks that create or delete git tags must be isolated behind `make lifecycle-version-contract`. `make release` must run that target first, before `clean`, build, package, checksum, or artifact work; `test`, `test-all`, `prerelease`, `release-pipeline`, `release-matrix`, `package-verify`, and other late release-flow commands must not run it. A correct focused test creates a temporary lightweight semver tag on the current `HEAD`, for example `v99.99.99`, validates release-owned script and Make version surfaces that should observe an exact tag, and removes the tag with a trap. Treat that tag as reserved test-only state, never a real release tag. Fail on a pre-existing reserved tag unless a lifecycle-owned recovery record under `build/` identifies the exact lightweight object created by this test. Persist that record only after exclusive tag creation succeeds; automatic recovery and trap cleanup must use compare-and-delete against the recorded object and preserve any changed or unowned ref. Interruption before ownership is recorded fails closed. Create the reserved temporary tag with signing disabled, for example `git -c tag.gpgSign=false tag v99.99.99`, so user or repository signing configuration cannot turn the temporary lightweight tag into an annotated or signed tag or make noninteractive release fail. Assert with `git cat-file -t <tag>` that accepted exact release tags and the reserved temporary test tag resolve directly to a `commit`; reject annotated or signed tag objects in the shared version resolver and in the focused release contract. When `HEAD` already has a non-reserved exact lightweight release tag, test that the exact tag takes precedence and skip the temporary-tag block so final tagged release runs do not create the reserved temporary tag. Do not use extra git checkouts, git worktrees, copied repositories, or source-archive staging as a substitute for testing exact-tag behavior on the active candidate. Release orchestration belongs to Make, not CMake; CMake version propagation must still be covered by Make-driven build/package/package-verification gates, but the pre-clean tag-mutating contract is not a CMake pipeline and should not configure CMake solely to validate tag mutation.
- Version override behavior for release-candidate builds, including Make, CMake, Lua artifacts, source archives, and dry-run packaging targets.
- CMake preset presence and option defaults that are part of the lifecycle contract.
- Dependency mode behavior, including host mode, bundled SDK mode, auto mode fallback, unsupported targets, and wrong-ABI dependency rejection.
- Package archive layout and forbidden payload checks.
- Release artifact privacy and relocatability checks that expand checksum-listed artifacts and nested payloads before scanning and inspecting runtime loader metadata.
- Negative release privacy fixtures that create release-shaped throwaway artifacts containing representative leaks and prove the gate fails with the exact artifact/file path. Cover at least repository paths, `$HOME`, absolute local `file://` URLs, absolute local or non-system ELF RPATH/RUNPATH, local Darwin install names, Darwin non-system absolute dependency paths such as `/lib/libfoo.dylib`, and Darwin local rpaths when the platform/tooling surface exists.
- Lua release artifact checks, when Lua artifacts exist, that build `dist/<project>-lua-<version>.tar.gz`, render the release rockspec, build the `.src.rock`, verify the standalone Lua source package layout and manifest, unpack the `.src.rock`, expand the embedded Lua source package, and fail on absolute local `file://` URLs or live-worktree source paths.
- Lua/C interop checks, when a Lua facade deliberately exposes C embedder interop, that validate a Lua-created object from C, obtain a generic core borrowed view, preserve fragmented callback-backed streaming across the boundary without full payload materialization, return status/error values for wrong stack values or wrong userdata, reject wrong `size` or `abi_version`, reject mismatched schema/record pairs, prove Lua registry references preserve retained-object lifetime, verify clear/reset behavior invalidates or updates record view state according to the documented contract, prove Lua-facing and C-interop behavior have parity for success and policy failures, and allow only intentional Lua interop exports while catching accidental core symbol leaks.
- Source archive extraction, configure, build, version agreement, and test smoke.
- Source archive manifest exactness: tracked non-ignored files plus deliberate generated release files, no more and no less.
- Release privacy and relocatability scans for `$HOME`, repository paths, build roots, dependency caches, package-manager temporary paths, parent-relative paths, absolute local `file://` URLs, credentials, VCS metadata, generated service state, hardening artifacts, ELF RPATH/RUNPATH, Darwin install names/dependency paths/rpaths, Darwin invalid post-mutation signature risk when `LC_CODE_SIGNATURE` is present, and unstripped binary metadata that exposes local toolchain or home paths.

Test registration:

- Large unit suites should expose named test groups or labels so agents can run narrow checks while still allowing broad gates.
- Generate or verify the list of registered unit groups when possible, so adding a test function without registering it fails locally.
- Use CTest labels consistently: `unit`, `smoke`, `local`, `packaging`, `fuzz`, `integration`, `example-smoke`, and additional project-specific labels only when they help select meaningful gates.
- Keep deterministic local integration separate from live external integration. Use a distinct label such as `offline` for integration-shaped tests that do not require credentials or real providers.
- Live tests must be opt-in and must never be required by `make test` or fast local confidence.
- Put explicit timeouts on CTest tests. Short local tests should fail quickly; live or long integration tests should have bounded, documented longer timeouts.
- Tests that intentionally skip unless an opt-in variable is set should print a clear `SKIP` line and exit successfully only when absence is acceptable for that gate.


## API And ABI Contract

The public API is the installed C header set, documented CLI behavior, documented Lua facade behavior, exported CMake targets, pkg-config metadata, and release artifact layout.

The ABI is promised only when the project ships shared libraries with an explicit stable ABI contract. Static-only libraries still need source compatibility and downstream rebuild compatibility, but do not imply a stable dynamic ABI by themselves.

Rules:

- Public headers installed under `include/` define the C source API.
- Public headers must compile standalone.
- Public headers must support C consumers with the selected project C standard.
- Public headers must support C++ consumers when the project claims C++ compatibility.
- Public structs should be opaque unless their layout is intentionally part of the API. Receiver-shell structs with public method fields and private `impl` pointers are an intentional public layout and must be treated accordingly.
- Changing function signatures, public struct layout, enum values, constants, ownership rules, error semantics, CLI behavior, CMake/pkg-config names or artifact paths can be breaking. Discuss transitions for mature interfaces or declared external-support commitments; pre-1.0 non-ABI cleanups without such a commitment permit a clean cutover without shims or permission solely for the cleanup. Shared-library ABI obligations remain independent of maturity.
- Shared libraries must set `SOVERSION` or the platform equivalent explicitly when ABI stability is promised.
- Linux shared libraries must have the intended SONAME and symlink set.
- Darwin shared libraries must have explicit install name and compatibility/current version policy when shared libraries are shipped.
- Removing or changing exported ABI symbols is breaking unless the symbol was explicitly private.
- ABI symbol checks are required when the project promises stable ABI; otherwise, at minimum verify the shipped shared library exposes only the exact intended public symbols. Every project-owned shared library needs an explicit export policy and a dynamic-symbol allowlist check; compiler visibility controls plus an executable symbol-table check are acceptable when they produce the exact approved dynamic table. An unadvertised but linkable symbol is still an accidental ABI surface.
- Breaking API/ABI changes do not automatically imply a release major bump. Follow repository maturity and support commitments; apply necessary shared-library ABI version changes from the last released baseline. Discuss unresolved authority or supported compatibility transitions, not merely a pre-1.0 non-ABI cleanup without a commitment.


## Native memory checking

Valgrind Memcheck is the first-class native memory hardening gate.

Contract:

- `valgrind` runs the native x86_64 Linux Bootlin debug C facade CTests under Memcheck with leak checking, origin tracking, and a nonzero error exit code. Include the executable tests for every public C facade, including local database e2e where that facade needs services; do not limit it to one facade or a mock. It must not run through cross-compilation, emulation, or QEMU.
- Declare any Memcheck suppression file in the owning repository input inventory. Require it before building, include it in source distributions and hash it into hardening evidence. Suppress only justified upstream failures with narrow stacks; a provider needing no suppressions may use an explicit empty policy or omit the override. Never inherit another provider's suppression merely to make the command run.
- The gate runs serially and fails clearly when the host has not installed Valgrind.
- Valgrind does not provide true MemorySanitizer coverage; document that boundary rather than claiming MSan equivalence.
- Release package verification must fail if hardening runtime paths or build paths appear in shipped artifacts.


## Fuzzing

Enable fuzzing when the project parses, frames, serializes, accepts untrusted bytes, handles protocols, or exposes complex state transitions.

Contract:

- `fuzz/` or `tests/fuzz/` contains fuzz targets and seeds.
- `fuzz` preset builds with the selected engine; the Bootlin GCC lifecycle selects pinned AFL++ GCC-plugin instrumentation. It runs only on the native x86_64 Linux host, never through cross-compilation, emulation, or QEMU.
- `make fuzz-smoke` runs bounded short jobs suitable for `prerelease`, `prerelease-hardening`, or `release` when fuzzing is part of the release gate.
- `make fuzz` runs standard bounded local jobs.
- `make fuzz-long` is opt-in and may run longer.
- Commit a nonempty initial seed corpus for each enabled harness. Declare its files in the owning input inventory, include them in source distributions and hash them into hardening evidence. Add fast tests proving missing or empty seeds fail before acquisition/compilation; a compiled harness alone does not prove that the fuzz command can run.
- Regression seeds that fixed bugs should be committed.
- Fuzzer targets should be added through a small CMake helper so compile flags, link flags, corpus paths, labels, and smoke tests remain consistent.
- Generated large corpora stay out of source unless deliberately curated.


## Benchmarks

Enable benchmarks when performance is part of the product contract.

Contract:

- `bench/` contains native benchmark sources, scripts, baselines, and README notes.
- `make bench` or `make benchmarks` runs normal local benchmarks.
- `make bench-gate` or `make perf-gate` enforces regression thresholds.
- `make bench-freeze-baseline` updates committed baselines intentionally.
- Language parity benchmarks, including Go benchmark harnesses, live behind explicit targets such as `benchmarks-go` or `benchmarks-gobencher`.
- Benchmark-generated source or fixture data must have deterministic regeneration targets.
- Benchmarks used as gates must use stable thresholds and actionable failure output.

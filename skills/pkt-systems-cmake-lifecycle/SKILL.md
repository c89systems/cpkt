---
name: pkt-systems-cmake-lifecycle
description: >-
  Self-contained lifecycle authority for pkt.systems-style C/CMake repositories
  that consume or produce cpkt SDK bundles: bootstrap new components, migrate
  existing projects, standardize Make/CMake/dependency/test/e2e/Lua/package/release
  workflows, resolve and cache cpkt C/C++ toolchains, preserve bespoke behavior
  behind lifecycle extension points, verify thoroughly, squash/tag, build dist
  artifacts, publish GitHub releases, and report project dependencies including
  cpkt, other pkt.systems components, external libraries, and vendored code.
metadata:
  short-description: pkt.systems C/CMake lifecycle
---

# pkt.systems C/CMake Lifecycle

Use this skill from a C/CMake repository root when asked to work on a pkt.systems-style C component that consumes or produces SDK bundles from `cpkt`. This includes dependency inventory questions, ordinary engineering work, lifecycle consolidation, bootstrap, migration, verification, packaging, and release.

This skill is the process authority. It must not require external example repositories, prior local knowledge, or historical convention lookup. Everything needed to shape the repository lifecycle is encoded in this skill and its first-level references.

## Non-Negotiables

- No scanner, test, verifier, packaging helper, or other tool may create temporary files or directories in the repository root or source directories. Put repository-local scratch under the repository's `build/`, which must be excluded by a checked-in `/build/` rule in `.gitignore`. Resolve that location independently of the caller's working directory; see [references/operability.md](references/operability.md#generated-workspaces).
- Local container-backed e2e uses rootless Podman Kube manifests. Keep all project-owned container state, caches, and temporary files under `build/devenv/`; `dev-reset` must remove them as the ordinary host user after stopping the pods. Follow [references/podman-kube-e2e.md](references/podman-kube-e2e.md).
- Do not tell the user or future agents to derive this lifecycle from other repositories.
- Do not write source-repository provenance, workstation-local paths, parent-relative project paths, credentials, or temporary machine paths into tracked/generated source or distribution files. Necessary local paths and generated test credentials may exist only in ignored local build/control/dependency state, with appropriate permissions and explicit exclusion from source/release artifacts; see [references/operability.md](references/operability.md) and [references/podman-kube-e2e.md](references/podman-kube-e2e.md).
- Release artifacts must be relocatable and must not contain `$HOME`, source repository paths, build directory paths, dependency cache paths, package-manager temporary paths, or any absolute local workstation path. The release gate must expand and scan all checksum-listed artifacts, including nested source rocks and nested source archives, inspect runtime loader metadata, and fail before release on any local path or non-relocatable runtime path.
- Preserve project-specific behavior, but move lifecycle behavior behind the standard surfaces when doing lifecycle work. Do not broaden ordinary engineering requests into lifecycle migrations unless the requested change requires it.
- Verification is the release gate. Each new lifecycle behavior must have an executable check.
- Where the project supports selected packages/groups, preserve their build, test, cleanup and payload ownership boundaries. Import verified prerequisites; missing/stale prerequisites fail with a preparation command rather than silently widening scope. Producer and verification reuse follow [references/package-isolation-and-build-reuse.md](references/package-isolation-and-build-reuse.md). Use the owning repository inventory and executable contracts to determine implemented commands and artifact naming; publication evidence remains separate.
- `release` is the final local release action and gate. Do not introduce a separate umbrella target as the accepted final gate; put the complete clean release pipeline behind `make release` and expose narrower rehearsals through named targets such as `prerelease`, `prerelease-hardening`, and `release-matrix`.
- Release uploads must include the checksum manifest, normally `dist/<project>-<version>-CHECKSUMS`, and GitHub release assets must be selected from that manifest rather than from a `dist/` glob.
- Treat warnings as errors for all project-owned and otherwise controllable build outputs. Use compiler warning flags plus `-Werror` or the platform equivalent, and use fatal linker-warning flags where supported. Exclude noisy upstream dependency builds only when the warnings are outside the project’s practical control; wrappers, facades, generated project-owned code, package smoke consumers, and release verification helpers remain warning-clean release blockers.
- During release work, reviews and verification commands are gates only. If a release gate finds review issues, test failures, packaging errors, artifact problems, or other correctness issues, stop the release process and report the blocker. Do not fix, amend, retag, rebuild, push, or publish as part of the same release flow unless the engineer explicitly starts a separate fix iteration.
- Build, test, package, release, service, package-manager, and long-running operational commands run serially.
- Make `make help` the authoritative human command index.
- `dist/` is generated output, not the release manifest. Release uploads must come from a verified checksum or manifest file.
- Git commits created by this workflow use Conventional Commits.
- Report the project's dependencies when this skill is first used for the current task/session, on an explicit dependency-inventory request, and after a successful release. Use repository evidence, distinguish declared/resolved/shipped dependencies, and include cpkt, other pkt.systems components, external libraries, and vendored code when present. Follow [references/dependency-reporting.md](references/dependency-reporting.md); do not acquire or update dependencies just to report them.
- Local CI/CD is the default operating model. Native macOS verification through GitHub Actions is an optional, explicit opt-in; repository availability alone does not enable it. The opt-in authorizes normal development-branch pushes, including creating the remote branch and setting its upstream, and running the declared verification workflow. It does not authorize early release-branch or release-tag pushes. Follow [references/github-actions.md](references/github-actions.md); preserve already-declared project requirements for native evidence.
- Toolchain resolution is lifecycle-owned. Every Linux build uses a complete pinned Bootlin GCC collection, including compiler, linker, binutils, libc, headers, and C++ runtime; never select a host compiler or host binutils fallback. Native memory checking uses host-provided Valgrind; fuzzing uses a pinned cached AFL++ GCC-plugin build tied to the selected Bootlin GCC collection. LLVM/Clang, including `clangd` and `clang-format`, is a host-provided development and osxcross prerequisite: do not download, cache, or package it through cpkt toolchain or SDK workflows. `clangd` validation is native-host-only: cross-target CTest, packaging, and release configurations must not invoke it or depend on host `clangd` reproducing a target ABI. C/C++ compiler discovery, automatic download, cache layout, static C++ runtime metadata, C89 compiler policy, and downstream setup must follow [references/toolchains.md](references/toolchains.md) and use the shared cache root `${CPKT_TOOLCHAIN_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/toolchains}`. Do not create project-specific toolchain cache directories.
- Dependency acquisition is lifecycle-owned. Cache every checksum-pinned external archive, including `cpkt` SDK bundles, third-party sources, and test-only packages, under `${CPKT_DEPENDENCY_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps}`. A verified SHA-256 hit must perform zero network requests, regardless of archive name, URL, source root, build mode, or source-archive reconstruction. Never override the resolved shared cache with a repository-local or temporary cache for real dependencies. This is a shared archive cache, not a build tree: local `.cache/` holds disposable extracted sources, dependency builds, and install roots. Verify every cache hit, publish verified downloads through an atomic rename, and never let clean, release, or packaging remove the shared archive cache. Follow [references/dependencies.md](references/dependencies.md).
- Apply the same digest-hit/no-network rule to toolchain archives, including Bootlin, host MIG, and AFL++. Keep their existing shared toolchain cache boundary; archive filenames must not prevent reuse of verified bytes already present there.

## Independent cpkt providers

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
family's archive names are `<provider>-<version>-<target>.tar.gz`; strip/map each
archive root into the chosen shared install prefix when composing them. Optional
archives contain only owned payload and identify the exact required core version,
target and package ID. Their own version need not match core. Preserve public
`<cpkt/...>` header names and library ABI identities through repository moves.
See [references/package-isolation-and-build-reuse.md](references/package-isolation-and-build-reuse.md)
and [references/packaging.md](references/packaging.md).

These provider assignments describe this SDK family, not a requirement to split
all downstream projects into three repositories.

## Operating Posture

Use the lifecycle on every pkt.systems C/CMake task, but do not turn every request into a lifecycle migration. Determine the engineer's intent from the request and repository state, then load only the references needed for the affected surfaces.

- If the request is ordinary engineering work, use the lifecycle as constraints, verification policy, and command discovery. Make the requested change with the smallest coherent repository impact.
- If the request is lifecycle work, actively shape the repository toward the consolidated lifecycle while preserving product behavior and bespoke project value.
- If the request is release work, follow the release protocol exactly.
- If the request is only to identify dependencies, use the read-only reporting mode in [references/dependency-reporting.md](references/dependency-reporting.md). Return the inventory and evidence gaps without starting implementation, provisioning, tests, release, or hosted verification.
- If the request is ambiguous, inspect first. Ask only when ambiguity changes product behavior, API/ABI, release authority, external services, lifecycle architecture, or unsupported-tool decisions.

Do not maintain an exhaustive list of possible engineering task types. Instead, map the work to affected surfaces: public API, ABI, dependencies, build graph, tests, hardening, Podman Kube e2e, Lua, packaging, release, benchmarks, fuzzing, documentation, or local developer workflow.

The normal operating loop is:

```text
inspect -> classify affected surfaces -> load references -> edit -> narrow gate -> repair -> broader gate -> summarize
```

For release work, replace the edit/repair loop with a stop-only gate loop: inspect -> classify -> load release references -> run the prescribed gates -> stop on any issue or publish only after all gates pass. Release gates are not invitations to repair during release.

When the skill already covers a lifecycle-mechanical decision, do not ask for permission. Implement, verify, and report.

## Reference Router

Read references only after the request and repository state indicate they are relevant. All references are first-level files under `references/`.

- Read [references/api-design.md](references/api-design.md) when creating or changing public C APIs, handle/object boundaries, receiver-style functions, examples, streaming APIs, error/ownership rules, or implementation layering.
- Read [references/operability.md](references/operability.md) for repository layout, CMake presets, Make targets, script surfaces, cache discipline, diagnostics, and lifecycle command shape.
- Read [references/toolchains.md](references/toolchains.md) when touching C/C++ compiler discovery, cross-target setup, autodownloaded compiler collections, CMake toolchain files or presets, static C++ runtime closure, downstream setup instructions, release target matrices, or package metadata that exposes compiler/runtime requirements.
- Read [references/dependencies.md](references/dependencies.md) when touching SDK dependencies, cache invalidation, dependency provenance, bundled/external dependency rules, license provenance, any JSON behavior owned by `lonejson`, or project-owned dependency behavior.
- Read [references/dependency-reporting.md](references/dependency-reporting.md) for the initial dependency summary, explicit inventory questions, dependency changes, and release reports. Read the acquisition/provenance reference as well when those surfaces change.
- Read [references/package-isolation-and-build-reuse.md](references/package-isolation-and-build-reuse.md) when changing component producer ownership, build/verification reuse, selected group operations, or composable SDK acquisition and packaging. It applies to implemented or explicitly requested capabilities, without requiring every project to split packages.
- Read [references/local-ci.md](references/local-ci.md) when touching build/test gates, API or ABI behavior, native memory checking, fuzzing, benchmarks, install-tree consumers, or quality contracts.
- Read [references/github-actions.md](references/github-actions.md) when native macOS hosted verification is requested, declared by project policy, or being evaluated for an eligible GitHub repository. It defines opt-in, development-branch push authority, existing-workflow adoption, evidence identity, and the release-ref boundary.
- Read [references/podman-kube-e2e.md](references/podman-kube-e2e.md) when the repository has or needs deterministic local service e2e, including migration from containerd/Compose.
- Read [references/lua.md](references/lua.md) when the repository has or needs Lua facades, Lua C modules, source rocks, Lua benchmarks, or Lua release artifacts.
- Read [references/packaging.md](references/packaging.md) when touching `dist/`, binary SDKs, checksums, source archives, single-header artifacts, vendored upstreams, RPATH/RUNPATH, Darwin install names, or artifact verification.
- Read [references/bootstrap.md](references/bootstrap.md) when creating a new repository or filling an intentionally blank lifecycle from a spec.
- Read [references/migration.md](references/migration.md) when consolidating an existing repository, retiring bespoke command shapes, or preserving behavior behind standard lifecycle surfaces.
- Read [references/release.md](references/release.md) for any release, version, squash, tag, push, or GitHub release work.

Suggested starting sets:

- Blank repository from a spec: `bootstrap`, `api-design`, `operability`, `toolchains`, `dependencies`, `local-ci`, `packaging`; add `lua` or `podman-kube-e2e` only when the spec calls for them.
- Existing repository consolidation: `migration`, `operability`, `api-design`, then inspect and load the affected surface references.
- Ordinary feature or fix: inspect first, then load only the affected surface references. Do not run the migration procedure unless the feature requires lifecycle restructuring.
- Cross-target, compiler, or C++ facade work: `toolchains`, `operability`, `packaging`, and any API/dependency references affected by the product change.
- Release: `release`, `packaging`, `local-ci`, `toolchains`, `dependency-reporting`, and any optional surface references whose artifacts or gates are part of the release; include `github-actions` when hosted verification is enabled or required by project policy.

## Test and example runtime

Follow [references/toolchains.md](references/toolchains.md#local-execution-with-the-selected-libc): one CMake helper selects the Bootlin runtime for all non-shipped development executables, including local CLIs, tests, examples, and fuzzers, regardless of build type. Execute these directly from CTest and e2e. Apply the same link-time policy to temporary SDK/downstream verification consumers; do not retain a runtime launcher or a second execution mode. Preserve host subprocess runtimes and shipped artifact linking; verify the packaging boundary.

## Toolchain Command Surface

Use `scripts/cpkt-toolchains.sh` from this skill to inspect or provision the pinned Bootlin Linux compiler collections. Native fuzzing uses a pinned AFL++ GCC-plugin build tied to the x86_64 Bootlin collection:

For a new development workstation, or before provisioning Darwin, read the
development-machine and user-supplied Xcode requirements in
[references/toolchains.md](references/toolchains.md#development-machine-provisioning).

On a native x86_64 Linux development host, install Valgrind through the host OS package manager and follow [references/toolchains.md](references/toolchains.md#host-llvm-and-clang) to install the latest stable host LLVM/Clang release, including `clangd` and `clang-format`. cpkt does not provision or ship these host tools. Valgrind and AFL++ are never run through a cross target, emulator, or QEMU.

Darwin MIG is different: cpkt owns its pinned PureDarwin-derived Linux host `mig`/`migcom` build helper. After the developer supplies a working osxcross SDK, run `cpkt-toolchains.sh ensure arm64-apple-darwin` from the resolved lifecycle scripts directory. The resolver selects the newest complete Darwin 25.x osxcross prefix by default; set `CPKT_OSXCROSS_HOST` to pin an exact installed prefix. Check `discover arm64-apple-darwin` for `status=ready`, `mig`, and `migcom` before configuring Darwin. Follow [references/toolchains.md](references/toolchains.md#darwin-osxcross-input-and-setup) for the complete commands. MIG stays in the shared toolchain cache and is never shipped in an SDK.

Resolve `lifecycle_scripts_dir` from the activated skill or the project's declared
vendored helper directory; consuming repositories need not contain `skills/`.
See [references/toolchains.md](references/toolchains.md#resolver-location).
Set `target_id` to the selected target before running these commands:
This block is explicit prerequisite preparation, not a selected consumer operation
or read-only inventory request. Stop immediately on a resolver failure.

```sh
"$lifecycle_scripts_dir/cpkt-toolchains.sh" discover
"$lifecycle_scripts_dir/cpkt-toolchains.sh" ensure "$target_id" || exit 1
resolved_toolchain_env=$("$lifecycle_scripts_dir/cpkt-toolchains.sh" env "$target_id") || exit 1
eval "$resolved_toolchain_env"

# Native x86_64 Linux AFL++ only:
"$lifecycle_scripts_dir/cpkt-aflpp.sh" ensure || exit 1
resolved_aflpp_env=$("$lifecycle_scripts_dir/cpkt-aflpp.sh" env) || exit 1
eval "$resolved_aflpp_env"
```

Downstream projects may vendor or call this lifecycle script, but the policy and cache root stay identical across pkt.systems C projects.

## Completion Report

When finished, report:

- Lifecycle surfaces added or changed.
- Dependency summary and relevant changes; after a successful release, the exact released dependency inventory and target differences, following [references/dependency-reporting.md](references/dependency-reporting.md).
- Gates run and results.
- Artifacts produced.
- Release tag and GitHub release URL when applicable.
- Any skipped optional gates and why.
- Any remaining risks or decisions for the engineer.

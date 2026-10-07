---
name: pkt-systems-cmake-lifecycle
description: >-
  Build, test, package and release any C/CMake pkt.systems or c89 systems
  component that chooses this lifecycle, with or without cpkt as a dependency.
  Keep Make, Bash and CMake/CTest simple; Python is limited to generators and
  test fixtures. Use for ordinary engineering, bootstrap, migration and release.
metadata:
  short-description: pkt.systems and c89 systems C/CMake lifecycle
---

# pkt.systems and c89 systems C/CMake lifecycle

Use this skill only for components that choose it. The owning project's spec
sets its products, dependencies, supported targets, artifacts and required tests.
Using this skill does not imply a cpkt dependency or an SDK delivery.

## First rule: a simple native pipeline

This rule governs every reference and example in this skill.

| Tool | Owns |
| --- | --- |
| Make | Public target names, simple prerequisites, short recipes. |
| Bash | Readable sequencing, arguments, environment, cleanup and release actions. |
| CMake | Configuration, targets, dependencies, generation, incremental builds and installation. |
| CTest | Test registration, selection, execution and results, including fixtures and CMake script tests. |

**Python must not control build, test, install, package, deploy or release pipelines.**
Do not use it for configuration/rebuild decisions, test scheduling or reuse,
lifecycle bookkeeping, dispatch, process management or release orchestration.
The ban covers scripts, inline Python and indirect helpers; calling one a
"validator" does not exempt it.

**Focused generators and test fixtures may use Python.** Generators transform
source/schema inputs into component source or resource outputs, with inputs,
outputs and dependencies declared in CMake. Fixtures run under CTest and may
exercise tools in isolated test workspaces. Neither exception may become a
production pipeline controller or a general evidence/receipt framework.

Any other Python use in the pipeline requires explicit developer permission
before writing or extending it. Explain the exact task and why native tools are
insufficient. A generic refactor instruction, existing Python code, convenience
or a JSON/XML file is not permission.

Do not translate an overgrown Python framework into an overgrown Bash or CMake
framework. Use existing tool features, one dependency graph and direct commands.
Add a script only for a concrete task the native tools do not already own.
Keep it small, deterministic and independently understandable. Remove redundant
wrappers, duplicate state and competing descriptions of the graph.

## Working loop

1. Inspect branch, dirty state, project instructions and the requested outcome.
   Resolve material spec gaps before coding; preserve unrelated work.
2. Read only the references for affected surfaces. Ordinary work is not an
   invitation to migrate the whole lifecycle.
3. Implement a coherent batch. Run affected tests through CTest, then only missing
   required coverage. Documentation-only work uses documentation validation.
4. Inspect the diff, commit under project policy, and report changes, checks,
   remaining state and limitations. Never claim unexecuted tests passed.

Use CMake dependencies and native incremental state to build each effective input
set once. Reuse passed checks while relevant inputs remain unchanged. Host-only
fixtures run once on the host; target/runtime checks keep their actual scope.
Do not invent a scheduler or receipt service to obtain reuse. Candidate preparation
is incremental; the final tagged `make release` starts clean. Within that clean
run, share identical prerequisites and execute each required check once.

## Boundaries

- Commands, builds, tests and services run serially under configured job limits.
- Keep scratch, logs and generated fixtures under ignored `build/`; final artifacts
  belong in `dist/`. Preserve shared verified archive/toolchain caches.
- Use the pinned lifecycle toolchains; no host compiler/binutils fallback for Linux.
  Host Bash >= 4.4 and LLVM/Clang are development prerequisites. Valgrind is required
  for the native x86_64 Linux memory gate, not for native macOS or cross execution.
- Preserve product behavior, warning cleanliness, public ABI and declared support
  commitments. Follow project maturity policy for non-ABI changes.
- Released artifacts must be relocatable, complete and free of private/local paths.
- Release gates stop on failure. Do not fix, retag, push or publish inside a failed
  release flow. Native hosted checks are explicit opt-in; publication needs authority.

## References: load only what the task needs

| Surface | Reference |
| --- | --- |
| Commands, layout and scripting | [operability.md](references/operability.md) |
| Tests, feedback and hardening | [local-ci.md](references/local-ci.md) |
| Producer reuse and selected packages | [package-isolation-and-build-reuse.md](references/package-isolation-and-build-reuse.md) |
| Toolchains and workstation setup | [toolchains.md](references/toolchains.md) |
| Dependencies and inventory | [dependencies.md](references/dependencies.md), [dependency-reporting.md](references/dependency-reporting.md) |
| API/ABI design | [api-design.md](references/api-design.md) |
| Packaging and release | [packaging.md](references/packaging.md), [release.md](references/release.md) |
| Bootstrap or migration | [bootstrap.md](references/bootstrap.md), [migration.md](references/migration.md) |
| Optional surfaces | [lua.md](references/lua.md), [podman-kube-e2e.md](references/podman-kube-e2e.md), [github-actions.md](references/github-actions.md) |
| cpkt provider contracts only | [cpkt-providers.md](references/cpkt-providers.md) |
| Native reuse example and skill decision checks | [native-test-reuse.md](references/native-test-reuse.md) |

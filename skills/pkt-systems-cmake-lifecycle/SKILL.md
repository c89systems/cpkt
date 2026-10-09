---
name: pkt-systems-cmake-lifecycle
description: >-
  Build, test, package and release C/CMake pkt.systems or c89 systems components
  that choose this lifecycle, with or without cpkt. Use simple Make/Bash/CMake/CTest
  workflows; Python is limited to source/data generators and small data fixtures.
metadata:
  short-description: Simple native C/CMake lifecycle
---

# Simple native C/CMake lifecycle

The project spec chooses products, dependencies, supported targets and artifacts.
This skill does not require cpkt, a public library, an SDK or optional services.

## Contract first

Work in a trusted developer checkout with declared inputs and prepared tools.
One operation owns each mutable workspace. Keep sources, configuration and tool
collections stable during a check. Native dependency tracking assumes ordinary
edits update timestamps; restored/copied state needs explicit invalidation.
Run commands serially with the project's configured build job limit.

The terminal or job runner owns the operation and cancels the whole job/process
group. Helpers run in the foreground and clean owned temporary state on ordinary
failure. Parent-PID-only signalling, hostile process environments and concurrent
in-place mutation are not supported lifecycle interfaces. State a violated
precondition and stop; do not build a supervisor or attestation system around it.

These limits do not waive checks on downloads, declared prerequisites or shipped
bytes. Reject missing required tools, wrong dependency identity, bad checksums,
unsafe cleanup scope and incomplete release evidence.

## Keep implementation small

| Tool | Responsibility |
| --- | --- |
| Make | Public target names, prerequisites and short recipes. |
| Bash | Arguments, environment and readable sequencing. |
| CMake | Configure, dependencies, generation, build and install. |
| CTest | Registered tests, selection, execution and results. |

Use native tool features before adding a script. No competing dependency graphs,
custom schedulers, preset interpreters, receipt services or process brokers.
A helper needs a concrete job that existing tools do not already perform.
Prefer deletion, consolidation or a clearer contract to another mechanism.

Python may implement a CMake-declared source/data generator or prepare/serve small
fixture data. A fixture supplies test inputs; it does not drive the test.
Test execution, assertions and expected-failure checks belong in CTest with
compiled tests or small CMake/Bash tests. Registering a Python test driver with
CTest, or naming it a fixture, does not make it permitted.
Python must not control configure/build/test/install/package/deploy/release decisions.
Any other pipeline Python requires explicit developer permission before writing
or extending it; existing Python and a generic refactor request are not permission.
Porting a large controller into Bash or CMake does not satisfy this rule.

## Feedback and delivery

Implement a coherent change, run affected CTest checks, then complete missing
required coverage. After failure, reproduce and prove the specific repair in
isolation before one affected broader rerun. Preserve unrelated valid work.
Build each effective input/configuration once and reuse matching passed checks.
A commit-message change is not a source change; a test edit need not rebuild a library.

Preparation is incremental. Final tagged release starts from clean local generated
state, builds each required configuration once and verifies the final shipped bytes.
Within that run, share equivalent prerequisites and checks. Release failures stop;
repair belongs to a separately authorized iteration. Do not retag, rebuild or
publish inside a failed release.

Review changes under [the simplicity contract](references/review-contract.md).
A clean review must respect that contract, not merely accumulate safeguards.
Inspect/commit under project policy and report actual evidence and remaining state.

## Read only the relevant reference

| Task | Reference |
| --- | --- |
| Commands and ownership | [operability](references/operability.md) |
| Tests and reuse | [local CI](references/local-ci.md) |
| Tool selection and pinned helpers | [toolchains](references/toolchains.md) |
| Dependencies | [acquisition](references/dependencies.md), [reporting](references/dependency-reporting.md) |
| Packaging and release | [packaging](references/packaging.md), [release](references/release.md) |
| Selected/composable packages | [package ownership](references/package-isolation-and-build-reuse.md) |
| Bootstrap/migration | [bootstrap](references/bootstrap.md), [migration](references/migration.md) |
| Declared API/Lua/services | [API](references/api-design.md), [Lua](references/lua.md), [local services](references/podman-kube-e2e.md) |
| Opted-in hosted checks | [GitHub Actions](references/github-actions.md) |
| cpkt-family contracts only | [providers](references/cpkt-providers.md) |

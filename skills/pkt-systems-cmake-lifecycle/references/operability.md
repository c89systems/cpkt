# Operability, layout and command surfaces

## Tool ownership and simplicity

[SKILL.md](../SKILL.md#first-rule-a-simple-native-pipeline) is the authority:
short Make recipes, readable Bash workflows, one CMake graph and CTest-owned tests.
Python is allowed for focused generators and fixtures, not lifecycle control.
Other pipeline use needs explicit developer permission before implementation.
Existing helpers and historical documentation do not grant an exemption.

Prefer `cmake --preset`, `cmake --build`, `cmake --install`, `ctest` and appropriate
`cmake -P` scripts. Use CMake's dependencies, custom commands and declared outputs
for generated work. Do not reimplement its graph or freshness logic. Use standard
archive/checksum tools for ordinary packaging operations.

Do not create custom schedulers, preset interpreters, test runners, generic
receipt services, dispatch frameworks or wrapper chains that only forward argv.
Do not recreate those systems in Bash or CMake to evade the Python restriction.
A new script must have a specific necessary job, a small interface and actionable
failure behavior. Keep build rules in CMake; Bash sequences the public operations.

## Host shell prerequisite

The lifecycle helper collection requires host Bash >= 4.4. Check it before cache
work or child processes; select it through PATH, including native macOS jobs.
Preserve argv boundaries, empty arguments and literal semicolons. See
[toolchains.md](toolchains.md#host-bash). This does not constrain unrelated SDK users.

## Agent Operability Contract

Inspect the request and existing state; implement a coherent batch; run affected
CTest coverage; complete missing gates; commit and report. Follow
[local-ci.md](local-ci.md#feedback-loop). Do not clean or run a matrix after every
edit. Escalate material product/API/ABI/release decisions and prohibited tool use.

Use serial commands and the configured job limits. Keep ownership boundaries
explicit. Cancellation must stop owned descendants, clean owned temporary state
and return failure; never kill unrelated processes. Use ordinary shell/process
facilities, not a new process-control service. Test interruption and cleanup
through CTest fixtures when that behavior changes.

## Lifecycle Spine

```text
deps -> configure -> build -> test -> package -> verify -> release
```

This describes dependencies, not a command list to rerun after every edit.
Add hardening, examples, e2e, Lua or benchmarks only for declared product needs.
The project chooses its shipment set. cpkt provider targets and payloads do not
apply automatically to other components.

## Repository Layout

Use ordinary `include/`, `src/`, `tests/`, `cmake/`, `scripts/` and `examples/`
directories as needed. Keep `CMakeLists.txt`, `CMakePresets.json`, `Makefile`,
`.clang-format`, `.gitignore`, README and license at the root. Avoid empty scaffolds.

- Make is the human command index; `make help` explains scope and prerequisites.
- Presets define build configurations. Keep `.clang-format` checked in; derive
  its initial style from `clang-format -style=llvm -dump-config`.
- `build/`, `dist/`, local `.cache/` and service/package-manager state are generated.
- Ignore `/VERSION` in git; inject it only in non-git source distributions.
- `clean` resets owned generated state; `clean-dist` removes delivery artifacts.
  Neither removes shared archive/toolchain caches. Normal build/test targets never
  depend on clean. Selected cleanup cannot touch borrowed or unrelated outputs.
- Preserve active operation locks and owned interruption-recovery state during
  cleanup. Use existing mechanisms; this rule does not require a new lock framework.

## Generated Workspaces

Keep extraction trees, scans, probes, logs and fixtures under `<repo>/build/`,
protected by a checked-in `/build/` ignore rule. Resolve the root from the script
location or an explicit validated argument, not the caller's cwd. In `cmake -P`,
`CMAKE_CURRENT_BINARY_DIR` may be the source root and is not a safe scratch default.

Use unique owned workspaces, reject traversal/symlink redirection, and clean them
on success or catchable failure. Residue from abrupt termination stays under
`build/`. Source/release archives exclude generated trees, credentials and VCS state.

## CMake Presets

Provide a hidden base, host Debug and the project's declared Release target
presets. Add native Valgrind and optional fuzz/e2e/coverage presets when supported.
Build/test presets follow the configurations they serve. Keep group-owned paths
coherent where selected groups are implemented.

Use project-prefixed options with explicit defaults for tests, examples,
static/shared outputs, dependency mode, roots and target identity. CMake evaluates
presets; do not write an interpreter. Required toolchains/runners fail clearly
when absent; optional targets may be skipped only under project policy.

## Make Surface

Expose existing applicable capabilities through small targets:

| Surface | Typical targets |
| --- | --- |
| Development | `help`, `build`, `build-debug`, `test`, `test-debug`, `finalize-slice` |
| Quality | `format`, `format-check`, `valgrind`, optional fuzz/e2e/bench gates |
| Packaging | `package`, `package-checksums`, `package-verify`, source smoke when shipped |
| Release | incremental `prerelease` / `release-matrix`, final clean `release` |
| Cleanup | `clean`, `clean-dist`, service reset when applicable |

Do not add targets merely to fill this table. Preserve documented scope and expose
selectors only for implemented capabilities. Pre-1.0 non-ABI cleanup does not
require legacy aliases without an explicit support commitment.

Tag-mutating checks belong to the pre-clean `lifecycle-version-contract` gate.
Follow [temporary test-tag ownership](release.md#temporary-test-tag-ownership).

## Script Surface

Use direct Bash workflows for argument handling, matrix iteration, environment
setup, archive operations and release actions. Use CMake for build/install rules
and CTest for tests. Share a small helper only where it removes real duplication.
Avoid a second dispatcher beneath every Make target or multiple copies of tool
resolution. Keep the configured shell and failure status visible.

## External tool discovery

Use tools from the producing configuration, not an unrelated host/debug cache.
Prefer explicit approved overrides, configured CMake tools, target-prefixed
compiler siblings, unprefixed siblings, then PATH only where valid. Never select
host inspection/link tools for cross-target artifacts.

For osxcross, keep its bin directory first on PATH and select the resolved linker
explicitly through `--ld-path`; do not invent major-version aliases or use an
absolute path with `-fuse-ld`. Follow [toolchains.md](toolchains.md). Use one native
CMake/Bash discovery surface; existing Python adapters are not exempt from policy.

## Structured diagnostics

On failure, report the command/phase, reason, relevant log or artifact and next
step. Preserve detailed logs under `build/`; avoid tracked audit files and a
custom logging framework. When performance is the task, use existing timing and
build/test output to identify repeated work before adding instrumentation.

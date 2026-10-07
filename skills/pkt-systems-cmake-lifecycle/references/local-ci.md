# Local build, test and quality

## Feedback loop

Use the native tool ownership in [SKILL.md](../SKILL.md#first-rule-a-simple-native-pipeline).
CMake owns the dependency graph; CTest owns test execution. Python generators and
fixtures are permitted leaf tasks, not alternate build/test controllers.

Implement a coherent batch, run affected CTest checks, then complete only missing
required coverage. Do not run a full suite, matrix or review between every edit.
Documentation-only changes use readback, syntax/link or rendered validation.

Build each effective source/toolchain/configuration once. Reuse matching upstream
outputs across consumers. Reuse passed checks until their actual inputs, outputs,
environment or mode change. A test-only edit does not rebuild an unchanged library.
Session, commit-message and signing changes do not invalidate tested code.

Use CMake dependencies and declared outputs for reuse. CTest does not itself cache
passed tests. If a success marker is needed, keep it local and tied to that check's
real inputs; publish it only after success and invalidate it before changed work.
Do not create a parallel scheduler, receipt service or source-closure interpreter.
Missing, corrupt, failed, skipped, interrupted or partial results cannot prove full
coverage. A zero CTest exit status alone is insufficient; required cases must pass.

Use the [small native suite example](native-test-reuse.md) when a marker is needed.
It defines actual input binding, failure invalidation, suite scope and explicit
reruns; its executable fixture tests the example itself. This is not a requirement
to introduce per-case records or a reusable caching framework.

Host lifecycle fixtures run once for their relevant host inputs. Target runtime,
ABI, loader and extracted-package checks keep their actual target/byte scope.
Debug/Release and plain/Memcheck are distinct when their behavior differs, while
identical prerequisite builds are shared. Preserve configured job limits.

Reuse a clean review for the same tree, scope and baseline. Review probes need a
specific unresolved concern; review is not another exhaustive test scheduler.
Performance claims require measured cold/warm results. Use existing timings and
build/test logs to locate repeated work; do not add a telemetry framework.

## Failure and fix posture

A failing broad gate identifies the next focused investigation; it is not a
reason to rerun that gate after every small edit.

1. Preserve the failing case, diagnostics and relevant inputs. Reproduce it with
   the smallest meaningful CTest selection or isolated fixture, retaining the
   actual target, toolchain, configuration, runner and cache/install context where
   they matter. A simplified fixture must reproduce the cause, not merely pass.
2. Diagnose the cause and implement a coherent repair with a focused regression.
   Prove the intended behavior and relevant failure/boundary cases. For intermittent
   failures, use bounded targeted checks appropriate to the mechanism; repeated
   green runs alone do not establish a fix.
3. Resolve remaining issues in that focused scope before broadening. Reuse valid
   unrelated builds/checks. Do not clean the whole tree, rebuild prerequisites,
   increase timeouts or suppress coverage to conceal an unexplained failure.
4. Once focused evidence is sufficient, rerun the affected larger gate once.
   Run a cross matrix only when its coverage is affected or still unproven. If
   another issue appears, return to focused investigation before the next broad run.

Report what failed, what explains the repair, which focused checks passed and
which broader coverage remains. Focused success proves its scope, not full-suite
readiness. This loop applies to development and separately authorized fix work;
failed candidate/release gates stop under [release.md](release.md), with no
in-flow repair or automatic cold-release retry.

## Build And Test Gates

Register executable tests, fixtures, examples and suitable CMake script checks
with CTest. Expose meaningful labels, exact scope and bounded timeouts. Fail when
required test executables or runners are missing. Required suites use
`--stop-on-failure`, `--output-on-failure` and reject an unexpectedly empty selection.
Test observable behavior rather than source wording or internal bookkeeping.

Use `finalize-slice` for affected Debug checks and formatting before a coherent
implementation commit. `prerelease` completes ordinary native/hardening and binary
matrix coverage incrementally. `prerelease-hardening` adds only declared expensive
tiers. `release-matrix` covers the project's binary shipment set. Final tagged
`release` starts clean and covers all required artifacts; follow [release.md](release.md).
A target name is not a reason to rerun coverage already supplied by another gate.

Keep deterministic integration local. Live/credentialed tests require explicit
opt-in and cannot be part of fast `test`. Fixture workspaces belong under `build/`.
Use readiness signals rather than fixed sleeps for subprocess/service tests;
bound waits and guarantee teardown after failure.

## Cross-Target Runner Contract

QEMU execution is a project choice. Once selected for a release target, its tests
are mandatory and missing runner/sysroot/configuration fails clearly. Without
that opt-in, cross coverage may be configure/build/link/package verification.
Never run Valgrind or AFL++ through QEMU or a cross target. osxcross proves build,
link and metadata, not native Darwin runtime success; report deferred coverage.

Native macOS hosted verification is explicit opt-in under
[github-actions.md](github-actions.md). Required hosted proof names the exact
commit and coverage. Hosted outputs do not replace local release artifacts.

## Quality Contracts

Cover the project's observable contracts, including relevant negative cases:

- API success/error paths, ownership, allocator behavior and declared ABI.
- Standalone installed headers, selected C standard and promised C++ compatibility.
- Warning-clean owned code, generators' output, examples and consumers; treat
  compiler/linker warnings as errors. Document uncontrollable upstream exclusions.
- Exact shared-library export allowlists and public-only facade imports. Check
  build outputs and extracted SDK bytes; header absence is not export policy.
- CMake/pkg-config install-tree consumers, static/shared/PIC closure and examples.
- Dependency modes/pins, target tools, package layout, version propagation,
  checksums, relocatability and privacy, including nested shipped artifacts.
- Shipped source extraction/build/test and exact source manifest coverage.

Follow [api-design.md](api-design.md), [packaging.md](packaging.md) and any applicable
Lua/e2e reference for their detailed contracts. Do not invent new product surfaces
or expand ordinary edits into every optional gate.

Tag-mutating tests stay in the pre-clean version contract. Follow
[temporary test-tag ownership](release.md#temporary-test-tag-ownership).

## API And ABI Contract

The installed headers and declared CLI/Lua/CMake/pkg-config/artifact interfaces
are public contracts. Follow project maturity and external-support policy for
source compatibility. Shared-library ABI is independently versioned: required
breaking bumps derive from the latest released ABI and advance exactly one.
Do not infer ABI compatibility from a repository version or hide required bumps.

## Native memory checking

Run the established native Valgrind gate for C test coverage. Fail on memory
defects and incomplete required cases. Suppress only identified upstream issues
with narrow stacks. Keep suppression inputs in source distributions; do not copy
another provider's policy merely to pass. Reuse unchanged valid coverage.

## Fuzzing

Use the pinned native AFL++ GCC-plugin toolchain and explicit CMake fuzz targets.
Run smoke as declared by ordinary confidence; standard/long runs remain separate
coverage modes, with long runs opt-in. Crashes/timeouts fail the gate. Preserve
findings and a nonempty checked-in seed corpus; missing seeds fail before building.
Fixtures may use Python to test these behaviors through the public commands.

## Benchmarks

When performance is a product contract, keep comparable baselines and declared
regression thresholds. Run the affected benchmark gate after hot-path changes;
report unmeasured behavior rather than claiming improvement. Do not create a
benchmark system merely because this skill is active.

# Tests and fast feedback

Register tests, fixtures and suitable script checks with CTest. Give them clear
scope and bounded timeouts. Required suites reject an empty selection, stop on
failure and print useful diagnostics. Missing executables/runners are failures.

A required test must actually pass. Do not configure a required check to skip on
failure or count optional/live skips as required success. Use CTest's native
results to establish coverage; a target name or exit status alone is not proof
that the intended inventory passed.

## Native reuse

CMake owns input/output dependencies and incremental builds. CTest does not cache
passed cases. Reuse matching results through the project's existing mechanism.
If a deterministic suite needs a local success stamp, use a small CMake custom
command with declared prerequisites and output, not a second scheduler.

The stamp is suite-scoped, created only after complete success and invalidated
before a forced rerun or changed verification work. Declare relevant binaries,
data, scripts, configuration and execution modes. Stable workspace ownership and
normal timestamps are preconditions; copied/restored state or unrepresented
environment changes require explicit invalidation. Do not hash entire checkouts
or toolchains or invent per-case evidence records.

Host-only fixtures run once for their host context. Target runtime, ABI and
extracted-package checks retain target/byte scope. Debug/Release and plain/memory
checking may differ; identical prerequisite builds still share one producer.
Final clean release creates fresh results and verifies final artifacts.

## Failure and fix posture

Preserve the failing diagnostics. Reproduce the cause with the smallest meaningful
CTest selection, retaining the target/toolchain/mode where it matters. Make a
coherent repair and prove normal, negative and boundary behavior in that scope.
An intermittent failure needs an explanation and targeted evidence, not repeated
green runs.

Only then rerun the affected broader gate once. If another issue appears, return
to focused diagnosis. Do not repeatedly clean, run complete suites/matrices,
increase timeouts or suppress checks while guessing. Report what is proven and
what coverage remains. Failed release gates stop; remediation is a separate iteration.

## Applicable coverage

Test declared APIs/CLIs, ownership/errors, warning cleanliness, installed consumers,
dependency modes, version propagation and shipped artifacts. Public shared-library
exports and facade imports must match the public boundary. Tests/examples should
assert observable behavior. Do not add optional products to fill a checklist.

On native x86_64 Linux, use the established Valgrind gate and narrow identified
upstream suppressions. Native AFL++ uses the pinned Bootlin/GCC-plugin setup;
smoke and longer runs are distinct. Neither runs through QEMU or on native macOS.

Cross execution is a project choice. Once selected, runner/sysroot availability
and runtime tests are mandatory; otherwise report build/link/package proof and
deferred execution. Hosted native checks are explicit opt-in.

Live/credentialed checks are opt-in. Local service tests use readiness signals,
bounded waits and teardown of the resources they started. Benchmarks need a
declared performance contract and comparable evidence; no unsolicited framework.

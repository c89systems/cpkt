# Fast and accurate feedback

Required coverage and low latency are joint lifecycle requirements. Repeating
unchanged work does not add confidence. Select commands from the missing proof
for the current inputs rather than running every familiar gate.

## Incremental preparation

Inspect existing outputs, test coverage and review evidence before starting work.
Implement a coherent batch, run focused checks for affected behavior, then complete
only missing required coverage before committing. Do not run a matrix, full native
suite or independent review between every local edit. Documentation/policy changes
use readback, link/frontmatter validation and consistency checks; executable gates
apply only when executable behavior or configuration is affected.

Build each component once for its effective source, recipe, target, compiler,
sysroot, flags and dependency inputs. CMake owns incremental compilation and
dependency edges. A no-op build must execute no compiler, linker or upstream
configure/build/install step. Preserve matching producer outputs across consumers.
Debug, Release and instrumentation can require distinct project outputs; they do
not justify rebuilding an identical ordinary upstream producer.

Execute a test once for its effective test/helper inputs, tested outputs, tools,
runtime/environment and coverage mode. Successful evidence remains reusable
across commands, commits and sessions when those identities and required coverage
match. Commit messages, signing and session boundaries do not change tested code.
Verify current identities and record the original result and provenance; do not
turn reused evidence into a claim of fresh execution. Explicitly requested reruns
or repeated trials for a test's specified statistical coverage remain supported.

A focused result satisfies its own coverage and can contribute to an aggregate
only after every required case has matching successful proof. Do not present a
partial run as complete readiness. Failed, interrupted, skipped, missing or corrupt
evidence cannot satisfy coverage. Invalidate affected proof before mutating its
inputs; a failed changed case must not leave an older aggregate marked ready.

Track build inputs separately from test inputs. A test edit does not rebuild an
unchanged library. A target-specific edit invalidates affected target proof;
shared behavior changes invalidate all affected targets. Use explicit dependency
closures and effective configuration, not an entire shared file hash when its
unrelated sections can be separated reliably. Never exclude a real semantic input
merely to preserve a receipt. Record the invalidation reason.

## Coverage ownership

Host lifecycle fixtures run once per matching host/input context, not once per
target or configuration. Register them in a host suite and aggregate their proof
with the target suites. Audit fixtures that inspect configured graphs: split or
parameterize genuine target-specific obligations rather than deleting coverage.
Target runtime, ABI, loader and install-tree checks retain their declared target
and runner requirements. QEMU coverage remains mandatory where selected.

Debug versus Release, plain versus Memcheck, and build-tree versus extracted SDK
are distinct when they test different behavior or bytes. Give each obligation an
owner and reuse identical prerequisite work. Package staging, composition and
final verification share validated consumer results for identical archive bytes,
consumer inputs and execution context. A new command or operation ID alone does
not make a successful check stale during preparation.

Review is an independent defect-finding gate, not another build/test scheduler.
Supply current validation evidence and the fixed baseline to reviewers. Review
probes run only for a specific unresolved concern or missing proof; avoid full
suites as reviewer preflight. Reuse a clean review for the same tree, review scope
and baseline. After a coherent remediation batch changes the tree, validate the
affected behavior and review the changed candidate once; do not repeat clean
reviews merely because of a new session, squash or signing-only amendment.

## One final clean release

Candidate verification is incremental. Use existing matching evidence and run
only missing candidate gates. Do not require a clean candidate `make release`
before the mandatory final tagged `make release`. An additional cold audit is
an explicitly requested extra, not an automatic precondition.

The final tagged release starts from empty local generated build/install/dist
state while retaining shared verified source/toolchain archive caches. Establish
fresh build/test/package evidence for that release; preparation receipts cannot
replace proof for freshly produced tagged bytes. Within this clean run each
effective producer and required check executes once, and aggregate gates reuse
matching same-run results. Final checksum, privacy and delivery checks still
validate the exact complete artifact set.

When shipping source archives, stage the final archive once and build from its
extracted tree with empty local compiled/install state. Use that reconstruction
as the sole native Release producer and test lane, and package its native SDK
outputs into the matrix. All release build/test lanes use that extracted build
domain and share matching ordinary upstream producers; do not first build native
Debug dependencies in the checkout and rebuild them for reconstruction. This
proves delivered source builds independently of checkout-generated state without
a second equivalent producer. Debug/hardening and other target lanes remain
distinct project configurations. Validate archive payload,
version and source identity before accepting reconstructed outputs; carry actual
output and coverage provenance into the matrix. Binary-only rehearsals need not
produce a source archive. A separately requested independent comparison build is
an extra proof obligation, not the default release architecture.

Release failures remain stop-only under [release.md](release.md). A separately
authorized fix iteration reuses unaffected preparation evidence; only relevant
changes require renewed checks and review. The subsequent tagged release still
starts clean. Do not launch repeated cold releases as a debugging loop.

## Performance acceptance

For lifecycle performance work, record monotonic durations for configuration,
producer build/install, project build, host tests, target tests, hardening,
archive/staging, installed consumers, source reconstruction and final validation.
Record executed/reused counts and reasons, input identities, configured job
limits, cache state, failures and total wall time under ignored `build/`. Attribute
overlapping review/probe spans without adding them twice.

Verify public commands with observable invocation counters: no-op preparation
executes zero producer/test/consumer work; an affected edit runs only affected
work; a test-only edit preserves compiled outputs; repeated packaging reuses
matching consumer proof; a clean release creates fresh proof once per obligation.
Missing coverage, changed runtime inputs, corrupt outputs, stale evidence and
interruption must fail closed. These regressions are implementation requirements,
not claims that changing this policy alone fixes existing scripts.

Use the engineer's stated end-to-end budget as acceptance, including review and
all required targets. cpkt core's current target is less than one hour for review,
tests and the full seven-target release build; this is not a default budget for
every downstream repository. Establish cold/warm phase measurements before
claiming compliance. If measured work exceeds the budget, expose the dominant
phase and correct it before repeating a broad run. Do not reduce required
coverage, weaken identity checks or raise configured job limits to hide a regression.

# Package Isolation And Verified Build Reuse

## Applicability and authority

Implement isolation and reuse with simple Make entrypoints, readable Bash
workflows and CMake's existing target/dependency/incremental-build facilities.
The identities and evidence described below are verification contracts, not a
requirement to build a custom scheduler, Python lifecycle dispatcher or parallel
build framework. Avoid duplicating CMake's graph or rebuild logic. Keep any
additional package identity checks focused and justify Python exceptions under
[operability.md](operability.md#tool-ownership-and-simplicity).

Use this reference when a project declares independently selectable build/test
groups, distributes composable SDK packages, or changes dependency producer or
verification reuse. These are separate capabilities: producer reuse also applies
to a single-package project. Adopt only the capabilities required by the task.

Determine the implemented command, artifact and consumer contracts from the
repository. A design spec records intended changes; it does not prove that its
commands or release assets exist. Do not migrate a project or select a planned
package layout merely because this reference is active. Keep component assignments,
inventory schemas, artifact names and migration plans in the owning repository.
Do not require every project to use core/db/misc, ship cmocka, or share one version
across independently released providers. When the repository declares one
coordinated release, package selection does not create separate release lifecycles.
Package-group names describe payload ownership. They do not redefine the
language-agnostic core-library boundary used in API and Lua facade design.

## Ownership and selected operations

Maintain one authoritative inventory of component dependencies, group ownership,
producer inputs/helpers, public surfaces, generators, tests/modes, installed files
and notices. Derive or validate configure, test, staging and CI selections from
it; reject missing ownership, duplicate payload owners and dependency cycles.
Separate production linkage, test-only edges and shared tooling. Shared dependencies
have one owner; optional sibling groups cannot require one another when the
repository promises independence. Registration must work with unrelated groups'
builds and installs absent, not merely filter an otherwise mandatory full graph.

Use the existing Make surfaces with one documented group selector, such as
`GROUP=<name>|all`, only after the project implements it. Preserve documented
unqualified scope. Validate groups, presets, evidence scopes and unsupported
modes before changing generated state. Distinguish explicit overrides from Make
defaults. `make help` explains which commands build, require existing outputs,
or prepare prerequisites. Complete-release gates reject any explicit narrowing.
Formatting remains global; selection limits builds/tests, not formatting policy.

A selected operation may build/test/stage/clean its owned state and read validated
prerequisites. It must not configure prerequisite producer graphs, refresh their
stamps/receipts, provision missing tools, or run their standalone suites. Fail on
missing/stale/unverified prerequisites with the component, target/configuration,
reason and exact preparation command. Repair is an explicit prerequisite operation
or an intentional all-group operation within the same repository. In independently
released providers, `GROUP=all` still cannot rebuild an imported core; core SDK
preparation remains explicit. Consumer integration exercising an imported
library remains consumer-owned coverage; it does not imply rerunning that library's
standalone tests. Core preparation cannot silently run optional consumers' suites.

Group cleanup deletes only owned generated state and stops only owned services.
Changing/removing a prerequisite makes dependent evidence stale through recorded
identities; do not rewrite unrelated groups' receipts in a selected operation.
Global clean remains the full local reset. Neither kind removes shared archive or
toolchain caches. Keep borrowed/caller-owned installs read-only. Component roots
retain the target/component paths in [dependencies.md](dependencies.md); store
identity in records rather than semantic hash/version directory names.

Serialize validation, mutation and completion publication for local prerequisites
with one repository operation lock, including supported direct scripts/configures.
Keep its control state under `build/control/`; the lock file/inode stays stable
across owners and waiters. Managed clean and teardown never unlink/recreate it,
including after an owner finishes; they remove reusable output/evidence instead.
Nested calls inherit validated ownership bound to the repository, live lock handle
and permitted scope; a bare environment flag cannot bypass locking or widen scope.
Only the outer owner releases the lock; nested calls cannot release it.
Bound contention waits and identify the owning operation. Source reconstruction
uses a separate extracted-root context while preserving outer ownership.
Read-only inventory and downstream discovery in a deployed SDK do not mutate
repository producers and need no producer lock. Test interruption and cleanup
without consulting the machine-wide process table.

## Three independent identities

| Identity | Evidence and permissible reuse |
| --- | --- |
| Archive | Required SHA-256 of acquired bytes. Verified hits make zero acquisition network requests, regardless of URL/name/root. |
| Build | Effective output-affecting inputs plus successful production and validated installed output bytes. Matching consumers may import those outputs. |
| Verification | Required cases, test/helper inputs, actual binaries/dependencies, target/runtime/runner, mode, relevant environment and successful completion. Only matching coverage is reusable. |

A verified archive does not prove a completed build; a build does not prove tests.
Expected contracts written at configure time are not completion receipts.
An unchanged Git revision, version, filename, directory or timestamp is insufficient.

Build identity includes pinned source digests, patches, public/compiled sources,
generators/schema inputs, component recipe and output-affecting helper closure,
toolchain/sysroot/SDK and deployment floor, flags/features/static/shared variants,
direct dependency identities and output metadata generation. Hash relevant normalized
inventory records and shared helpers, not an entire central file containing unrelated
recipes. Transitive input changes propagate through actual dependency edges.
Unrelated documentation, sibling pins, commit signing/topology and operational job
counts/timeouts do not invalidate output when they do not affect it. Release-version
changes do invalidate outputs that embed that version. Keep operational settings
in diagnostics; prove equivalence before excluding an input from the contract.

One owning producer graph completes each effective component build/install before
publishing success. Matching Debug/Release consumers can import ordinary upstream
outputs only when their effective upstream flags really match; project-owned debug
and release builds remain distinct. Retain both static/shared production steps when
required. Instrumented outputs need distinct contracts/state; borrowing ordinary
dependencies for facade-only hardening must be explicit. Never let multiple consumer
graphs share upstream stamps or race an install prefix. Contract-engine/schema
migration accepts old state only with evidence of equivalent meaning and validated
outputs; incompatible/unknown records require explicit regeneration.

## Readiness and evidence lifetime

Define readiness profiles from actual prerequisite coverage, separating built,
development-ready, package-ready and fully release-proven state. A build-only command
cannot publish tested readiness. A prerequisite's own tests bootstrap from built
state; they do not require a success receipt for themselves. Test-definition changes
can invalidate verification without invalidating compiled output. Consumer-only
test edits cannot change prerequisite coverage requirements.

Record target/configuration and coverage modes explicitly. Linux cross execution
uses the configured runner. Linux osxcross can establish compile/link/API/metadata
readiness with required native runtime cases recorded as deferred; it cannot claim
those cases passed or waive release runtime requirements. Native-host-only clangd,
Valgrind and AFL++ are never cross prerequisites. An optional group's Valgrind
checks need matching ordinary prerequisites, not their unrelated Memcheck suite.

Versioned local records under ignored `build/` contain input identity, owned output
inventory/content digests, coverage, status and execution-run identity. Validate
actual outputs before reuse. Missing/corrupt bytes, malformed/unknown records,
incomplete coverage and failed/interrupted installs refuse reuse with actionable
diagnostics. Invalidate old success before replacing outputs or rerunning required
verification, then atomically publish new evidence only after complete success.
Failed, skipped, cancelled and timed-out cases never count as passes.

Existing matching prerequisite readiness supports selected development operations.
It does not authorize a general persistent test-skipping cache. Selected tests run
normally. Initially suppress duplicate test/helper invocations only for exact
successful matches within the current proof run. Preserve early fail-fast fixtures;
later suites may reuse their exact evidence, while direct CTest without supplied
matching evidence runs them. Require the expected case/executable inventory and
reject an unexpectedly empty selection. Report legitimately inapplicable aggregate
tiers separately; explicitly requesting an unsupported tier fails.

Each clean release run has fresh evidence. Aggregate all required targets/modes
from actually passed cases and explicitly matched same-run proofs, recording reused
work and its originating evidence. The candidate rehearsal cannot satisfy the
tagged clean run. Debug versus Release, plain versus Memcheck, build-tree versus
extracted-package, and native source versus exact distributed bytes remain distinct
proof obligations. Follow [release.md](release.md) and [github-actions.md](github-actions.md).

## Composable SDK acquisition and packaging

Resolve only implemented, published package contracts for the selected provider
version/target and the full prerequisite package closure. Pin each required archive's
URL and digest and use [dependencies.md](dependencies.md)'s shared cache. Validate
the complete selection and pins before acquisition/extraction. Missing package
support or a missing pin fails clearly; do not substitute a planned group asset,
another version, a sibling checkout or host libraries. Explicitly supported host/auto
modes retain their declared semantics; package composition itself never authorizes
fallback. Do not add automatic legacy layout negotiation at a clean cutover.

Compose in an owned fresh stage or new versioned prefix. Each package owns its
regular files/symlinks, notices, public surface and metadata; only directory entries
may overlap. Keep shared dependencies in their prerequisite package rather than
copying them into optional packages. Do not overlay a new SDK onto an old prefix
and leave removed files behind. Validate archives/paths/symlinks before extraction;
reject traversal, collisions, dangling/escaping links and forbidden sibling edges.

Validate each selected package's declared inventory against actual installed bytes,
including version, target/libc/deployment requirements and prerequisite payload
identities. For independently released cpkt, cpktdb and cpktmisc providers, require the same
target and the exact declared core release/package identity. Their own release
versions are independent; do not infer a core version from an optional version.
A repository-local group selector cannot start a producer in another repository.
Other providers use their declared compatibility contract;
matching version strings alone never prove compatible composition. Bind portable
package IDs to payload/metadata using the repository's defined canonical encoding,
distinct from outer archive digests. Reject unknown schemas and recursive self-hash
designs. Do not embed local receipts or paths in released manifests.

Acquisition, CMake discovery and package verification use the same installed
composition validator. Direct pkg-config workflows validate explicitly before
invocation; pkg-config version checks cannot enforce content identity. Discovery
imports only selected groups and stays within the chosen prefix except permitted
OS facilities. Validate static/shared consumers, metadata and loader closure in
every supported combination and optional extraction order, with unrelated optional
packages absent. Reuse identical prerequisite checks while retaining each distinct
combination's proof. An isolated optional archive may reference its absent declared
prerequisite; runnable closure must pass after composition. Keep artifact-local
licenses and required upstream notices. cmocka is shipped only when the producer's
product contract declares it, never implicitly linked into production facades.

Selected packaging writes only its owned staging/evidence, normally under `build/`,
and consumes matching existing prerequisite archives. Development build reuse does not permit substituting a different prerequisite
identity. Independent provider versions may differ only under the exact declared
prerequisite contract. Distinguish
selected, binary-matrix and complete-release evidence. Partial checksums cannot
replace the authoritative release checksum manifest, authorize uploads or satisfy
source/native final gates. Publishing replacement distribution payloads invalidates
old complete-release evidence before mutation. See [packaging.md](packaging.md).

## Verification and performance acceptance

Add executable regressions for selected scope: zero prerequisite producer/test
invocations, unchanged unrelated outputs/stamps/receipts, absent sibling roots,
changed input/helper/test identities, corrupt outputs, missing readiness, interrupted
publication, lock/delegation failures, cleanup boundaries and unsupported selectors.
Composition negatives cover wrong/missing prerequisites, payload collisions,
host/sibling fallback, metadata leakage, missing notices and partial evidence offered
as full release proof. Use independently constructed expected identities and actual
consumer behavior, not only text matching or timestamps.

Measure monotonic phase durations and executed/reused counts for cold clean release,
warm all-group work, selected edits and no-op builds under the configured job limits.
No-op producer work must be zero when inputs/outputs match. Diagnose repeated
upstream work from producer contracts/stamps before removing it. Bound e2e readiness,
retain teardown on failure, and fix bottlenecks without reducing required coverage.
Keep QEMU build-tree and extracted consumers, plain and Memcheck e2e, distinct
configuration builds and independent reconstruction of shipped source archives.
Generate each selected source archive once per clean run; reconstruction uses empty local compiled state and the
shared verified archive caches. Logs/timings stay under `build/`. Both exhaustive
clean `make release` runs remain mandatory.

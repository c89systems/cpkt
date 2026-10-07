# Package isolation and build reuse

## Applicability and authority

Apply this reference to implemented selected groups, composable packages or
producer reuse. A single-package project also benefits from producer reuse;
package splitting is not a requirement. The owning repository defines commands,
artifacts and support commitments. cpkt-specific contracts are conditional under
[cpkt-providers.md](cpkt-providers.md).

Use one CMake dependency graph with simple Make/Bash entrypoints under
[SKILL.md](../SKILL.md#first-rule-a-simple-native-pipeline). These contracts do not
require a separate inventory language, scheduler, receipt service or test runner.
Python generators and fixtures do not authorize Python lifecycle bookkeeping.

## Ownership and selected operations

Declare component dependencies, producers, install outputs and test ownership in
the native graph. Package metadata describes delivered payload; it must not become
a competing build graph. Reject dependency cycles, duplicate payload owners and
undeclared imports.

Expose selectors only when implemented; preserve documented unqualified scope.
Validate selector combinations before mutation. A selected operation builds,
tests, stages and cleans only its owned payload. It cannot provision, rebuild,
retest or repair borrowed prerequisites, or touch sibling repositories. Missing
or stale prerequisites fail with the exact preparation command.

Shared prerequisites have one owner. Optional siblings cannot require each other
when independence is promised. One provider's `all` means that provider alone.
Selected checksums cannot authorize complete-release uploads. Reject partial
selectors for complete release gates.

Keep generated state under the owning build paths. Prevent traversal and symlink
redirection, including absent output leaves. Serialize mutation and preserve active
locks/recovery state through cleanup. Cancellation must fail and stop owned work.
Use the existing native/shell mechanisms; do not add a process-control framework.

## Producer and verification reuse

One CMake producer builds/installs each effective component input set. Inputs
include source, recipe, output-affecting tools/flags/options and dependency outputs.
Declare these dependencies and expected outputs so native incremental work decides
what changed. Complete configure/build/install before recording success; an
expected output declared at configure time is not proof it was produced.

Ordinary upstream outputs may serve Debug, Release and facade-only hardening when
their effective flags match. Distinct instrumentation requires distinct outputs.
Never share mutable upstream stamps between competing producer graphs.

Separate build invalidation from test invalidation. Test-only changes preserve
library outputs. Reuse unchanged passed checks through the native mechanisms in
[local-ci.md](local-ci.md#feedback-loop). Fail closed on missing/corrupt outputs,
changed inputs, failed work or incomplete coverage. Do not invent per-case
serialization schemas or a generic identity engine for ordinary test reuse.

The final tagged release builds from clean local generated state. Preparation
success cannot stand in for freshly produced tagged bytes. Inside that release,
share equivalent producers/checks; host fixtures are not repeated in every target
lane. Build-tree, extracted SDK and distinct execution modes retain actual scope.

## Composable SDK acquisition and packaging

Pin published prerequisites by exact provider version, target, archive digest and
required package identity where declared. Validate the selection before acquisition.
Never substitute a sibling checkout, host libraries, another version or unresolved
planned package. Verified archive hits make zero acquisition network requests.

Stage only owned payload. Validate manifests, notices and public metadata; prevent
collisions between packages. Test declared install combinations and both extraction
orders when order independence is promised. Use CMake/CTest consumer projects.
Reuse identical prerequisite/consumer work while its inputs and delivered bytes match.

Release manifests describe stable public payload identity, including declared
versions, target, dependencies and ownership. Preserve existing external contracts;
keep local build state and workstation paths out of them. Do not infer a new schema
or format version solely because an implementation changed.

## Verification and performance acceptance

Use CTest fixtures to prove selected scope, no-op reuse and invalidation through
observable compiler/test/consumer invocation counts and actual output behavior.
Cover missing/corrupt prerequisites, test-only edits, changed tools/inputs,
interruption, wrong package identity, collisions, host fallback and partial evidence
presented as complete delivery. Generators/fixtures may use Python within this scope.

Measure cold and warm work with existing tools, configured limits and unchanged
coverage. Diagnose repeated producer work before changing cache or cleanup policy.
Source reconstruction and final release ordering follow [release.md](release.md)
and [packaging.md](packaging.md); they do not justify a custom lifecycle engine.

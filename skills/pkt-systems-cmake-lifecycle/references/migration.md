# Migration Procedure

## Migration Procedure

For an existing repository:

1. Inventory current public API, ABI, binaries, examples, tests, dependencies, release artifacts, e2e services, Lua artifacts, benchmarks, fuzz targets, vendored patches, and documented commands.
2. Classify each behavior into a lifecycle surface.
3. Preserve product behavior and declared artifact/consumer compatibility commitments. For pre-1.0 non-ABI refactors without an external-support commitment, prefer a clean cutover without legacy paths or shims; do not ask solely because the old non-ABI interface changes. Shared-library ABI and published bundle/dependency compatibility requirements still apply regardless of maturity.
4. Inventory public API style separately from implementation style. Prefer receiver-style handle functions for new usage. Preserve free-function compatibility surfaces when a mature API or declared support commitment requires them; otherwise follow the preceding clean-cutover rule.
5. Update examples and documentation snippets to the preferred public style and add executable checks that prevent regression to discouraged usage forms.
6. Replace bespoke command names with standard Make targets. Retain compatibility aliases only when a declared external-support commitment or explicit engineer request requires them. Documentation alone does not require legacy aliases for a pre-1.0 clean cutover.
7. Follow [tool ownership](../SKILL.md#first-rule-a-simple-native-pipeline): short Make recipes, readable Bash sequencing, one CMake graph and CTest-owned tests. Remove competing controllers and redundant wrappers within the authorized migration; do not port them wholesale into Bash/CMake. Python generators and fixtures are allowed; other pipeline use requires explicit developer permission, not a documented exception invented by the agent.
8. Normalize presets, target IDs, dependency roots, cache layout, host/bundled dependency modes, and target-tool discovery for packaging and verification.
9. If package, release, Darwin, or runtime-path scripts each discover tools independently, consolidate them behind a shared helper such as `scripts/discover_target_tools.sh` and add regression tests for configured CMake cache values, compiler sibling tools, osxcross-prefixed tools, and PATH fallback.
10. Add missing verification before deleting old behavior.
11. Remove dead lifecycle paths after the standard targets pass.
12. Run the relevant gates and report any remaining unsupported surfaces.

For non-trivial migrations, maintain `docs/lifecycle-migration.md` until the migration is complete. It should record:

- old command or behavior;
- new lifecycle command or surface;
- behavior preserved;
- verification added;
- behavior removed, deprecated, or intentionally changed;
- engineer decisions still required.

The migration ledger exists to prevent silent loss of bespoke project behavior. Keep it concise and delete it only when the repository has fully converged and the engineer does not want the ledger retained as documentation.

## Migration between independent provider repositories

When explicitly asked to separate repositories, inventory source, public API/ABI,
generators, schema inputs, dependency recipes, fixtures, services, documentation,
licenses/notices, installed consumers, workflows and release helpers before moving
files. Preserve seeded repository license, authorship and signing settings. Create
focused development branches before substantive work; use each new repository's
own identity for project metadata, archive names and GitHub publication.

Complete structural separation in every destination before starting production
builds when requested. Validate all owned source and helper inputs, including
files inside fixture directories, script executable permissions and source-archive
closure, including Memcheck suppression policy and nonempty fuzz seed corpora.
Preserve auxiliary data files regardless of filename extension. Prove missing
inputs fail before acquisition/compilation. Syntax checks
alone cannot certify a moved pipeline. Audit copied tool resolvers against the
core-owned skill helpers: `discover`/`env` must remain read-only, only explicit
`ensure` may provision, and callers must check the resolver status before `eval`.
Test absent and partially prepared collections without creating caches, downloading
or repairing tools, as well as complete existing collections and runner refusal
after failed environment resolution.

Use synthetic verified SDK fixtures for early import/scope regressions while the
first core release is unpublished. Keep real prerequisite pins visibly unresolved
until actual verified published assets provide target-specific checksums and
package IDs. Never fabricate production pins or use an older or local sibling
bundle to claim the new dependency is ready.

Build/test and release core first. After publication, populate optional pins from
that exact verified release, prove real imports and independent composition, and
leave their own release cycles separate. Keep source and destination repositories
available until the transfer is verified; archival is a separate maintainer action.
Update the core-owned lifecycle skill sources to match the working pipelines and
review their references together. Installation requires its own authorization.

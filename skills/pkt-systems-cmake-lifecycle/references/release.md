# Release Procedure

## Release Procedure

Prerequisites:

- All intended source, version, lifecycle, and documentation changes are committed before release flow proceeds. The worktree must be clean except ignored generated files.
- Documentation has been reviewed for the release candidate, updated when needed, and committed on the feature or fix branch before the release flow begins. Documentation updates found at release decision time are preparation work, not release-gate repair work.
- Prepare the candidate dependency inventory under [dependency-reporting.md](dependency-reporting.md), including SDK/stack dependencies, upstreams and vendored code. Reconcile it with the resolved inputs and required provenance before publication; the final report must use the exact verified release artifact inventory.
- If releasing from a feature or fix branch, that branch contains the committed work intended for release.
- If releasing directly from the release branch, that branch's current `HEAD` contains the committed work intended for release.
- All review issues are already addressed before release proceeds.
- Required local gates already pass before release proceeds.
- Release authority is clear.
- Release gates are stop-only. Reviews, tests, package verification, artifact checks, checksum checks, privacy checks, and publish preconditions must fail the release process when they find an issue. Do not fix code, amend commits, move tags, regenerate artifacts after a failed gate, or otherwise continue the release as part of the same flow unless the engineer explicitly starts a separate fix iteration.

Documentation preparation:

- When the engineer decides to release, first review the repository documentation against the candidate changes before starting the release flow.
- Review at least the public README, API documentation, examples, changelog or release notes when present, build/install/package instructions, and any user-facing CLI, CMake, Lua, or artifact documentation affected by the candidate changes.
- Update documentation when it is stale, incomplete, misleading, missing new behavior, or inconsistent with the release artifacts, supported platforms, command surfaces, examples, packaging surfaces, or compatibility promises.
- Commit required documentation updates on the feature or fix branch with a Conventional Commit before proceeding to branch decision, review gate, release plan preview, candidate-branch gates, squash, tag, artifact generation, push, or publish.
- If currently on the release branch and documentation updates are needed, stop before release flow begins and ask the engineer whether to create or switch to a feature or fix branch for the documentation preparation commit. Do not make documentation updates directly on the release branch unless the engineer explicitly instructs that release-branch documentation preparation is acceptable.
- If the documentation review finds no required changes, record that result in the release plan preview and continue only with a clean worktree.

Recommended Make target shape:

- Make exposes the release targets with simple prerequisites and short recipes. Readable Bash scripts own procedural sequencing, validation and cleanup; CMake supplies build, test, install and package operations. Keep the lifecycle simple under [operability.md](operability.md#tool-ownership-and-simplicity). Do not embed complex shell programs in Makefiles or recreate the release pipeline in Python. CMake must not own tag mutation or replace `make release` as the final public gate.
- `make prerelease` should run the ordinary local proof and a binary-only `release-matrix` without cleaning generated state first. This gives fast release-equivalent binary-package feedback while engineers are still iterating.
- `make release` must run `make lifecycle-version-contract` first, before cleaning generated state, building, packaging, or producing artifacts. It then cleans generated state, runs every ordinary proof required by `prerelease`, and enters the final matrix for all declared artifact surfaces, including production/reconstruction of source archives when shipped.
- Keep source-archive reconstruction out of `prerelease`, binary `release-matrix`, and `package-verify`. When source archives are shipped, it is the final all-component reconstruction gate: after binary packages exist, generate the source archive, run `package-source-smoke`, regenerate/check the manifest, and verify the complete release artifact set. Do not introduce a source distribution merely because this skill is active.
- `release-pipeline` should run the ordinary local proof in order, then the binary release matrix. Keep expensive optional hardening either inside the matrix when mandatory for release or behind an explicitly named target such as `prerelease-hardening`.
- Tag-mutating version and manifest contract checks must live behind the focused `make lifecycle-version-contract` target. `make release` is the only standard release-flow target that runs this check, and it must run it before `clean`, `release-pipeline`, `release-matrix`, `package-verify`, checksum generation, or artifact production. Do not wire tests that create, delete, or otherwise mutate git tags into `test`, `test-all`, `prerelease`, `release-pipeline`, `release-matrix`, `package-verify`, or any other late release-flow command. This placement makes manual release (`make release`, squash, tag, push) exercise the exact-tag contract while failing before expensive or publishable release work begins.
- `release-matrix` should build every supported release target, run host-executable tests for the host release target, run every cross-target QEMU test suite the project has opted into, produce binary SDK artifacts, generate the binary checksum manifest, and run binary package, checksum, privacy, relocatability, instrumentation-leak, and loader-metadata verification. `make release` then runs the final source-archive reconstruction and verifies the complete manifest. Selected QEMU coverage is mandatory: fail clearly when its runner or configuration is unavailable; see [local-ci.md](local-ci.md).
- `prerelease-artifacts`, when kept for compatibility, should be an alias for `release-matrix`.
- `prerelease-hardening`, when no extra hardening tier exists, should be an alias for `prerelease` until a real hardening tier is defined.
- `prerelease-live` must fail closed unless live external-provider checks are explicitly enabled through a documented environment variable and credentials are available.
- `make help` must describe `prerelease` as incremental ordinary proof plus binary-matrix verification. Clean `release` additionally runs the pre-clean version contract and final source/non-binary distribution gates. Prerelease does not supply their proof.

Executable lifecycle tests:

- Add a focused test that asserts `release` runs `lifecycle-version-contract` before `clean`, that it runs every ordinary prerelease proof before the final matrix, and that shipped source archives are reconstructed only on the final clean release path or through an explicitly requested standalone source-smoke command.
- Add focused tests for checksum-manifest generation and upload-set selection: in complete-release scope, every intended release artifact under `dist/` must be checksum-listed, every listed artifact must exist, and the manifest itself must be uploaded. Reject stale/unlisted release artifacts except documented exclusions. Selected/binary fixtures check their exact declared inventories and prove identified out-of-scope source/other-version artifacts neither enter nor satisfy their evidence; see [packaging.md](packaging.md).
- Add a focused `make lifecycle-version-contract` test for lightweight-tag version behavior and release-command version selection, following [temporary test-tag ownership](#temporary-test-tag-ownership).
- Add focused tests for artifact verification failures that previously could escape until publish time: local source/cache/build path leaks, local `file://` URLs, hardening or fuzzer instrumentation markers, non-relocatable RPATH/RUNPATH/install-name metadata, missing dependency manifests, and stale or omitted release artifacts.
- Tests should exercise observable release contracts through the public Make/script surfaces rather than only checking implementation details. Light structural tests are acceptable for target wiring because the target graph is part of the lifecycle contract.

Branch decision:

- Release from the repository's release branch. Resolve both `<release-remote>` and `<release-branch>` before touching branch state. Prefer the configured remote default branch, whatever it is named, and remember the remote that owns it. If the remote default cannot be determined, fall back to the local branch that exists among `main`, `master`, and `trunk` and use its configured upstream remote when present, otherwise `origin`. Stop and ask if more than one plausible local release branch exists and the remote default is unclear.
- Detect the current branch.
- If already on the release branch, release from the current `HEAD` after confirming it contains the intended changes and the worktree is clean.
- Establish the intended release base before candidate review/rehearsal. During preparation, fast-forward the local release base from the selected remote when needed, then record the local base commit, actual remote branch tip, candidate commit and candidate tree. A topic candidate must contain that release base as an ancestor. If it does not, stop for a separate integration/preparation iteration; do not merge new source after candidate proof. Keep these recorded identities fixed through squash, signing and final verification.
- If on a feature or fix branch, keep that branch intact, run review and the full clean candidate-branch release rehearsal there against the established base, then squash it onto the release branch as one Conventional Commit with the same tree.
- If the resolved release branch exists only as `<release-remote>/<release-branch>`, create the local tracking branch during base preparation, before candidate gates, and verify it points at the remote branch. Stop and ask if neither a local nor remote-tracking release branch exists, if `HEAD` is detached, if `HEAD` is already on a tag, if a merge/rebase/cherry-pick is in progress, or if the current branch has ambiguous release intent.
- Never push or publish from a dirty worktree.
- Do not push the release branch or the tag until final tagged `make release` and every local artifact verification gate have succeeded. They remain local even when hosted macOS verification is enabled; never push them just to trigger Actions. A failed local release must remain repairable without rewriting remote release history.
- An explicit hosted-verification opt-in separately authorizes normal development-branch pushes (including remote branch creation/upstream setup) before release, under [github-actions.md](github-actions.md). It does not grant release publication authority or permit early release-ref pushes. Adopt the declared workflow's push/manual triggers; do not run identical hosted coverage twice.

Version decision:

- Inspect existing tags matching `vX.Y.Z`.
- Determine the next semver from the change set for release planning only. Do not let this planned version become the build or artifact version until the matching lightweight `vX.Y.Z` tag exists on `HEAD`.
- Use patch for compatible fixes and internal lifecycle repairs.
- Use minor for backward-compatible features, new APIs, new optional artifacts, new optional targets, or new non-breaking lifecycle surfaces.
- Disclose significant shared-library ABI changes and apply the necessary ABI-version bump from the last released baseline under repository authority. Discuss transitions across declared artifact/consumer/dependency compatibility commitments before choosing the release bump; for mature public APIs this includes breaking API, CLI, CMake/pkg-config, Lua and source-package interfaces. Pre-1.0 non-ABI cleanups without such a commitment do not require permission solely because an old spelling/layout changes; report the change and apply project policy. Published bundle/dependency compatibility remains required regardless of maturity.
- Do not bump the major version automatically. A major bump is a product decision and requires explicit engineer agreement.
- Treat any Conventional Commit with `!` or a `BREAKING CHANGE:` footer as a release-planning escalation, not an automatic major bump.
- Treat `fix:` as patch by default.
- Treat `feat:` as minor by default.
- Treat `perf:`, `refactor:`, `build:`, `ci:`, `test:`, `docs:`, and `chore:` as patch unless they introduce a public feature or breaking change.
- For `0.y.z` projects, breaking changes may stay within major version `0`; state their nature and choose the requested/appropriate minor or patch under project policy. Ask when the bump or a compatibility transition requires a decision, not merely because a pre-1.0 non-ABI cleanup occurred.
- Ensure generated headers, package metadata, Lua rockspecs, source archives, generated source-archive `VERSION`, and single-header artifacts agree with the selected version after the selected version is represented by a lightweight `vX.Y.Z` tag on `HEAD`.
- Version detection for git worktrees must prefer only an exact lightweight `vX.Y.Z` tag on `HEAD`, then a deliberate project-prefixed version override for release candidates when explicitly supplied, and otherwise `0.0.0`. Lightweight means the tag ref resolves directly to a `commit` object; annotated or signed tag objects must not satisfy the release contract or produce a release version from shared version resolver surfaces such as `make print-release-version`, package naming, source archive naming, checksum naming, or package verification. A git worktree with no exact lightweight `vX.Y.Z` tag on `HEAD` must never default to the planned next release version, `0.1.0`, or any other inferred semver.
- Git worktree version detection must not read `VERSION`. `/VERSION` should be ignored in git repositories. Version detection outside git uses the source-archive `VERSION` injected during staging. Publishable source archives require the exact lightweight release tag and its version. Nonpublishable rehearsal archives use the resolved candidate version (`0.0.0` or an explicit project-prefixed override) and inject that same value for full reconstruction; they do not require or authorize a release/candidate tag. Both rehearsal and final tagged runs exercise source generation/reconstruction, and only tagged final artifacts can be uploaded.
- Release-candidate overrides must be tested through both CMake and Make surfaces so Lua artifacts, source archives, package metadata, and checksum names cannot silently fall back to `0.0.0`.
- Verify the selected `vX.Y.Z` tag does not already exist locally or on `<release-remote>`.
- Verify the selected version is greater than the highest existing stable `vX.Y.Z` tag.
- Ignore prerelease tags for stable version ordering unless the release being prepared is explicitly a prerelease.
- If the change set includes multiple categories, choose the highest required bump.
- If the bump cannot be determined from commits and diffs, stop and ask before touching the release branch.

Review gate:

- Resolve the repository default branch as described above (normally `trunk` for pkt.systems) and run every independent local Codex review against it: `codex review -c model=gpt-6.1-sol -c model_reasoning_effort=high --base <default-branch>`.
- For direct release from the default branch, that branch cannot also be the review base: the comparison would be empty. Reuse recorded candidate review evidence only for the identical tree and established baseline. Otherwise verify an ancestor baseline from the previous actually published release, and explicitly use its local tag/ref with the same review model/effort and `--base`. Stop if no authoritative meaningful baseline can be established; do not report a self-comparison as a clean review.
- Do not use `codex review --uncommitted`; review committed candidate changes against the default-branch baseline.
- Do not run a second Codex review after squashing onto the local default branch when the squash commit contains the same code already reviewed against `<default-branch>`. The squash changes commit topology, not the reviewed tree content.
- Treat actionable findings as blockers and stop the release process.
- Do not address review findings during the release process. Report them and wait for the engineer to start or approve a separate fix iteration.

Release plan preview:

- Before touching the release branch, prepare and report a concise release plan.
- The plan must include selected version, tag, candidate branch, release remote, release branch name, intended release-base preparation and pinned commit/tree identities, review baseline, intended Conventional Commit summary, expected artifacts, candidate-branch clean release rehearsal, final tagged clean release gate, optional gates skipped, and engineer decisions required.
- When hosted verification is enabled, include the opted-in repository/workflow, development push destination, required native source coverage, and exact commit identity for each gate. Distinguish candidate checks from final tagged-commit checks and keep all release-ref pushes after local tagged proof. Release assets come from the local build, including osxcross Darwin outputs on Linux; Actions-built SDKs are diagnostics only. Do not add a draft-release handoff or draft-read secret requirement.
- The intended Conventional Commit summary must describe the durable repository change being squashed, such as what was added, fixed, changed, removed, or refactored. Do not use generic release-process wording such as "release version X.Y.Z", "prepare release", "preparing for release", "prep release", or similar; the commit is not the release action and must not be named as if it only performs release administration.
- If the release plan has unresolved engineer decisions, stop before touching the release branch.

Candidate clean gate before squash/tag:

Run this for every release candidate after establishing the release base,
including direct releases from the default branch. For a topic branch it
precedes squash; for a direct release it runs on the current untagged release
candidate with its meaningful review baseline. It always precedes tagging and
proves that candidate tree and effective inputs from a clean slate. Changes to
the pinned release base, source tree or effective inputs require renewed preparation
and proof. An authorized equivalent squash or signing/message-only amendment may
retain matching source-tree/review proof with its original provenance; exact-commit
evidence and ref checks must identify the resulting commit. Direct release does
not eliminate this first clean run or the later final tagged clean run.

The supplementary commands below apply only to required proof not already
included in the successful clean rehearsal, or an explicitly requested extra
tier. Inspect the repository's actual target graph and coverage first; do not
repeat Valgrind, fuzz, e2e, Lua, source reconstruction or package verification
merely because their names also appear in this list. Keep the final tagged
clean run mandatory even when the candidate rehearsal covers every gate.

1. `make release` when the repository defines `release`. This is a clean release rehearsal on the candidate branch, not a publishable tagged release. In an untagged git worktree it may resolve the version as `0.0.0` or use an explicit project-prefixed release-candidate override when supplied, but it must exercise the same build, test, package, checksum, and verification graph as the final release.
   - This gate runs `make lifecycle-version-contract` first. Any reserved temporary lightweight-tag mutation must complete and clean up before `clean`, build, package, checksum, or artifact work starts.
2. If the repository does not define `make release`, run `make clean`, then `make prerelease` when the repository defines it; otherwise run `make test-all` and the explicit local gates below.
3. `make valgrind` when supported and not already included in `prerelease`.
4. `make fuzz` when fuzzing is part of the project hardening contract.
6. `make fuzz-smoke` when fuzzing exists and is not already included in `prerelease`.
7. `make bench-gate` or `make perf-gate` when performance gates exist.
8. `make test-e2e` when deterministic e2e exists and is not already included in `prerelease`.
9. `make lua-test` when Lua exists and is not already included in `prerelease`.
10. `make package-source-smoke` when source archives are shipped.
11. `make package-verify`
12. `make prerelease-live` only when release authority includes live external-provider verification and required credentials are available.
13. `make prerelease-hardening` when the repository defines it and the engineer expects the expensive hardening tier before release.
14. Do not run or add a separate umbrella release gate. The final clean gate is `make release`.

If `make release` fails, stop the release process. If the failure is only that the repository refuses untagged release rehearsals, report that lifecycle gap and the closest available clean gate, but do not patch the lifecycle, squash, tag, push, or publish in the same release flow.

When opted in, push the clean committed development branch and obtain the
declared candidate native evidence through the existing workflow's trigger or
manual dispatch. Reuse an already-successful exact-commit run only when its
workflow, selected coverage, and dependency/artifact identities meet the gate.
Required candidate evidence must pass before squash; supplemental evidence is
reported separately. Candidate success does not establish success for the final
squashed/signed commit or the final distribution bytes.

Release matrix gate:

For implemented package groups, every clean release run covers the complete
inventory and combinations. Reject explicit partial group/target/evidence selectors
before mutation. Matching producers and exact repeated fixtures may be reused within
that run; prior rehearsal evidence cannot satisfy the final tagged clean run. Follow
[package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md).

- The release matrix builds every supported target preset.
- It runs host-executable release tests for the host target.
- It packages every target archive.
- It includes applicable binary SDK runtime libraries and binary smoke bundles, with installed headers, metadata and notices. Follow [lua.md](lua.md) for its source-only Lua facade distribution contract unless a combined artifact is explicitly selected. Final `make release` assembles source archives, standalone Lua source packages/source rocks, single-header distributions and other non-binary artifacts after this matrix, reconstructs the source archive, then verifies the complete manifest.
- It generates the explicit binary-scope checksum manifest under `build/` after its selected artifacts exist; do not replace the final distribution checksum manifest with a partial set. Follow [packaging.md](packaging.md) for selected inventory and warm-run treatment of existing out-of-scope source/other-version artifacts. Final `make release` regenerates/checks the complete artifact manifest.
- It runs binary artifact, privacy and relocatability verification from that scoped manifest. Final `make release` additionally verifies nested source archives/source rocks and every complete-manifest payload.
- It may skip optional targets whose cross toolchain is unavailable only with a clear message and only when that target is not mandatory for the release.

Squash and tag:

1. If on a feature or fix branch, remember the branch name and commit range.
2. Switch to the release branch.
3. Verify the release base and actual remote tip still match the identities recorded before candidate gates. Do not pull/integrate new commits after those gates. If either changed, stop for renewed preparation and proof.
4. If releasing from a feature or fix branch, squash that branch onto the release branch as one Conventional Commit whose subject answers "this commit will..." and whose body describes only durable repository changes: behavior, APIs, build/test surfaces, packaging, artifacts, and release impacts. Do not include non-change process actions such as Codex review runs, verification commands, gates passed, local rehearsal status, sign-off discussion, or other work performed to gain confidence; those belong in the final report, not the commit message.
5. If releasing from the release branch directly, do not squash; use the current `HEAD`.
6. Assert the resulting `HEAD^{tree}` equals the recorded candidate tree. Stop before tagging and ask the engineer whether to sign or otherwise amend the squashed commit, or continue with the current commit unchanged. Do not create the tag until the engineer explicitly chooses. If the engineer signs or amends the commit, re-read `HEAD`, verify the commit message and plan, and require the exact same tree. A content change requires a separate preparation/fix iteration and renewed review/rehearsal; signing/message-only changes do not invalidate source-tree proof.
7. Ensure no checked-in version file is being used for the git-worktree version. The selected version becomes active only through the lightweight `vX.Y.Z` tag on `HEAD`; generated source-archive `VERSION` files are created later as non-git release artifacts.
8. Create a lightweight tag on `HEAD` with `git -c tag.gpgSign=false tag vX.Y.Z`. Do not create an annotated or signed tag.
9. Verify the tag did not already exist locally or on `<release-remote>` before creating it.

Tagged release artifact generation from the release branch:

1. Run the repository's real release artifact generation pipeline from the tagged release branch, normally `make release`. This is the artifact-producing release build, even if the same command was already used as the clean candidate-branch rehearsal before squash.
2. The release pipeline must clean generated output, build release artifacts under `dist/` using the lightweight `vX.Y.Z` tag on `HEAD` as the version source, generate checksums, and run package verification. If no exact lightweight `vX.Y.Z` tag is on `HEAD`, the release pipeline must resolve the version as `0.0.0` and fail before publishing release artifacts rather than silently using `0.1.0` or another inferred version.
3. Run checksum verification when it is not already part of the release pipeline.
4. Run release privacy and relocatability verification when it is exposed as a focused target; otherwise confirm it is included in package verification or the release pipeline.
5. Do not rebuild artifacts after this point.
6. Verify `dist/<project>-<version>-CHECKSUMS` lists exactly the intended upload artifacts, and include that `CHECKSUMS` file itself in the GitHub release uploads. Record its SHA-256/size and the expected payload names, sizes and digests in release evidence before any upload so retries and final remote verification use the same immutable set.
7. Assert every checksum-listed artifact exists under `dist/`.
8. Assert no extra release-looking artifact under `dist/` is omitted from the checksum manifest unless deliberately excluded.
9. Verify `git rev-parse HEAD` matches `git rev-parse vX.Y.Z`, the tree still matches the proven candidate, and the actual remote release branch has not advanced unexpectedly since the pinned baseline. An already-identical release commit is acceptable for a verified operational retry; other advancement stops the flow.
10. Only after every preceding local gate passes, push the release branch to `<release-remote>` with an explicit branch refspec and `--no-follow-tags`; do not implicitly publish the tag with the branch push.
11. Verify the actual remote branch ref resolves to local `HEAD`; a stale remote-tracking ref alone is not proof. Keep that exact commit identity for the remaining steps.
12. Push only the verified lightweight release tag to `<release-remote>` with an explicit tag refspec; do not use `--tags` or push unrelated refs.
13. Verify the remote tag identifies the same commit as local `HEAD`.
14. When project policy requires final hosted native source evidence, obtain success for the exact final commit and declared test inventory before publication; follow [github-actions.md](github-actions.md). Reuse an automatic run on the released branch when it supplies that proof. Run the native build and tests together; do not require a draft handoff, draft-read secret, or additional hosted archive lane. A failed/missing required source gate stops publication. Its native build outputs remain diagnostics and must never replace the verified local release artifacts.
15. Confirm the dependency report describes the exact verified artifact set, target differences and external consumer requirements; keep test/build-only inputs separate. Use existing artifact evidence without rebuilding or changing final artifacts.
16. Publish the GitHub release locally with `gh release create vX.Y.Z`, using the verified local checksum-listed artifacts plus the checksum manifest itself. If an explicitly requested optional archive-transport operation already created a draft for this tag, verify its complete asset set against the same immutable local upload set and publish that existing draft locally with `gh release edit vX.Y.Z --draft=false`; do not try to create it again. A draft is not required for the normal release. Never publish Actions-built artifacts or delegate release publication to Actions. Do not upload from a `dist/` glob. This step requires release authority independently of hosted-verification opt-in.
17. Verify the actual remote asset set is exactly the checksum-listed payloads plus the verified checksum manifest, with matching sizes/digests through the declared remote-verification mechanism. Missing, extra or mismatched assets prevent a completion claim; follow the retry rules for transient missing-upload failures only. After this proof succeeds, include the released dependency inventory in the completion report alongside the tag/commit and release URL.

For GitHub asset proof, resolve the release ID for the selected repository/tag
and query `GET /repos/{owner}/{repo}/releases/{release_id}/assets`, including all
pages. Compare each asset's name, `state=uploaded`, size and `digest=sha256:...`
with the recorded local upload set, including the manifest. Missing digest
evidence is an unverified delivery, not success. These are control-plane
metadata requests, not permission to reacquire cached dependency archives.
GitHub documents these fields in its [release asset API](https://docs.github.com/en/rest/releases/assets).

## Failed local release recovery

If tagged release artifact generation or verification fails, stop the release
process and report the failing gate and current local state. Keep the release
branch and tag local; do not fix, rebuild or publish in that failed release flow.

Inspect the actual local and remote refs and present the proposed rewind before
asking for one approval. For a topic-branch release, return to the original
candidate branch, remove the owned local lightweight release tag, and restore the
unpushed release branch to the recorded pre-squash base. Preserve the intended
work on the candidate branch. If the original branch or base is unavailable,
establish how to preserve the work before requesting approval.

Once the engineer approves that rewind, execute and verify the entire approved
operation without further per-step permission requests. If the inspected state
changes or the operation fails, stop and report it; do not widen the approved
scope. Start the separately authorized fix iteration from the restored branch,
then restart the release gates from scratch.

Development Actions opt-in does not authorize a rewind. If release refs were
already pushed, leave them visible and the release unpublished; remote-history
repair requires its own explicit decision.

Release retry protocol:

- Retry only transient external publish failures that do not indicate a correctness problem in the release contents. Do not retry review, build, test, package, checksum, privacy, artifact, or metadata failures inside the release flow.
- Before any external publish retry, freshly query the selected remote's actual `refs/heads/<release-branch>` and release tag ref; local tracking refs are insufficient. Unexpected remote advancement or a different tag object stops the flow rather than qualifying as a transient retry.
- If pushing the release branch fails, stop before pushing the tag. Retry only after verifying the failure was external or operational, the actual remote branch is still the pinned baseline or already the exact release commit, the local release branch still points at the tagged commit, and no local content changed.
- If pushing the tag fails after the release branch was pushed, retry only after verifying the failure was external or operational, the remote release branch points at the same commit as the local release branch, local `vX.Y.Z` points at `HEAD`, and no remote tag with a different object exists.
- If `gh release create` fails after the release branch and the tag were pushed, do not rebuild artifacts and do not move the tag. Retry only after verifying the failure was external or operational, fresh actual remote branch/tag queries and local `HEAD`/`vX.Y.Z` all identify the same commit, and the complete checksum manifest still verifies the same checksum-listed artifacts.
- If a GitHub release was partially created, inspect it with `gh release view vX.Y.Z`. For transient missing uploads, select only missing members of the complete verified upload set: checksum-listed payloads plus the checksum manifest itself. Verify payload checksums and the recorded manifest identity before upload, then recheck the entire remote set before reporting success. If it exists with wrong/extra assets, stop and ask before deleting or replacing anything.
- Never force-push the release branch or retarget an already-pushed release tag without explicit engineer approval.
- If the pushed tag is wrong, stop immediately. Do not publish or repair silently.

Do not publish a release from an untagged commit, from a dirty worktree, from artifacts built before the final tag, from a tag that is not on `HEAD`, or from a `dist/` glob.

## Temporary test-tag ownership

The version-contract test uses a reserved temporary tag such as `v99.99.99` on
the current `HEAD`. Run it through `make lifecycle-version-contract`, before any
clean/build/package work, as prescribed above. Test the active checkout rather
than another checkout, worktree, copied repository or source-archive fixture.
CMake version propagation remains part of the normal build/package checks;
this focused tag test does not configure CMake.

- Create a lightweight tag with signing disabled, for example
  `git -c tag.gpgSign=false tag v99.99.99`. Assert that accepted version tags
  resolve directly to a `commit`; reject annotated or signed tag objects.
- When `HEAD` already has an exact non-reserved lightweight release tag, verify
  its precedence and skip temporary-tag creation.
- Record ownership under ignored `build/`. A nonce-bearing intent before exclusive creation
  permits recovery if the process dies before recording completed creation.
  Authenticate the zero-to-object Git reflog entry with the recorded nonce and
  object ID; intent alone does not prove tag ownership.
- Fail on an unowned pre-existing tag or mismatched ownership evidence.
  Automatic recovery and trap cleanup use compare-and-delete against the recorded object,
  preserving changed or unowned refs. A catchable exit cleans the owned tag;
  a later invocation recovers an interrupted test using the same ownership checks.
- Verify the reserved tag selects its version through the release script and
  Make surfaces, and cleanup restores the original version.

Automatic cleanup applies only to the owned temporary test tag. Actual release
tag removal and branch rewind follow [failed local release recovery](#failed-local-release-recovery).

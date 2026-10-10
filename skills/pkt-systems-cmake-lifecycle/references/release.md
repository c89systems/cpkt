# Release one verified artifact set

Release authority and project branch/commit policy govern publication.
Preparation is incremental; final tagged release is one clean local artifact build.
Failures stop. Repair is a separate authorized iteration, not an in-flow fix/retry.
The release boundary in SKILL.md has no exceptions: the amended, signed,
lightweight-tagged final commit must complete the entire local release gate before
any default/release-branch or release-tag push. Never push for CI or validation.

## Preparation

Review/update affected public documentation and commit the intended source.
Resolve release remote, branch and GitHub repository from the declared project;
fetch/push destinations must agree. Pin base commit, candidate tree and review baseline.
The candidate contains that base; integration changes happen before proof.

Complete only missing required native, hardening, selected-target and binary package
coverage. Reuse matching review for the same tree/scope/baseline. No full matrix
between tiny edits. Explicit cold audits use preparation/source-smoke gates.
Hosted checks are optional diagnostics under [opt-in policy](github-actions.md).
They never block release or provide its required proof or artifacts. Declared
Darwin release artifacts are built and verified locally through osxcross.

Choose semver from product/support policy: compatible fixes normally patch,
compatible features minor; material major/support transitions require direction.
Shared ABI changes follow the independent released baseline. Do not infer a
public format version increment from implementation change alone.

Report a concise plan: version/tag, source/base/tree identities, remote/repository,
review scope, artifacts, missing gates and required decisions. No new release-plan
schema or persistent evidence service.

## Version and branch boundary

Owned Git builds use an exact lightweight vX.Y.Z tag on HEAD, an explicitly selected
candidate override during preparation, or 0.0.0. They never use a checked-in VERSION
or infer the planned next version. Reject ambiguous exact release tags.
Annotated/signed/symbolic tags do not supply the lightweight release contract.

Extracted source uses its own injected VERSION and RELEASE_MANIFEST. Git discovery
of an enclosing checkout does not establish ownership; compare physical source root
with the declared owning Git root. Intentional subprojects declare that owner.

Prepare the release commit locally against the pinned release base, preserving
the development branch and leaving remote release refs unchanged. For a topic
release, squash the verified tree into one factual Conventional Commit.
Present that commit for the engineer to amend and sign. Verify the completed
signature and exact tree before tagging; unsigned continuation is forbidden.
"I have amended/signed the commit" is the continue checkpoint: verify it and
proceed with the authorized release, without asking for the same permission again.
Message/signature-only changes preserve preparation proof. Freeze the final
commit identity before the clean release build; later amendments require a new
final gate against the amended, signed, lightweight-tagged commit.

Verify tag absence locally/remotely and create the selected lightweight tag with
signing disabled. Tests of tag variations, when a project needs them, belong in
that project's disposable fixture environment. Do not mutate active release refs
for a lifecycle test or build a temporary-tag recovery protocol.

## Clean release gate

Before cleaning, compilation or packaging, require a clean committed worktree and
the amended final commit's verified signature and exact lightweight tag on HEAD,
with no candidate override. No unsigned commit or pre-signing build can authorize
a release push.
Read-only lifecycle-version-contract checks may expose that validation.
Invalid identity preserves generated outputs and starts no producers.

Clean the release checkout's entire build/, dist/ and local .cache/ once;
preserve only the declared global verified cache and workstation prerequisites.
A worktree is not a cleanup workaround: it must not reach into another checkout's
sources, build products or locally generated tools. Do not redirect caches to evade clean.
Build each effective configuration once, sharing prerequisites. Run each required
check once in its actual scope. If source ships, its clean reconstruction can supply
the native Release lane. Do not require every cross target to move to that workspace.

Verify final bytes, versions, checksums, complete manifest, privacy, relocation,
selected runtime coverage and licenses under [packaging](packaging.md).
No rebuild after final proof. Record the immutable upload set including the
checksum manifest's own identity, using existing logs/manifests.

## Publish and retry

After the complete local release gate passes, recheck the pinned remote branch
and release-tag absence immediately before mutation. Resolve one explicit release_repository
([HOST/]OWNER/REPO); every gh release create/edit/view/upload supplies --repo.
Use explicit hostname/owner/repository for API calls; no ambient GH_REPO/default routing.

Push only the completed release commit and verified lightweight tag with explicit
refs and no automatic tag following. The signature, tag, completed build and
verified upload set must all identify the frozen final commit. Check actual
remote identities. Never push early for GitHub Actions or wait for hosted native
builds as a release condition.
Publish local checksum-selected payloads plus their manifest with
`gh release create "$release_tag" --repo "$release_repository" --verify-tag ...`.
The arguments come from the verified inventory, never a dist glob. An existing
authorized draft is verified and published in that same repository.

Verify final remote asset membership, uploaded state, sizes and SHA-256 digests.
Missing proof is unverified delivery. gh/API queries do not authorize archive downloads.

Retry only transient external publish failures, using fresh ref queries and the
same immutable local bytes. Upload only verified missing assets. Wrong/extra assets,
changed refs or correctness failures stop; no silent deletion, overwrite, retag or
force push. Failed local release refs remain local. A proposed local rewind needs
one concrete engineer approval; preserve work on the candidate branch. Pushed refs
remain visible until an explicit recovery decision.

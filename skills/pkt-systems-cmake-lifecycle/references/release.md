# Release one verified artifact set

Release authority and project branch/commit policy govern publication.
Preparation is incremental; final tagged release is one clean local artifact build.
Failures stop. Repair is a separate authorized iteration, not an in-flow fix/retry.

## Preparation

Review/update affected public documentation and commit the intended source.
Resolve release remote, branch and GitHub repository from the declared project;
fetch/push destinations must agree. Pin base commit, candidate tree and review baseline.
The candidate contains that base; integration changes happen before proof.

Complete only missing required native, hardening, selected-target and binary package
coverage. Reuse matching review for the same tree/scope/baseline. No full matrix
between tiny edits. Explicit cold audits use preparation/source-smoke gates.
Hosted checks apply only under [opt-in policy](github-actions.md).

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

For a topic release, squash the verified tree onto the pinned release branch as
one factual Conventional Commit. Recheck unchanged base/remote tip and exact tree.
Before tagging, obtain the engineer's signing/amendment-or-continue decision;
existing explicit direction remains authoritative. Content changes renew affected
proof, while message/signature-only changes preserve tree proof.

Verify tag absence locally/remotely and create the selected lightweight tag with
signing disabled. Tests of tag variations, when a project needs them, belong in
that project's disposable fixture environment. Do not mutate active release refs
for a lifecycle test or build a temporary-tag recovery protocol.

## Clean release gate

Before cleaning, compilation or packaging, require a clean committed worktree and
the selected exact lightweight tag on HEAD, with no candidate override.
Read-only lifecycle-version-contract checks may expose that validation.
Invalid identity preserves generated outputs and starts no producers.

Clean owned local generated state and dist once; preserve shared verified caches.
Build each effective configuration once, sharing prerequisites. Run each required
check once in its actual scope. If source ships, its clean reconstruction can supply
the native Release lane. Do not require every cross target to move to that workspace.

Verify final bytes, versions, checksums, complete manifest, privacy, relocation,
selected runtime coverage and licenses under [packaging](packaging.md).
No rebuild after final proof. Record the immutable upload set including the
checksum manifest's own identity, using existing logs/manifests.

## Publish and retry

Recheck actual remote branch/tag against the final commit immediately before
mutation, including after hosted waits. Resolve one explicit release_repository
([HOST/]OWNER/REPO); every gh release create/edit/view/upload supplies --repo.
Use explicit hostname/owner/repository for API calls; no ambient GH_REPO/default routing.

After all local gates pass, push only the release branch and verified tag with
explicit refs, no automatic tag following. Check actual remote identities.
Obtain required final hosted commit evidence before publication where selected.
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

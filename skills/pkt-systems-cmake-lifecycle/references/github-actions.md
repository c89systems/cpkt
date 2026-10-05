# Optional native macOS verification on GitHub Actions

## Enablement and authority

Local commands own the lifecycle. Offer native macOS verification when the
repository is hosted on GitHub and a suitable runner is available. Inspect the
existing workflows and project policy first. Availability, public visibility,
and a `.github/` directory do not constitute opt-in.

Release archives are built, verified and published locally through the lifecycle.
On a Linux release host, the configured osxcross toolchain produces the Darwin
release archives. GitHub macOS Actions builds and tests provide native verification
evidence only; their SDK archives are diagnostic artifacts, never release assets.
Run the native build, runtime tests and installed-SDK checks together in the
existing workflow. The opt-in does not require a draft release, a draft-read
secret, or a second lane to transport local release archives to the runner.

An explicit engineer opt-in authorizes the lifecycle to push the development
branch normally, create it on the selected remote if absent, set its upstream,
and trigger the declared verification workflow. This authority persists across
ordinary implementation/fix iterations until withdrawn; do not ask again for
each push. A checked-in project policy may record an existing engineer opt-in.
Reading this skill or asking to document the option does not enable it.

Record the selected GitHub repository/remote, workflow path, native target,
test profile, and whether the check is supplemental or required for completion,
merge, or release. Put the declaration in existing project configuration or
documentation; no new configuration format is required. Required coverage and
branch protection are separate decisions from enabling the extension. Preserve
an existing mandatory gate rather than downgrading it to supplemental.

Check Actions availability, caller authentication/write access, the selected
runner, and applicable limits before launching work. Use standard public-repo
runners when suitable; recheck current terms rather than promise permanent free
compute. Private repositories and paid runners need an explicit cost decision.
See GitHub's [runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
and [billing guidance](https://docs.github.com/en/billing/concepts/product-billing/github-actions).

The opt-in covers verification and normal topic-branch publication. It does
not authorize force pushes, remote ref deletion, creating pull requests,
changing repository visibility/settings, provisioning credentials, publishing
a release, or pushing the default/release branch or release tags early.
Resolve the actual default/release branch using [release.md](release.md);
`trunk`, `main`, and `master` are never development push destinations.

## Adopt the repository's existing workflow

Keep its declared architecture, deployment target, test coverage, job policy,
timeouts, diagnostic artifacts, and stable check names. Discover trigger filters
before pushing. A development push may already start the required run; use it
instead of dispatching the same coverage twice.

When a branch is outside the push allowlist, use an existing `workflow_dispatch`
on that development branch. Dispatch requires the workflow to exist on the
remote default branch and support that event. If no authorized trigger works,
report the missing trigger and propose a workflow change on the development
branch. Never push `trunk` or a release tag to make a development check run.
See GitHub's [manual dispatch requirements](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow)
and [CLI dispatch reference](https://cli.github.com/manual/gh_workflow_run).

Before a push, require a clean committed worktree and a named development
branch. Resolve its intended remote repository and destination explicitly;
do not trust an upstream that points at the release branch. Record the local
commit and inspect remote destination state. Use a normal fast-forward push,
disable automatic tag following, and name only the development branch ref.
Reject mirror remotes and configurations that would publish additional refs.
A rejected/diverged push is a blocker, not permission to force it.

After those checks, the command shape is:

```sh
git push --no-follow-tags --set-upstream "$verification_remote" \
  "HEAD:refs/heads/$development_branch"
# Only when the push did not start the declared workflow:
gh workflow run "$verification_workflow" --repo "$verification_repository" \
  --ref "$development_branch"
```

The variables must come from the inspected repository and opt-in declaration.
Verify the remote branch resolves to the recorded commit before dispatching;
check that the selected workflow actually checks out that revision. If another
push races with dispatch, reject mismatched evidence and run the intended
revision again through an authorized development ref. Do not move a release
ref to recover from that race.

## Work and evidence contracts

Repository Make targets, CMake presets/labels, and scripts own dependency
resolution, builds, tests, packaging checks, and cleanup. A workflow supplies
the native machine, checkout, cache transport, and evidence. When creating or
migrating a workflow, expose its selected proof through documented Make
targets such as `test-darwin-native` and
`test-github-actions-contracts`; describe them in `make help`. Preserve an
existing workflow's behavior while moving recipes behind these targets.
Do not introduce an arbitrary-command input or a second release pipeline.

Native Darwin uses the selected runner's Xcode/Apple Clang tools, discovered
through `xcrun` where appropriate, rather than Linux osxcross wrappers.
Select compilers, SDK and inspection tools before producer configuration and use
the same discovery for extracted-SDK consumers. System compiler dispatchers and
the selected Xcode compiler can have different paths; do not mix those selection
routes or weaken mismatch checks to conceal a real toolchain change. Propagate
the selected SDK to external builds too: CMake subprojects receive
`CMAKE_OSX_SYSROOT`, while direct C/C++ and Make/Autotools invocations receive
`SDKROOT` in their configure, build and install environments. Clang's Darwin
driver uses an absolute existing `SDKROOT` as its default sysroot. Avoid embedding
shell quotes in scalar `CFLAGS`/`CXXFLAGS`: Autoconf can expand them without
reinterpreting those quotes. Preserve SDK paths containing spaces through the
environment or separate argument APIs, and test actual header lookup after the
external build's flag expansion, independently of CMake's compiler probes.
Export that same selected `SDKROOT` to native test and verification subprocesses
too; direct header, archive and export probes must not depend on a system
compiler dispatcher supplying an implicit SDK. Keep osxcross-only CMake inputs
out of native upstream projects, where they are unused.

Give direct/pkg-config verification executables the selected SDK library search
path explicitly; CMake's automatic build RPATH does not apply to a raw compiler
link. Pass link flags as separate arguments, including paths containing spaces,
rather than shell-quoted scalar environment strings. Run these links through the
same warning gate as CMake consumers. Remove duplicate shared/system `-l` flags
when Apple ld reports them, while preserving static archive order and repetitions
that may be needed for resolution. Exercise installed examples through both
CMake and pkg-config before and after SDK relocation.

Tool identities in consumer receipts follow the target format: `otool` for
Darwin, `readelf` and selected GNU runtime archives for Linux. An unused
`CMAKE_READELF-NOTFOUND` cache value on Darwin is normal; it is not a file to
fingerprint. Missing tools required for the selected target still fail closed.

Record OS/architecture, runner image, compiler/Xcode/SDK, deployment target, dependency
pins, and configured job limit. Keep the existing build-system parallelism;
a larger host does not authorize increasing it. Serialize operational commands
and test execution. Add older macOS or Intel coverage only when selected by
project policy; a newer arm64 run proves only its declared platform coverage.
Keep Linux Podman service e2e on its existing execution route.

Native verification records the tested commit and exact selected test inventory,
including installed-SDK checks against the archives built in that run. This
proves the native build and consumers; it does not prove execution of the local
osxcross archive bytes. Report that distinction without adding a separate archive
handoff gate. Testing supplied local archives is additional work only when the
engineer explicitly requests it; establish its transport and evidence contract
then. PR merge revisions and source heads must be identified separately; a tested
merge revision does not prove the source head was run.

Track a specific run ID and attempt for the declared repository, workflow,
event/ref, and expected commit. When dispatch returns no run ID, discover it
using workflow/branch/event/commit filters and the dispatch time; disambiguate
concurrent runs. Never accept the latest branch run without identity checks.
The [CLI run-list reference](https://cli.github.com/manual/gh_run_list) describes
these filters. Wait with bounded polling and inspect the selected run's jobs
and reports. Overall `success` is insufficient if required jobs or tests were
skipped or missing. Require all selected checks to complete successfully, plus
their expected inventory and cleanup evidence. Queued, cancelled, timed-out,
neutral, skipped, missing, or wrong-revision results are not success.

Retain compact machine-readable evidence, selected test names/counts, test
results (JUnit where supported), and failure logs under `build/`. Upload bounded
diagnostics on failure as well as success. Do not upload entire build trees,
credentials, or signing material. Preserve existing useful diagnostics during
migration. Cancel superseded development runs only under the declared policy;
never let such cancellation satisfy a required gate.

For an ordinary fix, inspect failed jobs, repair actionable issues, run local
checks, commit, push, and repeat until the exact current commit passes. Stop at
an unresolved decision or external blocker. Release gates remain stop-only:
a failure aborts the release and requires a separately authorized fix iteration.
Optional unavailable evidence must be reported as unverified; required evidence
fails closed and prevents completion/publication.

## Cache and workflow boundaries

Actions cache transport preserves the existing archive acquisition policy.
Resolve `${CPKT_DEPENDENCY_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps}`
normally. Restored archives still pass digest verification, and a verified
digest hit makes zero dependency-acquisition network requests. Control-plane
calls to push, dispatch, or inspect runs are not archive acquisition. Extracted
sources/install roots remain disposable under `.cache/`; reports/scratch stay
under `build/`. Cache keys should account for pins and relevant platform identity.
Prefer verified archive transport over configured build-tree reuse; optional
caches must not alter correctness. Never cache secrets or private configuration,
and restrict cache writes to the intended trusted context.

New/migrated verification workflows use least privilege (`contents: read` for
source verification), checkout without persisted credentials, and verified
full-SHA action pins with version comments. Do not execute untrusted PR code
in privileged `pull_request_target` jobs. Native source verification needs no
release-asset credential. Preserve existing workflow compatibility;
permission/action migrations need their own verification.

## Release-ref boundary

The development opt-in allows candidate source checks before squash. Keep both
clean local runs prescribed in [release.md](release.md): candidate rehearsal,
then final tagged `make release` on the local release branch. Before the latter
and all local checksum, package, privacy, and artifact gates pass, the release
branch and actual release tag remain local. This includes dispatch ref selection:
never publish them merely to obtain hosted feedback or bypass a trigger filter.

That boundary keeps a failed local release commit/tag repairable in a separately
authorized fix iteration without rewriting remote release history. Preserve
intended work and inspect local/remote refs before any rewind; opt-in itself is
not permission to discard changes or automatically repair a failed release.

Only an actual authorized release with successful tagged local artifact proof
may push the release branch/tag, using the release procedure's explicit refs.
Required final native evidence must identify the final signed/amended commit;
pre-squash candidate evidence cannot satisfy an exact-final-commit requirement.
Once refs have been pushed, a remote failure leaves them visible: stop, report
that state, and leave any release unpublished. No automatic remote rewind.

After required source checks pass, publish only the local checksum-selected
release artifacts under [release.md](release.md). Do not wait for a draft handoff
or launch an additional hosted archive lane as part of the normal lifecycle.
Keep any explicitly requested local-archive execution separate from native
source proof, and never replace the local archives with a native rebuild.

## Local verification when implementing this extension

Use fast policy/command fixtures to reject undeclared workflows, wrong refs,
unintended tag pushes, duplicate dispatch, stale/PR-merge evidence, missing
jobs/tests, failed cleanup, corrupt cache entries, and early release pushes.
Prove a missing remote development branch can be created by normal push and
required evidence cannot silently skip. Keep fixtures under `build/`; lint
workflow YAML when changed. Native behavior still needs real hosted execution
on the exact implementation commit. Verify cold and restored archive-cache
paths when adding cache transport. A local fixture or YAML lint is not native
runtime evidence.

In cpkt, `test-darwin-native` builds and tests all owned runtime suites and
installation combinations in the same native source run;
`test-github-actions-contracts` checks workflow contracts locally. Existing
`test-darwin-sdk` and hosted artifact transport helpers are additional facilities,
not prerequisites for native opt-in or release. Do not invoke them automatically.
The source workflow's diagnostic archives remain distinct from the locally
produced release assets. An offline fixture or an earlier source run does not
supply exact-current-commit native source evidence.

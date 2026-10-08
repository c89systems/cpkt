# Optional hosted native verification

Local commands own build, test, artifacts and publication. GitHub Actions supplies
a selected native host and evidence; it is not a second lifecycle pipeline.

Existing workflow/project policy and explicit engineer opt-in select repository,
remote, branch, workflow, architecture, coverage and whether proof is required.
Availability does not imply opt-in. Keep that declaration in existing configuration.
Check authentication, runner availability and cost constraints; paid use needs direction.

Opt-in permits normal committed development-branch pushes and the declared workflow
trigger. It does not authorize force pushes, settings changes, credentials, release
publication, or early default-branch/tag pushes. Use explicit repository/ref arguments
and disable implicit tag following. A push-triggered run must not also be dispatched.

Track the exact run/attempt and tested commit, workflow, event/ref and required
inventory. Overall success does not waive missing/skipped required jobs/cases.
Queued/cancelled/wrong-commit runs are not proof. Bound waiting and preserve useful
results/diagnostics without secrets or whole build trees.

Use documented Make/CMake/CTest entrypoints in the workflow. Honor configured
parallelism and serialization. Native macOS uses selected Xcode/xcrun tools, SDKROOT
and deployment target for producers and consumers. Host Bash >=4.4 is PATH-selected;
resolve its guard from the declared installed/vendored helper directory. No assumption
that every component checkout contains scripts/require-host-bash.sh.

Pass SDK paths and link flags as actual arguments/environment values, not embedded
shell quoting. Check installed CMake/pkg-config examples and target-correct metadata
before and after relocation. Native tests prove their native build, not execution
of the Linux/osxcross-produced archive bytes. Those native archives are diagnostics.

Cache transport respects verified archive policy: restored hits rehash and make
zero acquisition requests. Source/install trees remain disposable. Least privilege,
unpersistence of checkout credentials and verified action pins apply to new/migrated
workflows; untrusted PR code must not run in a privileged context.

Candidate proof precedes squash. Final exact-commit evidence uses the resulting
commit where required, after local tagged artifact proof. Never push release refs
merely to trigger feedback. Remote failure stops publication and leaves refs visible.
No default draft handoff, artifact relay or replacement of local release bytes.

For a failure, use focused diagnostics/repair/local proof before the next affected
hosted gate. Keep command/policy fixture tests small; offline lint is not native proof.

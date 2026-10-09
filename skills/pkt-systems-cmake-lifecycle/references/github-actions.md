# Optional hosted native diagnostics

Local commands own release build, tests, artifacts and publication. GitHub Actions
supplies optional development diagnostics. Native macOS builds never block release,
belong to its required proof, or supply its artifacts; Darwin release uses osxcross.

Existing workflow/project policy and explicit engineer opt-in select repository,
remote, existing ref, workflow, architecture and diagnostic coverage.
Availability does not imply opt-in. Keep that declaration in existing configuration.
Check authentication, runner availability and cost constraints; paid use needs direction.

Use explicit workflow_dispatch on an already available ref when authorized.
Never push any branch or tag to trigger a build or obtain feedback. Hosted opt-in
does not authorize pushes, settings changes, credentials or publication.
Configure these diagnostic workflows without push or pull-request triggers.
If the selected commit is unavailable remotely, report that limitation; it does
not block the local release. Do not publish refs to work around it.

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

Record which source commit the diagnostic run tested. A missing, queued or failed
macOS run is not a release stop or a reason to push. Do not add a post-push CI wait
to release publication, relay artifacts or replace local release bytes.

For a failure, use focused diagnostics/repair/local proof before the next affected
hosted gate. Keep command/policy fixture tests small; offline lint is not native proof.

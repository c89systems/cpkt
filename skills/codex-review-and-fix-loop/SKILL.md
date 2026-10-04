---
name: codex-review-and-fix-loop
description: Start a Codex goal to repeatedly run a prescribed review against a repository's default branch, commit and fix actionable findings, and stop only at a clean review or a developer decision boundary. Use when a developer asks to review and remediate a branch or working tree.
license: MIT
metadata:
  author: shared
  version: "0.1"
---

# Objective

Turn a Codex review into a safe, evidence-backed remediation goal. Each complete remediation batch is validated and committed with a Conventional Commit before the next review. Continue until the reviewer reports no unresolved issues, unless an architecture/API decision requires the developer to choose a direction.

# Activation Cues

Use this skill when the developer asks to:

- run `codex review` and fix its findings;
- review a branch or working tree against the default branch;
- address actionable review comments while preserving repository invariants;
- re-review a change after fixing review findings.
- explicitly request a whole-codebase review from the first commit on the default branch.

Do not use this skill for an architecture redesign, an API migration, or a performance-optimization project that is not prompted by a review finding.

# Inputs and Outputs

Inputs:

- A Git work tree and the repository's intended default branch.
- The `codex` CLI authenticated and available on `PATH`.
- Repository quality gates and, when applicable, its performance gate.
- Developer direction for any required architecture, refactoring, or public-API decision.

Optional review scope:

- **From-scratch whole-codebase review:** activate only when the developer explicitly asks to review the entire codebase from the first commit on the default branch. Do not infer this scope from an ordinary request to review a branch, run a thorough review, or fix findings.

Outputs:

- Fixes for actionable, in-scope findings, with project invariants preserved.
- Updated tests and affected documentation, public-API comments, and facade implementations.
- A recorded, scoped exception for each intentional out-of-scope finding.
- One or more Conventional Commits containing only the remediation batches owned by this loop.
- A clean final review or a clearly reported developer decision boundary, plus a closeout report listing commits, findings, review scope and baseline, every validation command run, and pre-existing/concurrent working-tree changes.

# Preconditions and Scope

1. Read repository instructions and identify the branch under review. Determine the canonical default-branch name from `refs/remotes/origin/HEAD`, removing the `origin/` prefix; if unavailable, use the repository's documented default, then a local `main` or `master` fallback. The result must be a branch name such as `main`, `trunk`, or `master`. If it remains ambiguous, ask the developer before running the review. Require a matching local branch at `refs/heads/<default-branch>`; if it is absent, pause and ask the developer rather than fetching or substituting a remote-tracking ref. Record `git rev-parse <default-branch>` as the expected default-branch tip; this pins the review scope without passing a SHA to `--base`.
2. Determine the checked-out branch with `git symbolic-ref --quiet --short HEAD` and record it as the topic branch for this loop. Record `git rev-parse HEAD` as the expected `HEAD` commit. A non-zero symbolic-ref result means `HEAD` is detached: stop dependent review work and ask the developer to check out or name the intended topic branch. If `HEAD` is on the resolved default branch, also stop dependent review work and ask the developer to switch to or name the intended topic branch. For normal branch-review scope, require `git merge-base <expected-default-branch-tip> <expected-HEAD>` to return a merge base. If it does not, pause and ask for the intended baseline rather than reviewing unrelated histories. Do not require the current default-branch tip to be an ancestor: a valid topic branch can predate newer default-branch commits. Do not run review or create remediation commits while `HEAD` is detached or on the default branch unless the developer explicitly directs it.
3. Capture a preflight snapshot with `git status --porcelain=v1 -uall`, recording pre-existing staged paths separately, and inspect the diff against the default branch. Establish and track which files the loop owns before editing. Preserve pre-existing or unrelated working-tree changes; do not stage them. If an owned fix overlaps another developer's uncommitted or staged work, ask before staging or committing that file.
4. Discover the relevant build, test, lint, documentation, supported-platform, and performance instructions from repository documentation and CI configuration. Read any existing review-exception records before triage.
5. Invocation authorizes local commits for remediation batches created by this loop. Do not push, open a pull request, alter CI configuration, rewrite history, or change supported-platform policy unless the developer explicitly asks.

# Goal Lifecycle

Before the first review, start Codex Goal mode when the current Codex client/session supports it. In an interactive Codex CLI session, use `/goal`; use the equivalent goal mechanism in other Codex clients. Set one goal for this repository and branch with this completion contract:

```text
Review and remediate this topic branch in the selected review scope. Use the prescribed default-branch review unless a from-scratch whole-codebase review was explicitly requested. Fix every reported issue that is actionable and in scope, run all applicable validation and performance gates, and commit each completed remediation batch with a specific Conventional Commit before reviewing again. Finish only when the selected review reports no issues. Stop dependent work and request direction if HEAD is detached, on the default branch, or no longer equals the expected commit; if the default-branch tip changes; before an architecture, public-API, compatibility, data-format, or supported-platform decision; when a documented exception recurs unchanged after re-review; or when a non-actionable finding cannot be resolved by an in-scope remediation commit.
```

Do not replace an active unrelated goal. If the active goal already includes this review, run the loop within that goal instead of creating another one. A clean review completes only the review stage; mark a broader goal complete only when its entire objective is achieved. Keep the goal in the same session while the loop runs. At a developer decision boundary, request the required direction and stop dependent work; keep the goal active while input is pending. Set the goal to paused only at the developer's explicit request, and apply the client's blocked-status rule when a blocker persists. Goal mode does not grant broader access or change approval requirements.

If Goal mode is not available in the client/session, run this same procedure as a normal task. Its absence is not a blocker and must not weaken the completion contract.

# Review Execution

Run every `codex review` through the coding harness's managed terminal/session facility, not a `pty: true` execution. A review can run much longer than a normal command wait, so launch it once and follow the returned terminal session with the harness's wait/poll mechanism. Do not start a second review while waiting for the first, and do not use global process-table commands to infer its status.

Capture both output streams in a unique temporary file, retain that file until the findings have been triaged, and inspect it after the terminal session completes. For example:

```sh
review_log="$(mktemp "${TMPDIR:-/tmp}/codex-review.XXXXXX.log")"
if codex review -c model=gpt-6.1-sol -c model_reasoning_effort=high --base <default-branch> >"$review_log" 2>&1; then
  review_exit=0
else
  review_exit=$?
fi
```

When the harness reports that the terminal is still running, wait on that same session in intervals no longer than 60 seconds. Once it finishes, read `"$review_log"`, preserve it while reporting any failure, and remove it only after its contents are no longer needed. Never silently substitute a different model or reasoning effort.

Always triage the completed log before acting on `review_exit`. A non-zero exit with a complete, parseable reviewer report still requires normal finding triage. Treat the review as a tool failure only when the log lacks a complete reviewer result or shows an execution, authentication, or base-branch error.

For the normal branch-review scope, run the prescribed command without a positional prompt. The CLI does not allow a positional custom review prompt together with `--base`.

For the explicit from-scratch whole-codebase scope, first require a complete local history:

```sh
git rev-parse --is-shallow-repository
```

Continue only when it prints `false`. Then find the default branch's first commit with:

```sh
first_commit=$(git rev-list --max-parents=0 <default-branch>)
```

Require exactly one result. If the repository is shallow, the default branch is unavailable locally, or there are multiple root commits, pause and ask the developer to provide the intended baseline. Do not infer or fetch a baseline without direction.

Use the same model and reasoning configuration, omit `--base`, and pass only this positional custom review prompt within the same temporary-output capture wrapper:

```sh
codex review -c model=gpt-6.1-sol -c model_reasoning_effort=high \
  "changes against $first_commit" \
  >"$review_log" 2>&1
```

Use `--base` only for normal branch-review scope, passing the canonical branch name such as `main`, `trunk`, or `master`. In from-scratch scope, do not pass `--base`; the first-commit SHA belongs only in the `changes against <SHA>` prompt.

Record the selected scope with every review log and in closeout: `normal` plus the default branch name, or `from-scratch` plus the default branch name and `first_commit` SHA.

# Procedure

1. Immediately before every review, confirm `HEAD` is attached to the recorded topic branch, is not on the default branch, and that `git rev-parse HEAD` equals the expected `HEAD` commit. Also confirm `git rev-parse <default-branch>` equals the recorded default-branch tip. If either commit differs, another process may have changed the review scope: stop dependent review work and ask the developer how to proceed. Run the required review from the repository root with the execution method above. For normal scope, use the prescribed command exactly, passing only the canonical branch name—such as `main`, `trunk`, or `master`—to `--base`; never substitute a SHA, `origin/<branch>`, or another commit ref. For from-scratch scope, which must be explicitly requested, omit `--base`, resolve the default branch's first commit, and use only the `changes against <SHA>` prompt specified above. If the CLI, authentication, or required branch information is unavailable, report the blocker and the command/error; do not claim a review occurred.

2. Triage each finding against the code, tests, repository invariants, and declared support policy. Treat a finding as actionable only when there is evidence of a defect, security issue, correctness/reliability risk, broken contract, or other behavior the project intends to support. Do not make speculative, stylistic, or unnecessary changes merely to satisfy the reviewer. For a non-actionable finding that is not an intentional out-of-scope behavior eligible for step 5, preserve the triage evidence and stop dependent review work and request developer direction. Do not re-run review without a remediation commit merely to see whether the same report recurs.

3. For each actionable in-scope finding:

   - Make the smallest coherent fix that preserves established invariants and compatibility.
   - Add or update a focused regression test when the project has an appropriate test layer.
   - Keep affected documentation, public API comments, examples, and facade/adapter implementations consistent with the changed behavior. Inspect both the implementation and every public or facade surface that represents it.
   - Run the focused validation for the changed area before broad gates.

4. Stop before implementing a finding when resolving it would require any of the following:

   - an architecture change or substantial refactor;
   - a public API, compatibility, data-format, or supported-platform policy change;
   - a trade-off among materially different correct implementations.

   Explain the finding, evidence, affected invariants, and realistic options to the developer. Recommend the approach that best serves the repository's stated intent and invariants when it is materially better. Do not implement a workaround solely to preserve the current shape unless the developer chooses that direction.

5. Handle intentional out-of-scope findings as exceptions, not as fixes. Verify the behavior is genuinely intentional and that the finding does not affect a declared platform, contract, or release target. Record a concise, discoverable exception in the project's existing review/policy/support documentation; if none exists, create `docs/review-exceptions.md`. Include the finding, affected scope, rationale, owner or policy source when known, and the condition that would require reconsideration. Future invocations must search for and read these exceptions during preflight.

   Commit the exception as its remediation batch and re-run review once. If that re-review reports the same exception with materially the same affected scope and rationale, do not edit code or documentation merely to force a different result. This is a decision boundary: stop dependent review work, report both review outputs and the exception record, and ask the developer whether to change the policy, accept a different implementation, or adjust the review guidance.

6. Assess performance before and after each change that can affect a known hot path, such as an inner loop, allocation-heavy path, serialization/deserialization path, high-frequency request path, query path, or startup-critical path. If the build or CI system defines a relevant performance gate, run it unless the developer specifically declines performance execution. For a repeatable local gate, collect a comparable pre-change baseline when practical.

   - If the gate regresses, investigate the measured cause and reclaim the lost performance before closeout.
   - When a code smell in the hot path causes avoidable work, optimize the hot path itself: reduce repeated work, allocations, indirection, synchronization, I/O, or algorithmic cost as evidence supports.
   - Do not present caching as the default performance remedy. Use caching only when it demonstrably addresses the measured cause and its invalidation, memory, and correctness costs fit repository invariants.
   - If no relevant performance gate exists, state that fact and run the strongest available focused correctness gate; do not invent a benchmark or claim performance was validated.

7. Run the repository's required quality gates, starting with targeted tests and then the normal broader build/test/lint/documentation gates affected by the changes. Treat a failure as a blocker unless it is demonstrably pre-existing; record evidence for any pre-existing failure.

8. Commit the completed remediation batch before another review can run:

   - Confirm `HEAD` is still attached to the recorded topic branch and that `git rev-parse HEAD` still equals the expected `HEAD` commit. If either changed, stop dependent review work; do not commit on the new branch or atop an unknown commit.
   - Inspect the staged diff and stage only files owned by the batch; never use a blanket stage operation that could include unrelated work.
   - If unrelated paths were already staged at preflight or have been staged concurrently, preserve their index state. Do not use a plain `git commit`, which would include them. Commit only the loop-owned paths with `git commit --only -- <owned-paths>` and the selected Conventional Commit message. If the batch cannot be represented safely by explicit paths, pause for developer direction.
   - Run `git diff --check --cached -- <owned-paths>` and `git diff --check -- <owned-paths>`, then review the staged diff for the owned paths before committing. Do not let unrelated pre-existing changes determine this batch's result. For every owned path, also verify that `git diff -- <owned-paths>` is empty, so the working tree still matches the reviewed index. If it does not, re-triage and validate the changed content before staging; do not let `git commit --only -- <owned-paths>` commit an unreviewed working-tree update.
   - Create a Conventional Commit subject that accurately describes the batch, such as `fix(parser): reject truncated frames` or `docs(platform): record Windows support exception`. Use an appropriate Conventional Commit type and an optional concise scope. Do not use a generic message such as `fix review findings`.
   - Include a commit body when it helps identify the reviewer finding, invariant, compatibility decision, or validation evidence.
   - After a path-limited commit, compare the current index with the preflight staged-path record. Confirm unrelated staged paths remain staged and unaltered; report any concurrent difference without staging or committing it.
   - Only after the loop-owned commit succeeds, update the expected `HEAD` commit with `git rev-parse HEAD`.
   - Before another review, capture `git status --porcelain=v1 -uall` and reconcile it with the preflight snapshot and loop-owned file list. If a hook or tool left a loop-owned staged or unstaged change, validate it as a new remediation batch and commit it before reviewing again. Preserve clearly unrelated pre-existing changes. If a new change is concurrent, overlaps another developer's work, or its ownership is unclear, stop dependent review work rather than reviewing it.
   - If Git refuses the commit, the staged scope is unclear, or validation is failing, stop and resolve the blocker; do not re-run review against uncommitted loop changes.

9. Re-run the required Codex review only after the remediation batch commit succeeds. Repeat triage, fix, validation, performance assessment, documentation synchronization, and commit steps for every reported issue. Finish only when the reviewer reports no issues. Apply the recurring-exception guard in step 5 rather than treating a repeated report as clean or looping without new information. At an architecture/API decision boundary, stop dependent review work before requesting developer direction; resume only after that direction arrives.

10. After a clean review and before completing the goal, run `git status --porcelain=v1 -uall` again and reconcile it with the preflight snapshot and the loop-owned file list:

   - A loop-owned file with staged or unstaged changes blocks completion. Validate and commit it as the appropriate remediation batch, then return to step 9 and run the selected review scope again. Pause for developer direction instead if its scope is unclear.
   - Preserve every pre-existing or concurrent unrelated change. Do not stage it. List it separately in closeout so it is not mistaken for loop output.
   - Do not claim a clean working tree when unrelated changes remain; claim only that no loop-owned changes remain uncommitted.

# Failure Modes and Abort Conditions

- Default branch cannot be determined or has no matching local branch: ask the developer; do not guess, fetch, substitute a SHA, or pass a remote-tracking ref to `--base`.
- `HEAD` is detached: stop dependent review work and ask the developer for the intended topic branch. Do not review or commit until directed.
- `HEAD` is on the default branch: stop dependent review work and ask the developer for the intended topic branch. Do not review or commit until directed.
- The checked-out branch changed from the recorded topic branch: stop dependent review work and ask the developer whether to resume on the original branch or adopt the new branch. Do not review or commit until directed.
- `HEAD` no longer equals the expected commit: stop dependent review work and report the expected and observed commits. Another process may have committed, rebased, or reset the topic branch; do not review or commit until the developer decides whether to adopt that history.
- The default-branch tip no longer equals the recorded commit: stop dependent review work and report the expected and observed commits. Do not continue with a silently changed review baseline or replace `--base` with a SHA; ask the developer whether to restart against the new baseline.
- The recorded default-branch tip and expected topic `HEAD` have no merge base in normal scope: pause and ask the developer for the intended baseline. Do not review unrelated histories with `--base`.
- From-scratch scope is requested in a shallow repository, the default branch is unavailable locally, or it has multiple root commits: pause and ask the developer for the intended baseline. Do not infer or fetch one automatically.
- `codex review` cannot run or its log lacks a complete reviewer result: report the exact failure and preserve the work tree. A non-zero exit with a complete report is triaged normally.
- Finding conflicts with a documented invariant or policy: verify the policy and record/maintain an exception rather than violating it.
- A finding is non-actionable but is not an intentional out-of-scope behavior eligible for an exception: stop dependent review work with the triage evidence and request developer direction. Do not re-run review without a remediation commit.
- A committed exception is reported again with materially unchanged scope after one re-review: stop dependent review work and request a developer decision; do not continue the review loop automatically.
- Required architecture/API decision: stop dependent review work at the decision boundary and request developer direction with a recommendation. Do not resume it automatically.
- Quality or performance gate fails: do not declare the loop complete until the regression is fixed, the failure is shown to pre-date the work, or the developer accepts a documented exception.
- A remediation batch cannot be committed: do not re-run review; report the staging, validation, identity, hook, or Git error that prevents the required commit boundary.
- An owned path differs between the reviewed index and working tree immediately before commit: re-triage and validate it before staging, or pause if its ownership is unclear. Do not commit unreviewed working-tree content with `git commit --only`.
- Unrelated staged paths would be included in a remediation commit or the batch cannot be committed safely with explicit paths: preserve the index and pause for developer direction.
- Post-commit reconciliation finds a new change with unclear ownership or overlapping concurrent work: preserve it and stop dependent review work. Do not start another review that could incorporate it.
- Final working-tree reconciliation finds loop-owned staged or unstaged changes: do not complete the goal until they are committed and followed by another clean review, or the developer directs otherwise.
- No findings: no commit is needed; report the clean review and validations performed.

# Safety and Side Effects

- This skill reads repository history and configuration, runs local review/build/test commands, edits source/tests/documentation, and creates local Git commits for its own completed remediation batches.
- Edits and commits are non-idempotent; make only scoped, review-driven changes and preserve unrelated work. Do not amend, squash, reset, or otherwise rewrite existing commits.
- Network access, credentials, the `codex` CLI, and CI permissions are not assumed. Request the necessary authorization before using a remote or privileged performance/CI gate.
- File removal is limited to loop-owned remediation and cleanup explicitly required by this procedure. Preserve unrelated files; this skill alone does not authorize resetting Git state, rewriting history or publishing changes.

# Verification

Completion requires all applicable conditions below:

1. Every review used the prescribed model/reasoning configuration, ran through a managed terminal session, and was captured to a temporary output file for triage. Closeout records the selected scope and its baseline: default branch name plus its recorded tip for normal scope, or default branch name plus root-commit SHA for from-scratch scope.
2. The final review in the selected scope reports no issues, unless work is paused at an architecture/API, non-actionable-finding, or recurring-exception decision boundary and the developer has been given a concrete recommendation.
3. Every remediation batch, including loop-owned hook or tool output found after a commit, is validated and committed before the next review with a specific Conventional Commit message. The reviewed index matched the owned working-tree paths at commit time. No unrelated staged path was included or altered; path-limited commits were used when necessary.
4. Each fix has proportionate focused validation; required broader quality gates pass, or pre-existing failures are evidenced and reported.
5. Changed behavior agrees across implementation, tests, documentation, public API comments, and facades/adapters.
6. Every hot-path-affecting change has passed the relevant performance gate unless the developer declined it; any measured regression is resolved or explicitly accepted by the developer.
7. Intentional exceptions are discoverable, scoped, committed, and available to the next review.
8. Final working-tree reconciliation confirms no loop-owned change remains staged or unstaged; any reconciliation commit was followed by another clean review. Pre-existing or concurrent unrelated changes are preserved and reported separately.
9. Every review and remediation commit ran from the recorded attached topic branch at the expected `HEAD` commit, against the recorded default-branch tip, unless the developer explicitly authorized a different branch state. The expected topic-branch commit advanced only after a successful loop-owned remediation commit.

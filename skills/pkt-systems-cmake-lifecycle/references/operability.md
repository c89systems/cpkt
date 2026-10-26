# Commands, layout and ownership

Make is the command index. Keep help accurate and recipes short; procedural work
belongs in readable Bash. CMake owns the graph and CTest owns tests. Use project
namespaces for project options and preserve native dependency target names.

Add only directories and targets the project needs. Typical surfaces are build,
test, format-check, package, package-verify, prerelease, release and clean.
Optional Lua, fuzz, benchmark, e2e and hosted targets need declared requirements.
A pipeline migration does not authorize product/API redesign.

Keep logs, extraction and fixture scratch under ignored `build/`; final artifacts
belong in `dist/`. Dependencies use disposable local state and shared verified
archives under [dependency policy](dependencies.md). Resolve an explicit owning
root, not the caller's working directory. Reject unsafe cleanup roots and output
redirection; remove only owned generated paths. Never clean borrowed prerequisites
or the declared global archive/toolchain cache. Full `make clean` removes `build/`,
`dist/` and local `.cache/` completely, including hidden contents and local tools.
Checkouts/worktrees are isolated; another checkout's generated state is not a
shared cache. Scoped cleanup preserves unrelated generated state.
Normal build/test entrypoints never depend on clean.

For projects using clangd, the normal native build prepares the real dependency
and generated headers and publishes its compilation database automatically. Keep
one documented database location matching `.clangd` and every producer of editor
metadata. Use the owning targets' language, defines, include paths and toolchain
context from this checkout; another checkout's database or build tree is not an
editor prerequisite. Full clean still removes generated editor state. Document
the native build command that restores it; do not introduce a separate SDK or
editor bootstrap solely for clangd.

Bash helpers preserve argv, check command status, and use ordinary foreground
execution. The caller owns whole-job cancellation under SKILL.md. Do not add a
second dispatcher or process manager beneath every Make target.

Use configured target tools, then matching compiler siblings, and PATH only
where target-correct. Linux compiler/binutils/libc come from the pinned collection;
Darwin inspection uses the matching Apple/osxcross tools. One small discovery
surface is enough; no duplicated resolver layers. Missing required tools fail.

Report the failed phase, command, reason and useful log/artifact path. Keep
transient diagnostics untracked. Use existing timings to investigate repeated
work; do not create a logging or telemetry framework.

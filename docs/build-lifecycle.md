# Build lifecycle

The repository follows [the lifecycle skill](../skills/pkt-systems-cmake-lifecycle/SKILL.md).
Make exposes commands, Bash sequences them, CMake owns build and install dependencies,
and CTest executes tests. One job owns each mutable workspace.

Plain `make build` and `make test` select native Debug. Use `PRESET=<preset>`
for a lane, or explicit `make build-release` / `make test-cross` for the Linux matrix.
The full package inventory contains seven SDK targets and the Darwin smoke ZIP.

Preparation is incremental. One Release dependency producer per target is shared
by Debug, Release and native hardening consumers. Recipes live in
`cmake/dependencies/`; their declared settings, patches and native ExternalProject
stamps determine when work changes. A borrowed stale or missing prerequisite fails
with its preparation requirement.
`make deps DEPENDENCY=<name>` prepares only that component and its prerequisite
closure; the next ordinary build restores the complete producer graph.

Whole host and runtime CTest scopes have native CMake output stamps. An unchanged
scope is reused. To investigate a failure, run the affected test directly:

```sh
bash scripts/build.sh test --preset debug --regex '^test_name$'
```

Fix and prove that case before rerunning its broader scope. Test changes should
invalidate their checks, while ordinary CMake dependencies determine compilation.
Use `CPKT_TEST_RERUN=1 make test` only when deliberately forcing a whole rerun.

Memory checks use the existing Debug binaries and CTest Memcheck. Fuzzing uses
the declared AFL++ configuration and bounded smoke, standard or opt-in long runs.
Host workflow fixtures run in native Debug; ABI and runtime checks use their
required target configurations.

`make format` remains part of development. `make clangd-surface` checks the editor
database, declared source examples and public function documentation.
Formatting stays global even when other work is selected.

Python is confined to CMake-declared source/data generators and small data servers.
It does not drive tests or make lifecycle decisions. Compiled tests and native
CMake/Bash tests own assertions. API inventory generators emit facts; CTest
asserts coverage. There are no receipts, process brokers or alternative schedulers.

Selected packaging uses an explicit Release preset:

```sh
make package GROUP=core PRESET=x86_64-linux-gnu-release
make package-checksums GROUP=core PRESET=x86_64-linux-gnu-release
make package-verify GROUP=core PRESET=x86_64-linux-gnu-release
```

Selected archives remain under `build/package-stage/`. Aggregate binary artifacts
use `dist/` and a binary checksum manifest under `build/package-manifests/`.
The complete release checksum manifest also includes the source archive.
Native package checks validate final layout, manifest hashes and identity,
architecture, runtime paths, privacy, and installed CMake/pkg-config consumers.
The Darwin smoke ZIP has a flat regular-file layout, fixed modes and timestamps;
its native check binds the packaged libraries to the verified SDK and checks
the executable architecture, deployment floor and relative loader paths.

`make prerelease` prepares incrementally. Final `make release` requires a clean
committed checkout with a verified signed commit and one exact lightweight release tag. It checks identity
before cleanup, packages the tracked source, and builds the final matrix in one
fresh extracted workspace. Each required configuration is built once within
that run. The source reconstruction shares those same producers and checks.
Preparation reuses a passed reconstruction of an unchanged source archive.
Publishing identical archive or checksum bytes preserves the existing file and
timestamp, so an unchanged artifact does not invalidate downstream native checks.
A failed release stops for a separately authorized repair; it does not retag,
retry the release or publish.

Sources, tools and configuration stay stable during a check. Restored timestamps
or changed tool collections require explicit invalidation. The terminal or job
runner cancels the whole process group; signalling only a wrapper is unsupported.
Full project cleanup removes build/, dist/ and local .cache/ entirely, including
hidden contents and locally generated tools. Only the declared global cpkt cache
and installed workstation prerequisites are shared. Checkouts/worktrees never
borrow one another's sources or generated state. External publication requires
explicit authorization.

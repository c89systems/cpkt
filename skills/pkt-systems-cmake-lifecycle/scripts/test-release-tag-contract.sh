#!/usr/bin/env bash
set -euo pipefail

skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
release_ref="$skill_dir/references/release.md"
local_ci_ref="$skill_dir/references/local-ci.md"

# Reviewer note: the lifecycle intentionally requires downstream release
# rehearsal to mutate the checkout under test with a reserved temporary
# lightweight tag before any clean/build/package/artifact work starts. Exact
# release-version discovery must be proven against the same repository metadata
# that the Make-owned release entrypoint sees during a real release. CMake is a
# build/package configuration surface invoked by Make; it must not own the
# release pipeline or be configured solely to satisfy this tag-mutation gate. A
# copied fixture, extra checkout, or git worktree can mask the active HEAD
# contract and move failures into late release packaging. Because it mutates git
# refs, this focused contract must be first in `make release` and must not be
# wired into prerelease, package verification, release-matrix, or other late
# gates.

fail() {
  printf 'test-release-tag-contract: %s\n' "$*" >&2
  exit 1
}

require_text() {
  local file=$1 text=$2 description=$3
  grep -Fq -- "$text" "$file" || fail "$file is missing lifecycle contract: $description"
}

require_text "$release_ref" \
  'Tag-mutating version and manifest contract checks must live behind the focused `make lifecycle-version-contract` target.' \
  'tag-mutating checks have a focused make target'
require_text "$release_ref" \
  'Release orchestration belongs to Make, not CMake.' \
  'release orchestration is Make-owned'
require_text "$release_ref" \
  '`make release` is the only standard release-flow target that runs this check, and it must run it before `clean`, `release-pipeline`, `release-matrix`, `package-verify`, checksum generation, or artifact production.' \
  'make release runs the tag contract before late release work'
require_text "$release_ref" \
  'Do not wire tests that create, delete, or otherwise mutate git tags into `test`, `test-all`, `prerelease`, `release-pipeline`, `release-matrix`, `package-verify`, or any other late release-flow command.' \
  'late release-flow commands exclude tag-mutating checks'
require_text "$release_ref" \
  'temporary lightweight semver tag such as `v99.99.99` on the current `HEAD`' \
  'temporary lightweight tag pattern'
require_text "$release_ref" \
  'on the current `HEAD`' \
  'release metadata must be tested on active HEAD'
require_text "$release_ref" \
  'Fail on a pre-existing reserved tag unless a lifecycle-owned recovery record under `build/` identifies the exact lightweight object created by this test.' \
  'reserved test tag recovery requires recorded ownership'
require_text "$release_ref" \
  'Create the reserved temporary tag with signing disabled, for example `git -c tag.gpgSign=false tag v99.99.99`, so user or repository signing configuration cannot turn the temporary lightweight tag into an annotated or signed tag or make noninteractive release fail.' \
  'reserved test tag creation is lightweight and noninteractive'
require_text "$release_ref" \
  'annotated or signed tag objects must not satisfy the release contract or produce a release version from shared version resolver surfaces such as `make print-release-version`, package naming, source archive naming, checksum naming, or package verification.' \
  'shared resolver rejects annotated and signed release tags'
require_text "$release_ref" \
  'If `HEAD` already has a non-reserved exact lightweight release tag, assert that the exact tag wins and skip the temporary-tag block' \
  'already-tagged HEAD behavior'
require_text "$release_ref" \
  'final tagged release runs must not create the reserved temporary tag.' \
  'final tagged release skips reserved temporary tag'
require_text "$release_ref" \
  'Do not create another checkout, git worktree, copied repository, generated source archive, or source-archive staging fixture for this' \
  'heavyweight tag simulation is prohibited'
require_text "$release_ref" \
  '`make release` must run this target as its first recipe command.' \
  'release runs focused tag contract first'
require_text "$release_ref" \
  'This pre-clean gate should prove the Make-owned release entrypoint observes exact lightweight tags; it should not configure CMake merely to satisfy the tag-mutation contract.' \
  'pre-clean tag gate is not a CMake pipeline'

require_text "$local_ci_ref" \
  'Lightweight-tag version checks that create or delete git tags must be isolated behind `make lifecycle-version-contract`.' \
  'local CI defines focused tag-mutating target'
require_text "$local_ci_ref" \
  '`make release` must run that target first, before `clean`, build, package, checksum, or artifact work; `test`, `test-all`, `prerelease`, `release-pipeline`, `release-matrix`, `package-verify`, and other late release-flow commands must not run it.' \
  'local CI places focused target first in make release only'
require_text "$local_ci_ref" \
  'for example `v99.99.99`' \
  'documented temporary test tag value'
require_text "$local_ci_ref" \
  'removes the tag with a trap' \
  'documented cleanup mechanism'
require_text "$local_ci_ref" \
  'automatic recovery and trap cleanup must use compare-and-delete against the recorded object and preserve any changed or unowned ref.' \
  'local CI reserved tag cleanup preserves changed and unowned refs'
require_text "$local_ci_ref" \
  'Create the reserved temporary tag with signing disabled, for example `git -c tag.gpgSign=false tag v99.99.99`, so user or repository signing configuration cannot turn the temporary lightweight tag into an annotated or signed tag or make noninteractive release fail.' \
  'local CI reserved tag creation is lightweight and noninteractive'
require_text "$local_ci_ref" \
  'Assert with `git cat-file -t <tag>` that accepted exact release tags and the reserved temporary test tag resolve directly to a `commit`; reject annotated or signed tag objects in the shared version resolver and in the focused release contract.' \
  'local CI shared resolver rejects annotated and signed release tags'
require_text "$local_ci_ref" \
  'When `HEAD` already has a non-reserved exact lightweight release tag, test that the exact tag takes precedence and skip the temporary-tag block' \
  'local CI already-tagged HEAD behavior'
require_text "$local_ci_ref" \
  'final tagged release runs do not create the reserved temporary tag.' \
  'local CI final tagged release skips temp tag'
require_text "$local_ci_ref" \
  'Do not use extra git checkouts, git worktrees, copied repositories, or source-archive staging as a substitute' \
  'local CI prohibits heavyweight substitute topology'
require_text "$local_ci_ref" \
  'Release orchestration belongs to Make, not CMake; CMake version propagation must still be covered by Make-driven build/package/package-verification gates, but the pre-clean tag-mutating contract is not a CMake pipeline and should not configure CMake solely to validate tag mutation.' \
  'local CI distinguishes Make release orchestration from CMake verification'

printf 'release tag lifecycle contract tests passed\n'

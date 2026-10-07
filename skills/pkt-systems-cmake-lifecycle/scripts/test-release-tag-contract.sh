#!/usr/bin/env bash
set -euo pipefail

skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
release_ref="$skill_dir/references/release.md"
local_ci_ref="$skill_dir/references/local-ci.md"
operability_ref="$skill_dir/references/operability.md"

# Validate canonical policy and reference routing without mutating Git refs.
# These are wording guards, not proof of agent decisions. Also review the decision
# cases and execute the isolated native example in references/native-test-reuse.md.

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
  'Make exposes the release targets with simple prerequisites and short recipes. Readable Bash scripts own procedural sequencing, validation and cleanup; CMake supplies build, test, install and package operations.' \
  'Make exposes targets, Bash sequences release work, and CMake owns build operations'
require_text "$release_ref" \
  '`make release` is the only standard release-flow target that runs this check, and it must run it before `clean`, `release-pipeline`, `release-matrix`, `package-verify`, checksum generation, or artifact production.' \
  'make release runs the tag contract before late release work'
require_text "$release_ref" \
  'Do not wire tests that create, delete, or otherwise mutate git tags into `test`, `test-all`, `prerelease`, `release-pipeline`, `release-matrix`, `package-verify`, or any other late release-flow command.' \
  'late release-flow commands exclude tag-mutating checks'
require_text "$release_ref" '## Temporary test-tag ownership' 'canonical ownership section'
require_text "$release_ref" 'the current `HEAD`' 'active-checkout version discovery'
require_text "$release_ref" 'this focused tag test does not configure CMake.' 'focused pre-clean scope'
require_text "$release_ref" 'git -c tag.gpgSign=false tag v99.99.99' 'unsigned lightweight test tag'
require_text "$release_ref" 'reject annotated or signed tag objects.' 'lightweight-only version tags'
require_text "$release_ref" 'skip temporary-tag creation.' 'already-tagged HEAD behavior'
require_text "$release_ref" 'nonce-bearing intent before exclusive creation' 'interrupted-creation recovery'
require_text "$release_ref" 'Authenticate the zero-to-object Git reflog entry' 'creation authentication'
require_text "$release_ref" 'intent alone does not prove tag ownership.' 'intent cannot authorize deletion'
require_text "$release_ref" 'Fail on an unowned pre-existing tag' 'foreign-tag refusal'
require_text "$release_ref" 'Automatic recovery and trap cleanup use compare-and-delete against the recorded object' 'owned-object deletion'
require_text "$release_ref" 'preserving changed or unowned refs.' 'changed-ref preservation'
require_text "$release_ref" 'cleanup restores the original version.' 'version restoration'
require_text "$release_ref" 'tag removal and branch rewind follow [failed local release recovery]' 'separate real-release approval'

for reference in "$local_ci_ref" "$operability_ref"; do
  require_text "$reference" \
    '[temporary test-tag ownership](release.md#temporary-test-tag-ownership)' \
    'routing to canonical ownership policy'
done

printf 'release tag policy wording guards passed\n'
